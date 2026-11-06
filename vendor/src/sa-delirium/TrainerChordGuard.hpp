#pragma once
// Requested by the user (06/11): the trainer menu opens only with
// Cross+Square+Circle. Grinch Trainer's own gamepad chord is two buttons
// (X+A in XInput = Square+Cross, which is also sprint+jump in GInput) and it
// reads the pad itself through XInputGetState. Only the trainer's samples are
// changed: X+A without B is reported to it as A alone. GInput and the game see
// the real pad. The trainer binary and its config stay unmodified.
namespace trainer_chord_guard {
using XInputGetStateFn=DWORD(WINAPI*)(DWORD,void*);
inline XInputGetStateFn originals[3]{};
inline bool tried[3]{};
inline HMODULE trainer=nullptr;
template<int N> DWORD WINAPI filtered(DWORD index,void*state){
    const DWORD result=originals[N](index,state);
    if(result!=ERROR_SUCCESS||!state||!trainer)return result;
    HMODULE caller=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(_ReturnAddress()),&caller);
    if(caller!=trainer)return result;
    // XINPUT_STATE: dwPacketNumber, then XINPUT_GAMEPAD.wButtons.
    auto&buttons=*reinterpret_cast<WORD*>(static_cast<unsigned char*>(state)+4);
    constexpr WORD A=0x1000,B=0x2000,X=0x4000;
    if((buttons&A)&&(buttons&X)&&!(buttons&B))buttons&=WORD(~X);
    return result;
}
// Called each frame: ImGui loads its XInput DLL after the device exists.
inline int maintain(){
    if(!trainer)trainer=GetModuleHandleW(L"GrinchTrainerSA.asi");
    if(!trainer)return 0;
    static const wchar_t* names[3]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"};
    static void* hooks[3]={reinterpret_cast<void*>(&filtered<0>),reinterpret_cast<void*>(&filtered<1>),reinterpret_cast<void*>(&filtered<2>)};
    int installed=0;
    for(int i=0;i<3;i++){
        if(tried[i]){installed+=originals[i]!=nullptr;continue;}
        HMODULE m=GetModuleHandleW(names[i]);if(!m)continue;
        tried[i]=true;void* fn=reinterpret_cast<void*>(GetProcAddress(m,"XInputGetState"));if(!fn)continue;
        if(MH_CreateHook(fn,hooks[i],reinterpret_cast<void**>(&originals[i]))==MH_OK&&MH_EnableHook(fn)==MH_OK)installed++;
        else originals[i]=nullptr;
    }
    return installed;
}
}
