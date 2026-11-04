#pragma once
// TEST_ONLY integration, never installed or selected implicitly. The parent
// owns hook installation and final HAnim/attachment publication as one commit.
#include "ExpandedRigBridge.hpp"
#include "ExpandedGpuPipelineHost.hpp"
#include "ExpandedSkinCulling.hpp"
#include "ExpandedSkinTestAbi.hpp"
#include "ExpandedSkinSurfaceDistance.hpp"
#include <memory>

namespace expandedSkinTest {
using Atomic=expandedSkinHost::Atomic;using Draw=expandedSkinHost::Draw;
using Visibility=int(__cdecl*)(void*,float*);
struct Actor {
    void*ped=nullptr,*clump=nullptr;int reference=-1,model=-1;unsigned frame=0;
    uint64_t nativeSerial=0;M root{};
    bool same(const Actor&b)const{return ped==b.ped&&clump==b.clump&&reference==b.reference&&model==b.model;}
};
struct HostApi {
    bool(*resolve)(void*,Actor&)=nullptr;
    unsigned(*frame)()=nullptr;
    void*(*defaultPipeline)()=nullptr;
    bool(*ownedTestGate)()=nullptr;
    void(*report)(const char*,int,const std::string&)=nullptr;
    void(*onDraw)(Atomic*)=nullptr;
};
// Structurally identical to the first40 bytes of the MODEL COL, not the
// mutable per-instance ped hit model returned by CEntity::GetColModel.
struct ModelBounds {V min,max,centre;float radius;};
struct ObservationStats {
    unsigned staged=0,draws=0,normalDraws=0,shadowDraws=0,refused=0;
    unsigned visibilityFrame=0,publishedFrame=0;uint64_t generation=0;
    float boxExcess=0,sphereExcess=0,originResidual=0;
};
class Manager {
    HostApi host_;expandedSkinCapture::Api captureApi_;expandedSkinHost::Api surfaceApi_;
    expandedGpu::PipelineApi pipelineApi_;expandedGpu::Api renderApi_;
    Actor actor_{};uint64_t nextGeneration_=0;bool selected_=false,committed_=false;
    std::unique_ptr<expandedSkinHost::Surface>surface_;
    std::unique_ptr<expandedGpu::PipelineOwner>pipeline_;
    expandedSkinHost::Identity id_{};expandedSkinCapture::Snapshot source_;
    expandedSkin::PoseBridge bridge_;expandedSkin::Binding binding_;
    expandedSkin::FrameOutput output_;M atomicWorld_{};NativeEnvelope visibility_{};
    ObservationStats stats_{};std::string lastFailure_;
    void fail(const char*why){++stats_.refused;if(lastFailure_!=why){lastFailure_=why;if(host_.report)host_.report("expanded_skin_test_refused",actor_.reference,why);}}
    bool current(Actor&out)const{return selected_&&host_.resolve&&host_.resolve(actor_.ped,out)&&out.same(actor_);}
    bool sourceStillCurrent()const{
        if(!id_.atomic||!id_.geometry)return false;Atomic atomic{};
        return captureApi_.read(&atomic,id_.atomic,sizeof atomic)&&atomic.clump==id_.clump&&atomic.geometry==id_.geometry&&
            captureApi_.hierarchy(id_.atomic)==id_.nativeHierarchy&&captureApi_.skin(id_.geometry)==reinterpret_cast<void*>(source_.skin)&&
            atomic.pipeline==reinterpret_cast<void*>(source_.sourcePipeline.address);
    }
    static void*__cdecl collect(void*a,void*data){auto&atoms=*static_cast<std::vector<Atomic*>*>(data);if(atoms.size()==32)return nullptr;atoms.push_back(static_cast<Atomic*>(a));return a;}
public:
    Manager(HostApi host,expandedSkinCapture::Api capture,expandedSkinHost::Api surface,expandedGpu::PipelineApi pipeline,expandedGpu::Api render):
        host_(host),captureApi_(capture),surfaceApi_(surface),pipelineApi_(pipeline),renderApi_(render){}
    ~Manager(){clear();}
    Manager(const Manager&)=delete;Manager&operator=(const Manager&)=delete;
    void clear(){committed_=false;surface_.reset();pipeline_.reset();source_={};id_={};output_={};visibility_={};selected_=false;lastFailure_.clear();}
    // Call only on an explicit owned test actor. One selected generation at a
    // time; resource teardown happens while RW is alive, never in DLL detach.
    bool select(void*ped){
        if(!host_.ownedTestGate||!host_.ownedTestGate()||!host_.resolve||!captureApi_.read)return false;
        Actor a;if(!host_.resolve(ped,a)||a.reference<0||!a.clump||!expandedSkin::affineFinite(a.root))return false;
        if(selected_&&a.same(actor_))return true;
        clear();actor_=a;selected_=true;stats_={};stats_.generation=++nextGeneration_;return true;
    }
    bool observes(void*ped)const{return selected_&&ped==actor_.ped;}
    void invalidateFrame(){committed_=false;}
    const ObservationStats&stats()const{return stats_;}
    const Actor&actor()const{return actor_;}
    const expandedSkin::FrameOutput&published()const{return output_;}
    bool handSurfaceClosest(int handTag,V target,V&closest,float&gap,unsigned&triangles)const{
        triangles=0;gap=std::numeric_limits<float>::infinity();
        if(!committed_||!finite(target)||output_.vertices.size()!=source_.source.size())return false;
        const auto handVertex=[&](unsigned vertex){for(const auto&w:source_.weights[vertex])if(w.value>0&&w.bone<source_.nodes.size()){
            const int tag=source_.nodes[w.bone].id;if(tag==handTag||tag==handTag+1||tag==handTag+2)return true;}return false;};
        for(const auto&t:source_.triangles)if(handVertex(t.vertex[0])||handVertex(t.vertex[1])||handVertex(t.vertex[2])){
            const V a=point(atomicWorld_,output_.vertices[t.vertex[0]].position),b=point(atomicWorld_,output_.vertices[t.vertex[1]].position),c=point(atomicWorld_,output_.vertices[t.vertex[2]].position);
            const V q=triangleClosest(target,a,b,c);const float distance=length(q-target);++triangles;if(distance<gap){gap=distance;closest=q;}
        }return triangles&&std::isfinite(gap);
    }
    // Before the original SetupEntityVisibility. Read the same model table
    // operand used by that function; limit-adjusted table relocation is okay,
    // a changed instruction is not. Parent must verify the entry hook ABI.
    void beforeVisibility(void*ped){
        if(!observes(ped))return;committed_=false;visibility_={};Actor a;if(!current(a)){fail("actor_generation_changed");return;}
        unsigned char opcode[3]{};uintptr_t table=0,model=0,col=0;ModelBounds bounds{};
        if(!captureApi_.read(opcode,reinterpret_cast<void*>(0x55423D),3)||opcode[0]!=0x8b||opcode[1]!=0x3c||opcode[2]!=0x85||
           !captureApi_.read(&table,reinterpret_cast<void*>(0x554240),4)||!table||a.model<0||
           !captureApi_.read(&model,reinterpret_cast<void*>(table+size_t(a.model)*4),4)||!model||
           !captureApi_.read(&col,reinterpret_cast<void*>(model+0x14),4)||!col||!captureApi_.read(&bounds,reinterpret_cast<void*>(col),sizeof bounds)){
            fail("native_visibility_model_bounds_unreadable");return;
        }
        visibility_={a.frame,stats_.generation,true,a.root,bounds.min,bounds.max,bounds.centre,bounds.radius};
        if(!validEnvelope(visibility_)){visibility_={};fail("native_visibility_model_bounds_invalid");return;}stats_.visibilityFrame=a.frame;
    }
    template<class RigType>bool prepare(const RigType&rig){
        if(!selected_||!host_.ownedTestGate()||!host_.defaultPipeline||!captureApi_.complete()||!surfaceApi_.complete())return false;
        Actor a;if(!current(a)||!a.nativeSerial){fail("missing_current_native_evaluation");return false;}
        if(surface_){if(sourceStillCurrent())return true;clear();fail("source_generation_changed_reselect_required");return false;}
        std::vector<Atomic*>atoms;reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(a.clump,&collect,&atoms);
        // First live proof is one assembled CJ/model7 atomic; multi-atomic
        // clothing will need an all-surfaces publication transaction.
        if(atoms.size()!=1){fail("test_scope_requires_one_assembled_atomic");return false;}
        expandedSkinCapture::Token token{a.reference,a.model,a.frame,a.nativeSerial,stats_.generation,a.clump,atoms[0]};
        expandedSkinCapture::Snapshot s;
        if(!expandedSkinCapture::capture(captureApi_,token,a.frame,host_.defaultPipeline(),s)){fail(s.failure.c_str());return false;}
        s.normaliseGlobalReadable=captureApi_.read(&s.normaliseGlobal,reinterpret_cast<void*>(0xC978E0),4);
        s.hardwareGlobalReadable=captureApi_.read(&s.hardwareGlobal,reinterpret_cast<void*>(0xC978E4),4);
        if(const auto*reason=expandedGpu::staticEligibility(s)){fail(reason);return false;}
        if(!expandedSkin::prepareFromRig(rig,s.inverseBind.data(),s.inverseBind.size(),bridge_)){fail("rig_expansion_rejected");return false;}
        std::vector<expandedSkin::Influences>weights;
        if(!bridge_.expandWeights(s.source.data(),s.weights.data(),s.source.size(),weights)||
           !binding_.prepare(s.source.data(),weights.data(),weights.size(),bridge_.count())){fail("eight_weight_binding_rejected");return false;}
        expandedSkinHost::Identity id{a.reference,a.model,a.clump,atoms[0],reinterpret_cast<expandedSkinHost::Geometry*>(s.geometry),reinterpret_cast<void*>(s.hierarchy),stats_.generation};
        auto surface=std::make_unique<expandedSkinHost::Surface>(surfaceApi_);auto pipeline=std::make_unique<expandedGpu::PipelineOwner>(pipelineApi_,renderApi_);
        if(!surface->prepare(id)||!pipeline->prepare(s,true)){fail("owned_render_resources_rejected");return false;}
        source_=std::move(s);id_=id;surface_=std::move(surface);pipeline_=std::move(pipeline);return true;
    }
    // Returns staged ALL outputs, but does not publish HAnim. Parent must use
    // this nativeWorld subset for attachments, and pose.world[n,n+1] for the
    // two virtual joint frames. Never call Rig::jointFrames to reconstruct them.
    const expandedSkin::FrameOutput*stage(const M*authoredWorld,const std::array<T,21>&physical,float blend,bool upperOnly){
        committed_=false;Actor a;
        if(!selected_||!host_.ownedTestGate()||!surface_||!pipeline_||!current(a)||!a.nativeSerial||!sourceStillCurrent()){fail("stage_identity_or_resources_invalid");return nullptr;}
        expandedSkinCapture::Frame*frame=nullptr;std::vector<expandedSkinCapture::FrameObservation>chain;
        if(!captureApi_.read(&frame,id_.atomic->frameObject+4,4)||!expandedSkinCapture::deriveFrameWorld(captureApi_,frame,atomicWorld_,chain)){fail("current_atomic_world_unverified");return nullptr;}
        expandedSkin::FrameOutput staged;
        if(!expandedSkin::prepareFrame(bridge_,binding_,authoredWorld,physical,blend,upperOnly,atomicWorld_,staged)){fail("frame_expansion_rejected");return nullptr;}
        if(!visibility_.valid||visibility_.frame!=a.frame||visibility_.generation!=stats_.generation){fail("native_visibility_epoch_missing");return nullptr;}
        const auto bound=withinNativeEnvelope(visibility_,a.frame,stats_.generation,staged.vertices.data(),staged.vertices.size(),atomicWorld_);
        stats_.boxExcess=bound.boxExcess;stats_.sphereExcess=bound.sphereExcess;
        if(!bound.okay){fail("expanded_stream_outside_observed_native_culling_envelope");return nullptr;}
        // The clump frustum callback has a second sphere test at draw time.
        // It uses the same COL sphere transformed by the clump root LTM.
        expandedSkinCapture::Frame*clumpFrame=nullptr;M clumpWorld{};
        if(!captureApi_.read(&clumpFrame,static_cast<char*>(a.clump)+4,4)||!expandedSkinCapture::deriveFrameWorld(captureApi_,clumpFrame,clumpWorld,chain)){
            fail("clump_culling_world_unverified");return nullptr;
        }
        int32_t clumpPlugin=0;void*clumpModel=nullptr,*clumpCol=nullptr;ModelBounds clumpBounds{};
        // Exact ClumpFrustumSphereCB732A40: model is a plugin on root frame.
        // It need not equal the model pointer read in entity visibility.
        if(!captureApi_.read(&clumpPlugin,reinterpret_cast<void*>(0x8D6090),4)||
           !captureApi_.read(&clumpModel,reinterpret_cast<char*>(clumpFrame)+clumpPlugin,4)||!clumpModel||
           !captureApi_.read(&clumpCol,static_cast<char*>(clumpModel)+0x14,4)||!clumpCol||
           !captureApi_.read(&clumpBounds,clumpCol,sizeof clumpBounds)||!finite(clumpBounds.centre)||
           !std::isfinite(clumpBounds.radius)||clumpBounds.radius<=0){fail("clump_culling_model_unverified");return nullptr;}
        const V clumpCentre=point(clumpWorld,clumpBounds.centre);
        for(const auto&v:staged.vertices)if(length(point(atomicWorld_,v.position)-clumpCentre)>clumpBounds.radius){fail("expanded_stream_outside_native_clump_sphere");return nullptr;}
        if(!surface_->upload(id_,a.frame,staged.vertices.data(),staged.vertices.size(),staged.bounds)){fail("owned_stream_upload_failed");return nullptr;}
        actor_=a;output_=std::move(staged);stats_.publishedFrame=a.frame;stats_.originResidual=output_.pose.mappedOriginResidual;++stats_.staged;return &output_;
    }
    // Only after caller published nativeWorld + attachments in the same frame.
    bool commit(unsigned frame,uint64_t nativeSerial){
        Actor a;if(!current(a)||a.frame!=frame||a.nativeSerial!=nativeSerial||stats_.publishedFrame!=frame||output_.nativeWorld.empty())return false;
        committed_=true;return true;
    }
    bool draw(Atomic*atomic,Draw original,Atomic*&result){
        if(!selected_||atomic!=id_.atomic)return false;Actor a;
        if(!committed_||!host_.ownedTestGate()||!current(a)||a.frame!=stats_.publishedFrame||a.nativeSerial!=actor_.nativeSerial||!sourceStillCurrent()){
            fail("draw_epoch_or_commit_invalid");return false;
        }
        expandedSkinCapture::Frame*frame=nullptr;M world{};std::vector<expandedSkinCapture::FrameObservation>chain;
        if(!captureApi_.read(&frame,atomic->frameObject+4,4)||!expandedSkinCapture::deriveFrameWorld(captureApi_,frame,world,chain)||!sameAffine(world,atomicWorld_)){
            fail("atomic_world_changed_after_publication");return false;
        }
        expandedSkinHost::DrawContract contract{reinterpret_cast<void*>(source_.sourcePipeline.address),pipeline_->get(),true,true,true,true,true};
        const uint32_t flags=atomic->geometry->flags;
        const auto outcome=pipeline_->draw(true,*surface_,id_,a.frame,contract,reinterpret_cast<void*>(source_.skin),original,result);
        if(!outcome.intercepted||!outcome.callbackCalled||!outcome.gpu.okay){fail(outcome.gpu.failure?outcome.gpu.failure:"gpu_dispatch_not_completed");return outcome.intercepted;}
        ++stats_.draws;if(flags&0x10)++stats_.normalDraws;else ++stats_.shadowDraws;
        if(host_.onDraw)host_.onDraw(atomic);
        if(stats_.draws==1&&host_.report)host_.report("expanded_skin_test_first_draw",a.reference,"eight_influences_preserved=1 native_resources_restored=1 not_gameplay_acceptance=1");return true;
    }
};
} // namespace expandedSkinTest
