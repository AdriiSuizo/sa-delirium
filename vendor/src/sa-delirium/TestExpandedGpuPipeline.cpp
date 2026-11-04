#define main materialReplayMain
#include "TestExpandedGpuRender.cpp"
#undef main
#include "ExpandedGpuPipelineHost.hpp"
#include <algorithm>
using expandedSkinCapture::Pipeline;using expandedSkinCapture::PipelineNode;
static unsigned checks=0,allocations=0;static bool sharedPrivate=false,corruptNative=false;
static void check(bool b,const char*why){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static bool read(void*out,const void*in,size_t n){
    for(const auto&s:NativeSignatures)if(reinterpret_cast<uintptr_t>(in)==s.address&&n==s.count){std::memcpy(out,s.bytes,n);if(corruptNative)static_cast<unsigned char*>(out)[0]^=1;return true;}
    return expandedSkinCapture::readSelf(out,in,n);
}
static Pipeline*__cdecl clone(Pipeline*p){
    auto*c=new Pipeline(*p);c->nodes=new PipelineNode(*p->nodes);++allocations;
    if(!sharedPrivate){c->nodes->privateData=new uint32_t[4];std::memcpy(c->nodes->privateData,p->nodes->privateData,16);}return c;
}
static void __cdecl destroy(Pipeline*p){if(!sharedPrivate)delete[]static_cast<uint32_t*>(p->nodes->privateData);delete p->nodes;delete p;--allocations;}
static void __cdecl setLighting(PipelineNode*n,void*f){static_cast<uint32_t*>(n->privateData)[2]=reinterpret_cast<uintptr_t>(f);}
static void __cdecl setRender(PipelineNode*n,void*f){static_cast<uint32_t*>(n->privateData)[3]=reinterpret_cast<uintptr_t>(f);}
int main(){
    void*skinDefinition[2]{const_cast<char*>("skin"),reinterpret_cast<void*>(0x7c7b90)};
    void*defaultDefinition[2]{const_cast<char*>("default"),reinterpret_cast<void*>(0x7575f0)};
    uint32_t skinCallbacks[7]{0x756df0,0x757400,0x7cb190,0x761170,0x761010,0x761030,0x761000};
    uint32_t defaultCallbacks[4]{0x7578c0,0x758270,0x757400,0x756df0};
    PipelineNode skinNode{},defaultNode{};skinNode.definition=skinDefinition;skinNode.privateData=skinCallbacks;defaultNode.definition=defaultDefinition;defaultNode.privateData=defaultCallbacks;
    Pipeline source{},base{};source.count=base.count=1;source.nodes=&skinNode;base.nodes=&defaultNode;source.plugin=0x116;source.data=1;base.plugin=2;
    expandedSkinCapture::Api observe{};observe.read=read;
    expandedSkinCapture::Snapshot snapshot;snapshot.sourcePipeline=expandedSkinCapture::observePipeline(observe,&source);snapshot.defaultPipeline=expandedSkinCapture::observePipeline(observe,&base);snapshot.derivedWorldVerified=true;snapshot.hierarchyFlags=0x3000;snapshot.skinHardwareReadable=snapshot.hardwareGlobalReadable=true;snapshot.skinHardware=snapshot.hardwareGlobal=1;snapshot.geometryFlags=0x36;snapshot.materials.resize(1);snapshot.materialEffects={0};
    snapshot.renderCallback=0x7335B0;
    PipelineApi pa{clone,destroy,setLighting,setRender,read};Api ra{begin,world,active,compose,light,constants,state,indices,streams,declaration,alpha,shader,count,colours,material,fog,draw,end};
    check(!staticEligibility(snapshot),"observed canonical GPU source eligible");
    {PipelineOwner owner(pa,ra);check(!owner.prepare(snapshot,false)&&!allocations,"unverified native ABI cannot create pipeline");}
    corruptNative=true;{PipelineOwner owner(pa,ra);check(!owner.prepare(snapshot,true)&&!allocations,"modified helper bytes are refused without unhooking");}corruptNative=false;
    sharedPrivate=true;{PipelineOwner owner(pa,ra);check(!owner.prepare(snapshot,true)&&!allocations,"clone aliasing source private data is refused");}sharedPrivate=false;
    {PipelineOwner owner(pa,ra);check(owner.prepare(snapshot,true)&&allocations==1,"independent pipeline clone prepared");check(owner.get()!=&base,"source pipeline not repurposed");check(!owner.prepare(snapshot,true)&&allocations==1,"duplicate prepare cannot leak");check(defaultCallbacks[2]==0x757400&&defaultCallbacks[3]==0x756df0&&skinCallbacks[2]==0x7cb190,"source callbacks unchanged");}
    check(allocations==0,"owned pipeline released");
    snapshot.skinHardware=0;check(staticEligibility(snapshot)!=nullptr,"preinstancing hardware0 is not treated as stable GPU contract");snapshot.skinHardware=1;
    snapshot.materialEffects[0]=1;check(staticEligibility(snapshot)!=nullptr,"MatFX unverified stream rejected");snapshot.materialEffects[0]=0;
    snapshot.sourcePipeline.stages[0].privateWords[3]=0x11111111;check(staticEligibility(snapshot)!=nullptr,"third-party lighting callback rejected");
    check(!expandedSkinHost::ExperimentalExpandedSkinRendering,"runtime remains OFF");
    std::printf("PASS %u GPU pipeline ownership/ABI/eligibility checks; no source mutation or render activation\n",checks);
}
