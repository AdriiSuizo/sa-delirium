#pragma once
#include "ABI.hpp"
#include "FrameCostProfile.hpp"
#include "delirium_physics.h"
#include "VehicleGeometry.hpp"
#include "VehicleDoorGeometry.hpp"
#include "DoorHingeMotion.hpp"
#include "BikeContactGeometry.hpp"
#include "NativeWorldCoverage.hpp"
#include <vector>
#include <array>
#include <filesystem>
#include <fstream>
struct CollisionWorld {
    struct Vehicle {int ref=-1,index=-1;void* entity=nullptr;T frame{};V half{},velocity{},spin{};const VehicleShape*shape=nullptr;int component=0;bool velocityAtFrame=false;std::shared_ptr<const BikeContactGeometry>bikeGeometryOwner;int identity()const{return ref*16+component;}};
    std::vector<Vehicle> vehicles;
    std::map<int,unsigned> doorVehicles;
    std::map<std::pair<int,int>,DoorHingeMotion> doorMotion;
    unsigned bikeProxyVehicles=0,bikeProxyFallbacks=0,bikeProxyUnverifiedMotion=0;
    const Vehicle* findVehicle(int ref,int component=0)const{for(const auto&v:vehicles)if(v.ref==ref&&v.component==component&&vehicleFromRef(ref)==v.entity)return &v;return nullptr;}
    const Vehicle* findIdentity(int id)const{return findVehicle(id/16,id%16);}
    NmWorld* world=nullptr;V center{};int currentArea=-1;unsigned builtAt=0;int entities=0,triangles=0,boxes=0,simpleTransforms=0,skippedNoCollision=0;
    std::filesystem::path debugMesh;
    NativeWorldCoverage nativeCoverage;
    // Which SA entity produced each run of mesh triangles (diagnostic map for
    // replay dumps: triangle_index -> entity). Rebuilt with the mesh.
    struct MeshSource {uint32_t first=0,count=0;int model=-1,type=0;V pos{};unsigned short triangles=0,boxes=0,spheres=0;bool simple=false;};
    std::vector<MeshSource> meshSources;
    std::vector<float> builtVertices;
    std::vector<uint32_t> builtIndices;
    std::vector<uint8_t> builtMaterials;
    ~CollisionWorld(){if(world)nm_world_destroy(world);}
    void applyVehicleBoxes(int ignore=-1){
        std::vector<float> data;data.reserve(vehicles.size()*13);
        for(const auto&v:vehicles){
            // Retain array indices: brace/grab messages refer to these indices.
            const bool skip=v.ref==ignore;const V p=skip?V{0,0,-10000}:v.frame.p;
            const V h=skip?V{.001f,.001f,.001f}:v.half;const Q q=v.frame.q;
            // SDK boxes rotate about their geometric centre. SA move speed
            // is at its handling COM: transport the velocity to that centre
            // before binding angular velocity, otherwise rotation adds energy.
            const V com=matrix(v.entity)?point(*matrix(v.entity),at<V>(v.entity,0xA4)):v.frame.p;
            const V velocity=(v.velocityAtFrame||v.component)?v.velocity:v.velocity+cross(v.spin,v.frame.p-com);
            data.insert(data.end(),{p.x,p.y,p.z,q.x,q.y,q.z,q.w,h.x,h.y,h.z,velocity.x,velocity.y,velocity.z});
        }
        nm_world_set_boxes(world,data.empty()?nullptr:data.data(),int(vehicles.size()));
        for(const auto&v:vehicles)if(v.ref!=ignore&&v.shape&&v.shape->id>=0)
            nm_world_set_vehicle_shape(world,v.index,v.shape->id,&v.spin.x);
    }
    void rebuild(V origin,void*required=nullptr){
        FrameCostProfile cost("world_rebuild",0,0,-1,-1,0.);
        nativeCoverage.begin(origin,area());
        std::vector<float> verts;std::vector<uint32_t> indices;std::vector<uint8_t> materials;
        std::array<void*,1025> found{};short count=0;entities=0;simpleTransforms=0;skippedNoCollision=0;meshSources.clear();
        // Bound-volume query: origin-only564A20 can omit large COLs whose
        // triangles overlap the retained mesh even when their origin is far.
        reinterpret_cast<void(__cdecl*)(const V&,float,bool,short*,short,void**,bool,bool,bool,bool,bool)>(0x568B80)(origin,100.f,true,&count,1024,found.data(),true,false,false,true,true);
        if(count<0||count>=1024)nativeCoverage.complete=false;
        count=std::clamp<short>(count,0,1024);
        if(required&&std::find(found.begin(),found.begin()+count,required)==found.begin()+count)found[count++]=required;
        auto triangle=[&](V a,V b,V c,unsigned char mat){
            V lo{std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})};
            V hi{std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})};
            if(origin.x+45<lo.x||origin.x-45>hi.x||origin.y+45<lo.y||origin.y-45>hi.y||origin.z+20<lo.z||origin.z-30>hi.z)return;
            if(materials.size()>=60000){nativeCoverage.complete=false;return;}
            if(length(cross(b-a,c-a))<1.e-5f)return;
            for(V v:{a,b,c}){indices.push_back(uint32_t(verts.size()/3));verts.insert(verts.end(),{v.x,v.y,v.z});}materials.push_back(mat);
        };
        for(int n=0;n<count;n++){
            auto* e=found[n];if(!e||(at<unsigned char>(e,0x2F)!=area()&&at<unsigned char>(e,0x2F)!=13))continue;
            // CEntity::bUsesCollision (0x1C bit 0). SA never collides with
            // such entities (pickups, e.g. dropped money 1212, keep a COL
            // sphere but have it off); baking them made solid balls inside cars.
            if(!(at<unsigned char>(e,0x1C)&1)){skippedNoCollision++;continue;}
            auto* col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(e);auto* m=matrix(e);
            if(!col||!col->data)continue;
            // CPlaceable may retain only position/heading. Those buildings
            // still have collision; skipping them silently removes road tiles.
            // Convert into a local RwMatrix, without allocating/mutating theirs.
            M simple{};if(!m){reinterpret_cast<void(__thiscall*)(void*,M*)>(0x54EF40)(static_cast<unsigned char*>(e)+4,&simple);m=&simple;simpleTransforms++;}
            auto* d=col->data;entities++;
            nativeCoverage.record(e,col,*m);
            meshSources.push_back({uint32_t(materials.size()),0,at<short>(e,0x22),at<unsigned char>(e,0x36)&7,m->pos,d->triangles,d->boxes,d->spheres,m==&simple});
            if(d->vertices&&d->tri)for(int i=0;i<d->triangles;i++){
                const auto t=d->tri[i];auto v=[&](int k){V p;reinterpret_cast<void(__thiscall*)(void*,V&,int)>(0x40F5E0)(d,p,k);return point(*m,p);};
                // SA CColTrianglePlane uses (C-A) cross (B-A); NM uses (B-A) cross
                // (C-A). Reverse COL winding so contacts face the same solid side.
                triangle(v(t.a),v(t.c),v(t.b),t.material);
            }
            if(d->box)for(int i=0;i<d->boxes;i++){
                const auto b=d->box[i];V v[8];for(int k=0;k<8;k++)v[k]=point(*m,{k&1?b.box.max.x:b.box.min.x,k&2?b.box.max.y:b.box.min.y,k&4?b.box.max.z:b.box.min.z});
                constexpr int faces[12][3]={{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};
                for(auto&f:faces)triangle(v[f[0]],v[f[1]],v[f[2]],b.material);
            }
            if(d->sphere)for(int i=0;i<d->spheres;i++){
                const auto s=d->sphere[i];if(s.radius<=0)continue;
                V v[6]={s.pos+V{s.radius,0,0},s.pos+V{-s.radius,0,0},s.pos+V{0,s.radius,0},s.pos+V{0,-s.radius,0},s.pos+V{0,0,s.radius},s.pos+V{0,0,-s.radius}};
                constexpr int f[8][3]={{4,0,2},{4,2,1},{4,1,3},{4,3,0},{5,2,0},{5,1,2},{5,3,1},{5,0,3}};
                // An octahedron cut deep into native spherical supports (the
                // wreck at Grove Street differed by 11 cm). Refine the actual
                // sphere surface; retain the same centre/radius and winding.
                auto subdivide=[&](auto&&self,V a,V b,V c,int depth)->void{
                    if(!depth){triangle(point(*m,a),point(*m,b),point(*m,c),s.material);return;}
                    const V ab=s.pos+normalized((a+b)*.5f-s.pos)*s.radius;
                    const V bc=s.pos+normalized((b+c)*.5f-s.pos)*s.radius;
                    const V ca=s.pos+normalized((c+a)*.5f-s.pos)*s.radius;
                    self(self,a,ab,ca,depth-1);self(self,ab,b,bc,depth-1);
                    self(self,ca,bc,c,depth-1);self(self,ab,bc,ca,depth-1);
                };
                for(auto& t:f)subdivide(subdivide,v[t[0]],v[t[1]],v[t[2]],2);
            }
            meshSources.back().count=uint32_t(materials.size())-meshSources.back().first;
        }
        cost.lap(0);
        // Recheck the native map every time. Reuse its acceleration data only
        // for identical vertices, material bytes and triangle order.
        const bool reuse=world&&!materials.empty()&&materials==builtMaterials&&indices==builtIndices&&
            verts.size()==builtVertices.size()&&std::memcmp(verts.data(),builtVertices.data(),verts.size()*sizeof(float))==0;
        bool imported=false;
        if(!reuse){
            auto* next=nm_world_create();
            if(!next){nativeCoverage.complete=false;return;}
            imported=!materials.empty()&&nm_world_add_mesh_two_sided(next,verts.data(),int(verts.size()/3),indices.data(),int(materials.size()),materials.data())>=0&&
                nm_world_triangle_count(next)==int(materials.size());
            if(!imported){nativeCoverage.complete=false;builtVertices.clear();builtIndices.clear();builtMaterials.clear();}
            if(world)nm_world_destroy(world);
            world=next;
        }else{
            // A freshly created world has no peer or vehicle snapshots.
            nm_world_set_capsules(world,nullptr,0);
            nm_world_set_boxes(world,nullptr,0);
        }
        cost.ref=reuse;
        cost.model=nm_world_triangle_count(world);
        cost.lap(1);
        if(!debugMesh.empty()){
            std::ofstream f(debugMesh,std::ios::binary);uint32_t n=uint32_t(materials.size());
            f.write(reinterpret_cast<const char*>(&n),sizeof(n));f.write(reinterpret_cast<const char*>(verts.data()),verts.size()*sizeof(float));f.write(reinterpret_cast<const char*>(materials.data()),materials.size());
        }
        if(!debugMesh.empty()&&GetEnvironmentVariableA("GTASA_WORLD_SNAPSHOT_AUDIT",nullptr,0)){
            std::ofstream f(debugMesh.string()+".sources.jsonl");
            f<<"{\"origin\":["<<origin.x<<','<<origin.y<<','<<origin.z<<"],\"area\":"<<area()<<",\"complete\":"<<(nativeCoverage.complete?"true":"false")
             <<",\"found\":"<<count<<",\"triangles\":"<<materials.size()<<",\"skipped_no_collision\":"<<skippedNoCollision<<"}\n";
            for(const auto&s:meshSources)f<<"{\"first\":"<<s.first<<",\"count\":"<<s.count<<",\"model\":"<<s.model<<",\"type\":"<<s.type
             <<",\"position\":["<<s.pos.x<<','<<s.pos.y<<','<<s.pos.z<<"],\"native_triangles\":"<<s.triangles<<",\"native_boxes\":"<<s.boxes<<",\"native_spheres\":"<<s.spheres<<"}\n";
        }
        if(imported){
            builtVertices=std::move(verts);builtIndices=std::move(indices);builtMaterials=std::move(materials);
        }
        center=origin;currentArea=area();builtAt=gameTime();triangles=cost.model;
    }
    void update(V origin){
        bikeProxyVehicles=bikeProxyFallbacks=bikeProxyUnverifiedMotion=0;
        for(auto it=doorMotion.begin();it!=doorMotion.end();)if(!vehicleFromRef(it->first.first))it=doorMotion.erase(it);else ++it;
        for(auto it=doorVehicles.begin();it!=doorVehicles.end();)if(!vehicleFromRef(it->first)||gameTime()-it->second>3000)it=doorVehicles.erase(it);else ++it;
        if(!world||currentArea!=area()||length(origin-center)>12||gameTime()-builtAt>2000)rebuild(origin);
        std::array<void*,256> found{};short count=0;vehicles.clear();
        reinterpret_cast<void(__cdecl*)(const V&,float,bool,short*,short,void**,bool,bool,bool,bool,bool)>(0x564A20)(origin,45.f,true,&count,256,found.data(),false,true,false,false,false);
        for(int i=0;i<count&&i<256;i++){
            void*e=found[i];auto*m=matrix(e);if(!m||at<unsigned char>(e,0x2F)!=area())continue;
            auto* c=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(e);if(!c)continue;
            V half=(c->box.max-c->box.min)*.5f,cen=point(*m,(c->box.max+c->box.min)*.5f),vel=at<V>(e,0x44)*50.f;Q q=rotation(*m);
            if(!finite(cen)||half.x<=0||half.y<=0||half.z<=0)continue;
            const int ref=vehicleRef(e);if(ref<0)continue;
            const int subtype=at<int>(e,0x594);
            if(bikeContactStageEnabled&&(subtype==9||subtype==10)){
                if(const auto*snapshot=currentBikeContactSnapshot(e)){
                    // The source COL is kept for SA's own vehicle/world solver.
                    // NM uses the complete installed articulated geometry, all
                    // components sharing this same physical parent and ref.
                    for(const auto&part:snapshot->components){
                        const auto&shape=*part.component;const int component=shape.node==1?0:shape.node;
                        vehicles.push_back({ref,int(vehicles.size()),e,part.frame,shape.half,part.velocity,part.spin,&shape.shape,component,true,bikeContactGeometryCache.at(ref).geometry});
                    }
                    bikeProxyVehicles++;if(!snapshot->motionVerified)bikeProxyUnverifiedMotion++;
                    continue;
                }
                // Missing/invalid metadata is an explicit whole-vehicle
                // fallback; never silently omit a wheel or partially replace.
                bikeProxyFallbacks++;
            }
            const auto&shape=vehicleShape(at<short>(e,0x22),c);
            vehicles.push_back({ref,int(vehicles.size()),e,{q,cen},half,vel,at<V>(e,0x50)*50.f,&shape});
            if(at<int>(e,0x594)==0)for(int node:{8,9,10,11}){
                const int door=node==10?2:node==8?3:node==11?4:5;
                void*d=static_cast<unsigned char*>(e)+0x5B8+door*0x18;
                void*f=at<void*>(e,0x648+node*4);if(!f)continue;
                const unsigned axis=at<unsigned char>(d,0xA);
                const float dt=*reinterpret_cast<float*>(0xB7CB5C)*.02f;
                const auto motion=doorMotion[{ref,node}].sample(
                    reinterpret_cast<std::uintptr_t>(at<void*>(e,0x18)),reinterpret_cast<std::uintptr_t>(f),axis,
                    *reinterpret_cast<unsigned*>(0xB7CB4C),gameTime(),at<float>(d,0xC),dt);
                if(std::abs(at<float>(d,0xC)-at<float>(d,4))<.001f&&!doorVehicles.contains(ref))continue; // retain a held door even when it closes
                const auto*shape=doorShape(e,node,f);if(!shape||shape->shape.id<0)continue;
                const M*fm=reinterpret_cast<M*(__cdecl*)(void*)>(0x7F0990)(f);if(!fm)continue;
                const V centre=point(*fm,shape->centre);const Q orientation=rotation(*fm);
                const V axisWorld=rotate(q,axis==0?V{1,0,0}:axis==1?V{0,1,0}:V{0,0,1});
                const V hingeSpin=axisWorld*motion.speed,carSpin=at<V>(e,0x50)*50.f;
                const V com=point(*m,at<V>(e,0xA4));
                const V linear=vel+cross(carSpin,centre-com)+cross(hingeSpin,centre-fm->pos);
                vehicles.push_back({ref,int(vehicles.size()),e,{orientation,centre},shape->half,linear,carSpin+hingeSpin,&shape->shape,node});
            }
        }
        boxes=int(vehicles.size());applyVehicleBoxes();
    }
};
