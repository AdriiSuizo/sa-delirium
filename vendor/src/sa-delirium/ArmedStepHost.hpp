#pragma once
#include "ArmedStep.hpp"
#include <cfloat>

namespace armed_step {
struct Actor {
    void* ped=nullptr;void* clump=nullptr;const Node* nodes=nullptr;
    player_cover::PoseReader pose;
    npc_gait::Binding binding;
    unsigned seen=0,frame=~0u,draws=0,traced=0;
    SideStep planState;
    Blend blend;
    int model=-1,weapon=-1;bool logged=false;
    float weight=0;
};
static std::map<int,Actor> actors;
static void clear(){actors.clear();}
static void prune(unsigned now){
    for(auto it=actors.begin();it!=actors.end();)
        if(now-it->second.seen>750)it=actors.erase(it);else ++it;
}
static bool room(void* p,V dir,float reach){
    const V pos=position(p);
    float floor=0;
    if(!player_cover::ground(pos,floor))return false;
    for(float dist:{reach*.5f,reach}){
        float next;
        if(!player_cover::ground(pos+dir*dist,next)||std::abs(next-floor)>.36f)return false;
    }
    const V across{dir.y,-dir.x,0};
    for(float side:{-.20f,.20f}){
        V from=pos+across*side;from.z=floor+.67f;
        ColPoint hit{};void* object=nullptr;
        if(player_cover::ray(from,from+dir*reach,hit,object))return false;
    }
    return true;
}
static bool gunActive(void* ped,void*& gun){
    auto* manager=taskManager(ped);
    if(!manager||!at<void*>(manager,0x14))return false;
    void* intel=at<void*>(ped,0x47C);
    gun=intel?reinterpret_cast<void*(__thiscall*)(void*)>(0x600F70)(intel):nullptr;
    return gun&&!at<unsigned char>(gun,8)&&aims(at<signed char>(gun,0xf),at<signed char>(gun,0x10));
}
static V playerWish(void* ped,bool& fast){
    auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
    auto* camera=activeCamera();
    if(!pad||at<short>(pad,0x10E)||!camera)return {};
    const float x=reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad)/128.f;
    const float y=-reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad)/128.f;
    fast=reinterpret_cast<bool(__thiscall*)(void*)>(0x5407A0)(pad);
    return cameraRelativeWish(x,y,at<V>(camera,0x190),matrix(ped)->up);
}
static V npcWish(Actor& a,void* ped,void* gun,V right,unsigned now,bool& fast){
    // Preserve a movement order from the existing gun task. When the task
    // holds position, alternate short checked steps with stationary firing.
    const float x=at<float>(gun,0x14),y=-at<float>(gun,0x18);
    const V nativeShift{50.f*at<float>(ped,0x550),50.f*at<float>(ped,0x554),0};
    const V wish=right*x+V{-right.y,right.x,0}*y;
    // SetMoveAnim consumes m_HasMoveControl before CPed::UpdatePosition.
    // A surviving command agrees with the native shift on that same step.
    if(std::hypot(x,y)>.14f&&(at<unsigned char>(gun,0xa)||dot(nativeShift,wish)>.04f))
        return wish*(1.f/std::max(1.f,std::hypot(x,y)));
    const V target=at<V>(gun,0x20);
    const bool point=finite(target)&&dot(target,target)>1.e-6f;
    if(!at<void*>(gun,0x1c)&&!point&&!at<unsigned char>(gun,0xb)&&!at<unsigned char>(gun,0xd))return {};
    const int side=plan(a.planState,now,[&](int sign,float reach){return room(ped,right*float(sign),reach);});
    fast=a.planState.brisk&&side!=0;
    return right*float(side);
}
static void move(void* ped){
    if(!ready||!combat||!npcGaitLoaded||!ped||!playing()||cutscene()||
       (protectMissions&&authoredMissionProtected()))return;
    const int weapon=combatWeapon(ped);
    const bool isPlayer=ped==player();
    if(!usesArmedStride(weapon,isPlayer)||!validPed(ped,false,false,true,FLT_MAX)||
       !nativeOnFootControl(at<unsigned char>(ped,0x46D))||(at<unsigned char>(ped,0x46F)&4u))return;
    void* gun=nullptr;
    if(!gunActive(ped,gun))return;
    const int ref=reference(ped);
    if(agents.contains(ref)||pending.contains(ref)||injuredMoving(ref)||player_cover::owns(ped)||
       police_cover::officers.contains(ref))return;
    auto* h=hierarchy(ped);
    if(!h||!h->nodes||!h->matrices)return;
    const unsigned now=gameTime(),frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    auto old=actors.find(ref);
    auto* clump=at<void*>(ped,0x18);
    const int model=at<short>(ped,0x22);
    if(old!=actors.end()&&(old->second.ped!=ped||old->second.model!=model||old->second.clump!=clump||
       old->second.weapon!=weapon||old->second.nodes!=h->nodes||
       old->second.binding.count!=h->num||now-old->second.seen>350))actors.erase(old);
    auto [it,fresh]=actors.try_emplace(ref);
    auto& a=it->second;
    if(fresh){
        if(!a.binding.init(h->nodes,h->num,npc_gait::bank.tags)){actors.erase(it);return;}
        a.ped=ped;a.model=model;a.weapon=weapon;a.clump=clump;a.nodes=h->nodes;
        a.planState.variation=unsigned(ref)*2654435761u+now;
    }
    if(a.frame==frame)return;
    a.frame=frame;a.seen=now;
    const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    if(dt<=0){a.weight=0;return;}
    V right=matrix(ped)->right;right.z=0;right=normalized(right);
    bool fast=false;
    const V wish=isPlayer?playerWish(ped,fast):npcWish(a,ped,gun,right,now,fast);
    if(!finite(wish)){a.weight=0;return;}
    const V forward{-right.y,right.x,0};
    const float pressure=length(wish);
    const float cap=!isPlayer&&fast?2.f:1.f;
    const bool moving=pressure>.1f;
    a.blend.select(moving?cap*dot(wish,right):0.f,moving?cap*dot(wish,forward):0.f,dt);
    const bool cycling=std::hypot(a.blend.right,a.blend.forward)>.01f;
    a.weight=std::clamp(a.weight+(cycling?8.f:-9.f)*dt,0.f,1.f);
    if(!a.weight)return;
    float speed=0,duration=0,sum=0;
    for(int i=0;i<a.blend.count;i++){
        const auto& layer=a.blend.layers[i];
        const auto meta=player_cover::clipMeta(a.pose,ped,layer.key);
        if(!(meta.duration>.05f&&meta.moverSpeed>=0&&meta.moverSpeed<10.f)){a.weight=0;return;}
        speed+=meta.moverSpeed*layer.weight;
        duration+=meta.duration*layer.weight;
        sum+=layer.weight;
    }
    a.blend.tick(dt,duration,sum);
    if(moving){
        const V direction=normalized(right*a.blend.right+forward*a.blend.forward);
        at<float>(ped,0x550)=direction.x*speed*.02f;
        at<float>(ped,0x554)=direction.y*speed*.02f;
    }
    if(!a.logged&&moving){event("armed_step_started",ref,model,a.blend.layers[0].key);a.logged=true;}
}
static bool visible(void* ped,int ref,unsigned frame){
    auto it=actors.find(ref);
    if(it==actors.end())return false;
    auto& a=it->second;
    if(a.ped!=ped||frame-a.frame>1||!a.blend.count||a.weight<=0||agents.contains(ref)||pending.contains(ref)||
       injuredMoving(ref)||player_cover::owns(ped)||police_cover::officers.contains(ref)||a.model!=at<short>(ped,0x22)||
       a.weapon!=combatWeapon(ped)||a.clump!=at<void*>(ped,0x18))return false;
    void* gun=nullptr;
    if(!gunActive(ped,gun)||!nativeOnFootControl(at<unsigned char>(ped,0x46D))||
       inVehicle(ped)||(at<unsigned char>(ped,0x46F)&4u))return false;
    auto* h=hierarchy(ped);
    return h&&h->matrices&&h->nodes==a.nodes&&h->num==a.binding.count&&matrix(ped);
}
static bool draw(void* ped,int ref){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(!visible(ped,ref,frame))return false;
    auto& a=actors.at(ref);
    auto* h=hierarchy(ped);
    auto sample=[&](const char* key,float phase)->const float*{
        float info[2]{};
        return nm_locomotion_clip_pose(combat,a.pose.reader,key,phase,a.pose.rows.data(),h->num,info)==1?a.pose.rows.data():nullptr;
    };
    if(!blendedLegs(a.blend,a.binding,*matrix(ped),h->matrices,a.weight,sample))return false;
    ++a.draws;
    if(gameTime()-a.traced>=1200){
        a.traced=gameTime();
        event("armed_step_pose",ref,a.model,"phase="+std::to_string(a.blend.phase)+" draws="+std::to_string(a.draws)+
              " side="+std::to_string(a.blend.right)+" forward="+std::to_string(a.blend.forward));
    }
    return true;
}
}
