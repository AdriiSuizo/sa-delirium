#pragma once

namespace drive_by_controls {
static constexpr uintptr_t primaryPad=0xB73458;
struct MouseState {
    unsigned char lmb,rmb,mmb,wheelUp,wheelDown,bmx1,bmx2,pad;
    float z,x,y;
};
static const MouseState& mouse(){return *reinterpret_cast<const MouseState*>(0xB73418);}
static bool installed=false, driveByEnabled=true, ivFootAimEnabled=true, testInput=false, connected=false;
static unsigned sampledFrame=~0u, hydraulicSuppressions=0;
static XINPUT_GAMEPAD input{}, testPad{};
static int lowriderVehicleRef=-1;
static bool lowriderCameraMode=false, prevToggleKey=false, prevTogglePad=false;
static unsigned lowriderToggleKey='G';
static WORD lowriderTogglePad=XINPUT_GAMEPAD_DPAD_UP;
static void* maskedHydraulicsCar=nullptr;
static short(__thiscall* nativeFire)(void*)=nullptr;
static short(__thiscall* nativeHandbrake)(void*)=nullptr;
static short(__thiscall* nativeVehicleAxisX)(void*)=nullptr;
static short(__thiscall* nativeVehicleAxisY)(void*)=nullptr;
static bool(__thiscall* nativeLookLeft)(void*)=nullptr;
static bool(__thiscall* nativeLookRight)(void*)=nullptr;
static bool(__thiscall* nativeLookBehindCar)(void*)=nullptr;
static bool(__thiscall* nativeHydraulicJump)(void*)=nullptr;
static void(__thiscall* nativeHydraulics)(void*)=nullptr;

static bool vehicleHasHydraulics(void*car){
    return car&&(at<unsigned>(car,0x38C)&0x20000u)!=0&&(at<unsigned>(car,0x38C)&0x40000u)==0&&
        at<short>(car,0x22)!=525&&at<short>(car,0x22)!=531;
}

static void* activeDriverVehicle(void*pad){
    if(pad!=reinterpret_cast<void*>(primaryPad)||at<short>(pad,0x10E)||!playing()||cutscene())return nullptr;
    auto*cj=player();
    if(!cj||!inVehicle(cj))return nullptr;
    auto*car=at<void*>(cj,0x58C);
    return vehicleRef(car)>=0&&at<void*>(car,0x460)==cj?car:nullptr;
}

static void* anyPlayerVehicle(void*pad){
    if(pad!=reinterpret_cast<void*>(primaryPad)||at<short>(pad,0x10E)||!playing()||cutscene())return nullptr;
    auto*cj=player();
    if(!cj||!inVehicle(cj))return nullptr;
    auto*car=at<void*>(cj,0x58C);
    return vehicleRef(car)>=0?car:nullptr;
}

static void sample(){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(sampledFrame==frame)return;
    sampledFrame=frame;
    XINPUT_STATE state{};
    connected=testInput||vehicle_nitro::readForegroundPad(state);
    input=testInput?testPad:state.Gamepad;
    auto*cj=player();
    void*car=cj&&inVehicle(cj)?at<void*>(cj,0x58C):nullptr;
    const int ref=vehicleRef(car);
    if(!car||ref<0){
        lowriderVehicleRef=-1;lowriderCameraMode=false;prevToggleKey=false;prevTogglePad=false;
        return;
    }
    if(ref!=lowriderVehicleRef){
        lowriderVehicleRef=ref;lowriderCameraMode=false;
    }
    if(!testInput&&vehicleHasHydraulics(car)&&at<void*>(car,0x460)==cj&&
       !at<short>(reinterpret_cast<void*>(primaryPad),0x10E)&&playing()&&!cutscene()){
        const auto*newKeys=reinterpret_cast<const short*>(0xB73190+0x18);
        const bool kDown=lowriderToggleKey&&newKeys[lowriderToggleKey]!=0;
        const bool pDown=connected&&lowriderTogglePad&&((input.wButtons&lowriderTogglePad)==lowriderTogglePad);
        if((kDown&&!prevToggleKey)||(pDown&&!prevTogglePad)){
            lowriderCameraMode=!lowriderCameraMode;
            const bool spanish=at<unsigned char>(reinterpret_cast<void*>(0xBA6748),0x84)==4;
            const char*msg=spanish?
                (lowriderCameraMode?"Lowrider: Camara normal (R3 mirar atras)":"Lowrider: Hidraulica activa"):
                (lowriderCameraMode?"Lowrider: Normal camera (R3 look behind)":"Lowrider: Hydraulics active");
            reinterpret_cast<void(__cdecl*)(const char*,bool,bool,bool)>(0x588BE0)(msg,true,false,false);
            event("lowrider_hydraulics_mode",ref,at<short>(car,0x22),lowriderCameraMode?"camera":"hydraulics");
        }
        prevToggleKey=kDown;prevTogglePad=pDown;
    }else{
        prevToggleKey=false;prevTogglePad=false;
    }
}

static void* armedDriverVehicle(void*pad){
    if(!driveByEnabled)return nullptr;
    auto*car=activeDriverVehicle(pad);
    if(!car)return nullptr;
    auto*cj=player();
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
    const bool padAim=connected&&(input.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
    const bool mouseAim=!testInput&&!at<short>(reinterpret_cast<void*>(primaryPad),0x10E)&&
        playing()&&!cutscene()&&mouse().rmb!=0;
    return padAim||mouseAim;
}

#include "DriveByAim.hpp"

static void beforeCameraProcess(void*camera){
    if(camera!=reinterpret_cast<void*>(mainCamera))return;
    sample();
    if(auto*car=activeDriverVehicle(reinterpret_cast<void*>(primaryPad));vehicleHasHydraulics(car)&&lowriderCameraMode){
        maskedHydraulicsCar=car;
        at<unsigned>(car,0x38C)&=~0x20000u;
    }
}

static void afterCameraProcess(void*){
    if(maskedHydraulicsCar){
        at<unsigned>(maskedHydraulicsCar,0x38C)|=0x20000u;
        maskedHydraulicsCar=nullptr;
    }
}

static short __fastcall fire(void*pad,void*){
    if(auto*car=armedDriverVehicle(pad)){
        sample();
        const bool padActive=connected&&(input.wButtons||input.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        if(padActive){
            const bool pressed=(input.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
            return pressed&&(!aimHeld()||hasAimView(car))?1:0;
        }
        if(!testInput&&(mouse().rmb||mouse().lmb))
            return mouse().lmb&&(!aimHeld()||hasAimView(car))?1:0;
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
    if(armedDriverVehicle(pad)&&aimHeld())return 0;
    if(auto*car=activeDriverVehicle(pad);vehicleHasHydraulics(car)&&lowriderCameraMode)return 0;
    return nativeVehicleAxisX(pad);
}

static short __fastcall vehicleAxisY(void*pad,void*){
    if(armedDriverVehicle(pad)&&aimHeld())return 0;
    if(auto*car=activeDriverVehicle(pad);vehicleHasHydraulics(car)&&lowriderCameraMode)return 0;
    return nativeVehicleAxisY(pad);
}

static bool __fastcall left(void*pad,void*){
    if(armedDriverVehicle(pad)){
        if(animationSide)return animationSide<0;
        if(aimHeld()||(connected&&(input.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)))return false;
    }
    return nativeLookLeft(pad);
}

static bool __fastcall right(void*pad,void*){
    if(armedDriverVehicle(pad)){
        if(animationSide)return animationSide>0;
        if(aimHeld()||(connected&&(input.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)))return false;
    }
    return nativeLookRight(pad);
}

static bool __fastcall lookBehindCar(void*pad,void*){
    auto*car=anyPlayerVehicle(pad);
    if(!car)return nativeLookBehindCar(pad);
    sample();
    const bool r3=connected&&(input.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)!=0;
    const bool l1r1=connected&&
        ((input.wButtons&(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER))==
         (XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER));
    if(r3)return !vehicleHasHydraulics(car)||lowriderCameraMode;
    if(l1r1||aimHeld())return false;
    return nativeLookBehindCar(pad)||(!testInput&&mouse().mmb!=0);
}

static bool __fastcall hydraulicJump(void*pad,void*){
    if(auto*car=activeDriverVehicle(pad);vehicleHasHydraulics(car)&&(lowriderCameraMode||aimHeld()))
        return false;
    return nativeHydraulicJump(pad);
}

static void __fastcall hydraulics(void*car,void*){
    // Hold the hydraulics' commanded position while aiming or when toggled to normal camera mode.
    if(car==activeDriverVehicle(reinterpret_cast<void*>(primaryPad))&&
       (aimHeld()||(vehicleHasHydraulics(car)&&lowriderCameraMode))){
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
    driveByEnabled=GetPrivateProfileIntW(L"VehicleLab",L"DriveByShoulderAim",1,ini.c_str())!=0;
    ivFootAimEnabled=GetPrivateProfileIntW(L"Camera",L"IvAim",1,ini.c_str())!=0;
    lowriderToggleKey=GetPrivateProfileIntW(L"VehicleLab",L"LowriderCameraToggleKey",lowriderToggleKey,ini.c_str());
    if(lowriderToggleKey>255)lowriderToggleKey='G';
    lowriderTogglePad=WORD(GetPrivateProfileIntW(L"VehicleLab",L"LowriderCameraTogglePadButton",lowriderTogglePad,ini.c_str())&0xFFFF);
    if(!driveByEnabled&&!ivFootAimEnabled)return true;
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
        {0x53FE70,reinterpret_cast<void*>(&lookBehindCar),reinterpret_cast<void**>(&nativeLookBehindCar)},
        {0x53FF70,reinterpret_cast<void*>(&hydraulicJump),reinterpret_cast<void**>(&nativeHydraulicJump)},
        {0x53FC50,reinterpret_cast<void*>(&vehicleAxisX),reinterpret_cast<void**>(&nativeVehicleAxisX)},
        {0x53FC10,reinterpret_cast<void*>(&vehicleAxisY),reinterpret_cast<void**>(&nativeVehicleAxisY)},
        {0x6A07A0,reinterpret_cast<void*>(&hydraulics),reinterpret_cast<void**>(&nativeHydraulics),6,{0x83,0xec,0x60,0x55,0x8b,0xe9}},
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
        onBeforeCameraProcess=&beforeCameraProcess;
        onAfterCameraProcess=&afterCameraProcess;
        for(const auto&hook:hooks)
            if(MH_QueueEnableHook(reinterpret_cast<void*>(hook.entry))!=MH_OK)installed=false;
        if(installed)return true;
    }
    for(unsigned i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(hooks[i].entry));
    return false;
}
}
