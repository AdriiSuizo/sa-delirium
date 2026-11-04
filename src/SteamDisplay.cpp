#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstddef>
#include <filesystem>
#include <fstream>

namespace {
using Factory = IDirect3D9*(WINAPI*)(UINT);
using CreateDevice = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                 D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
Factory originalFactory{};
CreateDevice originalCreate{};
Reset originalReset{};
HWND gameWindow{};
std::filesystem::path trace;

template<class Fn>
bool redirect(void** slot, Fn replacement, Fn& original) {
    if (*slot == reinterpret_cast<void*>(replacement)) return original != nullptr;
    if (original) return false;
    void* const prior = *slot;
    DWORD protection{};
    if (!prior || !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    original = reinterpret_cast<Fn>(prior);
    const bool replaced = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(replacement), prior) == prior;
    if (!replaced) original = nullptr;
    DWORD unused{};
    VirtualProtect(slot, sizeof(*slot), protection, &unused);
    return replaced;
}

void setWindowed(D3DPRESENT_PARAMETERS& params, HWND window) {
    params.Windowed = TRUE;
    params.FullScreen_RefreshRateInHz = 0;
    params.BackBufferFormat = D3DFMT_UNKNOWN;
    if (!window) return;
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const auto bounds = monitor.rcMonitor;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return;
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    const auto borderless = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))
                            | WS_POPUP;
    if (borderless != style) SetWindowLongPtrW(window, GWL_STYLE, borderless);
    SetWindowPos(window, nullptr, bounds.left, bounds.top, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    params.BackBufferWidth = static_cast<UINT>(width);
    params.BackBufferHeight = static_cast<UINT>(height);
    params.hDeviceWindow = window;
}

HRESULT STDMETHODCALLTYPE resetDevice(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    if (!params) return D3DERR_INVALIDCALL;
    setWindowed(*params, params->hDeviceWindow ? params->hDeviceWindow : gameWindow);
    return originalReset(device, params);
}

HRESULT STDMETHODCALLTYPE createDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                                      DWORD flags, D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device) {
    if (!params || !device) return D3DERR_INVALIDCALL;
    gameWindow = params->hDeviceWindow ? params->hDeviceWindow : focus;
    setWindowed(*params, gameWindow);
    const auto result = originalCreate(d3d, adapter, type, focus, flags, params, device);
    if (SUCCEEDED(result) && *device) {
        auto** methods = *reinterpret_cast<void***>(*device);
        if (!redirect(methods + 16, &resetDevice, originalReset)) {
            (*device)->Release();
            *device = nullptr;
            std::ofstream(trace, std::ios::app) << "borderless_reset_hook_unavailable\n";
            return D3DERR_NOTAVAILABLE;
        }
        std::ofstream(trace, std::ios::app) << "borderless_device " << params->BackBufferWidth << 'x'
                                           << params->BackBufferHeight << '\n';
    }
    return result;
}

IDirect3D9* WINAPI createFactory(UINT sdk) {
    auto* d3d = originalFactory(sdk);
    if (!d3d) return nullptr;
    auto** methods = *reinterpret_cast<void***>(d3d);
    if (redirect(methods + 16, &createDevice, originalCreate)) return d3d;
    std::ofstream(trace, std::ios::app) << "borderless_device_hook_unavailable\n";
    d3d->Release();
    return nullptr;
}
}

bool installSteamBorderless(const std::filesystem::path& log) {
    const auto* image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    auto** slot = reinterpret_cast<void**>(const_cast<std::byte*>(image) + 0x4a0374);
    trace = log;
    return redirect(slot, &createFactory, originalFactory);
}
