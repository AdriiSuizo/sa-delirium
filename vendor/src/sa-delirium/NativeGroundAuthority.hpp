#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

// Explicit per-frame authority for the opt-in original normal-body support.
// No executable writes or game globals here. Adapter owns the complete
// collision-coverage ledger, entity references, staging and atomic commit.
namespace native_ground_authority {
struct Identity {
    uintptr_t ped{},clump{},matrix{};int reference=-1,model=-1;uint32_t frame{};
};
inline bool same(const Identity&a,const Identity&b){return a.ped==b.ped&&a.clump==b.clump&&a.matrix==b.matrix&&a.reference==b.reference&&a.model==b.model&&a.frame==b.frame;}
struct Facts {
    Identity identity;
    bool testGate{},ordinaryOnFoot{},grounded{},alive{},nativePairReady{},sourceReady{};
    bool worldCoverageComplete{},finiteCohortComplete{},metadataComplete{},stageCapacityReady{};
    bool naturalMotion{},recovering{},inVehicle{},attached{},swimming{},climbing{},jumping{},fallTask{},submerged{};
};
inline bool ordinary(const Facts&f){return f.ordinaryOnFoot&&f.alive&&!f.naturalMotion&&!f.recovering&&!f.inVehicle&&!f.attached&&!f.swimming&&!f.climbing&&!f.jumping&&!f.fallTask&&!f.submerged;}
inline bool ready(const Facts&f){return f.testGate&&ordinary(f)&&f.grounded&&f.nativePairReady&&f.sourceReady&&f.worldCoverageComplete&&f.finiteCohortComplete&&f.metadataComplete&&f.stageCapacityReady&&f.identity.ped&&f.identity.clump&&f.identity.matrix&&f.identity.reference>=0&&f.identity.model>=0;}
enum class Decision {Native,Shared,Rejected};
enum class Coverage {Unknown,VerifiedStaticMesh,StagedFiniteOwner};
struct Lease {
    Identity identity{};bool reserved{},invalid{},claimed{},initialGrounded{};
    unsigned gravityCalls{},collisionCalls{};
    bool reserve(const Facts&f){
        *this={};if(!ready(f))return false;
        identity=f.identity;initialGrounded=f.grounded;reserved=true;return true;
    }
    Decision current(const Facts&f){
        if(!reserved)return Decision::Native;
        if(invalid)return Decision::Rejected;
        if(!same(identity,f.identity)||!f.testGate||!ordinary(f)||!f.sourceReady||!f.nativePairReady){
            // Before any native branch was suppressed, ordinary fallback is
            // safe. Afterwards this interval must fail/roll back as a whole;
            // never execute half native support and half original support.
            if(gravityCalls||collisionCalls||claimed){invalid=true;return Decision::Rejected;}
            reserved=false;return Decision::Native;
        }
        return Decision::Shared;
    }
    Decision gravity(const Facts&f){const auto d=current(f);if(d==Decision::Shared)++gravityCalls;return d;}
    Decision collision(const Facts&f,Coverage coverage){
        const auto d=current(f);if(d!=Decision::Shared)return d;
        if(coverage==Coverage::Unknown){invalid=true;return Decision::Rejected;}
        ++collisionCalls;return Decision::Shared;
    }
    bool claim(const Facts&f){
        if(current(f)!=Decision::Shared||!f.worldCoverageComplete||!f.finiteCohortComplete||!f.metadataComplete||!f.stageCapacityReady){
            if(reserved)invalid=true;return false;
        }
        claimed=true;return true;
    }
    bool canCommit(const Facts&f,bool sourcePlacementValid){return claimed&&!invalid&&current(f)==Decision::Shared&&sourcePlacementValid;}
};

// Verified SA CPed flags46C bits0/1 (plugin-sdk and5E30AB). Only this mapping
// belongs to ordinary support. Do not clear jumping, landing, swimming,
// phone/task or animation bits to keep an actor eligible for a claim.
inline uint32_t standingFlags(uint32_t flags,bool previous,bool grounded){return(flags&~3u)|(grounded?1u:0u)|(previous?2u:0u);}
inline bool nativeGravityEligible(uint32_t physical,uint8_t entityFlags,uint8_t entityTypeStatus,bool attached){
    return (physical&2)!=0&&(physical&(32|64|128))==0&&(entityFlags&1)!=0&&(entityTypeStatus>>3)!=2&&!attached;
}
struct Snapshot {
    Identity identity{};float position[3]{},velocity[3]{},groundHeight{};
    uint32_t physical40{},ped46c{},ped470{},ped474{};uintptr_t standing{},contact{};
};
template<class T>inline T read(uintptr_t base,size_t offset){T value;std::memcpy(&value,reinterpret_cast<const void*>(base+offset),sizeof value);return value;}
// Caller must first verify the native pool generation/clump. Captures facts,
// not ownership, and never invents a replacement for a missing native matrix.
inline bool capture(const Identity&id,Snapshot&out){
    if(!id.ped||!id.matrix)return false;
    out={};out.identity=id;std::memcpy(out.position,reinterpret_cast<void*>(id.matrix+0x30),12);std::memcpy(out.velocity,reinterpret_cast<void*>(id.ped+0x44),12);
    for(float v:out.position)if(!std::isfinite(v))return false;for(float v:out.velocity)if(!std::isfinite(v))return false;
    out.physical40=read<uint32_t>(id.ped,0x40);out.ped46c=read<uint32_t>(id.ped,0x46c);out.ped470=read<uint32_t>(id.ped,0x470);out.ped474=read<uint32_t>(id.ped,0x474);
    out.groundHeight=read<float>(id.ped,0x588);out.standing=read<uint32_t>(id.ped,0x568);out.contact=read<uint32_t>(id.ped,0x584);return true;
}

// CPhysical::ProcessControl5485E0 has ONE verified direct CALL to ApplyGravity.
// Redirect that complete call at548661; leave542FE0's function bytes intact.
// Expected call E8 7A A9 FF FF ->542FE0; following code starts548666 (pop edi).
inline constexpr uintptr_t gravityCallSite=0x548661;
inline constexpr unsigned char gravityCallBytes[5]={0xE8,0x7A,0xA9,0xFF,0xFF};
inline uintptr_t nativeGravity=0x542FE0;
#if defined(_MSC_VER) && defined(_M_IX86)
using GravityPredicate=bool(__cdecl*)(void*) noexcept;
inline GravityPredicate suppressGravity=nullptr;
inline bool __cdecl dispatchGravity(void*p) noexcept{return suppressGravity&&suppressGravity(p);}
// The host predicate must record Rejected as a failed interval and return
// true, never silently resume native gravity after another branch was skipped.
// Native fallback tail-calls the untouched original thiscall function.
__declspec(naked) inline void gravityBranch(){
    __asm {
        pushfd
        pushad
        sub esp,560
        mov ebp,esp
        lea edx,[esp+15]
        and edx,0FFFFFFF0h
        mov dword ptr [ebp+528],edx
        fxsave [edx]
        fninit
        push ecx
        call dispatchGravity
        add esp,4
        mov byte ptr [ebp+532],al
        mov edx,dword ptr [ebp+528]
        fxrstor [edx]
        cmp byte ptr [ebp+532],0
        je nativePath
        mov esp,ebp
        add esp,560
        popad
        popfd
        ret
    nativePath:
        mov esp,ebp
        add esp,560
        popad
        popfd
        jmp dword ptr [nativeGravity]
    }
}
#endif
}
