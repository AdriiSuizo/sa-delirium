#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using ProcessCamera = void(__thiscall*)(void*);
constexpr std::uintptr_t getStateRva = 0x37d704; // Game thunk through the Steam-visible IAT.
constexpr std::uintptr_t processCameraRva = 0x1397c0;
constexpr std::uintptr_t mouseCameraRva = 0x7fc063;
constexpr std::uintptr_t swapLookStickRva = 0x540014;
constexpr std::uintptr_t exclusiveLookStickRva = 0x8007a3;
std::byte* image{};
ULONGLONG lastStickPoll{};
std::filesystem::path trace;
bool connectedLogged{};
bool cameraLogged{};

void logOnce(bool& logged, const char* event) {
    if (logged) return;
    logged = true;
    std::ofstream(trace, std::ios::app) << event << '\n';
}

DWORD WINAPI observeStick(DWORD user, XINPUT_STATE* state) {
    const auto result = reinterpret_cast<GetState>(image + getStateRva)(user, state);
    if (user == 0) {
        const bool connected = result == ERROR_SUCCESS && state;
        const bool moving = connected &&
            (std::abs(int(state->Gamepad.sThumbRX)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
             std::abs(int(state->Gamepad.sThumbRY)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        lastStickPoll = moving ? GetTickCount64() : 0;
        if (connected) logOnce(connectedLogged, "xinput_player_one_seen");
    }
    return result;
}

void __fastcall processCamera(void* camera, void*) {
    const auto native = reinterpret_cast<ProcessCamera>(image + processCameraRva);
    auto& mouse = image[mouseCameraRva];
    if (!lastStickPoll || GetTickCount64() - lastStickPoll > 200 || mouse == std::byte{}) {
        native(camera);
        return;
    }
    // Only the camera chooses the joystick route. Walking, menus and the
    // native controller preferences keep their current inputs and values.
    auto& swap = image[swapLookStickRva];
    auto& exclusive = image[exclusiveLookStickRva];
    const auto oldMouse = mouse, oldSwap = swap, oldExclusive = exclusive;
    mouse = std::byte{};
    swap = exclusive = std::byte{1};
    native(camera);
    mouse = oldMouse;
    swap = oldSwap;
    exclusive = oldExclusive;
    logOnce(cameraLogged, "xinput_right_stick_camera_route");
}

struct Call {
    std::uintptr_t site, target, replacement;
};
bool matches(const Call& call) {
    auto* bytes = image + call.site;
    std::int32_t relative{};
    std::memcpy(&relative, bytes + 1, sizeof(relative));
    return *bytes == std::byte{0xe8} && call.site + 5 + relative == call.target;
}
bool change(const Call& call, std::uintptr_t address) {
    auto* bytes = image + call.site;
    DWORD protection{};
    if (!VirtualProtect(bytes, 5, PAGE_EXECUTE_READWRITE, &protection)) return false;
    const auto relative = static_cast<std::uint32_t>(address - reinterpret_cast<std::uintptr_t>(bytes + 5));
    std::memcpy(bytes + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), bytes, 5);
    DWORD unused{};
    VirtualProtect(bytes, 5, protection, &unused);
    return true;
}
}

bool installSteamGamepad(const std::filesystem::path& log) {
    image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    trace = log;
    const auto camera = reinterpret_cast<std::uintptr_t>(&processCamera);
    const std::array calls{
        Call{0x37c982, getStateRva, reinterpret_cast<std::uintptr_t>(&observeStick)},
        Call{0x14ba35, processCameraRva, camera},
        Call{0x14bbf7, processCameraRva, camera},
        Call{0x14be77, processCameraRva, camera},
        Call{0x163acd, processCameraRva, camera},
    };
    for (const auto& call : calls) if (!matches(call)) return false;
    for (std::size_t i = 0; i < calls.size(); ++i) {
        if (change(calls[i], calls[i].replacement)) continue;
        while (i != 0) {
            --i;
            change(calls[i], reinterpret_cast<std::uintptr_t>(image + calls[i].target));
        }
        return false;
    }
    return true;
}
