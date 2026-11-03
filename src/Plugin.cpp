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
    log << "delirium_bootstrap_started\n" << std::flush;
    std::array<char, 1024> status{};
    const int result = prepare(root.c_str(), root.native().size(), status.data(), status.size());
    log << (result == 0 ? "delirium_assets_ready " : "delirium_assets_error ") << status.data() << '\n';
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
    if (GetPrivateProfileIntW(L"AirPitch", L"Experimental", 0, config.c_str()) == 1) {
        log << (installSteamAirPitch() ? "experimental_air_pitch_installed\n" : "experimental_air_pitch_unavailable\n");
    }
    const int percent = GetPrivateProfileIntW(L"Suspension", L"DampingIncreasePercent", 8, config.c_str());
    if (percent <= 0) { log << "suspension_disabled\n"; return; }
    const auto loader = GetModuleHandleW(L"dinput8.dll");
    const auto add = loader ? reinterpret_cast<AddVirtualFile>(
        GetProcAddress(loader, "AddVirtualFileForOverloadW")) : nullptr;
    if (!add) { log << "loader_virtual_file_api_unavailable\n"; return; }
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
