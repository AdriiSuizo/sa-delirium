#pragma once
// When the game quits, everything it and its mods keep (settings, the trainer's
// toml, modloader's shutdown) is written before ExitProcess; afterwards only
// cleo.log was (06/11). The DLL detach that ExitProcess runs is where every
// normal exit failed: CLEO 4.4 notifies CLEO+, already unloaded, from its
// script cleanup (CLEO+0x11215), and Grinch Trainer's vectored handler logging
// that exception with its logger gone ends in abort() (fast fail 7 at
// +0x20432E). So the process ends at ExitProcess with its exit code and no
// detach. During play nothing changes.
#include <windows.h>
#include "MinHook.h"

namespace process_exit_guard {
inline void(WINAPI*originalExitProcess)(UINT)=nullptr;
inline void WINAPI exitProcess(UINT code){
    // Diagnostic only: GTASA_EXIT_MARKER=<file> stamps the moment ExitProcess starts.
    wchar_t marker[MAX_PATH]{};
    if(GetEnvironmentVariableW(L"GTASA_EXIT_MARKER",marker,MAX_PATH)){HANDLE h=CreateFileW(marker,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    TerminateProcess(GetCurrentProcess(),code);
    originalExitProcess(code);
}
// Queues the hook for the next MH_ApplyQueued.
inline bool install(){
    void* target=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"ExitProcess"));
    return target&&MH_CreateHook(target,reinterpret_cast<void*>(&exitProcess),reinterpret_cast<void**>(&originalExitProcess))==MH_OK&&
        MH_QueueEnableHook(target)==MH_OK;
}
}
