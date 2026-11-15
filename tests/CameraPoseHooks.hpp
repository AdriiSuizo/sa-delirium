#pragma once

// Cover is an on-foot feature. Its final camera pose must remain available
// when drive-by shoulder controls are disabled or an unrelated input hook
// was refused. Chain the native camera independently of that optional set.
namespace camera_pose_hooks {
constexpr uintptr_t deriveAddress = 0x5150E0;
static void(__thiscall* nativeDerivedCamera)(void*,bool,bool) = nullptr;

static void __fastcall deriveCamera(void* camera, void*, bool mirror, bool oriented) {
    if (reinterpret_cast<uintptr_t>(camera) == drive_by_controls::mainCamera &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == 0x52C988) {
        if (drive_by_controls::installed) drive_by_controls::applyAimView(camera);
        player_cover::applyCamera(camera);
    }
    nativeDerivedCamera(camera, mirror, oriented);
}

static bool install(void* entry = reinterpret_cast<void*>(deriveAddress)) {
    constexpr unsigned char signature[] = {0x6a,0xff,0x68,0x78,0xc7,0x83,0,0x64};
    if (std::memcmp(entry, signature, sizeof(signature)) != 0 ||
        MH_CreateHook(entry, reinterpret_cast<void*>(&deriveCamera),
                      reinterpret_cast<void**>(&nativeDerivedCamera)) != MH_OK) return false;
    if (MH_QueueEnableHook(entry) == MH_OK) return true;
    MH_RemoveHook(entry);
    return false;
}
}
