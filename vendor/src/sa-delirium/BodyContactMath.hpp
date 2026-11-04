#pragma once
#include "ContactMath.hpp"
struct BodyContact {int part=-1;V point{},normal{};float depth=0;};
inline bool capsuleContact(V a,V b,float radius,V c,V d,float otherRadius,BodyContact&out){
    V p,q;nearestSegments(a,b,c,d,p,q);const float distance=length(p-q);
    if(distance>=radius+otherRadius||distance<1.e-5f)return false;
    out.point=q+normalized(p-q)*otherRadius;out.normal=normalized(p-q);
    out.depth=radius+otherRadius-distance;return true;
}
// Perfectly inelastic normal contact. No bounce or scripted launch component.
inline float finiteContactImpulse(float closing,float inverseA,float inverseB){
    if(!std::isfinite(closing)||!std::isfinite(inverseA)||!std::isfinite(inverseB)||
       inverseA<=0||inverseB<=0)return 0;
    return std::max(0.f,closing)/(inverseA+inverseB);
}
inline float suspensionFraction(float lineFraction,float springLength,float lineLength){
    if(springLength<=0||lineLength<=0)return 1.f;
    const float ratio=springLength/lineLength;
    return (lineFraction-1.f+ratio)/ratio;
}
// Backward-Euler unilateral spring. The native force defines k, but both
// bodies' point mobility participates. Discrete spring+kinetic energy cannot
// grow in an isolated pair; no velocity cap or artificial upward kick.
inline float coupledSpringImpulse(float nativeJ,float compression,float separating,float dt,float mobility){
    if(!std::isfinite(nativeJ+compression+separating+dt+mobility)||nativeJ<=0||compression<=0||dt<=0||mobility<=0)return 0;
    const float kh=nativeJ/compression;
    return std::max(0.f,(nativeJ-kh*dt*separating)/(1.f+kh*dt*mobility));
}
inline float dissipativeImpulse(float requested,float closing,float mobility){
    if(!std::isfinite(requested+closing+mobility)||requested<=0||mobility<=0)return 0;
    return std::min(requested,std::max(0.f,closing)/mobility);
}
