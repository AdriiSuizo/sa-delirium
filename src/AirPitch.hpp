#pragma once

#include <algorithm>
#include <cmath>

namespace air_pitch {
struct Vector {
    float x, y, z;
};

inline float dot(Vector a, Vector b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Velocity uses SA's native 50 Hz units. Acceleration and limits use radians
// per second. The returned change is only around the vehicle's right axis.
inline float correction(Vector& angular, Vector right, float upright, unsigned contactWheels,
                        float throttle, float brake, float timeStep) {
    if (contactWheels || !std::isfinite(upright) || upright <= 0.25f ||
        !std::isfinite(timeStep) || timeStep <= 0.0f || timeStep > 5.0f ||
        !std::isfinite(throttle) || !std::isfinite(brake)) return 0.0f;

    const float lengthSquared = dot(right, right);
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.8f || lengthSquared > 1.2f) return 0.0f;
    const float scale = 1.0f / std::sqrt(lengthSquared);
    right = {right.x * scale, right.y * scale, right.z * scale};
    constexpr float updatesPerSecond = 50.0f;
    const float pitch = dot(angular, right) * updatesPerSecond;
    if (!std::isfinite(pitch)) return 0.0f;
    const float command = std::clamp(throttle, 0.0f, 1.0f) - std::clamp(brake, 0.0f, 1.0f);
    constexpr float acceleration = 0.85f;
    constexpr float rateLimit = 0.60f;
    const float step = command * acceleration * timeStep / updatesPerSecond;
    const float delta = std::clamp(step, std::min(0.0f, -rateLimit - pitch),
                                   std::max(0.0f, rateLimit - pitch)) / updatesPerSecond;
    if (delta == 0.0f) return 0.0f;
    angular.x += right.x * delta;
    angular.y += right.y * delta;
    angular.z += right.z * delta;
    return delta;
}
}
