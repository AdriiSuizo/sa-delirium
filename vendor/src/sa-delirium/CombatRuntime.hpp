#pragma once
#include "FinalPedPoseHistory.hpp"
#include "CombatSkeleton.hpp"
#include "CombatNativeIntent.hpp"
#include "CombatMovement.hpp"
#include "CombatTaskRetirement.hpp"
#include "CombatPoseEpoch.hpp"
#include "CombatClub.hpp"
#include "MeleeDisarm.hpp"
// Live host of the pinned IV controller. No desktop input, root teleports or SA kick.
struct CombatActor {
    void*ped=nullptr;int model=-1,target=-1;unsigned traceAt=0,movementTraceAt=0;
    std::unique_ptr<Rig> rig;std::vector<T> pose,previous;V root{};float state[16]{},motionSeconds=0;
    int lastResult=-2,deferredIntent=-1,lastDecision=0;bool owned=false;
    CombatPoseHistory poseHistory;SeedEpoch poseEpoch{},previousPoseEpoch{},fedPoseEpoch{};T finalizedRoot{};bool authoredPose=false;
    void*clubClump=nullptr;int clubModel=-1;bool clubReady=false;std::array<float,8> clubBounds{};
    // Last drawn authored pose about the ped's root, and when: the release blend's source.
    std::vector<T> releaseLocal;unsigned releasedAt=0;
    // Last pose drawn while not owned (SA's or the IV locomotion's), and when the authored pose took over.
    std::vector<T> drawnLocal;unsigned drawnAt=0,ownedSince=0;
};
static NmCombat* combat=nullptr;
static bool combatFrameReady=false;
static uint64_t combatPoseSerial=0;
static std::map<int,CombatActor> fighters;
static std::map<int,std::pair<int,unsigned>> combatTargets;
struct CombatReceipt {int source=-1,piece=0;unsigned time=0;bool eventCombat=false;int eventResponse=-1,weapon=0;};
static std::map<int,CombatReceipt> combatReceipts;
struct CombatInput {bool aim=false;unsigned buttons=0;float x=127,y=127;} combatInput;
static unsigned combatProbeButtons=0,combatProbeUntil=0;static int combatProbeTarget=-1;
static bool combatProbeTimedDodge=false,combatProbeCounter=false;
static bool combatProbeLock=true;
static unsigned combatProbeAlternateStart=0;
static bool combatProbeAlternateSent=false;
static bool combatProbeFatalCounter=false;
static unsigned combatProbeGymStart=0,combatProbeGymUntil=0;static int combatProbeGymRef=-1;static unsigned char combatProbeGymOriginal=4;
static bool combatProbeGymPassive=false;
static bool combatAllowed(){return combat&&ready&&playing()&&player()&&!cutscene()&&!(protectMissions&&scriptMissionActive());}
static int combatWeapon(void*p){const unsigned slot=at<unsigned char>(p,0x718);return slot<13?at<int>(p,0x5A0+slot*0x1C):-1;}
static bool combatResponseActive(void*p){
    const int ref=reference(p);const auto physical=agents.find(ref);
    return nm_combat_receiving(combat,ref)==1||
        (physical!=agents.end()&&physical->second->ped==p&&!physical->second->preview);
}
static bool combatCanFight(void*p){
    if(!combatAllowed()||!validPed(p,false,false,p==player()))return false;
    const int weapon=combatWeapon(p);
    if(clubWeapon(weapon))return at<void*>(p,0x4F4)&&(p==player()||nativeClubTarget(p));
    if(weapon<0||weapon>1)return false;
    if(p==player())return true;
    const int decision=nm_combat_decision(combat,reference(p));if(decision==1904||decision==200)return false;
    const auto intent=combatReadNativeIntent(p);if(intent.type<0)return true;
    const auto target=combatTargets.find(reference(p));
    const auto receipt=combatReceipts.find(reference(p));
    const bool eventOwner=receipt!=combatReceipts.end()&&receipt->second.eventCombat&&target!=combatTargets.end()&&receipt->second.source==target->second.first;
    return (decision==1905||(decision==0&&eventOwner))&&intent.type==TASK_COMPLEX_SMART_FLEE_ENTITY&&target!=combatTargets.end()&&intent.source==pedFromRef(target->second.first);
}
static bool combatEligible(void*p){return combatAllowed()&&validPed(p,false,false,p==player())&&
    (combatCanFight(p)||combatResponseActive(p)||(p!=player()&&(nm_combat_decision(combat,reference(p))==1904||nm_combat_decision(combat,reference(p))==200)));}
