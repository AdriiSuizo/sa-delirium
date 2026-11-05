// SPDX-License-Identifier: GPL-3.0-only
// Native-Steam backend for Open Limit Adjuster's model-store contract.
// Reference: GTAmodding/III.VC.SA.LimitAdjuster e454bfaf (MIT), ModelStores.cpp.
// Copyright (c) 2014 ThirteenAG and LINK/2012; see OpenLimitAdjuster-MIT.txt.
// Unlike the old fixed-address ASI, this backend uses the owner's actual native
// constructor, ASLR-aware guards and reviewed lifecycle operands. No game code
// or object bytes are copied into the distribution.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstring>
#include <atomic>
#include <cstdlib>
#include <span>
#include <vector>
#include "../vendor/third-party/minhook/include/MinHook.h"
#include "../vendor/src/sa-delirium/CodePatch.hpp"
#include "../native/SteamHostIdentity.hpp"
#include "../native/SteamModelStoreBindings.hpp"

namespace {
constexpr unsigned modelIds=20000;
constexpr uintptr_t tableRva=0x712818;
using Add=void*(__cdecl*)(int);
struct Store {
    uintptr_t originalStoreRva,addRva,constructorRva,vtableRva;
    unsigned originalCapacity,elementSize;
    std::span<const steam_model_store::Reference> references;
    const std::array<unsigned char,32>& addHash;
    const std::array<unsigned char,32>& constructorHash;
    const char* addedEvent;
    const char* installedEvent;
    const char* cleanupEvent;
    bool secondRwPointer,preconstruct;
    std::byte* moved{};
    std::atomic_bool armed{};
    Add originalAdd{};
    unsigned constructions{},largestCount{};
};
Store damage{0x7936d0,0xd0850,0xcf610,0x4a57b4,70,0x24,
    steam_model_store::references,steam_model_store::addHash,steam_model_store::constructorHash,
    "damage_model_added","native_damage_store_installed","native_cleanup",true,false};
Store timed{0x7940d8,0xd08d0,0xcf6b0,0x4a5844,169,0x24,
    steam_time_model_store::references,steam_time_model_store::addHash,steam_time_model_store::constructorHash,
    "time_model_added","native_time_store_installed","native_time_cleanup",false,false};
Store atomic{0x7260c8,0xd0810,0xcf570,0x4a576c,14000,0x20,
    steam_atomic_model_store::references,steam_atomic_model_store::addHash,steam_atomic_model_store::constructorHash,
    "atomic_model_added","native_atomic_store_installed","native_atomic_cleanup",false,true};
std::byte* image{};
HANDLE trace=INVALID_HANDLE_VALUE;

void record(Store&s,const char*kind,unsigned count){
    if(trace==INVALID_HANDLE_VALUE)return;
    char text[256];const int n=sprintf_s(text,"%s count=%u capacity=%u element_size=%u constructed=%u native_init=1 native_shutdown_refs=%zu\r\n",kind,count,modelIds,s.elementSize,s.constructions,s.references.size());
    DWORD written{};if(n>0)WriteFile(trace,text,DWORD(n),&written,nullptr);
}
void recordStoreFinalState(Store&s){
    if(!s.armed.load()||!s.moved||trace==INVALID_HANDLE_VALUE)return;
    unsigned live=0;
    for(unsigned i=0;i<s.largestCount;i++){
        const auto*p=s.moved+4+std::size_t(i)*s.elementSize;
        if(*reinterpret_cast<void*const*>(p+0x1c)||(s.secondRwPointer&&*reinterpret_cast<void*const*>(p+0x20)))++live;
    }
    record(s,"process_detach",*reinterpret_cast<unsigned*>(s.moved));
    char text[160];DWORD written{};const int n=sprintf_s(text,"%s count=%u constructed=%u live_rw_objects=%u\r\n",s.cleanupEvent,*reinterpret_cast<unsigned*>(s.moved),s.constructions,live);
    if(n>0)WriteFile(trace,text,DWORD(n),&written,nullptr);FlushFileBuffers(trace);
}
[[noreturn]] void reject(Store&s,const wchar_t* reason){
    record(s,"model_store_rejected",s.moved?*reinterpret_cast<unsigned*>(s.moved):0);
    MessageBoxW(nullptr,reason,L"Delirium SA · Native model storage",MB_OK|MB_ICONERROR);
    ExitProcess(ERROR_BAD_FORMAT);
}
void* addModel(Store&s,int index){
    if(!s.armed.load(std::memory_order_acquire))return s.originalAdd(index);
    auto&count=*reinterpret_cast<unsigned*>(s.moved);
    if(index<0||unsigned(index)>=modelIds||count>=modelIds)
        reject(s,L"Una definición de modelo queda fuera de la tabla de modelos de este ejecutable. Consulta el registro de modelos.");
    auto*slot=s.moved+4+std::size_t(count)*s.elementSize;
    if(!*reinterpret_cast<void**>(slot)){
        // The original CRT still constructs its untouched static store. Our
        // new zeroed slots use the SAME real native class constructor.
        reinterpret_cast<void*(__thiscall*)(void*)>(image+s.constructorRva)(slot);
        if(*reinterpret_cast<void**>(slot)!=image+s.vtableRva)
            reject(s,L"El constructor nativo no produjo el tipo de modelo esperado.");
        ++s.constructions;
    }
    ++count; // Same order as the corresponding native AddModel routine.
    const auto methods=*reinterpret_cast<void***>(slot);
    reinterpret_cast<void(__thiscall*)(void*)>(methods[6])(slot);
    reinterpret_cast<void**>(image+tableRva)[index]=slot;
    if(count>s.largestCount){s.largestCount=count;if(count==1||count>=s.originalCapacity)record(s,s.addedEvent,count);}
    return slot;
}
void* __cdecl addDamageModel(int index){return addModel(damage,index);}
void* __cdecl addTimeModel(int index){return addModel(timed,index);}
void* __cdecl addAtomicModel(int index){return addModel(atomic,index);}
void recordFinalState(){recordStoreFinalState(damage);recordStoreFinalState(timed);recordStoreFinalState(atomic);}

}

