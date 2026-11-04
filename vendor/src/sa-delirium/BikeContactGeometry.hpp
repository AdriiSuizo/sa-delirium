#pragma once
#include "delirium_physics.h"
#include "VehicleGeometry.hpp"
#include "BikeComponentFrames.hpp"
#include "../plugin-sdk-sa/plugin_sa/game_sa/rw/rwcore.h"
#include "../plugin-sdk-sa/plugin_sa/game_sa/rw/rpworld.h"
#include <memory>
#include <string>
#include <cstring>
#include <span>

// QPC accounting is observational: no geometry, clocks or contact result is
// changed. Keep this cost in the caller's full frame time as well as reporting
// its breakdown, so expensive diagnostics cannot masquerade as solver time.
inline double bikeGeometryClockMs(){
    static const double frequency=[](){LARGE_INTEGER v{};QueryPerformanceFrequency(&v);return double(v.QuadPart);}();
    LARGE_INTEGER v{};QueryPerformanceCounter(&v);return frequency>0?double(v.QuadPart)*1000.0/frequency:0;
}
struct BikeGeometryTimedScope {
    double&elapsed;double started=bikeGeometryClockMs();
    ~BikeGeometryTimedScope(){elapsed+=bikeGeometryClockMs()-started;}
};
struct BikeGeometryCost {
    double buildMs=0,shapeLookupAndRegistrationMs=0,sdkRegistrationMs=0;
    unsigned cacheHits=0,sdkRegistrations=0;
};

