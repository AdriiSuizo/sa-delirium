#pragma once
#include <xinput.h>

namespace vehicle_nitro {
static unsigned key='N',button=XINPUT_GAMEPAD_DPAD_DOWN;
static unsigned sampledFrame=~0u;
static bool wasDown=false,padPressed=false,installed=false;

static bool readForegroundPad(XINPUT_STATE&pad){
    using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    static GetState get=nullptr;
    if(!get)for(const wchar_t*name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"}){
        if(auto module=GetModuleHandleW(name))get=reinterpret_cast<GetState>(GetProcAddress(module,"XInputGetState"));
        if(get)break;
    }
    DWORD foreground=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    return get&&foreground==GetCurrentProcessId()&&get(0,&pad)==ERROR_SUCCESS;
}

static void samplePad(){
    if(!installed||!button)return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(sampledFrame==frame)return;
    sampledFrame=frame;
    XINPUT_STATE pad{};
    const bool down=readForegroundPad(pad)&&(pad.Gamepad.wButtons&button)==button;
    padPressed=down&&!wasDown;
    wasDown=down;
}

static short __fastcall input(void*pad,void*){
    // This replaces only the GetCarGunFired call made by NitrousControl.
    // Stock firing, acceleration, the bottle count and recharge stay native.
    if(at<short>(pad,0x10E))return 0;
    const auto*newKeys=reinterpret_cast<const short*>(0xB73190+0x18);
    const auto*oldKeys=reinterpret_cast<const short*>(0xB72F20+0x18);
    samplePad();
    return (key&&newKeys[key]&&!oldKeys[key])||padPressed;
}

static bool install(const std::filesystem::path&ini){
    if(!GetPrivateProfileIntW(L"VehicleLab",L"SeparateNitro",1,ini.c_str()))return true;
    key=GetPrivateProfileIntW(L"VehicleLab",L"NitroKey",key,ini.c_str());
    if(key>255)key='N';
    button=GetPrivateProfileIntW(L"VehicleLab",L"NitroPadButton",button,ini.c_str())&0xFFFF;
    constexpr uintptr_t address=0x6A3F66;
    constexpr unsigned char expected[]={0xE8,0x25,0xC0,0xE9,0xFF,0x66,0x85,0xC0};
    auto*site=reinterpret_cast<unsigned char*>(address);
    if(std::memcmp(site,expected,sizeof(expected))!=0)return false;
    DWORD previous=0;
    if(!VirtualProtect(site+1,4,PAGE_EXECUTE_READWRITE,&previous))return false;
    const auto rel=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&input)-(address+5));
    {
        const std::array ranges{CodeRange{address,address+5}};
        FrozenThreads frozen(ranges);
        if(frozen.ok()&&std::memcmp(site,expected,sizeof(expected))==0){
            std::memcpy(site+1,&rel,4);
            FlushInstructionCache(GetCurrentProcess(),site,5);
            installed=true;
        }
    }
    DWORD unused=0;
    VirtualProtect(site+1,4,previous,&unused);
    return installed;
}
}
