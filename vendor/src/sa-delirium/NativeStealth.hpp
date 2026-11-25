#pragma once

namespace native_stealth {
constexpr int knife = 4;
constexpr int stealthGroup = 87;
constexpr int victimDeath = 350;

inline bool preparationAllowed(bool targetHeld, float moveBlend, int moveState, bool secondaryAttack, bool blockLoaded) {
    return targetHeld && moveBlend < 1.9f && moveState != 7 && !secondaryAttack && blockLoaded;
}

inline bool processesKnifeInput(int activeTaskId) { return activeTaskId == 0; } // TASK_SIMPLE_PLAYER_ON_FOOT

inline bool activeDeathClip(int state, int group, int id, bool playing, float blend, float delta) {
    // The final frame can remain attached to a PEDSTATE_DEAD (55) indefinitely.
    return (state == 54 || state == 56) && group == stealthGroup && id == victimDeath && playing && (blend > 0.f || delta > 0.f);
}

inline void* partnerForRole(bool attackerRole, bool taskActorIsAttacker, int group, bool aborting, void* partner) {
    return attackerRole == taskActorIsAttacker && group == stealthGroup && !aborting ? partner : nullptr;
}

template<class Game> bool inSequenceWith(Game& game, void* ped) {
    void* cj = game.cj();
    if (!cj || !ped) return false;
    void* partner = game.pairedWith(cj, true);
    if (ped == cj) return partner != nullptr;
    return partner == ped || game.pairedWith(ped, false) == cj || game.finishing(ped);
}

template<class Game> bool ownsPoseWith(Game& game, void* ped) {
    if (inSequenceWith(game, ped)) return true;
    void* cj = game.cj();
    if (!cj || !ped) return false;
    return game.preparing(cj, ped == cj ? nullptr : ped) != nullptr;
}

template<class Game> bool ownsDamageWith(Game& game, void* victim, void* attacker, int weapon) {
    if (!victim || !attacker || attacker != game.cj() || attacker == victim || weapon != knife) return false;
    // Native damage is dispatched twice. The first event can precede the victim
    // task, and the fatal event can arrive after the player animation finishes.
    return game.pairedWith(attacker, true) == victim || game.pairedWith(victim, false) == attacker;
}
}

#ifndef NATIVE_STEALTH_POLICY_ONLY
static bool nativeStealthTargetHeld(void* pad);

namespace native_stealth {
struct SanAndreas {
    void* cj() const { return player(); }

    void* pairedWith(void* actor, bool attackerRole) const {
        void* task;
        if (attackerRole) {
            // ProcessPlayerWeapon installs this simple task directly in primary slot 3.
            void* manager = taskManager(actor);
            if (!manager) return nullptr;
            task = at<void*>(manager, 3 * sizeof(void*));
            if (!task || at<void*>(task, 0) != reinterpret_cast<void*>(0x86DA74)) return nullptr;
            // A higher-priority primary task may have interrupted this one.
            if (at<void*>(manager, 0) || at<void*>(manager, 4) || at<void*>(manager, 8)) return nullptr;
        } else {
            // Before the victim's first ProcessPed, the player's live task already
            // identifies them. Afterwards the event response may nest the victim task.
            const int state = at<int>(actor, 0x530);
            if (state != 56 && state != 54) return nullptr;
            void* manager = taskManager(actor);
            if (!manager) return nullptr;
            using FindTask = void* (__thiscall*)(void*, int);
            task = reinterpret_cast<FindTask>(0x681740)(manager, 1027);
            if (!task) return nullptr;
        }
        // CTaskSimpleStealthKill ctor 0x6225F0 registers and nulls the partner
        // reference on despawn. Its finish callback sets m_bIsAborting at +0x14.
        return partnerForRole(attackerRole, at<unsigned char>(task, 8) != 0, at<int>(task, 0x10),
                              at<unsigned char>(task, 0x14) != 0, at<void*>(task, 0x0C));
    }

