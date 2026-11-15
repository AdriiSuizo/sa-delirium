#pragma once
// Disposable test bridge only. These hooks never belong in a shipping ASI.
// SA 1.0 US 0x7468E0 uses the named CreateEventA below before FindWindowA /
// SetForegroundWindow; WinMain calls it at 0x74872D. The owned UAL build loads
// InitializeASI from the CRT (0x8245E5), before that check, on the startup thread.
static bool passiveWindow=false;
static bool concurrentQa=false;
static bool singletonRedirected=false;
static decltype(&CreateWindowExA) originalCreateWindow=nullptr;
static decltype(&ShowWindow) originalShowWindow=nullptr;
static decltype(&SetForegroundWindow) originalForeground=nullptr;
static decltype(&SetActiveWindow) originalActive=nullptr;
static decltype(&SetFocus) originalFocus=nullptr;
static decltype(&SetWindowPos) originalWindowPos=nullptr;
static decltype(&ClipCursor) originalClipCursor=nullptr;
static decltype(&SetCursorPos) originalCursorPos=nullptr;
static decltype(&CreateEventA) originalCreateEvent=nullptr;

static bool qaEnvironmentEnabled(const char* name){
    char value[4]{};
    return GetEnvironmentVariableA(name,value,sizeof(value))==1&&value[0]=='1';
}
static void qaIsolationRecord(const char* event,DWORD error=0){
    const DWORD saved=GetLastError();
    try{
        wchar_t executable[32768]{};
        const auto length=GetModuleFileNameW(nullptr,executable,32768);
        if(length&&length<32768){
            const auto folder=fs::path(executable).parent_path().parent_path()/L"logs"/L"bridge";
            fs::create_directories(folder);
            std::ofstream log(folder/(L"concurrent-isolation-"+std::to_wstring(GetCurrentProcessId())+L".jsonl"),std::ios::app);
            log<<"{\"pid\":"<<GetCurrentProcessId()<<",\"tick_ms\":"<<GetTickCount64()<<",\"thread\":"<<GetCurrentThreadId()
               <<",\"event\":\""<<event<<"\",\"error\":"<<error<<"}\n";
        }
    }catch(...){OutputDebugStringA("Concurrent QA isolation log could not be written.\n");}
    SetLastError(saved);
}
[[noreturn]] static void qaIsolationFail(const char* stage,DWORD error){
    qaIsolationRecord(stage,error);
    // Terminate only this test child. Returning would let WinMain activate the
    // unrelated GTA through the stock named-event collision path.
    TerminateProcess(GetCurrentProcess(),ERROR_INVALID_STATE);
    __fastfail(7);
}
static bool ownWindow(HWND h){DWORD pid=0;GetWindowThreadProcessId(h,&pid);return pid==GetCurrentProcessId();}
static HWND WINAPI passiveCreateWindow(DWORD ex,LPCSTR cls,LPCSTR title,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE instance,LPVOID param){
    const bool topLevel=!(style&WS_CHILD);
    auto hwnd=originalCreateWindow(topLevel?(ex|WS_EX_NOACTIVATE):ex,cls,title,topLevel?(style&~WS_VISIBLE):style,x,y,w,h,parent,menu,instance,param);
    if(hwnd&&topLevel){
        SetClassLongPtrA(hwnd,GCLP_HBRBACKGROUND,reinterpret_cast<LONG_PTR>(GetStockObject(BLACK_BRUSH)));
        RECT rc;GetClientRect(hwnd,&rc);auto dc=GetDC(hwnd);FillRect(dc,&rc,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));ReleaseDC(hwnd,dc);
        if(style&WS_VISIBLE)originalShowWindow(hwnd,SW_SHOWNOACTIVATE);
    }
    return hwnd;
}
static BOOL WINAPI passiveShowWindow(HWND h,int show){
    if(!ownWindow(h))return FALSE;
    return originalShowWindow(h,show!=SW_HIDE?SW_SHOWNOACTIVATE:show);
}
static BOOL WINAPI passiveForeground(HWND){return TRUE;}
static HWND WINAPI passiveActive(HWND){return GetActiveWindow();}
static HWND WINAPI passiveFocus(HWND){return GetFocus();}
static BOOL WINAPI passiveWindowPos(HWND h,HWND after,int x,int y,int w,int height,UINT flags){
    if(!ownWindow(h))return TRUE;
    flags|=SWP_NOACTIVATE;
    if(after==HWND_TOPMOST||after==HWND_TOP)after=HWND_BOTTOM;
    return originalWindowPos(h,after,x,y,w,height,flags);
}
static BOOL WINAPI passiveClipCursor(const RECT*){return TRUE;}
static BOOL WINAPI passiveCursorPos(int,int){return TRUE;}
static HANDLE WINAPI concurrentCreateEvent(LPSECURITY_ATTRIBUTES attributes,BOOL manualReset,BOOL initialState,LPCSTR name){
    if(!concurrentQa||!name||std::strcmp(name,"Grand theft auto San Andreas")!=0)
        return originalCreateEvent(attributes,manualReset,initialState,name);
    const auto isolated="Local\\SA-Delirium-Concurrent-QA-"+std::to_string(GetCurrentProcessId());
    const auto handle=originalCreateEvent(attributes,manualReset,initialState,isolated.c_str());
    const DWORD error=GetLastError();
    if(!handle)qaIsolationFail("singleton_create_failed",error);
    if(!singletonRedirected){singletonRedirected=true;qaIsolationRecord("native_singleton_redirected",error);}
    SetLastError(error);
    return handle;
}
static bool installPassiveWindow(){
    passiveWindow=qaEnvironmentEnabled("GTASA_PASSIVE_TEST_WINDOW");
    concurrentQa=qaEnvironmentEnabled("GTASA_CONCURRENT_QA");
    if(concurrentQa&&!passiveWindow)qaIsolationFail("passive_mode_required",ERROR_INVALID_PARAMETER);
    if(!passiveWindow)return true;
    auto status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)qaIsolationFail("minhook_initialize_failed",DWORD(status));
    struct Hook{void* target;void* replacement;void** original;};
    Hook hooks[]={
        {reinterpret_cast<void*>(&CreateWindowExA),reinterpret_cast<void*>(&passiveCreateWindow),reinterpret_cast<void**>(&originalCreateWindow)},
        {reinterpret_cast<void*>(&ShowWindow),reinterpret_cast<void*>(&passiveShowWindow),reinterpret_cast<void**>(&originalShowWindow)},
        {reinterpret_cast<void*>(&SetForegroundWindow),reinterpret_cast<void*>(&passiveForeground),reinterpret_cast<void**>(&originalForeground)},
        {reinterpret_cast<void*>(&SetActiveWindow),reinterpret_cast<void*>(&passiveActive),reinterpret_cast<void**>(&originalActive)},
        {reinterpret_cast<void*>(&SetFocus),reinterpret_cast<void*>(&passiveFocus),reinterpret_cast<void**>(&originalFocus)},
        {reinterpret_cast<void*>(&SetWindowPos),reinterpret_cast<void*>(&passiveWindowPos),reinterpret_cast<void**>(&originalWindowPos)},
        {reinterpret_cast<void*>(&ClipCursor),reinterpret_cast<void*>(&passiveClipCursor),reinterpret_cast<void**>(&originalClipCursor)},
        {reinterpret_cast<void*>(&SetCursorPos),reinterpret_cast<void*>(&passiveCursorPos),reinterpret_cast<void**>(&originalCursorPos)}};
    // Install on the same startup thread before returning to the CRT. Never
    // schedule singleton isolation on the later setup worker thread.
    for(auto& h:hooks){
        status=MH_CreateHook(h.target,h.replacement,h.original);
        if(status!=MH_OK)qaIsolationFail("passive_hook_create_failed",DWORD(status));
        status=MH_EnableHook(h.target);
        if(status!=MH_OK)qaIsolationFail("passive_hook_enable_failed",DWORD(status));
    }
    if(concurrentQa){
        status=MH_CreateHook(reinterpret_cast<void*>(&CreateEventA),reinterpret_cast<void*>(&concurrentCreateEvent),reinterpret_cast<void**>(&originalCreateEvent));
        if(status!=MH_OK)qaIsolationFail("singleton_hook_create_failed",DWORD(status));
        status=MH_EnableHook(reinterpret_cast<void*>(&CreateEventA));
        if(status!=MH_OK)qaIsolationFail("singleton_hook_enable_failed",DWORD(status));
        qaIsolationRecord("concurrent_isolation_ready");
    }else qaIsolationRecord("passive_isolation_ready");
    return true;
}
