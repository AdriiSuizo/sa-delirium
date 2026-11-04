#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstring>

// Explicit per-launch CPU observation. No timers or file writes on normal play.
struct FrameCostProfile {
    struct Output {
        std::ofstream file;
        bool allIntervals=false,allFrames=false,threadCycles=false;
        unsigned bufferedRows=0;
        Output(){
            const DWORD n=GetEnvironmentVariableW(L"GTASA_FRAME_PROFILE",nullptr,0);
            if(n<2||n>32768)return;
            std::wstring path(n,L'\0');GetEnvironmentVariableW(L"GTASA_FRAME_PROFILE",path.data(),n);path.resize(n-1);
            file.open(std::filesystem::path(path),std::ios::app);
            wchar_t passive[2]{},all[2]{};
            const bool controlled=GetEnvironmentVariableW(L"GTASA_PASSIVE_TEST_WINDOW",passive,2)==1&&passive[0]==L'1';
            allIntervals=controlled&&GetEnvironmentVariableW(L"GTASA_FRAME_PROFILE_ALL_INTERVALS",all,2)==1&&all[0]==L'1';
            allFrames=controlled&&GetEnvironmentVariableW(L"GTASA_FRAME_PROFILE_ALL_SCOPES",all,2)==1&&all[0]==L'1';
            threadCycles=controlled&&GetEnvironmentVariableW(L"GTASA_FRAME_PROFILE_CYCLES",all,2)==1&&all[0]==L'1';
        }
    };
    static Output& output(){static Output o;return o;}
    static double clock(){static const double hz=[](){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(f.QuadPart);}();
        LARGE_INTEGER t;QueryPerformanceCounter(&t);return double(t.QuadPart)*1000./hz;}
    static ULONG64 currentCycles(){
        ULONG64 value=0;QueryThreadCycleTime(GetCurrentThread(),&value);return value;
    }
    static ULONG64 elapsedCycles(ULONG64 start,ULONG64 end){return start&&end>=start?end-start:0;}
    const char*scope;bool enabled;double started=0,last=0,threshold;std::array<double,8> phases{};
    ULONG64 startCycles=0,lastCycles=0;std::array<ULONG64,8> phaseCycles{};
    float inputDt=0;int substeps=0;
    unsigned frame=0,game=0;ULONGLONG wall=0;int active,pending,ref,model;
    FrameCostProfile(const char*s,int a,int p,int r=-1,int m=-1,double limit=8.):
        scope(s),enabled(output().file.is_open()),threshold(limit),active(a),pending(p),ref(r),model(m){
        if(enabled){started=last=clock();frame=*reinterpret_cast<unsigned*>(0xB7CB4C);game=gameTime();wall=GetTickCount64();
            if(output().threadCycles)startCycles=lastCycles=currentCycles();
        }
        if(enabled&&(output().allFrames||(output().allIntervals&&std::strcmp(scope,"physics_interval")==0)))threshold=0.;
    }
    void lap(size_t i){
        if(!enabled)return;
        const double t=clock();phases[i]+=t-last;last=t;
        if(startCycles){const auto now=currentCycles();phaseCycles[i]+=elapsedCycles(lastCycles,now);lastCycles=now;}
    }
    ~FrameCostProfile(){
        if(!enabled)return;const double ms=clock()-started;if(ms<threshold)return;
        const auto cycles=startCycles?elapsedCycles(startCycles,currentCycles()):0;
        auto&f=output().file;f<<std::setprecision(9)<<"{\"scope\":\""<<scope<<"\",\"frame\":"<<frame
            <<",\"game_ms\":"<<game<<",\"wall_ms\":"<<wall<<",\"qpc_ms\":"<<std::setprecision(15)<<started
            <<std::setprecision(9)<<",\"ms\":"<<ms<<",\"active\":"<<active
            <<",\"input_dt\":"<<inputDt<<",\"substeps\":"<<substeps<<",\"pending\":"<<pending<<",\"ref\":"<<ref<<",\"model\":"<<model<<",\"phases_ms\":[";
        for(size_t i=0;i<phases.size();i++){if(i)f<<',';f<<phases[i];}
        f<<']';
        if(startCycles){
            f<<",\"thread_cycles\":"<<cycles<<",\"phase_cycles\":[";
            for(size_t i=0;i<phaseCycles.size();i++){if(i)f<<',';f<<phaseCycles[i];}
            f<<']';
        }
        f<<"}\n";
        auto&out=output();
        if(!out.allFrames||++out.bufferedRows==64){f.flush();out.bufferedRows=0;}
    }
};
