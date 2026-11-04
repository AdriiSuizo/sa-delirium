#pragma once
#include "OriginalPedMotor.hpp"
#include "OriginalPedMotorBranch.hpp"
// Include after OriginalAnimatedOwner and ExternalMobility. There are no
// physics writes in capture: native Z/support/predictor remain untouched.
// Root enables only after the branch and the common external-drive API are
// installed together. A present source file alone changes no live behaviour.
static bool originalPedMotorEnabled=false;
struct OriginalPedMotorSample {
    uint64_t id=0;int ref=-1,model=-1;void*ped=nullptr;void*clump=nullptr;
    unsigned frame=0;original_ped_motor::Facts facts{};
};
static std::map<uint64_t,OriginalPedMotorSample> originalPedMotorSamples;
static unsigned originalPedMotorFrame=~0u;
static void clearOriginalPedMotors(){originalPedMotorSamples.clear();originalPedMotorFrame=~0u;}
static void beginOriginalPedMotorFrame(){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(originalPedMotorFrame!=frame){originalPedMotorSamples.clear();originalPedMotorFrame=frame;}
}
static void invalidateOriginalPedMotorUpdate(void*p){
    beginOriginalPedMotorFrame();
    if(p){const int ref=reference(p);if(ref>=0)originalPedMotorSamples.erase(animatedExternalId(ref));}
}
static bool originalPedMotorOwnedGetUp(void*p){
    const auto a=agents.find(reference(p));
    return a!=agents.end()&&a->second->ped==p&&a->second->recovering&&a->second->recoveryAnimatedOwner;
}
static bool __cdecl captureOriginalPedMotor(void*p){
    invalidateOriginalPedMotorUpdate(p);
    if(!originalPedMotorEnabled||!ready||!recoveryAllowed()||!p||!animatedNativePairCurrent(p)||!animatedExternalEligible(p))return false;
    const int ref=reference(p);const uint64_t id=animatedExternalId(ref);
    const auto start=hostNativeStarts.find(id);
    if(start==hostNativeStarts.end()||!hostNativeStateCurrent(start->second)||start->second.entity!=p)return false;
    // These are corroborating guards for the verified native branch, not a
    // guessed emulation of the branches that did not reach it.
    const bool sourceGround=originalGroundActive(p);
    if((!(at<unsigned>(p,0x46C)&1)&&!originalGroundSupported(p))||at<void*>(p,0xFC)||
       (at<void*>(p,0x568)&&!sourceGround)||inVehicle(p)||at<float>(p,0x540)<=0)return false;
    const bool getUp=originalPedMotorOwnedGetUp(p);
    // Native SA bFallenDown alone is not proof of original task 205/207/2117.
    if((at<unsigned>(p,0x474)&0x100000)&&!getUp)return false;
    const V authored=V{at<float>(p,0x550),at<float>(p,0x554),0}*50.f;
    if(!finite(authored))return false;
    OriginalPedMotorSample s;
    s.id=id;s.ref=ref;s.model=at<short>(p,0x22);s.ped=p;s.clump=at<void*>(p,0x18);s.frame=originalPedMotorFrame;
    s.facts.authored={authored.x,authored.y,0};s.facts.player=p==player();s.facts.health=at<float>(p,0x540);
    s.facts.fallOrGetUp=getUp;s.facts.drivesVertical=false;
    // support None is proven by this branch; the native special-slope branch
    // bypasses it. SlideIntoCover and door-push memory have no mapped SA owner
    // yet; they are not inferred from arbitrary SA flags or collision speeds.
    originalPedMotorSamples[id]=s;return true;
}
// true means a valid row (possibly zero: the native motor retained authority).
// false means a CLAIMED row became inconsistent; caller must reject the drive
// batch, not silently substitute a new desired velocity or physical mass.
static bool originalPedMotorImpulse(const HostExternalOwner&o,float dt,V&impulse){
    impulse={};if(!o.animated)return true;
    beginOriginalPedMotorFrame();const auto it=originalPedMotorSamples.find(o.id);
    if(it==originalPedMotorSamples.end())return true;
    const auto&s=it->second;void*p=o.entity;
    if(s.frame!=*reinterpret_cast<unsigned*>(0xB7CB4C)||s.ped!=p||s.ref!=o.ref||pedFromRef(s.ref)!=p||s.model!=o.model||
       s.clump!=o.clump||at<void*>(p,0x18)!=s.clump||at<short>(p,0x22)!=s.model||!animatedExternalEligible(p)||
       !std::isfinite(dt)||dt<=0||!finite(o.staged.linear))return false;
    // The same single source mass as the original constrained collider; do
    // not use SA m_fMass=70/80 while the common owner actually has mass25.
    constexpr float inverseMass=1.f/25.f;
    for(int k=9;k<18;k++)if(o.rows[k]!=(k==9||k==13||k==17?inverseMass:0.f))return false;
    auto f=s.facts;f.actual={o.staged.linear.x,o.staged.linear.y,o.staged.linear.z};
    original_ped_motor::State state;const auto r=original_ped_motor::step(f,state,dt);
    impulse={25.f*r.deltaVelocity.x,25.f*r.deltaVelocity.y,25.f*r.deltaVelocity.z};
    return finite(impulse);
}
static void configureOriginalPedMotorCapture(){original_ped_motor_branch::capture=&captureOriginalPedMotor;}
