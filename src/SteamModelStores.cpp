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
#include "../vendor/third-party/minhook/include/MinHook.h"
#include "../vendor/src/sa-delirium/CodePatch.hpp"
#include "../native/SteamHostIdentity.hpp"
#include "../native/SteamModelStoreBindings.hpp"

namespace {
constexpr unsigned modelIds=20000,elementSize=0x24;
constexpr uintptr_t originalStoreRva=0x7936d0,tableRva=0x712818,addRva=0xd0850,constructorRva=0xcf610,vtableRva=0x4a57b4;
std::byte* image{};
std::byte* moved{};
std::atomic_bool armed{};
using Add=void*(__cdecl*)(int);
Add originalAdd{};
HANDLE trace=INVALID_HANDLE_VALUE;
unsigned constructions{},largestCount{};

void record(const char*kind,unsigned count){
    if(trace==INVALID_HANDLE_VALUE)return;
    char text[256];const int n=sprintf_s(text,"%s count=%u capacity=%u element_size=%u constructed=%u native_init=1 native_shutdown_refs=5\r\n",kind,count,modelIds,elementSize,constructions);
    DWORD written{};if(n>0)WriteFile(trace,text,DWORD(n),&written,nullptr);
}
void recordFinalState(){
    if(!armed.load()||!moved||trace==INVALID_HANDLE_VALUE)return;
    unsigned live=0;
    for(unsigned i=0;i<largestCount;i++){
        const auto*p=moved+4+std::size_t(i)*elementSize;
        if(*reinterpret_cast<void*const*>(p+0x1c)||*reinterpret_cast<void*const*>(p+0x20))++live;
    }
    record("process_detach",*reinterpret_cast<unsigned*>(moved));
    char text[160];DWORD written{};const int n=sprintf_s(text,"native_cleanup count=%u constructed=%u live_rw_objects=%u\r\n",*reinterpret_cast<unsigned*>(moved),constructions,live);
    if(n>0)WriteFile(trace,text,DWORD(n),&written,nullptr);FlushFileBuffers(trace);
}
[[noreturn]] void reject(const wchar_t* reason){
    record("model_store_rejected",moved?*reinterpret_cast<unsigned*>(moved):0);
    MessageBoxW(nullptr,reason,L"Delirium SA · Native model storage",MB_OK|MB_ICONERROR);
    ExitProcess(ERROR_BAD_FORMAT);
}
void* __cdecl addDamageModel(int index){
    if(!armed.load(std::memory_order_acquire))return originalAdd(index);
    auto&count=*reinterpret_cast<unsigned*>(moved);
    if(index<0||unsigned(index)>=modelIds||count>=modelIds)
        reject(L"Una definición de modelo queda fuera de la tabla de modelos de este ejecutable. Consulta el registro de modelos.");
    auto*slot=moved+4+std::size_t(count)*elementSize;
    if(!*reinterpret_cast<void**>(slot)){
        // The original CRT still constructs its untouched static store. Our
        // new zeroed slots use the SAME real native class constructor.
        reinterpret_cast<void*(__thiscall*)(void*)>(image+constructorRva)(slot);
        if(*reinterpret_cast<void**>(slot)!=image+vtableRva)
            reject(L"El constructor nativo no produjo el tipo de modelo esperado.");
        ++constructions;
    }
    ++count; // Same order as the native AddDamageAtomicModel routine.
    const auto methods=*reinterpret_cast<void***>(slot);
    reinterpret_cast<void(__thiscall*)(void*)>(methods[6])(slot);
    reinterpret_cast<void**>(image+tableRva)[index]=slot;
    if(count>largestCount){largestCount=count;if(count==1||count>=70)record("damage_model_added",count);}
    return slot;
}
}

