#pragma once
#include <cmath>
// A completed render must belong to the immediately preceding host interval.
// There is no time-based grace period that reuses a stale get-up palette.
inline bool recoveryContactSampleCurrent(unsigned sample,unsigned previousProcess,unsigned now,bool primed){
    return primed&&now>=previousProcess&&sample>=previousProcess&&sample<=now;
}
inline bool recoveryContactRequiresSimulation(float gap,float closing,float dt){
    if(!std::isfinite(gap)||!std::isfinite(closing)||!std::isfinite(dt)||dt<=0)return false;
    // A resting/separating overlap is not an impact or a request to fall again.
    // Promote only when actual normal point motion closes on this manifold.
    // No new radius or force threshold is introduced.
    return closing>0&&gap<=closing*dt;
}
// BlendFromNM2117 vf5 0x82615070 / 0x8260F970. SimpleGetUp205's
// urgent/null acceptance is not the original NM handback contract.
inline bool recoveryResponseGivesWay(unsigned task){
    return task==2118||task==217||task==218||task==250||task==268;
}
inline bool recoveryBlendMayAbort(unsigned urgency,unsigned event,unsigned response8,unsigned response12){
    return urgency==2||event==120||recoveryResponseGivesWay(response8?response8:response12);
}
