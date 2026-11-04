#include "RecoveryMath.hpp"
#include "PlayerPolicy.hpp"
#include "ReactionPolicy.hpp"
#include "ContactMath.hpp"
#include "MeleePolicy.hpp"
#include "OriginalMeleeForce.hpp"
#include "MeleeSweep.hpp"
#include "FootSupportMath.hpp"
#include "BodyContactMath.hpp"
#include "DamageOwnership.hpp"
#include "CombatSkeleton.hpp"
#include "CombatMovement.hpp"
#include "ObstacleTimeline.hpp"
#include <limits>
#include <cassert>
#include <iostream>
int main(){
    assert(nativeOnFootControl(0)&&nativeOnFootControl(8));
    assert(!nativeOnFootControl(2)&&!nativeOnFootControl(4)&&!nativeOnFootControl(6));
    {T old{},target{{0,0,std::sin(.3f),std::cos(.3f)},{1.5f,0,0}};
     for(int i=0;i<3;i++){
        old=advanceObstacle(old,target,.05f-i/60.f,1.f/60);
        assert(std::abs(old.p.x-.5f*(i+1))<1e-5f);
        assert(std::abs(2.f*std::atan2(old.q.z,old.q.w)-.2f*(i+1))<1e-5f);
     }
     assert(length(advanceObstacle(old,target,0,1.f/60).p-old.p)==0);
    }
    // A slow render frame cannot advance the clip by 100 ms while SA moves
    // only 60 ms. A later fast frame must consume the same authored distance.
    assert(std::abs(combatSimulationSeconds(3.f)-.06f)<1e-6f);
    for(float step:{.4f,1.f,3.f})
        assert(std::abs(2.f*combatMotionScale(.06f,step)*step-.12f)<1e-6f);
    assert(combatMotionScale(.06f,0.f)==0.f);
    // Observed knee hit 359084: IV right thigh 423 was unmapped, so native
    // momentum silently became zero. Rotation must move the contact even
    // when the bone's origin does not translate (same for elbows/headbutts).
    assert(combatStrikeBone(423)==51&&combatStrikeBone(0x1a2)==41);
    assert(combatStrikeBone(0x4b5)==5&&combatStrikeBone(0x4c2)==33);
    assert(combatStrikeBone(-1)==-1&&combatStrikeBone(0xdead)==-1);
    assert(combatBodyPart(0x4b2)==7&&combatBodyPart(0x4b3)==8&&combatBodyPart(0x36a0)==9&&combatBodyPart(0x36a1)==10);
    assert(combatBodyPart(0x4c3)==16&&combatBodyPart(0x4d0)==20&&combatBodyPart(0xdead)==-1);
    {T before{},after{{0,0,std::sin(.05f),std::cos(.05f)},{}};
     const V at=rotate(after.q,{.4f,0,0});
     const V velocity=combatPointVelocity(before,after,at,.01f);
     assert(std::abs(velocity.x-(at.x-.4f)*100)<.001f);
     assert(std::abs(velocity.y-at.y*100)<.001f&&length(velocity)>3.9f);
     assert(length(combatAngularVelocity(before.q,after.q,.01f)-V{0,0,10})<.001f);
     Q flipped{-after.q.x,-after.q.y,-after.q.z,-after.q.w};
     assert(length(combatAngularVelocity(before.q,flipped,.01f)-V{0,0,10})<.001f);
     assert(length(combatAngularVelocity(after.q,after.q,.01f))<.001f);
     assert(length(contactMomentum(velocity,{0,1,0},8,20))>22.f);
     assert(length(combatPointVelocity(after,after,at,.01f))<.001f);
     assert(length(combatPointVelocity(before,after,at,0))==0);
     before.p={.1f,0,0};after.p={.12f,0,0};before.q=after.q={};
     assert(length(combatPointVelocity(before,after,{.4f,0,0},.01f)-V{2,0,0})<.001f);}
    // Replay the observed 416500 -> 416522 ms delivery. Different impacts,
    // recycled vehicle slots, stale deliveries and non-road hits must not
    // receive this continuity exemption. Unsigned clock wrap stays valid.
    const RoadDamageReceipt road{416500,11011,49,3};
    assert(sameDeliveredRoadDamage(road,road,416522));
    assert(!sameDeliveredRoadDamage({},road,416522));
    assert(!sameDeliveredRoadDamage(road,{416501,11011,49,3},416522));
    assert(!sameDeliveredRoadDamage(road,{416500,11012,49,3},416522));
    assert(!sameDeliveredRoadDamage(road,{416500,11011,50,3},416522));
    assert(!sameDeliveredRoadDamage(road,{416500,11011,49,9},416522));
    assert(!sameDeliveredRoadDamage(road,road,416651));
    assert(!sameDeliveredRoadDamage(road,road,416499));
    assert(!sameDeliveredRoadDamage({42,10,0,3},{42,10,0,3},50));
    assert(sameDeliveredRoadDamage({0xfffffff0u,10,49,3},{0xfffffff0u,10,49,3},6));
    // Actual combat failure: the 434957 ms authored hit returned as native
    // event 9 at 434970 and incorrectly released its just-created NM body.
    const CombatDamageReceipt melee{434957,770,0,6};
    assert(sameDeliveredCombatDamage(melee,melee,434970));
    assert(!sameDeliveredCombatDamage({},melee,434970));
    assert(!sameDeliveredCombatDamage(melee,{434958,770,0,6},434970));
    assert(!sameDeliveredCombatDamage(melee,{434957,771,0,6},434970));
    assert(!sameDeliveredCombatDamage(melee,{434957,770,1,6},434970));
    assert(!sameDeliveredCombatDamage(melee,{434957,770,0,9},434970));
    assert(!sameDeliveredCombatDamage(melee,melee,435108));
    assert(!sameDeliveredCombatDamage(melee,melee,434956));
    assert(sameDeliveredCombatDamage({0xfffffff0u,1,0,3},{0xfffffff0u,1,0,3},6));
    // Coupled suspension must dissipate discrete kinetic+spring energy, at
    // varying body mass, frame rate, compression and approach/separation.
    for(float mass:{2.f,80.f,1200.f})for(float dt:{1.f/144,1.f/60,1.f/30})
    for(float compression:{.001f,.03f,.4f})for(float velocity:{-4.f,0.f,4.f}){
        const float mobility=1.f/mass+1.f/1200,k=30000;
        const float j=coupledSpringImpulse(k*compression*dt,compression,velocity,dt,mobility);
        const float after=velocity+j*mobility,x=std::max(0.f,compression-after*dt);
        const float beforeE=.5f*velocity*velocity/mobility+.5f*k*compression*compression;
        const float afterE=.5f*after*after/mobility+.5f*k*x*x;
        assert(afterE<=beforeE+.003f);
        const float damping=dissipativeImpulse(10000,std::abs(after),mobility);
        assert(damping*mobility<=std::abs(after)+.0001f);
    }
    assert(dissipativeImpulse(100,-3,.1f)==0);
    {BodyContact c;assert(capsuleContact({.15f,0,0},{.15f,0,1},.1f,{0,0,0},{0,0,1},.1f,c));
     assert(std::abs(c.depth-.05f)<1.e-5f&&c.normal.x>.999f);
     assert(!capsuleContact({1,0,0},{1,0,1},.1f,{0,0,0},{0,0,1},.1f,c));
     const float j=finiteContactImpulse(1.f,1.f/80,1.f/80);assert(std::abs(j-40.f)<.001f);
     assert(std::abs((1-j/80)-j/80)<.001f); // equal final speeds, conserved momentum
     assert(finiteContactImpulse(-1,1.f/80,1.f/80)==0);
     assert(finiteContactImpulse(1,1.f/1200,1.f/80)>j); // car transfers more than walker
     assert(std::abs(suspensionFraction(.75f,.5f,1.f)-.5f)<.0001f);
     assert(suspensionFraction(1,.5f,1)==1&&suspensionFraction(.5f,.5f,1)==0);}
    {FootSurface f;assert(footCapsule({0,0,1},2,{-1,0,0},{1,0,0},.2f,f));assert(std::abs(f.z-.2f)<1.e-5f&&f.normal.z>.999f);
     assert(!footCapsule({0,1,1},2,{-1,0,0},{1,0,0},.2f,f));
     assert(footCapsule({2430,-1658,14},2,{2430,-1658,12.4f},{2430,-1658,12.8f},.15f,f));assert(std::abs(f.z-12.95f)<.001f);}
    {LegSupportState s;std::array<FootSurface,2> f{{{true,.25f},{true,0}}};
     for(int n=0;n<120;n++)stepLegSupport(s,f,0,.95f,0,1.f/120);
     assert(std::abs(s.lift[0]-.25f)<.001f&&std::abs(s.lift[1])<.001f&&std::abs(s.drop)<.001f);
     V k,p;assert(supportLeg({0,0,.9f},{0,0,.45f},{0,0,.25f},{0,1,0},.45f,.45f,k,p));
     assert(std::abs(length(k-V{0,0,.9f})-.45f)<1.e-5f&&std::abs(length(p-k)-.45f)<1.e-5f&&k.y>0);
     f={};for(int n=0;n<120;n++)stepLegSupport(s,f,0,.95f,0,1.f/120);assert(std::abs(s.lift[0])<.001f);}
    // A fast sphere crossing a body within one frame must hit its near face,
    // even though the endpoint has already passed out through the far side.
    for(float dt:{.0083333f,.0166667f,.0333333f}){
        SweptMeleeContact hit;const float distance=dt*5.f;
        ContactSegment a{{-.20f-distance,0,0},{-.20f-distance,0,0},.10f};
        ContactSegment b{{-.20f+distance,0,0},{-.20f+distance,0,0},.10f};
        ContactSegment body{{0,0,-.3f},{0,0,.3f},.1f};
        assert(sweptMeleeContact(a,b,body,body,dt,hit));
        assert(hit.normal.x>.999f&&std::abs(hit.point.x+.1f)<.0001f&&std::abs(hit.relative.x-10.f)<.001f);
        assert(!sweptMeleeContact(b,a,body,body,dt,hit)); // withdrawing from contact
        auto far=body;far.a.y=far.b.y=1;assert(!sweptMeleeContact(a,b,far,far,dt,hit));
    }
    {SweptMeleeContact hit;ContactSegment a{{-.5f,0,0},{-.5f,0,0},.1f},b{{.5f,0,0},{.5f,0,0},.1f},body{{},{},.1f};
     assert(!sweptMeleeContact(a,b,body,body,.008f,hit));} // teleport, not a punch
    assert(strikerMask("head")==4&&!(strikerMask("head")&strikerMask("hands")));
    assert(!(strikerMask("left_hand")&strikerMask("left_foot")));
    assert(strikerMask("native")==0&&strikerMask("invalid")==0);
    assert(length(poseAngularVelocity(Q{},Q{0,0,std::sin(.01f),std::cos(.01f)},.01f)-V{0,0,2})<.001f);
    assert(length(poseAngularVelocity(Q{},Q{},0))==0);
    assert(meleeResponse(0,false)==MeleeResponse::Flinch);
    assert(meleeResponse(.7f,false)==MeleeResponse::Flinch);
    assert(meleeResponse(.7001f,false)==MeleeResponse::Balance);
    assert(meleeResponse(1,true)==MeleeResponse::Shove);
    assert(length(contactMomentum({0,-3,0},{0,1,0},3,6))==0);
    // Inelastic collision: both free masses have the same post-hit normal
    // speed, conserve momentum and cannot gain kinetic energy.
    for(float incoming:{.2f,3.f,10.f})for(float a:{1.f,3.f,8.f})for(float b:{1.f,6.f,20.f}){
        const V j=contactMomentum({0,incoming,2},{0,1,0},a,b);
        const float afterA=incoming-j.y/a,afterB=j.y/b;
        assert(std::abs(afterA-afterB)<.0001f&&j.x==0&&j.z==0);
        assert(std::abs(a*incoming-(a*afterA+b*afterB))<.0001f);
        assert(a*afterA*afterA+b*afterB*afterB<=a*incoming*incoming+.0001f);
    }
    assert(length(contactMomentum({0,3,0},{0,1,0},0,6))==0);
    {V h,b;nearestSegments({-1,0,0},{1,0,0},{0,-1,0},{0,1,0},h,b);assert(length(h-b)<.0001f);}
    {V h,b;nearestSegments({0,0,0},{1,0,0},{2,1,0},{2,2,0},h,b);assert(length(h-V{1,0,0})<.0001f&&length(b-V{2,1,0})<.0001f);}
    {V h,b;nearestSegments({0,0,0},{0,0,0},{-1,1,0},{1,1,0},h,b);assert(length(h)<.0001f&&length(b-V{0,1,0})<.0001f);}
    assert(!coherentPush({0,3,0},{0,-3,0},0,0));
    assert(!coherentPush({0,3,0},{0,3,0},0,.4f));
    assert(coherentPush({0,3,0},{0,3,0},0,0));
    // Contact IK may retract a hand, but must never stretch either arm segment
    // or put the elbow on the opposite side just because the target is far away.
    for(float yaw:{0.f,.8f,3.1415926f})for(float reach:{.05f,.2f,.42f,.8f}){
        Q q{0,0,std::sin(yaw*.5f),std::cos(yaw*.5f)};V origin{2430,-1658,14};
        V s=origin,e=origin+rotate(q,{.2f,.2f,0}),w=origin+rotate(q,{.45f,.1f,0}),goal=origin+rotate(q,{reach,.05f,.03f}),ne,nw;
        assert(solveContactArm(s,e,w,goal,ne,nw));
        assert(std::abs(length(ne-s)-length(e-s))<.001f&&std::abs(length(nw-ne)-length(w-e))<.001f);
        assert(length(nw-s)<=length(e-s)+length(w-e)+.001f);
        assert(length(rotate(turnBetween(e-s,ne-s),normalized(e-s))-normalized(ne-s))<.001f);
        if(reach==.42f)assert(length(nw-goal)<.001f);
    }
    {V e,w;assert(!solveContactArm({},{},{},{.1f,0,0},e,w));}
    assert(length(segmentNear({1,1,0},{0,0,0},{2,0,0})-V{1,0,0})<.0001f);
    assert(length(rotate(turnBetween({1,0,0},{-1,0,0}),{1,0,0})-V{-1,0,0})<.0001f);
    assert(std::abs(pedMomentumCorrection(10,0,1200,80,false)-9.375f)<1.e-5f); // finite-mass contact, never faster than before
    assert(pedMomentumCorrection(10,10,1200,80,false)==0);
    assert(pedMomentumCorrection(10,2,1200,80,true)==0); // wall / car cannot be undone
    assert(pedMomentumCorrection(10,2,0,80,false)==0);
    assert(pedMomentumCorrection(10,2,1200,0,false)==0);
    assert(pedMomentumCorrection(10,2,1200,std::numeric_limits<float>::quiet_NaN(),false)==0);
    assert(pedMomentumCorrection(10,8,1200,160,false)<pedMomentumCorrection(10,8,1200,80,false));
    assert(!movingExit(10.f/3.6f,15)&&movingExit(15.f/3.6f,15)&&movingExit(30.f/3.6f,15));
    for(int side:{-1,1}){
        assert(lateralExitClearance(side*.6f,-1,1,side,.2f)<0); // detached but still in cabin
        assert(lateralExitClearance(side*1.15f,-1,1,side,.2f)<0); // shape still overlaps side
        assert(lateralExitClearance(side*1.3f,-1,1,side,.2f)>.09f);
    }
    V inertia=inheritedVelocity({0,10,1},{0,0,2},{1,0,0},{0,0,0});assert(length(inertia-V{0,12,1})<.0001f);
    assert(blastDamage(0,10)>100&&blastDamage(5,10)>30&&blastDamage(5,10)<60&&blastDamage(10,10)==0);
    assert(articulatedHelper(6)&&articulatedHelper(7)&&articulatedHelper(302)&&articulatedHelper(8)&&!articulatedHelper(5));
    assert(directedHit({0,1,10},30).z<6.1f);
    M scaled{};scaled.right={0,2,0};scaled.up={-3,0,0};scaled.at={0,0,.7f};scaled.pos={2420,-1660,12};M inverted{};
    assert(affineInverse(scaled,inverted));V testPoint{.1f,.2f,.3f};assert(length(point(inverted,point(scaled,testPoint))-testPoint)<.001f);
    for(int weapon=0;weapon<60;weapon++){
        assert(playerRoadDamage(weapon,true)==(weapon==49||weapon==50));
        assert(!playerRoadDamage(weapon,false));
    }
    for(unsigned type=0;type<60;type++){
        assert(playerCrashEjection(type,1.f,10.f)==(type==49||type==53||type==54));
        assert(!playerCrashEjection(type,0.f,0.f));
        assert(!playerCrashEjection(type,std::numeric_limits<float>::quiet_NaN(),10.f));
        assert(playerForcedRiderRelease(type,true,0.f)==(type==0||type==55));
        assert(!playerForcedRiderRelease(type,false,0.f));
        assert(!playerForcedRiderRelease(type,true,10.f));
        assert(!playerForcedRiderRelease(type,true,std::numeric_limits<float>::quiet_NaN()));
    }
    for(int type=0;type<12;type++)assert(playerGroundVehicle(type)==(type!=3&&type!=4&&type!=5&&type!=7&&type!=8));
    assert(playerMovingContact(3,4));assert(!playerMovingContact(0,4));assert(!playerMovingContact(-3,4));assert(!playerMovingContact(3,-4));
    std::vector<T> clip(6);clip[0].p={0,0,0};clip[1].p={.2f,.4f,.1f};clip[2].p={-.3f,.6f,.2f};clip[3].p={-.5f,-.5f,0};clip[4].p={.4f,-.8f,0};clip[5].p={0,1.2f,.2f};
    const std::vector<int> nodes{0,1,2,3,4,5};
    for(float angle:{0.f,.4f,1.5707963f,3.1415926f,-2.6f}){
        T place{{0,0,std::sin(angle*.5f),std::cos(angle*.5f)},{2431.f,-1656.f,0}};
        auto body=clip;for(auto&t:body)t=mul(place,t);
        auto f=fitRecovery(clip,body,nodes);assert(f.valid&&f.error<1.e-6f);
        for(size_t n=0;n<clip.size();n++)assert(length(mul(f.delta,clip[n]).p-body[n].p)<.002f);
        // Height is determined by support, so fitting may not move the ground.
        for(auto&t:body)t.p.z+=2.f;auto raised=fitRecovery(clip,body,nodes);assert(raised.valid&&raised.delta.p.z==0&&raised.error>3.99f);
    }
    auto front=clip;front[2].p.z=1.5f;assert(fitRecovery(clip,clip,nodes).error<fitRecovery(front,clip,nodes).error);
    assert(!fitRecovery({},clip,nodes).valid);assert(!fitRecovery(clip,clip,{-1}).valid);
    assert(!fitRecovery(std::vector<T>(6),std::vector<T>(6),nodes).valid);
    // A bent but standing original response blends; a fallen response gets up.
    assert(originalStandingRecovery(true,true,false,false));
    assert(!originalStandingRecovery(true,true,true,true));
    assert(originalStandingRecovery(false,false,true,true));
    assert(!originalStandingRecovery(false,false,false,false));
    assert(!originalStandingRecovery(true,false,false,false)); // unbalanced/lying, fallen flag alone insufficient
    assert(length(originalMeleeImpulse({3,4,9})-V{67.5f,90,0})<.0001f);
    assert(length(originalMeleeImpulse({0,1,0},20)-V{0,30,0})<.0001f);
    assert(length(originalMeleeImpulse({0,0,1})-V{0,0,112.5f})<.0001f);
    for(float pending:{0.f,.15f,.073f,.001f})for(float dt:{1.f/30,1.f/60,1.f/120}){
        float left=originalMeleePushSeconds(pending),sum=0;const float force=112.5f/left;
        if(pending>0)assert(left==pending);
        while(left>0){const float h=std::min(left,dt);sum+=h*force;left-=h;}
        assert(std::abs(sum-112.5f)<.001f);
    }
    assert(recoveryEase(0)==0&&recoveryEase(1)==1&&recoveryEase(.5f)==.5f);
    assert(recoveryEase(.001f)<.00001f&&1-recoveryEase(.999f)<.00001f);
    std::cout<<"Recovery fitting: yaw/translation, ground constraint, candidate choice, invalid poses and smooth endpoints passed\n";
}