bool installSteamModelStores(const std::filesystem::path&root,std::ostream&log){
    if(armed.load())return true;
    image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if(!sa_steam_host_identity::supported()||
       !sa_steam_host_identity::prefixMatches(uintptr_t(image+addRva),steam_model_store::addHash)||
       !sa_steam_host_identity::prefixMatches(uintptr_t(image+constructorRva),steam_model_store::constructorHash))return false;
    for(const auto&r:steam_model_store::references){
        if(*reinterpret_cast<uintptr_t*>(image+r.operandRva)!=uintptr_t(image+originalStoreRva+r.delta)||
           !sa_steam_host_identity::prefixMatches(uintptr_t(image+r.instructionRva),r.hash))return false;
    }
    std::array<std::array<unsigned char,7>,5> originalPrefixes{};
    for(std::size_t i=0;i<5;i++)std::memcpy(originalPrefixes[i].data(),image+steam_model_store::references[i].instructionRva,7);
    if(*reinterpret_cast<unsigned*>(image+originalStoreRva)!=0){log<<"model_store_install_too_late\n";return false;}
    // Stable addresses for the model table's actual maximum ID count; this is
    // not an arbitrary increased bound. One prefix page preserves safe -1 reads.
    const std::size_t bytes=0x1000+4+std::size_t(modelIds)*elementSize;
    auto*allocation=static_cast<std::byte*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!allocation)return false;moved=allocation+0x1000;
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){VirtualFree(allocation,0,MEM_RELEASE);moved=nullptr;return false;}
    const auto target=image+addRva;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&addDamageModel),reinterpret_cast<void**>(&originalAdd))!=MH_OK){VirtualFree(allocation,0,MEM_RELEASE);moved=nullptr;return false;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);VirtualFree(allocation,0,MEM_RELEASE);moved=nullptr;return false;}
    // While unarmed, the detour delegates unchanged to the native allocator.
    // All allocations/hook setup occur BEFORE freezing threads.
    HMODULE own{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&installSteamModelStores),&own)){
        MH_DisableHook(target);MH_RemoveHook(target);VirtualFree(allocation,0,MEM_RELEASE);moved=nullptr;return false;
    }
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(own);
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const std::byte*>(own)+dos->e_lfanew);
    std::array<CodeRange,8> ranges{};
    for(std::size_t i=0;i<5;i++){const auto&r=steam_model_store::references[i];ranges[i]={uintptr_t(image+r.instructionRva),uintptr_t(image+r.instructionRva+r.instructionSize)};}
    ranges[5]={uintptr_t(target),uintptr_t(target+0x31)};
    ranges[6]={uintptr_t(originalAdd),uintptr_t(originalAdd)+64};
    ranges[7]={uintptr_t(own),uintptr_t(own)+nt->OptionalHeader.SizeOfImage};
    bool installed=false;unsigned protectedCount=0;
    std::array<DWORD,5> protections{};
    {
        FrozenThreads freeze(ranges);
        if(freeze.ok()&&*reinterpret_cast<unsigned*>(image+originalStoreRva)==0){
            bool valid=true;
            for(std::size_t i=0;i<5;i++){
                const auto&r=steam_model_store::references[i];
                valid=valid&&*reinterpret_cast<uintptr_t*>(image+r.operandRva)==uintptr_t(image+originalStoreRva+r.delta)&&
                    std::memcmp(image+r.instructionRva,originalPrefixes[i].data(),7)==0;
            }
            if(valid){
                for(std::size_t i=0;i<5;i++){
                    const auto&r=steam_model_store::references[i];
                    auto*site=image+r.operandRva;DWORD protection{};
                    if(!VirtualProtect(site,4,PAGE_EXECUTE_READWRITE,&protection))break;
                    protections[i]=protection;++protectedCount;
                }
                if(protectedCount==5){
                    for(const auto&r:steam_model_store::references){
                        const auto replacement=uintptr_t(moved+r.delta);std::memcpy(image+r.operandRva,&replacement,4);
                    }
                    installed=FlushInstructionCache(GetCurrentProcess(),image+0xd0000,0x1000)!=0;
                    if(!installed){
                        for(const auto&r:steam_model_store::references){
                            const auto prior=uintptr_t(image+originalStoreRva+r.delta);std::memcpy(image+r.operandRva,&prior,4);
                        }
                        FlushInstructionCache(GetCurrentProcess(),image+0xd0000,0x1000);
                    }
                }
                // Restore shared-page permissions in reverse order. All writes
                // were done only after every operand was writable.
                for(unsigned i=protectedCount;i-->0;){DWORD ignored{};VirtualProtect(image+steam_model_store::references[i].operandRva,4,protections[i],&ignored);}
                if(installed)armed.store(true,std::memory_order_release);
            }
        }
    }
    if(!installed){MH_DisableHook(target);MH_RemoveHook(target);VirtualFree(allocation,0,MEM_RELEASE);moved=nullptr;log<<"model_store_relocation_not_committed\n";return false;}
    trace=CreateFileW((root/L"SAStandalone-model-stores.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    record("native_damage_store_installed",0);
    if(std::atexit(recordFinalState)!=0)log<<"model_store_exit_observer_unavailable\n";
    log<<"native_damage_store_installed capacity="<<modelIds<<" native_constructor=1 lifecycle_refs=5 original_crt_store_preserved=1\n"<<std::flush;
    return true;
}
