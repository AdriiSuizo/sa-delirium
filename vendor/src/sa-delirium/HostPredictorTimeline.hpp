#pragma once
#include "Math.hpp"
#include "ObstacleTimeline.hpp"
#include <cstdint>

// The native host has already advanced start -> predictedEnd once. These
// frames are located at the owner's handling COM (or the original animated
// owner origin), not an arbitrary component/bounding-box centre. Both are
// snapshots of the SAME generation and native interval, verified by caller.
struct HostPredictorSnapshot {
    T centre{};
    V linear{},angular{};
};
struct HostPredictorCorrection {
    uint64_t owner=0;
    uint32_t frame=0,revision=0;
    float startFraction=0,endFraction=0,seconds=0;
    V deltaLinear{},deltaAngular{},translation{};
    Q rotation{};
};
inline bool hostPredictorRotationValid(Q q){
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&std::abs(n-1.f)<.001f;
}
inline Q hostPredictorWorldRotation(V radians){
    const float angle=length(radians);
    if(angle<1.e-6f)return unit({radians.x*.5f,radians.y*.5f,radians.z*.5f,1.f});
    const float s=std::sin(angle*.5f)/angle;
    return {radians.x*s,radians.y*s,radians.z*s,std::cos(angle*.5f)};
}

class HostPredictorTimeline {
    bool valid_=false;
    uint64_t owner_=0;
    uint32_t frame_=0,revision_=0;
    float duration_=0,consumed_=0;
    HostPredictorSnapshot start_{},predicted_{};
    V deltaLinear_{},deltaAngular_{},translation_{};
    Q rotation_{};
    static bool finiteSnapshot(const HostPredictorSnapshot&s){
        return finite(s.centre.p)&&hostPredictorRotationValid(s.centre.q)&&finite(s.linear)&&finite(s.angular);
    }
public:
    bool begin(uint64_t owner,uint32_t frame,float duration,const HostPredictorSnapshot&start,const HostPredictorSnapshot&predictedEnd){
        if(!owner||!std::isfinite(duration)||duration<=0||duration>.25f||!finiteSnapshot(start)||!finiteSnapshot(predictedEnd))return false;
        // Reinitializing a live interval would erase already committed contact
        // impulses. A new native interval naturally includes their old effect
        // in its native prediction and starts a fresh correction ledger.
        if(valid_&&owner_==owner&&frame_==frame)return false;
        valid_=true;owner_=owner;frame_=frame;revision_=0;duration_=duration;consumed_=0;
        start_=start;predicted_=predictedEnd;deltaLinear_={};deltaAngular_={};translation_={};rotation_={};return true;
    }
    bool matches(uint64_t owner,uint32_t frame)const{return valid_&&owner_==owner&&frame_==frame;}
    float consumedFraction()const{return consumed_;}
    V accumulatedLinear()const{return deltaLinear_;}
    V accumulatedAngular()const{return deltaAngular_;}
    V accumulatedTranslation()const{return translation_;}
    Q accumulatedRotation()const{return rotation_;}
    // Use this frame as the origin when sweeping the COMPLETE displacement
    // from propose(). sample() already includes carried velocity prediction;
    // starting that full sweep there would count the old delta-V twice.
    bool sampleAcceptedBase(float fraction,HostPredictorSnapshot&out)const{
        if(!valid_||!std::isfinite(fraction)||fraction<consumed_||fraction>1.f)return false;
        // Native end velocity is the predicted baseline, not another force
        // that should be integrated here. Interpolating start/end twists would
        // invent the timing of native impulses (for example a wall collision).
        HostPredictorSnapshot result=predicted_;
        result.centre=advanceObstacle(start_.centre,predicted_.centre,1.f,fraction);
        result.centre.p=result.centre.p+translation_;
        result.centre.q=mul(rotation_,result.centre.q);
        result.linear=result.linear+deltaLinear_;result.angular=result.angular+deltaAngular_;
        if(!finiteSnapshot(result))return false;
        out=result;return true;
    }
    bool sample(float fraction,HostPredictorSnapshot&out)const{
        HostPredictorSnapshot result;if(!sampleAcceptedBase(fraction,result))return false;
        // Stage already-known contact velocity over the pending interval as
        // well. This is prediction only: commit records the accepted movement
        // once, so staging again never spends the same displacement twice.
        const float pendingSeconds=duration_*(fraction-consumed_);
        const V pendingMove=deltaLinear_*pendingSeconds,pendingTurn=deltaAngular_*pendingSeconds;
        if(!finite(pendingMove)||!finite(pendingTurn)||!std::isfinite(length(pendingTurn)))return false;
        result.centre.p=result.centre.p+pendingMove;
        result.centre.q=mul(hostPredictorWorldRotation(pendingTurn),result.centre.q);
        if(!finiteSnapshot(result))return false;
        out=result;return true;
    }
    bool propose(float endFraction,float seconds,V deltaLinear,V deltaAngular,V split,V angularSplit,HostPredictorCorrection&out)const{
        if(!valid_||!std::isfinite(endFraction)||endFraction<=consumed_||endFraction>1.f||
           !std::isfinite(seconds)||seconds<=0||
           std::abs(seconds-duration_*(endFraction-consumed_))>std::max(1.e-6f,duration_*2.e-5f)||
           !finite(deltaLinear)||!finite(deltaAngular)||!finite(split)||!finite(angularSplit))return false;
        const V linear=deltaLinear_+deltaLinear,angular=deltaAngular_+deltaAngular;
        // TU8 pre-step82972440/82972650 advances the OLD twist, then the
        // contact solve changes momentum; post-step829726A0 applies push only.
        // Native prediction has already elapsed. Only PRIOR contacts can have
        // travelled during this interval; the new impulse starts at its end.
        const V move=deltaLinear_*seconds+split,turn=deltaAngular_*seconds+angularSplit;
        if(!finite(linear)||!finite(angular)||!finite(move)||!finite(turn)||!std::isfinite(length(turn)))return false;
        const Q rotation=hostPredictorWorldRotation(turn);if(!hostPredictorRotationValid(rotation))return false;
        out={owner_,frame_,revision_,consumed_,endFraction,seconds,deltaLinear,deltaAngular,move,rotation};return true;
    }
    bool commit(const HostPredictorCorrection&step,V actualTranslation,Q actualRotation){
        // The physical solver/sweep owns placement. Retain exactly its accepted
        // result, including a blocked move; never pretend the requested split
        // passed through a wall. Callers apply step.deltaLinear/Angular to the
        // native twist once, not the accumulated values returned by sample().
        if(!valid_||step.owner!=owner_||step.frame!=frame_||step.revision!=revision_||
           step.startFraction!=consumed_||step.endFraction<=consumed_||step.endFraction>1.f||
           !std::isfinite(step.endFraction)||!std::isfinite(step.seconds)||step.seconds<=0||
           std::abs(step.seconds-duration_*(step.endFraction-consumed_))>std::max(1.e-6f,duration_*2.e-5f)||
           !finite(step.deltaLinear)||!finite(step.deltaAngular)||!finite(actualTranslation)||!hostPredictorRotationValid(actualRotation))return false;
        const V linear=deltaLinear_+step.deltaLinear,angular=deltaAngular_+step.deltaAngular,move=translation_+actualTranslation;
        if(!finite(linear)||!finite(angular)||!finite(move))return false;
        deltaLinear_=linear;deltaAngular_=angular;translation_=move;rotation_=mul(actualRotation,rotation_);
        consumed_=step.endFraction;revision_++;return true;
    }
};
