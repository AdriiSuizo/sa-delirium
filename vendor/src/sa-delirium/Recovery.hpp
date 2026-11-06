#pragma once
// Native task/animation ABI: research/native-abi-reference/TaskSimpleGetUp.*,
// TaskManager.cpp, AnimBlendAssociation.* and the local plugin-sdk.
// The task manager owns the allocation. Never dereference a cached task after
// the manager has replaced it, even if the ped pool generation still matches.
static void* taskManager(void*p){auto*i=at<void*>(p,0x47C);return i?static_cast<unsigned char*>(i)+4:nullptr;}
static void* physicalTask(void*p){auto*m=taskManager(p);return m?at<void*>(m,0):nullptr;}
static bool recoveryAllowed(){return ready&&applyBones&&playing()&&!cutscene()&&!(protectMissions&&scriptMissionActive());}
static ShotDamageReceipt shotReceipt(const DamageEvent&d){return {d.time,reinterpret_cast<uintptr_t>(d.source),d.weapon,d.piece};}
static Active* groundShotOwner(void*p){
    if(!recoveryAllowed()||p==player()||!validPed(p,true))return nullptr;
    const auto i=agents.find(reference(p));if(i==agents.end())return nullptr;
    auto&a=*i->second;
    return a.ped==p&&!a.mounted&&!a.preview&&
        (a.recovering||a.current[10].p.z-a.current[0].p.z<.25f||nm_agent_has_fallen(a.agent)==1)?&a:nullptr;
}
static Active* matchedGroundShot(DamageEvent&d,void*p){
    auto*a=groundShotOwner(p);
    return a&&d.response.calculated&&sameDeliveredShot(a->groundShot,shotReceipt(d),gameTime())?a:nullptr;
}
static bool matchesEntryFatalDamage(const Active&a,const DamageEvent&d){
    // Match only our queued, calculated terminal drag event, not an unrelated
    // explosion, later road strike or SA's anonymous zero-health fallback.
    return recoveryAllowed()&&a.kind==Reaction::DoorEntry&&a.entryFatalAt&&
        gameTime()-a.entryFatalAt<=150&&at<float>(a.ped,0x540)<=0&&
        vehicleRef(d.source)==a.vehicle&&d.weapon==50&&d.piece==3&&
        d.time==a.entryFatalAt&&d.response.calculated&&d.response.zero;
}
static Active* ownedRecovery(void*task,void*ped){
    auto it=agents.find(reference(ped));if(it==agents.end())return nullptr;auto&a=*it->second;
    return a.ped==ped&&a.model==at<short>(ped,0x22)&&a.ownedTask==task?&a:nullptr;
}
static void __cdecl detachedAnimationCallback(void*,void*){}
static void retireRecoveryAnimation(Active&a){
    // The task may already have been deleted by a native event. Resolve the
    // association through the live clump before touching our cached pointer.
    void*p=pedFromRef(a.ref);
    if(p==a.ped&&p&&at<short>(p,0x22)==a.model&&at<void*>(p,0x18)&&a.ownedAnimation){
        void*live=reinterpret_cast<void*(__cdecl*)(void*,unsigned)>(0x4D68B0)(at<void*>(p,0x18),a.recoveryAnim);
        if(live==a.ownedAnimation){
            reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4CEBE0)(live,reinterpret_cast<void*>(&detachedAnimationCallback),nullptr);
            at<float>(live,0x24)=1.f;at<float>(live,0x1C)=-8.f;
            at<unsigned short>(live,0x2E)|=4u; // ANIMATION_IS_BLEND_AUTO_REMOVE
            event("recovery_animation_retired",a.ref,a.model);
        }
    }
    a.ownedAnimation=nullptr;
}
// CTaskManager primaries 0..4 (physical response, event temp/nontemp, primary,
// default) and TASK_SECONDARY_ATTACK (+0x14) by GetTaskType: whether SA's own
// damage response (e.g. KillCriminal/KillPedOnFoot) survives the body's hold.
static std::string nativeResponseSlots(void*p){
    std::string s;void*m=taskManager(p);if(!m)return "none";
    for(int i=0;i<6;i++){void*t=at<void*>(m,i==5?0x14:i*4);
        s+=(t?std::to_string(reinterpret_cast<int(__thiscall*)(void*)>((*reinterpret_cast<void***>(t))[4])(t)):std::string("-"))+(i<5?",":"");}
    return s;
}
static void releaseRecoveryTask(Active& a){
    retireRecoveryAnimation(a);
    auto*p=pedFromRef(a.ref);
    if(p==a.ped&&p&&at<short>(p,0x22)==a.model&&a.ownedTask&&physicalTask(p)==a.ownedTask){
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),nullptr,0,true);
        event("native_response_slots",a.ref,a.model,"stage=release slots="+nativeResponseSlots(p));
    }
    a.ownedTask=nullptr;
}
// A bike jack leaves SA's CTaskComplexFallAndGetUp (208) in an event-response
// slot under the physical task. The physical get-up has already played its
// CTaskSimpleGetUp clip: kept, it played the same clip a second time after the
// hand-back (CJ jacked off a BMX, 06/11). Slots are CTaskManager's primaries.
static void retireNativeFallAndGetUp(Active&a){
    void*m=taskManager(a.ped);if(!m)return;
    for(int slot:{1,2,3}){
        void*t=at<void*>(m,slot*4);
        if(!t||reinterpret_cast<int(__thiscall*)(void*)>((*reinterpret_cast<void***>(t))[4])(t)!=208)continue;
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(m,nullptr,slot,true);
        event("native_fall_getup_retired",a.ref,a.model,"slot="+std::to_string(slot));
    }
}
static bool ensureRecoveryTask(Active&a){
    if(a.taskAborted)return false;
    if(a.ownedTask&&physicalTask(a.ped)==a.ownedTask)return true;
    if(a.ownedTask){a.taskAborted=true;return false;}
    a.ownedTask=nullptr;auto*m=taskManager(a.ped);if(!m)return false;
    void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x10);if(!t)return false;
    reinterpret_cast<void*(__thiscall*)(void*)>(0x677F50)(t);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(m,t,0,true);
    a.ownedTask=t;event("physical_task_acquired",a.ref,a.model,"slots="+nativeResponseSlots(a.ped));return true;
}
static bool resumePhysicalContact(Active&a,const char*reason){
    if(!a.recovering&&!a.taskAborted)return false;
    const bool interruptedGetUp=a.recovering;
    releaseRecoveryTask(a);
    if(a.recovering&&a.recoveryAnimatedOwner&&!a.lastRendered.empty())for(int n=0;n<21;n++){
        const T pose=a.renderedParts[n];
        ok(nm_agent_set_part_state(a.agent,n,&pose.p.x,&pose.q.x,&a.renderVelocity[n].x,&a.renderSpin[n].x));
        a.current[n]=a.previous[n]=pose;
    }
    a.recovering=false;a.recoveryAnimatedOwner=false;a.recoveryPlacementReady=false;a.taskAborted=false;a.resumeDeliveredRoadDamage=false;a.resumeDeliveredCombatDamage=false;a.recoveryDone=false;a.nativeAnimationFinished=false;
    a.groundShotDelivered=false;
    a.uprightRecovery=false;a.recoveryStage=0;a.accumulator=0;a.stableFor=0;a.start=gameTime();a.bumpReady=false;a.exitReady=false;a.entryReady=false;
    if(applyBones)at<unsigned char>(a.ped,0x1C)&=~1u;
    recoveryInterrupted++;event(reason,a.ref,a.model,"same_agent=1 native_collision_gap=0 retained_rendered_velocity=1");
    // A hit during the get-up keeps the same body, but it is still the
    // recovery interruption Test-Recovery/Record-NativeRecovery look for.
    if(interruptedGetUp)event("recovery_interrupted_by_damage",a.ref,a.model,reason);
    return true;
}
static void __cdecl recoveryFinishCallback(void*anim,void*task){
    for(auto&[ref,entry]:agents){
        auto&a=*entry;
        if(a.ownedTask!=task||a.ownedAnimation!=anim)continue;
        if(pedFromRef(ref)!=a.ped||physicalTask(a.ped)!=task)return;
        // SA invokes FINISH callbacks for a faded/deleted association too.
        // Only reaching the actual end of the running clip is a completed rise.
        a.nativeAnimationFinished=a.recoveryStage==4&&at<float>(anim,0x20)+.001f>=a.animationDuration;
        if(!a.nativeAnimationFinished){a.taskAborted=true;event("recovery_animation_superseded",ref,a.model);}
        at<unsigned short>(anim,0x2E)|=4u;
        reinterpret_cast<void(__cdecl*)(void*,void*)>(0x678110)(anim,task);
        a.ownedAnimation=nullptr;return;
    }
}
static bool setRecoveryClip(Active&a,int id){
    if(!a.ownedTask||physicalTask(a.ped)!=a.ownedTask)return false;
    retireRecoveryAnimation(a);
    if(auto*old=at<void*>(a.ownedTask,0xC))
        reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4CEBE0)(old,reinterpret_cast<void*>(&detachedAnimationCallback),nullptr);
    void*anim=reinterpret_cast<void*(__cdecl*)(void*,int,int,float)>(0x4D4610)(at<void*>(a.ped,0x18),0,id,1000.f);
    if(!anim)return false;
    at<void*>(a.ownedTask,0xC)=anim;at<unsigned char>(a.ownedTask,8)=0;at<unsigned char>(a.ownedTask,9)=0;
    // Start resets both the time and key-frame cursors, including a reused clip.
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x4CEB70)(anim,0.f);
    at<float>(anim,0x18)=1.f;at<float>(anim,0x1C)=0;at<float>(anim,0x24)=0.f;
    at<unsigned short>(anim,0x2E)=(at<unsigned short>(anim,0x2E)&~(2u|8u))|4u;
    a.ownedAnimation=anim;
    reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4CEBE0)(anim,reinterpret_cast<void*>(&recoveryFinishCallback),a.ownedTask);
    a.sampleFrames=0;a.recoveryAnim=id;
    a.recoveryClipEpoch=currentNativePaletteEvaluation(a.ped)?currentNativePaletteEvaluation(a.ped)->serial:0;
    a.recoverySampleSerial=0;a.recoveryPlacementReady=false;
    if(auto*h=at<void*>(anim,0x14))a.animationDuration=at<float>(h,0x10);
    return true;
}
static bool(__thiscall* originalGetupProcess)(void*,void*)=nullptr;
static bool(__thiscall* originalGetupAbort)(void*,void*,int,void*)=nullptr;
// The native GetUp cannot start underneath a car. Check that same gate BEFORE
// freezing NM for the pose blend; otherwise the body waits motionless for the
// watchdog and re-enables its standing collider inside the car at timeout.
static void* recoveryBlockingCar(void*p){
    auto*car=reinterpret_cast<void*(__cdecl*)(const void*)>(0x616A40)(p);
    if(!car||at<int>(car,0x590)==9||at<int>(car,0x594)==2||
       car==at<void*>(p,0xFC)||car==at<void*>(p,0x568))return nullptr;
    return car;
}
static bool __fastcall getupAbort(void*task,void*,void*ped,int priority,void*ev){
    // Adrii 06/11: CJ getting up off the ground takes every blow's damage but is
    // not put down again, so several attackers cannot keep him on the floor. A
    // task that refuses to abort is SA's own path for that (ComputeDamageAnim
    // 0x4B3FC0 / ComputeDeathAnim: no damage anim, no fall, the damage stays).
    // Only living, melee-weapon damage (0..15); cars, shots and fatal blows abort.
    if(ped==player()&&at<float>(ped,0x540)>0&&ev&&reinterpret_cast<int(__thiscall*)(void*)>((*reinterpret_cast<void***>(ev))[1])(ev)==9&&
       static_cast<DamageEvent*>(ev)->weapon>=0&&static_cast<DamageEvent*>(ev)->weapon<=15){
        static unsigned logged=0;
        if(gameTime()-logged>=250){logged=gameTime();event("player_getup_kept",reference(ped),at<short>(ped,0x22),"weapon="+std::to_string(static_cast<DamageEvent*>(ev)->weapon)+" health="+std::to_string(at<float>(ped,0x540))+" owned="+std::to_string(ownedRecovery(task,ped)!=nullptr));}
        return false;
    }
    // A dormant native GetUp would reject nonfatal damage because it has no
    // animation yet. Allow events to interrupt our task; SA still decides damage.
    if(auto*a=ownedRecovery(task,ped);a&&priority>=1){
        const int type=ev?reinterpret_cast<int(__thiscall*)(void*)>((*reinterpret_cast<void***>(ev))[1])(ev):-1;
        // Preserve native event priorities. The only exception is real recent
        // damage against our dormant task, which has no association to abort.
        bool forceDamage=false;
        if(priority==1&&type==9){auto*d=static_cast<DamageEvent*>(ev);forceDamage=gameTime()-d->time<=150&&(d->response.health+d->response.armour>0);}
        if(!forceDamage&&!originalGetupAbort(task,ped,priority,ev))return false;
        a->resumeDeliveredRoadDamage=false;
        a->resumeDeliveredCombatDamage=false;
        a->entryDeathDelivered=false;
        if(type==9&&a->kind==Reaction::DoorEntry&&at<float>(ped,0x540)<=0){
            auto*d=static_cast<DamageEvent*>(ev);
            event("entry_death_delivery",a->ref,a->model,"fatal_at="+std::to_string(a->entryFatalAt)+" event_ms="+std::to_string(d->time)+" source_vehicle="+std::to_string(vehicleRef(d->source))+" expected_vehicle="+std::to_string(a->vehicle)+" weapon="+std::to_string(d->weapon)+" piece="+std::to_string(d->piece));
        }
        if(priority==1&&type==9)
            a->entryDeathDelivered=matchesEntryFatalDamage(*a,*static_cast<DamageEvent*>(ev));
        if(priority==1&&type==9&&recoveryAllowed()&&!a->recovering&&!a->preview&&!a->mounted&&
            a->kind==Reaction::Melee&&at<float>(ped,0x540)>0){
            auto*d=static_cast<DamageEvent*>(ev);
            const CombatDamageReceipt delivered{d->time,reference(d->source),d->weapon,d->piece};
            a->resumeDeliveredCombatDamage=sameDeliveredCombatDamage(a->combatDamage,delivered,gameTime());
            if(a->resumeDeliveredCombatDamage)event("combat_damage_delivery_matched",a->ref,a->model,
                "event_ms="+std::to_string(d->time)+" age_ms="+std::to_string(gameTime()-d->time)+" source="+std::to_string(delivered.source)+" damage_preserved=1");
        }
        if(priority==1&&type==9&&recoveryAllowed()&&!a->recovering&&!a->preview&&!a->mounted&&
            a->kind==Reaction::Impact&&at<float>(ped,0x540)>0){
            auto*d=static_cast<DamageEvent*>(ev);
            const RoadDamageReceipt delivered{d->time,vehicleRef(d->source),d->weapon,d->piece};
            a->resumeDeliveredRoadDamage=sameDeliveredRoadDamage(a->roadDamage,delivered,gameTime());
            if(a->resumeDeliveredRoadDamage)event("road_damage_delivery_matched",a->ref,a->model,
                "event_ms="+std::to_string(d->time)+" age_ms="+std::to_string(gameTime()-d->time)+" vehicle_ref="+std::to_string(delivered.vehicle)+" weapon="+std::to_string(d->weapon)+" damage_preserved=1");
        }
        retireRecoveryAnimation(*a);a->taskAborted=true;
        at<void*>(task,0xC)=nullptr;at<unsigned char>(task,8)=1;at<unsigned char>(task,9)=1;
        event("physical_task_aborted",a->ref,a->model,"priority="+std::to_string(priority)+" event="+std::to_string(type));return true;
    }
    return originalGetupAbort(task,ped,priority,ev);
}
static bool __fastcall getupProcess(void*task,void*,void*ped){
    auto*a=ownedRecovery(task,ped);
    if(!a)return originalGetupProcess(task,ped);
    if(!recoveryAllowed()||at<float>(ped,0x540)<=0||a->taskAborted){retireRecoveryAnimation(*a);a->ownedTask=nullptr;return true;}
    if(!a->recovering){
        if((a->current[10].p-a->current[0].p).z<.25f){
            auto&fallen=at<unsigned char>(ped,0x476);if(!(fallen&16u))a->ownedFallenFlag=true;fallen|=16u;
        }
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x5DEC00)(ped,1);
        at<V>(ped,0x44)=V{};return false;
    }
    // The clips are evaluated while NM remains the owner. They are held at
    // their first pose; neither a native StartAnim nor a palette refresh can
    // commit placement. Only fit + room at the current scene barrier does so.
    if(!a->recoveryAnimatedOwner||a->uprightRecovery||a->recoveryStage<4)return false;
    const bool finished=originalGetupProcess(task,ped);
    if(finished){
        a->ownedTask=nullptr;
        if(a->nativeAnimationFinished){a->recoveryDone=true;nativeGetupCompleted++;
            event("native_getup_completed",a->ref,a->model,"played_ms="+std::to_string(gameTime()-a->animationStarted)+" clip_seconds="+std::to_string(a->animationDuration));}
        else{a->taskAborted=true;event("recovery_task_ended_without_clip",a->ref,a->model);}
        return true;
    }
    return false;
}
static void(__thiscall* originalManageTasks)(void*)=nullptr;
static void __fastcall manageTasks(void* manager,void*){
    void*ped=at<void*>(manager,0x2C);
    if(recoveryAllowed()&&ped){
        auto it=agents.find(reference(ped));
        if(it!=agents.end()){
            auto&a=*it->second;
            if(ped==a.ped&&at<short>(ped,0x22)==a.model&&!a.recovering&&!a.preview&&!a.mounted&&at<float>(ped,0x540)>0)
                ensureRecoveryTask(a);
        }
    }
    originalManageTasks(manager);
}
// Candidate provenance is one actual native evaluation in this exact frame.
// It never grants ownership; the current-scene barrier must commit it.
struct RecoveryPlacementCandidate {void*ped=nullptr;unsigned frame=0;uint64_t serial=0;T nativeRoot{};};
static std::map<int,RecoveryPlacementCandidate> recoveryPlacements;
static std::vector<int> recoveryPoints(const Rig&r){
    std::vector<int> out;for(int id:{1,2,3,4,5,32,33,34,22,23,24,41,42,43,51,52,53}){int i=r.index(id);if(i>=0)out.push_back(i);}return out;
}
static bool beginRecovery(Active&a){
    if(applyBones&&!ensureRecoveryTask(a))return false;
    // 8261E688: preceding response is over, body remains relaxed and simulated
    // while BlendFromNM searches. No root settling, velocity zeroing or freeze.
    const V torso=a.current[10].p-a.current[0].p;
    const bool upright=originalStandingRecovery(a.kind==Reaction::Bump||a.meleeTaskOriginal,nm_agent_is_balancing(a.agent)==1,nm_agent_has_fallen(a.agent)==1,
        torso.z>.30f&&normalized(torso).z>.90f&&a.current[0].p.z-std::min(a.current[3].p.z,a.current[6].p.z)>.55f);
    if(GetEnvironmentVariableA("GTASA_MELEE_RECOVERY_AUDIT",nullptr,0))
        event("recovery_selection_audit",a.ref,a.model,"upright="+std::to_string(upright)+" engine_fallen="+std::to_string(nm_agent_has_fallen(a.agent))+
            " balancing="+std::to_string(nm_agent_is_balancing(a.agent))+" source_response="+std::to_string(a.kind==Reaction::Bump||a.meleeTaskOriginal)+" torso_up="+std::to_string(normalized(torso).z));
    // IV (peds.rs, task end Next::Blend while standing): the task goes, but
    // the balancer keeps the body up while it blends back onto the animation
    // (feet at its stance). Relaxing it here made every successful bump
    // Balance collapse ~0.1 s after its finish (2026-11-01, poses log).
    const bool keepBalance=upright&&(a.kind==Reaction::Bump||a.meleeTaskOriginal);
    if(keepBalance){
        if(!ok(nm_agent_road_cancel(a.agent,collision->world))||!ok(nm_agent_vehicle_exit_cancel(a.agent))||!ok(nm_agent_vehicle_entry_cancel(a.agent))||
           !ok(nm_agent_ped_bump_cancel(a.agent))||!ok(nm_agent_cancel_animation_pose(a.agent)))return false;
    }else if(!ok(nm_agent_recovery_wait(a.agent,collision->world)))return false;
    releaseVehicle(a,"recovery");
    a.uprightRecovery=upright;
    a.recovering=true;a.recoveryAnimatedOwner=false;a.recoveryPlacementReady=false;
    a.recoverStart=0;a.recoveryStage=0;a.recoveryNextProbe=0;a.recoveryWaitSince=0;a.recoverySampleSerial=0;
    a.recoveryDone=false;a.nativeAnimationFinished=false;a.recoveryClearanceLost=false;
    recoveryPlacements.erase(a.ref);
    if(!a.uprightRecovery&&setRecoveryClip(a,112))a.recoveryStage=1;
    recoveryStarts++;event("recovery_nm_search_started",a.ref,a.model,std::string("physics_retained=1 frozen_pose=0 velocity_reset=0 balance_kept=")+(keepBalance?"1":"0"));return true;
}
static void maintainRecoverySupport(){
    for(auto&[ref,entry]:agents){auto&a=*entry;
        if(!a.recovering||!a.recoveryAnimatedOwner||pedFromRef(ref)!=a.ped||!matrix(a.ped))continue;
        const V root=position(a.ped),from=root+V{0,0,.4f};ColPoint ground{};void*support=nullptr;
        if(reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,root.z-2.5f,ground,support,true,true,false,true,true,false,nullptr)&&ground.normal.z>.45f){
            const float target=ground.point.z+(a.nativeGroundClearance>0?a.nativeGroundClearance:a.rig->rootClearance);
            if(root.z<target){matrix(a.ped)->pos.z=target;if(at<V>(a.ped,0x44).z<0)at<V>(a.ped,0x44).z=0;
                reinterpret_cast<void(__thiscall*)(void*)>(0x542560)(a.ped);}
        }
    }
}
static void sampleRecoveryPose(Active&a){
    if(!a.ownedTask||physicalTask(a.ped)!=a.ownedTask)return;
    const auto*evaluation=currentNativePaletteEvaluation(a.ped);if(!evaluation)return;
    if(a.recoveryAnimatedOwner){
        if(!a.uprightRecovery&&a.recoveryStage==3&&(gameTime()-a.recoverStart)*.001f>=blendSeconds){
            if(auto*anim=at<void*>(a.ownedTask,0xC)){at<float>(anim,0x24)=1.f;a.animationStarted=gameTime();a.recoveryStage=4;
                event("recovery_animation_playing",a.ref,a.model,"anim="+std::to_string(a.recoveryAnim)+" duration="+std::to_string(a.animationDuration));}
        }return;
    }
    if(evaluation->serial<=a.recoverySampleSerial)return;
    if(!a.uprightRecovery&&!nativePaletteEvaluatedClip(a.ped,a.ownedAnimation,a.recoveryClipEpoch))return;
    a.recoverySampleSerial=evaluation->serial;
    // Fit against this body's latest solved pose, never the first frame of its
    // wait. The native clip really contributed to this evaluated palette.
    const auto physical=a.rig->physicalBones(a.current);
    const auto fit=fitRecovery(a.bones,physical,recoveryPoints(*a.rig));
    if(!fit.valid)return;
    if(!a.uprightRecovery&&(a.recoveryStage==1||a.recoveryStage==2)){
        const int sample=a.recoveryStage-1;a.recoveryFits[sample]=fit;
        if(sample==0){if(setRecoveryClip(a,115))a.recoveryStage=2;return;}
        const int selected=a.recoveryFits[1].error<a.recoveryFits[0].error?1:0;
        if(setRecoveryClip(a,selected?115:112))a.recoveryStage=3;
        event("recovery_pose_selected",a.ref,a.model,"anim="+std::to_string(a.recoveryAnim)+" back_error="+std::to_string(a.recoveryFits[0].error)+" front_error="+std::to_string(a.recoveryFits[1].error)+" physics_retained=1");return;
    }
    if(gameTime()<a.recoveryNextProbe)return;
    const T aligned=mul(fit.delta,transform(*matrix(a.ped)));
    if(!finite(aligned.p))return;
    recoveryPlacements[a.ref]={a.ped,evaluation->frame,evaluation->serial,transform(*matrix(a.ped))};a.recoveryPlacementReady=true;
}
constexpr unsigned RecoveryRoomWaitMs=5000;
// Called ONLY after current map, vehicles and animated owner obstacles exist.
// Every other NM body is tested separately with its exact physical components.
static void commitPendingRecoveryPlacements(){
    if(!collision||!collision->world)return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C),now=gameTime();
    for(auto&[ref,entry]:agents){auto&a=*entry;if(!a.recovering||a.recoveryAnimatedOwner||!a.recoveryPlacementReady)continue;
        a.recoveryPlacementReady=false;auto candidate=recoveryPlacements.find(ref);
        if(candidate==recoveryPlacements.end()||candidate->second.ped!=a.ped||candidate->second.frame!=frame||
           pedFromRef(ref)!=a.ped||physicalTask(a.ped)!=a.ownedTask)continue;
        auto c=candidate->second;recoveryPlacements.erase(candidate);
        const auto*evaluation=currentNativePaletteEvaluation(a.ped);if(!evaluation||evaluation->serial!=c.serial)continue;
        const auto currentFit=fitRecovery(a.bones,a.rig->physicalBones(a.current),recoveryPoints(*a.rig));
        if(!currentFit.valid)continue;const T aligned=mul(currentFit.delta,c.nativeRoot);
        V target=aligned.p;const float clearance=a.nativeGroundClearance>0?a.nativeGroundClearance:a.rig->rootClearance;
        // SA supplies its actual supporting surface, including vehicle roofs.
        // This vertical query is only ground placement; room is a volume query.
        // The probe starts above the physical pelvis. A lying body's pelvis is
        // near the ground; a standing one (upright handback) is ~1 m above its
        // feet, which the fixed 1.3 m never reached (blocked_by=no_ground on
        // every upright recovery, 2026-11-01). Reach past the lowest foot.
        const float standing=std::max(0.f,a.current[0].p.z-std::min(a.current[3].p.z,a.current[6].p.z));
        V from{target.x,target.y,a.current[0].p.z+.5f};ColPoint ground{};void*support=nullptr;
        bool clear=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,from.z-1.3f-standing,ground,support,true,true,false,true,true,false,nullptr)&&finite(ground.point)&&ground.normal.z>.45f;
        V ivRoot{};int why=clear?0:1;if(clear){target.z=ground.point.z+clearance;ivRoot=target+V{0,0,1.f-clearance};
            const int room=nm_agent_recovery_room(a.agent,collision->world,&ivRoot.x,(2ull<<32)|uint32_t(ref));clear=room==1;if(room<0)ok(room);if(!clear)why=2;}
        if(clear)for(auto&[otherRef,other]:agents)if(otherRef!=ref&&!other->preview&&!other->mounted&&
            !(other->recovering&&other->recoveryAnimatedOwner)&&pedFromRef(otherRef)==other->ped){
            const int occupied=nm_agent_recovery_room_obstructed(other->agent,&ivRoot.x);if(occupied!=0){if(occupied<0)ok(occupied);clear=false;why=3;break;}}
        // A body lying against a car or a kerb found no room for its get-up for
        // over a minute (Ryder, 06/11), and a mission ped stays down that long.
        // Past RecoveryRoomWaitMs SA gets the get-up anyway, as it does next to
        // vehicles: its own ped collision moves the standing body clear.
        bool forced=false;
        if(!clear&&why!=1){if(!a.recoveryWaitSince)a.recoveryWaitSince=now;else if(now-a.recoveryWaitSince>=RecoveryRoomWaitMs){clear=true;forced=true;}}
        if(!clear){a.recoveryNextProbe=now+(a.ped==player()?300:2000);
            event("recovery_room_wait",ref,a.model,"physics_retained=1 pose_frozen=0 blocked_by="+std::string(why==1?"no_ground":why==2?"world_or_owner":"agent")+
                " ground_nz="+std::to_string(ground.normal.z)+" target_vs_body_m="+std::to_string(length(target-a.current[0].p))+
                " player_gap_m="+std::to_string(player()?length(V{position(player()).x-ivRoot.x,position(player()).y-ivRoot.y,0}):-1.f));continue;}
        V com{};if(!ok(nm_agent_get_com(a.agent,&com.x,&a.recoveryVelocity.x)))continue;
        const auto solved=a.rig->physicalBones(a.current);
        a.frozenBones=a.bones.empty()?solved:a.rig->blendBones(a.bones,solved,1.f);
        const V front=rotate(aligned.q,{0,1,0});const float heading=std::atan2(-front.x,front.y);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(a.ped,heading);
        at<float>(a.ped,0x558)=heading;at<float>(a.ped,0x55C)=heading;movePhysicalRoot(a.ped,target,a.playerVehicle);
        // Original leave-NM restores its captured owner velocity (8261E880).
        at<V>(a.ped,0x44)=a.recoveryVelocity*.02f;
        if(a.ownedFallenFlag){at<unsigned char>(a.ped,0x476)&=~16u;a.ownedFallenFlag=false;}
        a.recoveryAnimatedOwner=true;a.recoverStart=now;a.recoveryStage=3;a.sampleFrames=1;
        // Recompose matrices from the one evaluated clip after placement; no
        // UpdateAnimations(0), no second evaluation, no callback/time advance.
        if(publishCurrentNativePalette(a.ped))a.rig->incoming(a.ped,a.incoming,a.bones);
        recoveryAligned++;if(a.uprightRecovery)uprightRecoveries++;
        event("recovery_owner_committed",ref,a.model,"normal_compound=1 nm_wait_ended=1 room_volume="+std::string(forced?"wait_limit":"1")+" current_peer_bounds=1 retained_velocity_mps="+std::to_string(length(a.recoveryVelocity)));
    }
}
static float recoveryWeight(const Active&a){
    if(!a.recoveryAnimatedOwner)return 1.f;
    if(a.recoveryStage==4)return 0.f;
    return 1.f-recoveryEase((gameTime()-a.recoverStart)*.001f/blendSeconds);
}

