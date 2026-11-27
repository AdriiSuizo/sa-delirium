#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace startup_video_mode {

struct Mode {
    int width, height, depth;
    unsigned flags;
    int refreshRate, format;
};

static int __cdecl chooseAvailableMode(int width, int height) {
    if (width <= 0 || height <= 0) return -1;
    const auto count = reinterpret_cast<int (__cdecl*)()>(0x7F2CC0)();
    const auto query = reinterpret_cast<Mode* (__cdecl*)(Mode*, int)>(0x7F2CF0);
    const double aspect = static_cast<double>(width) / height;
    int selected = -1;
    double lowestCost = std::numeric_limits<double>::infinity();
    for (int index = 0; index < count; ++index) {
        Mode mode{};
        if (!query(&mode, index) || mode.width <= 0 || mode.height <= 0 || mode.depth <= 0) continue;
        const double modeAspect = static_cast<double>(mode.width) / mode.height;
        const double sizeError = std::abs(static_cast<double>(mode.width) / width - 1.0) +
                                 std::abs(static_cast<double>(mode.height) / height - 1.0);
        const double aspectError = std::abs(modeAspect / aspect - 1.0);
        const bool exclusive = (mode.flags & 1u) != 0;
        // Respect the selected adapter's list. Prefer a 32-bit exclusive mode
        // near the requested size, with windowed and lower-depth modes available
        // as fallbacks for adapters that list no such mode.
        const double cost = sizeError + aspectError * .5 +
            (mode.depth < 24 ? 1000.0 : 0.0) + (!exclusive ? 100.0 : 0.0);
        if (cost < lowestCost) {
            lowestCost = cost;
            selected = index;
        }
    }
    return selected;
}

// The selector reaches this block only after its exact mode search is exhausted.
// EBP/EBX carry the requested dimensions from the same search. Resume at the
// game's normal mode-commit path or replay the original dialog arguments.
static __declspec(naked) void recoverUnavailableMode() {
    __asm {
        push ebx
        push ebp
        call chooseAvailableMode
        add esp, 8
        test eax, eax
        js unavailable
        mov dword ptr ds:[0x8D6220], eax
        mov eax, 0x7462C5
        jmp eax
    unavailable:
        push 0
        push 0x86D388
        mov eax, 0x7463C7
        jmp eax
    }
}

static bool install() {
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != 0x400000) return false;
    constexpr std::uintptr_t address = 0x7463C0;
    constexpr unsigned char expected[] = {0x6A,0,0x68,0x88,0xD3,0x86,0};
    constexpr unsigned char commit[] = {0x8B,0x15,0x20,0x62,0x8D,0,0x52,0x8D,0x44,0x24,0x18,0x50};
    auto* site = reinterpret_cast<unsigned char*>(address);
    if (std::memcmp(site, expected, sizeof(expected)) ||
        std::memcmp(reinterpret_cast<void*>(0x7462C5), commit, sizeof(commit))) return false;

    DWORD protection{};
    if (!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &protection)) return false;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(&recoverUnavailableMode) - (address + 5));
    unsigned char patch[] = {0xE9,0,0,0,0,0x90,0x90};
    std::memcpy(patch + 1, &displacement, sizeof(displacement));
    std::memcpy(site, patch, sizeof(patch));
    DWORD unused{};
    VirtualProtect(site, sizeof(patch), protection, &unused);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    return true;
}

}
