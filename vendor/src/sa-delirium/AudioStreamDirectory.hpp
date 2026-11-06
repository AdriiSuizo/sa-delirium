#pragma once
// Radio stream opening without changing the process working directory.
//
// CAEMP3TrackLoader::GetDataStream (0x4E0D20) runs on the audio stream
// thread. It builds the relative path "AUDIO\STREAMS\<pack>", calls
// CFileMgr::SetDir("") (0x4E0E3D -> 0x5387D0, a _chdir of the whole process)
// and lets CAEDataStream::Initialise (0x4DC2B0) open that relative path with
// CreateFileA. Meanwhile the main thread uses CFileMgr::SetDir("data\script")
// followed by a relative CFileMgr::OpenFile; CTheScripts::Init (0x468EB5) then
// reads main.scm without a null check. When the stream thread's chdir lands
// between those two calls the open fails and the read of a NULL FILE crashes
// in RtlEnterCriticalSection (write to 0x24). Observed while loading a save,
// when the radio starts: logs/cases/*/gta_sa.exe.*.log since 2026-10-28 and
// logs/bridge/file-diagnostics-*.jsonl (2026-11-05).
//
// The call at 0x4E0E3D is redirected to makeStreamPathAbsolute: the stream's
// file name becomes "<game folder>\AUDIO\STREAMS\<pack>" (game allocator, the
// stream frees it with the game's delete) and no directory change happens on
// the stream thread. Absolute names (DVD path, Mod Loader std.tracks
// overrides) are kept. Installed from InitializeASI on the main thread, before
// the audio thread exists.
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace audio_stream_directory {
constexpr uintptr_t SetDirCall=0x4E0E3D;   // call CFileMgr::SetDir inside GetDataStream
constexpr uintptr_t SetDir=0x5387D0;       // CFileMgr::SetDir(const char*)
constexpr uintptr_t GameNew=0x82119A;      // operator new used by GetDataStream (0x4E0DA2)
constexpr uintptr_t GameDelete=0x8214BD;   // operator delete used by ~CAEDataStream
constexpr size_t FileNameOffset=8;         // CAEDataStream::m_pszFilename, read by 0x4DC2BE

inline char gameFolder[MAX_PATH]{};
inline size_t gameFolderLength=0;

inline bool isAbsolute(const char* name){
    return name&&((name[0]&&name[1]==':')||(name[0]=='\\'&&name[1]=='\\'));
}
extern "C" inline void __cdecl makeStreamPathAbsoluteFor(void* stream){
    if(!stream||!gameFolderLength)return;
    auto& name=*reinterpret_cast<char**>(static_cast<std::uint8_t*>(stream)+FileNameOffset);
    if(!name||isAbsolute(name))return;
    const size_t length=gameFolderLength+std::strlen(name)+1;
    auto* full=reinterpret_cast<char*(__cdecl*)(size_t)>(GameNew)(length);
    if(!full)return; // keep the relative name; the root is the usual directory
    std::memcpy(full,gameFolder,gameFolderLength);
    std::strcpy(full+gameFolderLength,name);
    reinterpret_cast<void(__cdecl*)(void*)>(GameDelete)(name);
    name=full;
}
// Called in place of CFileMgr::SetDir(""): the caller pushed one argument and
// pops it itself; ESI holds the new CAEDataStream or zero. EAX/ECX/EDX are
// not used by the caller after the call (0x4E0E42..0x4E0E47).
__declspec(naked) inline void makeStreamPathAbsolute(){
    __asm {
        push esi
        call makeStreamPathAbsoluteFor
        add esp,4
        ret
    }
}

// Result for the adapter log: "installed", "already_different" (another
// plugin owns the site: left untouched) or "failed".
inline const char* install(){
    DWORD length=GetModuleFileNameA(nullptr,gameFolder,MAX_PATH);
    if(!length||length>=MAX_PATH)return "failed";
    while(length&&gameFolder[length-1]!='\\')length--;
    if(!length)return "failed";
    gameFolder[length]=0;gameFolderLength=length; // keeps the trailing backslash
    auto* site=reinterpret_cast<std::uint8_t*>(SetDirCall);
    std::int32_t relative=0;std::memcpy(&relative,site+1,4);
    if(site[0]!=0xE8||SetDirCall+5+relative!=SetDir)return "already_different";
    DWORD old=0;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old))return "failed";
    const std::int32_t replacement=static_cast<std::int32_t>(reinterpret_cast<uintptr_t>(&makeStreamPathAbsolute)-(SetDirCall+5));
    std::memcpy(site+1,&replacement,4);
    VirtualProtect(site,5,old,&old);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    return "installed";
}
}
