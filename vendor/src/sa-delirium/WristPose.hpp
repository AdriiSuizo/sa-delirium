#pragma once
#include "Math.hpp"

namespace wrist_pose {
constexpr float radians=3.14159265358979323846f/180.f;

inline Q axisAngle(V axis,float angle){
    const float half=angle*.5f,s=std::sin(half);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(half)};
}

struct Limited {
    Q forearm{},hand{};
};

inline Limited limit(Q forearm,Q hand,Q rest,V axis){
    Q relative=mul(mul(conjugate(forearm),hand),conjugate(rest));
    if(relative.w<0)relative={-relative.x,-relative.y,-relative.z,-relative.w};
    const float along=dot({relative.x,relative.y,relative.z},axis);
    const float twistAngle=2.f*std::atan2(along,relative.w);
    const Q twist=std::hypot(along,relative.w)>1.e-5f?axisAngle(axis,twistAngle):Q{};
    Q swing=mul(relative,conjugate(twist));
    if(swing.w<0)swing={-swing.x,-swing.y,-swing.z,-swing.w};
    const V swingAxis{swing.x,swing.y,swing.z};
    const float bend=2.f*std::atan2(length(swingAxis),swing.w);
    // Ambiguous full twists come from the solver or retargeting. Fade the
    // visible roll to neutral near the half-turn to avoid a sign flip.
    const float visible=1.f-std::clamp((std::abs(twistAngle)-120.f*radians)/(60.f*radians),0.f,1.f);
    const float distributed=std::clamp(twistAngle*.5f,-38.f*radians,38.f*radians)*visible;
    const float atWrist=std::clamp(twistAngle-distributed,-24.f*radians,24.f*radians)*visible;
    const float bendVisibility=1.f-std::clamp((bend-155.f*radians)/(25.f*radians),0.f,1.f);
    const Q limitedBend=bend>28.f*radians?
        axisAngle(normalized(swingAxis),28.f*radians*bendVisibility):swing;
    const Q roll=axisAngle(axis,distributed);
    // Bend in the original forearm frame. The distributed roll changes the
    // forearm skin but leaves its wrist endpoint and the bend direction fixed.
    const Q local=mul(limitedBend,axisAngle(axis,distributed+atWrist));
    return {mul(forearm,roll),mul(forearm,mul(local,rest))};
}
}
