#pragma once
#include "Math.hpp"
struct ObstacleHistory {
    T frame{};
    int shape=-1;
};
// Remaining native time starts at the last solver pose, including a fractional
// step left from previous frames. Each step consumes its share of that span.
inline T advanceObstacle(T previous,T current,float remaining,float step){
    if(!(remaining>0)||!(step>0))return previous;
    const float t=std::min(1.f,step/remaining);Q a=previous.q,b=current.q;
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0){b={-b.x,-b.y,-b.z,-b.w};d=-d;}
    Q q;
    if(d>.9995f)q=mix(a,b,t);
    else {const float angle=std::acos(std::clamp(d,-1.f,1.f)),denom=std::sin(angle);
        const float x=std::sin((1-t)*angle)/denom,y=std::sin(t*angle)/denom;
        q=unit({a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y,a.w*x+b.w*y});}
    return {q,previous.p+(current.p-previous.p)*t};
}
