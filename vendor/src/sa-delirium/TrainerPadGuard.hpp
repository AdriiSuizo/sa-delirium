#pragma once
// Grinch Trainer 1.0 redirects the CPad::GetPad call of a script button
// opcode handler (0x48AEEC) to a hook of its own (+0x329C0). The hook calls the
// original it keeps at [object+0x18] of a static hook object (+0x370304); on
// shop menus that pointer can still be null and the call lands at EIP 0. The
// trainer binary is shipped unmodified, so the call site is pointed at
// guardedGetPad, which enters the trainer's hook only once its original is
// known and otherwise calls the game's CPad::GetPad (0x53FB70).
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace trainer_pad_guard {
constexpr uintptr_t CallSite=0x48AEEC,GetPad=0x53FB70;
constexpr uintptr_t HookRva=0x329C0,HookObjectRva=0x370304,OriginalCheckRva=0x32A59;
constexpr std::uint8_t HookBytes[8]{0x55,0x8B,0xEC,0x8D,0x45,0x08,0x50,0xE8};
constexpr std::uint8_t OriginalCheckBytes[10]{0x83,0x7F,0x20,0x00,0x75,0x0F,0xFF,0x33,0x8B,0x47};
using GetPadFn=void*(__cdecl*)(int);
inline uintptr_t trainerBase=0;

inline void* __cdecl guardedGetPad(int pad){
    const auto object=*reinterpret_cast<const uintptr_t*>(trainerBase+HookObjectRva);
    if(object&&*reinterpret_cast<const uintptr_t*>(object+0x18))
        return reinterpret_cast<GetPadFn>(trainerBase+HookRva)(pad);
    return reinterpret_cast<GetPadFn>(GetPad)(pad);
}
inline uintptr_t callTarget(){
    const auto* site=reinterpret_cast<const std::uint8_t*>(CallSite);
    if(site[0]!=0xE8)return 0;
    std::int32_t rel;std::memcpy(&rel,site+1,4);return CallSite+5+rel;
}
// Main thread, between frames: nothing executes the opcode handler meanwhile.
// Returns true once, when the call site is pointed at the guard.
inline bool maintain(){
    const uintptr_t target=callTarget();
    if(!target||target==reinterpret_cast<uintptr_t>(&guardedGetPad)||target==GetPad)return false;
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(target),&module))return false;
    const auto base=reinterpret_cast<uintptr_t>(module);
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);
    const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
    if(target!=base+HookRva||_wcsicmp(name,L"GrinchTrainerSA.asi")!=0)return false;
    if(std::memcmp(reinterpret_cast<const void*>(target),HookBytes,sizeof(HookBytes))!=0||std::memcmp(reinterpret_cast<const void*>(base+OriginalCheckRva),OriginalCheckBytes,sizeof(OriginalCheckBytes))!=0)return false;
    trainerBase=base;
    auto* site=reinterpret_cast<std::uint8_t*>(CallSite);DWORD old=0;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old))return false;
    const std::int32_t rel=static_cast<std::int32_t>(reinterpret_cast<uintptr_t>(&guardedGetPad)-(CallSite+5));
    std::memcpy(site+1,&rel,4);
    VirtualProtect(site,5,old,&old);FlushInstructionCache(GetCurrentProcess(),site,5);
    return true;
}
}
