#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace npc_stride {
enum Track { Walk,Run,Sprint,Start,RunStart,StopL,StopR,RunStopL,RunStopR,
             WalkL,WalkR,RunL,RunR,SprintL,SprintR,Count };
inline constexpr const char* names[]={"walk","run","sprint","wstart","runstart_fwd","wstop_l","wstop_r",
    "rstop_l","rstop_r","walk_turn_l","walk_turn_r","run_turn_l","run_turn_r","sprint_turn_l","sprint_turn_r"};
struct Curve {
    float duration=0;
    std::vector<float> distance;
    float at(float t)const {
        const float frame=std::clamp(t/duration,0.f,1.f)*(distance.size()-1);
        const size_t left=std::min(static_cast<size_t>(frame),distance.size()-2);
        return distance[left]+(frame-left)*(distance[left+1]-distance[left]);
    }
};
using Profile=std::array<Curve,Count>;
using Bank=std::map<std::string,Profile,std::less<>>;
inline bool read(std::istream& in,Bank& bank){
    Bank next;
    std::string magic,profile,name;
    if(!(in>>magic)||magic!="NPM1")return false;
    while(in>>profile){
        int frames; float duration;
        if(!(in>>name>>duration>>frames)||!(duration>.02f&&duration<30.f)||frames<2||frames>1024||
           profile.size()>48||next.size()>64)return false;
        auto slot=std::find(std::begin(names),std::end(names),name);
        if(slot==std::end(names))return false;
        auto& c=next[profile][slot-std::begin(names)];
        if(c.duration)return false;
        c.duration=duration;
        c.distance.resize(frames);
        float previous=0;
        for(float& v:c.distance){
            if(!(in>>v)||!(v>=previous&&v<100.f))return false;
            previous=v;
        }
        if(c.distance.front()!=0||c.distance.back()<=0)return false;
    }
    if(!in.eof()||next.empty())return false;
    for(const auto& row:next)if(!row.second[Walk].duration||!row.second[Run].duration)return false;
    bank.swap(next);return true;
}
struct State {
    int clip=-1;
    float time=0,heading=0,speed=0;
    float lastIdle=1;
    bool braking=false;
};
inline bool stopIntent(State& s,float idle,float start,float stop){
    if(start>.01f||idle<.04f||idle+.003f<s.lastIdle)s.braking=false;
    if(stop>.15f||(idle>.05f&&idle>s.lastIdle+.0001f&&start<.01f))s.braking=true;
    s.lastIdle=idle;
    return s.braking;
}
struct Input {
    int gait;
    bool stopping,rightFoot;
    float dt,yaw,target,nativeSpeed,nativeGroundSpeed,remaining,legScale,curveRate;
};
struct Step {float yaw=0,speed=0;bool applied=false;};
inline Step advance(State& s,const Profile& p,const Input& in){
    if(in.gait<Walk||in.gait>Sprint||!(in.dt>0&&in.dt<=.1f)||
       !(in.nativeSpeed>.005f&&in.nativeSpeed<18.f)||!(in.remaining>=0)||
       !(in.legScale>.3f&&in.legScale<2.f)||!std::isfinite(in.yaw)||!std::isfinite(in.target)){
        s={};return {};
    }
    const int base=p[in.gait].duration?in.gait:Run;
    const bool fresh=s.clip<0;
    if(fresh)s.heading=in.yaw;
    const float delta=std::remainder(in.target-s.heading,6.2831853f);
    const int turn=WalkL+2*base+(delta<0);
    // Wide bends use the authored angular rate. Tight corners also allow the
    // navigation turn to pivot through a right angle in half a gait cycle.
    const float pivotRate=3.14159265f/p[base].duration;
    const float turnRate=std::lerp(in.curveRate>0?in.curveRate:pivotRate,pivotRate,
                                  std::clamp(std::abs(delta)/1.5707963f,0.f,1.f));
    s.heading=std::remainder(s.heading+std::clamp(delta,-turnRate*in.dt,turnRate*in.dt),6.2831853f);
    int next=std::abs(delta)>.18f&&p[turn].duration?turn:base;
    const int start=base==Walk?Start:RunStart;
    const int stop=(base==Walk?StopL:RunStopL)+in.rightFoot;
    const bool wasStop=s.clip>=StopL&&s.clip<=RunStopR;
    if(in.stopping&&p[stop].duration)next=wasStop?s.clip:stop;
    else if((fresh&&in.nativeGroundSpeed<.25f)||(s.clip==start&&s.time<p[start].duration))
        if(p[start].duration)next=start;
    if(next!=s.clip){
        const bool cycling=!(next>=Start&&next<=RunStopR)&&s.clip>=0&&!(s.clip>=Start&&s.clip<=RunStopR);
        s.time=cycling?s.time/p[s.clip].duration*p[next].duration:0.f;
        s.clip=next;
    }
    const auto& c=p[next];
    const bool stopFinished=wasStop&&in.stopping&&s.time>=c.duration;
    const float end=s.time+in.dt;
    float travel=c.at(end)-c.at(s.time);
    if(next<=Sprint||next>=WalkL){
        if(end>=c.duration)travel+=c.at(std::fmod(end,c.duration));
        s.time=std::fmod(end,c.duration);
    }else s.time=std::min(end,c.duration);
    const float facing=std::max(0.f,std::cos(in.target-s.heading));
    float speed=travel*in.legScale/in.dt*facing;
    // SA can ease into a waypoint. Never advance beyond it or prolong a stop
    // after the navigation task has taken away the movement request.
    if(in.stopping)speed=(stopFinished||!p[stop].duration)?in.nativeSpeed*facing:std::min(speed,in.nativeSpeed);
    speed=std::min({speed,in.remaining/in.dt,1.65f*in.nativeSpeed});
    s.speed=speed;
    return {s.heading,speed,true};
}
}
