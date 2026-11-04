#pragma once
struct BlastContext {V centre{};float radius=0;std::map<int,V> velocities;};
static const BlastContext* currentBlast=nullptr;
static void(__cdecl* originalExplosion)(const V&,float,float,void*,void*,bool,float)=nullptr;
static void __cdecl explosion(const V&point,float radiusValue,float visible,void*victim,void*creator,bool timer,float damageValue){
    if(!recoveryAllowed()){originalExplosion(point,radiusValue,visible,victim,creator,timer,damageValue);return;}
    BlastContext blast{point,radiusValue,{}};auto*pool=pedPool();
    if(pool)for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&128)){
        void*p=pool->objects+i*0x7C4;if(matrix(p)&&length(position(p)-point)<radiusValue+2)blast.velocities[reference(p)]=at<V>(p,0x44);
    }
    const auto*previous=currentBlast;currentBlast=&blast;
    {NativeHitScope scope;originalExplosion(point,radiusValue,visible,victim,creator,timer,damageValue);}
    currentBlast=previous;
}
static void configureBlastRequest(Request&r,void*p){
    r.impulse={};r.part=0;
    if(currentBlast&&currentBlast->radius>0){
        V difference=position(p)-currentBlast->centre;
        const float falloff=std::clamp(1.f-length(difference)/currentBlast->radius,0.f,1.f);
        difference.z=std::max(difference.z,.25f);r.direction=normalized(difference);
        r.deltaVelocity=r.direction*(2.f+8.f*falloff);r.deltaVelocity.z=std::clamp(r.deltaVelocity.z,1.f,4.f);
        auto it=currentBlast->velocities.find(reference(p));
        if(it!=currentBlast->velocities.end()){r.velocityOverride=true;r.velocity=it->second;}
    }else {r.deltaVelocity=normalized(r.direction)*4.f;r.deltaVelocity.z=1.5f;}
}
struct DamageCalculator {void*source;float factor;unsigned char piece,padding[3];int weapon;bool speak;};
static_assert(sizeof(DamageCalculator)==0x14&&offsetof(DamageCalculator,weapon)==0xC);
static void(__thiscall* originalCalculateDamage)(DamageCalculator*,void*,DamageResponse&,bool)=nullptr;
static void vehicleEntryDragDamage(Active&a,float dt){
    if(dt<=0||!boardingGripActive(a))return;
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
static void __fastcall calculateDamage(DamageCalculator*calculator,void*,void*ped,DamageResponse&response,bool speak){
    const bool calculated=response.calculated;
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
    originalCalculateDamage(calculator,ped,response,speak);
    if(calculated||!response.calculated||response.health+response.armour<=0)return;
    DamageEvent hit{};hit.source=calculator->source;hit.time=gameTime();hit.weapon=calculator->weapon;hit.piece=calculator->piece;hit.response=response;
    acceptedDamage(&hit,ped);
}
