#pragma once
#include "OriginalImpactDamage.hpp"
struct BlastContext {V centre{};float radius=0;std::map<int,V> velocities;};
static const BlastContext* currentBlast=nullptr;
static void configureBlastRequest(Request&,void*);
// Grinch Trainer 1.0 uses the native infinite-health byte (BAGUVIX)
// together with physical proof flags. The byte also supports the stock cheat.
static bool playerGodMode(void* ped) {
    return ped && ped == player() && *reinterpret_cast<const bool*>(0x96916D);
}
static void(__cdecl* originalExplosion)(const V&,float,float,void*,void*,bool,float)=nullptr;
static void __cdecl explosion(const V&point,float radiusValue,float visible,void*victim,void*creator,bool timer,float damageValue){
    if(!recoveryAllowed()){originalExplosion(point,radiusValue,visible,victim,creator,timer,damageValue);return;}
    BlastContext blast{point,radiusValue,{}};auto*pool=pedPool();
    if(pool)for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&128)){
        void*p=pool->objects+i*0x7C4;if(matrix(p)&&length(position(p)-point)<radiusValue+2)blast.velocities[reference(p)]=at<V>(p,0x44);
    }
    const auto*previous=currentBlast;currentBlast=&blast;
    {NativeHitScope scope;originalExplosion(point,radiusValue,visible,victim,creator,timer,damageValue);}
    auto* p=player();
    if(detectImpacts&&playerExplosions&&playerGodMode(p)&&validPed(p,false,false,true)&&
       !inVehicle(p)&&length(position(p)-point)<radiusValue){
        const int ref=reference(p);
        const auto queued=pending.find(ref);
        if(queued==pending.end()||queued->second.kind!=Reaction::Explosion){
            Request request;
            request.time=gameTime();request.kind=Reaction::Explosion;request.weapon=51;request.playerVehicle=true;
            configureBlastRequest(request,p);
            pending[ref]=std::move(request);
            event("god_mode_physical_explosion",ref,at<short>(p,0x22),"native_health_preserved=1");
        }
    }
    currentBlast=previous;
}
static void configureBlastRequest(Request&r,void*p){
    r.impulse={};r.part=0;
    if(currentBlast&&currentBlast->radius>0){
        V difference=position(p)-currentBlast->centre;
        const float falloff=std::clamp(1.f-length(difference)/currentBlast->radius,0.f,1.f);
        difference.z=std::max(difference.z,.25f);r.direction=normalized(difference);
        r.deltaVelocity=r.direction*(2.f+8.f*falloff);
        const float outwardBoost=1.08f+.08f*falloff;
        r.deltaVelocity.x*=outwardBoost;
        r.deltaVelocity.y*=outwardBoost;
        // Add a small lift with the same falloff. Keep the upward component
        // bounded even when the explosion is directly below the body.
        r.deltaVelocity.z=std::clamp(r.deltaVelocity.z,1.f,4.f)+.32f+.50f*falloff;
        auto it=currentBlast->velocities.find(reference(p));
        if(it!=currentBlast->velocities.end()){r.velocityOverride=true;r.velocity=it->second;}
    }else {r.deltaVelocity=normalized(r.direction)*4.4f;r.deltaVelocity.z=1.9f;}
}
struct DamageCalculator {void*source;float factor;unsigned char piece,padding[3];int weapon;bool speak;};
static_assert(sizeof(DamageCalculator)==0x14&&offsetof(DamageCalculator,weapon)==0xC);
static void(__thiscall* originalCalculateDamage)(DamageCalculator*,void*,DamageResponse&,bool)=nullptr;
static void vehicleEntryDragDamage(Active&a,float dt){
    if(dt<=0||playerGodMode(a.ped)||!boardingGripActive(a))return;
    void*car=vehicleFromRef(a.vehicle);if(!car)return;
    // 0x8241EB4C / getincontrol::tail: 6.5 IV health per simulated second.
    // The established IV<->SA health mapping is 200<->100. Let SA calculate
    // armour, defence and attribution; this is not invulnerability or a clamp.
    // Follow SA's damage-event eligibility before calculating damage. The
    // native constructor/destructor also own the source entity reference.
    DamageEvent hit{};
    reinterpret_cast<void*(__thiscall*)(void*,void*,unsigned,int,int,unsigned char,bool,bool)>(0x4AD830)
        (&hit,car,gameTime(),50,3,0,false,inVehicle(a.ped));
    const auto destroyHit=[&](){reinterpret_cast<void(__thiscall*)(void*,unsigned)>
        ((*reinterpret_cast<void***>(&hit))[0])(&hit,0);};
    if(!reinterpret_cast<bool(__thiscall*)(void*,void*)>(0x4B35A0)(&hit,a.ped)){
        destroyHit();return;
    }
    const float before=at<float>(a.ped,0x540);
    DamageCalculator calculator{car,dt*6.5f*.5f,3,{},50,false};DamageResponse response{};
    originalCalculateDamage(&calculator,a.ped,response,false);
    if(gameTime()-a.entryDamageReport>=500||response.zero){
        a.entryDamageReport=gameTime();
        event("vehicle_entry_drag_damage",a.ref,a.model,"original_iv_rate=6.5 sa_requested="+std::to_string(dt*3.25f)+" health="+std::to_string(at<float>(a.ped,0x540))+" armour="+std::to_string(at<float>(a.ped,0x548)));
    }
    if(before>0&&response.zero){
        // Submit this already-calculated response exactly once. Calling
        // KillPedWithCar after health is zero loses its source and SA later
        // creates an anonymous death event; it can also add the old road kick.
        hit.response=response;a.entryFatalAt=hit.time;
        auto*intelligence=at<unsigned char*>(a.ped,0x47C);
        void*accepted=intelligence?reinterpret_cast<void*(__thiscall*)(void*,void*,bool)>(0x4AB420)
            (intelligence+0x68,&hit,true):nullptr;
        if(!accepted)a.entryFatalAt=0;
        event("vehicle_entry_fatal_event",a.ref,a.model,"accepted="+std::to_string(accepted!=nullptr)+
            " event_ms="+std::to_string(hit.time)+" vehicle="+std::to_string(a.vehicle)+" calculated_once=1");
    }
    destroyHit();
}
// IV's per-impact damage for CJ's physical body (82465360, OriginalImpactDamage.hpp): every solved world or obstacle
// contact of the step, ground included, so landings after a fall, a jump from a moving car, a run-over or being thrown
// by a blast hurt him. SA calculates armour and attribution natively; only the requested amount comes from IV (08/11).
static void playerImpactDamage(Active&a,float dt){
    if(dt<=0||a.ped!=player()||a.preview||inVehicle(a.ped))return;
    float out[4]{};
    const int impact=nm_agent_take_impact(a.agent,out);
    if(playerGodMode(a.ped)){a.impactDamageCarry=0.f;return;}
    if(impact!=1)return;
    const float before=at<float>(a.ped,0x540);if(!(before>0))return;
    void*car=nullptr;original_impact_damage::Kind kind=original_impact_damage::Kind::Building;
    if(out[2]>=0){
        kind=original_impact_damage::Kind::Other;
        for(const auto&v:collision->vehicles)if(v.index==int(out[2])&&vehicleFromRef(v.ref)==v.entity){car=v.entity;kind=original_impact_damage::Kind::Vehicle;break;}
    }
    original_impact_damage::Facts f{};
    f.impulse=out[0];f.dt=dt;f.invMass=out[3];f.health=ivHealth(a.ped);f.component=int(out[1]);f.other=kind;f.playerCut=true;
    const auto r=original_impact_damage::calculate(f);
    if(!r.valid||!(r.amountIV>0))return;
    // CJ's visible range is 200..100 in IV. SA's calculator still applies its .33 player factor.
    a.impactDamageCarry+=combat_health::nativeDamage(r.amountIV,true)/.33f;
    if(a.impactDamageCarry<3.f)return;
    const float amount=a.impactDamageCarry;a.impactDamageCarry=0;
    DamageEvent hit{};
    reinterpret_cast<void*(__thiscall*)(void*,void*,unsigned,int,int,unsigned char,bool,bool)>(0x4AD830)
        (&hit,car,gameTime(),r.weapon,3,0,false,false);
    const auto destroyHit=[&](){reinterpret_cast<void(__thiscall*)(void*,unsigned)>
        ((*reinterpret_cast<void***>(&hit))[0])(&hit,0);};
    if(!reinterpret_cast<bool(__thiscall*)(void*,void*)>(0x4B35A0)(&hit,a.ped)){destroyHit();return;}
    DamageCalculator calculator{car,amount,3,{},r.weapon,false};DamageResponse response{};
    originalCalculateDamage(&calculator,a.ped,response,false);
    if(gameTime()-a.impactDamageReport>=250||response.zero){
        a.impactDamageReport=gameTime();
        event("player_iv_impact_damage",a.ref,a.model,"impulse="+std::to_string(out[0])+" part="+std::to_string(int(out[1]))+
            " other="+std::to_string(int(out[2]))+" vehicle="+std::to_string(car!=nullptr)+" amount_iv="+std::to_string(r.amountIV)+
            " sa_requested="+std::to_string(amount)+" health_before="+std::to_string(before)+" health="+std::to_string(at<float>(a.ped,0x540))+
            " armour="+std::to_string(at<float>(a.ped,0x548)));
    }
    if(response.zero){
        hit.response=response;
        auto*intelligence=at<unsigned char*>(a.ped,0x47C);
        void*accepted=intelligence?reinterpret_cast<void*(__thiscall*)(void*,void*,bool)>(0x4AB420)(intelligence+0x68,&hit,true):nullptr;
        event("player_iv_impact_fatal",a.ref,a.model,"accepted="+std::to_string(accepted!=nullptr)+" weapon="+std::to_string(r.weapon));
    }
    destroyHit();
}
static void __fastcall calculateDamage(DamageCalculator*calculator,void*,void*ped,DamageResponse&response,bool speak){
    const bool calculated=response.calculated;
    if(!calculated&&playerGodMode(ped)){
        response={};
        response.calculated=true;
        response.check=true;
        return;
    }
    const bool isPlayer=ped==player();
    const bool authored=!calculated&&combatDelivering&&authoredMeleeDamage.victim==ped&&
       authoredMeleeDamage.source==calculator->source&&authoredMeleeDamage.weapon==calculator->weapon&&
       authoredMeleeDamage.piece==calculator->piece;
    float ivAmount=authored?authoredMeleeDamage.amount:-1.f;
    bool penetratesArmour=authored;
    float nativeGroupFactor=1.f;
    if(!calculated&&!authored&&recoveryAllowed()&&detectImpacts&&firearmDamage(calculator->weapon)){
        void*source=calculator->source;
        const float bullet=firearm::damageAmount(ped,source,calculator->weapon,calculator->piece,calculator->factor);
        if(bullet>=0){
            const bool pedSource=source!=ped&&reference(source)>=0;
            const bool same=pedSource&&reinterpret_cast<bool(__cdecl*)(const void*,const void*)>(0x5F7F40)(ped,source);
            const bool friendHit=isPlayer&&pedSource&&reinterpret_cast<bool(__cdecl*)(const void*,const void*)>(0x601D10)(ped,source);
            const bool friendly=(source==player()&&same)||friendHit||(same&&at<unsigned char>(ped,0x484)==2);
            nativeGroupFactor=friendly?.1f:1.f;
            ivAmount=combat_health::bulletDamage(bullet,ivDefenceWeakness(ped),isPlayer,
                !isPlayer&&inVehicle(ped)&&at<unsigned char>(ped,0x484)==1,friendly);
            penetratesArmour=firearm::profile(calculator->weapon==52?28:calculator->weapon).penetratesArmour;
        }
    }
    bool adapted=false;
    if(ivAmount>=0&&std::isfinite(ivAmount)){
        const auto* stats=at<void*>(ped,0x59C);
        const float nativeFactor=isPlayer?.33f:stats?at<float>(const_cast<void*>(stats),0x2C):1.f;
        if(std::isfinite(nativeFactor)&&nativeFactor>=.05f&&nativeFactor<=10.f){
            float damage=combat_health::nativeDamage(ivAmount,isPlayer);
            if(!isPlayer&&!authored&&inVehicle(ped)&&nativeGroupFactor==1.f){
                const float shield=penetratesArmour?0.f:at<float>(ped,0x548);
                const float health=at<float>(ped,0x540);
                if(health-std::max(0.f,damage-shield*.5f)<combat_health::injured*.5f)
                    damage=std::max(damage,health+shield*.5f);
            }
            calculator->factor=damage/(nativeFactor*nativeGroupFactor);
            adapted=true;
        }
    }
    if(!calculated&&recoveryAllowed()&&calculator->weapon==51&&currentBlast&&currentBlast->radius>0){
        const float d=length(position(ped)-currentBlast->centre);
        if(d>currentBlast->radius*.2f){
            float requested=blastDamage(d,currentBlast->radius);
            // Native stats multiply player damage by .33 before armour. Aim
            // for the same substantial middle-range injury as an ordinary NPC.
            if(ped==player())requested/=.33f;
            calculator->factor=std::min(calculator->factor,requested);
        }
    }
    auto& armour=at<float>(ped,0x548);
    const float oldArmour=armour;
    const float armourScale=isPlayer?1.f:.5f;
    if(adapted)armour=penetratesArmour?0.f:armour*armourScale;
    originalCalculateDamage(calculator,ped,response,speak);
    if(adapted){
        armour=penetratesArmour?oldArmour:armour/armourScale;
        response.armour/=armourScale;
        if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))
            event("iv_combat_health",reference(ped),at<short>(ped,0x22),"weapon="+std::to_string(calculator->weapon)+
                " input_iv="+std::to_string(ivAmount)+" health_sa="+std::to_string(at<float>(ped,0x540))+
                " armour="+std::to_string(armour)+" native_fatal="+std::to_string(response.zero));
    }
    if(calculated||!response.calculated||response.health+response.armour<=0)return;
    DamageEvent hit{};hit.source=calculator->source;hit.time=gameTime();hit.weapon=calculator->weapon;hit.piece=calculator->piece;hit.response=response;
    acceptedDamage(&hit,ped);
}
