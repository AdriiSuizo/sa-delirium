#pragma once
#include <cmath>
inline bool nativeOnFootControl(unsigned char airFlags){return (airFlags&6u)==0;}
// CTimer::ms_fTimeStep is measured in 1/50 s and clipped independently of
// m_snTimeInMilliseconds (CPed::UpdatePosition / CTimer::UpdateVariables).
inline float combatSimulationSeconds(float nativeStep){
    return std::isfinite(nativeStep)&&nativeStep>0.f&&nativeStep<=5.f?nativeStep*.02f:0.f;
}
inline float combatMotionScale(float authoredSeconds,float nativeStep){
    return combatSimulationSeconds(nativeStep)>0.f&&std::isfinite(authoredSeconds)&&authoredSeconds>=0.f?authoredSeconds/nativeStep:0.f;
}
