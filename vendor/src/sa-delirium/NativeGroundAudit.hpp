#pragma once
#include "CodePatch.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <Windows.h>

// Read-only observations at the verified native ground-height/velocity stores.
// No decision, velocity, support flag or final stored value is changed.
// 1.0 US source bytes checked against the actual project EXE. Include only
// in the Win32 adapter; installation is explicitly enabled by its caller.
namespace native_ground_audit {
struct Sample {
    void* ped{}; void* other{}; void* matrix{};
    uint32_t site{};
    float before{}, incoming{}, velocity[3]{}, groundHeight{};
    uint32_t flags46c{}, flags470{}, flags474{};
    void* standing{}; void* contact{};
    bool velocityStore{};float positionZ{};
};
using Observer=void(__cdecl*)(const Sample&);
inline Observer observer=nullptr;
template<class T> inline T read(void*p,uintptr_t offset){T value;std::memcpy(&value,static_cast<unsigned char*>(p)+offset,sizeof value);return value;}
inline void __cdecl dispatch(void*p,void*other,uint32_t site,float incoming) noexcept {
    if(!observer)return;
    Sample s;s.ped=p;s.other=other;s.site=site;s.incoming=incoming;
    s.matrix=read<void*>(p,0x14);
    // These native paths require a matrix. A null matrix remains an
    // original native error; the observer does not invent a fallback place.
    if(!s.matrix)return;
    s.positionZ=read<float>(s.matrix,0x38);
    s.velocityStore=site==0x5E30B4;
    s.before=s.velocityStore?read<float>(p,0x4c):s.positionZ;
    if(s.velocityStore)s.incoming=0.0f;
    std::memcpy(s.velocity,static_cast<unsigned char*>(p)+0x44,sizeof s.velocity);
    s.flags46c=read<uint32_t>(p,0x46c);s.flags470=read<uint32_t>(p,0x470);s.flags474=read<uint32_t>(p,0x474);
    s.standing=read<void*>(p,0x568);s.contact=read<void*>(p,0x584);s.groundHeight=read<float>(p,0x588);
    try{observer(s);}catch(...){/* Diagnostics cannot change native control flow. */}
}
inline uintptr_t continueGround=0x5E2C77,continueDirect=0x5E2C85,continueCeiling=0x5E31BF,continueVelocity=0x5E30BB;
#if defined(_MSC_VER) && defined(_M_IX86)
// Called with a site argument on the stack and a LIVE x87 value in ST(0).
// Save x87/SSE/MXCSR in an aligned 512-byte area before executing any C++;
// restore them plus all GPRs/EFLAGS before the displaced original store.
__declspec(naked) inline void tap(){
    __asm {
        pushfd
        pushad
        mov ecx,dword ptr [esp+40]
        sub esp,560
        mov ebp,esp
        lea edx,[esp+15]
        and edx,0FFFFFFF0h
        mov dword ptr [ebp+528],edx
        fxsave [edx]
        fst dword ptr [ebp+532]
        fninit
        push dword ptr [ebp+532]
        push ecx
        push ebx
        push esi
        call dispatch
        add esp,16
        mov edx,dword ptr [ebp+528]
        fxrstor [edx]
        mov esp,ebp
        add esp,560
        popad
        popfd
        ret 4
    }
}
__declspec(naked) inline void ground(){
    __asm {
        mov eax,dword ptr [esi+14h]
        push 05E2C71h
        call tap
        fstp dword ptr [eax+38h]
        jmp dword ptr [continueGround]
    }
}
__declspec(naked) inline void direct(){
    __asm {
        mov eax,dword ptr [esi+14h]
        push 05E2C7Fh
        call tap
        fstp dword ptr [eax+38h]
        jmp dword ptr [continueDirect]
    }
}
__declspec(naked) inline void ceiling(){
    __asm {
        // This operand belongs to the ORIGINAL stack, before any audit save.
        fsub dword ptr [esp+14h]
        push 05E31B8h
        call tap
        fstp dword ptr [ecx+38h]
        jmp dword ptr [continueCeiling]
    }
}
struct Patch {uintptr_t site;size_t length;std::array<unsigned char,7> original;void* branch;};
__declspec(naked) inline void velocity(){
    __asm {
        push 05E30B4h
        call tap
        // MOV does not alter EFLAGS or pop the live x87 comparison operand.
        mov dword ptr [esi+4Ch],0
        jmp dword ptr [continueVelocity]
    }
}
inline const std::array<Patch,4> patches{{
    {0x5E2C71,6,{0x8b,0x46,0x14,0xd9,0x58,0x38,0},reinterpret_cast<void*>(&ground)},
    {0x5E2C7F,6,{0x8b,0x46,0x14,0xd9,0x58,0x38,0},reinterpret_cast<void*>(&direct)},
    {0x5E31B8,7,{0xd8,0x64,0x24,0x14,0xd9,0x59,0x38},reinterpret_cast<void*>(&ceiling)},
    {0x5E30B4,7,{0xc7,0x46,0x4c,0,0,0,0},reinterpret_cast<void*>(&velocity)}
}};
inline bool install(Observer callback,uintptr_t& failedSite){
    failedSite=0;observer=nullptr;
    for(const auto&p:patches)if(std::memcmp(reinterpret_cast<void*>(p.site),p.original.data(),p.length)!=0){failedSite=p.site;return false;}
    size_t written=0;
    for(const auto&p:patches){
        DWORD old=0,unused=0;
        if(!VirtualProtect(reinterpret_cast<void*>(p.site),p.length,PAGE_EXECUTE_READWRITE,&old)){failedSite=p.site;break;}
        std::array<unsigned char,7> bytes{0xe9,0,0,0,0,0x90,0x90};
        const int32_t rel=static_cast<int32_t>(reinterpret_cast<uintptr_t>(p.branch)-(p.site+5));std::memcpy(bytes.data()+1,&rel,4);
        {
            // A thread parked at site+3 (second replaced instruction) would
            // resume inside the new JMP operand: freeze and require none inside.
            const std::array<CodeRange,1> range{{{p.site,p.site+p.length}}};
            FrozenThreads freeze(range);
            if(!freeze.ok()){failedSite=p.site;VirtualProtect(reinterpret_cast<void*>(p.site),p.length,old,&unused);break;}
            std::memcpy(reinterpret_cast<void*>(p.site),bytes.data(),p.length);
        }
        ++written;
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p.site),p.length)!=0;
        const bool protectedAgain=VirtualProtect(reinterpret_cast<void*>(p.site),p.length,old,&unused)!=0;
        if(!flushed||!protectedAgain){failedSite=p.site;break;}
    }
    if(written==patches.size()&&!failedSite){observer=callback;return true;}
    for(size_t i=0;i<written;i++){
        const auto&p=patches[i];DWORD old=0,unused=0;
        if(!VirtualProtect(reinterpret_cast<void*>(p.site),p.length,PAGE_EXECUTE_READWRITE,&old))continue;
        {const std::array<CodeRange,1> range{{{p.site,p.site+p.length}}};FrozenThreads freeze(range);std::memcpy(reinterpret_cast<void*>(p.site),p.original.data(),p.length);}
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p.site),p.length);
        VirtualProtect(reinterpret_cast<void*>(p.site),p.length,old,&unused);
    }
    return false;
}
#endif
}