// Exact installed RpGeometry, partitioned at the actual native articulated
// nodes. Native SA collision remains unchanged. No enclosing convex hull or
// disappearance of wheels is permitted when replacing the NM contact proxy.
struct BikeContactComponent {
    int node=0,parent=0;M bridge{};V centre{},half{};VehicleShape shape;
};
struct BikeContactGeometry {
    int model=-1,ref=-1;void*clump=nullptr;bool bicycle=false,valid=false;
    std::array<RwFrame*,10> nodes{};std::vector<BikeContactComponent> components;
    std::string reason;unsigned sourceTriangles=0,degenerateTriangles=0,excludedLodTriangles=0;
    BikeGeometryCost cost;
};
struct BikeContactFrame {
    const BikeContactComponent*component=nullptr;T frame{},relative{};M nativeLocal{};V velocity{},spin{};
};
struct BikeContactSnapshot {
    int vehicle=-1;unsigned frame=0;std::vector<BikeContactFrame> components;
    float nativeMatrixError=0,nativeWorldError=0;bool matched=false,motionVerified=false,renderedNative=false;
    std::string failure;
};
static bool bikeContactStageEnabled=false;
// The SDK owns registered shapes for its lifetime. Reuse exact installed
// component data across spawned clumps; otherwise traffic respawns would leak
// another full model into that registry. Hashes only select candidates: every
// primitive must still match before an existing handle is reused.
inline int registerBikeContactShape(const std::vector<VehiclePrimitive>&primitives,BikeGeometryCost*cost=nullptr){
    BikeGeometryCost ignored;auto&measured=cost?*cost:ignored;
    BikeGeometryTimedScope total{measured.shapeLookupAndRegistrationMs};
    static std::map<uint64_t,std::vector<VehicleShape>> cache;
    uint64_t hash=14695981039346656037ull;
    for(const auto&p:primitives)for(unsigned char b:std::span(reinterpret_cast<const unsigned char*>(p.data()),sizeof(p))){hash^=b;hash*=1099511628211ull;}
    auto&bucket=cache[hash];for(const auto&shape:bucket)if(shape.primitives==primitives){measured.cacheHits++;return shape.id;}
    int id=-1;{BikeGeometryTimedScope registration{measured.sdkRegistrationMs};measured.sdkRegistrations++;
        id=nm_vehicle_shape_register(primitives.front().data(),int(primitives.size()));}
    if(id>=0)bucket.push_back({id,primitives});return id;
}
inline M bikeIdentityMatrix(){M m{};m.right={1,0,0};m.up={0,1,0};m.at={0,0,1};return m;}
inline RwFrame* bikeFrameParent(RwFrame*f){return f?static_cast<RwFrame*>(f->object.parent):nullptr;}
inline const M&bikeFrameLocal(RwFrame*f){return *reinterpret_cast<const M*>(&f->modelling);}
inline std::string bikeFrameName(RwFrame*f){const char*s=reinterpret_cast<char*(__cdecl*)(RwFrame*)>(0x72FB30)(f);return s?std::string(s,strnlen(s,32)):std::string{};}
inline int bikeNativeNode(const BikeContactGeometry&g,RwFrame*f){for(int i=1;i<10;i++)if(g.nodes[i]==f)return i;return 0;}
inline bool bikeRelativeToAnchor(const BikeContactGeometry&g,RwFrame*f,int&anchor,M&local){
    local=bikeIdentityMatrix();anchor=0;
    for(int depth=0;f&&depth<64;depth++,f=bikeFrameParent(f)){
        if((anchor=bikeNativeNode(g,f))!=0)return true;
        local=affineProduct(bikeFrameLocal(f),local);
    }return false;
}
static RpAtomic* __cdecl collectBikeContactAtomic(RpAtomic*atomic,void*data){
    auto&g=*static_cast<BikeContactGeometry*>(data);if(!g.reason.empty())return atomic;
    if(!atomic||!atomic->geometry){g.reason="missing_atomic_geometry";return atomic;}
    auto*frame=static_cast<RwFrame*>(atomic->object.object.parent);auto*geometry=atomic->geometry;
    if(!frame){g.reason="missing_atomic_frame";return atomic;}
    const auto name=bikeFrameName(frame);
    if(name.size()>=4&&name.substr(name.size()-4)=="_vlo"){g.excludedLodTriangles+=geometry->numTriangles;return atomic;}
    if(geometry->numMorphTargets!=1||!geometry->morphTarget||!geometry->morphTarget[0].verts||!geometry->triangles||
       geometry->numVertices<3||geometry->numVertices>50000||geometry->numTriangles<1||geometry->numTriangles>10000){g.reason="unsupported_render_geometry";return atomic;}
    int node=0;M relative{};if(!bikeRelativeToAnchor(g,frame,node,relative)){g.reason="atomic_outside_native_component_hierarchy";return atomic;}
    auto component=std::find_if(g.components.begin(),g.components.end(),[&](const auto&c){return c.node==node;});
    if(component==g.components.end()){g.reason="missing_component_owner";return atomic;}
    auto vertex=[&](int i){const auto&p=geometry->morphTarget[0].verts[i];return point(relative,{p.x,p.y,p.z});};
    for(int i=0;i<geometry->numTriangles;i++){
        const auto&t=geometry->triangles[i];g.sourceTriangles++;
        for(auto index:t.vertIndex)if(index>=geometry->numVertices){g.reason="invalid_render_triangle_index";return atomic;}
        const V a=vertex(t.vertIndex[0]),b=vertex(t.vertIndex[1]),c=vertex(t.vertIndex[2]);
        if(!finite(a)||!finite(b)||!finite(c)){g.reason="nonfinite_render_vertex";return atomic;}
        if(length(cross(b-a,c-a))<=1.e-6f){g.degenerateTriangles++;continue;}
        component->shape.primitives.push_back({0,a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z});
    }return atomic;
}
inline std::shared_ptr<BikeContactGeometry> buildBikeContactGeometry(void*bike){
    auto g=std::make_shared<BikeContactGeometry>();g->ref=vehicleRef(bike);
    BikeGeometryTimedScope total{g->cost.buildMs};
    if(g->ref<0||!matrix(bike)){g->reason="invalid_vehicle_generation";return g;}
    const int type=at<int>(bike,0x594);g->bicycle=type==10;
    if(type!=9&&type!=10){g->reason="not_cbike_or_cbmx";return g;}
    g->model=at<short>(bike,0x22);g->clump=at<void*>(bike,0x18);
    if(!g->clump){g->reason="missing_streamed_clump";return g;}
    const char* motorcycle[]={"","chassis_dummy","forks_front","forks_rear","wheel_front","wheel_rear","mudguard","handlebars","misc_a","misc_b"};
    const char* bicycle[]={"","chassis_dummy","forks_front","forks_rear","wheel_front","wheel_rear","handlebars","chainset","pedal_r","pedal_l"};
    for(int node=1;node<10;node++){
        g->nodes[node]=at<RwFrame*>(bike,0x5A0+node*4);
        if(g->nodes[node]&&bikeFrameName(g->nodes[node])!=(g->bicycle?bicycle[node]:motorcycle[node])){g->reason="native_node_name_mismatch";return g;}
    }
    if(!g->nodes[1]||!g->nodes[4]||!g->nodes[5]){g->reason="missing_chassis_or_wheel";return g;}
    const auto*clumpRoot=static_cast<RpClump*>(g->clump)->object.parent;
    for(int node=1;node<10;node++)if(g->nodes[node]){
        BikeContactComponent c;c.node=node;c.bridge=bikeIdentityMatrix();auto*f=bikeFrameParent(g->nodes[node]);int depth=0;
        for(;f&&f!=clumpRoot&&depth<64;f=bikeFrameParent(f),depth++){
            if((c.parent=bikeNativeNode(*g,f))!=0)break;
            c.bridge=affineProduct(bikeFrameLocal(f),c.bridge);
        }
        if(!f||depth>=64){g->reason="disconnected_component_hierarchy";return g;}
        g->components.push_back(std::move(c));
    }
    reinterpret_cast<void*(__cdecl*)(void*,RpAtomicCallBack,void*)>(0x749B70)(g->clump,&collectBikeContactAtomic,g.get());
    if(!g->reason.empty())return g;
    unsigned accounted=g->degenerateTriangles;
    for(auto&c:g->components){
        accounted+=unsigned(c.shape.primitives.size());
        if(c.shape.primitives.size()>2048){g->reason="exact_component_exceeds_shape_abi";return g;}
        if(c.shape.primitives.empty())continue; // a native pivot may have no own geometry
        V lo{1.e9f,1.e9f,1.e9f},hi{-1.e9f,-1.e9f,-1.e9f};
        for(const auto&r:c.shape.primitives)for(int i:{1,4,7}){const V p{r[i],r[i+1],r[i+2]};lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
        c.centre=(lo+hi)*.5f;c.half=(hi-lo)*.5f;
        for(auto&r:c.shape.primitives)for(int i:{1,4,7}){r[i]-=c.centre.x;r[i+1]-=c.centre.y;r[i+2]-=c.centre.z;}
    }
    if(accounted!=g->sourceTriangles||g->sourceTriangles==0){g->reason="incomplete_source_coverage";return g;}
    for(auto&c:g->components)if(!c.shape.primitives.empty()){
        c.shape.id=registerBikeContactShape(c.shape.primitives,&g->cost);
        if(c.shape.id<0){g->reason="shape_registration_failed";return g;}
    }
    for(int required:{1,4,5}){
        const auto c=std::find_if(g->components.begin(),g->components.end(),[&](const auto&v){return v.node==required;});
        if(c==g->components.end()||c->shape.id<0){g->reason="empty_chassis_or_wheel";return g;}
    }
    g->valid=true;return g;
}

// Call at an explicitly owned post-native-PreRender point. This sampler does
// not advance simulation and will refuse stale vehicle generations/clumps.
inline bool sampleBikeComponentFrames(void*bike,const BikeContactGeometry&g,BikeContactSnapshot&out,bool renderedNative){
    out={};out.vehicle=g.ref;out.frame=*reinterpret_cast<unsigned*>(0xB7CB4C);out.renderedNative=renderedNative;
    auto fail=[&](const char*reason){out.failure=reason;return false;};
    if(!g.valid||vehicleFromRef(g.ref)!=bike||at<void*>(bike,0x18)!=g.clump||!matrix(bike))return fail("stale_component_owner");
    auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(bike);
    if(!col||!col->data||!col->data->line)return fail("missing_native_suspension_lines");
    BikeComponentFrameState s;s.bicycle=g.bicycle;s.bicycleSuspension=*reinterpret_cast<bool*>(0xC1C83C);s.status=at<unsigned char>(bike,0x36)>>3;
    for(int i=1;i<10;i++)if(g.nodes[i]){if(at<RwFrame*>(bike,0x5A0+i*4)!=g.nodes[i])return fail("changed_native_component_nodes");s.present[i]=true;s.local[i]=bikeFrameLocal(g.nodes[i]);}
    auto*model=reinterpret_cast<void**>(0xA9B0C8)[g.model];if(!model)return fail("missing_model_info");
    s.steer=at<float>(bike,0x644);s.steerAxisDegrees=at<float>(model,0x58);s.lean=at<float>(bike,0x648);
    s.frontAngle=at<float>(bike,0x750);s.rearAngle=at<float>(bike,0x754);s.frontHeight=at<float>(bike,0x760);s.rearHeight=at<float>(bike,0x764);
    s.frontOriginal=at<float>(bike,0x768);s.rearOriginal=at<float>(bike,0x76C);s.swingArm=at<float>(bike,0x798);
    s.frontForkY=at<float>(bike,0x79C);s.frontForkZ=at<float>(bike,0x7A0);s.steerTangent=at<float>(bike,0x7A4);
    s.frontLineMidY=(at<float>(col->data->line,4)+at<float>(col->data->line,0x24))*.5f;s.collisionMinZ=col->box.min.z;
    s.frontBurst=at<unsigned char>(bike,0x65C)==1;s.rearBurst=at<unsigned char>(bike,0x65D)==1;
    if(g.bicycle){s.sprintLean=at<float>(bike,0x81C);s.chainAngle=at<float>(bike,0x820);s.rightPedalAngle=at<float>(bike,0x824);s.leftPedalAngle=at<float>(bike,0x828);s.wheelDistance=at<float>(bike,0x82C);s.wheelBalance=at<float>(bike,0x830);}
    std::array<M,10> predicted{},world{},modelFrame{};if(!bikeComponentLocalFrames(s,predicted))return fail("invalid_component_state");
    std::array<bool,10> done{};world[0]=*matrix(bike);modelFrame[0]=bikeIdentityMatrix();done[0]=true;
    for(const auto&c:g.components){
        const auto&a=predicted[c.node];const auto&b=s.local[c.node];
        if(renderedNative)out.nativeMatrixError=std::max({out.nativeMatrixError,length(a.right-b.right),length(a.up-b.up),length(a.at-b.at),length(a.pos-b.pos)});
    }
    for(int pass=0;pass<10;pass++)for(const auto&c:g.components)if(!done[c.node]&&done[c.parent]){
        const M local=affineProduct(c.bridge,predicted[c.node]);world[c.node]=affineProduct(world[c.parent],local);modelFrame[c.node]=affineProduct(modelFrame[c.parent],local);done[c.node]=true;
    }
    for(const auto&c:g.components){
        if(!done[c.node])return fail("unresolved_component_parent");
        const M&w=world[c.node];
        if(std::abs(length(w.right)-1)>.001f||std::abs(length(w.up)-1)>.001f||std::abs(length(w.at)-1)>.001f||
            std::abs(dot(w.right,w.up))>.001f||std::abs(dot(w.right,w.at))>.001f||std::abs(dot(w.up,w.at))>.001f)return fail("non_rigid_component_transform");
        if(renderedNative){
            const auto*actual=reinterpret_cast<const M*(__cdecl*)(RwFrame*)>(0x7F0990)(g.nodes[c.node]);if(!actual)return fail("missing_evaluated_component_ltm");
            out.nativeWorldError=std::max({out.nativeWorldError,length(w.pos-actual->pos),length(w.right-actual->right),length(w.up-actual->up),length(w.at-actual->at)});
        }
        if(c.shape.id>=0){const T component{rotation(w),point(w,c.centre)},relative{rotation(modelFrame[c.node]),point(modelFrame[c.node],c.centre)};out.components.push_back({&c,component,relative,s.local[c.node]});}
    }
    out.matched=out.nativeMatrixError<2.e-5f&&out.nativeWorldError<.001f;
    if(!out.matched)out.failure="native_component_matrix_mismatch";
    return out.matched;
}

struct BikeContactCacheEntry {std::shared_ptr<BikeContactGeometry>geometry;BikeContactSnapshot snapshot,previousSnapshot;};
static std::map<int,BikeContactCacheEntry> bikeContactGeometryCache;
inline V bikeRelativeAngularVelocity(Q before,Q after,float dt){
    Q delta=mul(after,conjugate(before));if(delta.w<0)delta={-delta.x,-delta.y,-delta.z,-delta.w};
    const V axis{delta.x,delta.y,delta.z};const float sine=length(axis);
    return sine>1.e-7f?axis*(2*std::atan2(sine,delta.w)/(sine*dt)):axis*(2/dt);
}
// Owned post-PreRender callback only: current native component transforms and
// their velocities are one frame-stamped sample. No stale LTM may be rebound to
// a newly advanced vehicle. Two consecutive evaluated samples establish local
// articulation velocity; parent handling-COM motion is never differentiated.
inline bool recordBikeContactSnapshot(void*bike,bool renderedNative){
    const int ref=vehicleRef(bike);if(ref<0)return false;
    for(auto i=bikeContactGeometryCache.begin();i!=bikeContactGeometryCache.end();)if(!vehicleFromRef(i->first))i=bikeContactGeometryCache.erase(i);else ++i;
    auto&entry=bikeContactGeometryCache[ref];
    if(!entry.geometry||entry.geometry->clump!=at<void*>(bike,0x18)||entry.geometry->model!=at<short>(bike,0x22)){entry={};entry.geometry=buildBikeContactGeometry(bike);}
    if(!entry.geometry->valid)return false;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);if(entry.snapshot.matched&&entry.snapshot.frame==frame)return true;
    BikeContactSnapshot current;if(!sampleBikeComponentFrames(bike,*entry.geometry,current,renderedNative)){entry.snapshot=std::move(current);return false;}
    const float dt=*reinterpret_cast<float*>(0xB7CB5C)*.02f;
    const bool consecutive=entry.snapshot.matched&&frame-entry.snapshot.frame==1&&dt>0&&std::isfinite(dt)&&entry.snapshot.components.size()==current.components.size();
    const Q rotation=::rotation(*matrix(bike));const V com=point(*matrix(bike),at<V>(bike,0xA4));
    const V linear=at<V>(bike,0x44)*50.f,angular=at<V>(bike,0x50)*50.f;
    for(size_t i=0;i<current.components.size();i++){
        auto&c=current.components[i];
        c.velocity=linear+cross(angular,c.frame.p-com);c.spin=angular;
        if(consecutive){
            const auto&previous=entry.snapshot.components[i];
            if(c.component->node!=previous.component->node)return false;
            const V localVelocity=(c.relative.p-previous.relative.p)*(1/dt);
            const V localSpin=bikeRelativeAngularVelocity(previous.relative.q,c.relative.q,dt);
            c.velocity=c.velocity+rotate(rotation,localVelocity);c.spin=c.spin+rotate(rotation,localSpin);
        }
    }
    current.motionVerified=consecutive;
    // Store the actual prior articulated frame, not a reconstruction using
    // today's fork/lean/wheel transform with yesterday's parent. Geometry
    // replacement clears the entire entry, so both snapshots share ownership.
    entry.previousSnapshot=consecutive?std::move(entry.snapshot):BikeContactSnapshot{};
    entry.snapshot=std::move(current);return true;
}
inline const BikeContactSnapshot* currentBikeContactSnapshot(void*bike){
    const int ref=vehicleRef(bike);const auto i=bikeContactGeometryCache.find(ref);
    if(i==bikeContactGeometryCache.end()||!i->second.geometry||i->second.geometry->clump!=at<void*>(bike,0x18))return nullptr;
    const auto&s=i->second.snapshot;
    return bikeContactStageEnabled&&s.matched&&s.frame==*reinterpret_cast<unsigned*>(0xB7CB4C)?&s:nullptr;
}
inline bool previousBikeComponentFrame(void*bike,int component,T&out){
    const auto*current=currentBikeContactSnapshot(bike);if(!current||!current->motionVerified)return false;
    const int ref=vehicleRef(bike);const auto found=bikeContactGeometryCache.find(ref);
    if(found==bikeContactGeometryCache.end()||!found->second.geometry||found->second.geometry->clump!=at<void*>(bike,0x18))return false;
    const auto&previous=found->second.previousSnapshot;
    if(!previous.matched||previous.vehicle!=ref||current->frame-previous.frame!=1)return false;
    const int node=component==0?1:component;
    const auto now=std::find_if(current->components.begin(),current->components.end(),[&](const auto&p){return p.component&&p.component->node==node;});
    const auto before=std::find_if(previous.components.begin(),previous.components.end(),[&](const auto&p){return p.component&&p.component->node==node;});
    // Pointer equality additionally proves that the frame belongs to the exact
    // same cached geometry/shape, not merely a reused model or vehicle slot.
    if(now==current->components.end()||before==previous.components.end()||before->component!=now->component||now->component->shape.id<0)return false;
    out=before->frame;return true;
}
