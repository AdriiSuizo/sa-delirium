#include "NpcStride.hpp"
#include <cstdio>
#include <stdexcept>

static void expect(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// A curve may finish more than once during a slow frame. Compare integrated
// travel with the same elapsed time split into small simulation steps.
static float travel(float dt, int steps, bool stopping) {
    npc_stride::Profile profile{};
    for (auto& curve : profile) {
        curve.duration = .025f;
        curve.distance = {0.f, .025f};
    }
    npc_stride::State state;
    state.clip = npc_stride::Walk;
    state.time = .0125f;
    float distance = 0;
    for (int i = 0; i < steps; ++i) {
        const auto result = npc_stride::advance(state, profile,
            {npc_stride::Walk, stopping, false, dt, 0, 0, 1, 1, 100, 1, 0});
        expect(result.applied, "valid stride rejected");
        expect(std::isfinite(result.speed) && result.speed >= 0, "invalid speed");
        distance += result.speed * dt;
    }
    return distance;
}

int main() {
    const float fine = travel(.005f, 20, false);
    const float coarse = travel(.1f, 1, false);
    expect(std::abs(fine - .1f) < 1.e-5f, "fine travel changed");
    expect(std::abs(coarse - fine) < 1.e-5f, "stride loses distance across multiple loop boundaries");
    // Stop clips are clamped, never repeated.
    expect(std::abs(travel(.1f, 1, true) - .025f) < 1.e-5f, "stop clip repeated");
    puts("PASS: gait distance is independent of step partition; stops do not loop");
}
