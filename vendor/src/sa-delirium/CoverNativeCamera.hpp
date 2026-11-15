#pragma once
#include <cfloat>

// Observe SA's aim placement before native collision shortens its one shoulder.
// The native collision call remains authoritative and is never changed here.
// Only the opposite cover shoulder may consume this point, followed by its own
// existing near-plane sweep. No camera distance is inferred from CCam fields.
namespace cover_native_camera {
constexpr uintptr_t collisionAddress=0x520190;
constexpr uintptr_t aimCallSite=0x522A67;
constexpr uintptr_t aimReturnAddress=0x522A77;
constexpr uintptr_t mainCameraAddress=0xB6F028;
struct Sample {
    void* cam=nullptr;
    int ref=-1;
    unsigned frame=0;
    V beforeSource{},beforeTarget{},afterSource{},afterTarget{};
    bool valid=false;
};
static Sample sample;
static bool installed=false;
static void(__thiscall* nativeCollision)(void*,V*,V*)=nullptr;

static bool samePoint(V a,V b){
    if(!finite(a)||!finite(b))return false;
    const auto same=[](float x,float y){
        return std::abs(x-y)<=4.f*FLT_EPSILON*std::max(1.f,std::max(std::abs(x),std::abs(y)));
    };
    return same(a.x,b.x)&&same(a.y,b.y)&&same(a.z,b.z);
}
static bool unoccludedSource(void* cam,int ref,unsigned frame,V nativeSource,V poseSource,V& source){
    if(!installed||!sample.valid||sample.cam!=cam||sample.ref!=ref||sample.frame!=frame||
       !samePoint(sample.afterSource,nativeSource)||!samePoint(sample.afterSource,poseSource)||
       !samePoint(sample.beforeTarget,sample.afterTarget)||!finite(sample.beforeSource))return false;
    source=sample.beforeSource;
    return true;
}
static void __fastcall observeCollision(void* camera,void*,V* source,V* target){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto* cam=activeCamera();auto* ped=player();
    const bool selected=caller==aimReturnAddress&&
        reinterpret_cast<uintptr_t>(camera)==mainCameraAddress&&cam&&
        source==reinterpret_cast<V*>(reinterpret_cast<unsigned char*>(cam)+0x19C)&&
        target&&player_cover::owns(ped);
    Sample observed;
    if(selected){
        observed.cam=cam;observed.ref=reference(ped);
        observed.frame=*reinterpret_cast<const unsigned*>(0xB7CB4C);
        observed.beforeSource=*source;observed.beforeTarget=*target;
    }
    // Foreign callers pass through identically; record only the scoped call.
    nativeCollision(camera,source,target);
    if(selected){
        observed.afterSource=*source;observed.afterTarget=*target;
        observed.valid=finite(observed.beforeSource)&&finite(observed.beforeTarget)&&
            finite(observed.afterSource)&&finite(observed.afterTarget);
        sample=observed;
    }
}
static bool install(void* entry=reinterpret_cast<void*>(collisionAddress),
                    const void* caller=reinterpret_cast<const void*>(aimCallSite)){
    if(installed)return true;
    // Exact verified 1.0 US entry and Process_AimWeapon call. A changed entry,
    // alternate version or another hook disables only this optional correction.
    constexpr unsigned char signature[]={0x83,0xec,0x24,0x53,0x55,0x8b,0x6c,0x24,0x30,0xd9,0x45,0,0x57,0x8b,0x7c,0x24};
    constexpr unsigned char call[]={0x8d,0x4c,0x24,0x38,0x51,0x53,0xb9,0x28,0xf0,0xb6,0,0xe8,0x19,0xd7,0xff,0xff};
    if(std::memcmp(entry,signature,sizeof(signature))||std::memcmp(caller,call,sizeof(call)))return false;
    if(MH_CreateHook(entry,reinterpret_cast<void*>(&observeCollision),
                     reinterpret_cast<void**>(&nativeCollision))!=MH_OK)return false;
    // Keep an optional enable failure out of the shared core hook transaction.
    if(MH_EnableHook(entry)!=MH_OK){MH_RemoveHook(entry);nativeCollision=nullptr;return false;}
    installed=true;
    event("cover_native_camera_capture_enabled",-1,-1,"aim_return=0x522A77 native_collision_unchanged=1");
    return true;
}
}
