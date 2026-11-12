#pragma once

static void playIvMeleeImpact(int weapon,int part,V point,int ref){
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return;
    const auto&camera=at<M>(reinterpret_cast<void*>(0xB6F028),0x974);
    const V delta=point-camera.pos;
    const float distance=length(delta);
    if(!std::isfinite(distance)||distance>=25.f)return;

    // CMenuManager::m_nSfxVolume is a 0..64 byte on the supported 1.0 US build.
    const float effects=std::clamp(float(*reinterpret_cast<const signed char*>(0xBA6797))/64.f,0.f,1.f);
    if(effects<=0.f)return;

    struct Bass {
        using Load=DWORD(WINAPI*)(BOOL,const void*,unsigned long long,DWORD,DWORD,DWORD);
        using Channel=DWORD(WINAPI*)(DWORD,DWORD);
        using Attribute=BOOL(WINAPI*)(DWORD,DWORD,float);
        using Play=BOOL(WINAPI*)(DWORD,BOOL);
        using Device=DWORD(WINAPI*)();
        using Init=BOOL(WINAPI*)(int,DWORD,DWORD,HWND,const GUID*);
        Device device{};
        Init init{};
        Load load{};
        Channel channel{};
        Attribute attribute{};
        Play play{};
    };
    static Bass bass;
    if(!bass.play){
        const auto module=GetModuleHandleW(L"bass.dll");
        if(!module)return;
        bass.device=reinterpret_cast<Bass::Device>(GetProcAddress(module,"BASS_GetDevice"));
        bass.init=reinterpret_cast<Bass::Init>(GetProcAddress(module,"BASS_Init"));
        bass.load=reinterpret_cast<Bass::Load>(GetProcAddress(module,"BASS_SampleLoad"));
        bass.channel=reinterpret_cast<Bass::Channel>(GetProcAddress(module,"BASS_SampleGetChannel"));
        bass.attribute=reinterpret_cast<Bass::Attribute>(GetProcAddress(module,"BASS_ChannelSetAttribute"));
        if(!bass.load||!bass.channel||!bass.attribute)return;
        bass.play=reinterpret_cast<Bass::Play>(GetProcAddress(module,"BASS_ChannelPlay"));
        if(!bass.play)return;
    }
    struct Sample {const wchar_t* file;const char* label;DWORD handle;};
    static Sample samples[]={
        {L"bat.wav","bat",0},{L"fist2.wav","fist2",0},{L"fist3.wav","fist3",0},
        {L"fist5.wav","fist5",0},{L"face1.wav","face1",0}
    };
    const auto variety=(gameTime()^unsigned(ref))%3u;
    auto&sample=samples[weapon==5?0:part==12?4:1+variety];
    if(!sample.handle){
        const auto path=folder/L"melee-audio"/sample.file;
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)return;
        // Reuse CLEO's device if it is already initialized.
        if(bass.device&&bass.init&&bass.device()==DWORD(-1)&&
           !bass.init(-1,44100,0,nullptr,nullptr))return;
        constexpr DWORD unicode=0x80000000u;
        sample.handle=bass.load(FALSE,path.c_str(),0,0,4,unicode);
        if(!sample.handle)return;
    }
    const DWORD channel=bass.channel(sample.handle,0);
    if(!channel)return;
    const float gain=std::clamp(1.f-distance/25.f,0.f,1.f);
    constexpr DWORD volume=2,pan=3;
    bass.attribute(channel,volume,.62f*effects*gain*gain);
    bass.attribute(channel,pan,std::clamp(dot(delta,camera.right)/std::max(distance,1.f),-.8f,.8f));
    if(bass.play(channel,TRUE))event("iv_melee_audio",ref,-1,sample.label);
}
