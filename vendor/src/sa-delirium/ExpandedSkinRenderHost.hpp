#pragma once
// EXPERIMENTAL, OFF and deliberately not installed by Adapter.cpp.
// Native API addresses are pinned in plugin-sdk RenderWare.cpp and the owned
// 1.0 US executable. Pipeline/shadow contracts are NOT inferred from a pointer.
#include "CpuExpandedSkin.hpp"
#include <cstring>
#include <limits>

namespace expandedSkinHost {
inline constexpr bool ExperimentalExpandedSkinRendering=false;
struct Sphere { V centre;float radius; };
struct Triangle { uint16_t vertex[3],material; };
struct Geometry;
struct Morph {Geometry*parent;Sphere bounds;V*vertices;V*normals;};
struct MaterialList {void**items;int count,capacity;};
struct Geometry {
    unsigned char object[8];uint32_t flags;uint16_t locked;int16_t references;
    int trianglesCount,verticesCount,morphsCount,uvCount;MaterialList materials;
    Triangle*triangles;uint32_t*prelight;void*uv[8];void*mesh;void*instance;Morph*morph;
};
struct Atomic {
    unsigned char frameObject[0x14];void*instance;Geometry*geometry;
    Sphere localBounds,worldBounds;void*clump;unsigned char clumpLink[8];
    void*renderCallback;unsigned char interpolator[0x14];uint16_t renderFrame,pad;
    unsigned char worldSectors[8];void*pipeline;
};
static_assert(sizeof(Geometry)==0x60&&offsetof(Geometry,morph)==0x5c);
static_assert(sizeof(Atomic)==0x70&&offsetof(Atomic,geometry)==0x18&&offsetof(Atomic,pipeline)==0x6c);
static_assert(sizeof(Morph)==0x1c&&sizeof(Triangle)==8);
using Draw=Atomic*(__cdecl*)(Atomic*);
struct Api {
    Geometry*(__cdecl*create)(int,int,uint32_t)=nullptr;
    Geometry*(__cdecl*lock)(Geometry*,int)=nullptr;
    Geometry*(__cdecl*unlock)(Geometry*)=nullptr;
    Geometry*(__cdecl*addRef)(Geometry*)=nullptr;
    int(__cdecl*destroy)(Geometry*)=nullptr;
    Geometry*(__cdecl*material)(Geometry*,Triangle*,void*)=nullptr;
    Atomic*(__cdecl*setGeometry)(Atomic*,Geometry*,uint32_t)=nullptr;
    void*(__cdecl*hierarchy)(Atomic*)=nullptr;
    int(__cdecl*freeInstance)(void*)=nullptr;
    bool complete()const{return create&&lock&&unlock&&addRef&&destroy&&material&&setGeometry&&hierarchy&&freeInstance;}
};
// This table is only a declaration; constructing it does not call native code.
inline Api pinnedApi(){return {
    reinterpret_cast<decltype(Api::create)>(0x74CA90),reinterpret_cast<decltype(Api::lock)>(0x74C7D0),
    reinterpret_cast<decltype(Api::unlock)>(0x74C800),reinterpret_cast<decltype(Api::addRef)>(0x74CCB0),
    reinterpret_cast<decltype(Api::destroy)>(0x74CCC0),reinterpret_cast<decltype(Api::material)>(0x74C6C0),
    reinterpret_cast<decltype(Api::setGeometry)>(0x749D40),reinterpret_cast<decltype(Api::hierarchy)>(0x7C7540),
    reinterpret_cast<decltype(Api::freeInstance)>(0x807DE0)
};}

struct DrawContract {
    void*sourcePipeline=nullptr;void*cpuPipeline=nullptr;
    bool pipelineSemanticsVerified=false,normalPolicyVerified=false,allShadowPassesVerified=false;
    bool frameCullingBoundsVerified=false,geometryPluginStreamsVerified=false;
    // GPU bridge keeps one complete stream declaration across shadow/main
    // ordering; that bridge applies temporary pass flags AFTER instancing.
    bool stableInstanceDeclaration=false;
    bool ready()const{return pipelineSemanticsVerified&&normalPolicyVerified&&allShadowPassesVerified&&frameCullingBoundsVerified&&geometryPluginStreamsVerified;}
};
struct Identity {
    int reference=-1,model=-1;void*clump=nullptr;Atomic*atomic=nullptr;
    Geometry*geometry=nullptr;void*nativeHierarchy=nullptr;
    uint64_t geometryGeneration=0;
    bool same(const Identity&b)const{return reference==b.reference&&model==b.model&&clump==b.clump&&atomic==b.atomic&&geometry==b.geometry&&nativeHierarchy==b.nativeHierarchy&&geometryGeneration==b.geometryGeneration;}
};

// Owns a new unskinned stream and a strong reference to the native source. No
// source positions, normals, weights, materials or hierarchy are modified.
class Surface {
    Api api_{};Geometry*source_=nullptr,*render_=nullptr;Identity id_{};
    bool busy_=false,uploaded_=false;unsigned poseFrame_=0;
public:
    explicit Surface(Api api):api_(api){}
    Surface(const Surface&)=delete;Surface&operator=(const Surface&)=delete;
    ~Surface(){clear();}
    void clear(){if(busy_)return;if(render_)api_.destroy(render_);if(source_)api_.destroy(source_);render_=source_=nullptr;uploaded_=false;}
    Geometry*renderGeometry()const{return render_;}
    bool prepare(const Identity&id){
        if(busy_||!api_.complete()||!id.atomic||!id.geometry||!id.clump||!id.nativeHierarchy||id.reference<0||!id.geometryGeneration)return false;
        auto*s=id.geometry;
        if(id.atomic->geometry!=s||id.atomic->clump!=id.clump||api_.hierarchy(id.atomic)!=id.nativeHierarchy)return false;
        if(s->morphsCount!=1||!s->morph||!s->morph->vertices||!s->morph->normals||s->verticesCount<1||s->verticesCount>65535||
           s->trianglesCount<1||s->uvCount<0||s->uvCount>8||(s->flags&0x03000000)||!(s->flags&0x10)||
           !s->triangles||s->materials.count<1||!s->materials.items)return false;
        if((s->flags&8)&&!s->prelight)return false;
        for(int i=0;i<s->uvCount;i++)if(!s->uv[i])return false;
        for(int i=0;i<s->verticesCount;i++)if(!finite(s->morph->vertices[i])||!finite(s->morph->normals[i]))return false;
        for(int i=0;i<s->trianglesCount;i++){
            const auto&t=s->triangles[i];
            if(t.material>=s->materials.count||!s->materials.items[t.material])return false;
            for(auto index:t.vertex)if(index>=s->verticesCount)return false;
        }
        auto*g=api_.create(s->verticesCount,s->trianglesCount,(s->flags&0xffff)|(uint32_t(s->uvCount)<<16));
        if(!g)return false;
        const auto reject=[&](){api_.destroy(g);return false;};
        if(!g->morph||!g->morph->vertices||!g->morph->normals||!g->triangles)return reject();
        for(int i=0;i<s->trianglesCount;i++){
            g->triangles[i]=s->triangles[i];
            if(!api_.material(g,&g->triangles[i],s->materials.items[s->triangles[i].material]))return reject();
        }
        const size_t count=size_t(s->verticesCount);
        std::memcpy(g->morph->vertices,s->morph->vertices,count*sizeof(V));
        std::memcpy(g->morph->normals,s->morph->normals,count*sizeof(V));g->morph->bounds=s->morph->bounds;
        if(s->flags&8){if(!g->prelight)return reject();std::memcpy(g->prelight,s->prelight,count*4);}
        for(int i=0;i<s->uvCount;i++){if(!g->uv[i])return reject();std::memcpy(g->uv[i],s->uv[i],count*8);}
        if(!api_.unlock(g))return reject();
        if(!api_.addRef(s))return reject();
        clear();source_=s;render_=g;id_=id;return true;
    }
    bool upload(const Identity&id,unsigned frame,const expandedSkin::Vertex*staging,size_t count,const expandedSkin::Bounds&bounds){
        if(busy_||!render_||!source_||!id.same(id_)||!staging||count!=size_t(render_->verticesCount)||
           !finite(bounds.min)||!finite(bounds.max)||bounds.min.x>bounds.max.x||bounds.min.y>bounds.max.y||bounds.min.z>bounds.max.z)return false;
        if(id.atomic->geometry!=source_||id.atomic->clump!=id.clump||api_.hierarchy(id.atomic)!=id.nativeHierarchy)return false;
        for(size_t i=0;i<count;i++)if(!finite(staging[i].position)||!finite(staging[i].normal))return false;
        auto*g=api_.lock(render_,6);if(!g){uploaded_=false;return false;}
        for(size_t i=0;i<count;i++){g->morph->vertices[i]=staging[i].position;g->morph->normals[i]=staging[i].normal;}
        g->morph->bounds.centre=(bounds.min+bounds.max)*.5f;g->morph->bounds.radius=length(bounds.max-bounds.min)*.5f;
        uploaded_=bool(api_.unlock(g));poseFrame_=frame;return uploaded_;
    }
private:
    struct Scope {
        Surface&s;Atomic*a;void*oldPipeline,*oldInstance;Sphere oldLocal,oldWorld;unsigned char oldInterpolator[0x14];uint32_t oldRenderFlags;bool restored=false,drawn=false;
        Scope(Surface&surface,Atomic*atomic):s(surface),a(atomic),oldPipeline(a->pipeline),oldInstance(a->instance),oldLocal(a->localBounds),oldWorld(a->worldBounds),oldRenderFlags(s.render_->flags){std::memcpy(oldInterpolator,a->interpolator,sizeof oldInterpolator);s.busy_=true;}
        ~Scope(){restore();}
        void restore(){
            if(restored)return;restored=true;
            // Any atomic-local instance belongs to this temporary draw only.
            if(a->instance&&a->instance!=oldInstance)s.api_.freeInstance(a->instance);
            a->instance=oldInstance;s.api_.setGeometry(a,s.source_,1);a->pipeline=oldPipeline;
            a->localBounds=oldLocal;a->worldBounds=oldWorld;std::memcpy(a->interpolator,oldInterpolator,sizeof oldInterpolator);s.render_->flags=oldRenderFlags;s.busy_=false;
        }
    };
    static Atomic* invokeFinally(Scope*scope,Draw original,Atomic*atomic,void*cpuPipeline,bool stableDeclaration){
        // No C++ locals requiring unwind here. __finally restores even a native
        // SEH fault; the original exception continues to the game's reporter.
        __try{
            atomic->instance=nullptr;
            if(!scope->s.api_.setGeometry(atomic,scope->s.render_,1))return nullptr;
            atomic->pipeline=cpuPipeline;atomic->localBounds=scope->s.render_->morph->bounds;
            // Shadow705BF0/705C80 changes these flags on the native source before
            // calling default render. Mirror that pass's flags on our own stream.
            if(!stableDeclaration)scope->s.render_->flags=scope->s.source_->flags;scope->drawn=true;
            return original(atomic);
        }__finally{scope->restore();}
    }
public:
    // A caller must verify the exact current default/render/shadow pipeline,
    // native normal policy and frame registration before enabling this flag.
    // Unknown pipeline or stale clothing generation returns false, never guesses.
    bool drawExperimental(bool enabled,const Identity&id,unsigned frame,const DrawContract&contract,Draw original,Atomic*&result){
        if(!enabled||!contract.ready()||!original||!uploaded_||busy_||frame!=poseFrame_||!id.same(id_)||
           !id.atomic||id.atomic->geometry!=source_||id.atomic->clump!=id.clump||
           api_.hierarchy(id.atomic)!=id.nativeHierarchy||id.atomic->pipeline!=contract.sourcePipeline)return false;
        Scope scope(*this,id.atomic);
        // A native source instance is retained, not released or repurposed.
        result=invokeFinally(&scope,original,id.atomic,contract.cpuPipeline,contract.stableInstanceDeclaration);return scope.drawn;
    }
};
} // namespace expandedSkinHost
