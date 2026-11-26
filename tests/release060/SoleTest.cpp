#include "NpcGaitSole.hpp"
#include <cassert>
#include <cmath>
using namespace npc_gait;
int main(){
    // The two source cycles have independent timing. A different flight arc
    // or a missed sample must not be corrected as if the foot were planted.
    SoleCalibration state;
    for(unsigned frame=1;frame<=400;++frame){
        const unsigned t=frame*17;
        const bool flight=(frame%23u)>14u;
        if(t-state.lastSample>=32)
            state.observe(-1.f+((frame%21u)>13u?.14f:0.f),-.936f+(flight?.22f:0.f),1.f,t);
        state.advance(.017f);
        assert(std::isfinite(state.bias)&&std::abs(state.bias)<=.12f);
        if(frame>120)assert(std::abs(state.bias+.064f)<.0001f);
    }
    const float calibrated=state.bias;
    state.observe(std::numeric_limits<float>::infinity(),0,1,8000);
    state.advance(0);
    assert(state.bias==calibrated);
}
