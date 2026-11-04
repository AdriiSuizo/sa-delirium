#pragma once
// Included after VehicleEntries.hpp. All mutations are owned TEST_ONLY render
// resources; native gameplay geometry and the external Claude tree stay intact.
#include "ExpandedSkinTestHooks.hpp"
static expandedSkinTest::Manager*expandedTestManager=nullptr; // explicit RW lifetime, no DllMain destructor
static bool expandedTestHooksReady=false;
struct ExpandedTestAuthored {
    int ref=-1,model=-1;void*ped=nullptr,*clump=nullptr;unsigned frame=0;uint64_t serial=0;
    M root{};std::vector<M>palette;
};
static ExpandedTestAuthored expandedTestAuthored;
static int expandedTestActor=-1;static unsigned expandedTestDrawReport=0;
static bool resolveExpandedTestActor(void*p,expandedSkinTest::Actor&out){
    const int ref=reference(p);if(ref<0||pedFromRef(ref)!=p||!matrix(p)||!at<void*>(p,0x18))return false;
    const auto*e=currentNativePaletteEvaluation(p);
    out={p,at<void*>(p,0x18),ref,at<short>(p,0x22),*reinterpret_cast<unsigned*>(0xB7CB4C),e?e->serial:0,*matrix(p)};
    return expandedSkin::affineFinite(out.root);
}
static void expandedTestReport(const char*name,int ref,const std::string&detail){
    void*p=pedFromRef(ref);event(name,ref,p?at<short>(p,0x22):-1,detail);
}
static void observeExpandedTestDraw(expandedSkinTest::Atomic*){
    if(!expandedTestManager)return;const auto&id=expandedTestManager->actor();const auto&stats=expandedTestManager->stats();
    if(stats.draws!=1&&gameTime()-expandedTestDrawReport<100)return;expandedTestDrawReport=gameTime();
    auto it=agents.find(id.reference);if(it==agents.end()||it->second->ped!=id.ped)return;auto&a=*it->second;
    const auto&out=expandedTestManager->published();auto*h=hierarchy(a.ped);float publicationError=0;
    if(h&&size_t(h->num)==out.nativeWorld.size())for(size_t i=0;i<out.nativeWorld.size();i++){
        const auto&x=h->matrices[i];const auto&y=out.nativeWorld[i];publicationError=std::max({publicationError,length(x.pos-y.pos),length(x.right-y.right),length(x.up-y.up),length(x.at-y.at)});
    }else publicationError=-1;
    event("expanded_skin_test_draw",a.ref,a.model,"frame="+std::to_string(id.frame)+" native_serial="+std::to_string(id.nativeSerial)+
        " generation="+std::to_string(stats.generation)+" draws="+std::to_string(stats.draws)+" normal_draws="+std::to_string(stats.normalDraws)+" shadow_draws="+std::to_string(stats.shadowDraws)+
        " vertices="+std::to_string(out.vertices.size())+" virtual_joints=2 max_influences=8 native_palette_error="+std::to_string(publicationError)+
        " mapped_origin_residual="+std::to_string(stats.originResidual)+" box_excess="+std::to_string(stats.boxExcess)+" sphere_excess="+std::to_string(stats.sphereExcess)+
        " weapon_present="+std::to_string(at<void*>(a.ped,0x4F4)!=nullptr)+" real_gpu_dispatch=1 test_only=1");
    float points[12]{};const int mask=nm_agent_grab_points(a.agent,points);
    for(int side=0;side<2;side++)if(mask&(1<<side)){
        const int part=side?20:16,node=a.rig->node[part],tag=side?24:34;const float*v=points+side*6;
        const V held{v[0],v[1],v[2]},target{v[3],v[4],v[5]};V closest{};float surfaceGap=-1;unsigned triangles=0;
        const bool actualSurface=expandedTestManager->handSurfaceClosest(tag,target,closest,surfaceGap,triangles);
        const T bone=transform(out.nativeWorld[node]);const V palettePalm=gripPointOnSkin(held,a.current[part],a.rig->bodyToBone[node],bone);
        event("expanded_skin_test_hand",a.ref,a.model,"frame="+std::to_string(id.frame)+" side="+std::to_string(side)+" solver_gap="+std::to_string(length(held-target))+
            " palette_binding_gap="+std::to_string(length(palettePalm-target))+" palette_vs_solver="+std::to_string(length(palettePalm-held))+
            " surface_valid="+std::to_string(actualSurface)+" actual_hand_triangle_gap="+std::to_string(surfaceGap)+" hand_triangles="+std::to_string(triangles)+
            " surface_point="+std::to_string(closest.x)+","+std::to_string(closest.y)+","+std::to_string(closest.z)+" same_pose_draw=1");
    }
}
static void clearExpandedSkinAdapterTest(){
    expandedSkinTest::detachTestManager();if(expandedTestManager){delete expandedTestManager;expandedTestManager=nullptr;}
    expandedTestAuthored={};expandedTestActor=-1;expandedTestDrawReport=0;
}
static void captureExpandedSkinAuthored(void*p){
    if(!expandedTestHooksReady||!expandedSkinTest::environmentGate())return;
    const int ref=reference(p);const bool fixture=ref>=0&&(ref==entryProbeRef||ref==expandedTestActor);
    if(!fixture)return;
    if(expandedTestActor>=0&&ref!=expandedTestActor)clearExpandedSkinAdapterTest();
    if(!expandedTestManager){
        expandedSkinTest::HostApi host{resolveExpandedTestActor,[](){return *reinterpret_cast<unsigned*>(0xB7CB4C);},expandedSkinRuntimeDefaultPipeline,expandedSkinTest::environmentGate,expandedTestReport,observeExpandedTestDraw};
        expandedTestManager=new expandedSkinTest::Manager(host,expandedSkinCapture::pinnedApi(),expandedSkinHost::pinnedApi(),expandedGpu::pinnedPipelineApi(),expandedGpu::pinnedApi());
        expandedSkinTest::activeManager=expandedTestManager;
    }
    if(!expandedTestManager->select(p))return;expandedTestActor=ref;
    const auto*evaluation=currentNativePaletteEvaluation(p);auto*h=hierarchy(p);
    if(!evaluation||!h||!h->matrices||h->num<20||h->num>128||!matrix(p))return;
    std::vector<M> full(h->matrices,h->matrices+h->num);
    if(!std::all_of(full.begin(),full.end(),expandedSkin::affineFinite))return;
    expandedTestAuthored={ref,at<short>(p,0x22),p,at<void*>(p,0x18),evaluation->frame,evaluation->serial,*matrix(p),std::move(full)};
}
static const expandedSkin::FrameOutput*stageExpandedSkinAdapterTest(Active&a,float weight,bool animatedRecovery){
    if(!expandedTestManager||a.ref!=expandedTestActor||a.kind!=Reaction::DoorEntry)return nullptr;
    expandedTestManager->invalidateFrame();const auto*evaluation=currentNativePaletteEvaluation(a.ped);const auto&s=expandedTestAuthored;
    if(!evaluation||s.ref!=a.ref||s.ped!=a.ped||s.clump!=at<void*>(a.ped,0x18)||s.model!=a.model||s.serial!=evaluation->serial||s.frame!=evaluation->frame||s.palette.size()!=a.rig->ids.size()){
        event("expanded_skin_test_refused",a.ref,a.model,"authored_full_matrix_epoch_missing=1");return nullptr;
    }
    if(!expandedTestManager->prepare(*a.rig))return nullptr;
    // Rebase the saved authored palette after host root/heading correction,
    // using the same native evaluation, including all affine facial helpers.
    M previousInverse{};if(!affineInverse(s.root,previousInverse))return nullptr;const M delta=affineProduct(*matrix(a.ped),previousInverse);
    std::vector<M> authored=s.palette;for(auto&m:authored)m=affineProduct(delta,m);
    auto parts=a.current;
    if(animatedRecovery){
        const int pelvis=a.rig->node[0];if(pelvis<0||size_t(pelvis)>=a.frozenBones.size())return nullptr;
        // Keep BOTH physical virtual joints through BlendFromNM. frozenBones
        // carries the existing host's later rigid transport; never infer the
        // missing torso DOFs from those native bones.
        const T originalPelvis=mul(parts[0],a.rig->bodyToBone[pelvis]);
        const T transport=mul(a.frozenBones[pelvis],inverse(originalPelvis));for(auto&part:parts)part=mul(transport,part);
    }
    return expandedTestManager->stage(authored.data(),parts,weight,a.preview||a.mounted);
}
static void maintainExpandedSkinAdapterTest(){
    if(!expandedTestManager)return;
    if(!expandedSkinTest::environmentGate()||pedFromRef(expandedTestActor)!=expandedTestManager->actor().ped||
       (entryProbeRef!=expandedTestActor&&!agents.contains(expandedTestActor))){clearExpandedSkinAdapterTest();return;}
    expandedTestManager->invalidateFrame();
}
static bool installExpandedSkinAdapterTest(){
    if(!expandedSkinTest::environmentGate())return true;
    if(!expandedSkinCaptureAbi())return false;
    const expandedSkinTest::HookApi hooks{
        [](void*t,void*r,void**o){return MH_CreateHook(t,r,o)==MH_OK;},
        [](void*t){return MH_RemoveHook(t)==MH_OK;},[](void*t){return MH_QueueEnableHook(t)==MH_OK;},expandedSkinCapture::readSelf};
    expandedTestHooksReady=expandedSkinTest::queueTestHooks(hooks,true);
    event(expandedTestHooksReady?"expanded_skin_test_queued":"expanded_skin_test_install_refused",-1,-1,"opt_in=1 one_fixture_actor=1 production_enabled=0");return expandedTestHooksReady;
}
