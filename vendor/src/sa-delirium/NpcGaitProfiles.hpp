#pragma once

namespace npc_gait {

// SA 1.0 walkcycle groups from eAnimations.h / data/animgrp.dat.
enum WalkGroup {
    Man = 118, Shuffle, OldMan, GangWalk1, GangWalk2, OldFatMan, FatMan,
    Jogger, DrunkMan, BlindMan, Swat, Woman, Shopping, BusyWoman, SexyWoman,
    Pro, OldWoman, FatWoman, JogWoman, OldFatWoman, Skate
};

// Numbering from SA's ePedType and stock data/pedstats.dat respectively.
enum PedType {
    CivilianMale = 4, CivilianFemale = 5, Cop = 6, FirstGang = 7, LastGang = 16,
    Medic = 18, Fireman = 19, Bum = 21, Prostitute = 22
};
enum Stat {
    PoliceStat = 1, SuitMan = 15, OldMale = 18, SuitWoman = 21,
    OldFemale = 24, TrampMale = 26, Tourist = 28, OldShopper = 37
};

constexpr bool IsJoggerGroup(int group) {
    return group == Jogger || group == JogWoman;
}

// SA CKeyGen::GetUppercaseKey uses the uncomplemented CRC-32 remainder.
// Literals in switch labels are evaluated at compile time.
constexpr unsigned ModelKey(const char* name) {
    unsigned key = 0xffffffffu;
    while (*name) {
        unsigned char c = static_cast<unsigned char>(*name++);
        key ^= (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
        for (int bit = 0; bit < 8; ++bit)
            key = (key >> 1) ^ ((key & 1u) ? 0xedb88320u : 0u);
    }
    return key;
}

// The generic groups omit many body types. These civilian names were checked
// against their actual original and Vados meshes (tests/body_shapes.py).
// Uniforms, bodybuilders and the backpacker are intentionally absent.
constexpr bool HeavyCivilianModel(unsigned key) {
    switch (key) {
    case ModelKey("smoke"): case ModelKey("smokev"): case ModelKey("bb"):
    case ModelKey("wmoice"): case ModelKey("ballas2"):
    case ModelKey("smyst"): case ModelKey("smyst2"): case ModelKey("bmotr1"):
    case ModelKey("sbfost"): case ModelKey("swfost"):
    case ModelKey("cwfohb"): case ModelKey("dnfolc2"):
        return true;
    default:
        return false;
    }
}

// Stock peds named as older adults but assigned a generic walk and personality.
// Only consulted for generic groups. An explicit walk supplied by a model mod wins.
constexpr bool StockOlderCivilian(unsigned modelKey) {
    switch (modelKey) {
    case ModelKey("bfori"): case ModelKey("bmori"): case ModelKey("bmost"):
    case ModelKey("hfori"): case ModelKey("hmori"): case ModelKey("hmost"):
    case ModelKey("omori"): case ModelKey("omost"): case ModelKey("wmoprea"):
    case ModelKey("wmori"): case ModelKey("wmomib"): case ModelKey("bmochil"):
    case ModelKey("vbmocd"): case ModelKey("sbfori"): case ModelKey("sbfost"):
    case ModelKey("sbmocd"): case ModelKey("sbmori"): case ModelKey("sbmost"):
    case ModelKey("sofost"): case ModelKey("somobu"): case ModelKey("somori"):
    case ModelKey("somost"): case ModelKey("swfori"): case ModelKey("swfost"):
    case ModelKey("swmocd"): case ModelKey("swmori"): case ModelKey("swmost"):
    case ModelKey("bmocd"): case ModelKey("wmoice"):
        return true;
    default:
        return false;
    }
}

constexpr const char* SpecialSlotProfile(unsigned key) {
    switch (key) {
    case ModelKey("bbthin"):
        return "move_m@casual";
    case ModelKey("cat"): case ModelKey("kendl"):
        return "move_f@casual";
    case ModelKey("wfyclem"): case ModelKey("wfycllv"): case ModelKey("wfyclpd"):
        return "move_f@bness_a";
    case ModelKey("tenpen"): case ModelKey("pulaski"): case ModelKey("hern"):
    case ModelKey("csherna"): case ModelKey("dsherna"): case ModelKey("lapdna"):
    case ModelKey("lvpdna"): case ModelKey("sfpdna"):
        return "move_cop";
    case ModelKey("emmet"): case ModelKey("truth"):
        return "move_m@old_a";
    default:
        return nullptr;
    }
}

// Returns the IV movement dictionary, or nullptr to retain the game's own pose.
// Uses the model's loaded IDE metadata, including Mod Loader overrides. The runtime
// owns task eligibility and blend weights. This function is independent of animation time.
constexpr const char* ResolveProfile(int modelId, int group, int pedType, int stat,
                                    unsigned modelKey = 0) {
    if (modelId <= 0)
        return nullptr;

    const bool female = pedType == CivilianFemale || pedType == Prostitute ||
        (group >= Woman && group <= OldFatWoman);
    // SA special slots are declared male even for Kendl and Catalina, and slot
    // 299 always declares a heavy body. Streaming updates the actual name.
    if (modelId >= 290 && modelId <= 299 && (group == Man || group == FatMan)) {
        if (const char* special = SpecialSlotProfile(modelKey))
            return special;
    }
    if (group == FatMan || group == OldFatMan || group == FatWoman || group == OldFatWoman)
        return female ? "move_f@fat" : "move_m@fat";
    if (pedType == Cop || stat == PoliceStat) {
        // SA assigns the same walkcycle to patrol, sheriff, SWAT, FBI and army.
        return modelId == 285 || modelId == 286 || modelId == 287 ? "move_m@swat" : "move_cop";
    }
    if (pedType == Medic || pedType == Fireman)
        return "move_m@bness_a"; // IV M_Y_pmedic and M_Y_Fireman.
    if (group == Swat)
        return "move_m@swat";
    if ((group == Man || group == Woman || group == GangWalk2 || group == OldWoman)
        && HeavyCivilianModel(modelKey))
        return female ? "move_f@fat" : "move_m@fat";
    if (group == OldMan || group == OldWoman || stat == OldMale ||
        stat == OldFemale || stat == OldShopper)
        return female ? "move_f@old_a" : "move_m@old_a";
    if (group == GangWalk1 || group == GangWalk2)
        return group == GangWalk2 ? "move_gng@generic_b" : "move_gng@generic_a";
    if (group == SexyWoman || group == Pro)
        return "move_f@sexy";
    if (group == BusyWoman || group == Shopping)
        return "move_f@bness_a";
    if (!female && (pedType == Bum || stat == TrampMale))
        return "move_m@bum";
    if ((group == Man || group == Woman) && StockOlderCivilian(modelKey))
        return female ? "move_f@old_a" : "move_m@old_a";
    if (pedType >= FirstGang && pedType <= LastGang)
        return "move_gng@generic_a";
    if (stat == SuitMan || stat == SuitWoman)
        return female ? "move_f@bness_a" : "move_m@bness_a";
    if (stat == Tourist)
        return female ? "move_f@casual" : "move_m@casual_b";
    return female ? "move_f@casual" : "move_m@casual";
}

} // namespace npc_gait
