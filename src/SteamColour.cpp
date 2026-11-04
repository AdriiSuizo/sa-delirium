#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
struct Rgba { std::uint8_t r, g, b, a; };
struct ScreenVertex {
    float x, y, z, rhw;
    std::uint32_t argb;
    float u, v;
};
static_assert(sizeof(ScreenVertex) == 28);

using StateSet = int(__cdecl*)(int, void*);
using StateGet = int(__cdecl*)(int, void*);
using Draw = int(__cdecl*)(int, ScreenVertex*, int, const std::uint16_t*, int);
using Colour = void(__cdecl*)(Rgba, Rgba);
std::byte* image{};
std::filesystem::path trace;
constexpr std::size_t nativeFilter = 0x3428d0;
constexpr std::array<int, 8> stateIds{9, 14, 6, 8, 1, 12, 10, 11};

void __cdecl colourFilter(Rgba first, Rgba second) {
    const auto original = reinterpret_cast<Colour>(image + nativeFilter);
    const auto* rw = *reinterpret_cast<const std::byte**>(image + 0x923664);
    const auto raster = *reinterpret_cast<void**>(image + 0x8db208);
    const auto night = image[0x8db1e6] != std::byte{};
    const auto infrared = image[0x8db1e7] != std::byte{};
    if (!rw || !raster || night || infrared) { original(first, second); return; }

    const auto set = *reinterpret_cast<const StateSet*>(rw + 0x20);
    const auto get = *reinterpret_cast<const StateGet*>(rw + 0x24);
    const auto draw = *reinterpret_cast<const Draw*>(rw + 0x34);
    std::array<void*, stateIds.size()> saved{};
    for (std::size_t i = 0; i < stateIds.size(); ++i) {
        if (!get(stateIds[i], &saved[i])) { original(first, second); return; }
    }
    const auto setInt = [set](int state, std::uintptr_t value) { set(state, reinterpret_cast<void*>(value)); };
    setInt(9, 2);  // Linear sampling.
    setInt(14, 0); // The source raster already contains the scene's fog.
    setInt(6, 0);
    setInt(8, 0);
    set(1, raster);
    setInt(12, 0);
    setInt(10, 2);
    setInt(11, 1);

    std::array<ScreenVertex, 4> quad;
    std::memcpy(quad.data(), image + 0x8db008, sizeof(quad));
    const auto indices = reinterpret_cast<const std::uint16_t*>(image + 0x54d29c);
    const auto gain = [a = int(second.a)](int base, int add) {
        return (255 * (128 * base) + 128 * add * a + 8192) / 16384;
    };
    // GS modulation uses 128 as unity. Split the combined multiplier into
    // representable UNORM layers, replacing the framebuffer on the first
    // pass and adding only the remainder. No resampling or blur offsets.
    const std::array gains{gain(first.r, second.r), gain(first.g, second.g), gain(first.b, second.b)};
    const int maximum = *std::max_element(gains.begin(), gains.end());
    for (int offset = 0; ; offset += 255) {
        const auto channel = [offset](int value) { return std::uint32_t(std::clamp(value - offset, 0, 255)); };
        const auto argb = 0xff000000u | (channel(gains[0]) << 16) | (channel(gains[1]) << 8) | channel(gains[2]);
        for (auto& v : quad) v.argb = argb;
        draw(3, quad.data(), 4, indices, 6);
        if (maximum <= offset + 255) break;
        if (offset == 0) {
            setInt(12, 1);
            setInt(11, 2);
        }
    }
    for (std::size_t i = 0; i < stateIds.size(); ++i) set(stateIds[i], saved[i]);
    if (!trace.empty()) {
        std::ofstream(trace, std::ios::app) << "ps2_colour_first_frame\n";
        trace.clear();
    }
}
}

// Called during ASI initialization, before the verified game begins rendering.
bool installSteamColour(const std::filesystem::path& log) {
    image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    constexpr std::size_t callRva = 0x34438b;
    auto* call = image + callRva;
    constexpr std::array<unsigned char, 5> expected{0xe8, 0x40, 0xe5, 0xff, 0xff};
    if (std::memcmp(call, expected.data(), expected.size()) != 0) return false;
    trace = log;
    const auto displacement = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&colourFilter) -
                                                         reinterpret_cast<std::uintptr_t>(call + 5));
    DWORD old{};
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(call + 1, &displacement, sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    DWORD unused{};
    VirtualProtect(call, 5, old, &unused);
    return true;
}
