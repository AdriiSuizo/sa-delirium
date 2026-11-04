#pragma once
#include "CodePatch.hpp"
#include "NativePaletteEpoch.hpp"
// A finite skin palette does not identify when it was evaluated. Capture the
// native animation evaluation exactly once, then transport its local pose by
// the latest actual rider/parent pair observed THIS frame. Native controls may
// test bike collision before the rider's SetPedPositionInCar; the authored
// evaluation already exposes a coherent current root/parent pair before that
// control pass. No PreRender, animation
// timestep, callback, or guessed seat placement is replayed at a contact.
struct BikeAuthoredPose {
    int ped=-1,vehicle=-1,model=-1;void*clump=nullptr;unsigned frame=0;
    std::vector<int> ids;std::vector<T> local;
};
enum class BikeRootSource {None,AuthoredEvaluation,NativeSeatPlacement};
static const char*bikeRootSourceName(BikeRootSource source){
    switch(source){case BikeRootSource::AuthoredEvaluation:return "authored_evaluation";case BikeRootSource::NativeSeatPlacement:return "native_seat_placement";default:return "none";}
}
struct BikeSeatPose {int vehicle=-1;void*clump=nullptr;unsigned frame=0;T parent{},root{};BikeRootSource source=BikeRootSource::None;uint64_t order=0,nativeSerial=0;};
struct BikeContactPose {
    int ped=-1,vehicle=-1,model=-1;void*clump=nullptr;
    SeedEpoch epoch{};T root{};std::vector<int> ids;std::vector<T> bones;
    BikeRootSource rootSource=BikeRootSource::None;unsigned rootFrame=0;uint64_t rootOrder=0,nativeSerial=0;
};
struct BikePoseProgress {unsigned frame=0,authoredCalls=0,seatCalls=0;uint64_t authoredOrder=0,seatOrder=0;const char*authoredResult="not_observed";};
struct BikePoseDiagnostic {
    int ped=-1,vehicle=-1,pedArea=-1,worldArea=-1;unsigned frame=0,registryFrame=0,authoredFrame=0,seatFrame=0,nativeFrame=0;
    uint64_t nativeSerial=0;bool registered=false,requested=false;BikePoseProgress progress{};
    BikeRootSource rootSource=BikeRootSource::None;uint64_t rootOrder=0;
    const char*reason="not_captured";
};
static std::map<int,BikePoseProgress> bikePoseProgress;
static uint64_t bikePoseObservationOrder=0;
static std::map<void*,int> bikePoseClumps;
static std::map<int,BikeAuthoredPose> bikeAuthoredPoses;
static std::map<int,BikeSeatPose> bikeSeatPoses;
static uint64_t bikePoseSerial=0;
static float bikePoseFrameSeconds=0;
static unsigned bikePoseFrame=~0u;
static void beginBikePoseFrame(){
    bikePoseFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    bikePoseFrameSeconds=*reinterpret_cast<float*>(0xB7CB5C)*.02f;
    bikePoseClumps.clear();bikeAuthoredPoses.clear();bikeSeatPoses.clear();bikePoseProgress.clear();
}
static void registerBikePoseRider(void*ped,void*bike){
    if(!ped||reference(ped)<0||!ridingBike(ped)||at<void*>(ped,0x58C)!=bike||!matrix(ped))return;
    if(void*clump=at<void*>(ped,0x18))bikePoseClumps[clump]=reference(ped);
}
static SeedEpoch bikePoseEpoch(SeedPhase phase){
    return {*reinterpret_cast<unsigned*>(0xB7CB4C),++bikePoseSerial,phase,
        *reinterpret_cast<float*>(0xB7CB5C)*.02f,bikePoseFrameSeconds};
}
static void* trackedBikePosePed(void*clump){
    const auto i=bikePoseClumps.find(clump);if(i==bikePoseClumps.end())return nullptr;
    void*p=pedFromRef(i->second);
    if(!p||at<void*>(p,0x18)!=clump||!ridingBike(p)||!matrix(p)||
       vehicleRef(at<void*>(p,0x58C))<0||*reinterpret_cast<unsigned*>(0xB7CB4C)!=bikePoseFrame)return nullptr;
    return p;
}
static void recordBikeAuthoredPose(void*p){
    if(!p||reference(p)<0)return;
    auto&progress=bikePoseProgress[reference(p)];progress.frame=bikePoseFrame;progress.authoredCalls++;progress.authoredOrder=++bikePoseObservationOrder;
    if(!ridingBike(p)){progress.authoredResult="not_riding";return;}
    if(!currentNativePaletteEvaluation(p)){progress.authoredResult="no_current_native_evaluation";return;}
    void*clump=at<void*>(p,0x18);
    // An occupant can spawn after the pre-process registry snapshot. The owner
    // is supplied by CEntity::UpdateAnim itself, so this is not a clump guess.
    registerBikePoseRider(p,at<void*>(p,0x58C));
    auto*h=hierarchy(p);if(!h||!h->matrices||!h->nodes||h->num<20||h->num>128){progress.authoredResult="invalid_hierarchy";return;}
    if(!publishCurrentNativePalette(p)){progress.authoredResult="publication_rejected";return;}
    BikeAuthoredPose pose;pose.ped=reference(p);pose.vehicle=vehicleRef(at<void*>(p,0x58C));
    pose.model=at<short>(p,0x22);pose.clump=clump;pose.frame=bikePoseFrame;
    const T observedRoot=transform(*matrix(p));const T rootInverse=inverse(observedRoot);
    for(int i=0;i<h->num;i++){
        const T bone=transform(h->matrices[i]);
        if(!finite(bone.p)||length(bone.p-position(p))>6){progress.authoredResult="invalid_world_bone";return;}
        pose.ids.push_back(h->nodes[i].id);pose.local.push_back(mul(rootInverse,bone));
    }
    // World::Process updates all animations before processing the moving list.
    // CBike can test collision inside its control pass, before this rider gets
    // SetPedPositionInCar. Preserve the actual pair at this single native
    // evaluation instead of requiring a future seat callback or guessing one.
    void*bike=at<void*>(p,0x58C);
    if(matrix(bike)){
        const T observedParent=transform(*matrix(bike));
        const auto*evaluation=currentNativePaletteEvaluation(p);
        if(finite(observedRoot.p)&&finite(observedParent.p)&&evaluation)
            bikeSeatPoses[pose.ped]={pose.vehicle,clump,bikePoseFrame,observedParent,observedRoot,
                BikeRootSource::AuthoredEvaluation,progress.authoredOrder,evaluation->serial};
    }
    bikeAuthoredPoses[pose.ped]=std::move(pose);
    progress.authoredResult="captured";
}
static void(__thiscall*originalBikeSeatPosition)(void*)=nullptr;
static void __fastcall bikeNativeSeatPosition(void*p,void*){
    originalBikeSeatPosition(p);
    if(ready&&p&&reference(p)>=0&&ridingBike(p)){
        auto&progress=bikePoseProgress[reference(p)];progress.frame=bikePoseFrame;progress.seatCalls++;progress.seatOrder=++bikePoseObservationOrder;
    }
    if(!ready||!p||trackedBikePosePed(at<void*>(p,0x18))!=p)return;
    void*bike=at<void*>(p,0x58C);if(!matrix(bike))return;
    const auto*evaluation=currentNativePaletteEvaluation(p);
    bikeSeatPoses[reference(p)]={vehicleRef(bike),at<void*>(p,0x18),bikePoseFrame,
        transform(*matrix(bike)),transform(*matrix(p)),BikeRootSource::NativeSeatPlacement,
        bikePoseProgress[reference(p)].seatOrder,evaluation?evaluation->serial:0};
}
static std::shared_ptr<BikeContactPose> captureBikeContactPose(void*p,void*bike,const SeedEpoch&epoch,bool contact,BikePoseDiagnostic*diagnostic=nullptr){
    BikePoseDiagnostic local;auto&d=diagnostic?*diagnostic:local;d={};d.frame=epoch.frame;d.registryFrame=bikePoseFrame;d.vehicle=vehicleRef(bike);
    if(!p||!matrix(p)||!matrix(bike)||!ridingBike(p)||at<void*>(p,0x58C)!=bike){d.reason="rider_or_vehicle_invalid";return {};}
    d.ped=reference(p);d.pedArea=at<unsigned char>(p,0x2F);d.worldArea=area();d.requested=nativePaletteRequested(p);
    d.registered=bikePoseClumps.find(at<void*>(p,0x18))!=bikePoseClumps.end();
    if(auto pi=bikePoseProgress.find(d.ped);pi!=bikePoseProgress.end())d.progress=pi->second;
    if(auto ni=nativePaletteEvaluations.find(d.ped);ni!=nativePaletteEvaluations.end()){d.nativeFrame=ni->second.frame;d.nativeSerial=ni->second.serial;}
    const int ref=reference(p),vehicle=vehicleRef(bike);const auto i=bikeAuthoredPoses.find(ref);
    if(i==bikeAuthoredPoses.end()){d.reason="authored_snapshot_missing";return {};}
    const auto&a=i->second;
    d.authoredFrame=a.frame;
    if(a.frame!=epoch.frame||a.vehicle!=vehicle||a.clump!=at<void*>(p,0x18)||a.model!=at<short>(p,0x22)){d.reason="authored_identity_or_epoch_mismatch";return {};}
    T root=transform(*matrix(p));BikeSeatPose source;
    if(contact){
        const auto s=bikeSeatPoses.find(ref);
        if(s==bikeSeatPoses.end()){d.reason="seat_snapshot_missing";return {};}
        d.seatFrame=s->second.frame;
        if(s->second.frame!=epoch.frame||s->second.vehicle!=vehicle||s->second.clump!=a.clump){d.reason="seat_identity_or_epoch_mismatch";return {};}
        source=s->second;d.rootSource=source.source;d.rootOrder=source.order;
        // CPhysical tests a candidate vehicle matrix before applying impulse;
        // the rider's current native root still belongs to the observed parent
        // epoch. Carry that measured attachment, not a calculated seat offset.
        root=mul(mul(transform(*matrix(bike)),inverse(s->second.parent)),s->second.root);
    }
    auto out=std::make_shared<BikeContactPose>();out->ped=ref;out->vehicle=vehicle;out->model=a.model;
    out->clump=a.clump;out->epoch=epoch;out->root=root;out->ids=a.ids;
    if(contact){out->rootSource=source.source;out->rootFrame=source.frame;out->rootOrder=source.order;out->nativeSerial=source.nativeSerial;}
    for(const auto&local:a.local)out->bones.push_back(mul(root,local));
    d.reason="captured";
    return out;
}
static std::shared_ptr<PoseSeed> fitBikeContactPose(void*p,const BikeContactPose&sample,std::string*reason=nullptr){
    const auto rejected=[&](const std::string&why)->std::shared_ptr<PoseSeed>{if(reason)*reason=why;return {};};
    if(!p||reference(p)!=sample.ped||at<void*>(p,0x18)!=sample.clump||at<short>(p,0x22)!=sample.model)return rejected("fit_actor_identity_mismatch");
    try{
        Rig rig(p,sourceBuild);if(rig.ids!=sample.ids||sample.bones.size()!=rig.ids.size())return rejected("fit_hierarchy_mismatch");
        for(size_t n=0;n<sample.bones.size();n++)if(rig.parents[n]>=0){
            const float expected=length(rig.bindLocal[n].p),actual=length(sample.bones[n].p-sample.bones[rig.parents[n]].p);
            if(expected>.03f&&(actual<expected*.35f||actual>expected*2.f))return rejected("fit_bone_length_rejected bone="+std::to_string(rig.ids[n])+" expected="+std::to_string(expected)+" actual="+std::to_string(actual));
        }
        auto pose=std::make_shared<PoseSeed>();pose->vehicle=sample.vehicle;pose->bones=sample.bones;
        const auto joints=rig.jointFrames(pose->bones);
        for(int n=0;n<21;n++)pose->parts[n]=mul(joints[n],rig.offsets[n]);
        pose->poseEpoch=sample.epoch;pose->sampledRoot=sample.root;if(reason)*reason="fitted";return pose;
    }catch(const std::exception&e){event("bike_contact_pose_fit_failed",sample.ped,sample.model,e.what());return rejected("fit_exception");}
}
static bool installBikePoseSampling(){
    // Owned executable: unique CALL in CEntity::UpdateAnim. The seat entry has
    // no code-as-data aliases; only its complete first instruction is detoured.
    const unsigned char callBytes[]={0xE8,0x57,0xD5,0xF9,0xFF};
    const unsigned char seatBytes[]={0x64,0xA1,0,0,0,0,0x6A,0xFF};
    const unsigned char ownerBytes[]={0x56,0x8B,0xF1};
    if(std::memcmp(reinterpret_cast<void*>(0x535F94),callBytes,5)||
       std::memcmp(reinterpret_cast<void*>(0x535F00),ownerBytes,sizeof(ownerBytes))||
       std::memcmp(reinterpret_cast<void*>(0x5DF910),seatBytes,sizeof(seatBytes))){
        event("bike_pose_sampling_refused",-1,-1,"native_bytes_changed=1 no_mutation=1");return false;
    }
    auto status=MH_CreateHook(reinterpret_cast<void*>(0x5DF910),reinterpret_cast<void*>(&bikeNativeSeatPosition),reinterpret_cast<void**>(&originalBikeSeatPosition));
    if(status!=MH_OK){event("bike_pose_sampling_refused",-1,-1,"seat_create_status="+std::to_string(status));return false;}
    DWORD protection=0,unused=0;
    if(!VirtualProtect(reinterpret_cast<void*>(0x535F94),5,PAGE_EXECUTE_READWRITE,&protection)){MH_RemoveHook(reinterpret_cast<void*>(0x5DF910));return false;}
    bool written=false;
    const std::array<CodeRange,1> range{{{0x535F94,0x535F99}}};
    {
        FrozenThreads freeze(range); // main thread may be running this call site
        if(freeze.ok()&&std::memcmp(reinterpret_cast<void*>(0x535F94),callBytes,5)==0){
            unsigned char replacement[5];std::memcpy(replacement,callBytes,5);
            const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&nativeEntityAnimationsCall)-0x535F99);
            std::memcpy(replacement+1,&relative,4);std::memcpy(reinterpret_cast<void*>(0x535F94),replacement,5);written=true;
        }
    }
    const bool flushed=written&&FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x535F94),5);
    const bool protectedAgain=VirtualProtect(reinterpret_cast<void*>(0x535F94),5,protection,&unused)!=0;
    status=flushed&&protectedAgain?MH_QueueEnableHook(reinterpret_cast<void*>(0x5DF910)):MH_UNKNOWN;
    if(flushed&&protectedAgain&&status==MH_OK){nativePaletteSampleObserver=&recordBikeAuthoredPose;event("bike_pose_sampling_installed",-1,-1,"one_native_animation_call=1 seated_parent_epoch=1 entity_owner_from_verified_caller=1");return true;}
    bool restored=!written;
    if(written){
        if(!protectedAgain||VirtualProtect(reinterpret_cast<void*>(0x535F94),5,PAGE_EXECUTE_READWRITE,&unused)){
            {FrozenThreads freeze(range);std::memcpy(reinterpret_cast<void*>(0x535F94),callBytes,5);}
            const bool cache=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x535F94),5)!=0;
            restored=VirtualProtect(reinterpret_cast<void*>(0x535F94),5,protection,&unused)!=0&&cache;
        }
    }
    MH_RemoveHook(reinterpret_cast<void*>(0x5DF910));
    event("bike_pose_sampling_rolled_back",-1,-1,"bytes_and_protection_restored="+std::to_string(restored)+" seat_queue_status="+std::to_string(status));return false;
}
