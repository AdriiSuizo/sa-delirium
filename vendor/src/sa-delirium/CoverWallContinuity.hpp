#pragma once
#include "Math.hpp"

// Continuation of already-owned low cover only. Initial wall acquisition keeps
// its original strict normal test. Rounded COL spheres may project millimetres
// in front of a flat box; their vertical normal alone must not invent a gap.
namespace cover_wall_continuity {
constexpr float maxPlaneOffset=.04f;
constexpr float probeOffsets[]={-.10f,.10f};
inline bool eligibleType(unsigned type){return type==1||type==4||type==5;}
inline bool stationary(unsigned type,bool isStatic,V move={},V spin={}){
    if(!eligibleType(type))return false;
    if(type!=4)return true; // Buildings/dummies have no CPhysical velocity.
    return isStatic&&finite(move)&&finite(spin)&&dot(move,move)<=1.e-8f&&dot(spin,spin)<=1.e-8f;
}
inline bool validPlane(V point,V normal){
    return finite(point)&&finite(normal)&&std::abs(normal.z)<.001f&&
        dot(normal,normal)>.99f&&dot(normal,normal)<1.01f;
}
inline bool nearPlane(V hit,V point,V normal){
    return finite(hit)&&validPlane(point,normal)&&std::abs(dot(hit-point,normal))<=maxPlaneOffset;
}
inline float alignment(V normal,V expected){
    normal.z=0;return dot(normalized(normal),expected);
}
inline bool retryRounded(bool low,int vehicle,V point,V normal,V planePoint,V planeNormal){
    return low&&vehicle<0&&finite(normal)&&dot(normal,normal)>.99f&&dot(normal,normal)<1.01f&&
        std::abs(normal.z)>.4f&&std::abs(normal.z)<=.75f&&
        nearPlane(point,planePoint,planeNormal)&&alignment(normal,planeNormal)>=.95f;
}
inline bool recovered(V point,V normal,V planePoint,V planeNormal){
    return finite(normal)&&dot(normal,normal)>.99f&&dot(normal,normal)<1.01f&&
        std::abs(normal.z)<=.4f&&nearPlane(point,planePoint,planeNormal)&&
        alignment(normal,planeNormal)>.82f;
}
}
