#pragma once
#include "ContactMath.hpp"
struct ContactSegment {V a{},b{};float radius=0;};
struct SweptMeleeContact {float fraction=0,gap=0;V point{},normal{},relative{};};
inline ContactSegment contactLerp(ContactSegment a,ContactSegment b,float t){return {a.a+(b.a-a.a)*t,a.b+(b.b-a.b)*t,a.radius+(b.radius-a.radius)*t};}
// Conservative advancement of two moving capsules. Finds first surface
// contact even if a fast fist has crossed the target between rendered frames.
// No proximity alone, withdrawing hand, or animation teleport is a strike.
inline bool sweptMeleeContact(ContactSegment s0,ContactSegment s1,ContactSegment b0,ContactSegment b1,float dt,SweptMeleeContact& out){
    if(!std::isfinite(dt)||dt<.002f||dt>.05f)return false;
    for(V p:{s0.a,s0.b,s1.a,s1.b,b0.a,b0.b,b1.a,b1.b})if(!finite(p))return false;
    const V sa=s1.a-s0.a,sb=s1.b-s0.b,ba=b1.a-b0.a,bb=b1.b-b0.b;
    const float sourceMove=std::max(length(sa),length(sb)),bodyMove=std::max(length(ba),length(bb));
    if(sourceMove>.6f||bodyMove>.6f||sourceMove/dt>20.f||bodyMove/dt>20.f)return false;
    const float bound=sourceMove+bodyMove+std::abs(s1.radius-s0.radius)+std::abs(b1.radius-b0.radius);
    if(bound<1.e-6f)return false;
    float t=0;
    for(int i=0;i<40&&t<=1.f;i++){
        const auto s=contactLerp(s0,s1,t),b=contactLerp(b0,b1,t);V p,q;
        nearestSegments(s.a,s.b,b.a,b.b,p,q);const V normal=normalized(q-p);
        const float gap=length(q-p)-s.radius-b.radius;
        if(gap<=.002f){
            const auto along=[](V p,V a,V b){return std::clamp(dot(p-a,b-a)/std::max(dot(b-a,b-a),1.e-8f),0.f,1.f);};
            const V velocity=(sa+(sb-sa)*along(p,s.a,s.b)-ba-(bb-ba)*along(q,b.a,b.b))*(1.f/dt);
            if(dot(velocity,normal)<=.15f)return false;
            out={t,gap,q-normal*b.radius,normal,velocity};return true;
        }
        t+=std::max((gap-.001f)/bound,.00001f);
    }
    return false;
}
