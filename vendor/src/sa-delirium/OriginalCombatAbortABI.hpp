#pragma once
#include <cstdint>
// Original Damage9 task arbitration; original IDs only. main=UINT32_MAX none.
struct NmOriginalCombatDamageAbort {
    std::uint32_t size,urgency,ped;std::int32_t ped_kind;
    std::uint32_t ped_flags,locomotor,target,source,combat_flags,secondary,main,sub_kind,child_accepted;
    std::int32_t event_age;
};
struct NmOriginalCombatDamageAbortResult {
    std::uint32_t accepted,asked_child,dropped_main,combat_flags,ped_flags;std::int32_t event_age;
};
static_assert(sizeof(NmOriginalCombatDamageAbort)==56);
static_assert(sizeof(NmOriginalCombatDamageAbortResult)==24);
extern "C" int nm_original_combat_damage_abort(const NmOriginalCombatDamageAbort*,NmOriginalCombatDamageAbortResult*);
