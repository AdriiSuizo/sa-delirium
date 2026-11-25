#pragma once
#include <cmath>
#include <optional>

namespace cj_high_fall {
constexpr float defaultDrop = 4.5f;
constexpr float probeDepth = 9.5f;

struct Tracker {
    std::optional<float> takeoffSurface;
    std::optional<float> firstAirRoot;

    void reset() { takeoffSurface.reset(); firstAirRoot.reset(); }
    void grounded(float surfaceZ) {
        reset();
        if (std::isfinite(surfaceZ)) takeoffSurface = surfaceZ;
    }
    void airborne(float rootZ) {
        if (!firstAirRoot && std::isfinite(rootZ)) firstAirRoot = rootZ;
    }
    bool shouldStart(float rootZ, float supportZ, float normalZ, float verticalSpeedSA,
                     float minimumDrop = defaultDrop) const {
        if (!firstAirRoot || !std::isfinite(rootZ) || !std::isfinite(supportZ) ||
            !std::isfinite(normalZ) || !std::isfinite(verticalSpeedSA) ||
            !std::isfinite(minimumDrop) || minimumDrop < 0.f) return false;
        const float clearance = rootZ - supportZ;
        // If observation began mid-air, use only descent actually witnessed.
        const float drop = takeoffSurface ? *takeoffSurface - supportZ : *firstAirRoot - rootZ;
        return drop >= minimumDrop && verticalSpeedSA <= -.12f && normalZ >= .65f &&
               clearance >= 2.7f && clearance <= probeDepth;
    }
};
}
