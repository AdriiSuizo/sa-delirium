#pragma once
#include "Math.hpp"
// 822BF3D0, called at8226CCF8/8226CD4C/8226CFA0 with scalar-1:
// the accepted blow record's +20 direction and default75, not a hand-mass collision.
inline V originalMeleeImpulse(V direction,float supplied=-1.f){
    const float xy=std::sqrt(direction.x*direction.x+direction.y*direction.y);
    if(xy>.01f){direction.x/=xy;direction.y/=xy;direction.z=0;}
    // 82620430: melee damage type1 scales the force by1.5.
    return direction*((supplied>0?supplied:75.f)*1.5f);
}
// 82620430 +30: a live record keeps its time left; its force is replaced.
// A fresh melee record gets0.15s. The integration is performed by the caller.
inline float originalMeleePushSeconds(float remaining){return remaining>0?remaining:.15f;}
