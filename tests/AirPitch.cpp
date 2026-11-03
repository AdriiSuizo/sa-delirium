#include <cassert>
#include <cmath>
#include <limits>
#include "AirPitch.hpp"

using air_pitch::Vector;
using air_pitch::correction;

static bool near(float a, float b) {
    return std::fabs(a - b) < 0.00001f;
}

int main() {
    constexpr Vector right{1.0f, 0.0f, 0.0f};
    constexpr Vector initial{0.0f, 0.03f, -0.02f};
    Vector angular = initial;
    for (unsigned contact = 1; contact <= 4; ++contact) {
        assert(correction(angular, right, 1.0f, contact, 1.0f, 0.0f, 1.0f) == 0.0f);
    }
    assert(correction(angular, right, 1.0f, 0, 0.0f, 0.0f, 1.0f) == 0.0f);
    assert(correction(angular, right, 1.0f, 0, 1.0f, 1.0f, 1.0f) == 0.0f);
    assert(correction(angular, right, -1.0f, 0, 1.0f, 0.0f, 1.0f) == 0.0f);
    assert(correction(angular, {}, 1.0f, 0, 1.0f, 0.0f, 1.0f) == 0.0f);
    assert(correction(angular, right, 1.0f, 0, 1.0f, 0.0f, 0.0f) == 0.0f);
    assert(correction(angular, right, 1.0f, 0, 1.0f, 0.0f, 6.0f) == 0.0f);
    assert(correction(angular, right, 1.0f, 0, std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f) == 0.0f);
    assert(angular.x == initial.x && angular.y == initial.y && angular.z == initial.z);

    assert(correction(angular, right, 1.0f, 0, 1.0f, 0.0f, 1.0f) > 0.0f);
    assert(correction(angular, right, 1.0f, 0, 0.0f, 1.0f, 1.0f) < 0.0f);
    assert(near(angular.x, initial.x) && angular.y == initial.y && angular.z == initial.z);

    Vector rotated{};
    const Vector diagonal{0.8f, 0.6f, 0.0f};
    const float delta = correction(rotated, diagonal, 1.0f, 0, 0.5f, 0.0f, 1.0f);
    assert(delta > 0.0f);
    assert(near(air_pitch::dot(rotated, {0.6f, -0.8f, 0.0f}), 0.0f));
    assert(near(air_pitch::dot(rotated, diagonal), delta));
    assert(rotated.z == 0.0f);

    Vector quick{}, slow{};
    for (int i = 0; i < 24; ++i) correction(quick, right, 1.0f, 0, 1.0f, 0.0f, 0.25f);
    for (int i = 0; i < 6; ++i) correction(slow, right, 1.0f, 0, 1.0f, 0.0f, 1.0f);
    assert(near(quick.x, slow.x));

    for (int i = 0; i < 100; ++i) correction(quick, right, 1.0f, 0, 1.0f, 0.0f, 1.0f);
    assert(near(quick.x * 50.0f, 0.60f));
    quick.x = 0.03f;
    assert(correction(quick, right, 1.0f, 0, 1.0f, 0.0f, 1.0f) == 0.0f);
    assert(quick.x == 0.03f);
    assert(correction(quick, right, 1.0f, 0, 0.0f, 1.0f, 1.0f) < 0.0f);
}
