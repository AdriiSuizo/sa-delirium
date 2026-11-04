#pragma once
// Owned TEST_ONLY dispatcher. No hook or Adapter activation in this header.
// Source pipeline, geometry, skin and native HAnim are never edited.
#include "ExpandedGpuRender.hpp"
#include "ExpandedGpuNativeAbi.hpp"
#include "ExpandedSkinCapture.hpp"

namespace expandedGpu {
using expandedSkinCapture::Pipeline;using expandedSkinCapture::PipelineNode;
using expandedSkinCapture::PipelineObservation;using expandedSkinCapture::Snapshot;
struct PipelineApi {
    Pipeline*(__cdecl*clone)(Pipeline*)=nullptr;
    void(__cdecl*destroy)(Pipeline*)=nullptr;
    void(__cdecl*setLighting)(PipelineNode*,void*)=nullptr;
    void(__cdecl*setRender)(PipelineNode*,void*)=nullptr;
    expandedSkinCapture::Read read=nullptr;
    bool complete()const{return clone&&destroy&&setLighting&&setRender&&read;}
};
inline PipelineApi pinnedPipelineApi(){return{
    reinterpret_cast<decltype(PipelineApi::clone)>(0x806AC0),reinterpret_cast<decltype(PipelineApi::destroy)>(0x805820),
    reinterpret_cast<decltype(PipelineApi::setLighting)>(0x7573C0),reinterpret_cast<decltype(PipelineApi::setRender)>(0x7573E0),expandedSkinCapture::readSelf};}
inline bool canonicalPipeline(const PipelineObservation&p,bool skin){
    if(!p.readable||p.entry!=0||p.stages.size()!=1||p.plugin!=(skin?0x116u:2u)||p.data!=(skin?1u:0u))return false;
    const auto&s=p.stages[0];if(!s.privateReadable||s.body!=(skin?0x7C7B90u:0x7575F0u))return false;
    const std::vector<uint32_t>expected=skin?std::vector<uint32_t>{0x756DF0,0x757400,0x7CB190,0x761170,0x761010,0x761030,0x761000}:
                                                    std::vector<uint32_t>{0x7578C0,0x758270,0x757400,0x756DF0};
    return s.privateWords==expected;
}
inline const char*staticEligibility(const Snapshot&s){
    if(!s.failure.empty()||!s.derivedWorldVerified||s.hierarchyFlags!=0x3000)return "unsupported_palette_space_or_capture";
    if(s.renderCallback!=0x7335B0)return "unverified_ped_atomic_callback";
    if(!canonicalPipeline(s.sourcePipeline,true)||!canonicalPipeline(s.defaultPipeline,false))return "modified_or_unknown_pipeline";
    if(!s.skinHardwareReadable||s.skinHardware!=1||!s.hardwareGlobalReadable||s.hardwareGlobal!=1)return "gpu_skin_path_not_observed";
    if(s.geometryFlags&~0xffu||!(s.geometryFlags&0x10)||s.uvs.size()>8||s.materials.empty()||s.materialEffects.size()!=s.materials.size())return "unsupported_geometry_streams";
    for(size_t i=0;i<s.materials.size();i++)if(s.materials[i].pipeline||s.materialEffects[i]!=0)return "custom_material_pipeline_or_effect";
    return nullptr;
}
class PipelineOwner {
    PipelineApi api_{};Api renderApi_{};Pipeline*pipeline_=nullptr;
    struct Context {Context*previous=nullptr;Atomic*atomic=nullptr;Api api{};Budget budget{};uint32_t passFlags=0;Result result{};bool called=false;};
    static inline thread_local Context*current_=nullptr;
    static Result invokePassFinally(Context*c,Atomic*atomic,Header*header){
        const uint32_t originalFlags=atomic->geometry->flags;
        __try{atomic->geometry->flags=c->passFlags;return render(c->api,atomic,header,reinterpret_cast<Mesh*>(header+1),c->budget);}
        __finally{atomic->geometry->flags=originalFlags;}
    }
    static void __cdecl dispatch(void*resource,void*object,uint8_t type,uint32_t){
        Context*c=current_;auto*atomic=static_cast<Atomic*>(object);
        if(!c||c->atomic!=atomic||type!=1||!resource)return;
        c->called=true;
        auto*header=reinterpret_cast<Header*>(static_cast<unsigned char*>(resource)+0x18);
        c->result=invokePassFinally(c,atomic,header);
    }
    static bool invokeFinally(Context*ctx,expandedSkinHost::Surface*surface,const expandedSkinHost::Identity*id,unsigned frame,
                              const expandedSkinHost::DrawContract*contract,expandedSkinHost::Draw original,Atomic**result){
        __try{return surface->drawExperimental(true,*id,frame,*contract,original,*result);}
        __finally{current_=ctx->previous;}
    }
public:
    explicit PipelineOwner(PipelineApi api,Api renderApi):api_(api),renderApi_(renderApi){}
    ~PipelineOwner(){if(pipeline_)api_.destroy(pipeline_);}
    PipelineOwner(const PipelineOwner&)=delete;PipelineOwner&operator=(const PipelineOwner&)=delete;
    void*get()const{return pipeline_;}
    bool prepare(const Snapshot&capture,bool nativeAbiVerified){
        if(pipeline_||!nativeAbiVerified||!api_.complete()||!renderApi_.complete()||!verifyNativeAbi(api_.read)||staticEligibility(capture))return false;
        expandedSkinCapture::Api observer{};observer.read=api_.read;
        auto*source=reinterpret_cast<void*>(capture.sourcePipeline.address);auto*base=reinterpret_cast<Pipeline*>(capture.defaultPipeline.address);
        if(!canonicalPipeline(expandedSkinCapture::observePipeline(observer,source),true)||!canonicalPipeline(expandedSkinCapture::observePipeline(observer,base),false))return false;
        Pipeline*p=api_.clone(base);if(!p||p==base)return false;
        const auto observed=expandedSkinCapture::observePipeline(observer,p);
        if(!canonicalPipeline(observed,false)){api_.destroy(p);return false;}
        Pipeline info{};if(!api_.read(&info,p,sizeof info)||info.count!=1||!info.nodes||info.nodes==base->nodes){api_.destroy(p);return false;}
        PipelineNode node{};if(!api_.read(&node,info.nodes,sizeof node)||node.privateData==reinterpret_cast<void*>(capture.defaultPipeline.stages[0].privateData)){api_.destroy(p);return false;}
        // Own node only: instancing remains canonical unskinned. Shader lights
        // are evaluated once by the ported GPU stage, not additionally as FFP.
        api_.setLighting(info.nodes,nullptr);api_.setRender(info.nodes,reinterpret_cast<void*>(&dispatch));
        uint32_t words[4]{};
        if(!api_.read(words,node.privateData,sizeof words)||words[0]!=0x7578C0||words[1]!=0x758270||words[2]!=0||words[3]!=reinterpret_cast<uintptr_t>(&dispatch)){api_.destroy(p);return false;}
        pipeline_=p;return true;
    }
    struct DrawResult {bool intercepted=false,callbackCalled=false;Result gpu{};};
    DrawResult draw(bool ownedTestGate,expandedSkinHost::Surface&surface,const expandedSkinHost::Identity&id,unsigned frame,
                    expandedSkinHost::DrawContract contract,void*sourceSkin,expandedSkinHost::Draw original,Atomic*&result){
        DrawResult out;
        if(!ownedTestGate||!pipeline_||!id.atomic||!sourceSkin||!original||!contract.ready()||contract.cpuPipeline!=pipeline_)return out;
        expandedSkinCapture::Api observer{};observer.read=api_.read;
        if(!canonicalPipeline(expandedSkinCapture::observePipeline(observer,contract.sourcePipeline),true))return out;
        uint32_t hw=0,bones=0,limit=0;
        // Skin+20 becomes1 at first instancing; do not cache its earlier0 as
        // permission for a CPU fallback. Source resources remain immutable.
        if(!api_.read(&hw,static_cast<char*>(sourceSkin)+0x20,4)||hw!=1||
           !api_.read(&bones,static_cast<char*>(sourceSkin)+0x1c,4)||bones>128||
           !api_.read(&limit,reinterpret_cast<void*>(0xC978EC),4)||!limit||limit>256)return out;
        contract.stableInstanceDeclaration=true;
        Context context{current_,id.atomic,renderApi_,{bones,limit},id.atomic->geometry->flags};current_=&context;
        out.intercepted=invokeFinally(&context,&surface,&id,frame,&contract,original,&result);
        out.callbackCalled=context.called;out.gpu=context.result;return out;
    }
};
} // namespace expandedGpu
