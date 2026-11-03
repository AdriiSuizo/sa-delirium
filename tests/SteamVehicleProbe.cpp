#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

// Lab-only fixture for the exact Steam executable checked by the runner.
namespace {
using ProcessControl = void(__thiscall*)(void*);
using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
ProcessControl nativeControl{};
GetState nativeGetState{};
const std::byte* image{};
FILE* trace{};
ULONGLONG started{};
unsigned padCalls{}, carCalls{}, playerCarCalls{};

template <typename T>
T field(const std::byte* object, std::size_t offset) {
    T value{};
    std::memcpy(&value, object + offset, sizeof(value));
    return value;
}

bool install(void** entry, void* expected, void* replacement) {
    if (*entry != expected) return false;
    DWORD protection{};
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &protection)) return false;
    const bool swapped = InterlockedCompareExchangePointer(entry, replacement, expected) == expected;
    DWORD unused{};
    const bool restored = VirtualProtect(entry, sizeof(*entry), protection, &unused) != 0;
    return swapped && restored;
}

void __fastcall observeControl(void* vehicle, void*) {
    nativeControl(vehicle);
    const auto* car = static_cast<const std::byte*>(vehicle);
    if (field<unsigned>(car, 0x594) != 0) return;
    const bool player = (field<unsigned char>(car, 0x36) & 0xf8) == 0 && field<void*>(car, 0x460);
    ++carCalls;
    if (player) ++playerCarCalls;
    if (carCalls != 1 && carCalls % 300 && (!player || playerCarCalls % 20)) return;
    std::fprintf(trace,
        "car %llu calls=%u player_calls=%u player=%d dt50=%.5f contacts=%u driven=%u gas=%.3f brake=%.3f\n",
        GetTickCount64() - started, carCalls, playerCarCalls, player, field<float>(image, 0x80fd50),
        unsigned(field<unsigned char>(car, 0x960)), unsigned(field<unsigned char>(car, 0x961)),
        field<float>(car, 0x49c), field<float>(car, 0x4a0));
    std::fflush(trace);
}

DWORD WINAPI labPad(DWORD user, XINPUT_STATE* state) {
    if (user != 0 || !state) return nativeGetState(user, state);
    const auto elapsed = GetTickCount64() - started;
    const auto poll = ++padCalls;
    if (poll == 1 || poll % 250 == 0) {
        std::fprintf(trace, "pad %llu polls=%u\n", elapsed, poll);
        std::fflush(trace);
    }
    *state = {};
    state->dwPacketNumber = poll;
    // Start a fresh game from the default menu selection. No save commands.
    if (elapsed >= 1000 && elapsed < 38000 && elapsed % 1200 < 240) {
        state->Gamepad.wButtons = XINPUT_GAMEPAD_A;
    }
    return ERROR_SUCCESS;
}
} // namespace

extern "C" __declspec(dllexport) void InitializeASI() {
    wchar_t enabled[4]{};
    if (GetEnvironmentVariableW(L"SA_STEAM_VEHICLE_PROBE", enabled, 4) != 1 || enabled[0] != L'1') return;
    if (trace) return;
    if (fopen_s(&trace, "steam-vehicle-probe.log", "w") != 0) return;
    started = GetTickCount64();
    image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    const auto xinput = GetModuleHandleW(L"xinput1_3.dll");
    nativeGetState = xinput ? reinterpret_cast<GetState>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
    nativeControl = reinterpret_cast<ProcessControl>(image + 0x2dfe40);
    const auto carSlot = reinterpret_cast<void**>(const_cast<std::byte*>(image) + 0x4bc49c) + 10;
    const auto padSlot = reinterpret_cast<void**>(const_cast<std::byte*>(image) + 0x4a036c);
    const bool carHook = install(carSlot, reinterpret_cast<void*>(nativeControl), reinterpret_cast<void*>(observeControl));
    const bool padHook = nativeGetState && install(padSlot, reinterpret_cast<void*>(nativeGetState), reinterpret_cast<void*>(labPad));
    std::fprintf(trace, "probe car=%d pad=%d\n", carHook, padHook);
    std::fflush(trace);
}
