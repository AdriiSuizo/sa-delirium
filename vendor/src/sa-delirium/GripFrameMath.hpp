#pragma once
#include "Math.hpp"

// A grip lives in the physical component frame. Transport the EXACT same
// material point through the body-to-skin binding for diagnostics. Neither
// wrist origin nor a guessed palm offset is the constrained point.
inline V gripPointOnSkin(V physicalPoint,const T&body,const T&bodyToBone,const T&skinBone){
    const T bodyPoint{{},physicalPoint};
    const V boneLocal=mul(inverse(bodyToBone),mul(inverse(body),bodyPoint)).p;
    return mul(skinBone,T{{},boneLocal}).p;
}

