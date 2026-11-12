#pragma once

namespace drive_by_controls {
static constexpr uintptr_t primaryPad=0xB73458;
static bool installed=false, testInput=false, connected=false;
static unsigned sampledFrame=~0u, hydraulicSuppressions=0;
static XINPUT_GAMEPAD input{}, testPad{};
static short(__thiscall* nativeFire)(void*)=nullptr;
static short(__thiscall* nativeHandbrake)(void*)=nullptr;
static short(__thiscall* nativeVehicleAxisX)(void*)=nullptr;
static short(__thiscall* nativeVehicleAxisY)(void*)=nullptr;
static bool(__thiscall* nativeLookLeft)(void*)=nullptr;
static bool(__thiscall* nativeLookRight)(void*)=nullptr;
static void(__thiscall* nativeHydraulics)(void*)=nullptr;

static void sample(){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(sampledFrame==frame)return;
    sampledFrame=frame;
    XINPUT_STATE state{};
    connected=testInput||vehicle_nitro::readForegroundPad(state);
    input=testInput?testPad:state.Gamepad;
}

static void* armedDriverVehicle(void*pad){
    if(pad!=reinterpret_cast<void*>(primaryPad)||at<short>(pad,0x10E)||!playing()||cutscene())return nullptr;
    auto*cj=player();
    if(!cj||!inVehicle(cj))return nullptr;
    auto*car=at<void*>(cj,0x58C);
    if(vehicleRef(car)<0||at<void*>(car,0x460)!=cj)return nullptr;
    if(at<short>(car,0x22)==432)return nullptr;
    const int type=at<int>(car,0x594);
    if((type<0||type>2)&&type!=9&&type!=10)return nullptr;
    const unsigned slot=at<unsigned char>(cj,0x718);
    if(slot>=13)return nullptr;
    auto*weapon=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)
        (at<int>(cj,0x5A0+slot*0x1C),1);
    return weapon&&at<int>(weapon,0x14)==4?car:nullptr;
}

static bool aimHeld(){
    sample();
    return connected&&(input.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
}

#include "DriveByAim.hpp"

static short __fastcall fire(void*pad,void*){
    if(auto*car=armedDriverVehicle(pad)){
        sample();
        if(connected&&(input.wButtons||input.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD)){
            const bool pressed=(input.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
            return pressed&&(!aimHeld()||hasAimView(car))?1:0;
        }
    }
    return nativeFire(pad);
}

static short __fastcall handbrake(void*pad,void*){
    if(armedDriverVehicle(pad)){
        sample();
        if(connected&&(input.wButtons&(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER))&&
           !(input.wButtons&XINPUT_GAMEPAD_A))return 0;
    }
    return nativeHandbrake(pad);
}

static short __fastcall vehicleAxisX(void*pad,void*){
    return armedDriverVehicle(pad)&&aimHeld()?0:nativeVehicleAxisX(pad);
}

static short __fastcall vehicleAxisY(void*pad,void*){
    return armedDriverVehicle(pad)&&aimHeld()?0:nativeVehicleAxisY(pad);
}

static bool __fastcall left(void*pad,void*){
    if(armedDriverVehicle(pad)){
        if(animationSide)return animationSide<0;
        if(aimHeld())return false;
    }
    return nativeLookLeft(pad);
}

static bool __fastcall right(void*pad,void*){
    if(armedDriverVehicle(pad)){
        if(animationSide)return animationSide>0;
        if(aimHeld())return false;
    }
    return nativeLookRight(pad);
}

static void __fastcall hydraulics(void*car,void*){
    // The hydraulics' input changes the collision lines. Hold their current
    // commanded position while aiming; the road suspension still runs.
    if(car==armedDriverVehicle(reinterpret_cast<void*>(primaryPad))&&aimHeld()){
        if(testInput)++hydraulicSuppressions;
        return;
    }
    nativeHydraulics(car);
}

static bool recognizedPadEntry(uintptr_t address){
    const unsigned char native[]={0x66,0x83,0xb9,0x0e,0x01,0,0,0};
    const auto*bytes=reinterpret_cast<const unsigned char*>(address);
    if(std::memcmp(bytes,native,sizeof(native))==0)return true;
    if(bytes[0]!=0xE9)return false;
    int32_t displacement=0;
    std::memcpy(&displacement,bytes+1,sizeof(displacement));
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const wchar_t*>(address+5+displacement),&module)&&module==GetModuleHandleW(L"GInputSA.asi");
}

static bool install(const std::filesystem::path&ini){
    if(!GetPrivateProfileIntW(L"VehicleLab",L"DriveByShoulderAim",1,ini.c_str()))return true;
    struct Hook {
        uintptr_t entry;void* replacement;void** original;
        size_t size=0;
        std::array<unsigned char,8> signature{};
    };
    const Hook hooks[]={
        {0x53FF90,reinterpret_cast<void*>(&fire),reinterpret_cast<void**>(&nativeFire)},
        {0x540040,reinterpret_cast<void*>(&handbrake),reinterpret_cast<void**>(&nativeHandbrake)},
        {0x53FDD0,reinterpret_cast<void*>(&left),reinterpret_cast<void**>(&nativeLookLeft)},
        {0x53FE10,reinterpret_cast<void*>(&right),reinterpret_cast<void**>(&nativeLookRight)},
        {0x53FC50,reinterpret_cast<void*>(&vehicleAxisX),reinterpret_cast<void**>(&nativeVehicleAxisX)},
        {0x53FC10,reinterpret_cast<void*>(&vehicleAxisY),reinterpret_cast<void**>(&nativeVehicleAxisY)},
        {0x6A07A0,reinterpret_cast<void*>(&hydraulics),reinterpret_cast<void**>(&nativeHydraulics),6,{0x83,0xec,0x60,0x55,0x8b,0xe9}},
        {0x5150E0,reinterpret_cast<void*>(&deriveCamera),reinterpret_cast<void**>(&nativeDerivedCamera),8,{0x6a,0xff,0x68,0x78,0xc7,0x83,0,0x64}},
        {0x741FD0,reinterpret_cast<void*>(&driveBy),reinterpret_cast<void**>(&nativeDriveBy),8,{0x83,0xec,0x24,0x56,0x57,0x8b,0xf9,0x90}},
        {0x73CBA0,reinterpret_cast<void*>(&carBullet),reinterpret_cast<void**>(&nativeCarBullet),8,{0x6a,0xff,0x68,0x68,0x8e,0x84,0,0x64}},
        {0x58E020,reinterpret_cast<void*>(&crosshair),reinterpret_cast<void**>(&nativeCrosshair),8,{0x83,0xec,0x30,0x0f,0xb6,0x05,0x74,0xcd}},
    };
    for(const auto&hook:hooks){
        const bool valid=hook.size?std::memcmp(reinterpret_cast<void*>(hook.entry),
            hook.signature.data(),hook.size)==0:recognizedPadEntry(hook.entry);
        if(!valid){
            event("vehicle_drive_by_hook_mismatch",-1,-1,"address="+std::to_string(hook.entry));
            return false;
        }
    }
    unsigned created=0;
    for(const auto&hook:hooks){
        if(MH_CreateHook(reinterpret_cast<void*>(hook.entry),hook.replacement,hook.original)!=MH_OK)break;
        ++created;
    }
    if(created==std::size(hooks)){
        installed=true;
        for(const auto&hook:hooks)
            if(MH_QueueEnableHook(reinterpret_cast<void*>(hook.entry))!=MH_OK)installed=false;
        if(installed)return true;
    }
    for(unsigned i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(hooks[i].entry));
    return false;
}
}
