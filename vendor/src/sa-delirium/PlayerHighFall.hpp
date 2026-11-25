#pragma once
#include "CjHighFall.hpp"

static float playerHighFallMinDrop = cj_high_fall::defaultDrop;
struct PlayerFallObservation {
    cj_high_fall::Tracker track;
    int ref=-1, areaId=-1;
    unsigned lastTime=0, surfaceTime=0;
    V previousRoot{}, surfaceSample{};
};
static PlayerFallObservation playerFall;

static bool playerFallSurface(V from,float depth,ColPoint& ground){
    void* support=nullptr;
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    // Include vehicles: the first surface can be the roof of a parked van.
    return vertical(from,from.z-depth,ground,support,true,true,false,true,true,false,nullptr)&&
           support&&finite(ground.point)&&finite(ground.normal);
}

static void samplePlayerTakeoff(V root,unsigned now){
    auto& s=playerFall;
    const V delta=root-s.surfaceSample;
    const bool recent=s.track.takeoffSurface&&!s.track.firstAirRoot&&now-s.surfaceTime<250;
    if(recent&&dot(delta,delta)<.09f&&std::abs(delta.z)<.05f)return;
    ColPoint ground{};
    if(playerFallSurface(root,1.65f,ground)&&ground.normal.z>.45f&&root.z-ground.point.z>=0.f){
        s.track.grounded(ground.point.z);
        s.surfaceSample=root;s.surfaceTime=now;
    }else if(!recent||dot(delta,delta)>.49f||std::abs(delta.z)>.15f){
        // The centre can already overhang the edge while one foot still stands.
        // Only reuse a recent sample on that same small patch of ground.
        s.track.reset();
    }
}

static bool playerHighFallStillEligible(void* p){
    if(!p||p!=player()||playerFall.ref!=reference(p)||playerFall.areaId!=area()||
       !(at<unsigned char>(p,0x46D)&2)||inVehicle(p)||combatWeapon(p)==46||
       at<float>(p,0x540)<=0||playerLeafTask(p)!=241)return false;
    const V root=position(p),velocity=at<V>(p,0x44);
    ColPoint ground{};
    return finite(root)&&finite(velocity)&&velocity.z<=-.12f&&
           playerFallSurface(root,cj_high_fall::probeDepth,ground)&&
           playerFall.track.shouldStart(root.z,ground.point.z,ground.normal.z,velocity.z,playerHighFallMinDrop);
}

static void tickPlayerHighFall(){
    void*p=player();
    if(!recoveryAllowed()||!validPed(p,false,false,true)||inVehicle(p)||combatWeapon(p)==46||
       agents.contains(reference(p))){playerFall={};return;}
    const int ref=reference(p);
    const auto waiting=pending.find(ref);
    if(waiting!=pending.end()&&waiting->second.kind!=Reaction::HighFall){playerFall={};return;}
    const unsigned now=gameTime();
    const V root=position(p);
    const V velocity=at<V>(p,0x44);
    if(!finite(root)||!finite(velocity)){playerFall={};return;}
    const unsigned elapsed=now-playerFall.lastTime;
    if(ref!=playerFall.ref||area()!=playerFall.areaId||elapsed>1000||
       length(root-playerFall.previousRoot)>2.f+(50.f*length(velocity)+12.f)*float(elapsed)*.001f)
        playerFall={};
    playerFall.ref=ref;playerFall.areaId=area();playerFall.lastTime=now;playerFall.previousRoot=root;
    if(!(at<unsigned char>(p,0x46D)&2)){
        if(at<unsigned char>(p,0x46C)&1)samplePlayerTakeoff(root,now);
        else if(now-playerFall.surfaceTime>250)playerFall.track.reset();
        return;
    }
    if(!playerFall.track.firstAirRoot&&now-playerFall.surfaceTime>250)playerFall.track.reset();
    playerFall.track.airborne(root.z);
    if(waiting!=pending.end()||!playerHighFallStillEligible(p))return;
    const float nativeGravity=*reinterpret_cast<float*>(0x863984)*2500.f;
    if(!std::isfinite(nativeGravity)||nativeGravity<1.f)return;
    Request r;
    r.kind=Reaction::HighFall;r.time=now;r.playerVehicle=true;
    r.velocityOverride=true;r.velocity=velocity;
    // Keep the ballistic fall height as ownership crosses from SA gravity
    // to the IV solver. Horizontal momentum belongs to the actual takeoff.
    r.velocity.z*=std::sqrt(9.81f/nativeGravity);
    pending[ref]=r;
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
    const float rawDamage=verticalSpeed<-.6f?500.f:
        400.f*std::max(0.f,(soft?-.375f:-.25f)-verticalSpeed)+
        100.f*std::max(0.f,std::hypot(impact.x,impact.y)-(soft?.375f:.25f));
    const float damage=rawDamage*.25f;
    if(damage>=1.f)
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)
            (a.ped,nullptr,54,int(std::lround(damage)),3,2);
    event("player_high_fall_landed",a.ref,a.model,"closing_mps="+std::to_string(hit[1])+
        " native_damage_factor="+std::to_string(damage)+" soft_surface="+std::to_string(soft));
}
