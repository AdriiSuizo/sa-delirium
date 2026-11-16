#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <windows.h>

#include "CodePatch.hpp"

namespace blood_decals {
struct Site {
    uintptr_t address;
    std::array<unsigned char, 5> expected;
};

inline constexpr Site kStaticBloodSite{0x630E63u, {0xE8u, 0x98u, 0xABu, 0x0Du, 0x00u}};
inline constexpr Site kPermanentBloodSite{0x630EA2u, {0xE8u, 0xB9u, 0x60u, 0x0Du, 0x00u}};

struct Anchor {
    V pos{};
    float lastSize = 0.f;
    unsigned lastTimeMs = 0u;
};

inline std::mutex anchorMutex;
inline std::unordered_map<uintptr_t, Anchor> anchors;
inline uintptr_t currentTaskEdi = 0u;
inline bool installed = false;

using StoreStaticShadowFn = bool(__cdecl*)(
    unsigned int id, unsigned char type, void* texture, V* posn,
    float frontX, float frontY, float sideX, float sideY,
    short intensity, unsigned char red, unsigned char green, unsigned char blue,
    float zDistance, float scale, float drawDistance, bool temporaryShadow, float upDistance);

using AddPermanentShadowFn = void(__cdecl*)(
    unsigned char type, void* texture, V* posn,
    float topX, float topY, float rightX, float rightY,
    short intensity, unsigned char red, unsigned char green, unsigned char blue,
    float drawDistance, unsigned int time, float upDistance);

inline StoreStaticShadowFn origStoreStaticShadow = reinterpret_cast<StoreStaticShadowFn>(0x70BA00u);
inline AddPermanentShadowFn origAddPermanentShadow = reinterpret_cast<AddPermanentShadowFn>(0x706F60u);

static V anchorStaticPosition(uintptr_t taskPtr, const V& livePos, float frontX, float sideY) {
    if (!std::isfinite(livePos.x) || !std::isfinite(livePos.y) || !std::isfinite(livePos.z)) return livePos;
    const unsigned nowMs = *reinterpret_cast<unsigned*>(0xB7CB84u);
    const float size = std::max(std::abs(frontX), std::abs(sideY));
    std::lock_guard<std::mutex> lock(anchorMutex);
    if (anchors.size() > 64u) {
        for (auto it = anchors.begin(); it != anchors.end();) {
            if (nowMs - it->second.lastTimeMs > 7000u) it = anchors.erase(it);
            else ++it;
        }
    }
    auto it = anchors.find(taskPtr);
    const bool expired = it == anchors.end() ||
                         (nowMs - it->second.lastTimeMs > 300u) ||
                         (size + 1e-4f < it->second.lastSize * 0.75f);
    if (expired) {
        Anchor entry{livePos, size, nowMs};
        anchors[taskPtr] = entry;
        return livePos;
    }
    it->second.lastSize = std::max(it->second.lastSize, size);
    it->second.lastTimeMs = nowMs;
    return it->second.pos;
}

static bool consumePermanentPosition(uintptr_t taskPtr, V& outPos) {
    const unsigned nowMs = *reinterpret_cast<unsigned*>(0xB7CB84u);
    std::lock_guard<std::mutex> lock(anchorMutex);
    const auto it = anchors.find(taskPtr);
    if (it == anchors.end()) return false;
    const bool fresh = (nowMs - it->second.lastTimeMs) < 1500u;
    if (fresh) outPos = it->second.pos;
    anchors.erase(it);
    return fresh;
}

static bool __cdecl hookStoreStaticShadow(
    unsigned int id, unsigned char type, void* texture, V* posn,
    float frontX, float frontY, float sideX, float sideY,
    short intensity, unsigned char red, unsigned char green, unsigned char blue,
    float zDistance, float scale, float drawDistance, bool temporaryShadow, float upDistance) {
    V anchoredPos{};
    V* passPos = posn;
    if (posn && id > 0x11u) {
        const uintptr_t taskPtr = uintptr_t(id) - 0x11u;
        anchoredPos = anchorStaticPosition(taskPtr, *posn, frontX, sideY);
        passPos = &anchoredPos;
    }
    return origStoreStaticShadow(id, type, texture, passPos, frontX, frontY, sideX, sideY,
                                 intensity, red, green, blue, zDistance, scale, drawDistance, temporaryShadow, upDistance);
}

static void __cdecl hookAddPermanentShadow(
    unsigned char type, void* texture, V* posn,
    float topX, float topY, float rightX, float rightY,
    short intensity, unsigned char red, unsigned char green, unsigned char blue,
    float drawDistance, unsigned int time, float upDistance) {
    const uintptr_t taskPtr = currentTaskEdi;
    V anchoredPos{};
    V* passPos = posn;
    if (posn && taskPtr != 0u && consumePermanentPosition(taskPtr, anchoredPos)) {
        passPos = &anchoredPos;
    }
    origAddPermanentShadow(type, texture, passPos, topX, topY, rightX, rightY,
                           intensity, red, green, blue, drawDistance, time, upDistance);
}

static __declspec(naked) void hookAddPermanentShadowThunk() {
    __asm {
        mov currentTaskEdi, edi
        jmp hookAddPermanentShadow
    }
}

static bool validSites() {
    return std::memcmp(reinterpret_cast<const void*>(kStaticBloodSite.address), kStaticBloodSite.expected.data(), 5) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(kPermanentBloodSite.address), kPermanentBloodSite.expected.data(), 5) == 0;
}

