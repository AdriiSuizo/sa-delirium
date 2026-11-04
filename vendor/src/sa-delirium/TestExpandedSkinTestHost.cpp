#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include "ExpandedSkinTestHooks.hpp"
using namespace expandedSkinTest;
static unsigned checks=0,drawCalls=0,visibilityCalls=0,createCalls=0,queueCalls=0,removeCalls=0;
static unsigned failCreate=0,failQueue=0;static bool gate=false,badAbi=false;
static Actor actor{reinterpret_cast<void*>(0x1234),reinterpret_cast<void*>(0x5678),1,7,11,22,expandedSkin::rigidMatrix(T{})};
static void check(bool okay,const char*why){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static bool resolve(void*p,Actor&out){if(p!=actor.ped)return false;out=actor;return true;}
static unsigned frame(){return actor.frame;}
static bool enabled(){return gate;}
static void*basePipeline(){return nullptr;}
static void report(const char*,int,const std::string&){}
static bool read(void*out,const void*address,size_t n){
    for(const auto&s:HostSignatures)if(reinterpret_cast<uintptr_t>(address)==s.address&&n==s.count){std::memcpy(out,s.bytes,n);if(badAbi)static_cast<unsigned char*>(out)[0]^=1;return true;}
    for(const auto&s:expandedGpu::NativeSignatures)if(reinterpret_cast<uintptr_t>(address)==s.address&&n==s.count){std::memcpy(out,s.bytes,n);return true;}
    return false;
}
static Atomic*__cdecl originalDraw(Atomic*a){++drawCalls;return a;}
static int __cdecl originalVisibilityFunction(void*p,float*distance){check(p==actor.ped,"visibility argument preserved");++visibilityCalls;*distance=13.25f;return 2;}
static bool create(void*address,void*,void**trampoline){++createCalls;if(createCalls==failCreate)return false;*trampoline=reinterpret_cast<uintptr_t>(address)==0x7491C0?reinterpret_cast<void*>(&originalDraw):reinterpret_cast<void*>(&originalVisibilityFunction);return true;}
static bool remove(void*){++removeCalls;return true;}
static bool queue(void*){return ++queueCalls!=failQueue;}
static void resetHooks(){originalAtomicDraw=nullptr;originalVisibility=nullptr;createCalls=queueCalls=removeCalls=0;failCreate=failQueue=0;}
struct CompileRig {std::vector<int>ids,parents,partForBone;std::vector<T>bind,bodyToBone;std::array<T,21>offsets;
    int index(int)const{return 0;}std::array<T,21>jointFrames(const std::vector<T>&)const{return{};}};
int main(int argc,char**){
    check(length(triangleClosest({.2f,.3f,2},{},{1,0,0},{0,1,0})-V{.2f,.3f,0})<1.e-6f,"hand triangle distance projects onto actual face");
    check(length(triangleClosest({2,2,0},{},{1,0,0},{0,1,0})-V{.5f,.5f,0})<1.e-6f,"hand triangle distance clamps to actual edge");
    check(length(triangleClosest({.5f,2,0},{},{1,0,0},{2,0,0})-V{.5f,0,0})<1.e-6f,"degenerate triangle retains closest actual segment");
    NativeEnvelope envelope{11,4,true,expandedSkin::rigidMatrix(T{}),{-2,-2,-2},{2,2,2},{},3};
    expandedSkin::Vertex vertices[2]{{{1,0,0},{0,0,1}},{{0,1,0},{0,0,1}}};M atomicWorld=expandedSkin::rigidMatrix(T{});
    auto proof=withinNativeEnvelope(envelope,11,4,vertices,2,atomicWorld);check(proof.okay&&proof.examined==2,"actual vertices inside observed box and sphere");
    check(!withinNativeEnvelope(envelope,12,4,vertices,2,atomicWorld).okay,"prior frame culling cannot authorize new frame");
    check(!withinNativeEnvelope(envelope,11,5,vertices,2,atomicWorld).okay,"prior clothing generation rejected");
    vertices[0].position.x=2.1f;proof=withinNativeEnvelope(envelope,11,4,vertices,2,atomicWorld);check(!proof.okay&&proof.boxExcess>.09f&&proof.sphereExcess==0,"sphere alone cannot hide box/occlusion excess");
    envelope.radius=1.5f;vertices[0].position={1.2f,1.2f,0};proof=withinNativeEnvelope(envelope,11,4,vertices,2,atomicWorld);check(!proof.okay&&proof.sphereExcess>.19f&&proof.boxExcess==0,"box alone cannot hide camera sphere excess");
    envelope.radius=3;vertices[0].position={1,0,0};envelope.entityWorld=expandedSkin::rigidMatrix(T{unit(Q{0,0,.70710677f,.70710677f}),{13,17,0}});atomicWorld=envelope.entityWorld;
    check(withinNativeEnvelope(envelope,11,4,vertices,2,atomicWorld).okay,"world/entity transforms used for both bounds");
    atomicWorld.pos.x+=10;check(!withinNativeEnvelope(envelope,11,4,vertices,2,atomicWorld).okay,"later root displacement cannot reuse old native bound");
    atomicWorld=envelope.entityWorld;atomicWorld.right=atomicWorld.right*4;check(!withinNativeEnvelope(envelope,11,4,vertices,2,atomicWorld).okay,"native sphere radius is not enlarged to hide affine scale");
    auto invalid=envelope;invalid.entityWorld.right={};check(!withinNativeEnvelope(invalid,11,4,vertices,2,atomicWorld).okay,"singular entity frame rejected");
    check(sameAffine(envelope.entityWorld,envelope.entityWorld),"same affine accepted");auto altered=envelope.entityWorld;altered.pos.x+=.001f;check(!sameAffine(altered,envelope.entityWorld),"post-publication world mutation detected");
    HostApi host{resolve,frame,basePipeline,enabled,report};expandedSkinCapture::Api capture{};capture.read=read;
    Manager manager(host,capture,{}, {}, {});
    check(!manager.select(actor.ped),"owned test environment gate required");gate=true;
    check(manager.select(actor.ped)&&manager.stats().generation==1,"one selected actor recorded");
    check(manager.select(actor.ped)&&manager.stats().generation==1,"same identity does not churn resources");
    auto*oldPed=actor.ped;actor.reference=2;check(manager.select(actor.ped)&&manager.stats().generation==2,"pool reference change creates new generation");
    Atomic dummy{};Atomic*result=nullptr;
    check(!manager.draw(&dummy,originalDraw,result)&&drawCalls==0,"unselected atomic remains pass-through");
    check(!manager.commit(actor.frame,actor.nativeSerial),"no staged palette cannot be committed");
    std::array<T,21>physical{};M authored{};
    check(!manager.stage(&authored,physical,1,false),"missing verified resources cannot publish");
    manager.beforeVisibility(actor.ped);check(manager.stats().visibilityFrame==0,"unreadable native bounds do not grant visibility proof");
    manager.clear();check(!manager.observes(oldPed),"lifecycle teardown forgets target");
    if(argc==123456){CompileRig rig;manager.prepare(rig);} // Compile the full template without calling native functions.
    HookApi hooks{create,remove,queue,read};resetHooks();
    check(queueTestHooks(hooks,false)&&createCalls==0,"OFF installs no native hooks");badAbi=true;
    check(!queueTestHooks(hooks,true)&&createCalls==0,"modified native entries rejected before any hook");badAbi=false;
    failCreate=2;check(!queueTestHooks(hooks,true)&&removeCalls==1&&!originalAtomicDraw&&!originalVisibility,"second create failure rolls back first hook");
    resetHooks();failQueue=2;check(!queueTestHooks(hooks,true)&&removeCalls==2&&!originalAtomicDraw&&!originalVisibility,"queue failure rolls back both hooks");
    resetHooks();check(queueTestHooks(hooks,true)&&createCalls==2&&queueCalls==2,"both verified hooks queued");
    activeManager=nullptr;check(atomicDrawHook(&dummy)==&dummy&&drawCalls==1,"normal render delegates exactly once when no experimental actor");
    float distance=0;check(visibilityHook(actor.ped,&distance)==2&&distance==13.25f&&visibilityCalls==1,"visibility original called exactly once with actual output");
    activeManager=&manager;detachTestManager();check(activeManager==nullptr,"explicit shutdown detaches before native resources die");
    check(!expandedSkinHost::ExperimentalExpandedSkinRendering,"production flag remains OFF");
    std::printf("PASS %u test-only host checks: bounds, generation, frame, gates, hook rollback, exact pass-through; no live acceptance\n",checks);return 0;
}
