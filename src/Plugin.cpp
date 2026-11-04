#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include "Handling.hpp"
#include "SteamVehicle.hpp"

namespace fs = std::filesystem;
using AddVirtualFile = bool(WINAPI*)(const wchar_t*, const unsigned char*, std::size_t, int);
bool installSteamColour(const fs::path& log);
bool installSteamGamepad(const fs::path& log);
bool installSteamBorderless(const fs::path& log);

static std::string read(const fs::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    const auto size = in.tellg();
    if (size <= 0 || static_cast<std::size_t>(size) > limit) return {};
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.seekg(0);
    if (!in.read(bytes.data(), bytes.size())) return {};
    return bytes;
}

static bool supported(const fs::path& path) {
    if (_wcsicmp(path.filename().c_str(), L"gta-sa.exe") != 0) return false;
    const auto data = read(path, 8 * 1024 * 1024);
    if (data.size() != 5971456) return false;
    std::array<unsigned char, 32> hash{};
    constexpr std::array<unsigned char, 32> steamR2{
        0x15,0xe3,0xcf,0xed,0xba,0x9a,0x84,0x1d,0xf6,0x7d,0x81,0x94,0xe7,0x24,0x9a,0xfb,
        0x49,0x3b,0x0e,0x10,0xd6,0x13,0x8f,0xb8,0xeb,0xab,0x2c,0x13,0x6e,0x54,0x3e,0xfb
    };
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()),
        hash.data(), static_cast<ULONG>(hash.size())) >= 0 && hash == steamR2;
}

static void prepareDelirium(const fs::path& root, std::ostream& log) {
    const auto library = root / L"scripts" / L"sa_delirium_bootstrap.dll";
    if (!fs::is_regular_file(library)) return;
    const auto module = LoadLibraryExW(library.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Prepare = int(__cdecl*)(const wchar_t*, std::size_t, char*, std::size_t);
    const auto prepare = module ? reinterpret_cast<Prepare>(GetProcAddress(module, "sa_delirium_prepare")) : nullptr;
    if (!prepare) {
        if (module) FreeLibrary(module);
        log << "delirium_bootstrap_unavailable\n";
        return;
    }
    for (;;) {
        log << "delirium_bootstrap_started\n" << std::flush;
        std::array<char, 1024> status{};
        const int result = prepare(root.c_str(), root.native().size(), status.data(), status.size());
        if (result == 0) {
            log << "delirium_assets_ready " << status.data() << '\n';
            return;
        }
        log << "delirium_assets_error " << status.data() << '\n' << std::flush;
        if (result == -2) {
            const auto message =
                L"Coloca la ISO de tu copia de GTA IV para Xbox 360 en esta carpeta y pulsa Reintentar.\n\n"
                + root.native() +
                L"\n\nDespués de preparar los datos podrás retirar la ISO. Cancelar inicia el juego sin esos datos.";
            if (MessageBoxW(nullptr, message.c_str(), L"Delirium SA · Primera instalación",
                            MB_RETRYCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND) == IDRETRY) continue;
            log << "delirium_iso_prompt_cancelled\n";
        } else {
            std::array<wchar_t, 1024> detail{};
            MultiByteToWideChar(CP_UTF8, 0, status.data(), -1, detail.data(), static_cast<int>(detail.size()));
            const auto message = std::wstring(L"No se pudieron preparar los datos de Delirium.\n\n")
                + detail.data() + L"\n\nEl juego continuará sin esos datos. Consulta SAStandalone.log.";
            MessageBoxW(nullptr, message.c_str(), L"Delirium SA · Error de preparación",
                        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        }
        return;
    }
}

static void preparePs2TimeCycle(const fs::path& root, AddVirtualFile add, std::ostream& log) {
    static const auto table = read(root / L"data" / L"timecycp.dat", 128 * 1024);
    const auto target = root / L"data" / L"timecyc.dat";
    if (table.empty() || !fs::is_regular_file(target)) {
        log << "ps2_timecycle_source_unavailable\n";
        return;
    }
    const bool registered = add(target.c_str(), reinterpret_cast<const unsigned char*>(table.data()), table.size(), 0);
    log << (registered ? "ps2_timecycle_overlay_registered\n" : "ps2_timecycle_overlay_rejected\n");
    if (registered)
        log << (installSteamColour(root / L"SAStandalone.log") ? "ps2_colour_hook_installed\n" : "ps2_colour_hook_unavailable\n");
}

static void initialize() {
    wchar_t name[32768];
    const DWORD len = GetModuleFileNameW(nullptr, name, static_cast<DWORD>(std::size(name)));
    if (!len || len >= std::size(name)) return;
    const fs::path exe(std::wstring_view(name, len));
    const auto root = exe.parent_path();
    std::ofstream log(root / L"SAStandalone.log", std::ios::trunc);
    if (!supported(exe)) { log << "unsupported_executable\n"; return; }
    prepareDelirium(root, log);
    const auto config = root / L"scripts" / L"SAStandalone.ini";
    if (GetPrivateProfileIntW(L"Display", L"Borderless", 1, config.c_str()) == 1)
        log << (installSteamBorderless(root / L"SAStandalone.log") ? "borderless_hook_installed\n" : "borderless_hook_unavailable\n");
    if (GetPrivateProfileIntW(L"Controls", L"RightStickCamera", 1, config.c_str()) == 1)
        log << (installSteamGamepad(root / L"SAStandalone.log") ? "xinput_camera_hook_installed\n" : "xinput_camera_hook_unavailable\n");
    if (GetPrivateProfileIntW(L"AirPitch", L"Experimental", 0, config.c_str()) == 1) {
        log << (installSteamAirPitch() ? "experimental_air_pitch_installed\n" : "experimental_air_pitch_unavailable\n");
    }
    const int percent = GetPrivateProfileIntW(L"Suspension", L"DampingIncreasePercent", 8, config.c_str());
    const auto loader = GetModuleHandleW(L"dinput8.dll");
    const auto add = loader ? reinterpret_cast<AddVirtualFile>(
        GetProcAddress(loader, "AddVirtualFileForOverloadW")) : nullptr;
    if (!add) { log << "loader_virtual_file_api_unavailable\n"; return; }
    if (GetPrivateProfileIntW(L"Visuals", L"Ps2TimeCycle", 1, config.c_str()) == 1)
        preparePs2TimeCycle(root, add, log);
    if (percent <= 0) { log << "suspension_disabled\n"; return; }
    const auto path = root / L"data" / L"handling.cfg";
    const auto handling = read(path, 1024 * 1024);
    const auto vehicles = read(root / L"data" / L"vehicles.ide", 1024 * 1024);
    if (handling.empty() || vehicles.empty()) { log << "vehicle_data_unavailable\n"; return; }
    static HandlingOverlay overlay;
    overlay = tuneSuspension(handling, vehicles, percent);
    if (!overlay.adjusted) { log << "vehicle_data_unrecognized\n"; return; }
    if (!add(path.c_str(), reinterpret_cast<const unsigned char*>(overlay.text.data()), overlay.text.size(), 0)) {
        log << "virtual_file_rejected\n";
        return;
    }
    log << "suspension_overlay_registered rows=" << overlay.adjusted << '\n';
}

extern "C" __declspec(dllexport) void InitializeASI() {
    static std::once_flag once;
    try { std::call_once(once, initialize); }
    catch (...) { OutputDebugStringW(L"SA Standalone: initialization failed"); }
}