static bool installStore(Store&s,void*hookFunction,std::ostream&log){
    if(s.armed.load())return true;
    image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if(!sa_steam_host_identity::supported()||
       !sa_steam_host_identity::prefixMatches(uintptr_t(image+s.addRva),s.addHash)||
       !sa_steam_host_identity::prefixMatches(uintptr_t(image+s.constructorRva),s.constructorHash))return false;
    for(const auto&r:s.references){
        if(*reinterpret_cast<uintptr_t*>(image+r.operandRva)!=uintptr_t(image+s.originalStoreRva+r.delta)||
           !sa_steam_host_identity::prefixMatches(uintptr_t(image+r.instructionRva),r.hash))return false;
    }
    const auto n=s.references.size();
    std::vector<std::array<unsigned char,7>> originalPrefixes(n);
    for(std::size_t i=0;i<n;i++)std::memcpy(originalPrefixes[i].data(),image+s.references[i].instructionRva,7);
    if(*reinterpret_cast<unsigned*>(image+s.originalStoreRva)!=0){log<<"model_store_install_too_late\n";return false;}
    // Stable addresses for the model table's actual maximum ID count; this is
    // not an arbitrary increased bound. One prefix page preserves safe -1 reads.
    const std::size_t bytes=0x1000+4+std::size_t(modelIds)*s.elementSize;
    auto*allocation=static_cast<std::byte*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!allocation)return false;s.moved=allocation+0x1000;
    // Native Initialise creates its fixed atomic placeholders directly in the
    // array, bypassing AddAtomicModel. They need genuine preconstructed slots.
    if(s.preconstruct){
        for(unsigned i=0;i<modelIds;i++){
            auto*slot=s.moved+4+std::size_t(i)*s.elementSize;
            reinterpret_cast<void*(__thiscall*)(void*)>(image+s.constructorRva)(slot);
            if(*reinterpret_cast<void**>(slot)!=image+s.vtableRva){VirtualFree(allocation,0,MEM_RELEASE);s.moved=nullptr;return false;}
        }
        s.constructions=modelIds;
    }
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){VirtualFree(allocation,0,MEM_RELEASE);s.moved=nullptr;return false;}
    const auto target=image+s.addRva;
    if(MH_CreateHook(target,hookFunction,reinterpret_cast<void**>(&s.originalAdd))!=MH_OK){VirtualFree(allocation,0,MEM_RELEASE);s.moved=nullptr;return false;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);VirtualFree(allocation,0,MEM_RELEASE);s.moved=nullptr;return false;}
    // While unarmed, the detour delegates unchanged to the native allocator.
    // All allocations/hook setup occur BEFORE freezing threads.
    HMODULE own{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&installStore),&own)){
        MH_DisableHook(target);MH_RemoveHook(target);VirtualFree(allocation,0,MEM_RELEASE);s.moved=nullptr;return false;
    }
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(own);
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const std::byte*>(own)+dos->e_lfanew);
    std::vector<CodeRange> ranges(n+3);
    for(std::size_t i=0;i<n;i++){const auto&r=s.references[i];ranges[i]={uintptr_t(image+r.instructionRva),uintptr_t(image+r.instructionRva+r.instructionSize)};}
    ranges[n]={uintptr_t(target),uintptr_t(target+0x31)};
    ranges[n+1]={uintptr_t(s.originalAdd),uintptr_t(s.originalAdd)+64};
    ranges[n+2]={uintptr_t(own),uintptr_t(own)+nt->OptionalHeader.SizeOfImage};
    bool installed=false;unsigned protectedCount=0;
    std::vector<DWORD> protections(n);
    {
        FrozenThreads freeze(ranges.data(),ranges.size());
        if(freeze.ok()&&*reinterpret_cast<unsigned*>(image+s.originalStoreRva)==0){
            bool valid=true;
            for(std::size_t i=0;i<n;i++){
                const auto&r=s.references[i];
                valid=valid&&*reinterpret_cast<uintptr_t*>(image+r.operandRva)==uintptr_t(image+s.originalStoreRva+r.delta)&&
                    std::memcmp(image+r.instructionRva,originalPrefixes[i].data(),7)==0;
            }
            if(valid){
                for(std::size_t i=0;i<n;i++){
                    const auto&r=s.references[i];
                    auto*site=image+r.operandRva;DWORD protection{};
                    if(!VirtualProtect(site,4,PAGE_EXECUTE_READWRITE,&protection))break;
                    protections[i]=protection;++protectedCount;
                }
                if(protectedCount==n){
                    for(const auto&r:s.references){
                        const auto replacement=uintptr_t(s.moved+r.delta);std::memcpy(image+r.operandRva,&replacement,4);
                    }
                    installed=FlushInstructionCache(GetCurrentProcess(),image+0xd0000,0x1000)!=0;
                    if(!installed){
                        for(const auto&r:s.references){
                            const auto prior=uintptr_t(image+s.originalStoreRva+r.delta);std::memcpy(image+r.operandRva,&prior,4);
                        }
                        FlushInstructionCache(GetCurrentProcess(),image+0xd0000,0x1000);
                    }
                }
                // Restore shared-page permissions in reverse order. All writes
                // were done only after every operand was writable.
                for(unsigned i=protectedCount;i-->0;){DWORD ignored{};VirtualProtect(image+s.references[i].operandRva,4,protections[i],&ignored);}
                if(installed)s.armed.store(true,std::memory_order_release);
            }
        }
    }
    if(!installed){MH_DisableHook(target);MH_RemoveHook(target);VirtualFree(allocation,0,MEM_RELEASE);s.moved=nullptr;log<<"model_store_relocation_not_committed\n";return false;}
    record(s,s.installedEvent,0);
    log<<s.installedEvent<<" capacity="<<modelIds<<" native_constructor=1 lifecycle_refs="<<n<<" original_crt_store_preserved=1\n"<<std::flush;
    return true;
}

bool installSteamModelStores(const std::filesystem::path&root,std::ostream&log){
    if(damage.armed.load()&&timed.armed.load()&&atomic.armed.load())return true;
    if(trace==INVALID_HANDLE_VALUE){
        trace=CreateFileW((root/L"SAStandalone-model-stores.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(std::atexit(recordFinalState)!=0)log<<"model_store_exit_observer_unavailable\n";
    }
    if(!installStore(damage,reinterpret_cast<void*>(&addDamageModel),log))return false;
    if(!installStore(timed,reinterpret_cast<void*>(&addTimeModel),log))return false;
    return installStore(atomic,reinterpret_cast<void*>(&addAtomicModel),log);
}
