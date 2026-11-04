#pragma once
#include "BikeMotion.hpp"

// Included after VehicleExits.hpp. Never sample the linear velocity before a
// native crash and combine it with angular velocity after that crash.
struct BikeMotionSample { V linear{},angular{};unsigned frame=0; };
static std::map<int,BikeMotionSample> beforeBikeMotion;
static void recordBikeHandoffGeometry(void*ped,void*bike,const PoseSeed&pose){
    // Optional geometry evidence only; constructing a second native shape and
    // serializing it on every real ejection is not part of the transition.
    if(!GetEnvironmentVariableA("GTASA_BIKE_HANDOFF_AUDIT",nullptr,0))return;
    const double started=bikeGeometryClockMs();
    auto*frame=matrix(bike);auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(bike);
    if(!frame||!col)return;
    const auto&shape=vehicleShape(at<short>(bike,0x22),col);
    const double shapeCompleted=bikeGeometryClockMs();
    const V centre=(col->box.min+col->box.max)*.5f;
    const T shapeFrame{rotation(*frame),point(*frame,centre)};
    const auto file=folder/("bike-handoff-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(reference(ped))+"-"+std::to_string(gameTime())+".json");
    std::ofstream out(file);out.precision(9);
    auto writeVector=[&](V v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';};
    auto writeTransform=[&](T t){out<<"{\"p\":";writeVector(t.p);out<<",\"q\":["<<t.q.x<<','<<t.q.y<<','<<t.q.z<<','<<t.q.w<<"]}";};
    out<<"{\"ped\":"<<reference(ped)<<",\"vehicle\":"<<pose.vehicle<<",\"vehicle_model\":"<<at<short>(bike,0x22)<<",\"shape_frame\":";
    writeTransform(shapeFrame);out<<",\"handling_com\":";writeVector(pose.centre);
    out<<",\"linear_mps\":";writeVector(pose.velocity*50);out<<",\"angular_rads\":";writeVector(pose.angular*50);
    out<<",\"sampled_root\":";writeTransform(pose.sampledRoot);
    auto writeEpoch=[&](const SeedEpoch&e){out<<"{\"frame\":"<<e.frame<<",\"serial\":"<<e.serial<<",\"phase\":"<<int(e.phase)<<",\"native_step_seconds\":"<<e.nativeStepSeconds<<",\"frame_step_seconds\":"<<e.frameStepSeconds<<'}';};
    out<<",\"pose_epoch\":";writeEpoch(pose.poseEpoch);out<<",\"motion_epoch\":";writeEpoch(pose.motionEpoch);
    out<<",\"parts\":[";
    for(size_t i=0;i<pose.parts.size();i++){if(i)out<<',';writeTransform(pose.parts[i]);}
    out<<"],\"primitives\":[";
    for(size_t i=0;i<shape.primitives.size();i++){if(i)out<<',';out<<'[';for(size_t j=0;j<10;j++){if(j)out<<',';out<<shape.primitives[i][j];}out<<']';}
    out<<"]}";out.close();const double completed=bikeGeometryClockMs();
    event("bike_handoff_geometry",reference(ped),at<short>(ped,0x22),"file="+file.filename().string()+" primitives="+std::to_string(shape.primitives.size())+
        " native_shape_ms="+std::to_string(shapeCompleted-started)+" output_ms="+std::to_string(completed-shapeCompleted)+" total_ms="+std::to_string(completed-started)+" output_ok="+std::to_string(!out.fail()));
}
static void captureBikeMotionSeeds(){
    beforeBikeMotion.clear();
    beginBikePoseFrame();
    if(!ready||!playing()||!player())return;
    auto*pool=vehiclePool();if(!pool||pool->size<1||pool->size>10000)return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        void*v=pool->objects+i*0xA18;const int type=at<int>(v,0x594);
        if((type!=9&&type!=10&&type!=2)||!matrix(v)||length(position(v)-position(player()))>radius+15)continue;
        const V linear=at<V>(v,0x44),angular=at<V>(v,0x50);
        if(finite(linear)&&finite(angular))beforeBikeMotion[(i<<8)|pool->flags[i]]={linear,angular,frame};
        for(size_t offset:{size_t(0x460),size_t(0x464)})registerBikePoseRider(at<void*>(v,offset),v);
    }
}
static std::shared_ptr<PoseSeed> captureBikeExitPose(void*ped,void*vehicle){
    if(!matrix(vehicle))return {};
    const auto epoch=bikePoseEpoch(SeedPhase::NativeTask);
    const auto sampled=captureBikeContactPose(ped,vehicle,epoch,false);
    auto pose=sampled?fitBikeContactPose(ped,*sampled):captureVehiclePose(ped,vehicle);if(!pose)return {};
    // A shot/task release is not the previous frame's collision. Capture its
    // actual complete twist now, before native detach, with its current COM.
    const auto motion=bikeRigidMotion(*matrix(vehicle),at<V>(vehicle,0xA4),at<V>(vehicle,0x44),at<V>(vehicle,0x50));
    if(!finite(motion.linear)||!finite(motion.angular)||!finite(motion.centre))return {};
    pose->velocity=motion.linear;pose->angular=motion.angular;pose->centre=motion.centre;
    pose->motionEpoch=epoch;
    if(!sampled)event("bike_pose_epoch_unverified",reference(ped),at<short>(ped,0x22),"native_task_fallback=1 no_current_authored_snapshot=1");
    recordBikeHandoffGeometry(ped,vehicle,*pose);
    event("bike_motion_capture",reference(ped),at<short>(ped,0x22),
        "current_task_twist=1 pose_epoch_verified="+std::to_string(bool(sampled))+" frame="+std::to_string(epoch.frame)+
        " vx="+std::to_string(motion.linear.x*50)+" vy="+std::to_string(motion.linear.y*50)+
        " vz="+std::to_string(motion.linear.z*50)+" spin="+std::to_string(length(motion.angular)*50)+
        " com_x="+std::to_string(motion.centre.x)+" com_y="+std::to_string(motion.centre.y)+" com_z="+std::to_string(motion.centre.z));
    return pose;
}
#include "BikeImpactCapture.hpp"
static std::shared_ptr<PoseSeed> captureNativeBikeExitPose(void*ev,void*ped){
    if(auto pose=nativeBikeImpactPose(ev,ped)){
        recordBikeHandoffGeometry(ped,at<void*>(ev,0x38),*pose);
        event("bike_motion_capture",reference(ped),at<short>(ped,0x22),"pre_collision_contact=1 frame="+std::to_string(*reinterpret_cast<unsigned*>(0xB7CB4C))+
            " vx="+std::to_string(pose->velocity.x*50)+" vy="+std::to_string(pose->velocity.y*50)+" vz="+std::to_string(pose->velocity.z*50)+
            " spin="+std::to_string(length(pose->angular)*50));
        return pose;
    }
    event("bike_preimpact_unmatched",reference(ped),at<short>(ped,0x22),"native_collision_epoch_not_verified");
    return captureBikeExitPose(ped,at<void*>(ev,0x38));
}
struct BikeExitLease {
    void*event=nullptr;int vehicle=-1;unsigned eventTime=0,frame=0;
    std::shared_ptr<PoseSeed> pose;V root{};bool detached=false;
};
static std::map<int,BikeExitLease> bikeExitLeases;
static bool bikeExitAudit(){static const bool on=GetEnvironmentVariableA("GTASA_BIKE_EXIT_AUDIT",nullptr,0)!=0;return on;}
struct BikeFallAudit {void*ped;void*task;unsigned started,next;};
static std::map<int,BikeFallAudit> bikeFallAudits;
static void observeNativeBikeFalls(){
    const unsigned now=gameTime();
    for(auto it=bikeFallAudits.begin();it!=bikeFallAudits.end();){
        const auto sample=it->second;
        void*ped=pedFromRef(it->first);
        if(now-sample.started>800||!ped||ped!=sample.ped||physicalTask(ped)!=sample.task){
            it=bikeFallAudits.erase(it);continue;
        }
        if(now>=sample.next){
            it->second.next=now+80;
            const bool falling=reinterpret_cast<bool(__thiscall*)(void*)>(0x6787D0)(sample.task);
            event("bike_native_fall_observed",it->first,at<short>(ped,0x22),
                "age_ms="+std::to_string(now-sample.started)+" falling="+std::to_string(falling)+
                " root_z="+std::to_string(position(ped).z)+" vz_mps="+std::to_string(at<V>(ped,0x44).z*50.f));
        }
        ++it;
    }
}
static bool resumeNativeBikeFall(Active& a,const char* reason){
    if(a.kind!=Reaction::BikeFall||!a.seededBikeFall||a.mounted||a.recovering||
       a.taskAborted||!applyBones||!finite(a.acceptedBikeVelocity))return false;
    void* ped=pedFromRef(a.ref);
    if(!ped||ped!=a.ped||at<short>(ped,0x22)!=a.model||at<float>(ped,0x540)<=0||
       inVehicle(ped)||at<void*>(ped,0xFC)||!matrix(ped))return false;
    void* manager=taskManager(ped);
    if(!manager||(physicalTask(ped)&&physicalTask(ped)!=a.ownedTask))return false;
    void* task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x18);
    if(!task)return false;
    // Restore a fall task after retiring the NM owner. The original launch
    // was suppressed on admission, so removing the physical task alone leaves
    // the rider in the standing animation. Carry only the accepted velocity.
    const V travel=a.acceptedBikeVelocity;
    const float forward=dot(travel,matrix(ped)->up),right=dot(travel,matrix(ped)->right);
    const int direction=std::abs(right)>std::abs(forward)?(right>=0?1:3):(forward>=0?0:2);
    reinterpret_cast<void*(__thiscall*)(void*,int,int)>(0x678700)(task,direction,0);
    restore(a,reason,false);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(manager,task,0,true);
    at<V>(ped,0x44)=travel*.02f;
    if(bikeExitAudit())bikeFallAudits[a.ref]={ped,task,gameTime(),gameTime()+30};
    event("bike_native_fall_resumed",a.ref,a.model,
        "reason="+std::string(reason)+" direction="+std::to_string(direction)+
        " speed_mps="+std::to_string(length(travel))+" added_launch=0");
    return true;
}
static bool bikePhysicalExitEligible(void*ev,void*ped){
    if(!ready||!detectImpacts||!playing()||cutscene()||(protectMissions&&scriptMissionActive())||!ridingBike(ped))return false;
    void*bike=at<void*>(ev,0x38);
    if(vehicleRef(bike)<0||at<void*>(ped,0x58C)!=bike||(at<unsigned>(bike,0x40)&0x08000100u))return false;
    void*actor=at<void*>(ev,0x30);const int actorRef=reference(actor);
    const bool knownJacker=actor&&actor!=ped&&actorRef>=0&&pedFromRef(actorRef)==actor;
    const bool playerRelease=ped==player()&&playerBikeCrashEjections&&
        (playerCrashEjection(at<unsigned char>(ev,0x36),length(at<V>(ev,0x18)),at<float>(ev,0x24))||
         playerForcedRiderRelease(at<unsigned char>(ev,0x36),knownJacker,at<float>(ev,0x24)));
    return (ped!=player()||playerRelease)&&validPed(ped,true,true,playerRelease);
}
static BikeExitLease* bikeExitLease(void*ev,void*ped){
    auto i=bikeExitLeases.find(reference(ped));if(i==bikeExitLeases.end())return nullptr;
    auto&x=i->second;
    if(x.event!=ev||x.eventTime!=at<unsigned>(ev,0x2C)||vehicleFromRef(x.vehicle)!=at<void*>(ev,0x38)||
       *reinterpret_cast<unsigned*>(0xB7CB4C)-x.frame>1)return nullptr;
    return &x;
}
static bool(__thiscall* originalBikeSafePosition)(void*,void*)=nullptr;
static int(__thiscall* originalBikeForcesAndAnims)(void*,void*)=nullptr;
static bool __fastcall bikeSafePosition(void*ev,void*,void*ped){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(auto it=bikeExitLeases.begin();it!=bikeExitLeases.end();)if(frame-it->second.frame>1||!pedFromRef(it->first)||!vehicleFromRef(it->second.vehicle))it=bikeExitLeases.erase(it);else ++it;
    const int ref=reference(ped);bikeExitLeases.erase(ref);
    const bool eligible=bikePhysicalExitEligible(ev,ped);
    if(bikeExitAudit())event("bike_exit_eligibility",reference(ped),at<short>(ped,0x22),
        "eligible="+std::to_string(eligible)+" type="+std::to_string(at<unsigned char>(ev,0x36))+" actor_ref="+std::to_string(reference(at<void*>(ev,0x30)))+
        " impact="+std::to_string(length(at<V>(ev,0x18)))+" damage="+std::to_string(at<float>(ev,0x24))+" source_vehicle="+std::to_string(vehicleRef(at<void*>(ev,0x38))));
    const auto pose=eligible?captureNativeBikeExitPose(ev,ped):nullptr;
    const V root=pose?pose->sampledRoot.p:position(ped);
    // Keep native seat bookkeeping and the safe-position failure path. Its
    // capsule search is not our ragdoll's collision or separation mechanism.
    const bool found=originalBikeSafePosition(ev,ped);
    if(found&&pose)bikeExitLeases[ref]={ev,pose->vehicle,at<unsigned>(ev,0x2C),*reinterpret_cast<unsigned*>(0xB7CB4C),pose,root,false};
    return found;
}
static void clearBikeNativeIgnore(void*ped,void*bike){
    if(at<void*>(ped,0x128)==bike){
        reinterpret_cast<void(__thiscall*)(void*,void**)>(0x571A00)(bike,&at<void*>(ped,0x128));
        at<void*>(ped,0x128)=nullptr;
    }
}
static void processNativeBikeOut(void*ev,void*ped){
    auto*lease=bikeExitLease(ev,ped);
    // Direct SetPedOutCar callers (scripted diagnostics) do not pass through
    // SetPedSafePosition. They still capture before removing the seated pose.
    if(!lease&&bikePhysicalExitEligible(ev,ped)){
        const auto pose=captureNativeBikeExitPose(ev,ped);
        if(pose){bikeExitLeases[reference(ped)]={ev,pose->vehicle,at<unsigned>(ev,0x2C),*reinterpret_cast<unsigned*>(0xB7CB4C),pose,pose->sampledRoot.p,false};lease=bikeExitLease(ev,ped);}
    }
    originalBikeOut(ev,ped);
    if(!lease||inVehicle(ped))return;
    // SafePosition can move the capsule up 0.5 m and replace the riding pose.
    // The saved physical pose is authoritative: restore its root rather than
    // translating all parts by that artificial capsule-search displacement.
    const float nativeRelocation=length(position(ped)-lease->root);
    movePhysicalRoot(ped,lease->root,ped==player());
    at<V>(ped,0x44)=lease->pose->velocity;
    clearBikeNativeIgnore(ped,at<void*>(ev,0x38));
    queuePhysicalExit(ped,lease->pose,position(ped),Reaction::BikeFall);
    lease->detached=true;bikeEjections++;
    event("bike_native_handoff",reference(ped),at<short>(ped,0x22),
        "native_capsule_relocation="+std::to_string(nativeRelocation)+" source_vehicle="+std::to_string(lease->vehicle)+" no_launch=1");
}
static int __fastcall bikeForcesAndAnims(void*ev,void*,void*ped){
    auto*lease=bikeExitLease(ev,ped);
    const auto queued=pending.find(reference(ped));
    if(lease&&lease->detached&&queued!=pending.end()&&queued->second.kind==Reaction::BikeFall&&queued->second.pose==lease->pose){
        // The actual 1.0 handler calls this AFTER SetPedOutCar. Its randomized
        // vertical impulse, 0.3 speed multiplier and ignored-vehicle flag must
        // not run after a successful physical handoff. Other callers stay stock.
        at<V>(ped,0x44)=lease->pose->velocity;clearBikeNativeIgnore(ped,at<void*>(ev,0x38));
        event("bike_native_launch_replaced",reference(ped),at<short>(ped,0x22),"owned_event=1 animation_sentinel=191");
        bikeExitLeases.erase(reference(ped));return 191;
    }
    const V before=at<V>(ped,0x44);const int result=originalBikeForcesAndAnims(ev,ped);
    if(bikeExitAudit())event("bike_unowned_native_forces",reference(ped),at<short>(ped,0x22),
        "type="+std::to_string(at<unsigned char>(ev,0x36))+" actor_ref="+std::to_string(reference(at<void*>(ev,0x30)))+" result="+std::to_string(result)+
        " before_vx="+std::to_string(before.x*50)+" before_vy="+std::to_string(before.y*50)+" before_vz="+std::to_string(before.z*50)+
        " after_vx="+std::to_string(at<V>(ped,0x44).x*50)+" after_vy="+std::to_string(at<V>(ped,0x44).y*50)+" after_vz="+std::to_string(at<V>(ped,0x44).z*50));
    return result;
}
