// Probe only the verified DXVK runtime in our prepared profile. No game,
// ASI, saves or user input is loaded. The window is never shown or activated.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <iostream>

int wmain(int argc,wchar_t**argv) {
    if(argc!=2)return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(wcscmp(argv[1],L"--check-legacy-runtime")==0) {
        auto input=LoadLibraryExW(L"xinput1_3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto effects=LoadLibraryExW(L"d3dx9_43.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        bool present=input&&effects&&GetProcAddress(input,"XInputGetState")&&GetProcAddress(effects,"D3DXCreateTextureFromFileA");
        if(input)FreeLibrary(input);if(effects)FreeLibrary(effects);
        std::cout<<"{\"legacy_runtime_ready\":"<<(present?"true":"false")<<"}\n";
        return present?0:7;
    }
    auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module){std::cout<<"{\"status\":\"runtime_load_failed\"}\n";return 3;}
    using Factory=IDirect3D9*(WINAPI*)(UINT);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"Direct3DCreate9"));
    auto d3d=factory?factory(D3D_SDK_VERSION):nullptr;
    if(!d3d){std::cout<<"{\"status\":\"adapter_unavailable\"}\n";FreeLibrary(module);return 4;}
    auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"SADeliriumRendererProbe";
    RegisterClassW(&cls);
    auto window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,instance,nullptr);
    if(!window){d3d->Release();FreeLibrary(module);return 5;}
    D3DPRESENT_PARAMETERS present{};present.Windowed=TRUE;present.hDeviceWindow=window;
    present.BackBufferWidth=32;present.BackBufferHeight=32;present.BackBufferCount=1;
    present.BackBufferFormat=D3DFMT_X8R8G8B8;present.SwapEffect=D3DSWAPEFFECT_DISCARD;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;
    const auto status=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&present,&device);
    const bool supported=SUCCEEDED(status)&&device;
    if(device)device->Release();DestroyWindow(window);UnregisterClassW(cls.lpszClassName,instance);
    d3d->Release();FreeLibrary(module);
    std::cout<<"{\"status\":\""<<(supported?"device_created":"device_unavailable")<<"\",\"hresult\":"<<static_cast<long>(status)<<"}\n";
    return supported?0:6;
}
