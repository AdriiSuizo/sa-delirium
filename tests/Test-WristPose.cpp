#include "../vendor/src/sa-delirium/WristPose.hpp"
#include <cassert>

static float angle(Q q){
    return 2.f*std::acos(std::clamp(std::abs(q.w),0.f,1.f))/wrist_pose::radians;
}

int main(){
    using namespace wrist_pose;
    const V axis=normalized({.8f,.3f,-.2f});
    const V side=normalized(cross(axis,{0,0,1}));
    const Q rest=axisAngle(side,.8f), forearm=axisAngle({0,0,1},1.3f);
    for(float bend:{0.f,15.f,45.f,90.f,145.f,179.f})
        for(float twist:{-179.f,-140.f,-90.f,-45.f,0.f,45.f,90.f,140.f,179.f}){
            const Q expected=mul(mul(axisAngle(side,bend*radians),axisAngle(axis,twist*radians)),rest);
            const auto pose=limit(forearm,mul(forearm,expected),rest,axis);
            const Q local=mul(mul(conjugate(pose.forearm),pose.hand),conjugate(rest));
            assert(angle(local)<38.f);
            assert(length(rotate(pose.forearm,axis)-rotate(forearm,axis))<1.e-5f);
        }
    const auto gentle=limit(forearm,mul(forearm,rest),rest,axis);
    assert(angle(mul(gentle.hand,conjugate(mul(forearm,rest))))<.1f);
}
