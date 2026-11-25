#pragma once
#include "DamageOwnership.hpp"

namespace cj_recovery {
inline bool preserveOnDamage(int weapon, bool calculated, bool healthZero, bool forceDeath,
                             float currentHealth, bool ownsPhysicalTask) {
    // CEventHandler::ComputeDamageResponse also selects death for health < 1.
    return ownsPhysicalTask && firearmDamage(weapon) && calculated &&
           !healthZero && !forceDeath && currentHealth >= 1.f;
}
}
