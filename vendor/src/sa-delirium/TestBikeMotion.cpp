#include "BikeMotion.hpp"
#include <cassert>
#include <iostream>
static bool near(V a,V b){return length(a-b)<1.e-5f;}
int main(){
    // A bike pitching at 1 rad/s, COM one metre ahead of its origin. A rider
    // half a metre above the COM retains 4 m/s upward ramp momentum; rotating
    // offset contributes only the analytically expected 0.5 m/s fore/aft.
    M frame{};write(frame,T{});
    auto motion=bikeRigidMotion(frame,{0,1,0},{0,.4f,.08f},{.02f,0,0});
    assert(near(motion.centre,{0,1,0}));
    assert(near(bikePointVelocity(motion,{0,1,.5f})*50,{0,19.5f,4}));
    // The same physical body expressed around a different model origin must
    // have identical point velocities and kinetic energy.
    M shifted=frame;shifted.pos={10,4,-2};
    auto same=bikeRigidMotion(shifted,{-10,-3,2},motion.linear,motion.angular);
    assert(near(motion.centre,same.centre));
    for(V p:{V{-.4f,1,.1f},V{.4f,1,.1f},V{0,1,.9f}}){
        const V a=bikePointVelocity(motion,p),b=bikePointVelocity(same,p);
        assert(near(a,b));assert(std::abs(dot(a,a)-dot(b,b))<1.e-6f);
    }
    // Angularly rotating the entire fixture rotates its inherited velocity;
    // it must not turn world-up motion into a synthetic upwards kick.
    const Q q=unit({0,0,.70710678f,.70710678f});
    M turned{};write(turned,{q,{}});
    auto rotated=bikeRigidMotion(turned,{0,1,0},rotate(q,motion.linear),rotate(q,motion.angular));
    assert(near(bikePointVelocity(rotated,rotate(q,{0,1,.5f})),rotate(q,bikePointVelocity(motion,{0,1,.5f}))));
    std::cout<<"bike motion: coherent COM, upward inertia, frame covariance and energy invariance passed\n";
}
