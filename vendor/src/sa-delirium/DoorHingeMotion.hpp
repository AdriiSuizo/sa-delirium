#pragma once
#include <cmath>
#include <cstdint>

// CDoor::m_fPrevAngle is the previous *Open()* target, not the angle of the
// preceding simulation frame. CDoor::Process() changes m_fAngle without
// updating that field. Differentiate our consecutive native samples instead.
struct DoorHingeMotion {
    struct Sample {
        unsigned frame=0,time=0;
        float angle=0;
        bool valid=false;
    } previous{},current{};
    std::uintptr_t clump=0,doorFrame=0;
    unsigned axis=0;
    struct Result {float speed=0;bool verified=false;};

    Result sample(std::uintptr_t newClump,std::uintptr_t newDoorFrame,unsigned newAxis,
                  unsigned frame,unsigned time,float angle,float nativeDt){
        const bool identity=clump==newClump&&doorFrame==newDoorFrame&&axis==newAxis;
        if(!identity||!std::isfinite(angle)||!newClump||!newDoorFrame){
            previous={};current={};clump=newClump;doorFrame=newDoorFrame;axis=newAxis;
        }
        if(!std::isfinite(angle)||!newClump||!newDoorFrame)return {};
        if(!current.valid||frame!=current.frame||time!=current.time){
            // Frame counters may wrap. Game-time reversal or a skipped native
            // frame cannot be paired with only today's simulation timestep.
            previous=current.valid&&frame-current.frame==1&&time>=current.time?current:Sample{};
        }
        current={frame,time,angle,true};
        // Repeated world queries within a native frame retain its predecessor.
        // If that frame's door was evaluated later, recompute against the same
        // predecessor; never differentiate two phases using a full-frame dt.
        if(!previous.valid||!(nativeDt>0)||!std::isfinite(nativeDt))return {};
        // Native hinged doors do not wrap at +/-pi; preserving signed angular
        // displacement also preserves actual rebound direction at the stop.
        const float speed=(angle-previous.angle)/nativeDt;
        return std::isfinite(speed)?Result{speed,true}:Result{};
    }
};
