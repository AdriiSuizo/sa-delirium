#pragma once

#include <algorithm>

namespace cj_recovery {
// Keep a shorter configured delay, including zero for a finished reaction task.
constexpr float responseSeconds(bool player, float configured) {
    return player ? std::min(configured, 0.50f) : configured;
}

constexpr float stableSeconds(bool player, float configured) {
    return player ? std::min(configured, 0.18f) : configured;
}

constexpr float blendSeconds(bool player, float configured) {
    return player ? std::min(configured, 0.25f) : configured;
}

constexpr float clipRate(bool player) {
    return player ? 1.35f : 1.f;
}
}
