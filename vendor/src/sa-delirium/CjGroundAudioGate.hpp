#pragma once
#include <algorithm>
#include <cmath>

struct CjGroundAudioGate {
    bool hadContact=false, played=false;
    unsigned contactAt=0, playedAt=0;
    unsigned sequence=0;

    bool accepts(unsigned now,float impulse,float closing){
        // The solver aggregates upward-facing ground contacts over one step.
        // Treat a string of contacts from different limbs as one audible hit.
        if(!std::isfinite(impulse)||!std::isfinite(closing)||impulse<.22f||closing<2.f)return false;
        const bool onset=!hadContact||now-contactAt>=75;
        contactAt=now;
        hadContact=true;
        if(!onset||(played&&now-playedAt<125))return false;
        played=true;
        playedAt=now;
        ++sequence;
        return true;
    }

    static float strength(float impulse,float closing){
        const float speed=std::clamp((closing-2.f)/8.f,0.f,1.f);
        const float force=std::clamp((impulse-.22f)/2.f,0.f,1.f);
        return .60f*speed+.40f*force;
    }
};
