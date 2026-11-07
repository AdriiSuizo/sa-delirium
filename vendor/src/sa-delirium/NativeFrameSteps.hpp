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
    // fastest: the quickest physical part (m/s). A body thrown from a car at
    // 33 m/s moved 0.31 m per 9.4 ms step, past its 0.1-0.15 m capsules, and
    // CJ fell through the road/kerb seam (06/11). At most 0.1 m per step, <=8.
    explicit NativeFrameSteps(float dt,float fastest=0,int limit=8) {
        if(std::isfinite(dt)&&dt>0&&dt<=.25f){
            // Nothing fast: one step up to 22 ms. At 60 a frame just over 16.7 ms took two steps, the two steps
            // made the next frame slower, and the game fell from 140 to 55 fps and stayed there (07/11).
            duration=dt;count=std::max(1,int(std::ceil(double(dt)*(fastest>0?60.0:45.0)-1.e-6)));
            if(std::isfinite(fastest)&&fastest>0)count=std::max(count,std::min(limit,int(std::ceil(double(fastest)*dt/.1))));
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
