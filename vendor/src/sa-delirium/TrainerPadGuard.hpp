#pragma once
// Grinch Trainer 1.0 redirects game CALLs to hook entries of one template
// (push ebp; mov ebp,esp; lea eax,[ebp+8]; push eax; call body). The body calls
// the original it keeps at [object+0x18] (cmp [edi+20h],0 / push [ebx] /
// mov eax,[edi+18h]). Neither path returns safely in this build: with the
// original still null it calls address 0, and once it is set it returned two
// bytes into the next instruction (opcode 0494 at 0x48AEEC, fast-food menu,
// 06/11). The trainer has five entries of that template; in the running game
// four keep the right original (0x53EBF5 frame end, 0x5B864C/0x5B8654 level
// loading) and only 0x48AEEC's stays null. The trainer binary is shipped
// unmodified, so a game CALL/JMP pointing at such an entry is undone when the
// entry's original differs from the target of the call gta_sa.exe has there.
// The null original came from a site the trainer took for a plain call: GInput
// had rewritten opcode 0494's pad read as push esi / call its handler / jmp
// 0x48AF15, and the trainer's 5 bytes left the last byte of GInput's call and
// its jmp, which ran as code (0x48AEF3, shop menus, 06/11). So the bytes go
// back to what the other mod wrote when it is known (GInput, rebuilt from its
// own installer and checked against what is left), and otherwise to the game's
// own code over every byte that differs from the exe, never half of a patch.
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "CodePatch.hpp"

