#pragma once
#include "Math.hpp"
#include <array>

// Pure translation of the component-matrix tails in the owned 1.0 executable:
// CBike 0x6BD8EF..0x6BDDEC; CBmx 0x6C0DA1..0x6C143F. The input is an explicit
// render-state snapshot AFTER native suspension/wheel-phase evaluation, not a
// stale world LTM. No game memory, wheel phase, suspension state or FX is changed.
struct BikeComponentFrameState {
    bool bicycle=false,bicycleSuspension=false;
    int status=0;
    std::array<bool,10> present{};
    std::array<M,10> local{};
    float steer=0,steerAxisDegrees=0,lean=0,sprintLean=0;
    float frontAngle=0,rearAngle=0,frontHeight=0,rearHeight=0,frontOriginal=0,rearOriginal=0;
    float swingArm=0,frontForkY=0,frontForkZ=0,steerTangent=0,frontLineMidY=0,collisionMinZ=0;
    float wheelDistance=0,wheelBalance=0,chainAngle=0,rightPedalAngle=0,leftPedalAngle=0;
    bool frontBurst=false,rearBurst=false;
};
inline Q bikeAxisRotation(V axis,float angle){const float s=std::sin(angle*.5f);return {axis.x*s,axis.y*s,axis.z*s,std::cos(angle*.5f)};}
inline Q bikeEuler(float x,float y,float z){return mul(bikeAxisRotation({0,0,1},z),mul(bikeAxisRotation({0,1,0},y),bikeAxisRotation({1,0,0},x)));}
inline bool bikeComponentLocalFrames(const BikeComponentFrameState&s,std::array<M,10>&out){
    out=s.local;
    auto rotateAt=[&](int node,Q q){const V p=out[node].pos;write(out[node],{q,p});};
    for(float x:{s.steer,s.steerAxisDegrees,s.lean,s.sprintLean,s.frontAngle,s.rearAngle,s.frontHeight,s.rearHeight,
        s.frontOriginal,s.rearOriginal,s.swingArm,s.frontForkY,s.frontForkZ,s.steerTangent,s.frontLineMidY,s.collisionMinZ,
        s.wheelDistance,s.wheelBalance,s.chainAngle,s.rightPedalAngle,s.leftPedalAngle})if(!std::isfinite(x))return false;
    const float axle=s.steerAxisDegrees*0.01745329238474369f;
    const Q fork=bikeAxisRotation({0,std::sin(axle),-std::cos(axle)},-s.steer);
    if(s.present[2]){
        rotateAt(2,fork);
        const int handle=s.bicycle?6:7;
        if(s.present[handle])rotateAt(handle,(s.status==4||s.status==5)?fork:Q{});
    }
    if(s.present[3]){
        if(s.swingArm==0)return false;
        const float ratio=(s.rearHeight-s.rearOriginal)/s.swingArm;
        if(std::abs(ratio)>1)return false;
        rotateAt(3,bikeEuler(-std::asin(ratio),0,0));
    }
    if(!s.present[4]||!s.present[5])return false;
    rotateAt(4,bikeEuler(s.frontAngle,0,s.frontBurst?std::sin(s.frontAngle)*(s.bicycle?.02f:.05f):0));
    if(!s.bicycle||s.bicycleSuspension){
        out[4].pos.y=s.frontLineMidY-s.frontForkY-(s.frontHeight-s.frontOriginal)*s.steerTangent;
        out[4].pos.z=s.frontHeight-s.frontForkZ;
    }
    if(!s.bicycle&&s.present[6])out[6].pos=out[4].pos;
    rotateAt(5,bikeEuler(s.rearAngle,0,s.rearBurst?std::sin(s.rearAngle)*(s.bicycle?.04f:.07f):0));
    if(s.bicycle&&s.bicycleSuspension&&!s.present[3])out[5].pos.z=s.rearHeight;
    if(s.present[1]){
        float height=0,pitch=0;
        if(s.bicycle&&!s.bicycleSuspension){
            const float front=s.frontHeight-s.frontOriginal,rear=s.rearHeight-s.rearOriginal;
            height=(1-s.wheelBalance)*front+s.wheelBalance*rear;
            pitch=std::atan2(front-rear,s.wheelDistance);
        }
        rotateAt(1,mul(bikeAxisRotation({0,1,0},s.lean+(s.bicycle?s.sprintLean:0)),bikeAxisRotation({1,0,0},std::abs(s.lean)*-.05f+pitch)));
        out[1].pos.z=(1-std::cos(s.lean))*s.collisionMinZ*.9f+height;
    }
    if(s.bicycle){
        if(s.present[7])rotateAt(7,bikeEuler(s.chainAngle,0,0));
        if(s.present[8])rotateAt(8,bikeEuler(s.rightPedalAngle,0,0));
        if(s.present[9])rotateAt(9,bikeEuler(s.leftPedalAngle,0,0));
    }
    for(int i=1;i<10;i++)if(s.present[i]&&(!finite(out[i].pos)||!finite(out[i].right)||!finite(out[i].up)||!finite(out[i].at)))return false;
    return true;
}
