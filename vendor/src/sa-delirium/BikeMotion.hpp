#pragma once
#include "Math.hpp"

// SA stores linear/angular velocities per 1/50 s. CPhysical's linear velocity
// is at its handling centre of mass (local +0xA4), not its model origin.
struct BikeRigidMotion {
    V linear{}, angular{}, centre{};
};
inline BikeRigidMotion bikeRigidMotion(const M& frame,V localCentre,V linear,V angular){
    return {linear,angular,point(frame,localCentre)};
}
inline V bikePointVelocity(const BikeRigidMotion& motion,V pointWorld){
    return motion.linear+cross(motion.angular,pointWorld-motion.centre);
}
