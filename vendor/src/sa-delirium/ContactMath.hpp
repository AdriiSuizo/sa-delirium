#pragma once
#include "Math.hpp"
inline V segmentNear(V p,V a,V b){V d=b-a;return a+d*std::clamp(dot(p-a,d)/std::max(dot(d,d),1.e-8f),0.f,1.f);}
inline void nearestSegments(V p,V q,V a,V b,V&onHand,V&onBody){
    const V d=q-p,e=b-a,r=p-a;const float dd=dot(d,d),ee=dot(e,e),de=dot(d,e),dr=dot(d,r),er=dot(e,r);
    float s=0,t=0;
    if(dd<1.e-8f)t=ee<1.e-8f?0.f:std::clamp(er/ee,0.f,1.f);
    else if(ee<1.e-8f)s=std::clamp(-dr/dd,0.f,1.f);
    else{
        const float denominator=dd*ee-de*de;
        if(denominator>1.e-8f)s=std::clamp((de*er-dr*ee)/denominator,0.f,1.f);
        t=(de*s+er)/ee;
        if(t<0){t=0;s=std::clamp(-dr/dd,0.f,1.f);}
        else if(t>1){t=1;s=std::clamp((de-dr)/dd,0.f,1.f);}
    }
    onHand=p+d*s;onBody=a+e*t;
}
inline Q turnBetween(V a,V b){
    a=normalized(a);b=normalized(b);float c=std::clamp(dot(a,b),-1.f,1.f);
    if(c<-.9999f){V axis=normalized(cross(a,std::abs(a.x)<.8f?V{1,0,0}:V{0,1,0}));return {axis.x,axis.y,axis.z,0};}
    V axis=cross(a,b);return unit({axis.x,axis.y,axis.z,1.f+c});
}
inline bool solveContactArm(V shoulder,V elbow,V wrist,V goal,V&newElbow,V&newWrist){
    const float upper=length(elbow-shoulder),lower=length(wrist-elbow),wanted=length(goal-shoulder);
    if(!finite(goal)||upper<.05f||lower<.05f||upper>.8f||lower>.8f||wanted<1.e-5f)return false;
    const V direction=normalized(goal-shoulder);const float d=std::clamp(wanted,std::abs(upper-lower)+.001f,upper+lower-.001f);
    V pole=elbow-shoulder-direction*dot(elbow-shoulder,direction);
    if(length(pole)<1.e-4f)pole=cross(direction,std::abs(direction.z)<.8f?V{0,0,1}:V{1,0,0});
    const float along=(upper*upper+d*d-lower*lower)/(2*d);
    newElbow=shoulder+direction*along+normalized(pole)*std::sqrt(std::max(0.f,upper*upper-along*along));
    newWrist=shoulder+direction*d;return finite(newElbow)&&finite(newWrist);
}
