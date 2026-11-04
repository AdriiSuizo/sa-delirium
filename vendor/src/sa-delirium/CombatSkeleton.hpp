#pragma once
#include "Math.hpp"
// Inverse of animation::iv_bone. A strike may name a thigh (knee), forearm,
// clavicle or head, not just a hand/foot. Never infer it from proximity.
inline int combatStrikeBone(int iv){
    switch(iv){
    case 0:return 0;case 0x1a1:return 1;case 0x4b2:case 0x4b3:return 2;
    case 0x36a0:case 0x36a1:return 3;case 0x4b4:return 4;case 0x4b5:return 5;
    case 0x4c0:return 31;case 0x4c1:return 32;case 0x4c2:return 33;case 0x4c3:return 34;
    case 0x4c7:return 21;case 0x4c8:return 22;case 0x4c9:return 23;case 0x4d0:return 24;
    case 0x35d6:return 35;case 0x35d7:return 36;case 0x35b6:return 25;case 0x35b7:return 26;
    case 0x1a2:return 41;case 0x1a3:return 42;case 0x1a4:return 43;case 0x1a5:return 44;
    case 0x1a7:return 51;case 0x1a8:return 52;case 0x1a9:return 53;case 0x4b0:return 54;
    default:return -1;
    }
}
inline int combatBodyPart(int iv){
    // A fitted fragment has four spine parts; routing through SA's two torso
    // bones collapses these and delivers a measured hit to the wrong body.
    constexpr int tags[]={0x1a1,0x1a2,0x1a3,0x1a4,0x1a7,0x1a8,0x1a9,0x4b2,0x4b3,0x36a0,0x36a1,0x4b4,0x4b5,0x4c0,0x4c1,0x4c2,0x4c3,0x4c7,0x4c8,0x4c9,0x4d0};
    for(int i=0;i<21;i++)if(tags[i]==iv)return i;return -1;
}
inline V combatPointVelocity(T previous,T current,V contact,float dt){
    if(!std::isfinite(dt)||dt<=0||!finite(contact))return {};
    // The hip can remain still while its thigh swings the knee into a target.
    // Track the material contact point through both transforms, including
    // angular motion, rather than differentiating only the bone origin.
    const V local=rotate(conjugate(current.q),contact-current.p);
    return (contact-(previous.p+rotate(previous.q,local)))*(1.f/dt);
}
inline V combatAngularVelocity(Q previous,Q current,float dt){
    if(!std::isfinite(dt)||dt<=0)return {};
    Q delta=mul(current,conjugate(previous));
    if(delta.w<0)delta={-delta.x,-delta.y,-delta.z,-delta.w};
    const V axis{delta.x,delta.y,delta.z};const float sine=length(axis);
    return sine>1.e-6f?axis*(2.f*std::atan2(sine,delta.w)/(sine*dt)):V{};
}
