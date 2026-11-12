#pragma once

static void tickPlayerHighFall(){
    void*p=player();
    if(!recoveryAllowed()||!validPed(p,false,false,true)||inVehicle(p)||
       !(at<unsigned char>(p,0x46D)&2)||agents.contains(reference(p))||pending.contains(reference(p)))return;
    const V velocity=at<V>(p,0x44);
    if(velocity.z>-.12f||playerLeafTask(p)!=241||combatWeapon(p)==46)return;
    const V from=position(p);
    ColPoint ground{};void*support=nullptr;
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    if(!vertical(from,from.z-9.5f,ground,support,true,false,false,true,true,false,nullptr)||
       !support||from.z-ground.point.z<2.7f||ground.normal.z<.65f)return;
    const float nativeGravity=*reinterpret_cast<float*>(0x863984)*2500.f;
    if(!std::isfinite(nativeGravity)||nativeGravity<1.f)return;
    Request r;
    r.kind=Reaction::HighFall;r.time=gameTime();r.playerVehicle=true;
    r.velocityOverride=true;r.velocity=velocity;
    // Keep the ballistic fall height as ownership crosses from SA gravity
    // to the IV solver. Horizontal momentum belongs to the actual takeoff.
    r.velocity.z*=std::sqrt(9.81f/nativeGravity);
    pending[reference(p)]=r;
}

static void finishPlayerHighFall(Active&a,V com,const float hit[2]){
    if(a.kind!=Reaction::HighFall||a.highFallLanded||hit[0]<.8f||hit[1]<4.f||
       a.stepLinear[0].z>=-4.f||at<float>(a.ped,0x540)<=0.f)return;
    ColPoint ground{};void*support=nullptr;
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    if(!vertical(com,com.z-2.f,ground,support,true,false,false,true,true,false,nullptr)||
       !support||ground.normal.z<.65f)return;
    a.highFallLanded=true;
    const V impact=a.stepLinear[0]*.02f;
    const float nativeGravity=*reinterpret_cast<float*>(0x863984)*2500.f;
    const float verticalSpeed=impact.z*std::sqrt(std::max(1.f,nativeGravity)/9.81f);
    const bool soft=reinterpret_cast<bool(__thiscall*)(void*,unsigned)>(0x55E690)
        (reinterpret_cast<void*>(0xB79538),ground.surfaces[7]);
    // The stationary-ground fall factor from CPed::ProcessEntityCollision
    // (0x5E2D06..0x5E2F74). Let SA apply armour bypass, damage and death.
    const float damage=verticalSpeed<-.6f?500.f:
        400.f*std::max(0.f,(soft?-.375f:-.25f)-verticalSpeed)+
        100.f*std::max(0.f,std::hypot(impact.x,impact.y)-(soft?.375f:.25f));
    if(damage>=1.f)
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)
            (a.ped,nullptr,54,int(std::lround(damage)),3,2);
    event("player_high_fall_landed",a.ref,a.model,"closing_mps="+std::to_string(hit[1])+
        " native_damage_factor="+std::to_string(damage)+" soft_surface="+std::to_string(soft));
}