namespace trainer_hook_guard {
constexpr std::uint8_t EntryBytes[8]{0x55,0x8B,0xEC,0x8D,0x45,0x08,0x50,0xE8};
constexpr std::uint8_t BodyBytes[11]{0x83,0x7F,0x20,0x00,0x75,0x00,0xFF,0x33,0x8B,0x47,0x18}; // [5]: any jne offset
constexpr size_t BodyReach=0x120,ScanBytesPerFrame=0x10000;
struct Range {uintptr_t begin=0,end=0;};
struct State {
    bool prepared=false,disabled=false;
    struct Entry {uintptr_t address,object;};  // hook entry, global holding its hook object
    std::vector<Entry> entries;
    Range code;                       // gta_sa code section in memory
    std::vector<std::uint8_t> clean;  // the same bytes from gta_sa.exe on disk
    uintptr_t cursor=0;
};
inline State state;

inline const IMAGE_SECTION_HEADER* codeSection(const std::uint8_t* image){
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return nullptr;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return nullptr;
    const auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)if(section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE)return &section[i];
    return nullptr;
}
inline bool bodyMatches(const std::uint8_t* p){
    for(size_t i=0;i<sizeof(BodyBytes);i++)if(i!=5&&p[i]!=BodyBytes[i])return false;
    return true;
}
// The body's first call is the lookup of its static hook object; the lookup
// copies the object from its global (mov ecx,[global] / mov [eax],ecx).
inline uintptr_t objectGlobal(uintptr_t body,uintptr_t begin,uintptr_t end){
    for(uintptr_t a=body;a<body+0x60&&a+5<=end;a++){
        if(*reinterpret_cast<const std::uint8_t*>(a)!=0xE8)continue;
        std::int32_t rel;std::memcpy(&rel,reinterpret_cast<const void*>(a+1),4);const uintptr_t lookup=a+5+rel;
        if(lookup<begin||lookup+0xA8>end)return 0;
        for(uintptr_t b=lookup;b<lookup+0xA0;b++){const auto* p=reinterpret_cast<const std::uint8_t*>(b);
            if(p[0]==0x8B&&p[1]==0x0D&&p[6]==0x89&&p[7]==0x08){uintptr_t global;std::memcpy(&global,p+2,4);return global;}}
        return 0;
    }
    return 0;
}
inline std::vector<State::Entry> brokenEntries(HMODULE trainer){
    std::vector<State::Entry> found;
    const auto* base=reinterpret_cast<const std::uint8_t*>(trainer);const auto* section=codeSection(base);if(!section)return found;
    const uintptr_t begin=uintptr_t(base)+section->VirtualAddress,end=begin+section->Misc.VirtualSize;
    for(uintptr_t a=begin;a+12<=end;a++){
        if(std::memcmp(reinterpret_cast<const void*>(a),EntryBytes,sizeof(EntryBytes))!=0)continue;
        std::int32_t rel;std::memcpy(&rel,reinterpret_cast<const void*>(a+8),4);
        const uintptr_t body=a+12+rel;
        if(body<begin||body+BodyReach+sizeof(BodyBytes)>end)continue;
        bool matches=false;for(uintptr_t b=body;b<body+BodyReach&&!matches;b++)matches=bodyMatches(reinterpret_cast<const std::uint8_t*>(b));
        if(matches)if(const uintptr_t global=objectGlobal(body,begin,end))found.push_back({a,global});
    }
    return found;
}
inline bool readCleanCode(Range&code,std::vector<std::uint8_t>&clean){
    const auto* image=reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* section=codeSection(image);if(!section)return false;
    wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(nullptr,path,MAX_PATH))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const DWORD size=std::min<DWORD>(section->SizeOfRawData,section->Misc.VirtualSize);
    clean.resize(size);DWORD got=0;LARGE_INTEGER at{};at.QuadPart=section->PointerToRawData;
    const bool read=SetFilePointerEx(file,at,nullptr,FILE_BEGIN)&&ReadFile(file,clean.data(),size,&got,nullptr)&&got==size;
    CloseHandle(file);if(!read)return false;
    code.begin=uintptr_t(image)+section->VirtualAddress;code.end=code.begin+size;return true;
}
// GInput's installer (+0xCD15): mov byte [0x48AEED],0E8h / mov eax,handler.
inline bool ginputPatch(uintptr_t site,std::uint8_t out[5]){
    if(site!=0x48AEEC)return false;
    const auto ginput=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"GInputSA.asi"));if(!ginput)return false;
    constexpr std::uint8_t Installer[8]{0xC6,0x05,0xED,0xAE,0x48,0x00,0xE8,0xB8};
    const auto* installer=reinterpret_cast<const std::uint8_t*>(ginput+0xCD15);
    if(std::memcmp(installer,Installer,sizeof(Installer))!=0)return false;
    uintptr_t handler;std::memcpy(&handler,installer+8,4);if(handler<ginput||handler>=ginput+0x100000)return false;
    const std::uint32_t rel=static_cast<std::uint32_t>(handler-(site+6));
    const auto* left=reinterpret_cast<const std::uint8_t*>(site+5);
    if(left[0]!=std::uint8_t(rel>>24)||left[1]!=0xEB||left[2]!=0x21)return false;
    out[0]=0x56;out[1]=0xE8;std::memcpy(out+2,&rel,3);return true;
}
inline bool restore(uintptr_t site,const State::Entry&entry,const char*&how){
    const std::uint8_t* disk=state.clean.data()+(site-state.code.begin);
    if(disk[0]!=0xE8&&disk[0]!=0xE9)return false;
    std::int32_t rel;std::memcpy(&rel,disk+1,4);const uintptr_t original=site+5+rel;
    if(original<state.code.begin||original>=state.code.end)return false;
    // A missing object is created on the first call with a null original.
    const uintptr_t object=*reinterpret_cast<const uintptr_t*>(entry.object);
    if(object&&*reinterpret_cast<const uintptr_t*>(object+0x18)==original)return false;
    const auto* live=reinterpret_cast<const std::uint8_t*>(site);
    std::uint8_t bytes[16];size_t length=5;
    if(ginputPatch(site,bytes))how="ginput";
    else if(std::memcmp(live+5,disk+5,3)==0){std::memcpy(bytes,disk,5);how="exe";}
    else{while(length<sizeof(bytes)&&site+length<state.code.end&&live[length]!=disk[length])length++;
        std::memcpy(bytes,disk,length);how="exe_run";}
    DWORD old=0;bool written=false;
    {
        FrozenThreads frozen(std::array<CodeRange,1>{{{site,site+length}}});
        if(frozen.ok()&&VirtualProtect(reinterpret_cast<void*>(site),length,PAGE_EXECUTE_READWRITE,&old)){
            std::memcpy(reinterpret_cast<void*>(site),bytes,length);
            VirtualProtect(reinterpret_cast<void*>(site),length,old,&old);
            FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),length);written=true;
        }
    }
    return written;
}
// Main thread, once per frame. The first call scans all of gta_sa's code (the
// trainer installs its hooks at load); later calls scan one slice each, for
// hooks it installs when a feature is used. Returns the sites restored now.
struct Restored {unsigned count=0;uintptr_t sites[8]{},targets[8]{};const char* how[8]{};};
inline Restored maintain(){
    Restored result;
    if(state.disabled)return result;
    bool whole=false;
    if(!state.prepared){
        HMODULE trainer=GetModuleHandleW(L"GrinchTrainerSA.asi");if(!trainer)return result;
        state.prepared=true;
        // Diagnostic only: GTASA_TRAINER_HOOK_GUARD=0 leaves the trainer's hooks as installed.
        char off[4]{};if(GetEnvironmentVariableA("GTASA_TRAINER_HOOK_GUARD",off,sizeof off)&&off[0]=='0'){state.disabled=true;return result;}state.entries=brokenEntries(trainer);
        if(state.entries.empty()||!readCleanCode(state.code,state.clean)){state.disabled=true;return result;}
        std::sort(state.entries.begin(),state.entries.end(),[](const State::Entry&x,const State::Entry&y){return x.address<y.address;});state.cursor=state.code.begin;whole=true;
    }
    const uintptr_t last=state.code.end-5,stop=whole?last:std::min(last,state.cursor+ScanBytesPerFrame);
    for(uintptr_t a=state.cursor;a<stop;a++){
        const std::uint8_t op=*reinterpret_cast<const std::uint8_t*>(a);if(op!=0xE8&&op!=0xE9)continue;
        std::int32_t rel;std::memcpy(&rel,reinterpret_cast<const void*>(a+1),4);
        const uintptr_t entry=a+5+rel;
        const auto hit=std::lower_bound(state.entries.begin(),state.entries.end(),entry,[](const State::Entry&x,uintptr_t v){return x.address<v;});
        const char* how="";
        if(hit!=state.entries.end()&&hit->address==entry&&restore(a,*hit,how)){
            if(result.count<8){result.sites[result.count]=a;result.targets[result.count]=entry;result.how[result.count]=how;}
            result.count++;a+=4;}
    }
    state.cursor=stop>=last?state.code.begin:stop;
    return result;
}
inline size_t entryCount(){return state.entries.size();}
}
