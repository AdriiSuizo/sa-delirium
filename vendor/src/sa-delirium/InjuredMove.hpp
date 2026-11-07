#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "OriginalInjuredState.hpp"
// IV CTaskComplexMoveAboutInjured (1310, ctor 0x824C8738): after the injured get-up a wounded ped
// moves away for rand*20+10 s on move_injured_lower or move_injured_upper (coin, 0x824CC4F0), then
// dies (0x824CAAEC). InjuredOnGround 278 picks who and when (injured.rs, OriginalInjuredState.hpp):
// half go (rand<0.5), under 50 they bleed out instead, and the rest lie healing 3 HP/s to 95 from
// at most 75, after an NMPose of 1 to 4 s. SA's 100 health reads as IV's 200, so IV 50..100 is SA
// 25..50. Shots and melee alike. Clips: the owner's disc (bootstrap import).
struct InjuredMove {
    void* ped=nullptr;int model=-1;NmLocomotion* mover=nullptr;std::unique_ptr<Rig> rig;std::vector<float> pose;
    std::string set;float phase=0,duration=1,speed=0;unsigned until=0,frame=UINT32_MAX,updates=0,drawn=0,drawnFrame=UINT32_MAX;bool moving=false,posed=false;
    ~InjuredMove(){if(mover)nm_locomotion_destroy(mover);}
};
static std::map<int,std::unique_ptr<InjuredMove>> injuredMoves;
static float randomUnit(){return reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(0.f,1.f);}
static bool injuredMoving(int ref){return injuredMoves.contains(ref);}
static bool injuredHoldsGround(Active&a,unsigned now){
    if(!a.injuredDecided){
        a.injuredDecided=true;const float health=at<float>(a.ped,0x540);
        // Tests only: every ped goes, at once.
        static const bool test=GetEnvironmentVariableA("GTASA_INJURED_TEST",nullptr,0)!=0;
        if(a.ped!=player()&&(test||(health>=25&&health<50&&randomUnit()<.5f))){
            const float seconds=(95.f-original_injured_state::initialHealth(health*2.f))/3.f+1.f+randomUnit()*3.f;
            a.injuredChosen=true;a.injuredRiseAt=now+unsigned((test?.5f:seconds)*1000.f);
            event("injured_on_ground",a.ref,a.model,"seconds="+std::to_string(seconds)+" health="+std::to_string(health));
        }
    }
    return a.injuredChosen&&now<a.injuredRiseAt;
}
static void beginInjuredMove(void* p){
    if(!combat||!p||p==player()||injuredMoves.contains(reference(p))||!validPed(p,false,false,true))return;
    const float health=at<float>(p,0x540);if(health<=0)return;
    auto move=std::make_unique<InjuredMove>();
    try{
        move->rig=std::make_unique<Rig>(p,sourceBuild);
        const auto&r=*move->rig;std::vector<uint32_t> ids(r.ids.begin(),r.ids.end()),parents(r.parents.begin(),r.parents.end());
        std::vector<float> bind(ids.size()*7);
        for(size_t i=0;i<ids.size();i++){const auto&t=r.bind[i];float*v=bind.data()+7*i;v[0]=t.p.x;v[1]=t.p.y;v[2]=t.p.z;v[3]=t.q.x;v[4]=t.q.y;v[5]=t.q.z;v[6]=t.q.w;}
        move->mover=nm_locomotion_create(combat,ids.data(),parents.data(),bind.data(),int(ids.size()),at<float>(p,0x558));
        if(!move->mover)return;
        move->pose.resize(bind.size());
    }catch(...){return;}
    move->set=randomUnit()<.5f?"move_injured_lower":"move_injured_upper";
    float info[2]{};
    if(nm_locomotion_clip_pose(combat,move->mover,(move->set+"/walk").c_str(),0,move->pose.data(),int(move->rig->ids.size()),info)!=1){
        event("injured_move_unavailable",reference(p),at<short>(p,0x22),"clips_missing=1");return;
    }
    move->ped=p;move->model=at<short>(p,0x22);move->until=gameTime()+unsigned((GetEnvironmentVariableA("GTASA_INJURED_TEST",nullptr,0)?2.f:randomUnit()*20.f+10.f)*1000.f);
    if(void* task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x40)){
        reinterpret_cast<void*(__thiscall*)(void*,void*,bool,float,int,int,float)>(0x65C430)(task,player(),false,40.f,1000000,1000,1.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),task,1,true);
    }
    event("injured_move_started",reference(p),move->model,"set="+move->set+" seconds="+std::to_string((move->until-gameTime())*.001f)+" health="+std::to_string(health));
    injuredMoves[reference(p)]=std::move(move);
}
static void injuredUpdatePosition(void* p){
    const auto found=injuredMoves.find(reference(p));if(found==injuredMoves.end())return;
    auto&m=*found->second;if(m.ped!=p||agents.contains(found->first))return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(m.frame==frame)return;m.frame=frame;
    const float dt=*reinterpret_cast<float*>(0xB7CB5C)*.02f;
    // SA's flee task keeps the route; only the gait and its speed are the original clip's.
    const float sx=at<float>(p,0x550),sy=at<float>(p,0x554);m.moving=sx*sx+sy*sy>1.e-8f;
    float info[2]{};
    m.posed=nm_locomotion_clip_pose(combat,m.mover,(m.set+(m.moving?"/walk":"/idle")).c_str(),m.phase,m.pose.data(),int(m.rig->ids.size()),info)==1;
    if(!m.posed)return;
    m.updates++;m.duration=std::max(info[0],.05f);m.speed=info[1];m.phase=std::fmod(m.phase+dt/m.duration,1.f);
    if(m.moving){
        const float yaw=at<float>(p,0x558),scale=combatMotionScale(dt,*reinterpret_cast<float*>(0xB7CB5C));
        at<float>(p,0x550)=-std::sin(yaw)*m.speed*scale;at<float>(p,0x554)=std::cos(yaw)*m.speed*scale;
    }
}
// With the ped's own palette evaluation, as Niko's walk: drawn any earlier the game's walk replaced it
// on most frames, and the ped flickered between the two (07/11).
static void injuredDraw(void* p){
    const auto found=injuredMoves.find(reference(p));if(found==injuredMoves.end())return;
    auto&m=*found->second;if(m.ped!=p||!m.posed||agents.contains(found->first))return;
    auto*h=hierarchy(p);if(!h||h->num!=int(m.rig->ids.size()))return;
    const T root=transform(*matrix(p));std::vector<T> bones;
    for(size_t i=0;i<m.rig->ids.size();i++){const float*r=m.pose.data()+7*i;bones.push_back(mul(root,T{{r[3],r[4],r[5],r[6]},{r[0],r[1],r[2]}}));}
    const auto palette=m.rig->renderPalette(h->matrices,bones);std::copy(palette.begin(),palette.end(),h->matrices);
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);if(m.drawnFrame!=frame){m.drawnFrame=frame;m.drawn++;}
}
static void injuredRender(){
    const unsigned now=gameTime();
    // Tests only: the first ped nearby goes at once; a run-over test kills its target outright.
    static const bool test=GetEnvironmentVariableA("GTASA_INJURED_TEST",nullptr,0)!=0;static bool tested=false;
    if(test&&!tested&&player())if(auto*pool=pedPool())for(int i=0;i<pool->size&&!tested;i++)if(!(pool->flags[i]&0x80)){
        void* p=pool->objects+i*0x7C4;if(p==player()||!validPed(p,false,false,true)||at<float>(p,0x540)<=0)continue;
        tested=true;beginInjuredMove(p);
    }
    for(auto it=injuredMoves.begin();it!=injuredMoves.end();){
        auto&m=*it->second;void* p=pedFromRef(it->first);
        if(p!=m.ped||at<short>(p,0x22)!=m.model||at<float>(p,0x540)<=0||inVehicle(p)){it=injuredMoves.erase(it);continue;}
        if(now>=m.until){
            event("injured_move_finished",it->first,m.model,"dies=1 updates="+std::to_string(m.updates)+" drawn="+std::to_string(m.drawn));
            it=injuredMoves.erase(it);
            reinterpret_cast<bool(__cdecl*)(void*,void*,int,int,int,unsigned char)>(0x73A530)(p,nullptr,54,1000,3,0);
            continue;
        }
        ++it;
    }
}
// IV's own force per weapon, from the owner's weaponinfo.xml. SA's pistol stays the reference
// (its old push), so the approved feel of the common case does not move. No file: old formula.
static float ivWeaponForce(int weapon){
    static const std::map<int,float> forces=[]{
        std::map<int,float> out;std::ifstream file(folder/"weaponinfo.xml");if(!file)return out;
        std::stringstream text;text<<file.rdbuf();const std::string xml=text.str();
        auto force=[&](const char* name)->float{
            const auto at=xml.find("<weapon type=\""+std::string(name)+"\">");if(at==std::string::npos)return 0;
            const auto end=xml.find("</weapon>",at),key=xml.find("<physics force=\"",at);
            return key<end?std::strtof(xml.c_str()+key+16,nullptr):0.f;
        };
        const std::pair<int,const char*> table[]={{22,"PISTOL"},{23,"PISTOL"},{24,"DEAGLE"},{25,"SHOTGUN"},{26,"SHOTGUN"},{27,"BARETTA"},{28,"MICRO_UZI"},
            {29,"MP5"},{30,"AK47"},{31,"M4"},{32,"MICRO_UZI"},{33,"M40a1"},{34,"SNIPERRIFLE"},{38,"MINIGUN"}};
        for(const auto&[id,name]:table)if(const float f=force(name);f>0)out[id]=f;
        event("weapon_table",-1,-1,"weapons="+std::to_string(out.size()));
        return out;
    }();
    const auto found=forces.find(weapon);return found==forces.end()?0.f:found->second;
}
static float bulletMomentum(int weapon,float amount){
    constexpr float pistolPush=6.25f,pistolForce=50.f;
    const float force=ivWeaponForce(weapon);
    return force>0?std::min(force*pistolPush/pistolForce,35.f):std::clamp(amount*.25f,3.f,35.f);
}
