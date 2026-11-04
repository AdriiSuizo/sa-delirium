#pragma once
#include <cstdint>
#include <cmath>
// An authored clip pose is produced before native movement, then finalized at
// the native end root. Differentiate only consecutive completed intervals.
struct CombatPoseHistory {
    unsigned frame=0,previousFrame=0;uint64_t serial=0,previousSerial=0;
    bool primed=false,completed=false,previousCompleted=false;
    void begin(unsigned currentFrame,uint64_t nextSerial){
        previousFrame=frame;previousSerial=serial;previousCompleted=primed&&completed;
        frame=currentFrame;serial=nextSerial;primed=true;completed=false;
    }
    bool finish(unsigned currentFrame){
        if(!primed||completed||frame!=currentFrame)return false;
        completed=true;return true;
    }
    bool differentiable(unsigned currentFrame,float seconds)const{
        return primed&&completed&&previousCompleted&&frame==currentFrame&&
            unsigned(frame-previousFrame)==1u&&previousSerial!=0&&serial>previousSerial&&
            std::isfinite(seconds)&&seconds>0;
    }
};
