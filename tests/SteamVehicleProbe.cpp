#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

// Lab-only fixture for the exact Steam executable checked by the runner.
namespace {
using ProcessControl = void(__thiscall*)(void*);
using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
ProcessControl nativeControl{};
ProcessControl nativePedPreRender{};
GetState nativeGetState{};
const std::byte* image{};
FILE* trace{};
ULONGLONG started{};
unsigned padCalls{}, carCalls{}, playerCarCalls{};
unsigned pedCalls{}, playerPedCalls{};
bool civilPoseSeen{}, playerPoseSeen{};
bool cameraFixture{};
ULONGLONG lastCameraSample{};

int cameraAxis(ULONGLONG elapsed) {
    if (elapsed >= 28000 && elapsed < 30000) return 23000;
    if (elapsed >= 31500 && elapsed < 33500) return -23000;
    return 0;
}

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

void __fastcall observePedPreRender(void* actor, void*) {
    nativePedPreRender(actor);
    const auto* ped = static_cast<const std::byte*>(actor);
    const bool player = field<const void*>(ped, 0) == image + 0x4b7eb4;
    auto& seen = player ? playerPoseSeen : civilPoseSeen;
    auto& calls = player ? playerPedCalls : pedCalls;
    ++calls;
    const auto elapsed = GetTickCount64() - started;
    if (player && cameraFixture && elapsed >= 26500 && elapsed - lastCameraSample >= 120) {
        const auto* view = image + 0x7fc448 + 0x974;
        std::fprintf(trace, "camera %llu input=%d mouse=%d raw=%.4f yaw=%.5f\n", elapsed,
            cameraAxis(elapsed), field<unsigned char>(image, 0x7fc063),
            field<float>(image, 0x918b10), std::atan2(field<float>(view, 4), field<float>(view, 0)));
        std::fflush(trace);
        lastCameraSample = elapsed;
    }
    if (seen) return;

    const auto clump = field<void*>(ped, 0x18);
    if (!clump || !(field<unsigned>(ped, 0x474) & 0x400)) return;
    const auto hierarchyForClump = reinterpret_cast<const std::byte*(__cdecl*)(void*)>(image + 0x361890);
    const auto nodeIndex = reinterpret_cast<int(__cdecl*)(const std::byte*, int)>(image + 0x3f99a0);
    const auto matrixArray = reinterpret_cast<const std::byte*(__cdecl*)(const std::byte*)>(image + 0x3f9920);
    const auto* hierarchy = hierarchyForClump(clump);
    if (!hierarchy) return;
    const int nodes = field<int>(hierarchy, 4);
    if (nodes < 20 || nodes > 128) return;
    const int pelvis = nodeIndex(hierarchy, 1);
    const int head = nodeIndex(hierarchy, 5);
    if (pelvis < 0 || head < 0 || pelvis >= nodes || head >= nodes) return;
    const auto* matrices = matrixArray(hierarchy);
    if (!matrices) return;
    const float pelvisZ = field<float>(matrices, pelvis * 64 + 56);
    const float headZ = field<float>(matrices, head * 64 + 56);
    const float height = headZ - pelvisZ;
    if (!std::isfinite(height) || height < 0.20f || height > 1.20f) return;
    seen = true;
    std::fprintf(trace, "ped_pose %llu player=%d calls=%u nodes=%d head_above_pelvis=%.5f\n",
        GetTickCount64() - started, player, calls, nodes, height);
    std::fflush(trace);
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
    if (elapsed >= 1000 && elapsed < 38000 && elapsed % 1200 < 240 && (!cameraFixture || !playerPoseSeen)) {
        state->Gamepad.wButtons = XINPUT_GAMEPAD_A;
    }
    if (cameraFixture) state->Gamepad.sThumbRX = static_cast<SHORT>(cameraAxis(elapsed));
    return ERROR_SUCCESS;
}
} // namespace

extern "C" __declspec(dllexport) void InitializeASI() {
    wchar_t enabled[4]{};
    if (GetEnvironmentVariableW(L"SA_STEAM_VEHICLE_PROBE", enabled, 4) != 1 || enabled[0] != L'1') return;
    if (trace) return;
    cameraFixture = GetEnvironmentVariableW(L"SA_STEAM_CAMERA_PROBE", enabled, 4) == 1 && enabled[0] == L'1';
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
    nativePedPreRender = reinterpret_cast<ProcessControl>(image + 0x204f20);
    auto** civilianSlot = reinterpret_cast<void**>(const_cast<std::byte*>(image) + 0x4b6c8c) + 17;
    auto** playerSlot = reinterpret_cast<void**>(const_cast<std::byte*>(image) + 0x4b7eb4) + 17;
    const bool civilianHook = install(civilianSlot, reinterpret_cast<void*>(nativePedPreRender),
                                      reinterpret_cast<void*>(observePedPreRender));
    const bool playerHook = install(playerSlot, reinterpret_cast<void*>(nativePedPreRender),
                                    reinterpret_cast<void*>(observePedPreRender));
    std::fprintf(trace, "probe car=%d pad=%d civilian=%d player=%d\n", carHook, padHook, civilianHook, playerHook);
    std::fflush(trace);
}
