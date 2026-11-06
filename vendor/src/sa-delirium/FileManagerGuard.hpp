#pragma once
// CFileMgr passes its FILE* straight to the CRT. CLEO+ called CFileMgr::Seek
// with a null file while a game was restarting (0x5389B0 -> _lock_file ->
// EnterCriticalSection(0x20), three crashes 02-05/11). Each CFileMgr call on a
// file now answers a null file the way it answers a failed one, whichever mod
// made the call; real files go through unchanged.
#include <windows.h>
#include <cstdio>
#include "MinHook.h"

namespace file_manager_guard {
inline int(__cdecl*originalRead)(FILE*,char*,int)=nullptr;
inline int(__cdecl*originalWrite)(FILE*,const char*,int)=nullptr;
inline bool(__cdecl*originalReadLine)(FILE*,char*,int)=nullptr;
inline bool(__cdecl*originalSeek)(FILE*,int,int)=nullptr;
inline int(__cdecl*originalClose)(FILE*)=nullptr;
inline int(__cdecl*originalLength)(FILE*)=nullptr;
inline int(__cdecl*originalError)(FILE*)=nullptr;
inline unsigned refused=0;

inline int __cdecl read(FILE* f,char* buffer,int size){if(!f){refused++;return 0;}return originalRead(f,buffer,size);}
inline int __cdecl write(FILE* f,const char* buffer,int size){if(!f){refused++;return 0;}return originalWrite(f,buffer,size);}
inline bool __cdecl readLine(FILE* f,char* buffer,int size){if(!f){refused++;return false;}return originalReadLine(f,buffer,size);}
inline bool __cdecl seek(FILE* f,int offset,int origin){if(!f){refused++;return true;}return originalSeek(f,offset,origin);}  // true: fseek failed
inline int __cdecl close(FILE* f){if(!f){refused++;return EOF;}return originalClose(f);}
inline int __cdecl length(FILE* f){if(!f){refused++;return 0;}return originalLength(f);}
inline int __cdecl error(FILE* f){if(!f){refused++;return 0x20;}return originalError(f);}  // the _IOERR bit it tests

// Queues the hooks for the next MH_ApplyQueued. Returns how many were queued.
inline int install(){
    struct Hook {uintptr_t address;void* replacement;void** original;};
    const Hook hooks[]={
        {0x538950,reinterpret_cast<void*>(&read),reinterpret_cast<void**>(&originalRead)},          // CFileMgr::Read
        {0x538970,reinterpret_cast<void*>(&write),reinterpret_cast<void**>(&originalWrite)},        // CFileMgr::Write
        {0x538990,reinterpret_cast<void*>(&readLine),reinterpret_cast<void**>(&originalReadLine)},  // CFileMgr::ReadLine
        {0x5389B0,reinterpret_cast<void*>(&seek),reinterpret_cast<void**>(&originalSeek)},          // CFileMgr::Seek
        {0x5389D0,reinterpret_cast<void*>(&close),reinterpret_cast<void**>(&originalClose)},        // CFileMgr::CloseFile
        {0x5389E0,reinterpret_cast<void*>(&length),reinterpret_cast<void**>(&originalLength)},      // CFileMgr::GetTotalSize
        {0x538A50,reinterpret_cast<void*>(&error),reinterpret_cast<void**>(&originalError)}};       // CFileMgr::GetErrorReadWrite
    int queued=0;
    for(const auto&h:hooks)if(MH_CreateHook(reinterpret_cast<void*>(h.address),h.replacement,h.original)==MH_OK&&
        MH_QueueEnableHook(reinterpret_cast<void*>(h.address))==MH_OK)queued++;
    return queued;
}
}
