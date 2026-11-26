#include "NpcGaitProfiles.hpp"
#include <iostream>
#include <string_view>

using namespace npc_gait;
using namespace std::literals;

static_assert(ResolveProfile(7, Man, CivilianMale, 16) == "move_m@casual"sv);
static_assert(ResolveProfile(13, Woman, CivilianFemale, 20) == "move_f@casual"sv);
static_assert(ResolveProfile(9, Woman, CivilianFemale, 42, ModelKey("BFORI")) == "move_f@old_a"sv);
static_assert(ResolveProfile(9, Woman, CivilianFemale, 42, ModelKey("REPLACEMENT")) == "move_f@casual"sv);
static_assert(ResolveProfile(32, Man, CivilianMale, OldMale) == "move_m@old_a"sv);
static_assert(ResolveProfile(31, OldFatWoman, CivilianFemale, OldFemale) == "move_f@fat"sv);
static_assert(ResolveProfile(299, FatMan, CivilianMale, 41) == "move_m@fat"sv);
static_assert(ResolveProfile(281, Swat, Cop, PoliceStat) == "move_cop"sv);
static_assert(ResolveProfile(285, Swat, Cop, PoliceStat) == "move_m@swat"sv);
static_assert(ResolveProfile(286, Swat, Cop, PoliceStat) == "move_m@swat"sv);
static_assert(ResolveProfile(287, Swat, Cop, PoliceStat) == "move_m@swat"sv);
static_assert(ResolveProfile(1000, Swat, CivilianMale, 16) == "move_m@swat"sv);
static_assert(ResolveProfile(280, FatMan, Cop, PoliceStat) == "move_m@fat"sv);
static_assert(ResolveProfile(274, Swat, Medic, 2) == "move_m@bness_a"sv);
static_assert(ResolveProfile(278, Swat, Fireman, 3) == "move_m@bness_a"sv);
static_assert(ResolveProfile(103, GangWalk2, FirstGang, 4) == "move_gng@generic_b"sv);
static_assert(ResolveProfile(125, Man, FirstGang + 5, 9) == "move_gng@generic_a"sv);
static_assert(ResolveProfile(9, BusyWoman, CivilianFemale, 42) == "move_f@bness_a"sv);
static_assert(ResolveProfile(0, Man, CivilianMale, 16) == nullptr);
// The stable all-peds revision resolves unfamiliar native groups through the
// model and demographic fallback; the host separately checks the active task.
static_assert(ResolveProfile(10, Woman, 1, 0) == "move_f@casual"sv);
static_assert(ResolveProfile(100, 54, CivilianMale, 14) == "move_m@casual"sv);
static_assert(ResolveProfile(99, Skate, CivilianMale, 40) == "move_m@casual"sv);
static_assert(ResolveProfile(99, BlindMan, CivilianMale, OldMale) == "move_m@old_a"sv);
static_assert(ResolveProfile(99, DrunkMan, CivilianMale, 14) == "move_m@casual"sv);
static_assert(IsJoggerGroup(Jogger) && IsJoggerGroup(JogWoman) && !IsJoggerGroup(Man));
static_assert(ResolveProfile(290, Man, CivilianMale, 41, ModelKey("smoke")) == "move_m@fat"sv);
static_assert(ResolveProfile(290, Man, CivilianMale, 41, ModelKey("bb")) == "move_m@fat"sv);
static_assert(ResolveProfile(290, Man, CivilianMale, 41, ModelKey("sweet")) == "move_m@casual"sv);
static_assert(ResolveProfile(299, FatMan, CivilianMale, 41, ModelKey("bbthin")) == "move_m@casual"sv);
static_assert(ResolveProfile(290, Man, CivilianMale, 41, ModelKey("kendl")) == "move_f@casual"sv);
static_assert(ResolveProfile(290, Man, CivilianMale, 41, ModelKey("tenpen")) == "move_cop"sv);
static_assert(ResolveProfile(241, Man, CivilianMale, 14, ModelKey("smyst")) == "move_m@fat"sv);
static_assert(ResolveProfile(264, Man, CivilianMale, 14, ModelKey("wmoice")) == "move_m@fat"sv);
static_assert(ResolveProfile(103, GangWalk2, FirstGang, 4, ModelKey("ballas2")) == "move_m@fat"sv);
static_assert(ResolveProfile(218, Woman, CivilianFemale, 20, ModelKey("sbfost")) == "move_f@fat"sv);
static_assert(ResolveProfile(26, Man, CivilianMale, 17, ModelKey("wmybp")) == "move_m@casual"sv);

int main() {
    int model, group, type, stat;
    unsigned key;
    while (std::cin >> model >> group >> type >> stat >> key) {
        auto profile = ResolveProfile(model, group, type, stat, key);
        std::cout << model << ' ' << (profile ? profile : "native") << '\n';
    }
}
