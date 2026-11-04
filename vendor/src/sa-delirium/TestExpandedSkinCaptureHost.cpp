#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <cstdint>
#include <string>
#include <cstdio>
static bool ready=true;static std::filesystem::path folder=".";
struct NativePaletteEvaluation {unsigned frame=0;uint64_t serial=0;};
static int unsafeCalls=0;
static int reference(void*){++unsafeCalls;return -1;}
static void*pedFromRef(int){++unsafeCalls;return nullptr;}
static const NativePaletteEvaluation*currentNativePaletteEvaluation(void*){++unsafeCalls;return nullptr;}
template<class T>static T&at(void*p,size_t n){return *reinterpret_cast<T*>(static_cast<char*>(p)+n);}
static void event(const std::string&,int,int,const std::string&){}
#include "ExpandedSkinCaptureHost.hpp"
int main(){
    SetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr);SetEnvironmentVariableA("GTASA_CAPTURE_EXPANDED_SKIN",nullptr);
    captureExpandedSkinRuntime(reinterpret_cast<void*>(1),"player");
    SetEnvironmentVariableA("GTASA_CAPTURE_EXPANDED_SKIN","1");captureExpandedSkinRuntime(reinterpret_cast<void*>(1),"player");
    SetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW","1");SetEnvironmentVariableA("GTASA_CAPTURE_EXPANDED_SKIN","0");captureExpandedSkinRuntime(reinterpret_cast<void*>(1),"player");
    if(unsafeCalls||expandedSkinCaptureEnabled())return 1;
    SetEnvironmentVariableA("GTASA_CAPTURE_EXPANDED_SKIN","1");if(!expandedSkinCaptureEnabled())return 2;
    if(expandedSkinHost::ExperimentalExpandedSkinRendering)return 3;
    std::puts("PASS capture host compiled; both explicit owned-test gates required before any ped/native access; render remains OFF");
}
