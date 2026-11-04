#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ExpandedSkinCapture.hpp"
#include <cstdio>
#include <sstream>
using namespace expandedSkinCapture;
static int checks=0;static void check(bool b,const char*s){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
static M identity(){M m{};m.right={1,0,0};m.up={0,1,0};m.at={0,0,1};return m;}
static M matrices[32],inverseBinds[32];static Node nodes[32];static Hierarchy h{};
static float weights[12]={1,0,0,0,.25f,.25f,.25f,.25f,1,0,0,0};static uint32_t indices[3]={0,0x03020100,31};
static void* __cdecl skin(Geometry*){return reinterpret_cast<void*>(0x1234);}
static uint32_t __cdecl bones(void*){return 32;}
static const float* __cdecl getWeights(void*){return weights;}
static const uint32_t* __cdecl getIndices(void*){return indices;}
static const M* __cdecl getInverse(void*){return inverseBinds;}
static Hierarchy* __cdecl hierarchy(Atomic*){return &h;}
static int __cdecl effects(const void*){return 0;}
int main(int argc,char**argv){
    check(!expandedSkinHost::ExperimentalExpandedSkinRendering,"capture cannot enable renderer");
    std::string allBytes;for(unsigned c=1;c<256;c++)allBytes+=char(c);
    std::ostringstream oldString;JsonBuffer newString;writeString(oldString,allBytes);writeString(newString,allBytes);
    check(oldString.str()==newString.bytes(),"JSON string escaped and unsigned-byte formatting matches ostream");
    Api api{readSelf,skin,bones,getWeights,getIndices,getInverse,hierarchy,effects};
    for(int i=0;i<32;i++){matrices[i]=inverseBinds[i]=identity();nodes[i]={i,i,0,nullptr};}h.count=32;h.nodes=nodes;h.matrices=matrices;
    Geometry geometry{};geometry.verticesCount=3;geometry.trianglesCount=1;geometry.morphsCount=1;geometry.uvCount=1;geometry.flags=0x1e;
    V positions[3]{{0,0,0},{1,0,0},{0,1,0}},normals[3]{{0,0,1},{0,0,1},{0,0,1}};
    expandedSkinHost::Morph morph{&geometry,{},positions,normals};geometry.morph=&morph;
    expandedSkinHost::Triangle triangle{{0,1,2},0};geometry.triangles=&triangle;uint32_t colors[3]{1,2,3};geometry.prelight=colors;
    float uv[6]{0,0,1,0,0,1};geometry.uv[0]=uv;Material material{nullptr,0xffffffff,nullptr,1,0,1,1,0};void*materials[1]{&material};geometry.materials={materials,1,1};
    Frame frame{};frame.local=frame.world=identity();frame.root=&frame;
    Atomic atomic{};atomic.geometry=&geometry;atomic.clump=reinterpret_cast<void*>(0x4321);Frame*framePtr=&frame;std::memcpy(atomic.frameObject+4,&framePtr,4);
    void*definition[2]{const_cast<char*>("D3D9Skin\nobserved"),reinterpret_cast<void*>(0x7cb2a0)};
    PipelineNode node{};node.definition=definition;Pipeline pipeline{};pipeline.count=1;pipeline.nodes=&node;pipeline.plugin=0x116;atomic.pipeline=&pipeline;
    uint32_t callbacks[7]{0x756df0,0x757400,0x7cb190,0x761170,0x761010,0x761030,0x761000};node.privateData=callbacks;
    definition[1]=reinterpret_cast<void*>(0x7c7b90);
    auto observed=observePipeline(api,&pipeline);
    check(observed.stages[0].privateReadable&&observed.stages[0].privateWords.size()==7&&observed.stages[0].privateWords[6]==callbacks[6],"skin callbacks copied with verified seven-word bound");
    definition[1]=reinterpret_cast<void*>(0x7575f0);observed=observePipeline(api,&pipeline);
    check(observed.stages[0].privateReadable&&observed.stages[0].privateWords.size()==4,"default callback structure has four words");
    definition[1]=reinterpret_cast<void*>(0x11111111);node.privateData=reinterpret_cast<void*>(1);observed=observePipeline(api,&pipeline);
    check(!observed.stages[0].privateReadable&&observed.stages[0].privateWords.empty()&&!observed.stages[0].privateExpectedWords,"unknown node body never guesses private-data ABI");
    definition[1]=reinterpret_cast<void*>(0x7c7b90);observed=observePipeline(api,&pipeline);
    check(!observed.stages[0].privateReadable&&observed.stages[0].privateWords.empty()&&observed.stages[0].privateExpectedWords==7,"unreadable callbacks remain explicitly unknown");node.privateData=callbacks;
    Token token{5,0,19,212,1,atomic.clump,&atomic};Snapshot snapshot;
    check(capture(api,token,19,nullptr,snapshot),"actual assembled 32-node skin captured");
    check(snapshot.source.size()==3&&snapshot.nodes.size()==32&&snapshot.weights[2][0].bone==31,"runtime indices retained without source-DFF remap");
    check(snapshot.weights[1][3].value==.25f&&snapshot.weights[1][3].bone==3,"all four source weights retained");
    check(snapshot.frameWorldVerified&&snapshot.sourcePipeline.stages[0].name=="D3D9Skin\nobserved","existing frame and actual pipeline observed");
    check(snapshot.defaultPipeline.address==0&&!snapshot.defaultPipeline.readable,"default unknown remains unknown");
    check(snapshot.prelight[1]==2&&snapshot.uvs[0][2]==1&&snapshot.materialEffects[0]==0,"authored material inputs copied");
    positions[1].x=7;matrices[0].pos.x=8;check(snapshot.source[1].position.x==1&&snapshot.palette[0].pos.x==0,"capture immutable after native arrays change");positions[1].x=1;matrices[0].pos.x=0;
    const auto original=atomic;frame.object[3]=5;frame.local.pos={7,8,9};check(capture(api,token,19,nullptr,snapshot)&&!snapshot.frameWorldVerified&&frame.object[3]==5&&snapshot.derivedWorldVerified&&snapshot.derivedWorld.pos.x==7&&frame.world.pos.x==0,"dirty frame reported and resolved purely without updating frame tree");frame.object[3]=0;frame.local=identity();
    Frame child{};child.root=&frame;child.local=identity();child.local.pos={0,3,0};child.world=identity();child.world.pos={8,9,10};Frame*rootPtr=&frame;std::memcpy(child.object+4,&rootPtr,4);std::vector<FrameObservation>observations;M derived;
    check(deriveFrameWorld(api,&child,derived,observations)&&derived.pos.x==8,"clean root returns exact cached LTM even if modelling differs");
    frame.object[3]=1;child.object[3]=4;frame.world.pos={2,0,0};check(deriveFrameWorld(api,&child,derived,observations)&&derived.pos.x==2&&derived.pos.y==3,"dirty child composes from clean parent's actual world");
    frame.object[3]=5;frame.local.pos={4,0,0};child.object[3]=0;check(deriveFrameWorld(api,&child,derived,observations)&&derived.pos.x==4&&derived.pos.y==3,"ancestor dirty subtree propagates to clean child");
    Frame*self=&child;std::memcpy(child.object+4,&self,4);check(!deriveFrameWorld(api,&child,derived,observations),"cyclic frame parent chain rejected");frame.object[3]=0;frame.local=frame.world=identity();
    check(!capture(api,token,20,nullptr,snapshot)&&snapshot.failure=="invalid_capture_epoch_or_identity","wrong frame rejected");
    auto old=token.clump;token.clump=reinterpret_cast<void*>(0x9999);check(!capture(api,token,19,nullptr,snapshot)&&snapshot.failure=="atomic_identity_changed","wrong actor/clump rejected");token.clump=old;
    indices[2]=35;check(!capture(api,token,19,nullptr,snapshot)&&snapshot.failure=="invalid_actual_skin_weight","source37 index forbidden in runtime32");indices[2]=31;
    weights[0]=std::numeric_limits<float>::infinity();check(!capture(api,token,19,nullptr,snapshot)&&snapshot.failure=="invalid_actual_skin_weight","nonfinite weight rejected");weights[0]=1;
    float nan=std::numeric_limits<float>::quiet_NaN();uv[0]=nan;check(!capture(api,token,19,nullptr,snapshot)&&snapshot.failure=="nonfinite_uv","nonfinite UV prevents invalid JSON");uv[0]=0;
    check(!readSelf(&nan,reinterpret_cast<void*>(1),sizeof nan),"invalid pointer read fails without SEH escaping");
    check(std::memcmp(&atomic,&original,sizeof atomic)==0&&geometry.references==0,"no atomic/hierarchy/resource mutation");
    check(capture(api,token,19,nullptr,snapshot),"capture restored valid fixture");
    if(argc>1){std::string p=argv[1];check(write(snapshot,std::wstring(p.begin(),p.end())),"snapshot writer");}
    std::printf("PASS %d checks: read-only assembled skin/actual palette/material/pipeline snapshot; no eval; exact frame/identity/index validation\n",checks);
}
