#pragma once
#include "ContactMath.hpp"
#include <array>
// Adapted from the read-only LSC4 legik.rs (0x8235B640 / 0x8235A900).
// SA's actual ground/root clearance replaces IV's one metre convention.
struct FootSurface {bool hit=false;float z=0;V normal{0,0,1};int body=-1,part=-1;};
struct LegSupportState {float weight[2]{},global=0,drop=0,acc=0,low=0,lift[2]{};bool hasLow=false;};
// The native probe (IV 823581F0) supplies ground; an upright peer is a
// body collision, not terrain. The SA body-step extension may sample only a
// unbalanced physical owner, and never a standing/native recovery owner.
inline bool physicalFootSupportEligible(bool fallen,bool balancing,bool nativeOwner,bool uprightRecovery){
    return (fallen||!balancing)&&!nativeOwner&&!uprightRecovery;
}
inline float supportApproach(float a,float b,float step){return a+std::clamp(b-a,-step,step);}
inline void stepLegSupport(LegSupportState&s,const std::array<FootSurface,2>&f,float base,float pelvis,float speed,float dt,float maxLift=.4f){
    dt=std::clamp(dt,0.f,.05f);s.global=std::min(1.f,s.global+dt*5.f);
    for(int i=0;i<2;i++)s.weight[i]=std::clamp(s.weight[i]+(f[i].hit?1.f:-1.f)*dt*5.f,0.f,1.f);
    const float gl=f[0].hit?f[0].z:base,gr=f[1].hit?f[1].z:base,g=std::min(gl,gr);
    const float k=std::clamp(1.f+.3f*(speed-1.5f),0.f,1.f);
    s.acc+=dt*10.f*k*(g-base-2.f*s.acc);
    const float pace=std::max(1.f,1.f+.7f*(speed-2.5f));
    s.low=s.hasLow?supportApproach(s.low,g,2.f*dt*pace):g;s.hasLow=true;
    s.low=std::clamp(s.low,g-.3f,g+.3f);
    const float fast=std::clamp((speed-1.5f)*.5f,0.f,1.f);
    float drop=(1.f-fast)*(s.low-base)+fast*std::min(s.acc,0.f);
    if(drop<0)drop=std::min(0.f,drop+std::max(0.f,.9f-(pelvis-base)));
    drop+=std::max(0.f,std::max(gl,gr)+.35f-(pelvis+drop));
    s.drop=supportApproach(s.drop,std::clamp(drop,-.3f,.3f),dt);
    for(int i=0;i<2;i++)s.lift[i]=supportApproach(s.lift[i],f[i].hit?std::clamp((f[i].z-base)*s.weight[i],-.4f,maxLift):0.f,2.f*dt);
}
// Downward ray against a finite capsule. Work relative to its endpoint to
// retain precision at SA world coordinates; return the first upward surface.
inline bool rayCapsule(V from,V d,float depth,V a,V b,float radius,float&distance,V&hitNormal){
    if(!finite(from)||!finite(a)||!finite(b)||radius<=0||depth<=0)return false;
    const V ba=b-a,oa=from-a;float best=depth+1;V normal{};
    auto accept=[&](float t,V n){if(t>=0&&t<=depth&&t<best&&n.z>.1f){best=t;normal=n;}};
    const float baba=dot(ba,ba),bard=dot(ba,d),baoa=dot(ba,oa),rdoa=dot(d,oa);
    const float aa=baba-bard*bard,bb=baba*rdoa-baoa*bard,cc=baba*dot(oa,oa)-baoa*baoa-radius*radius*baba;
    const float disc=bb*bb-aa*cc;
    if(aa>1.e-8f&&disc>=0){float t=(-bb-std::sqrt(disc))/aa,y=baoa+t*bard;
        if(y>=0&&y<=baba)accept(t,normalized(oa+d*t-ba*(y/baba)));}
    for(V c:{a,b}){const V oc=from-c;const float along=dot(oc,d),h=along*along-dot(oc,oc)+radius*radius;
        if(h>=0){const float t=-along-std::sqrt(h);accept(t,normalized(oc+d*t));}}
    if(best>depth)return false;distance=best;hitNormal=normal;return true;
}
inline bool footCapsule(V from,float depth,V a,V b,float radius,FootSurface&out){
    float distance=0;V normal{};
    if(!rayCapsule(from,{0,0,-1},depth,a,b,radius,distance,normal))return false;
    out.hit=true;out.z=from.z-distance;out.normal=normal;return true;
}
inline bool supportLeg(V hip,V knee,V goal,V forward,float upper,float lower,V&newKnee,V&newFoot){
    // A straight SA leg has no useful bend plane. Its native forward vector
    // supplies the pole, never an arbitrary sideways bend.
    if(!finite(goal)||upper<.05f||lower<.05f||upper>.8f||lower>.8f||length(goal-hip)<1.e-5f)return false;
    const V axis=normalized(goal-hip);
    V pole=knee-hip-axis*dot(knee-hip,axis);
    if(length(pole)<.015f)pole=forward-axis*dot(forward,axis);
    if(length(pole)<1.e-5f)return false;
    const float d=std::clamp(length(goal-hip),std::abs(upper-lower)+.001f,upper+lower-.001f);
    const float along=(upper*upper+d*d-lower*lower)/(2*d);
    newKnee=hip+axis*along+normalized(pole)*std::sqrt(std::max(0.f,upper*upper-along*along));
    newFoot=hip+axis*d;return finite(newKnee)&&finite(newFoot);
}
