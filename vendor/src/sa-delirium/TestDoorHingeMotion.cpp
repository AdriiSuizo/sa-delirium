#include "DoorHingeMotion.hpp"
#include "Math.hpp"
#include <cassert>
#include <iostream>
#include <limits>
static void near(float a,float b,float eps=1.e-5f){assert(std::abs(a-b)<eps);}
int main(){
    DoorHingeMotion h;
    assert(!h.sample(1,2,2,100,1000,0,.01f).verified);
    auto m=h.sample(1,2,2,101,1010,-.1f,.01f);assert(m.verified);near(m.speed,-10);
    near(h.sample(1,2,2,101,1010,-.1f,.01f).speed,-10);
    near(h.sample(1,2,2,101,1010,-.12f,.01f).speed,-12);
    near(h.sample(1,2,2,102,1020,-.15f,.01f).speed,-3);
    // Exact live free-door sequence: native PrevAngle remains -.420180.
    h={};h.sample(1,2,2,200,322926,-.249091f,.013616f);
    m=h.sample(1,2,2,201,322943,-.186931f,.017563f);
    near(m.speed,3.5392587f,2.e-5f);
    const float stalePrevSpeed=(-.186931f-(-.420180f))/.017563f;
    assert(stalePrevSpeed>13&&stalePrevSpeed>3*m.speed);
    // A held/reopened door eventually stops; an obsolete Open() angle cannot
    // keep supplying angular velocity after its actual frame stops moving.
    near(h.sample(1,2,2,202,322961,-.186931f,.017563f).speed,0);
    assert(!h.sample(9,2,2,203,322978,-.1f,.017563f).verified); // clump replacement
    assert(!h.sample(9,3,2,204,322996,-.05f,.017563f).verified); // frame replacement
    assert(!h.sample(9,3,1,205,323013,0,.017563f).verified); // changed hinge axis
    assert(!h.sample(9,3,1,207,323048,.1f,.017563f).verified); // missing frame
    assert(h.sample(9,3,1,208,323065,.2f,.017563f).verified);
    assert(!h.sample(9,3,1,209,100,.3f,.017563f).verified); // save/time reset
    assert(!h.sample(9,3,1,210,100,.3f,0).verified); // paused
    assert(!h.sample(9,3,1,211,101,std::numeric_limits<float>::quiet_NaN(),.01f).verified);
    assert(!h.sample(9,3,1,212,111,0,.01f).verified);
    // Native timestep, not rounded/capped wall milliseconds, owns velocity.
    near(h.sample(9,3,1,213,145,.05f,.01f).speed,5);
    // Parent rotation/translation plus hinge rotation gives the same velocity
    // at every material point whether computed from centre or hinge origin.
    const V carVelocity{4,-2,0},carSpin{0,0,.7f},hingeSpin{0,0,m.speed};
    const V com{10,20,1},hinge{11,20,1},centre{11,20.4f,1},palm{11,20.7f,1.2f};
    const V centreVelocity=carVelocity+cross(carSpin,centre-com)+cross(hingeSpin,centre-hinge);
    const V viaCentre=centreVelocity+cross(carSpin+hingeSpin,palm-centre);
    const V viaHinge=carVelocity+cross(carSpin,palm-com)+cross(hingeSpin,palm-hinge);
    assert(length(viaCentre-viaHinge)<1.e-5f);
    std::cout<<"Door hinge motion: consecutive identity/frame, duplicate queries, frozen native PrevAngle, native dt and material-point transport passed\n";
}
