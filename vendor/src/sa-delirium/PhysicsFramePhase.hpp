#pragma once
#include <cstdint>
// A native simulation tick can have many pedestrian PreRender callbacks, but
// exactly one physical island step after their current animation palettes.
struct PhysicsFramePhase {
    uint32_t frame=0,time=0;
    bool pending=false,consuming=false;
    uint32_t lastConsumed=0;
    bool hasConsumed=false;
    bool begin(uint32_t f,uint32_t t){
        if(pending||consuming||(hasConsumed&&f==lastConsumed))return false;
        frame=f;time=t;pending=true;return true;
    }
    bool consume(uint32_t f){
        if(!pending||consuming||f!=frame)return false;
        pending=false;consuming=true;lastConsumed=f;hasConsumed=true;return true;
    }
    void finish(){consuming=false;}
    // Retire a missing/invalid ticket, but never unlock a solver already on the
    // stack or make its frame eligible a second time. finish owns that unlock.
    void reset(){pending=false;}
    // Explicit counter-epoch reset, only while no solver owns the phase. Normal
    // unsigned counter wrap needs no reset; comparing equality handles it.
    bool resetEpoch(){
        if(consuming)return false;
        pending=false;hasConsumed=false;return true;
    }
};
