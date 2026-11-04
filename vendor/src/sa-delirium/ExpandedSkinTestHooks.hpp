#pragma once
#include "ExpandedSkinTestHost.hpp"
// Native hooks are queued only under BOTH owned-process gates. Parent's normal
// MH_ApplyQueued transaction activates them. Entries modified by another mod
// are rejected intact. No hooks or native calls happen on header inclusion.
namespace expandedSkinTest {
inline bool environmentGate(){char value[8]{};return GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&
    GetEnvironmentVariableA("GTASA_TEST_EXPANDED_SKIN",value,sizeof value)==1&&value[0]=='1';}
inline Manager*activeManager=nullptr;
inline Draw originalAtomicDraw=nullptr;
inline Visibility originalVisibility=nullptr;
inline Atomic*__cdecl atomicDrawHook(Atomic*a){
    Atomic*result=nullptr;
    if(activeManager&&activeManager->draw(a,originalAtomicDraw,result))return result;
    return originalAtomicDraw(a);
}
inline int __cdecl visibilityHook(void*entity,float*distance){
    if(activeManager)activeManager->beforeVisibility(entity);
    return originalVisibility(entity,distance);
}
struct HookApi {
    bool(*create)(void*,void*,void**)=nullptr;
    bool(*remove)(void*)=nullptr;
    bool(*queueEnable)(void*)=nullptr;
    expandedSkinCapture::Read read=nullptr;
};
inline bool queueTestHooks(const HookApi&api,bool ownedGate){
    if(!ownedGate)return true;
    if(!api.create||!api.remove||!api.queueEnable||!api.read||originalAtomicDraw||originalVisibility||
       !verifyHostAbi(api.read)||!expandedGpu::verifyNativeAbi(api.read))return false;
    auto*draw=reinterpret_cast<void*>(0x7491C0);auto*visibility=reinterpret_cast<void*>(0x554230);
    if(!api.create(draw,reinterpret_cast<void*>(&atomicDrawHook),reinterpret_cast<void**>(&originalAtomicDraw)))return false;
    if(!api.create(visibility,reinterpret_cast<void*>(&visibilityHook),reinterpret_cast<void**>(&originalVisibility))){
        const bool rolledBack=api.remove(draw);if(rolledBack)originalAtomicDraw=nullptr;return false;
    }
    if(!api.queueEnable(draw)||!api.queueEnable(visibility)){
        const bool b=api.remove(visibility),a=api.remove(draw);if(b)originalVisibility=nullptr;if(a)originalAtomicDraw=nullptr;return false;
    }
    return true;
}
// Release Manager/streams BEFORE RW shutdown, not from DllMain. Until then a
// null manager leaves both wrappers exact pass-through, each original once.
inline void detachTestManager(){activeManager=nullptr;}
} // namespace expandedSkinTest