static bool acceptRecoveryPlayerContact(Active&a,void*peer,float impulse,float dt,V peerVelocity){
    if(!a.recovering||!recoveryAllowed()||peer!=player()||a.ped==player()||!matrix(peer)||
       inVehicle(peer)||at<float>(a.ped,0x540)<=0||dot(peerVelocity,peerVelocity)<=.25f||impulse<=0)return false;
    // Host stage numbers are not original animation groups. Only a genuinely
    // running equivalent front/back get-up contributes its normalised phase.
    int clip=0;float phase=0;
    if(a.recoveryAnimatedOwner&&!a.uprightRecovery&&a.recoveryStage==4&&a.ownedAnimation&&
       nativePaletteEvaluatedClip(a.ped,a.ownedAnimation)&&a.animationDuration>0){
        clip=a.recoveryAnim==115?1:a.recoveryAnim==112?2:0;
        phase=at<float>(a.ownedAnimation,0x20)/a.animationDuration;
        if(phase<0||phase>1||!std::isfinite(phase))return false;
    }
    const V peerRoot=position(peer),look=peerRoot+V{0,0,.65f},root=position(a.ped);
    const float points[]={peerRoot.x,peerRoot.y,peerRoot.z,look.x,look.y,look.z,root.x,root.y,root.z};
    const unsigned slot=at<unsigned char>(a.ped,0x718);const int weapon=slot<13?at<int>(a.ped,0x5A0+slot*0x1C):-1;
    const unsigned flags=1u|((weapon>=22&&weapon<=38)?8u:0u)|(a.recoveryAnimatedOwner?16u:0u)|(at<unsigned char>(a.ped,0x484)!=1?4u:0u);
    const int selected=nm_agent_prepare_recovery_contact(a.agent,points,at<float>(a.ped,0x540)*2.f,impulse,dt,clip,phase,flags);
    if(selected<0){ok(selected);return false;}if(!selected)return false;
    if(!resumePhysicalContact(a,"recovery_original_contact_response"))return false;
    const int started=nm_agent_start_recovery_contact(a.agent,collision->world);if(started<=0){ok(started);return false;}
    a.kind=Reaction::Bump;a.bumpReady=false;a.meleeAttacker=reference(peer);a.meleeTaskOriginal=false;a.physicalAnimation=false;
    ensureRecoveryTask(a);
    event("recovery_original_response_started",a.ref,a.model,"task="+std::to_string(started)+" semantic_clip="+std::to_string(clip)+" phase="+std::to_string(phase)+" same_body=1 impulse_already_solved=1");return true;
}
