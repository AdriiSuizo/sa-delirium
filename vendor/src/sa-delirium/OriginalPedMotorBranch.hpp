#pragma once
#include <cstdint>
// 1.0 US EXE SHA256 f01a00ce950fa40ca1ed59df0e789848c6edcf6405456274965885d0929343ac.
// Interior ordinary-XY branch of CPed::UpdatePosition, NOT its entry (the entry
// is already owned by CombatRuntime). No installer or executable writes here.
// The replaced instruction is exactly D9 86 50 05 00 00 at 005E1E4C.
// Every route here has an empty x87 stack, ESI=ped, saved EDI/ESI and 0x4C locals.
// The successful callback only captures motor facts; it must never write vXY.
namespace original_ped_motor_branch {
inline bool(__cdecl* capture)(void*)=nullptr;
inline uintptr_t nativeContinue=0x5E1E52;
inline uintptr_t ordinaryExit=0x5E1F99;
inline constexpr uintptr_t site=0x5E1E4C;
inline constexpr unsigned char expected[6]={0xD9,0x86,0x50,0x05,0x00,0x00};
inline bool __cdecl dispatch(void*p){return capture&&capture(p);}
#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) static void branch(){
    __asm {
        pushfd
        pushad
        push esi
        call dispatch
        add esp,4
        test al,al
        jz fallback
        popad
        popfd
        jmp dword ptr [ordinaryExit]
    fallback:
        popad
        popfd
        fld dword ptr [esi+550h]
        jmp dword ptr [nativeContinue]
    }
}
#endif
}
