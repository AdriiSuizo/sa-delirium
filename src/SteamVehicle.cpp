#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstring>
#include "AirPitch.hpp"
#include "SteamVehicle.hpp"

namespace {
using ProcessControl = void(__thiscall*)(void*);
ProcessControl nativeControl{};
const std::byte* gameImage{};

template <typename T>
T field(const std::byte* object, std::size_t offset) {
    T value{};
    std::memcpy(&value, object + offset, sizeof(value));
    return value;
}

void __fastcall processControl(void* vehicle, void*) {
    nativeControl(vehicle);

    auto* car = static_cast<std::byte*>(vehicle);
    if (field<unsigned>(car, 0x594) != 0 || (field<unsigned char>(car, 0x36) & 0xf8) ||
        !field<void*>(car, 0x460)) return;
    const auto contacts = field<unsigned char>(car, 0x960);
    if (contacts) return;
    const auto* matrix = field<const std::byte*>(car, 0x14);
    if (!matrix) return;

    auto angular = field<air_pitch::Vector>(car, 0x50);
    const float delta = air_pitch::correction(angular, field<air_pitch::Vector>(matrix, 0x00),
        field<float>(matrix, 0x28), contacts, field<float>(car, 0x49c), field<float>(car, 0x4a0),
        field<float>(gameImage, 0x80fd50));
    if (delta != 0.0f) std::memcpy(car + 0x50, &angular, sizeof(angular));
}
}

// The caller has already validated the executable's SHA-256.
bool installSteamAirPitch() {
    auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    auto** slot = reinterpret_cast<void**>(image + 0x4bc49c) + 10;
    auto* expected = image + 0x2dfe40;
    if (*slot != expected) return false;
    gameImage = image;
    nativeControl = reinterpret_cast<ProcessControl>(expected);

    DWORD protection{};
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    const bool installed = InterlockedCompareExchangePointer(slot,
        reinterpret_cast<void*>(processControl), expected) == expected;
    DWORD ignored{};
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return installed;
}
