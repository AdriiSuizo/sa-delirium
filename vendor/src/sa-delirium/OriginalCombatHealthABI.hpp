#pragma once
#include <cstdint>
#include <cstddef>
// Source snapshots, no SA task IDs or health units. Every boolean is explicit.
struct NmCombatHealthRequest {
    std::uint32_t version,source,victim,now,bone,kind,flags;
    float amount;
};
struct NmCombatHealthFacts {
    std::uint64_t generation,birth,revision,source_generation,source_birth;
    float health,maximum,armour,factor;
    std::uint32_t proof_flags,proof_group,victim_flags,source_group,relationship_flags,complete;
    std::int32_t decision_index;std::uint32_t decision_flags;
};
struct NmCombatHealthResult {
    float health,maximum,armour,lost,absorbed;
    std::uint32_t outcome_flags,rng_before,rng_after,selection_flags,action,result;
    std::int32_t decision_response;std::uint32_t queue_flags;
};
using NmCombatHealthCallback=int(__cdecl*)(void*,std::uint32_t,const NmCombatHealthRequest*,NmCombatHealthFacts*,const NmCombatHealthResult*);
struct NmCombatHealthHost {std::uint32_t version;void*context;NmCombatHealthCallback callback;};
// Request flags: sourcePlayer1,victimPlayer2,currentSourceIsABlock4.
// Victim flags bit0..16: player,inVehicle,population,hat,swat,vest,ragdolling,
// shootableInVehicle,hurtable,fireProof,mission,diesWhenInjured,deepWater,
// noCritical,drowningOK,weapon58OK,bladesOK.
// Relationship flags: friend1,sameGroup2,sourceFloor4,sourceOwnedByVictim8.
// source_group=UINT32_MAX means no source relationship group.
// decision_flags: mapped PED/group index1,hostileToSource2,networkRefused4.
// queue_flags: decisionMapped1,trial1200=2,trial1700=4,trial300=8,networkRefused16.
// Result selection: valid1,sourceNM2,pendingMeleeRequest4; hashes0 mean absent.
// Callback phase0 prepares lifetime/revision facts;phase1 commits all reserved
// source response/accounting storage;phase2 cancels. 0 refuses without mutation,
// 1 succeeds; negative fails the frame. No reentry into the same NmCombat.
static_assert(sizeof(NmCombatHealthRequest)==32);
static_assert(sizeof(NmCombatHealthFacts)==88);
static_assert(sizeof(NmCombatHealthResult)==52);
static_assert(offsetof(NmCombatHealthFacts,health)==40);
static_assert(offsetof(NmCombatHealthResult,selection_flags)==32);
