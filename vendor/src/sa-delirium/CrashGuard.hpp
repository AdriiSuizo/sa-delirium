#pragma once
// Crash reports from play (06-07/11) were a CLEO script's 0A8D READ_MEMORY of
// address 0 (CLEO+0x3AF2) and an opcode handler's bad write (0x48AEF3), both
// under the script loop. Nothing below them handled the fault, so the CRT's
// __except ran _exit, modloader unloaded the mods and a call into the unloaded
// Lively Vehicle Physics was all the dump showed. Now each opcode runs under
// __try: a failed 0A8D reads 0 like a good read would and the script goes on,
// any other script fault parks only that script. A fault elsewhere in
// CGame::Process skips the rest of that frame's game step. What is still
// unhandled writes crash-report.txt and a minidump, then ends the process
// before any unload. [Diagnostics] DetailedCapture=1 adds per-frame stage and
// script timing (frame-stages.jsonl) and every first-chance fault.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include "MinHook.h"

namespace crash_guard {
using Handler=char(__thiscall*)(void*,int);
inline HANDLE faultLog=INVALID_HANDLE_VALUE,stageLog=INVALID_HANDLE_VALUE;
inline std::wstring sessionDirectory;
inline bool detailed=false,fullDumps=false;
inline int testMode=0;
inline char* volatile currentScript=nullptr;
inline volatile int currentOpcode=-1;
inline unsigned scriptFaults=0,scriptsParked=0,gameFaults=0,firstChanceRows=0;
inline EXCEPTION_RECORD lastRecord{};
inline CONTEXT lastContext{};

inline bool fatalCode(DWORD c){return c==EXCEPTION_ACCESS_VIOLATION||c==EXCEPTION_ILLEGAL_INSTRUCTION||c==EXCEPTION_INT_DIVIDE_BY_ZERO||
    c==EXCEPTION_PRIV_INSTRUCTION||c==EXCEPTION_DATATYPE_MISALIGNMENT||c==EXCEPTION_ARRAY_BOUNDS_EXCEEDED||c==EXCEPTION_IN_PAGE_ERROR;}
inline void write(HANDLE file,const char* text){if(file==INVALID_HANDLE_VALUE)return;DWORD written=0;WriteFile(file,text,lstrlenA(text),&written,nullptr);}
inline void where(uintptr_t address,char* out,size_t size){
    MEMORY_BASIC_INFORMATION m{};char path[MAX_PATH]{};
    if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))&&m.Type==MEM_IMAGE&&GetModuleFileNameA(static_cast<HMODULE>(m.AllocationBase),path,MAX_PATH)){
        const char* name=strrchr(path,'\\');
        snprintf(out,size,"%s+0x%X",name?name+1:path,unsigned(address-reinterpret_cast<uintptr_t>(m.AllocationBase)));
    }else snprintf(out,size,"0x%08X",unsigned(address));
}
inline void scriptText(char* out,size_t size){
    char* s=currentScript;
    if(!s){snprintf(out,size,"none");return;}
    char name[9]{};memcpy(name,s+8,8);
    snprintf(out,size,"%s ip=+0x%X opcode=%04X",name,unsigned(*reinterpret_cast<uintptr_t*>(s+0x14)-*reinterpret_cast<uintptr_t*>(s+0x10)),unsigned(currentOpcode));
}
// No heap: this also runs after a fault in the allocator.
inline void report(HANDLE file,const char* tag,const EXCEPTION_RECORD& r,const CONTEXT& c){
    char at[160],script[64],line[512];SYSTEMTIME t;GetLocalTime(&t);
    where(uintptr_t(r.ExceptionAddress),at,sizeof(at));scriptText(script,sizeof(script));
    const bool access=r.ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&r.NumberParameters>=2;
    snprintf(line,sizeof(line),"[%02u:%02u:%02u.%03u game_ms=%u tid=%lu] %s code=0x%08lX at %s %s0x%08X\r\n"
        "  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX\r\n  last_script=%s\r\n  stack:",
        t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,*reinterpret_cast<unsigned*>(0xB7CB84),GetCurrentThreadId(),tag,r.ExceptionCode,at,
        access?(r.ExceptionInformation[0]==1?"write ":r.ExceptionInformation[0]==8?"execute ":"read "):"",access?unsigned(r.ExceptionInformation[1]):0u,
        c.Eax,c.Ebx,c.Ecx,c.Edx,c.Esi,c.Edi,c.Ebp,c.Esp,script);
    write(file,line);
    MEMORY_BASIC_INFORMATION m{};uintptr_t top=c.Esp;
    if(VirtualQuery(reinterpret_cast<void*>(c.Esp),&m,sizeof(m)))top=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
    int shown=0;
    for(uintptr_t p=c.Esp;p+4<=top&&p<c.Esp+8192&&shown<32;p+=4){
        const uintptr_t value=*reinterpret_cast<uintptr_t*>(p);MEMORY_BASIC_INFORMATION code{};
        if(value<0x10000||!VirtualQuery(reinterpret_cast<void*>(value),&code,sizeof(code))||code.Type!=MEM_IMAGE||
            !(code.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))continue;
        where(value,at,sizeof(at));snprintf(line,sizeof(line)," %s",at);write(file,line);shown++;
    }
    write(file,"\r\n");
}
inline void capture(EXCEPTION_POINTERS* p){lastRecord=*p->ExceptionRecord;lastContext=*p->ContextRecord;}

