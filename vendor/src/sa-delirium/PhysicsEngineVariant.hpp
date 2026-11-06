#pragma once
// delirium_physics.dll is delay-loaded (build.cmd /DELAYLOAD). The first
// engine call (setup thread, never DllMain) picks the build for this CPU:
// delirium_physics_avx2.dll is the same source compiled with AVX2/FMA/BMI
// (tools/Build-Deploy-Physics.ps1); four thrown bodies step 1.8x faster
// (examples/bench_group.rs: 1.14 -> 0.64 ms). CPUs or Windows versions
// without those features, or a missing file, keep the baseline DLL.
// The distribution names its engine sa_delirium_bootstrap.dll and keeps it
// beside this ASI in scripts/ (release-work Prepare-DevelopmentRuntime.py
// defines both names); each is looked for there first, then beside the game.
#include <windows.h>
#include <delayimp.h>
#include <intrin.h>
#include <cstring>

#ifndef DELIRIUM_ENGINE_DLL
#define DELIRIUM_ENGINE_DLL "delirium_physics.dll"
#define DELIRIUM_ENGINE_AVX2_DLL "delirium_physics_avx2.dll"
#endif

namespace physics_engine_variant {
inline const char* chosen="baseline";
inline HMODULE loaded=nullptr;

inline bool cpuHasAvx2Build(){
    int r[4]{};__cpuid(r,0);if(r[0]<7)return false;
    __cpuid(r,1);const unsigned ecx1=unsigned(r[2]);
    const bool fma=ecx1&(1u<<12),movbe=ecx1&(1u<<22),popcnt=ecx1&(1u<<23),osxsave=ecx1&(1u<<27),avx=ecx1&(1u<<28),f16c=ecx1&(1u<<29),sse42=ecx1&(1u<<20);
    if(!(fma&&movbe&&popcnt&&osxsave&&avx&&f16c&&sse42))return false;
    // The OS must save the YMM state (XCR0 bits 1 and 2).
    if((_xgetbv(0)&6)!=6)return false;
    __cpuidex(r,7,0);const unsigned ebx7=unsigned(r[1]);
    const bool bmi1=ebx7&(1u<<3),avx2=ebx7&(1u<<5),bmi2=ebx7&(1u<<8);
    __cpuid(r,0x80000000);if(unsigned(r[0])<0x80000001u)return false;
    __cpuid(r,0x80000001);const bool lzcnt=unsigned(r[2])&(1u<<5);
    return bmi1&&avx2&&bmi2&&lzcnt;
}

// The file beside this ASI, else beside the game executable.
inline HMODULE loadBeside(const char* file){
    HMODULE self=nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&loadBeside),&self);
    const HMODULE folders[]{self,nullptr};
    for(HMODULE folder:folders){
        char path[MAX_PATH]{};DWORD n=GetModuleFileNameA(folder,path,MAX_PATH);
        while(n&&path[n-1]!='\\')n--;
        if(!n||n+std::strlen(file)>=MAX_PATH)continue;
        std::strcpy(path+n,file);
        if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES)continue;
        if(HMODULE m=LoadLibraryA(path))return m;
    }
    return nullptr;
}

inline FARPROC WINAPI notify(unsigned reason,PDelayLoadInfo info){
    if(reason!=dliNotePreLoadLibrary||!info||!info->szDll||_stricmp(info->szDll,DELIRIUM_ENGINE_DLL)!=0)return nullptr;
    if(cpuHasAvx2Build())if(HMODULE m=loadBeside(DELIRIUM_ENGINE_AVX2_DLL)){chosen="avx2";loaded=m;return reinterpret_cast<FARPROC>(m);}
    loaded=loadBeside(DELIRIUM_ENGINE_DLL);
    return reinterpret_cast<FARPROC>(loaded);
}

// Loads the engine now (if no engine call has yet) and returns its module,
// whichever variant it is; nullptr if it could not be loaded.
inline HMODULE load(){
    if(FAILED(__HrLoadAllImportsForDll(DELIRIUM_ENGINE_DLL)))return nullptr;
    return loaded?loaded:GetModuleHandleA(DELIRIUM_ENGINE_DLL);
}
}
extern "C" const PfnDliHook __pfnDliNotifyHook2=physics_engine_variant::notify;
