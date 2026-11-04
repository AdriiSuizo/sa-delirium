#pragma once
#include "Math.hpp"
#include <array>

// SA tags 2 and 3 are the two actual torso joints. Tag 2 shares its
// attachment point with pelvis; that does not make its torso body zero length.
// Fred's four bodies subdivide the two native rigid segments. A subdivision
// inherits its segment's orientation, not a pelvis-to-neck quaternion blend.
inline std::array<T,4> nativeTorsoJoints(const T&spine,const T&chest,const T&neck,
                                      float lowerFraction,float upperFraction){
    T lower=mix(spine,chest,lowerFraction);lower.q=spine.q;
    T upper=mix(chest,neck,upperFraction);upper.q=chest.q;
    return {spine,lower,chest,upper};
}