// Script opcodes. 0x469FF2 is the loop's push/mov/call in CRunningScript::Process.
inline LONG scriptFilter(EXCEPTION_POINTERS* p){if(!fatalCode(p->ExceptionRecord->ExceptionCode))return EXCEPTION_CONTINUE_SEARCH;capture(p);return EXCEPTION_EXECUTE_HANDLER;}
inline bool cleoMemoryRead(uintptr_t address){
    const auto cleo=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CLEO.asi"));
    return cleo&&(address==cleo+0x3AF2||address==cleo+0x3B11||address==cleo+0x3B32);
}
inline bool cleoMemoryWrite(uintptr_t address){
    const auto cleo=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CLEO.asi"));
    return cleo&&address>=cleo+0x3000&&address<cleo+0x3A80;  // 0A8C's handler ends where 0A8D's begins
}
inline void storeRead(char* script){
    __try{*reinterpret_cast<int*>(0xA43C78)=0;reinterpret_cast<void(__thiscall*)(void*,short)>(0x464370)(script,1);}
    __except(EXCEPTION_EXECUTE_HANDLER){*reinterpret_cast<unsigned*>(script+0xCC)=0xFFFFFFFFu;}
}
inline char recover(int opcode,char* script){
    scriptFaults++;
    const auto address=uintptr_t(lastRecord.ExceptionAddress);
    const bool read=opcode==0x0A8D&&(cleoMemoryRead(address)||testMode==1);
    const bool wrote=opcode==0x0A8C&&cleoMemoryWrite(address);
    if(scriptFaults<=200)report(faultLog,read?"script_fault_read_as_zero":wrote?"script_fault_write_skipped":"script_fault_parked",lastRecord,lastContext);
    if(scriptFaults<=50){char text[64],at[160],detail[256];scriptText(text,sizeof(text));where(address,at,sizeof(at));
        snprintf(detail,sizeof(detail),"%s at=%s action=%s",text,at,read?"read_as_zero":wrote?"write_skipped":"parked");event("script_fault",-1,-1,detail);}
    if(read){storeRead(script);return 0;}
    if(wrote)return 0;
    scriptsParked++;*reinterpret_cast<unsigned*>(script+0xCC)=0xFFFFFFFFu;  // wake time: this script never runs again
    return 1;
}
// GTASA_CRASH_GUARD_TEST: same state a failed 0A8D leaves (its three parameters read).
inline char __fastcall testHandler(void* script,void*,int){
    reinterpret_cast<void(__thiscall*)(void*,short)>(0x464080)(script,3);
    return char(*reinterpret_cast<volatile int*>(0));
}
inline char __stdcall guarded(Handler handler,int opcode,char* script){
    currentScript=script;currentOpcode=opcode;
    if(testMode&&testMode<=2&&opcode==0x0A8D)handler=reinterpret_cast<Handler>(&testHandler);
    __try{return handler(script,opcode);}
    __except(scriptFilter(GetExceptionInformation())){return recover(opcode,script);}
}
inline uintptr_t dispatchResume=0x469FF7;
__declspec(naked) inline void dispatch(){
    __asm{
        push esi
        push ecx
        push edx
        call guarded
        jmp dword ptr [dispatchResume]
    }
}

