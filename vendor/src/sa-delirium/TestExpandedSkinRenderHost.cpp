#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ExpandedSkinRenderHost.hpp"
#include <cstdio>
#include <stdexcept>
using namespace expandedSkinHost;
static int checks=0,alive=0,draws=0,freedInstances=0;static void*expectedHierarchy=reinterpret_cast<void*>(0x1234);
static void check(bool b,const char*s){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
static Geometry* __cdecl make(int nv,int nt,uint32_t flags){
    auto*g=new Geometry{};g->references=1;g->flags=flags;g->verticesCount=nv;g->trianglesCount=nt;g->morphsCount=1;g->uvCount=flags>>16;
    g->triangles=new Triangle[nt]{};g->materials.items=new void*[nt]{};g->materials.capacity=nt;
    g->morph=new Morph{g,{},new V[nv]{},new V[nv]{}};if(flags&8)g->prelight=new uint32_t[nv]{};
    for(int i=0;i<g->uvCount;i++)g->uv[i]=new float[nv*2]{};++alive;return g;
}
static Geometry* __cdecl lock(Geometry*g,int){return g;}
static Geometry* __cdecl retain(Geometry*g){++g->references;return g;}
static Geometry* __cdecl unlock(Geometry*g){return g;}
static int __cdecl destroy(Geometry*g){
    if(--g->references)return 1;delete[]g->triangles;delete[]g->materials.items;delete[]g->morph->vertices;delete[]g->morph->normals;delete g->morph;
    delete[]g->prelight;for(int i=0;i<g->uvCount;i++)delete[]static_cast<float*>(g->uv[i]);delete g;--alive;return 1;
}
static Geometry* __cdecl setMaterial(Geometry*g,Triangle*t,void*m){
    int i=0;for(;i<g->materials.count;i++)if(g->materials.items[i]==m)break;
    if(i==g->materials.count)g->materials.items[g->materials.count++]=m;t->material=uint16_t(i);return g;
}
static Atomic* __cdecl setGeometry(Atomic*a,Geometry*g,uint32_t){if(a->geometry!=g){if(g)retain(g);if(a->geometry)destroy(a->geometry);a->geometry=g;}return a;}
static void* __cdecl getHierarchy(Atomic*){return expectedHierarchy;}
static int __cdecl freeInstance(void*){++freedInstances;return 1;}
static Atomic* __cdecl draw(Atomic*a){
    check(a->geometry->morph->vertices[0].x==9,"draw consumes own deformed geometry");
    check(a->pipeline==reinterpret_cast<void*>(0x6789),"explicit audited CPU pipeline");
    check(getHierarchy(a)==reinterpret_cast<void*>(0x1234),"native HAnim unchanged during render");
    a->instance=reinterpret_cast<void*>(0x8888);++draws;return a;
}
static Atomic* __cdecl cppError(Atomic*a){draw(a);throw std::runtime_error("deliberate render exception");}
static Atomic* __cdecl sehError(Atomic*a){draw(a);RaiseException(0xEABC1234,0,0,nullptr);return nullptr;}
static bool invokeSeh(Surface*s,Identity*id,DrawContract*c){
    Atomic*result=nullptr;
    __try{s->drawExperimental(true,*id,19,*c,sehError,result);}
    __except(GetExceptionCode()==0xEABC1234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
int main(){
    check(!ExperimentalExpandedSkinRendering,"production experimental flag OFF");
    Api api{make,lock,unlock,retain,destroy,setMaterial,setGeometry,getHierarchy,freeInstance};
    auto*source=make(3,1,0x1001e);source->materials.items[0]=reinterpret_cast<void*>(0x4444);source->materials.count=1;
    source->triangles[0]={{0,1,2},0};source->prelight[0]=0x12345678;static_cast<float*>(source->uv[0])[0]=.7f;
    for(int i=0;i<3;i++){source->morph->vertices[i]={float(i),1,2};source->morph->normals[i]={0,0,1};}
    Atomic atomic{};atomic.geometry=source;atomic.clump=reinterpret_cast<void*>(0x4321);atomic.pipeline=reinterpret_cast<void*>(0x1111);
    atomic.instance=reinterpret_cast<void*>(0x7777);atomic.localBounds={{1,2,3},4};atomic.worldBounds={{4,5,6},7};
    Identity id{1,0,atomic.clump,&atomic,source,expectedHierarchy,1};
    {
        Surface surface(api);check(surface.prepare(id),"private geometry prepared");auto*g=surface.renderGeometry();
        check(g!=source&&g->prelight[0]==source->prelight[0]&&static_cast<float*>(g->uv[0])[0]==.7f,"prelight and UV copied without changing source");
        check(g->materials.items[0]==source->materials.items[0],"authored material object retained");
        expandedSkin::Vertex staging[3]{{{9,0,0},{0,0,1}},{{10,0,0},{0,0,1}},{{9,1,0},{0,0,1}}};expandedSkin::Bounds bounds{{9,0,0},{10,1,0}};
        check(surface.upload(id,19,staging,3,bounds),"private stream staged");check(source->morph->vertices[0].x==0,"source vertex remains immutable");
        DrawContract c;c.sourcePipeline=atomic.pipeline;c.cpuPipeline=reinterpret_cast<void*>(0x6789);Atomic*result=nullptr;
        check(!surface.drawExperimental(true,id,19,c,draw,result)&&draws==0,"unknown pipeline/normal/shadow contract refuses");
        c.pipelineSemanticsVerified=c.normalPolicyVerified=c.allShadowPassesVerified=c.frameCullingBoundsVerified=c.geometryPluginStreamsVerified=true;
        check(!surface.drawExperimental(false,id,19,c,draw,result)&&draws==0,"disabled flag leaves native alone");
        auto changed=id;changed.geometryGeneration=2;check(!surface.drawExperimental(true,changed,19,c,draw,result),"clothing generation mismatch refuses");
        check(!surface.drawExperimental(true,id,20,c,draw,result),"stale pose frame refuses");
        check(surface.drawExperimental(true,id,19,c,draw,result)&&result==&atomic,"verified experimental draw executes once");
        const auto restored=[&](){check(atomic.geometry==source&&atomic.pipeline==c.sourcePipeline&&atomic.instance==reinterpret_cast<void*>(0x7777),"geometry/pipeline/native instance restored");check(atomic.localBounds.radius==4&&atomic.worldBounds.radius==7,"atomic bounds restored");check(source->references==2&&g->references==1,"strong geometry references balanced");};restored();
        try{surface.drawExperimental(true,id,19,c,cppError,result);check(false,"expected C++ error");}catch(const std::runtime_error&){restored();}
        check(invokeSeh(&surface,&id,&c),"native SEH still propagates");restored();
        check(freedInstances==3&&draws==3,"temporary instances released, no original instance released");
        const auto declarationFlags=g->flags,sourceFlags=source->flags;
        source->flags&=~0x10u;c.stableInstanceDeclaration=true;
        check(surface.drawExperimental(true,id,19,c,draw,result),"shadow-first scoped draw allowed with stable declaration");
        check(g->flags==declarationFlags&&(g->flags&0x10),"temporary shadow flags cannot strip owned instance normals");
        check(!(source->flags&0x10),"source pass flags remain untouched");source->flags=sourceFlags;restored();
    }
    check(source->references==1&&alive==1,"only original geometry remains");destroy(source);check(alive==0,"private resources released");
    std::printf("PASS %d checks: experimental OFF; immutable source/prelight/UV/materials/HAnim; generation/frame guards; scoped C++ and SEH restoration\n",checks);
}
