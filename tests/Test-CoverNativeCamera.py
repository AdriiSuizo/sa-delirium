"""Compile the actual optional camera capture against a native host double; no GTA."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=True)
source = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
struct V {float x=0,y=0,z=0;};
static bool finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
static unsigned char cameraMemory[0x300];
static int actor=0;
static void* currentCam=cameraMemory;
static bool owner=true;
static void* activeCamera(){return currentCam;}
static void* player(){return &actor;}
static int reference(void*){return 129;}
namespace player_cover {static bool owns(void* p){return owner&&p==&actor;}}
static uintptr_t callReturn=0x522A77;
static unsigned testFrame=300;
static void* testReturn(){return reinterpret_cast<void*>(callReturn);}
#define _ReturnAddress testReturn
static void event(const char*,int,int,const char*){}
static unsigned created=0,enabled=0,removed=0,nativeCalls=0;
static bool failCreate=false,failEnable=false;
static void* capturedThis=nullptr;
static V *capturedSource=nullptr,*capturedTarget=nullptr,seenSource{},seenTarget{};
static V nativeResult{2421.081055f,-1664.766724f,14.071426f};
static void __fastcall original(void* self,void*,V* source,V* target){
    ++nativeCalls;capturedThis=self;capturedSource=source;capturedTarget=target;
    seenSource=*source;seenTarget=*target;*source=nativeResult;
}
constexpr int MH_OK=0;
static int MH_CreateHook(void*,void*,void** originalFunction){
    if(failCreate)return 1;
    ++created;*originalFunction=reinterpret_cast<void*>(&original);return MH_OK;
}
static int MH_EnableHook(void*){if(failEnable)return 1;++enabled;return MH_OK;}
static int MH_RemoveHook(void*){++removed;return MH_OK;}
#include "CoverNativeCamera.hpp"
using namespace cover_native_camera;
static void reset(){
    created=enabled=removed=0;failCreate=failEnable=installed=false;sample={};
    nativeCollision=nullptr;
}
int main(){
    const unsigned char signature[]={0x83,0xec,0x24,0x53,0x55,0x8b,0x6c,0x24,0x30,0xd9,0x45,0,0x57,0x8b,0x7c,0x24};
    const unsigned char call[]={0x8d,0x4c,0x24,0x38,0x51,0x53,0xb9,0x28,0xf0,0xb6,0,0xe8,0x19,0xd7,0xff,0xff};
    unsigned char entry[16]{},caller[16]{};
    assert(!install(entry,caller)&&!installed&&created==0);
    std::memcpy(entry,signature,16);
    assert(!install(entry,caller)&&!installed&&created==0);
    std::memcpy(caller,call,16);entry[0]^=1;
    assert(!install(entry,caller)&&!installed&&created==0);
    entry[0]^=1;failCreate=true;
    assert(!install(entry,caller)&&!installed&&created==0);
    reset();failEnable=true;
    assert(!install(entry,caller)&&!installed&&created==1&&removed==1&&!nativeCollision);
    V output{1,2,3};
    assert(!unoccludedSource(currentCam,129,300,nativeResult,nativeResult,output)&&output.x==1);
    reset();assert(install(entry,caller)&&installed&&created==1&&enabled==1);
    assert(install(entry,caller)&&created==1&&enabled==1);
    auto* camSource=reinterpret_cast<V*>(cameraMemory+0x19C);
    const V desired{2422.298828f,-1665.076172f,14.222619f};
    V target{2420.858887f,-1664.710205f,14.043827f};
    auto* main=reinterpret_cast<void*>(mainCameraAddress);
    auto capture=[&]{*camSource=desired;observeCollision(main,nullptr,camSource,&target);};
    capture();
    assert(nativeCalls==1&&capturedThis==main&&capturedSource==camSource&&capturedTarget==&target);
    assert(samePoint(seenSource,desired)&&samePoint(seenTarget,target));
    assert(sample.valid&&sample.frame==300&&sample.ref==129&&sample.cam==currentCam);
    assert(samePoint(sample.beforeSource,desired)&&samePoint(sample.afterSource,nativeResult));
    assert(unoccludedSource(currentCam,129,300,nativeResult,nativeResult,output)&&samePoint(output,desired));
    const auto good=sample;
    auto rejected=[&](void* cam,int ref,unsigned frame,V native,V pose){
        V unchanged{1,2,3};
        assert(!unoccludedSource(cam,ref,frame,native,pose,unchanged)&&unchanged.x==1&&unchanged.y==2&&unchanged.z==3);
    };
    rejected(currentCam,129,301,nativeResult,nativeResult);
    rejected(currentCam,129,299,nativeResult,nativeResult);
    rejected(currentCam,130,300,nativeResult,nativeResult);
    rejected(reinterpret_cast<void*>(0x1234),129,300,nativeResult,nativeResult);
    V downstream=nativeResult;downstream.x+=.02f;
    rejected(currentCam,129,300,downstream,nativeResult);
    rejected(currentCam,129,300,nativeResult,downstream);
    sample.afterTarget.x+=.02f;rejected(currentCam,129,300,nativeResult,nativeResult);sample=good;
    sample.beforeSource.x=std::numeric_limits<float>::quiet_NaN();
    rejected(currentCam,129,300,nativeResult,nativeResult);sample=good;
    installed=false;rejected(currentCam,129,300,nativeResult,nativeResult);installed=true;
    sample={};callReturn=0x123456;capture();assert(!sample.valid&&nativeCalls==2);
    callReturn=aimReturnAddress;sample={};*camSource=desired;
    observeCollision(reinterpret_cast<void*>(0x1234),nullptr,camSource,&target);
    assert(!sample.valid&&nativeCalls==3&&capturedThis==reinterpret_cast<void*>(0x1234));
    sample={};V foreignSource=desired;observeCollision(main,nullptr,&foreignSource,&target);
    assert(!sample.valid&&nativeCalls==4&&capturedSource==&foreignSource);
    sample={};owner=false;capture();assert(!sample.valid&&nativeCalls==5);owner=true;
    sample={};currentCam=nullptr;capture();assert(!sample.valid&&nativeCalls==6);currentCam=cameraMemory;
    nativeResult.x=std::numeric_limits<float>::infinity();capture();assert(!sample.valid&&nativeCalls==7);
    puts("PASS optional signature/hook fallback, native call identity, camera/caller/actor scope, same-frame source/target validation, downstream changes and nonfinite rejection");
}
'''
# This correction is optional: refusing its signature does not abort vehicles or physics.
lab = (repo / "tests/VehicleLab.hpp").read_text(encoding="utf-8")
assert 'if (cameraReady && !cover_native_camera::install()) event("cover_native_camera_capture_unavailable");' in lab
prepare = (repo / "tests/Prepare-VehicleLabRuntime.py").read_text(encoding="utf-8")
assert "native_cover_camera_probe::install()" not in prepare
assert "namespace native_cover_camera_probe=cover_native_camera;" in (repo / "tests/NativeCoverCameraProbe.hpp").read_text()
cpp = args.out / "cover-native-camera.cpp"
exe = args.out / "cover-native-camera.exe"
cmd = args.out / "build.cmd"
# Substitute only the fixed game's frame-counter read with host storage. A
# preferred VirtualAlloc address is not reliable under host ASLR. All capture,
# validation, hook installation and call-chaining logic is the real header.
header = (repo / "vendor/src/sa-delirium/CoverNativeCamera.hpp").read_text(encoding="utf-8")
counter = '*reinterpret_cast<const unsigned*>(0xB7CB4C)'
assert header.count(counter) == 1
(args.out / "CoverNativeCamera.hpp").write_text(header.replace(counter, "testFrame"), encoding="utf-8")
cpp.write_text(source, encoding="utf-8")
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\Tools\\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul\n'
               + f'cl /nologo /EHsc /std:c++20 /I"{repo / "vendor/src/sa-delirium"}" "{cpp.resolve()}" /Fe:"{exe.resolve()}" /link /BASE:0x10000000\n', encoding="utf-8")
subprocess.run(["cmd", "/d", "/c", str(cmd.resolve())], cwd=args.out, check=True)
result = subprocess.run([str(exe.resolve())], capture_output=True, text=True)
print(result.stdout, end="")
if result.stderr:
    print(result.stderr, end="")
result.check_returncode()
