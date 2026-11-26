#include "CjHighFall.hpp"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>

using cj_high_fall::Tracker;
int checks = 0;

static void expect(bool result, const char* reason) {
    ++checks;
    if (!result) throw std::runtime_error(reason);
}

// In SA, moving-speed units are metres per native 1/50-second tick. The
// first few frames of a running jump may still be above the takeoff platform.
static float airbornePath(float ledge, float launch, int fps, bool knownStart, bool floorAvailable, bool previousGates=false) {
    constexpr float base = 50.f;
    constexpr float rootAboveFeet = .98f;
    constexpr float gravity = 20.f;
    Tracker tracker{};
    if (knownStart) tracker.grounded(base + ledge);
    const float departureRoot = base + ledge + rootAboveFeet;
    for (int frame=0; frame<fps*8; ++frame) {
        const float time = float(frame)/fps;
        const float root = departureRoot + launch*time - .5f*gravity*time*time;
        const float speed = (launch-gravity*time)/50.f;
        if (root < base+rootAboveFeet) break;
        tracker.airborne(root);
        const float support = time < .12f ? base + ledge : base;
        if (!floorAvailable || root<support) continue;
        // Earlier PlayerHighFall.hpp compared the current root directly with
        // the ray hit and knew only the ray's 9.5-metre reach.
        if (previousGates ? (root-support<=9.5f && root-support>=2.7f && speed<=-.12f)
                          : tracker.shouldStart(root,support,1.f,speed)) return time;
    }
    return -1;
}

static void trajectories() {
    for (int fps : {30,60,120}) {
        expect(airbornePath(3.f,0.f,fps,true,true,true)>0,"old three-metre step-down not reproduced");
        for (float launch : {0.f,5.2f,8.f}) {
            for (float ledge : {0.f,.8f,2.f,3.f,4.49f})
                expect(airbornePath(ledge,launch,fps,true,true)<0,"short drop or jump apex incorrectly triggers");
            for (float ledge : {4.5f,6.f,15.f}) {
                const float first = airbornePath(ledge,launch,fps,true,true);
                expect(first>0, "verified high ledge never triggered");
                expect(launch-20.f*first <= -5.999f, "trigger before sufficient descent");
            }
            expect(airbornePath(15.f,launch,fps,true,false)<0,"missing support");
        }
        expect(airbornePath(3.f,8.f,fps,false,true)<0,"spawned midair, jump apex faked a tall drop");
        expect(airbornePath(15.f,0.f,fps,false,true)>0,"untracked high fall never became provable");
    }
}

static void transitions() {
    Tracker t{};
    t.grounded(5.f);
    t.airborne(6.9f);
    expect(t.shouldStart(5.f,0.f,.65f,-.12f),"valid exact speed/slope bounds");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    expect(!t.shouldStart(nan,0.f,1.f,-.2f),"invalid root");
    expect(!t.shouldStart(5.f,nan,1.f,-.2f),"invalid surface");
    expect(!t.shouldStart(5.f,0.f,nan,-.2f),"invalid surface normal");
    expect(!t.shouldStart(5.f,0.f,1.f,nan),"invalid velocity");
    expect(!t.shouldStart(5.f,0.f,1.f,-.2f,nan),"invalid threshold");
    expect(!t.shouldStart(5.f,0.f,1.f,-.2f,-1.f),"negative threshold");
    expect(!t.shouldStart(9.51f,0.f,1.f,-.2f),"support beyond ray range");
    expect(!t.shouldStart(5.f,0.f,.649f,-.13f),"steep surface");
    expect(!t.shouldStart(5.f,0.f,1.f,-.119f),"descent too slow");
    expect(!t.shouldStart(5.f,0.f,1.f,.3f),"ascending");
    expect(!t.shouldStart(2.69f,0.f,1.f,-.3f),"landing already in reach");
    expect(!t.shouldStart(4.9f,2.f,1.f,-.3f),"lower roof is first landing");
    expect(!t.shouldStart(5.f,0.f,1.f,-.2f,6.f),"configured threshold above this drop");
    expect(t.shouldStart(5.f,0.f,1.f,-.2f,5.f),"configured threshold equals this drop");
    t.grounded(2.f);
    t.airborne(3.3f);
    expect(!t.shouldStart(3.f,0.f,1.f,-.14f),"old roof reused on a later short jump");
    t.reset();
    t.airborne(10.f);
    t.airborne(13.f);
    t.airborne(8.f);
    expect(!t.shouldStart(8.f,0.f,1.f,-.2f),"apex reused without a departure surface");
    t.airborne(5.5f);
    expect(t.shouldStart(5.5f,0.f,1.f,-.2f),"fallback after genuine 4.5 m descent");
    t.reset();
    t.airborne(5.f);
    expect(!t.shouldStart(5.f,0.f,1.f,-.2f),"reset did not discard the earlier fall");
}

int main() try {
    trajectories();
    transitions();
    std::printf("%d fall-classification checks passed\n",checks);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL after %d checks: %s\n",checks,e.what());
    return 1;
}
