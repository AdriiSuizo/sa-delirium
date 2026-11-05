// SPDX-License-Identifier: GPL-3.0-only
// Native Steam backend for Open Limit Adjuster's visual-pool dependencies.
// OLA reference e454bfaf, MIT, ThirteenAG/LINK2012; notice shipped with the pack.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <ostream>
#include <span>
#include "../native/SteamHostIdentity.hpp"
#include "../native/SteamVisualPoolBindings.hpp"
#include "../vendor/src/sa-delirium/CodePatch.hpp"

bool installSteamVisualPools(const std::filesystem::path&root,std::ostream&log){
    static bool installed=false;
    if(installed)return true;
    struct Pool {
        const wchar_t*option;
        const char*name;
        unsigned original,capacity,maximum,unroll;
        uintptr_t global,first,last;
        std::span<const steam_model_store::Reference> refs;
    };
    std::array pools{
        Pool{L"CollisionModels","collision",10150,20000,100000,5,0x8018bc,0x166760,0x166847,steam_visual_pools::references},
        Pool{L"Buildings","building",13500,30000,32766,6,0x8018b0,0x166499,0x166583,steam_building_pool::references}
    };
    const auto ini=root/L"scripts"/L"SAStandalone.ini";
    auto*image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if(!sa_steam_host_identity::supported())return false;
    std::array<std::array<unsigned char,7>,8> snapshots{};
    std::array<CodeRange,2> ranges{};std::size_t index=0;
    for(std::size_t n=0;n<pools.size();n++){
        auto&p=pools[n];p.capacity=GetPrivateProfileIntW(L"Streaming",p.option,p.capacity,ini.c_str());
        // Native flag initialization is unrolled: retain the algorithm and
        // require a capacity divisible by its reviewed group size.
        if(p.capacity<p.original||p.capacity>p.maximum||p.capacity%p.unroll){
            log<<"native_visual_pool_invalid_capacity pool="<<p.name<<'\n';return false;
        }
        if(*reinterpret_cast<void**>(image+p.global))return false;
        ranges[n]={uintptr_t(image+p.first),uintptr_t(image+p.last)};
        for(const auto&r:p.refs){
            if(*reinterpret_cast<unsigned*>(image+r.operandRva)!=p.original||
               !sa_steam_host_identity::prefixMatches(uintptr_t(image+r.instructionRva),r.hash))return false;
            std::memcpy(snapshots[index++].data(),image+r.instructionRva,7);
        }
    }
    bool committed=false;
    {
        FrozenThreads frozen(ranges);
        if(!frozen.ok())return false;
        index=0;
        for(const auto&p:pools){
            if(*reinterpret_cast<void**>(image+p.global))return false;
            for(const auto&r:p.refs)
                if(*reinterpret_cast<unsigned*>(image+r.operandRva)!=p.original||
                   std::memcmp(image+r.instructionRva,snapshots[index++].data(),7))return false;
        }
        std::array<DWORD,8> protections{};unsigned count=0;bool writable=true;
        for(const auto&p:pools){
            for(const auto&r:p.refs){
                if(!VirtualProtect(image+r.operandRva,4,PAGE_EXECUTE_READWRITE,&protections[count])){writable=false;break;}
                ++count;
            }
            if(!writable)break;
        }
        if(count==8){
            for(const auto&p:pools)for(const auto&r:p.refs)std::memcpy(image+r.operandRva,&p.capacity,4);
            committed=FlushInstructionCache(GetCurrentProcess(),image+0x166499,0x3ae)!=0;
            if(!committed){
                for(const auto&p:pools)for(const auto&r:p.refs)std::memcpy(image+r.operandRva,&p.original,4);
                FlushInstructionCache(GetCurrentProcess(),image+0x166499,0x3ae);
            }
        }
        // Shared-page permissions are restored in reverse protection order.
        for(unsigned i=count;i-->0;){DWORD ignored{};const auto&r=pools[i/4].refs[i%4];VirtualProtect(image+r.operandRva,4,protections[i],&ignored);}
    }
    installed=committed;
    for(const auto&p:pools)log<<"native_"<<p.name<<"_pool_configured committed="<<unsigned(committed)
        <<" capacity="<<p.capacity<<" native_allocator=1 native_shutdown=1 constructor_operands=4\n";
    log<<std::flush;return committed;
}
