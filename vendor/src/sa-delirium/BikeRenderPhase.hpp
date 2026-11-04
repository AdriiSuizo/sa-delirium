#pragma once
#include "BikeContactGeometry.hpp"
#include "MinHook.h"
#include "CodePatch.hpp"
#include <sstream>
#include <iomanip>

static void bikeRenderInstallDiagnostic(const char*stage,uintptr_t address,int mhStatus=-1){
    const DWORD win32Error=GetLastError();
    std::array<unsigned char,16> bytes{};SIZE_T read=0;
    if(address)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),bytes.data(),bytes.size(),&read);
    std::ostringstream detail;detail<<"stage="<<stage<<" address=0x"<<std::hex<<address<<" runtime_bytes=";
    for(size_t i=0;i<read;i++)detail<<std::setw(2)<<std::setfill('0')<<unsigned(bytes[i]);
    uintptr_t destination=0;const char*branch="none";
    if(read>=5&&(bytes[0]==0xE8||bytes[0]==0xE9)){
        int32_t relative=0;std::memcpy(&relative,bytes.data()+1,4);destination=address+5+relative;branch=bytes[0]==0xE8?"CALL_rel32":"JMP_rel32";
    }else if(read>=6&&bytes[0]==0xFF&&(bytes[1]==0x25||bytes[1]==0x15)){
        uint32_t pointer=0,value=0;std::memcpy(&pointer,bytes.data()+2,4);SIZE_T copied=0;
        if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(pointer),&value,4,&copied)&&copied==4)destination=value;
        branch=bytes[1]==0x25?"JMP_indirect":"CALL_indirect";
    }else if(read>=6&&bytes[0]==0x68&&bytes[5]==0xC3){
        uint32_t value=0;std::memcpy(&value,bytes.data()+1,4);destination=value;branch="PUSH_RET";
    }
    detail<<" branch="<<branch<<" destination=0x"<<destination;
    HMODULE owner=nullptr;char module[32768]={};
    if(destination&&GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(destination),&owner))GetModuleFileNameA(owner,module,sizeof(module));
    detail<<" destination_module="<<(module[0]?module:"<unresolved>")<<std::dec<<" mh_status="<<mhStatus<<" win32_error="<<win32Error;
    event("bike_render_install_refused",-1,-1,detail.str());
}

