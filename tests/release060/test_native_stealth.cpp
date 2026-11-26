// Exercise the actual ownership policy with game queries supplied by a fixture.
#define NATIVE_STEALTH_POLICY_ONLY
#include "NativeStealth.hpp"
#include <cstdio>

namespace ns = native_stealth;

struct Ped {};

struct Game {
    Ped player{}, target{}, bystander{};
    void* aim{};
    bool targetHeld{true}, secondaryAttack{}, blockLoaded{true}, viable{true};
    float speed{};
    int moveState{1};
    void* attackerTaskTarget{};
    void* victimTaskPartner{};
    bool finalClip{};

    void* cj() { return &player; }
    void* pairedWith(void* actor, bool attackerRole) {
        if (attackerRole && actor == &player) return attackerTaskTarget;
        if (!attackerRole && actor == &target) return victimTaskPartner;
        return nullptr;
    }
    bool finishing(void* actor) { return finalClip && actor == &target; }
    void* preparing(void* actor, void* specific) {
        if (actor != &player || !viable || (specific && specific != aim)) return nullptr;
        return ns::preparationAllowed(targetHeld, speed, moveState, secondaryAttack, blockLoaded)
            ? aim : nullptr;
    }
};

int checks{};
int failures{};
void expect(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

void testPreparation() {
    Game g;
    g.aim = &g.target;
    expect(ns::ownsPoseWith(g, &g.player), "eligible approach, CJ");
    expect(ns::ownsPoseWith(g, &g.target), "eligible approach, current target");
    expect(!ns::ownsPoseWith(g, &g.bystander), "bystander keeps locomotion");
    expect(!ns::inSequenceWith(g, &g.player), "preparation is not a paired task");
    expect(!ns::ownsDamageWith(g, &g.target, &g.player, 4), "aim alone cannot own damage");
    g.aim = &g.bystander;
    expect(!ns::ownsPoseWith(g, &g.target), "switching aim releases old target immediately");
    g.viable = false;
    expect(!ns::ownsPoseWith(g, &g.player), "failed native eligibility releases CJ");
    g.viable = true;
    g.targetHeld = false;
    expect(!ns::ownsPoseWith(g, &g.player), "releasing aim restores locomotion");
}

void testPair() {
    Game g;
    g.attackerTaskTarget = &g.target;
    expect(ns::inSequenceWith(g, &g.player), "attacker task is sufficient for CJ");
    expect(ns::inSequenceWith(g, &g.target), "attacker task identifies victim");
    expect(ns::ownsDamageWith(g, &g.target, &g.player, 4), "paired knife damage stays native");
    expect(!ns::ownsDamageWith(g, &g.target, &g.player, 3), "other weapon keeps normal damage");
    expect(!ns::ownsDamageWith(g, &g.bystander, &g.player, 4), "unrelated victim");
    expect(!ns::ownsDamageWith(g, &g.target, &g.bystander, 4), "unrelated attacker");
    g.victimTaskPartner = &g.player;
    g.attackerTaskTarget = nullptr;
    expect(ns::inSequenceWith(g, &g.target), "victim retains ownership during task handoff");
    expect(ns::ownsDamageWith(g, &g.target, &g.player, 4), "victim task retains the paired hit");
    g.victimTaskPartner = nullptr;
    g.finalClip = true;
    expect(ns::ownsPoseWith(g, &g.target), "native dying clip after tasks exit");
    expect(!ns::ownsPoseWith(g, &g.player), "finished attacker recovers locomotion immediately");
    expect(!ns::ownsDamageWith(g, &g.target, &g.player, 4), "final clip does not suppress new hits");
    g.finalClip = false;
    expect(!ns::ownsPoseWith(g, &g.target), "release victim when native animation ends");
    expect(!ns::inSequenceWith(g, nullptr), "null actor");
}

int main() {
    testPreparation();
    testPair();
    Ped partner;
    expect(ns::partnerForRole(true, true, 87, false, &partner) == &partner, "attacker role");
    expect(ns::partnerForRole(false, false, 87, false, &partner) == &partner, "victim role");
    expect(!ns::partnerForRole(true, false, 87, false, &partner), "wrong actor role");
    expect(!ns::partnerForRole(true, true, 86, false, &partner), "different animation group");
    expect(!ns::partnerForRole(false, false, 87, true, &partner), "aborting victim");
    expect(!ns::partnerForRole(false, false, 87, false, nullptr), "removed partner");
    expect(ns::processesKnifeInput(0), "active player on foot");
    expect(!ns::processesKnifeInput(-1), "missing player task");
    expect(!ns::processesKnifeInput(211), "unrelated active task");
    expect(ns::preparationAllowed(true, 1.89f, 3, false, true), "native approach below 1.9");
    expect(!ns::preparationAllowed(true, 1.9f, 3, false, true), "native speed boundary at 1.9");
    expect(!ns::preparationAllowed(true, 0.0f, 7, false, true), "sprint");
    expect(!ns::preparationAllowed(true, 0.0f, 1, true, true), "occupied attack slot");
    expect(!ns::preparationAllowed(true, 0.0f, 1, false, false), "animation block unavailable");
    expect(ns::activeDeathClip(54, 87, 350, true, 1.f, 0.f), "recorded dying state 54");
    expect(ns::activeDeathClip(56, 87, 350, true, 1.f, 0.f), "native stealth death state 56");
    expect(!ns::activeDeathClip(55, 87, 350, true, 1.f, 0.f), "recorded corpse with residual 350");
    expect(!ns::activeDeathClip(54, 87, 350, false, 1.f, 0.f), "finished association");
    expect(!ns::activeDeathClip(54, 87, 349, true, 1.f, 0.f), "victim damage is not final clip");
    std::printf("%d/%d ownership checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