static bool combatClearPath(void*p,void*t){
    if(!p||!t||!matrix(p)||!matrix(t))return false;
    ColPoint cp{};void*obstacle=nullptr;
    return !lineOfSight(position(p)+V{0,0,.3f},position(t)+V{0,0,.3f},cp,obstacle,true,true,false,true,false,false,false,false);
}
static bool combatOwns(void*p){auto i=fighters.find(reference(p));return combatAllowed()&&i!=fighters.end()&&i->second.ped==p&&i->second.owned&&!agents.contains(i->first)&&combatEligible(p);}
static bool combatLocomotionOwns(void*p){return combatOwns(p)&&fighters.at(reference(p)).state[0]!=3;}
static bool combatEndedAgainst(void*p,void*target){
    const int ref=reference(p);auto a=fighters.find(ref);
    return nm_combat_decision(combat,ref)==200&&a!=fighters.end()&&a->second.ped==p&&
        target&&pedFromRef(a->second.target)==target;
}
static bool(__cdecl* originalCombatGenerateDamage)(void*,void*,int,int,int,unsigned char)=nullptr;
static bool __cdecl combatGenerateDamage(void*victim,void*source,int weapon,int damage,int piece,unsigned char direction){
    if(firearmDamage(weapon)&&damage>0)if(auto*a=groundShotOwner(victim);a&&at<float>(victim,0x540)<=0){
        auto*manager=taskManager(victim);
        auto*task=manager?reinterpret_cast<void*(__thiscall*)(void*)>(0x6819D0)(manager):nullptr;
        // GenerateDamageEvent's dead-ped branch only blends FloorHit. Retain
        // the existing corpse and deliver this contact to its physical part.
        if(a->corpseResting||(task&&reinterpret_cast<int(__thiscall*)(void*)>(at<void**>(task,0)[4])(task)==TASK_SIMPLE_DEAD)){
            a->corpseResting=false;a->deadSince=gameTime();
            const int part=piece==9?12:piece==5?14:piece==6?18:piece==7?1:piece==8?4:piece==3?8:10;
            const V hit=a->current[part].p;
            const V impulse=directedHit(source?hit-position(source):matrix(victim)->up*-1.f,std::clamp(damage*.25f,3.f,35.f));
            ok(nm_agent_apply_impulse(a->agent,part,&hit.x,&impulse.x));
            event("grounded_corpse_shot",a->ref,a->model,"weapon="+std::to_string(weapon)+" same_agent=1 native_floor_hit_skipped=1");
            return true;
        }
    }
    // CWeapon::GenerateDamageEvent can be reached outside Fight::ProcessPed
    // by the player's old partial punch. It used SA's marker/range even while
    // the rendered IV action had not touched anything. Only our sweep owns
    // unarmed contact; retain the native accounting for its accepted receipt.
    if(!combatDelivering&&((weapon>=0&&weapon<=1)||(clubWeapon(weapon)&&combatOwns(source)))&&reference(victim)>=0&&
       reference(source)>=0&&(combatCanFight(source)||combatEndedAgainst(source,victim))&&validPed(victim,true,false,victim==player())){
        combatTargets[reference(source)]={reference(victim),gameTime()};
        event("combat_native_strike_suppressed",reference(victim),at<short>(victim,0x22),
              "attacker="+std::to_string(reference(source))+" weapon="+std::to_string(weapon)+" authored_contact_required=1");
        return false;
    }
    return originalCombatGenerateDamage(victim,source,weapon,damage,piece,direction);
}
static bool combatFightTask(void*task,void*ped){
    if(!combatEligible(ped))return false;
    if(ped!=player()&&combatEndedAgainst(ped,at<void*>(task,0x18)))return true;
    // Native retarget/pursuit requests cannot replace the source and clock of
    // a received reaction. The native task remains intact for the handback.
    if(combatResponseActive(ped))return !agents.contains(reference(ped));
    void*target=at<void*>(task,0x18);const int ref=reference(target);
    if(ref<0||!validPed(target,true,false,target==player()))return false;
    combatTargets[reference(ped)]={ref,gameTime()};
    if(clubWeapon(combatWeapon(ped)))return combatOwns(ped);
    // Keep the native seek outside melee range; own action clocks inside it.
    return length(position(target)-position(ped))<5.5f&&combatClearPath(ped,target)&&!agents.contains(reference(ped));
}
static bool(__thiscall* originalCombatJumpJustDown)(void*)=nullptr;
static bool __fastcall combatJumpJustDown(void*pad,void*){return combatOwns(player())?false:originalCombatJumpJustDown(pad);}
static bool(__thiscall* originalCombatExitVehicle)(void*)=nullptr;
static bool(__thiscall* originalCombatExitVehicleJustDown)(void*)=nullptr;
// The IV Y/Triangle action is SA's vehicle-entry button. Sample its original
// provider before the task pass, then consume it only while melee owns CJ.
static bool __fastcall combatExitVehicle(void*pad,void*){return combatOwns(player())?false:originalCombatExitVehicle(pad);}
static bool __fastcall combatExitVehicleJustDown(void*pad,void*){return combatOwns(player())?false:originalCombatExitVehicleJustDown(pad);}
static void(__thiscall* originalCombatUpdatePosition)(void*)=nullptr;
static void __fastcall combatUpdatePosition(void*p,void*){
    invalidateOriginalPedMotorUpdate(p);
    if(combatOwns(p)&&fighters.at(reference(p)).state[0]==3){
        auto&a=fighters.at(reference(p));nikoUpdatePosition(p);
        const unsigned axes=nm_combat_mover_axes(combat,reference(p));
        if(axes&4)at<float>(p,0x558)=at<float>(p,0x55C)=a.state[1];
        const float yaw=at<float>(p,0x558),c=std::cos(yaw),s=std::sin(yaw);
        const float x=at<float>(p,0x550),y=at<float>(p,0x554);
        const float scale=combatMotionScale(a.motionSeconds,*reinterpret_cast<float*>(0xB7CB5C));
        const float right=axes&1?a.state[2]*scale:c*x+s*y,forward=axes&2?a.state[3]*scale:-s*x+c*y;
        at<float>(p,0x550)=c*right-s*forward;at<float>(p,0x554)=s*right+c*forward;
        originalCombatUpdatePosition(p);return;
    }
    if(combatOwns(p)){
        auto&a=fighters.at(reference(p));const float yaw=a.state[1];
        at<float>(p,0x558)=at<float>(p,0x55C)=yaw;
        // Consume this clip step's displacement in the native world step.
        // Contact queries run afterwards at the collision-resolved root.
        // CPed still resolves support/collisions; no direct position write.
        const float nativeStep=*reinterpret_cast<float*>(0xB7CB5C);
        const float scale=combatMotionScale(a.motionSeconds,nativeStep);
        const float c=std::cos(yaw),s=std::sin(yaw);
        at<float>(p,0x550)=(c*a.state[2]-s*a.state[3])*scale;
        at<float>(p,0x554)=(s*a.state[2]+c*a.state[3])*scale;
        const V requested{at<float>(p,0x550)*50.f,at<float>(p,0x554)*50.f,0};
        originalCombatUpdatePosition(p);
        if(a.state[4]>=0&&gameTime()-a.movementTraceAt>=50){
            const V actual=at<V>(p,0x44)*50.f;a.movementTraceAt=gameTime();
            event("combat_mover",reference(p),a.model,"phase="+std::to_string(a.state[5])+" requested="+std::to_string(requested.x)+","+std::to_string(requested.y)+" actual="+std::to_string(actual.x)+","+std::to_string(actual.y)+" standing="+std::to_string(at<unsigned char>(p,0x46C)&1)+" time_step="+std::to_string(nativeStep)+" authored_seconds="+std::to_string(a.motionSeconds));
        }
        return;
    }
    nikoUpdatePosition(p);injuredUpdatePosition(p);
    originalCombatUpdatePosition(p);
}
static void(__thiscall* originalCombatDamageAnim)(DamageEvent*,void*,bool)=nullptr;
static void(__thiscall* originalCombatDeathAnim)(DamageEvent*,void*,bool)=nullptr;
static bool combatReplacesNativeReaction(DamageEvent*damage,void*ped){
    auto r=combatReceipts.find(reference(ped));
    if(combatAllowed()&&r!=combatReceipts.end()&&sameDeliveredCombatDamage(
        {r->second.time,r->second.source,r->second.weapon,r->second.piece},
        {damage->time,reference(damage->source),damage->weapon,damage->piece},gameTime())){
        // Only our delivered event: keep native health/armour/crime/hostility.
        // SA's sentinel is 191, not -1: GenerateDamageEvent compares it at
        // 0x73A7B0 before calling CAnimManager. -1 indexes before the group.
        damage->anim=191;damage->flags&=~2u;return true;
    }
    return false;
}
static void __fastcall combatDamageAnim(DamageEvent*damage,void*,void*ped,bool abort){
    if(matchedGroundShot(*damage,ped)){damage->anim=191;damage->flags&=~2u;return;}
    if(combatReplacesNativeReaction(damage,ped))return;
    originalCombatDamageAnim(damage,ped,abort);
}
static void __fastcall combatDeathAnim(DamageEvent*damage,void*,void*ped,bool abort){
    if(replacePhysicalDeathAnimation(damage,ped))return;
    // ComputeAnim selects a different routine once health reaches zero. SA's
    // death routine adds force.z=5 even for an authored counter with no impact.
    // Replace that reaction before it runs; damage/death/crime still run in SA.
    auto held=agents.find(reference(ped));
    const bool entryDeath=held!=agents.end()&&held->second->ped==ped&&matchesEntryFatalDamage(*held->second,*damage);
    if(entryDeath){damage->anim=191;damage->flags&=~2u;}
    if(entryDeath||combatReplacesNativeReaction(damage,ped)){
        // CTaskSimpleDie::StartAnim normally records this timestamp. Its
        // no-animation path skips StartAnim entirely. Leaving it at zero
        // makes CPopulation::ManagePed consider a new corpse >30 s old.
        // Initialize at the accepted fatal event, once, not on every update.
        auto&deathTime=at<unsigned>(ped,0x750);
        if(deathTime==0||int(damage->time-deathTime)>0)deathTime=damage->time;
        event(entryDeath?"entry_native_death_replaced":"combat_native_death_replaced",reference(ped),at<short>(ped,0x22),"event_ms="+std::to_string(damage->time)+" fixed_vertical_kick_not_applied=1");return;
    }
    originalCombatDeathAnim(damage,ped,abort);
}
static void initializeCombat(){
    const auto ini=(folder.parent_path()/"DeliriumSA.ini").string();
    if(!GetPrivateProfileIntA("Combat","Enabled",0,ini.c_str()))return;
    const auto data=(folder/"combat").string();combat=nm_combat_load(data.c_str());
    if(!combat){event("combat_load_failed");return;}
    if(!ok(nm_combat_enable(combat,data.c_str()))){nm_combat_destroy(combat);combat=nullptr;return;}
    event("combat_loaded",-1,-1,"controller=LSC4_380c8cb7d49a authored_assets=IV target_identity=pool_generation next_step_requests=1 native_guns=1");
}
static void combatClear(){
    nikoClear();
    combatFrameReady=false;
    if(combat)nm_combat_remove(combat,UINT32_MAX);
    fighters.clear();combatTargets.clear();combatReceipts.clear();combatInput={};
}
static void combatPrepare(){
    combatInput={};if(!combatCanFight(player())||agents.contains(reference(player())))return;
    auto*pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);if(!pad)return;
    combatInput.aim=reinterpret_cast<bool(__thiscall*)(void*)>(0x540670)(pad);
    const auto attack=reinterpret_cast<unsigned char(__thiscall*)(void*,bool)>(0x540340)(pad,false);
    const bool block=reinterpret_cast<bool(__thiscall*)(void*)>(0x540750)(pad);
    const bool kick=reinterpret_cast<bool(__thiscall*)(void*)>(0x5407A0)(pad);
    const bool second=originalCombatExitVehicle&&originalCombatExitVehicle(pad);
    combatInput.buttons=(combatInput.aim?1u:0u)|(attack?0x20u:0u)|(block?0x40u:0u)|(kick?0x80u:0u)|(second?0x10u:0u);
    combatInput.x=std::clamp(127.f+reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad),0.f,255.f);
    combatInput.y=std::clamp(127.f+reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad),0.f,255.f);
    // Fixture input enters only this module, never CPad or Windows input.
    if(combatProbeGymUntil&&gameTime()>=combatProbeGymUntil){
        if(void*p=pedFromRef(combatProbeGymRef))at<unsigned char>(p,0x72D)=combatProbeGymOriginal;
        combatProbeGymUntil=0;
    }
    if(combatProbeUntil>gameTime()&&GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)){
        combatInput.aim=combatProbeLock;combatInput.buttons=(combatProbeLock?1u:0u)|combatProbeButtons;combatInput.x=combatInput.y=127;
        if(combatProbeAlternateStart){
            const unsigned elapsed=gameTime()-combatProbeAlternateStart;
            if(elapsed<80)combatInput.buttons|=0x10u;
            else if(!combatProbeAlternateSent){
                char clip[160]{};
                nm_combat_clip(combat,reference(player()),clip,sizeof(clip));
                if(std::string_view(clip)=="melee_unarmed_base/cross_r_recoil"){
                    combatInput.buttons|=0x10u;
                    combatProbeAlternateSent=true;
                }
            }
        }
        if(combatProbeGymUntil>gameTime()&&gameTime()-combatProbeGymStart>600&&
           (gameTime()-combatProbeGymStart)%180<40)combatInput.buttons|=0x80u;
        if(combatProbeTimedDodge){
            const auto foe=fighters.find(combatProbeTarget);
            // Tests only: GTASA_PROBE_DODGE_PHASE=x moves the block press to the foe's swing phase (x, x+0.07); default 0.35 (club swings).
            static const float dodgeAt=[]{char b[16]{};return GetEnvironmentVariableA("GTASA_PROBE_DODGE_PHASE",b,16)?float(std::atof(b)):.35f;}();
            if(foe!=fighters.end()&&foe->second.state[4]>=0&&foe->second.state[5]>dodgeAt&&foe->second.state[5]<dodgeAt+.07f)combatInput.buttons|=0x40u;
        }
        if(combatProbeCounter&&GetEnvironmentVariableA("GTASA_PROBE_LOCK_ON",nullptr,0)){combatInput.aim=true;combatInput.buttons|=1u;} // tests: IV disarm needs lock-on
        if(combatProbeCounter){
            const auto cj=fighters.find(reference(player()));char clip[160]{};nm_combat_clip(combat,reference(player()),clip,sizeof(clip));
            if(cj!=fighters.end()&&std::string(clip).find("dodge_")!=std::string::npos&&cj->second.state[5]>.65f&&cj->second.state[5]<.73f)combatInput.buttons|=0x20u;
        }
    }
    if(GetEnvironmentVariableA("GTASA_COMBAT_INPUT_AUDIT",nullptr,0)){
        static unsigned prior=UINT32_MAX;const unsigned value=combatInput.buttons|(combatInput.aim?0x100u:0u);
        if(value!=prior){prior=value;event("combat_input_audit",reference(player()),0,"buttons="+std::to_string(combatInput.buttons)+" aim="+std::to_string(combatInput.aim)+" fixture="+std::to_string(combatProbeUntil>gameTime()));}
    }
}
static CombatActor* combatRegister(void*p){
    const int ref=reference(p),model=at<short>(p,0x22);auto it=fighters.find(ref);
    if(it!=fighters.end()&&it->second.ped==p&&it->second.model==model)return &it->second;
    try{
        CombatActor a;a.ped=p;a.model=model;a.rig=std::make_unique<Rig>(p,sourceBuild);
        std::vector<uint32_t> ids(a.rig->ids.begin(),a.rig->ids.end()),parents(a.rig->parents.begin(),a.rig->parents.end());std::vector<float> bind(ids.size()*7);
        for(size_t i=0;i<ids.size();i++){auto&t=a.rig->bind[i];float*o=bind.data()+7*i;o[0]=t.p.x;o[1]=t.p.y;o[2]=t.p.z;o[3]=t.q.x;o[4]=t.q.y;o[5]=t.q.z;o[6]=t.q.w;}
        const int type=at<int>(p,0x598);const char*profile=p==player()?"player":type==6?"m_y_cop":type>=7&&type<=16?"m_y_prison":"m_y_thief";
        if(!ok(nm_combat_register(combat,ref,p==player(),profile,ids.data(),parents.data(),bind.data(),int(ids.size()))))return nullptr;
        const auto& head=a.rig->headOffset;const float headRow[]={head.p.x,head.p.y,head.p.z,head.q.x,head.q.y,head.q.z,head.q.w};
        if(!ok(nm_combat_bind_geometry(combat,ref,a.rig->build,a.rig->node.data(),a.rig->spineFractions.data(),headRow)))return nullptr;
        if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)){
            const int needed=nm_combat_geometry(combat,ref,nullptr,0);std::vector<char> geometry(needed>0?needed:1);nm_combat_geometry(combat,ref,geometry.data(),int(geometry.size()));
            std::ofstream f(folder/("combat-rig-"+std::to_string(model)+".json"));f<<"{\"ids\":[";for(size_t n=0;n<ids.size();n++){if(n)f<<',';f<<ids[n];}f<<"],\"parents\":[";for(size_t n=0;n<parents.size();n++){if(n)f<<',';f<<parents[n];}f<<"],\"bind\":[";for(size_t n=0;n<bind.size();n++){if(n)f<<',';f<<bind[n];}f<<"],\"parts\":"<<geometry.data()<<"}";
        }
        auto*stats=at<void*>(p,0x59C);
        event("combat_actor",ref,model,std::string("profile=")+profile+" player="+std::to_string(p==player())+" sa_stats="+std::to_string(stats?at<unsigned>(stats,0):UINT32_MAX)+" attack="+std::to_string(stats?at<float>(stats,0x28):1.f)+" defend="+std::to_string(stats?at<float>(stats,0x2C):1.f));
        return &fighters.insert_or_assign(ref,std::move(a)).first->second;
    }catch(const std::exception&e){event("combat_rig_rejected",ref,model,e.what());return nullptr;}
}
constexpr float combatReleaseSeconds=.25f;
static bool combatRender(void*p){
    auto i=fighters.find(reference(p));if(i==fighters.end()||!combatOwns(p))return false;
    auto&a=i->second;auto*h=hierarchy(p);if(!h||h->num!=int(a.pose.size()))return false;
    auto pose=a.pose;const V delta=position(p)-a.root;for(auto&t:pose)t.p=t.p+delta;
    // Taking over: the authored pose's own blend starts from SA's raw palette, not from what was on screen
    // (the IV locomotion layer). Blend in from the last drawn pose over the same time as the release.
    const unsigned now=gameTime();
    if(a.releaseLocal.empty()||now-a.releasedAt>50)a.ownedSince=now;
    if(const float t=(now-a.ownedSince)*.001f;t<combatReleaseSeconds&&a.drawnLocal.size()==pose.size()&&a.ownedSince-a.drawnAt<=100){
        const T root=transform(*matrix(p));std::vector<T> shown;shown.reserve(pose.size());for(const auto&l:a.drawnLocal)shown.push_back(mul(root,l));
        pose=a.rig->blendBones(shown,pose,t/combatReleaseSeconds);
    }
    const auto output=a.rig->renderPalette(h->matrices,pose);std::copy(output.begin(),output.end(),h->matrices);
    const T root=inverse(transform(*matrix(p)));a.releaseLocal.clear();for(const auto&t:pose)a.releaseLocal.push_back(mul(root,t));a.releasedAt=gameTime();
    return true;
}
// When the authored melee pose stops owning the body (the stance quit, a hit
// reaction over) SA's own pose came back in one frame: up to 0.6 m at a hand.
// IV's move network never cuts out of a melee clip; draw the last authored pose
// blending into the current on-foot pose over this time. Drawing only: SA's
// movement, heading and contacts are already its own.
static void combatReleaseRender(void*p){
    auto i=fighters.find(reference(p));if(i==fighters.end())return;auto&a=i->second;
    if(a.releaseLocal.empty())return;
    const unsigned now=gameTime();const float t=(now-a.releasedAt)*.001f;
    auto*h=hierarchy(p);std::array<T,21> parts;std::vector<T> current;
    if(a.ped!=p||agents.contains(i->first)||now<a.releasedAt||t>=combatReleaseSeconds||!h||h->num!=int(a.releaseLocal.size())||!a.rig->incoming(p,parts,current)){a.releaseLocal.clear();return;}
    // (the drawn pose for a later take-over is recorded by combatRecordDrawn)
    const T root=transform(*matrix(p));std::vector<T> held;held.reserve(a.releaseLocal.size());for(const auto&l:a.releaseLocal)held.push_back(mul(root,l));
    const auto blended=a.rig->blendBones(current,held,1.f-t/combatReleaseSeconds);
    const auto output=a.rig->renderPalette(h->matrices,blended);std::copy(output.begin(),output.end(),h->matrices);
}
// After every non-owned draw: what is on screen now, about the root, for the next take-over.
static void combatRecordDrawn(void*p){
    auto i=fighters.find(reference(p));if(i==fighters.end()||i->second.ped!=p)return;auto&a=i->second;
    std::array<T,21> parts;std::vector<T> current;if(!a.rig->incoming(p,parts,current)){a.drawnLocal.clear();return;}
    const T root=inverse(transform(*matrix(p)));a.drawnLocal.clear();for(const auto&t:current)a.drawnLocal.push_back(mul(root,t));a.drawnAt=gameTime();
}
static bool combatQueryPose(void*p,std::vector<T>&pose){
    if(!combatOwns(p))return false;auto&a=fighters.at(reference(p));pose=a.pose;
    const V delta=position(p)-a.root;for(auto&t:pose)t.p=t.p+delta;return !pose.empty();
}
static int combatPart(int iv){const int part=combatBodyPart(iv);return part<0?10:part;}
static int combatPiece(int part){return part==12?9:part>=13&&part<=16?5:part>=17?6:part>=1&&part<=3?7:part>=4&&part<=6?8:3;}
struct CombatNativeRetirement {
    void*ped;void*target;
    void*primary(int slot){auto*m=taskManager(ped);return m?at<void*>(m,4*slot):nullptr;}
    bool matchesPrimary(void*t){
        // A melee child inside police/arrest/script AI is not ownership of its
        // parent. Only retire the standalone carriers that combat adapts.
        if(!t)return false;
        const int rootType=reinterpret_cast<int(__thiscall*)(void*)>(at<void**>(t,0)[4])(t);
        if(!combatOwnsPrimaryRoot(rootType))return false;
        for(int depth=0;t&&depth<16;depth++){
            auto*v=at<void**>(t,0);const int type=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t);
            if(type==TASK_COMPLEX_KILL_PED_ON_FOOT_MELEE&&at<void*>(t,0xC)==target)return true;
            t=reinterpret_cast<void*(__thiscall*)(void*)>(v[2])(t);
        }return false;
    }
    CombatAttackTask attack(){
        auto*m=taskManager(ped);void*t=m?at<void*>(m,0x14):nullptr;if(!t)return {};
        auto*v=at<void**>(t,0);const bool fight=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t)==TASK_SIMPLE_FIGHT;
        return {t,fight?at<void*>(t,0x18):nullptr,fight};
    }
    bool abort(void*t,int urgency){auto*v=at<void**>(t,0);return reinterpret_cast<bool(__thiscall*)(void*,void*,int,void*)>(v[6])(t,ped,urgency,nullptr);}
    void clearPrimary(int slot){reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(ped),nullptr,slot,true);}
    void clearAttack(){reinterpret_cast<void(__thiscall*)(void*,void*,int)>(0x681B60)(taskManager(ped),nullptr,0);}
};
static void combatApplyDecision(CombatActor&a,int decision){
    if(a.ped==player()||combatResponseActive(a.ped)||inVehicle(a.ped)||at<float>(a.ped,0x540)<=0)return;
    void*target=pedFromRef(a.target);if(!target)return;
    if(decision==1904){
        const auto intent=combatReadNativeIntent(a.ped);
        if(intent.type==TASK_COMPLEX_SMART_FLEE_ENTITY&&intent.source==target)return;
        if(intent.type>=0)return;
        void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x40);if(!task)return;
        // Original Retreat's route distance is 40 m (pedcombat.rs). SA executes
        // navigation; no substitute body force, stun, turn rate or flee roll.
        reinterpret_cast<void*(__thiscall*)(void*,void*,bool,float,int,int,float)>(0x65C430)(task,target,false,40.f,1000000,1000,1.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(a.ped),task,1,true);
        event("combat_iv_retreat_navigation",reference(a.ped),a.model,"target="+std::to_string(a.target)+" original_decision=1904 native_navigation=1");
    }else if(decision==200){
        // Original control has finished and no received/physical action owns
        // this ped. Retire its matching native carriers through their normal
        // urgent cleanup, including TASK_SECONDARY_ATTACK; slot0/3/4 remain.
        // Verified local exe: 6210F0 delegates;61DD00 leisure always refuses;
        // 6239F0 urgent+null detaches callbacks, marks finished and returns true.
        CombatNativeRetirement host{a.ped,target};
        if(!retireEndedCombatTasks(host))return;
        combatTargets.erase(reference(a.ped));
        event("combat_iv_target_released",reference(a.ped),a.model,"target="+std::to_string(a.target)+" original_upkeep=1 secondary_fight_retired=1");
    }
}
static void combatFeedDecision(void*p,int ref,void*foe,unsigned now,const std::map<int,int>&targets){
    if(p==player()||clubWeapon(combatWeapon(p)))return;
    const int kind=at<int>(p,0x598),targetKind=foe?at<int>(foe,0x598):-1;
    const bool population=at<unsigned char>(p,0x484)==1;
    const bool supported=population&&(kind==4||kind==5||(kind>=7&&kind<=16))&&combatWeapon(p)>=0&&combatWeapon(p)<=1;
    const auto intent=combatReadNativeIntent(p);
    const bool sameFlee=foe&&intent.type==TASK_COMPLEX_SMART_FLEE_ENTITY&&intent.source==foe;
    const bool hard=intent.type>=0&&!sameFlee;
    const auto receipt=combatReceipts.find(ref);
    const bool eventOwner=receipt!=combatReceipts.end()&&receipt->second.eventCombat&&foe&&receipt->second.source==reference(foe);
    const bool answeredNone=receipt!=combatReceipts.end()&&foe&&receipt->second.source==reference(foe)&&receipt->second.eventResponse==200;
    unsigned flags=(supported?1u:0u)|(targets.contains(ref)&&!answeredNone&&(intent.type<0||(sameFlee&&eventOwner))?2u:0u)|(hard?4u:0u)|(sameFlee?8u:0u)|
        (foe==player()?16u:0u)|(targetKind==6?32u:0u)|(foe&&inVehicle(foe)?64u:0u)|
        (kind>=7&&kind<=16?128u:0u)|(population?256u:0u)|(foe&&at<unsigned char>(foe,0x484)==2?512u:0u);
    if(foe&&targetKind>=0&&targetKind<32){const unsigned bit=1u<<targetKind;
        if((at<unsigned>(p,0x4E0)|at<unsigned>(p,0x4E4))&bit)flags|=1024u;
        if((at<unsigned>(p,0x4EC)|at<unsigned>(p,0x4F0))&bit)flags|=2048u;
    }
    if(auto receipt=combatReceipts.find(ref);receipt!=combatReceipts.end()&&foe&&receipt->second.source==reference(foe))flags|=16384u;
    // Count among the real nearest 16, not all registered combat actors.
    std::vector<std::pair<float,void*>> neighbours;auto*pool=pedPool();
    for(int n=0;pool&&n<pool->size;n++)if(!(pool->flags[n]&128)){
        void*other=pool->objects+n*0x7C4;if(other==p||!matrix(other)||at<float>(other,0x540)<50.f)continue;
        const V d=position(other)-position(p);neighbours.push_back({dot(d,d),other});
    }
    std::sort(neighbours.begin(),neighbours.end(),[](const auto&a,const auto&b){return a.first<b.first;});
    unsigned fights=0;for(size_t i=0;i<std::min<size_t>(16,neighbours.size());i++){
        void*other=neighbours[i].second;const int k=at<int>(other,0x598),r=reference(other);
        if(k>=7&&k<=16&&(targets.contains(r)||nm_combat_decision(combat,r)==1905))fights++;
    }
    const int w=foe?combatWeapon(foe):0;const float armed=w<=1?0.f:w<=15?1.f:2.f;
    const float row[8]={float(flags),foe?float(reference(foe)):-1.f,foe?std::max(0.f,at<float>(foe,0x540)*2.f):0.f,armed,
        foe?length(position(foe)-position(p)):0.f,foe?(combatClearPath(p,foe)?1.f:2.f):0.f,float(fights),0.f};
    ok(nm_combat_decision_feed(combat,ref,now,row));
}
static void combatTick(float dt){
    combatFrameReady=false;
    // Native movement uses clipped simulation time, not the millisecond clock
    // (the latter can advance 300 ms while this physics step advances 60 ms).
    dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    if(!combatAllowed()||dt<=0)return;
    // CCamera::m_aCams[active].m_vecFront, verified against the local SA SDK.
    // Keep mapped CPad values intact for attack/dodge selection. Only the
    // original locked stance projects its movement wish through this view.
    void*camera=reinterpret_cast<void*>(0xB6F028);
    const unsigned activeCamera=at<unsigned char>(camera,0x59);
    if(activeCamera<3){const V front=at<V>(camera,0x174+activeCamera*0x238+0x190);nm_combat_view(combat,front.x,front.y);}
    const unsigned now=gameTime();const int cj=reference(player());std::map<int,int> targets;
    for(auto it=combatTargets.begin();it!=combatTargets.end();){
        void*p=pedFromRef(it->first),*t=pedFromRef(it->second.first);
        const int intent=p&&p!=player()?combatNativeExitIntent(p):-1;
        if(p&&intent>=0&&combatResponseActive(p)){
            auto actor=fighters.find(it->first);
            if(actor!=fighters.end()&&actor->second.deferredIntent!=intent){
                actor->second.deferredIntent=intent;
                event("combat_native_intent_deferred",it->first,at<short>(p,0x22),"task="+std::to_string(intent)+" source_ref="+std::to_string(reference(combatReadNativeIntent(p).source))+" target_ref="+std::to_string(it->second.first)+" receiving="+std::to_string(nm_combat_receiving(combat,it->first))+" physical="+std::to_string(agents.contains(it->first)));
            }
        }
        const bool response=p&&combatResponseActive(p);
        const int decision=p?nm_combat_decision(combat,it->first):0;
        if(!p||!combatEligible(p)||(!response&&(!t||(decision==0&&now-it->second.second>10000)||at<float>(t,0x540)<=0))){
            if(p&&intent>=0)event("combat_native_intent_yield",it->first,at<short>(p,0x22),"task="+std::to_string(intent)+" source_ref="+std::to_string(reference(combatReadNativeIntent(p).source))+" target_ref="+std::to_string(it->second.first)+" receiving="+std::to_string(nm_combat_receiving(combat,it->first))+" physical="+std::to_string(agents.contains(it->first)));
            it=combatTargets.erase(it);continue;
        }
        targets[it->first]=it->second.first;++it;
    }
    if(combatCanFight(player())&&!combatResponseActive(player())&&(combatInput.aim||(combatInput.buttons&0x20))){
        void*selected=at<void*>(player(),0x71C);int target=reference(selected);float best=-1.e9f;
        if(target<0||!validPed(selected,true,false)){
            const V here=position(player()),look=matrix(player())->up;auto*pool=pedPool();target=-1;
            for(int n=0;pool&&n<pool->size;n++)if(!(pool->flags[n]&128)){
                void*p=pool->objects+n*0x7C4;if(!validPed(p,true,false)||p==player())continue;
                V d=position(p)-here;float distance=length(d);if(distance>4.5f||distance<.01f)continue;
                float alignment=dot(look,d*(1/distance));if(alignment<.15f||!combatClearPath(player(),p))continue;
                float score=alignment-distance*.08f;if(score>best){best=score;target=reference(p);}
            }
        }
        if(combatProbeUntil>now&&pedFromRef(combatProbeTarget))target=combatProbeTarget;
        targets[cj]=target;
    }else if(combatOwns(player()))targets[cj]=fighters[cj].target;
    std::map<int,bool> wanted;for(auto&[ref,target]:targets){wanted[ref]=true;if(target>=0)wanted[target]=true;}
    // IV controls exist before the task starts. Keep the player pad history
    // through ordinary on-foot frames; creating it on attack loses the edge.
    if(combatCanFight(player()))wanted[cj]=true;
    for(auto&[ref,a]:fighters)if(pedFromRef(ref)==a.ped&&combatEligible(a.ped)&&combatResponseActive(a.ped))wanted[ref]=true;
    for(auto&[ref,a]:agents)if(pedFromRef(ref)==a->ped&&length(position(a->ped)-position(player()))<8.f)wanted[ref]=true;
    std::erase_if(wanted,[](const auto&row){return injuredMoving(row.first);});
    for(auto it=fighters.begin();it!=fighters.end();){if(!wanted.contains(it->first)||pedFromRef(it->first)!=it->second.ped){nm_combat_remove(combat,it->first);it=fighters.erase(it);}else ++it;}
    for(auto[ref,_]:wanted){void*p=pedFromRef(ref);if(!p||!matrix(p)||!at<void*>(p,0x18))continue;
        watchSessionPed(p);
        auto*a=combatRegister(p);if(!a)continue;std::vector<T> native;
        const auto*completed=previousFinalPedPose(p,a->rig->ids);if(!completed)continue;
        native=completed->bones;
        // The consumer runs before native motion; preserve predecessor time.
        a->fedPoseEpoch={completed->frame,completed->serial,SeedPhase::EvaluatedEnd,0,0};
        if(native.size()!=a->rig->ids.size())continue;
        float frame[20]{};const V pos=position(p);frame[0]=pos.x;frame[1]=pos.y;frame[2]=pos.z;frame[3]=at<float>(p,0x558);frame[4]=at<float>(p,0x540)*2;
        const bool club=clubWeapon(combatWeapon(p));
        auto*clump=club?at<void*>(p,0x4F4):nullptr;const int model=club?at<int>(p,0x740):-1;
        if(clump!=a->clubClump||model!=a->clubModel){
            a->clubClump=clump;a->clubModel=model;a->clubReady=combatClubBounds(clump,a->clubBounds);
            if(club)event("combat_club_geometry",ref,a->model,"weapon="+std::to_string(combatWeapon(p))+" model="+std::to_string(model)+" ready="+std::to_string(a->clubReady));
        }
        if(!ok(nm_combat_feed_weapon(combat,ref,a->clubReady?a->clubBounds.data():nullptr,club&&bladeWeapon(combatWeapon(p)))))continue;
        const bool physical=agents.contains(ref)&&!agents[ref]->preview;void*foe=targets.contains(ref)?pedFromRef(targets[ref]):nullptr;
        const bool engaged=foe&&(!club||a->clubReady)&&combatCanFight(p)&&length(position(foe)-pos)<5.5f&&combatClearPath(p,foe);
        const bool receiving=nm_combat_receiving(combat,ref)==1;
        const auto active=agents.find(ref);
        const bool down=physical&&(nm_agent_has_fallen(active->second->agent)==1||active->second->current[10].p.z-active->second->current[0].p.z<.25f);
        const bool drive=physical&&active->second->physicalAnimation&&!active->second->recovering&&!down&&frame[4]>0;
        frame[5]=(p==player()?1:0)|((engaged||drive||receiving)?2:0)|((p==player()?combatInput.aim:engaged)?4:0)|(physical?8:0)|(frame[4]<=0?16:0)|(drive?32:0)|(down?64:0)|((!combatCanFight(p)||!engaged)?128:0);
        a->target=targets.contains(ref)?targets[ref]:(drive?active->second->meleeAttacker:-1);frame[6]=float(a->target);frame[8]=frame[9]=127;
        if(p==player()){frame[7]=float(combatInput.buttons);frame[8]=combatInput.x;frame[9]=combatInput.y;frame[10]=combatInput.aim?255.f:0;}
        const V vel=at<V>(p,0x44)*50.f;frame[12]=vel.x;frame[13]=vel.y;frame[14]=vel.z;
        frame[15]=at<int>(p,0x598)>=7&&at<int>(p,0x598)<=16?1.f:0.f;
        frame[19]=p==player()?float(at<unsigned char>(p,0x72D)):0.f;
        if(combatProbeGymPassive&&combatProbeGymUntil>now&&ref==combatProbeTarget&&GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))frame[5]=float(unsigned(frame[5])|256u);
        // CPedStat has the same AttackStrength/DefendWeakness semantics used
        // by the original action-table filters. Fear/temper/decision maker
        // remain in SA's primary task system, not a second aggression roll.
        if(p!=player())if(auto*stats=at<void*>(p,0x59C)){
            const float attack=at<float>(stats,0x28),defend=at<float>(stats,0x2C);
            if(std::isfinite(attack)&&std::isfinite(defend)&&attack>0&&defend>0){frame[16]=attack;frame[17]=defend;frame[18]=1;}
        }
        std::vector<float> pose(native.size()*7);for(size_t n=0;n<native.size();n++){const auto&t=native[n];float*o=pose.data()+7*n;o[0]=t.p.x;o[1]=t.p.y;o[2]=t.p.z;o[3]=t.q.x;o[4]=t.q.y;o[5]=t.q.z;o[6]=t.q.w;}
        if(!ok(nm_combat_feed(combat,ref,now,frame,pose.data(),int(native.size()))))continue;
        combatFeedDecision(p,ref,foe,now,targets);
    }
    if(!ok(nm_combat_begin(combat,now,dt)))return;
    for(auto&[ref,a]:fighters){void*p=pedFromRef(ref);if(p!=a.ped)continue;std::vector<float> output(a.rig->ids.size()*7);
        const int own=nm_combat_pose(combat,ref,output.data(),int(a.rig->ids.size()),a.state);if(own<0){ok(own);continue;}
        if(a.state[11]>=0&&pedFromRef(int(a.state[11])))combatTargets[ref]={int(a.state[11]),now};
        if(a.state[13]>=0)event("combat_request_step",ref,a.model,"attacker="+std::to_string(int(a.state[13]))+" taken="+std::to_string(int(a.state[12]))+" result="+std::to_string(int(a.state[4]))+" ownership="+std::to_string(own));
        a.owned=own==1||own==3;a.previousPoseEpoch=a.poseEpoch;a.previous=a.pose;a.pose.clear();a.root=position(p);a.motionSeconds=dt;
        a.poseHistory.begin(*reinterpret_cast<unsigned*>(0xB7CB4C),++combatPoseSerial);
        a.authoredPose=own==1||own==2||own==3;
        a.poseEpoch=a.authoredPose?SeedEpoch{a.poseHistory.frame,a.poseHistory.serial,SeedPhase::AuthoredEnd,dt,dt}:a.fedPoseEpoch;
        const int decision=int(a.state[15]);
        if(p!=player()&&decision!=a.lastDecision){event("combat_iv_decision",ref,a.model,"target="+std::to_string(a.target)+" from="+std::to_string(a.lastDecision)+" to="+std::to_string(decision));a.lastDecision=decision;}
        if(p!=player())combatApplyDecision(a,decision);
        for(size_t n=0;n<a.rig->ids.size();n++){float*o=output.data()+n*7;a.pose.push_back(T{{o[3],o[4],o[5],o[6]},{o[0],o[1],o[2]}});}
        if(!a.owned&&own!=2)continue;
        if(a.owned&&own!=3){reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(p,a.state[1]);at<float>(p,0x558)=at<float>(p,0x55C)=a.state[1];}
        if(a.lastResult!=int(a.state[4])||now-a.traceAt>=250){char clip[160]{};nm_combat_clip(combat,ref,clip,sizeof(clip));
            if(combatProbeGymUntil>now){char diagnostic[4096]{};if(nm_combat_gym_diagnostics(combat,ref,diagnostic,sizeof(diagnostic))>0)event("combat_gym_selector",ref,a.model,diagnostic);}
            if(a.clubReady&&GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)){
                if(auto*foe=pedFromRef(a.target))event("probe_club_spacing",ref,a.model,"clip="+std::string(clip)+" root_distance_m="+std::to_string(length(position(p)-position(foe)))+" forward_mps="+std::to_string(a.state[3])+" length_m="+std::to_string(length(V{a.clubBounds[3]-a.clubBounds[0],a.clubBounds[4]-a.clubBounds[1],a.clubBounds[5]-a.clubBounds[2]})));
            }
            event(a.lastResult!=int(a.state[4])?"combat_action":"combat_frame",ref,a.model,"target="+std::to_string(a.target)+" clip="+clip+" phase="+std::to_string(a.state[5])+" result="+std::to_string(int(a.state[4]))+" strike="+std::to_string(int(a.state[7]))+" blend="+std::to_string(a.state[9])+" yaw="+std::to_string(a.state[1])+" ownership="+std::to_string(own));a.lastResult=int(a.state[4]);a.traceAt=now;
        }
    }
    combatFrameReady=true;
}
static void combatFinish(){
    if(!combatFrameReady||!combatAllowed())return;combatFrameReady=false;
    injuredRender();
    const unsigned now=gameTime();const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    for(auto&[ref,a]:fighters){
        if(pedFromRef(ref)!=a.ped){nm_combat_remove(combat,ref);continue;}
        const V root=position(a.ped),delta=root-a.root;
        if(!a.poseHistory.finish(*reinterpret_cast<unsigned*>(0xB7CB4C)))continue;
        if(!ok(nm_combat_place(combat,ref,&root.x)))return;
        for(auto&t:a.pose)t.p=t.p+delta;a.root=root;a.finalizedRoot=transform(*matrix(a.ped));
        if(a.state[0]==3&&!agents.contains(ref)){
            // Re-evaluate the normal on-foot layer at this native world step,
            // then compose once before BOTH contact sampling and drawing.
            if(currentNativePaletteEvaluation(a.ped)&&publishCurrentNativePalette(a.ped)){
                nikoRender(a.ped);std::array<T,21> parts;std::vector<T> under;
                if(a.rig->incoming(a.ped,parts,under)){
                    std::vector<float> rows,output(under.size()*7);for(const auto&t:under)for(float v:{t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w})rows.push_back(v);
                    const T w=transform(*matrix(a.ped));const float world[]={w.p.x,w.p.y,w.p.z,w.q.x,w.q.y,w.q.z,w.q.w};
                    if(ok(nm_combat_compose_standalone(combat,ref,world,rows.data(),int(under.size())))&&nm_combat_pose(combat,ref,output.data(),int(under.size()),a.state)==3){
                        a.pose.clear();for(size_t n=0;n<under.size();n++){const float*v=output.data()+7*n;a.pose.push_back(T{{v[3],v[4],v[5],v[6]},{v[0],v[1],v[2]}});}
                    }
                }
            }
        }
        if(!a.authoredPose&&!agents.contains(ref)){
            const auto*e=currentNativePaletteEvaluation(a.ped);std::array<T,21> parts;std::vector<T> current;
            if(e&&publishCurrentNativePalette(a.ped)&&a.rig->incoming(a.ped,parts,current)){
                std::vector<float> rows;rows.reserve(current.size()*7);for(const auto&t:current)
                    for(float x:{t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w})rows.push_back(x);
                if(ok(nm_combat_refresh_native_pose(combat,ref,rows.data(),int(current.size())))){
                    a.pose=std::move(current);a.poseEpoch={e->frame,e->serial,SeedPhase::EvaluatedEnd,e->seconds,e->seconds};
                }
            }else a.poseEpoch.phase=SeedPhase::Unknown;
        }
    }
    const int hits=nm_combat_finish(combat);if(!ok(hits))return;
    for(int i=0;i<hits;i++){float hit[16]{};if(nm_combat_contact(combat,i,hit)!=1)continue;
        const int source=int(hit[0]),target=int(hit[1]),part=combatPart(int(hit[2]));void*p=pedFromRef(target),*by=pedFromRef(source);if(!p||!by||!fighters.contains(target))continue;
        // Hit kind 3: the taker's DisarmOpponent at its clip's 0x200 event (sa-combat runtime.rs): the foe's weapon changes hands, nothing else.
        if(hit[15]==3){meleeDisarm(by,p);continue;}
        const int held=combatWeapon(by),weapon=clubWeapon(held)?held:0;
        const float before=at<float>(p,0x540);const V motionBefore=at<V>(p,0x44);const int piece=combatPiece(part);
        const bool motorStart=hit[15]==2;
        // Diagnostic setup only, after a real counter has been selected. Low
        // health at spawn changes the original attack/dodge selection and does
        // not reliably exercise SelfDamage. Do not inject a hit or force a clip.
        if(motorStart&&combatProbeFatalCounter&&target==combatProbeTarget&&
           GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)){
            at<float>(p,0x540)=15.f;combatProbeFatalCounter=false;
            event("probe_combat_fatal_counter",target,at<short>(p,0x22),"sa_health_at_paired_start=15 original_damage_event_still_required=1");
        }
        combatTargets[target]={source,now};
        // A swing may also meet a bystander. Keep the attacker's existing
        // task target; the recipient's hostility still names the attacker.
        if(!combatTargets.contains(source)){
            const auto fighter=fighters.find(source);const int intended=fighter==fighters.end()?-1:fighter->second.target;
            combatTargets[source]={pedFromRef(intended)?intended:target,now};
        }
        if(!motorStart){
            const auto prior=combatReceipts.find(target);const bool alreadyOwned=prior!=combatReceipts.end()&&prior->second.source==source&&prior->second.eventCombat;
            const auto beforeIntent=combatReadNativeIntent(p);const float armourBefore=at<float>(p,0x548);
            const int priorResponse=prior!=combatReceipts.end()&&prior->second.source==source?prior->second.eventResponse:-1;
            combatReceipts[target]={source,piece,now,alreadyOwned,priorResponse,weapon};
            combatDelivering=true;reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,by,weapon,std::max(0,int(std::lround(hit[3]*.5f))),piece,0);combatDelivering=false;
            // Own the initiating event, not a flee flag observed after the hit.
            // A prior flee (even from this source), other panic, mission role,
            // non-civilian or harmless blow retains its existing host owner.
            const int kind=at<int>(p,0x598);const float harm=before-at<float>(p,0x540)+armourBefore-at<float>(p,0x548);
            if(hit[15]==0&&p!=player()&&(kind==4||kind==5)&&at<unsigned char>(p,0x484)==1&&beforeIntent.type<0&&harm>0&&at<float>(p,0x540)>0){
                const int sourceKind=at<int>(by,0x598);const unsigned bit=sourceKind>=0&&sourceKind<32?1u<<sourceKind:0;
                const unsigned relation=(by==player()?1u:0u)|((at<unsigned>(p,0x4EC)|at<unsigned>(p,0x4F0))&bit?2u:0u)|((at<unsigned>(p,0x4E0)|at<unsigned>(p,0x4E4))&bit?4u:0u);
                const int response=nm_combat_civilian_harm(combat,target,source,at<float>(p,0x540)*2.f,relation);
                if(response<0)ok(response);else{if(response!=0)combatReceipts[target].eventResponse=response;if(response==1900)combatReceipts[target].eventCombat=true;}
                event("combat_civilian_damage_event",target,at<short>(p,0x22),"source="+std::to_string(source)+" response="+std::to_string(response)+" prior_intent="+std::to_string(beforeIntent.type)+" health_iv="+std::to_string(at<float>(p,0x540)*2.f)+" harm_sa="+std::to_string(harm)+" event_owned="+std::to_string(combatReceipts[target].eventCombat));
            }
        }
        const V motionAfter=at<V>(p,0x44);
        event("combat_damage_motion",target,at<short>(p,0x22),"before_vz="+std::to_string(motionBefore.z*50.f)+" after_vz="+std::to_string(motionAfter.z*50.f)+" delta_speed="+std::to_string(length(motionAfter-motionBefore)*50.f)+" fatal="+std::to_string(at<float>(p,0x540)<=0));
        queueMeleeFrames(target);
        float contactGap=0;const bool geometric=nm_combat_contact_gap(combat,source,target,&contactGap)==1;
        if(geometric)event("combat_contact_geometry",target,at<short>(p,0x22),"attacker="+std::to_string(source)+" striker_iv="+std::to_string(int(hit[12]))+" signed_clearance_m="+std::to_string(contactGap)+" root_distance_m="+std::to_string(length(position(by)-position(p)))+" point="+std::to_string(hit[4])+","+std::to_string(hit[5])+","+std::to_string(hit[6])+" fitted_bounds=1 exact_boxes=1");
        event(motorStart?"combat_reaction_motor_start":hit[15]==1?"combat_authored_damage":"combat_contact",target,at<short>(p,0x22),"attacker="+std::to_string(source)+" part="+std::to_string(part)+" weapon="+std::to_string(weapon)+" damage_iv="+std::to_string(hit[3])+" health_after="+std::to_string(at<float>(p,0x540))+" health_before="+std::to_string(before)+" reaction="+std::to_string(int(hit[8]))+" ragdoll="+std::to_string(int(hit[7]))+" blocked="+std::to_string(int(hit[14]))+" on_mission="+std::to_string(onMission())+" script_mission="+std::to_string(scriptMissionActive()));
        // CJ already down or getting up keeps getting up (Recovery.hpp getupAbort):
        // the blow's damage is in, but no new physical reaction restarts his fall.
        const auto down=agents.find(target);
        const bool playerKeepsGettingUp=p==player()&&at<float>(p,0x540)>0&&down!=agents.end()&&!down->second->mounted&&
            (down->second->recovering||(down->second->current[10].p-down->second->current[0].p).z<.30f);
        if(playerKeepsGettingUp&&hit[7]!=0)event("player_getup_kept",target,at<short>(p,0x22),"combat_reaction_skipped=1 attacker="+std::to_string(source));
        if(!playerKeepsGettingUp&&(hit[7]!=0||(!motorStart&&at<float>(p,0x540)<=0))){
            auto&a=fighters[target];Request r;r.kind=Reaction::Melee;r.time=now;r.attackerRef=source;r.weapon=weapon;r.part=part;r.nativeMeleePart=part;r.playerVehicle=p==player();r.meleeContactReady=true;
            if(!motorStart)r.combatDamage={now,source,weapon,piece};
            r.originalMeleeBlow=hit[15]==0;r.contactKnown=true;r.contact={hit[4],hit[5],hit[6]};r.direction=normalized(r.contact-position(by));r.contactNormal=normalized(V{hit[9],hit[10],hit[11]});
            auto&attacker=fighters[source];const int striker=combatStrikeBone(int(hit[12])),n=attacker.rig->index(striker);
            if(n>=0&&attacker.poseEpoch.frame==*reinterpret_cast<unsigned*>(0xB7CB4C)&&attacker.previousPoseEpoch.frame+1==attacker.poseEpoch.frame&&attacker.poseEpoch.phase!=SeedPhase::Unknown&&attacker.previous.size()==attacker.pose.size()&&attacker.poseHistory.differentiable(*reinterpret_cast<unsigned*>(0xB7CB4C),attacker.motionSeconds)){
                r.strikeVelocity=combatPointVelocity(attacker.previous[n],attacker.pose[n],r.contact,attacker.motionSeconds);
                V receiving=at<V>(p,0x44)*50.f;
                if(auto active=agents.find(target);active!=agents.end()){
                    V spin{};if(nm_agent_get_part_velocity(active->second->agent,part,&receiving.x,&spin.x)>=0)
                        receiving=receiving+cross(spin,r.contact-active->second->current[part].p);
                }
                r.strikeVelocity=r.strikeVelocity-receiving;
                r.strikingMass=at<float>(by,0x8C)*(striker==saBone::head?.08f:(striker>=41)?.10f:.045f);
            }
            r.reactionDraw=reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(0.f,1.f);r.melee=meleeResponse(r.reactionDraw,false);
            r.physicalAnimation=(hit[8]>=0||hit[8]==-2)&&at<float>(p,0x540)>0;
            if(r.physicalAnimation)r.melee=MeleeResponse::Balance;
            event("combat_physics_source",target,at<short>(p,0x22),"striker_iv="+std::to_string(int(hit[12]))+" striker_sa="+std::to_string(striker)+" relative_speed="+std::to_string(length(r.strikeVelocity))+" closing_speed="+std::to_string(dot(r.strikeVelocity,r.contactNormal))+" striking_mass="+std::to_string(r.strikingMass)+" behaviour_draw="+std::to_string(r.reactionDraw));
            // Authored counter damage has no new collision impulse. Preserve
            // its pose/inertia on a fatal handoff, without inventing a strike.
            if(hit[15]!=0){r.strikingMass=0;r.strikeVelocity={};}
            r.pose=std::make_shared<PoseSeed>();r.pose->bones=a.pose;r.pose->deriveParts=true;r.pose->velocity=motionBefore;
            r.pose->poseEpoch=a.poseEpoch;r.pose->sampledRoot=a.finalizedRoot;
            // Preserve the displayed animation's per-part motion, including
            // its rotation, at the ownership boundary. Native root velocity
            // is not the velocity of a reacting head, elbow or lifted foot.
            if(a.poseEpoch.frame==*reinterpret_cast<unsigned*>(0xB7CB4C)&&a.previousPoseEpoch.frame+1==a.poseEpoch.frame&&a.poseEpoch.phase!=SeedPhase::Unknown&&a.previous.size()==a.pose.size()&&!a.pose.empty()&&a.poseHistory.differentiable(*reinterpret_cast<unsigned*>(0xB7CB4C),a.motionSeconds)){
                const auto previousJoints=a.rig->jointFrames(a.previous),currentJoints=a.rig->jointFrames(a.pose);
                r.pose->hasPartVelocities=true;
                for(int n=0;n<21;n++){
                    const T previous=mul(previousJoints[n],a.rig->offsets[n]),current=mul(currentJoints[n],a.rig->offsets[n]);
                    r.pose->linear[n]=(current.p-previous.p)*(1.f/a.motionSeconds);
                    r.pose->spin[n]=combatAngularVelocity(previous.q,current.q,a.motionSeconds);
                }
                r.pose->motionEpoch=a.poseEpoch;
            }
            event("combat_physics_handoff",target,at<short>(p,0x22),"per_part_motion="+std::to_string(r.pose->hasPartVelocities)+" pelvis_vz="+std::to_string(r.pose->linear[0].z)+" native_pre_damage_vz="+std::to_string(motionBefore.z*50.f)+" authored_damage="+std::to_string(hit[15]!=0));
            pending[target]=r;a.owned=false;
        }
    }
}
static bool combatReactionPlaying(const Active&a){
    const auto i=fighters.find(a.ref);
    return a.physicalAnimation&&i!=fighters.end()&&i->second.state[0]==2&&i->second.state[4]>=0;
}
static void combatDrivePhysics(Active&a){
    auto actor=fighters.find(a.ref);
    if(!a.physicalAnimation||a.recovering||a.preview||a.mounted||at<float>(a.ped,0x540)<=0||actor==fighters.end()||actor->second.state[0]!=2){
        nm_agent_cancel_animation_pose(a.agent);return;
    }
    auto&f=actor->second;if(f.pose.size()!=a.rig->ids.size())return;
    auto joints=a.rig->jointFrames(f.pose);std::array<float,147> rows{};
    for(int n=0;n<21;n++){const auto t=mul(joints[n],a.rig->offsets[n]);const float row[]={t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w};std::copy(row,row+7,rows.begin()+n*7);}
    const unsigned flags=unsigned(f.state[8]);
    // Carry the authored reaction's leg articulation too. Partial clips and
    // stance keep native stepping. Full-body means joint motor targets only:
    // no pelvis pin, position write, velocity replacement or extra impulse.
    const bool fullReaction=f.state[4]>=0&&f.state[14]!=0&&flags!=0x208001&&flags!=0x8008;
    ok(nm_agent_submit_animation_pose(a.agent,rows.data(),21,fullReaction?"fb":"ub",1.f));
}