// The rest of the game step.
inline LONG gameFilter(EXCEPTION_POINTERS* p){
    static DWORD windowStart=0;static unsigned inWindow=0;
    if(!fatalCode(p->ExceptionRecord->ExceptionCode))return EXCEPTION_CONTINUE_SEARCH;
    const DWORD now=GetTickCount();if(now-windowStart>10000){windowStart=now;inWindow=0;}
    if(++inWindow>30)return EXCEPTION_CONTINUE_SEARCH;  // every frame failing: report it instead of looping
    capture(p);return EXCEPTION_EXECUTE_HANDLER;
}
inline void gameRecovered(){
    gameFaults++;report(faultLog,"game_process_fault_frame_skipped",lastRecord,lastContext);
    char at[160],detail[192];where(uintptr_t(lastRecord.ExceptionAddress),at,sizeof(at));snprintf(detail,sizeof(detail),"at=%s",at);
    if(gameFaults<=50)event("game_process_fault",-1,-1,detail);
}
inline void gameProcess(void(__cdecl* process)()){
    __try{
        if(testMode==3&&*reinterpret_cast<unsigned*>(0xB7CB84)>20000){testMode=0;*reinterpret_cast<volatile int*>(0)=1;}
        process();
    }__except(gameFilter(GetExceptionInformation())){gameRecovered();}
}