// The only authored-render barrier is the verified final direct call in
// CRenderer::PreRender, after every native entity list and before player
// shadows. No second CBike/CBmx::PreRender is run (it mutates suspension,
// wheel phases, rain/tyre state, effects, and RNG).
static void(__thiscall*originalBikeGeometryPreRender)(void*)=nullptr;
static void(__thiscall*originalBmxGeometryPreRender)(void*)=nullptr;
static void(__cdecl*originalBikeFrameIdle)(void*)=nullptr;
static void(__cdecl*chainedBeforeShadows)()=nullptr;
static void(*finishBikePhysicsFrame)()=nullptr;
static bool(*bikePhysicsFramePending)()=nullptr;
static std::map<int,unsigned> nativeBikeRenderedFrame;
struct BikeRenderPhaseStats {
    unsigned frame=0,rendered=0,offscreen=0,rejected=0,unverifiedMotion=0;
    float maxNativeLocalError=0,maxNativeWorldError=0;
    bool rendererBarrier=false;
    double sampleMs=0,geometryBuildMs=0,geometryRegistrationMs=0;
    unsigned geometryBuilds=0,geometryCacheHits=0,geometrySdkRegistrations=0;
};
static BikeRenderPhaseStats lastBikeRenderPhase;
static void __fastcall bikeGeometryPreRender(void*bike,void*){
    originalBikeGeometryPreRender(bike);
    const int ref=vehicleRef(bike);if(ref>=0)nativeBikeRenderedFrame[ref]=*reinterpret_cast<unsigned*>(0xB7CB4C);
}
static void __fastcall bmxGeometryPreRender(void*bike,void*){
    originalBmxGeometryPreRender(bike);
    const int ref=vehicleRef(bike);if(ref>=0)nativeBikeRenderedFrame[ref]=*reinterpret_cast<unsigned*>(0xB7CB4C);
}
static void sampleBikeContactsForFrame(bool rendererBarrier){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    lastBikeRenderPhase={};lastBikeRenderPhase.frame=frame;lastBikeRenderPhase.rendererBarrier=rendererBarrier;
    BikeGeometryTimedScope phaseTime{lastBikeRenderPhase.sampleMs};
    if(!playing()||!player())return;
    auto*pool=vehiclePool();if(!pool||pool->size<1||pool->size>10000)return;
    for(auto i=nativeBikeRenderedFrame.begin();i!=nativeBikeRenderedFrame.end();)if(!vehicleFromRef(i->first))i=nativeBikeRenderedFrame.erase(i);else ++i;
    static std::map<int,std::string> reportedFailures;
    for(auto i=reportedFailures.begin();i!=reportedFailures.end();)if(!vehicleFromRef(i->first))i=reportedFailures.erase(i);else ++i;
    const V origin=position(player());
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        void*bike=pool->objects+i*0xA18;const int ref=(i<<8)|pool->flags[i],subtype=at<int>(bike,0x594);
        if((subtype!=9&&subtype!=10)||!matrix(bike)||!at<void*>(bike,0x18)||at<unsigned char>(bike,0x2F)!=area()||length(position(bike)-origin)>50.f)continue;
        const auto evaluated=nativeBikeRenderedFrame.find(ref);
        const bool rendered=rendererBarrier&&evaluated!=nativeBikeRenderedFrame.end()&&evaluated->second==frame;
        const auto cached=bikeContactGeometryCache.find(ref);
        const bool rebuild=cached==bikeContactGeometryCache.end()||!cached->second.geometry||cached->second.geometry->clump!=at<void*>(bike,0x18)||cached->second.geometry->model!=at<short>(bike,0x22);
        const bool valid=recordBikeContactSnapshot(bike,rendered);
        auto&entry=bikeContactGeometryCache.at(ref);const auto&s=entry.snapshot;
        if(rebuild&&entry.geometry){
            const auto&g=*entry.geometry;const auto&cost=g.cost;
            lastBikeRenderPhase.geometryBuilds++;lastBikeRenderPhase.geometryBuildMs+=cost.buildMs;
            lastBikeRenderPhase.geometryRegistrationMs+=cost.sdkRegistrationMs;lastBikeRenderPhase.geometryCacheHits+=cost.cacheHits;lastBikeRenderPhase.geometrySdkRegistrations+=cost.sdkRegistrations;
            event("bike_geometry_profile",-1,g.model,"vehicle="+std::to_string(ref)+" valid="+std::to_string(g.valid)+" source_triangles="+std::to_string(g.sourceTriangles)+
                " build_ms="+std::to_string(cost.buildMs)+" shape_lookup_registration_ms="+std::to_string(cost.shapeLookupAndRegistrationMs)+" sdk_registration_ms="+std::to_string(cost.sdkRegistrationMs)+
                " cache_hits="+std::to_string(cost.cacheHits)+" sdk_registrations="+std::to_string(cost.sdkRegistrations));
        }
        lastBikeRenderPhase.maxNativeLocalError=std::max(lastBikeRenderPhase.maxNativeLocalError,s.nativeMatrixError);
        lastBikeRenderPhase.maxNativeWorldError=std::max(lastBikeRenderPhase.maxNativeWorldError,s.nativeWorldError);
        if(valid){
            if(rendered)lastBikeRenderPhase.rendered++;else lastBikeRenderPhase.offscreen++;
            if(!s.motionVerified)lastBikeRenderPhase.unverifiedMotion++;
            if(reportedFailures.erase(ref))event("bike_contact_geometry_restored",-1,at<short>(bike,0x22),"vehicle="+std::to_string(ref));
        }else{
            lastBikeRenderPhase.rejected++;
            const std::string reason=entry.geometry&&!entry.geometry->valid?entry.geometry->reason:s.failure;
            if(reportedFailures[ref]!=reason){
                reportedFailures[ref]=reason;
                event("bike_contact_geometry_refused",-1,at<short>(bike,0x22),"vehicle="+std::to_string(ref)+" reason="+reason+
                    " local_error="+std::to_string(s.nativeMatrixError)+" world_error="+std::to_string(s.nativeWorldError)+" native_rendered="+std::to_string(rendered));
            }
        }
    }
}
static void completeBikePhysicsFrame(bool rendererBarrier){
    // The owner's simulation ticket is authoritative, including idempotence.
    // A repeated render must not spend CPU sampling or step physics again.
    if(!bikeContactStageEnabled||!finishBikePhysicsFrame||!bikePhysicsFramePending||!bikePhysicsFramePending())return;
    sampleBikeContactsForFrame(rendererBarrier);
    finishBikePhysicsFrame();
}
static void __cdecl bikeRenderBeforeShadows(){
    completeBikePhysicsFrame(true);
    chainedBeforeShadows(); // current callee: native SpecialHeliPreRender or another mod's chained hook
}
static void __cdecl bikeFrameIdle(void*renderArgument){
    // If another loader saved THIS function as its "original" and we later
    // chained to that loader, we are re-entered: run the native Idle once.
    static bool inside=false;
    if(inside){reinterpret_cast<void(__cdecl*)(void*)>(0x53E920)(renderArgument);return;}
    FrameCostProfile cost("idle",0,0,-1,int(renderArgument!=nullptr));
    inside=true;originalBikeFrameIdle(renderArgument);inside=false;
    cost.lap(0);
    // Idle can process CGame yet skip renderer lists when renderArgument==0,
    // menu/fade paths, or another native early render exit. Complete that SAME
    // frame now using current pure component state and owner's dt=0 palette
    // evaluation. Never carry old dt or reuse an old world-space LTM next tick.
    completeBikePhysicsFrame(false);
    cost.lap(1);
}
static bool installBikeRenderBarrierCall(){
    // Both original callees remain intact. In particular SilentPatch writes
    // EB43 at Idle+3 and fixes its saved-ESI stack path; moving that entry to a
    // guessed native trampoline would conflict with an already owned patch.
    // modloader (and possibly other ASI) already redirect these CALLs to their
    // own functions, which then call the native callee. Requiring the pristine
    // bytes refused the whole adapter in normal play (2026-11-01, session
    // 151742). We now accept any direct CALL rel32 whose destination is inside
    // a loaded module and chain to that CURRENT destination, so the other mod
    // keeps running and the native callee still runs exactly once.
    struct Site {uintptr_t address;uintptr_t native;std::array<unsigned char,5> expected;void*replacement;void**chain;};
    Site sites[]={
        // 0x53ECBD (Idle caller) is NOT patched here: see maintainIdleCaller.
        {0x5539FC,0x6C4750,{},reinterpret_cast<void*>(&bikeRenderBeforeShadows),reinterpret_cast<void**>(&chainedBeforeShadows)}
    };
    struct Page {uintptr_t address;DWORD protection;bool writable=false;};
    std::vector<Page> pages;SYSTEM_INFO info{};GetSystemInfo(&info);
    for(auto&s:sites){
        std::memcpy(s.expected.data(),reinterpret_cast<const void*>(s.address),5);
        if(s.expected[0]!=0xE8){bikeRenderInstallDiagnostic("frame_call_signature",s.address);return false;}
        int32_t relative=0;std::memcpy(&relative,s.expected.data()+1,4);
        const uintptr_t destination=s.address+5+relative;HMODULE owner=nullptr;
        if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(destination),&owner)){bikeRenderInstallDiagnostic("frame_call_destination_unmapped",s.address);return false;}
        if(destination==reinterpret_cast<uintptr_t>(s.replacement)){bikeRenderInstallDiagnostic("frame_call_already_ours",s.address);return false;}
        *s.chain=reinterpret_cast<void*>(destination);
        if(destination!=s.native){char module[MAX_PATH]={};GetModuleFileNameA(owner,module,sizeof(module));
            event("bike_render_call_chained",-1,-1,"address="+std::to_string(s.address)+" native="+std::to_string(s.native)+" current="+std::to_string(destination)+" module="+module);}
        const uintptr_t first=s.address/info.dwPageSize*info.dwPageSize;
        const uintptr_t last=(s.address+4)/info.dwPageSize*info.dwPageSize;
        for(uintptr_t address=first;address<=last;address+=info.dwPageSize){
            if(std::any_of(pages.begin(),pages.end(),[&](const auto&p){return p.address==address;}))continue;
            MEMORY_BASIC_INFORMATION region{};
            if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))||region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))){bikeRenderInstallDiagnostic("frame_call_page",s.address);return false;}
            pages.push_back({address,region.Protect});
        }
    }
    const std::array<CodeRange,1> ranges{{{sites[0].address,sites[0].address+5}}};
    auto rollback=[&](bool written){
        bool writable=true,flushed=true,restored=true;DWORD unused=0;
        if(written){
            // A previous page may already have been restored when a later
            // restore failed. Reacquire only the pages needed for rollback.
            for(auto&p:pages)if(!p.writable){
                p.writable=VirtualProtect(reinterpret_cast<void*>(p.address),info.dwPageSize,PAGE_EXECUTE_READWRITE,&unused)!=0;
                writable=writable&&p.writable;
            }
            if(writable){
                FrozenThreads freeze(ranges);
                for(const auto&s:sites)std::memcpy(reinterpret_cast<void*>(s.address),s.expected.data(),5);
                for(const auto&s:sites)flushed=(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(s.address),5)!=0)&&flushed;
            }
        }
        for(auto&p:pages)if(p.writable){
            const bool ok=VirtualProtect(reinterpret_cast<void*>(p.address),info.dwPageSize,p.protection,&unused)!=0;
            restored=restored&&ok;if(ok)p.writable=false;
        }
        event("bike_render_barrier_rolled_back",-1,-1,"bytes_restored="+std::to_string(!written||writable)+" cache_flushed="+std::to_string(flushed)+" protection_restored="+std::to_string(restored));
        return false;
    };
    DWORD unused=0;
    for(auto&p:pages){
        if(!VirtualProtect(reinterpret_cast<void*>(p.address),info.dwPageSize,PAGE_EXECUTE_READWRITE,&unused)){bikeRenderInstallDiagnostic("frame_call_protect",p.address);return rollback(false);}
        p.writable=true;
    }
    // Recheck and write with every other thread suspended (CodePatch.hpp):
    // the main thread may already be rendering while setup runs.
    bool frozen=false,changed=false,flushOk=true;
    {
        FrozenThreads freeze(ranges);frozen=freeze.ok();
        if(frozen){
            for(const auto&s:sites)if(std::memcmp(reinterpret_cast<const void*>(s.address),s.expected.data(),5))changed=true;
            if(!changed){
                for(const auto&s:sites){
                    auto replacement=s.expected;const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(s.replacement)-(s.address+5));
                    std::memcpy(replacement.data()+1,&relative,4);std::memcpy(reinterpret_cast<void*>(s.address),replacement.data(),5);
                }
                for(const auto&s:sites)flushOk=(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(s.address),5)!=0)&&flushOk;
            }
        }
    }
    if(!frozen){bikeRenderInstallDiagnostic("frame_call_freeze",sites[0].address);return rollback(false);}
    if(changed){bikeRenderInstallDiagnostic("frame_call_concurrent_change",sites[0].address);return rollback(false);}
    if(!flushOk){bikeRenderInstallDiagnostic("frame_call_flush",sites[0].address);return rollback(true);}
    for(auto&p:pages){
        if(!VirtualProtect(reinterpret_cast<void*>(p.address),info.dwPageSize,p.protection,&unused)){bikeRenderInstallDiagnostic("frame_call_restore_protection",p.address);return rollback(true);}
        p.writable=false;
    }
    return true;
}
// Idle caller 0x53ECBD. modloader redirects this CALL during startup, at a
// time that differs between bridge cases and normal play. Chain to whatever
// destination is current, from the MAIN thread inside CGame::Process (all
// loaders done; this call's return address is already past the instruction),
// and re-verify every frame so a later overwrite is detected and logged.
static unsigned idleCallerChains=0;
static void maintainIdleCaller(){
    if(!bikeContactStageEnabled)return;
    constexpr uintptr_t site=0x53ECBD;
    const auto*code=reinterpret_cast<const unsigned char*>(site);
    static bool reportedStop=false;
    if(code[0]!=0xE8){if(!reportedStop){reportedStop=true;bikeRenderInstallDiagnostic("idle_caller_not_call",site);}return;}
    int32_t relative=0;std::memcpy(&relative,code+1,4);
    const uintptr_t destination=site+5+relative;
    if(destination==reinterpret_cast<uintptr_t>(&bikeFrameIdle))return;
    HMODULE owner=nullptr;
    if(idleCallerChains>=4||!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(destination),&owner)){
        if(!reportedStop){reportedStop=true;bikeRenderInstallDiagnostic(idleCallerChains>=4?"idle_caller_contended":"idle_caller_destination_unmapped",site);}
        return;
    }
    DWORD protection=0,unused=0;
    if(!VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&protection)){if(!reportedStop){reportedStop=true;bikeRenderInstallDiagnostic("idle_caller_protect",site);}return;}
    originalBikeFrameIdle=reinterpret_cast<decltype(originalBikeFrameIdle)>(destination);
    unsigned char call[5]={0xE8};const int32_t ours=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&bikeFrameIdle)-(site+5));std::memcpy(call+1,&ours,4);
    bool written=false;
    {const std::array<CodeRange,1> range{{{site,site+5}}};FrozenThreads freeze(range);if(freeze.ok()){std::memcpy(reinterpret_cast<void*>(site),call,5);written=true;}}
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),5);
    VirtualProtect(reinterpret_cast<void*>(site),5,protection,&unused);
    if(!written)return;
    idleCallerChains++;
    char module[MAX_PATH]={};GetModuleFileNameA(owner,module,sizeof(module));
    event("idle_caller_chained",-1,-1,"chain="+std::to_string(idleCallerChains)+" previous="+std::to_string(destination)+" native="+std::to_string(destination==0x53E920)+" module="+module);
}
static bool installBikeRenderPhase(void(*finish)(),bool(*pending)()){
    if(!finish||!pending){bikeRenderInstallDiagnostic("missing_frame_callback",0);return false;}
    // Verified owned executable entries; no code bytes are also read as data.
    // See native-idle-frame-paths.json and render-phase-xrefs.json.
    const unsigned char bikeStart[]={0x6A,0xFF,0x68,0x21,0x83,0x84,0};
    const unsigned char bmxStart[]={0x6A,0xFF,0x68,0x01,0x84,0x84,0};
    bool signatures=true;
    if(std::memcmp(reinterpret_cast<void*>(0x6BD090),bikeStart,sizeof(bikeStart))){bikeRenderInstallDiagnostic("bike_prerender_signature",0x6BD090);signatures=false;}
    if(std::memcmp(reinterpret_cast<void*>(0x6C0810),bmxStart,sizeof(bmxStart))){bikeRenderInstallDiagnostic("bmx_prerender_signature",0x6C0810);signatures=false;}
    if(!signatures)return false;
    struct Site {uintptr_t address;void*replacement;void**original;};
    const Site sites[]={
        {0x6BD090,reinterpret_cast<void*>(&bikeGeometryPreRender),reinterpret_cast<void**>(&originalBikeGeometryPreRender)},
        {0x6C0810,reinterpret_cast<void*>(&bmxGeometryPreRender),reinterpret_cast<void**>(&originalBmxGeometryPreRender)}
    };
    for(const auto&s:sites){
        const auto status=MH_CreateHook(reinterpret_cast<void*>(s.address),s.replacement,s.original);
        if(status!=MH_OK){bikeRenderInstallDiagnostic("create_hook",s.address,int(status));return false;}
    }
    if(!installBikeRenderBarrierCall())return false;
    finishBikePhysicsFrame=finish;bikePhysicsFramePending=pending;bikeContactStageEnabled=true;
    for(const auto&s:sites){
        const auto status=MH_QueueEnableHook(reinterpret_cast<void*>(s.address));
        if(status!=MH_OK){bikeRenderInstallDiagnostic("queue_hook",s.address,int(status));return false;}
    }
    event("bike_render_phase_installed",-1,-1,"post_native_lists=1 before_shadows=1 idle_nonrender_fallback=1 duplicate_prerender=0 idle_entry_unchanged=1 verified_calls=1 idle_caller=main_thread_chain");
    return true;
}
