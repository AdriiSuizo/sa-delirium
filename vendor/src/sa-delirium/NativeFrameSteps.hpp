#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Complete the host's simulated interval before publishing its final pose.
// A residual fixed-step accumulator renders NM and the car at different times.
// The original NM API accepts variable dt and subdivides long intervals; keep
// the adapter's <=60 Hz maximum step without throwing away a short final step.
struct NativeFrameSteps {
    float duration=0;
    int count=0;
    explicit NativeFrameSteps(float dt) {
        if(std::isfinite(dt)&&dt>0&&dt<=.25f){
            duration=dt;count=std::max(1,int(std::ceil(double(dt)*60.0-1.e-6)));
        }
    }
    float remaining(int index) const {return index>=count?0.f:duration-float(double(duration)*index/count);}
    float step(int index) const {return remaining(index)-remaining(index+1);}
};

// An end-of-frame pose cannot be advanced over that already elapsed frame.
// A verified older source can consume this frame; its older intra-frame
// remainder still needs separate provenance, never a guessed replay duration.
inline bool physicalAdmissionCanStep(uint32_t admitted,uint32_t current,bool verifiedOlderSource){
    return admitted!=current||verifiedOlderSource;
}
