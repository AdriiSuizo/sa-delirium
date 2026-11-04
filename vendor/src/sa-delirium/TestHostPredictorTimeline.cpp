#include "HostPredictorTimeline.hpp"
#include "NativeFrameSteps.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

static void check(bool value,const char*reason){if(!value)throw std::runtime_error(reason);}
static bool near(V a,V b,float e=2.e-5f){return length(a-b)<e;}
static bool near(Q a,Q b){return std::abs(std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)-1.f)<2.e-5f;}
static HostPredictorSnapshot state(V p,V v={},Q q={},V w={}){return{{q,p},v,w};}
int main(){try{
    unsigned cases=0;
    for(float dt:{1.f/144.f,1.f/120.f,1.f/60.f,1.f/30.f,.05f,.071f,.1f}){
        NativeFrameSteps steps(dt);HostPredictorTimeline timeline;
        const auto start=state({5,7,2},{7,0,0});
        const auto predicted=state({5+7*dt,7,2},{7,0,0},hostPredictorWorldRotation({0,0,.4f}));
        check(timeline.begin(11,100,dt,start,predicted),"native baseline begin");
        for(int i=0;i<steps.count;i++){
            const float f=float(i+1)/steps.count,h=steps.step(i);HostPredictorSnapshot current;
            check(timeline.sample(f,current),"native baseline stage");
            check(near(current.centre.p,start.centre.p+(predicted.centre.p-start.centre.p)*f),"native predictor position advanced twice");
            check(near(current.centre.q,hostPredictorWorldRotation({0,0,.4f*f})),"native rotation advanced twice");
            check(near(current.linear,predicted.linear),"predicted baseline changed");
            HostPredictorCorrection correction;
            check(timeline.propose(f,h,{},{},{},{},correction),"native baseline proposal");
            check(near(correction.translation,{}),"native baseline emitted displacement");
            check(timeline.commit(correction,correction.translation,correction.rotation),"native baseline commit");
            cases++;
        }
        check(near(timeline.accumulatedTranslation(),{}),"unforced predictor has correction");
    }
    // An early impulse must continue advancing the owner on every later
    // substep even when no later contact generates another delta velocity.
    for(int substeps:{1,2,3,6,9}){
        const float dt=.1f;HostPredictorTimeline timeline;auto s=state({10,20,30},{4,0,0});
        auto e=s;e.centre.p.x+=4*dt;check(timeline.begin(3,101,dt,s,e),"impulse begin");
        V expected{};
        for(int i=0;i<substeps;i++){
            const float f=float(i+1)/substeps,prior=float(i)/substeps,h=dt*(f-prior);
            HostPredictorSnapshot current;check(timeline.sample(f,current),"impulse stage");
            const V pending=i?V{2,1,-.5f}*h:V{};
            check(near(current.centre.p,s.centre.p+(e.centre.p-s.centre.p)*f+expected+pending),"stage omitted carried contact velocity");
            HostPredictorSnapshot acceptedBase;check(timeline.sampleAcceptedBase(f,acceptedBase)&&near(acceptedBase.centre.p+pending,current.centre.p),"accepted sweep base includes pending prediction twice");
            HostPredictorSnapshot repeat;check(timeline.sample(f,repeat)&&near(current.centre.p,repeat.centre.p),"stage mutated timeline");
            HostPredictorCorrection c;const V dv=i?V{}:V{2,1,-.5f};
            check(timeline.propose(f,h,dv,{0,0,i?0.f:2.f},{},{},c),"impulse propose");
            check(near(c.translation,pending),"new impulse integrated backwards into elapsed substep");
            expected=expected+c.translation;check(timeline.commit(c,c.translation,c.rotation),"impulse commit");
            check(!timeline.commit(c,c.translation,c.rotation),"double commit accepted");
            cases++;
        }
        const float remainingSeconds=dt*(substeps-1)/substeps;
        check(near(timeline.accumulatedTranslation(),V{2,1,-.5f}*remainingSeconds),"post-contact impulse timestamp lost");
        check(near(timeline.accumulatedRotation(),hostPredictorWorldRotation({0,0,2*remainingSeconds})),"angular impulse timestamp lost");
        HostPredictorSnapshot final;check(timeline.sample(1,final),"final sample");
        check(near(final.linear,{6,1,-.5f}),"twist double applied or lost");
    }
    // Split corrections alter accepted placement only; they must never become
    // velocity or be automatically repeated on the following substep.
    HostPredictorTimeline split;auto zero=state({});check(split.begin(5,9,.1f,zero,zero),"split begin");
    HostPredictorCorrection c;check(split.propose(.5f,.05f,{2,0,0},{},{0,0,.3f},{},c),"split propose");
    check(near(c.translation,{0,0,.3f}),"split must not include newly solved velocity");
    check(split.commit(c,{.02f,0,.1f},{}),"partly blocked commit");
    HostPredictorSnapshot staged;check(split.sample(1,staged),"partly blocked sample");
    check(near(staged.centre.p,{.12f,0,.1f}),"unaccepted move leaked into prediction");
    check(split.propose(1,.05f,{},{},{},{},c)&&near(c.translation,{.1f,0,0}),"split reapplied as velocity");
    check(split.commit(c,c.translation,c.rotation),"split second commit");
    check(near(split.accumulatedTranslation(),{.12f,0,.1f}),"accepted displacement ledger");cases++;

    // Preserve existing velocity across a native frame boundary by letting the
    // NEXT native prediction incorporate it; do not retain the old ledger.
    auto nextStart=state({.12f,0,.1f},{2,0,0}),nextEnd=nextStart;nextEnd.centre.p.x+=.2f;
    check(!split.begin(5,9,.1f,nextStart,nextEnd),"same-frame reset erased contacts");
    check(split.begin(5,10,.1f,nextStart,nextEnd),"next native interval begin");
    check(split.sample(1,staged)&&near(staged.centre.p,nextEnd.centre.p)&&near(staged.linear,{2,0,0}),"prior contact integrated twice next frame");
    check(split.propose(1,.1f,{},{},{},{},c)&&near(c.translation,{}),"old velocity delta carried into new baseline");cases++;

    // Reject stale owner/frame/revision, bad duration and non-finite data
    // without changing the already accepted trajectory.
    auto wrong=c;wrong.owner=6;check(!split.commit(wrong,{},{}),"different owner accepted");
    wrong=c;wrong.frame++;check(!split.commit(wrong,{},{}),"different frame accepted");
    wrong=c;wrong.revision++;check(!split.commit(wrong,{},{}),"different revision accepted");
    wrong=c;wrong.seconds=.03f;check(!split.commit(wrong,{},{}),"mismatched interval accepted");
    check(!split.propose(.5f,.1f,{},{},{},{},wrong),"wrong substep duration accepted");
    check(!split.sample(-.1f,staged)&&!split.sample(1.1f,staged),"invalid fraction accepted");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    check(!split.commit(c,{nan,0,0},{}),"nonfinite accepted translation");
    check(!split.commit(c,{},Q{0,0,0,0}),"nonunit accepted rotation");
    check(!split.propose(1,.1f,{nan,0,0},{},{},{},wrong),"nonfinite impulse accepted");
    check(split.consumedFraction()==0&&near(split.accumulatedTranslation(),{}),"refused operation changed state");
    check(split.commit(c,c.translation,c.rotation),"valid commit after rejected attempts");
    check(!split.sample(.5f,staged),"sample rewound committed time");cases++;
    std::cout<<"PASS "<<cases<<" predictor substeps/cases: common fractions, carried linear/angular velocity, exact accepted split, single commits and native-frame reset\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
