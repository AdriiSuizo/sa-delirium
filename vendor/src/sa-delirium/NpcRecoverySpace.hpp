#pragma once
#include "Math.hpp"

namespace npc_recovery {
inline float shotDelay(bool npcShot,unsigned sinceLastShot,float normal){
    return npcShot&&sinceLastShot>=1500u?std::min(normal,1.5f):normal;
}
// Relative trajectory of the NPC in the component's local frame. Motion
// counts only if it enters the clearance volume in the prediction interval.
inline bool vehicleApproaches(V local,V relative,V half){
    float enter=0.f,leave=.12f;
    auto slab=[&](float p,float v,float h){
        if(std::abs(v)<.0001f)return std::abs(p)<=h;
        float a=(-h-p)/v,b=(h-p)/v;
        if(a>b)std::swap(a,b);
        enter=std::max(enter,a);leave=std::min(leave,b);
        return enter<=leave;
    };
    V box{half.x+.35f,half.y+.35f,half.z+.8f};
    if(std::abs(local.x)<=box.x&&std::abs(local.y)<=box.y&&std::abs(local.z)<=box.z){
        // Outside the actual footprint and already moving clear of it.
        V gap{local.x-std::clamp(local.x,-half.x,half.x),
              local.y-std::clamp(local.y,-half.y,half.y),0};
        if(dot(gap,gap)>0.f&&dot(gap,relative)>=0.f)return false;
        return true;
    }
    return slab(local.x,relative.x,box.x)&&slab(local.y,relative.y,box.y)&&slab(local.z,relative.z,box.z);
}

// Prefer the nearest usable position on the same supporting surface.
template<class Check> bool nearby(V origin,V& result,Check check){
    constexpr float diag=.70710678f;
    constexpr V directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},
        {diag,diag,0},{-diag,diag,0},{diag,-diag,0},{-diag,-diag,0}};
    for(float distance:{.16f,.30f})for(V axis:directions){
        V next=origin+axis*distance;
        if(check(next)){result=next;return true;}
    }
    return false;
}
}
