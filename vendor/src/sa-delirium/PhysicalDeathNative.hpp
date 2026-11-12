#pragma once
#include "PhysicalDeathContinuity.hpp"
// Include after CombatRuntime/Active. Inactive until the root installs the two
// verified native function wrappers and delegates the existing death-anim seam.
// No physical force or speed write exists in this bridge.
static std::map<int,physical_death_continuity::Record> physicalDeaths;
struct PhysicalDeathScope {DamageEvent*event=nullptr;void*ped=nullptr;};
static thread_local PhysicalDeathScope physicalDeathScope{};
static thread_local PhysicalDeathScope roadAnimationScope{};
static physical_death_continuity::Identity physicalDeathIdentity(const Active&a){
    return {a.ref,a.model,reinterpret_cast<uintptr_t>(a.ped),
        reinterpret_cast<uintptr_t>(at<void*>(a.ped,0x18)),
        reinterpret_cast<uintptr_t>(a.agent),a.admittedFrame};
}
static void clearPhysicalDeaths(){physicalDeaths.clear();physicalDeathScope={};}
static void forgetPhysicalDeath(int ref){physicalDeaths.erase(ref);}
static bool replacePhysicalDeathAnimation(DamageEvent*d,void*p){
    if(roadAnimationScope.event==d&&roadAnimationScope.ped==p){
        d->anim=191;d->flags&=~2u;
        auto&deathTime=at<unsigned>(p,0x750);
        if(deathTime==0||int(d->time-deathTime)>0)deathTime=d->time;
        return true;
    }
    if(physicalDeathScope.event!=d||physicalDeathScope.ped!=p)return false;
    auto a=agents.find(reference(p));auto r=physicalDeaths.find(reference(p));
    if(a==agents.end()||r==physicalDeaths.end()||a->second->ped!=p||
       !r->second.selected(physicalDeathIdentity(*a->second),reinterpret_cast<uintptr_t>(d)))return false;
    d->anim=191;d->flags&=~2u;return true;
}
static bool consumePhysicalImpactDeath(Active&a){
    auto r=physicalDeaths.find(a.ref);
    return r!=physicalDeaths.end()&&r->second.takeOver(physicalDeathIdentity(a),at<float>(a.ped,0x540)<=0);
}
static void(__thiscall*originalPhysicalDamageResponse)(void*,DamageEvent*,void*,void*,void*)=nullptr;
static void __fastcall physicalDamageResponse(void*handler,void*,DamageEvent*d,void*active,void*simplest,void*aborted){
    void*p=at<void*>(handler,0);const int ref=reference(p);
    auto a=agents.find(ref);bool own=false;physical_death_continuity::Facts f{};
    auto*shot=matchedGroundShot(*d,p);
    const bool road=roadReplacesDamageAnimation(*d,p);
    if(shot){
        d->anim=191;d->flags&=~2u;
        shot->groundShot=shotReceipt(*d);shot->groundShotDelivered=true;
        event("owned_shot_native_reaction_replaced",ref,shot->model,"weapon="+std::to_string(d->weapon)+" native_accounting=1");
    }
    if(a!=agents.end()&&a->second->ped==p){
        auto&body=*a->second;const auto&hit=body.roadDamage;
        f.owner=physicalDeathIdentity(body);f.accepted={hit.time,hit.vehicle,hit.weapon,hit.piece};
        f.delivered={d->time,vehicleRef(d->source),d->weapon,d->piece};
        f.enabled=ready&&playing();f.protectedContext=cutscene()||(protectMissions&&scriptMissionActive());
        f.inVehicle=inVehicle(p);f.preview=body.preview;f.mounted=body.mounted;
        f.impact=body.kind==Reaction::Impact;f.healthZero=at<float>(p,0x540)<=0||(road&&d->response.zero);
        f.calculated=d->response.calculated;f.killed=d->response.zero;
        // Animated handback currently has a different owner and must be
        // explicitly transferred before it can enter this branch.
        f.physicalOwner=!body.recoveryAnimatedOwner;f.ownCarrier=simplest&&simplest==body.ownedTask;
        if(shot){
            f.matchedShot=true;f.healthZero=f.killed;f.physicalOwner=true;
            // The receipt was matched on the real source, weapon, piece and
            // native event time. Keep the shot key for the native Die carrier.
            f.accepted=f.delivered={d->time,reference(d->source),d->weapon,d->piece};
        }
        if((!simplest||f.ownCarrier||road)&&physical_death_continuity::acceptedPhysicalDeath(f))
            own=physicalDeaths[ref].begin(f,reinterpret_cast<uintptr_t>(d));
    }
    const auto previous=physicalDeathScope;
    const auto previousRoad=roadAnimationScope;
    if(road){
        const int nativeAnim=d->anim;
        d->anim=191;d->flags&=~2u;
        if(d->response.zero||at<float>(p,0x540)<1.f){
            roadAnimationScope={d,p};
            // The stock fall/get-up branch can overwrite the sentinel.
            simplest=nullptr;
        }
        event("native_road_damage_animation_replaced",ref,at<short>(p,0x22),
              "native_anim="+std::to_string(nativeAnim)+" vehicle_ref="+std::to_string(vehicleRef(d->source))+
              " fatal="+std::to_string(d->response.zero));
    }
    if(own){
        physicalDeathScope={d,p};d->anim=191;d->flags&=~2u;
        // 4C0987 selects FloorHit directly for SA GetUp205. This task is
        // merely our NM ownership carrier, not the actual body action.
        if(f.ownCarrier)simplest=nullptr;
    }
    originalPhysicalDamageResponse(handler,d,active,simplest,aborted);
    physicalDeathScope=previous;
    roadAnimationScope=previousRoad;
    if(!own)return;
    a=agents.find(ref);auto r=physicalDeaths.find(ref);
    if(a==agents.end()||r==physicalDeaths.end()||a->second->ped!=p)return;
    void*task=at<void*>(handler,0x24);
    const bool nativeDie=task&&at<void*>(task,0)==reinterpret_cast<void*>(0x86DE78)&&at<int>(task,0x14)==191;
    if(r->second.taskCreated(physicalDeathIdentity(*a->second),reinterpret_cast<uintptr_t>(task),nativeDie))
        event(shot?"shot_native_death_selected":"impact_native_death_selected",ref,a->second->model,"native_complex_die=1 attributed_event=1 same_body=1");
    else physicalDeaths.erase(r);
}
static void*(__thiscall*originalPhysicalDieFirst)(void*,void*)=nullptr;
static void* __fastcall physicalDieFirst(void*task,void*,void*p){
    // Original sets DIE, clears native tasks and constructs SimpleDie. Only
    // its animation-independent StartAnim bookkeeping is supplied below.
    void*simple=originalPhysicalDieFirst(task,p);
    const int ref=reference(p);auto a=agents.find(ref);auto r=physicalDeaths.find(ref);
    if(a==agents.end()||r==physicalDeaths.end()||a->second->ped!=p)return simple;
    const bool nativeSimpleDie=simple&&at<void*>(simple,0)==reinterpret_cast<void*>(0x86DDBC)&&at<int>(simple,0xC)==191;
    if(!r->second.initializeTask(physicalDeathIdentity(*a->second),reinterpret_cast<uintptr_t>(task),nativeSimpleDie))return simple;
    // Exact 637587..6375A5 tail. No BlendAnimation, position or velocity write.
    reinterpret_cast<void(__thiscall*)(void*)>(0x5E5320)(p);
    at<float>(p,0x540)=0;
    reinterpret_cast<void(__thiscall*)(void*)>(0x5DED10)(p);
    at<unsigned>(p,0x750)=gameTime();
    event("impact_native_death_bookkeeping",ref,a->second->model,"clear_all=1 stop_nonpartial=1 native_death_time=1 velocity_unchanged=1");
    return simple;
}
