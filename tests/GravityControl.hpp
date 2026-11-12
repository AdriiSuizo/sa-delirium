#pragma once

namespace gravity_control {
constexpr uintptr_t gravityAddress = 0x542FE0;
constexpr uintptr_t gravityConstant = 0x863984;
constexpr float normalGravity = .008f;
constexpr float reducedScale = .01f;
constexpr float controlledScale = .5f;
constexpr WORD padChord = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
static void (__thiscall* nativeGravity)(void*) = nullptr;
static void (__cdecl* engineGravity)(int) = nullptr;
static void (__cdecl* agentGravity)(NmAgent*, int) = nullptr;
static bool enabled = true, held = false, fired = false;
static DWORD started = 0;

static bool controlled(void* physical) {
    auto* cj = player();
    if (!cj) return false;
    if (physical == cj) return true;
    return (at<unsigned char>(physical, 0x36) & 7u) == 2u && at<void*>(physical, 0x460) == cj;
}

static float scale(void* physical) {
    if (enabled) return 1.f;
    const unsigned type = at<unsigned char>(physical, 0x36) & 7u;
    if (type != 2u && type != 3u) return 1.f;
    return controlled(physical) ? controlledScale : reducedScale;
}

static void setEnabled(bool value, bool notify) {
    if (!engineGravity || enabled == value) return;
    engineGravity(value ? 0 : 1);
    enabled = value;
    for (auto& [ref, active] : agents)
        if (pedFromRef(ref) == active->ped) agentGravity(active->agent, active->ped == player());
    event("gravity_changed", -1, -1, value ? "normal" : "npc_vehicle_scale=0.01 player_scale=0.5");
    if (notify) {
        const bool spanish = at<unsigned char>(reinterpret_cast<void*>(0xBA6748), 0x84) == 4;
        const char* message = spanish ? (value ? "Gravedad normal" : "Gravedad: CJ 50%, trafico y peatones 1%")
                                      : (value ? "Normal gravity" : "Gravity: CJ 50%, traffic and peds 1%");
        reinterpret_cast<void(__cdecl*)(const char*,bool,bool,bool)>(0x588BE0)(message, true, false, false);
    }
}

static void sample(bool pressed, DWORD now) {
    if (!pressed) { held = fired = false; return; }
    if (!held) { started = now; held = true; }
    if (!fired && now - started >= 600) {
        fired = true;
        setEnabled(!enabled, true);
    }
}

static void tick() {
    if (!engineGravity) return;
    if (cutscene() || !player() || *reinterpret_cast<int*>(0xC8D4C0) != 9 ||
        (protectMissions && (scriptMissionActive() || onMission()))) {
        sample(false, 0);
        setEnabled(true, false);
        return;
    }
    XINPUT_STATE pad{};
    sample(playing() && vehicle_nitro::readForegroundPad(pad) &&
        (pad.Gamepad.wButtons & padChord) == padChord, GetTickCount());
}

static void __fastcall applyGravity(void* physical, void*) {
    const float gravityScale = scale(physical);
    if (gravityScale == 1.f) { nativeGravity(physical); return; }
    const float before = at<float>(physical, 0x4C);
    const bool infinite = (at<unsigned>(physical, 0x40) & 0x40u) != 0;
    const V oldSpin = infinite ? at<V>(physical, 0x50) : V{};
    nativeGravity(physical);
    // Scale the native gravity impulse for this entity. The shared constant
    // keeps its normal value for the other native callers.
    auto& vertical = at<float>(physical, 0x4C);
    vertical = before + (vertical - before) * gravityScale;
    if (infinite) {
        // 0x543014 uses its own negative constant and ApplyForce also turns the body.
        auto& spin = at<V>(physical, 0x50);
        spin = oldSpin + (spin - oldSpin) * gravityScale;
    }
}

static bool install(HMODULE engine) {
    auto set = reinterpret_cast<decltype(engineGravity)>(GetProcAddress(engine, "sa_delirium_set_moon_gravity"));
    auto* address = reinterpret_cast<void*>(gravityAddress);
    constexpr unsigned char entry[] = {0x83,0xEC,0x18,0x56,0x8B,0xF1,0x8B,0x46,0x40};
    auto perAgent = reinterpret_cast<decltype(agentGravity)>(GetProcAddress(engine, "nm_agent_set_lunar_player"));
    if (!set || !perAgent || *reinterpret_cast<const float*>(gravityConstant) != normalGravity ||
        std::memcmp(address, entry, sizeof(entry)) != 0 ||
        MH_CreateHook(address, reinterpret_cast<void*>(&applyGravity), reinterpret_cast<void**>(&nativeGravity)) != MH_OK)
        return false;
    if (MH_QueueEnableHook(address) != MH_OK) { MH_RemoveHook(address); return false; }
    engineGravity = set;
    agentGravity = perAgent;
    return true;
}
}