// Still unhandled. The CRT's __except filter accepts everything on the main
// thread (then _exit, the unload crash), so it is hooked as well as the top filter.
inline void writeDump(EXCEPTION_POINTERS* p){
    using Write=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
    const HMODULE help=LoadLibraryW(L"dbghelp.dll");if(!help)return;
    const auto dump=reinterpret_cast<Write>(GetProcAddress(help,"MiniDumpWriteDump"));if(!dump)return;
    wchar_t path[MAX_PATH+32];lstrcpynW(path,sessionDirectory.c_str(),MAX_PATH);lstrcatW(path,L"\\crash.dmp");
    const HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),p,FALSE};
    int type=MiniDumpWithDataSegs|MiniDumpWithUnloadedModules|MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithThreadInfo|MiniDumpWithHandleData;
    if(fullDumps)type|=MiniDumpWithPrivateReadWriteMemory;
    dump(GetCurrentProcess(),GetCurrentProcessId(),file,MINIDUMP_TYPE(type),&info,nullptr,nullptr);CloseHandle(file);
}
inline LONG WINAPI unhandled(EXCEPTION_POINTERS* p){
    static volatile LONG entered=0;
    if(InterlockedExchange(&entered,1))TerminateProcess(GetCurrentProcess(),p->ExceptionRecord->ExceptionCode);
    wchar_t path[MAX_PATH+32];lstrcpynW(path,sessionDirectory.c_str(),MAX_PATH);lstrcatW(path,L"\\crash-report.txt");
    const HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    report(file,"fatal",*p->ExceptionRecord,*p->ContextRecord);report(faultLog,"fatal",*p->ExceptionRecord,*p->ContextRecord);
    char counts[128];snprintf(counts,sizeof(counts),"script_faults=%u parked=%u game_faults=%u\r\n",scriptFaults,scriptsParked,gameFaults);write(file,counts);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    writeDump(p);
    TerminateProcess(GetCurrentProcess(),p->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline int(__cdecl*originalCrtFilter)(unsigned long,EXCEPTION_POINTERS*)=nullptr;
inline int __cdecl crtFilter(unsigned long,EXCEPTION_POINTERS* p){return unhandled(p);}
inline LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*originalSetFilter)(LPTOP_LEVEL_EXCEPTION_FILTER)=nullptr;
inline LPTOP_LEVEL_EXCEPTION_FILTER WINAPI setFilter(LPTOP_LEVEL_EXCEPTION_FILTER){return &unhandled;}  // a mod's filter would hide the report
inline LONG CALLBACK firstChance(EXCEPTION_POINTERS* p){
    if(!fatalCode(p->ExceptionRecord->ExceptionCode))return EXCEPTION_CONTINUE_SEARCH;
    // Three rows per place: the trainer alone faults hundreds of times at one address during startup.
    static void* places[256]{};static unsigned char seen[256]{};void* at=p->ExceptionRecord->ExceptionAddress;
    for(int i=0;i<256;i++){
        if(places[i]&&places[i]!=at)continue;
        places[i]=at;if(seen[i]<3){seen[i]++;firstChanceRows++;report(faultLog,"first_chance",*p->ExceptionRecord,*p->ContextRecord);}
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// DetailedCapture: where a slow frame went.
enum Stage{World,Scripts,Streaming,RenderScene,Render2d,Present,StageCount};
inline void(__cdecl*stageOriginal[StageCount])()={};
inline double stageMs[StageCount]{},gameMs=0,lastFrame=0,secondStart=0,worstFrame=0,worstScriptMs=0;
inline char worstScript[9]{};
inline unsigned scriptRuns=0,framesThisSecond=0,slowFrames=0;
inline char(__fastcall*originalScript)(void*,void*)=nullptr;
inline double now(){static const double hz=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(f.QuadPart);}();LARGE_INTEGER t;QueryPerformanceCounter(&t);return double(t.QuadPart)*1000./hz;}
template<int I> void __cdecl timedStage(){const double t=now();stageOriginal[I]();stageMs[I]+=now()-t;}
inline char __fastcall timedScript(void* script,void* unused){
    const double t=now();const char result=originalScript(script,unused);const double ms=now()-t;
    scriptRuns++;if(ms>worstScriptMs){worstScriptMs=ms;memcpy(worstScript,static_cast<char*>(script)+8,8);}
    return result;
}
inline int poolUsed(uintptr_t global){
    const auto* pool=*reinterpret_cast<const char* const*>(global);if(!pool)return -1;
    const auto* map=*reinterpret_cast<const unsigned char* const*>(pool+4);const int size=*reinterpret_cast<const int*>(pool+8);int used=0;
    for(int i=0;i<size;i++)used+=!(map[i]&0x80);
    return used;
}
// Lively registers a DLL notification and modloader unloads it at exit, so the next unload called freed code.
inline void pinLively(){
    static bool done=false;static unsigned tries=0;
    if(done||(++tries&255)!=1)return;
    HMODULE module=nullptr;done=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"livelyvehiclephysics.asi",&module)!=0;
}
inline void tick(int active,int pending){
    pinLively();
    if(testMode==4&&*reinterpret_cast<unsigned*>(0xB7CB84)>20000){testMode=0;*reinterpret_cast<volatile int*>(0)=1;}
    if(!detailed)return;
    const double t=now(),frame=t-lastFrame;lastFrame=t;
    char row[768];
    if(frame>=25.&&frame<5000.){
        slowFrames++;double known=0;for(double ms:stageMs)known+=ms;
        snprintf(row,sizeof(row),"{\"kind\":\"slow_frame\",\"frame\":%u,\"game_ms\":%u,\"ms\":%.2f,\"active\":%d,\"pending\":%d,\"world\":%.2f,\"scripts\":%.2f,\"streaming\":%.2f,\"render_scene\":%.2f,\"render_2d\":%.2f,\"present\":%.2f,\"game_step\":%.2f,\"other\":%.2f,\"script_runs\":%u,\"worst_script\":\"%s\",\"worst_script_ms\":%.2f}\n",
            *reinterpret_cast<unsigned*>(0xB7CB4C),*reinterpret_cast<unsigned*>(0xB7CB84),frame,active,pending,stageMs[World],stageMs[Scripts],stageMs[Streaming],stageMs[RenderScene],stageMs[Render2d],stageMs[Present],gameMs,frame-known,scriptRuns,worstScript,worstScriptMs);
        write(stageLog,row);
    }
    if(frame>worstFrame&&frame<5000.)worstFrame=frame;
    framesThisSecond++;
    if(t-secondStart>=1000.){
        MEMORYSTATUSEX m{sizeof(m)};GlobalMemoryStatusEx(&m);
        snprintf(row,sizeof(row),"{\"kind\":\"second\",\"game_ms\":%u,\"fps\":%u,\"worst_ms\":%.2f,\"slow_frames\":%u,\"active\":%d,\"peds\":%d,\"vehicles\":%d,\"objects\":%d,\"tasks\":%d,\"events\":%d,\"used_address_mb\":%u,\"free_address_mb\":%u,\"stream_mb\":%u,\"stream_limit_mb\":%u,\"script_faults\":%u,\"parked\":%u,\"game_faults\":%u}\n",
            *reinterpret_cast<unsigned*>(0xB7CB84),framesThisSecond,worstFrame,slowFrames,active,poolUsed(0xB74490),poolUsed(0xB74494),poolUsed(0xB7449C),poolUsed(0xB744A8),poolUsed(0xB744AC),
            unsigned((m.ullTotalVirtual-m.ullAvailVirtual)>>20),unsigned(m.ullAvailVirtual>>20),*reinterpret_cast<unsigned*>(0x8E4CB4)>>20,*reinterpret_cast<unsigned*>(0x8A5A80)>>20,scriptFaults,scriptsParked,gameFaults);
        write(stageLog,row);secondStart=t;framesThisSecond=0;worstFrame=0;slowFrames=0;
    }
    memset(stageMs,0,sizeof(stageMs));gameMs=0;scriptRuns=0;worstScriptMs=0;worstScript[0]=0;
}
inline void timedGameProcess(void(__cdecl* process)()){
    if(!detailed){gameProcess(process);return;}
    const double t=now();gameProcess(process);gameMs+=now()-t;
}

// Before anything reads the profiling variables.
inline void prepare(const std::filesystem::path& game,const std::filesystem::path& session){
    namespace fs=std::filesystem;
    fs::path directory=session.empty()?game.parent_path()/"logs"/"crash-guard":session;
    std::error_code ec;fs::create_directories(directory,ec);sessionDirectory=directory.wstring();
    const auto ini=(game/"DeliriumSA.ini").wstring();
    detailed=GetPrivateProfileIntW(L"Diagnostics",L"DetailedCapture",0,ini.c_str())!=0;fullDumps=detailed;
    faultLog=CreateFileW((directory/"faults.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    char test[4]{};if(GetEnvironmentVariableA("GTASA_CRASH_GUARD_TEST",test,4))testMode=atoi(test);
    SetEnvironmentVariableW(L"GTASA_PANIC_LOG",(directory/"rust-panics.log").c_str());
    SetUnhandledExceptionFilter(&unhandled);
    if(!detailed)return;
    sessionlog::eventBudget=268435456;  // 16 MB ran out 25 minutes into a session
    stageLog=CreateFileW((directory/"frame-stages.jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    wchar_t present[2]{};
    if(!GetEnvironmentVariableW(L"GTASA_FRAME_PROFILE",present,2))SetEnvironmentVariableW(L"GTASA_FRAME_PROFILE",(directory/"frame-profile.jsonl").c_str());
    if(!GetEnvironmentVariableW(L"GTASA_CONTACT_COST_PROFILE",present,2))SetEnvironmentVariableW(L"GTASA_CONTACT_COST_PROFILE",(directory/"contact-cost.jsonl").c_str());
    if(!GetEnvironmentVariableW(L"GTASA_PHYSICS_BREAKDOWN",present,2))SetEnvironmentVariableW(L"GTASA_PHYSICS_BREAKDOWN",(directory/"physics-breakdown.jsonl").c_str());
    AddVectoredExceptionHandler(1,&firstChance);
    lastFrame=secondStart=now();
}
// Queues the hooks for the next MH_ApplyQueued. Returns a summary for the log.
inline std::string install(){
    std::string result;
    auto* site=reinterpret_cast<unsigned char*>(0x469FF2);
    const bool expected=!memcmp(site,"\x51\x8B\xCE\xFF\xD2",5);void* unused=nullptr;
    const bool script=expected&&MH_CreateHook(site,reinterpret_cast<void*>(&dispatch),&unused)==MH_OK&&MH_QueueEnableHook(site)==MH_OK;
    result=script?"script_guard=1":expected?"script_guard=hook_failed":"script_guard=site_changed";
    void* setter=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"SetUnhandledExceptionFilter"));
    const bool keeps=setter&&MH_CreateHook(setter,reinterpret_cast<void*>(&setFilter),reinterpret_cast<void**>(&originalSetFilter))==MH_OK&&MH_QueueEnableHook(setter)==MH_OK;
    result+=keeps?" top_filter=1":" top_filter=0";
    void* crt=reinterpret_cast<void*>(0x82D726);  // _XcptFilter, called from __tmainCRTStartup's filter at 0x824714
    const bool caught=MH_CreateHook(crt,reinterpret_cast<void*>(&crtFilter),reinterpret_cast<void**>(&originalCrtFilter))==MH_OK&&MH_QueueEnableHook(crt)==MH_OK;
    result+=caught?" crt_filter=1":" crt_filter=0";
    if(detailed){
        struct Hook{uintptr_t address;void* replacement;void** original;};
        const Hook hooks[]={
            {0x5684A0,reinterpret_cast<void*>(&timedStage<World>),reinterpret_cast<void**>(&stageOriginal[World])},              // CWorld::Process
            {0x46A000,reinterpret_cast<void*>(&timedStage<Scripts>),reinterpret_cast<void**>(&stageOriginal[Scripts])},          // CTheScripts::Process
            {0x40E670,reinterpret_cast<void*>(&timedStage<Streaming>),reinterpret_cast<void**>(&stageOriginal[Streaming])},      // CStreaming::Update
            {0x53DF40,reinterpret_cast<void*>(&timedStage<RenderScene>),reinterpret_cast<void**>(&stageOriginal[RenderScene])},  // RenderScene
            {0x53E230,reinterpret_cast<void*>(&timedStage<Render2d>),reinterpret_cast<void**>(&stageOriginal[Render2d])},        // Render2dStuff
            {0x53D840,reinterpret_cast<void*>(&timedStage<Present>),reinterpret_cast<void**>(&stageOriginal[Present])},          // DoRWStuffEndOfFrame
            {0x469F00,reinterpret_cast<void*>(&timedScript),reinterpret_cast<void**>(&originalScript)}};                         // CRunningScript::Process
        int queued=0;
        for(const auto&h:hooks)if(MH_CreateHook(reinterpret_cast<void*>(h.address),h.replacement,h.original)==MH_OK&&MH_QueueEnableHook(reinterpret_cast<void*>(h.address))==MH_OK)queued++;
        result+=" detailed_hooks="+std::to_string(queued)+"/7";
    }
    return result;
}
}
