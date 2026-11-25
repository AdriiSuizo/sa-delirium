#pragma once
#include "ContactMath.hpp"

namespace cover_skin {
// IV supplies the hip, knee and ankle trajectories. Orient CJ's skin around
// those same segments using his own bind frame to avoid counter-twisting the
// two heavily overlapping thigh influences at the crotch of his jeans.
inline void alignSegment(T* pose,const T* bindLocal,int parent,int joint,int child){
    const V segment=pose[child].p-pose[joint].p;
    const Q neutral=mul(pose[parent].q,bindLocal[joint].q);
    const V restAxis=rotate(neutral,bindLocal[child].p);
    if(length(segment)<1.e-5f||length(restAxis)<1.e-5f)return;
    const float alignment=dot(normalized(restAxis),normalized(segment));
    const Q oriented=mul(turnBetween(restAxis,segment),neutral);
    // The shortest swing is ambiguous around 180 degrees. Fade back to the
    // source orientation as the directions become antiparallel.
    const float gain=std::clamp((1.f+alignment)/.08f,0.f,1.f);
    pose[joint].q=mix(pose[joint].q,oriented,gain);
}
inline void alignLeg(T* pose,const T* bindLocal,int pelvis,int hip,int knee,int ankle){
    alignSegment(pose,bindLocal,pelvis,hip,knee);
    alignSegment(pose,bindLocal,hip,knee,ankle);
}
}