static void patchRelCall(uintptr_t site, uintptr_t target) {
    std::array<unsigned char, 5> bytes{0xE8u, 0, 0, 0, 0};
    const int32_t rel = static_cast<int32_t>(target - (site + 5u));
    std::memcpy(bytes.data() + 1u, &rel, sizeof(rel));
    std::memcpy(reinterpret_cast<void*>(site), bytes.data(), bytes.size());
}

inline bool install() {
    if (installed) return true;
    if (!validSites()) return false;
    const uintptr_t first = kStaticBloodSite.address;
    const uintptr_t end = kPermanentBloodSite.address + 5u;
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(reinterpret_cast<const void*>(first), &region, sizeof(region))) return false;
    if (uintptr_t(region.BaseAddress) + region.RegionSize < end) return false;
    const DWORD protection = region.Protect;
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(first), end - first, PAGE_EXECUTE_READWRITE, &old)) return false;
    DWORD unused = 0;
    if (!validSites()) {
        VirtualProtect(reinterpret_cast<void*>(first), end - first, protection, &unused);
        return false;
    }
    const std::array<CodeRange, 2> ranges{{{kStaticBloodSite.address, kStaticBloodSite.address + 5u},
                                           {kPermanentBloodSite.address, kPermanentBloodSite.address + 5u}}};
    bool frozen = false;
    bool flushed = false;
    {
        FrozenThreads freeze(ranges);
        frozen = freeze.ok();
        if (frozen) {
            patchRelCall(kStaticBloodSite.address, reinterpret_cast<uintptr_t>(&hookStoreStaticShadow));
            patchRelCall(kPermanentBloodSite.address, reinterpret_cast<uintptr_t>(&hookAddPermanentShadowThunk));
            flushed = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(first), end - first) != 0;
        }
    }
    if (!frozen || !flushed) {
        FrozenThreads freeze(ranges);
        if (freeze.ok()) {
            std::memcpy(reinterpret_cast<void*>(kStaticBloodSite.address), kStaticBloodSite.expected.data(), 5);
            std::memcpy(reinterpret_cast<void*>(kPermanentBloodSite.address), kPermanentBloodSite.expected.data(), 5);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(first), end - first);
        }
        VirtualProtect(reinterpret_cast<void*>(first), end - first, protection, &unused);
        return false;
    }
    VirtualProtect(reinterpret_cast<void*>(first), end - first, protection, &unused);
    installed = true;
    return true;
}

inline void remove() {
    if (!installed) return;
    const uintptr_t first = kStaticBloodSite.address;
    const uintptr_t end = kPermanentBloodSite.address + 5u;
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(first), end - first, PAGE_EXECUTE_READWRITE, &old)) return;
    const std::array<CodeRange, 2> ranges{{{kStaticBloodSite.address, kStaticBloodSite.address + 5u},
                                           {kPermanentBloodSite.address, kPermanentBloodSite.address + 5u}}};
    {
        FrozenThreads freeze(ranges);
        if (freeze.ok()) {
            std::memcpy(reinterpret_cast<void*>(kStaticBloodSite.address), kStaticBloodSite.expected.data(), 5);
            std::memcpy(reinterpret_cast<void*>(kPermanentBloodSite.address), kPermanentBloodSite.expected.data(), 5);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(first), end - first);
        }
    }
    DWORD unused = 0;
    VirtualProtect(reinterpret_cast<void*>(first), end - first, old, &unused);
    {
        std::lock_guard<std::mutex> lock(anchorMutex);
        anchors.clear();
    }
    installed = false;
}
} // namespace blood_decals