    bool finishing(void* actor) const {
        const int state = at<int>(actor, 0x530);
        if (state != 54 && state != 56) return false;
        void* clump = at<void*>(actor, 0x18);
        if (!clump) return false;
        using FindAnimation = void* (__cdecl*)(void*, unsigned);
        void* anim = reinterpret_cast<FindAnimation>(0x4D68B0)(clump, victimDeath);
        return anim && activeDeathClip(state, at<unsigned short>(anim, 0x0E), at<short>(anim, 0x2C),
            (at<unsigned short>(anim, 0x2E) & 1) != 0, at<float>(anim, 0x18), at<float>(anim, 0x1C));
    }

    void* preparing(void* cj, void* specificTarget) const {
        if (inVehicle(cj) || at<float>(cj, 0x540) <= 0.f) return nullptr;
        const unsigned slot = at<unsigned char>(cj, 0x718);
        if (slot >= 13 || at<int>(cj, 0x5A0 + slot * 0x1C) != knife) return nullptr;

        // Same precedence as ProcessPlayerWeapon at 0x685C9A. A lock on a
        // non-ped does not fall through to the mouse target.
        void* target = at<void*>(cj, 0x71C);
        if (!target && *reinterpret_cast<unsigned char*>(0xB6EC2E)) target = at<void*>(cj, 0x79C);
        if (!target || target == cj || (specificTarget && specificTarget != target) || reference(target) < 0) return nullptr;
        if ((at<unsigned char>(target, 0x36) & 7) != 3 || !matrix(target) || !matrix(cj) || !at<void*>(target, 0x18) || !at<void*>(target, 0x47C)) return nullptr;

        void* intelligence = at<void*>(cj, 0x47C);
        void* data = at<void*>(cj, 0x480);
        if (!intelligence || !data || !nativeStealthTargetHeld(reinterpret_cast<void*>(0xB73458))) return nullptr;
        using WeaponInfo = void* (__cdecl*)(int, int);
        void* info = reinterpret_cast<WeaponInfo>(0x743C60)(knife, 1);
        if (!info || at<int>(info, 0) != 0 || at<int>(info, 0x1C) != stealthGroup) return nullptr;
        void* groups = *reinterpret_cast<void**>(0xB4EA34);
        void* block = groups ? at<void*>(groups, stealthGroup * 0x14) : nullptr;
        void* manager = taskManager(cj);
        if (!preparationAllowed(true, at<float>(data, 0x14), at<int>(cj, 0x534),
                                at<void*>(manager, 5 * sizeof(void*)) != nullptr, block && at<unsigned char>(block, 0x10))) return nullptr;
        // Only the active on-foot primary task processes a melee weapon (0x6888FE).
        // The other caller is the jetpack and explicitly requires an instant-hit gun.
        using ActiveLeaf = void* (__thiscall*)(void*);
        void* leaf = reinterpret_cast<ActiveLeaf>(0x6819D0)(manager);
        if (!leaf) return nullptr;
        using TaskType = int (__thiscall*)(void*);
        if (!processesKnifeInput(reinterpret_cast<TaskType>(at<void**>(leaf, 0)[4])(leaf))) return nullptr;
        // TestForStealthKill passes update=false to GetBonePosition (0x601E21).
        // Its head query reads a palette or a rest offset; it never reevaluates it.
        // Native false-mode checks posture, range, direction and target awareness.
        // CJ's standing and duck branches share this gate, including the case where
        // the duck task supplies the first association instead of KILL_PARTIAL (347).
        using CheckTarget = bool (__thiscall*)(void*, void*, bool);
        return reinterpret_cast<CheckTarget>(0x601E00)(intelligence, target, false) ? target : nullptr;
    }
};

inline bool ownsPose(void* ped) { SanAndreas game; return ownsPoseWith(game, ped); }
inline bool inSequence(void* ped) { SanAndreas game; return inSequenceWith(game, ped); }
inline bool ownsDamage(void* victim, void* attacker, int weapon) { SanAndreas game; return ownsDamageWith(game, victim, attacker, weapon); }
}
#endif
