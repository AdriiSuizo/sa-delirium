#pragma once
enum class IvPlayback { played, unavailable, inaudible };
struct IvSample {const wchar_t* file; const char* label; DWORD handle=0; bool failed=false; unsigned attemptAt=0;};
static IvPlayback playIvSample(const wchar_t* bank,IvSample& sample,V point,float level){
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return IvPlayback::unavailable;
    const auto&camera=at<M>(reinterpret_cast<void*>(0xB6F028),0x974);
    const V delta=point-camera.pos;
    const float distance=length(delta);
    if(!std::isfinite(distance)||distance>=25.f)return IvPlayback::inaudible;

    // CMenuManager::m_nSfxVolume is a 0..64 byte on the supported 1.0 US build.
    const float effects=std::clamp(float(*reinterpret_cast<const signed char*>(0xBA6797))/64.f,0.f,1.f);
    if(effects<=0.f)return IvPlayback::inaudible;

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
        if(!module)return IvPlayback::unavailable;
        bass.device=reinterpret_cast<Bass::Device>(GetProcAddress(module,"BASS_GetDevice"));
        bass.init=reinterpret_cast<Bass::Init>(GetProcAddress(module,"BASS_Init"));
        bass.load=reinterpret_cast<Bass::Load>(GetProcAddress(module,"BASS_SampleLoad"));
        bass.channel=reinterpret_cast<Bass::Channel>(GetProcAddress(module,"BASS_SampleGetChannel"));
        bass.attribute=reinterpret_cast<Bass::Attribute>(GetProcAddress(module,"BASS_ChannelSetAttribute"));
        if(!bass.load||!bass.channel||!bass.attribute)return IvPlayback::unavailable;
        bass.play=reinterpret_cast<Bass::Play>(GetProcAddress(module,"BASS_ChannelPlay"));
        if(!bass.play)return IvPlayback::unavailable;
    }
    if(!sample.handle){
        const unsigned now=gameTime();
        if(sample.failed&&now-sample.attemptAt<2000)return IvPlayback::unavailable;
        sample.failed=true;sample.attemptAt=now;
        const auto path=folder/bank/sample.file;
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)return IvPlayback::unavailable;
        // Reuse CLEO's device if it is already initialized.
        if(bass.device&&bass.init&&bass.device()==DWORD(-1)&&
           !bass.init(-1,44100,0,nullptr,nullptr))return IvPlayback::unavailable;
        constexpr DWORD unicode=0x80000000u;
        sample.handle=bass.load(FALSE,path.c_str(),0,0,4,unicode);
        if(!sample.handle)return IvPlayback::unavailable;
        sample.failed=false;
    }
    const DWORD channel=bass.channel(sample.handle,0);
    if(!channel)return IvPlayback::unavailable;
    const float gain=std::clamp(1.f-distance/25.f,0.f,1.f);
    constexpr DWORD volume=2,pan=3;
    bass.attribute(channel,volume,level*effects*gain*gain);
    bass.attribute(channel,pan,std::clamp(dot(delta,camera.right)/std::max(distance,1.f),-.8f,.8f));
    return bass.play(channel,TRUE)?IvPlayback::played:IvPlayback::unavailable;
}
