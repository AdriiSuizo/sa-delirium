#pragma once
#include "BikePoseCapture.hpp"
static std::shared_ptr<PoseSeed> captureVehiclePose(void*ped,void*vehicle){
    if(vehicleRef(vehicle)<0)return {};
    try{
        Rig rig(ped,sourceBuild);auto pose=std::make_shared<PoseSeed>();
        const auto frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
        const bool evaluated=evaluatedPedFrame==frame&&evaluatedPeds.contains(reference(ped))&&evaluatedPeds.at(reference(ped))==ped;
        const bool current=evaluated||publishCurrentNativePalette(ped);
        if(!rig.incoming(ped,pose->parts,pose->bones))return {};
        const int ref=vehicleRef(vehicle);
        const auto epoch=bikePoseEpoch(SeedPhase::NativeTask);
        pose->vehicle=ref;pose->velocity=at<V>(vehicle,0x44);
        pose->angular=at<V>(vehicle,0x50);pose->centre=matrix(vehicle)?point(*matrix(vehicle),at<V>(vehicle,0xA4)):position(vehicle);
        pose->sampledRoot=transform(*matrix(ped));pose->motionEpoch=epoch;
        // Matrix publication does not replay UpdateAnimations. Without a real
        // evaluation receipt the legacy finite palette remains explicit Unknown.
        if(current){pose->poseEpoch=epoch;if(evaluated)pose->poseEpoch.phase=SeedPhase::EvaluatedEnd;}
        if(!finite(pose->velocity)||!finite(pose->angular))return {};
        return pose;
    }catch(const std::exception&e){event("vehicle_exit_pose_failed",reference(ped),at<short>(ped,0x22),e.what());return {};}
}
static void queuePhysicalExit(void*ped,const std::shared_ptr<PoseSeed>&pose,V before,Reaction kind){
    if(!pose||inVehicle(ped))return;
    // Collision-driven bike ejection may displace the root. Voluntary exits
    // capture the already separated pose and therefore pass a zero shift.
    V shift=position(ped)-before;
    for(auto&t:pose->bones)t.p=t.p+shift;
    for(auto&t:pose->parts)t.p=t.p+shift;
    pose->sampledRoot.p=pose->sampledRoot.p+shift;
    at<V>(ped,0x44)=pose->velocity;
    Request r{{},normalized(pose->velocity),gameTime(),0,false,kind,pose->vehicle,pose,ped==player()};
    pending[reference(ped)]=r;
    event("vehicle_inertia_handoff",reference(ped),at<short>(ped,0x22),"speed_kmh="+std::to_string(length(pose->velocity)*180.f)+" vz_mps="+std::to_string(pose->velocity.z*50.f)+" added_launch=0");
}
static bool(__thiscall* originalCarJumpOut)(void*,void*)=nullptr;
struct ExitTransit {void* task=nullptr;int vehicle=-1;unsigned started=0,lastSample=0;int side=0;std::shared_ptr<PoseSeed> previous;unsigned poseTime=0,poseFrame=0;};
static std::map<int,ExitTransit> exitTransits;
static bool seedExitAnimationMotion(PoseSeed&pose,const ExitTransit&transit,unsigned frame){
    if(!transit.previous||frame-transit.poseFrame!=1)return false;
    const float dt=*reinterpret_cast<float*>(0xB7CB5C)*.02f;
    // Only consecutive native updates belong to this handoff. A pause/loading
    // discontinuity cannot be interpreted as an animated limb velocity.
    // CTimer's wall milliseconds diverge from animation/physics time when SA
    // clips a long frame. Use the same simulated step as the native animation.
    if(!std::isfinite(dt)||dt<=0)return false;
    std::array<float,147> current{},previous{};std::array<float,126> motion{};
    for(int i=0;i<21;i++){
        auto write=[i](auto&out,const T&t){out[i*7]=t.p.x;out[i*7+1]=t.p.y;out[i*7+2]=t.p.z;out[i*7+3]=t.q.x;out[i*7+4]=t.q.y;out[i*7+5]=t.q.z;out[i*7+6]=t.q.w;};
        write(current,pose.parts[i]);write(previous,transit.previous->parts[i]);
    }
    const V referenceVelocity=pose.velocity*50.f;
    if(!ok(nm_animation_start_motion(current.data(),previous.data(),21,dt,&referenceVelocity.x,motion.data())))return false;
    for(int i=0;i<21;i++){pose.linear[i]={motion[i*6],motion[i*6+1],motion[i*6+2]};pose.spin[i]={motion[i*6+3],motion[i*6+4],motion[i*6+5]};}
    pose.hasPartVelocities=true;return true;
}
static float exitClearance(void*vehicle,const PoseSeed&pose,int side){
    auto*m=matrix(vehicle);auto*c=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(vehicle);M inverseFrame{};
    if(!m||!c||!affineInverse(*m,inverseFrame))return -1000.f;
    float clearance=1000.f;
    // The entire body must clear the side of the chassis, including a margin
    // for each physical shape. The bInVehicle flag alone changes at ~0.07 s,
    // while the rollout still has CJ's torso and legs in the cabin.
    for(const auto&t:pose.parts){
        V local=point(inverseFrame,t.p);
        clearance=std::min(clearance,lateralExitClearance(local.x,c->box.min.x,c->box.max.x,side,.20f));
    }
    return clearance;
}
static bool __fastcall carJumpOut(void*task,void*,void*ped){
    void*v=at<void*>(task,0x10);const int ref=vehicleRef(v),pedRef=reference(ped);
    const unsigned now=gameTime(),frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(auto it=exitTransits.begin();it!=exitTransits.end();)if(now-it->second.started>4000||!pedFromRef(it->first)||!vehicleFromRef(it->second.vehicle))it=exitTransits.erase(it);else ++it;
    if(!recoveryAllowed()||!physicalVehicleExits||ref<0||pedRef<0||!playerGroundVehicle(at<int>(v,0x594))){exitTransits.erase(pedRef);return originalCarJumpOut(task,ped);}
    auto it=exitTransits.find(pedRef);
    if(it!=exitTransits.end()&&(it->second.task!=task||it->second.vehicle!=ref)){exitTransits.erase(it);it=exitTransits.end();}
    if(it==exitTransits.end()){
        const V velocity=beforeVehicleVelocity.count(ref)?beforeVehicleVelocity[ref]:at<V>(v,0x44)*50.f;
        if(!inVehicle(ped)||!movingExit(length(velocity),exitThresholdKmh))return originalCarJumpOut(task,ped);
        const unsigned door=at<unsigned>(task,0x18);const int side=(door==8||door==9)?1:-1;
        it=exitTransits.emplace(pedRef,ExitTransit{task,ref,now,0,side}).first;
        event("vehicle_exit_transit",pedRef,at<short>(ped,0x22),"native_lateral_exit side="+std::to_string(side));
    }
    const bool done=originalCarJumpOut(task,ped);
    const auto pose=captureVehiclePose(ped,v);
    if(!inVehicle(ped)){
        const int type=at<int>(v,0x594);const bool openSeat=type==2||type==9||type==10;
        const float clearance=pose?exitClearance(v,*pose,it->second.side):-1000.f;
        if(now-it->second.lastSample>=100){
            it->second.lastSample=now;
            event("vehicle_exit_clearance",pedRef,at<short>(ped,0x22),"metres="+std::to_string(clearance)+" elapsed_ms="+std::to_string(now-it->second.started));
        }
        if(pose&&(openSeat||clearance>=0.f)){
        const bool animated=seedExitAnimationMotion(*pose,it->second,frame);
        float maxRelative=0,maxSpin=0;
        if(animated)for(int i=0;i<21;i++){maxRelative=std::max(maxRelative,length(pose->linear[i]-pose->velocity*50.f));maxSpin=std::max(maxSpin,length(pose->spin[i]));}
        event("vehicle_exit_animation_motion",pedRef,at<short>(ped,0x22),"original_start="+std::to_string(animated)+" sample_ms="+std::to_string(it->second.previous?now-it->second.poseTime:0)+" simulated_seconds="+std::to_string(*reinterpret_cast<float*>(0xB7CB5C)*.02f)+" max_relative_mps="+std::to_string(maxRelative)+" max_spin_rads="+std::to_string(maxSpin)+" pelvis_vz="+std::to_string(pose->linear[0].z));
        if(!openSeat&&at<void*>(ped,0x128)==v){
            reinterpret_cast<void(__thiscall*)(void*,void**)>(0x571A00)(v,&at<void*>(ped,0x128));
            at<void*>(ped,0x128)=nullptr;
        }
        queuePhysicalExit(ped,pose,position(ped),Reaction::VehicleExit);
        event(openSeat?"vehicle_exit_open_seat_released":"vehicle_exit_body_clear",pedRef,at<short>(ped,0x22),"metres="+std::to_string(clearance)+" side="+std::to_string(it->second.side)+" elapsed_ms="+std::to_string(now-it->second.started));
        // End the animation only after lateral clearance. Retain the actual
        // door-release motion, but discard SA's remaining prerecorded roll.
        if(auto*anim=at<void*>(task,0xC))at<float>(anim,0x1C)=-1000.f;
        at<bool>(task,8)=true;
        exitTransits.erase(it);
        return true;
        }
    }
    if(done){event("vehicle_exit_kept_native",pedRef,at<short>(ped,0x22),"body_never_cleared_chassis");exitTransits.erase(pedRef);}
    else if(pose&&frame!=it->second.poseFrame){it->second.previous=pose;it->second.poseTime=now;it->second.poseFrame=frame;}
    return done;
}
