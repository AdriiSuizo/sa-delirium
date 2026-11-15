"""Exercise the shipping camera hook with a mocked native host, without GTA.

--baseline-ref compiles the old installer function itself and reproduces the
missing cover callback when the independent drive-by option is disabled.
"""
import argparse
from pathlib import Path
import subprocess


def function(text, name):
    start = text.index(name)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--baseline-ref")
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=True)
prefix = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
static unsigned coverCalls=0, driveCalls=0, nativeCalls=0;
static void* receivedCamera=nullptr;
static bool receivedMirror=false, receivedOriented=false;
static uintptr_t returnAddress=0x52C988;
static void* testReturnAddress(){return reinterpret_cast<void*>(returnAddress);}
#define _ReturnAddress testReturnAddress
namespace player_cover { static void applyCamera(void*){++coverCalls;} }
namespace drive_by_controls {
constexpr uintptr_t mainCamera=0xB6F028;
static bool installed=false;
static void applyAimView(void*){++driveCalls;}
}
static void __fastcall nativeDerived(void* self,void*,bool mirror,bool oriented){
    ++nativeCalls;receivedCamera=self;receivedMirror=mirror;receivedOriented=oriented;
}
static void* detour=nullptr;
static unsigned created=0,queued=0,removed=0;
static bool failCreate=false,failQueue=false;
constexpr int MH_OK=0;
static int MH_CreateHook(void*,void* fn,void** original){
    if(failCreate)return 1;
    ++created;detour=fn;*original=reinterpret_cast<void*>(&nativeDerived);return MH_OK;
}
static int MH_QueueEnableHook(void*){if(failQueue)return 1;++queued;return MH_OK;}
static int MH_RemoveHook(void*){++removed;detour=nullptr;return MH_OK;}
static void reset(){created=queued=removed=0;detour=nullptr;failCreate=failQueue=false;}
'''

if args.baseline_ref:
    old = subprocess.check_output(["git", "show", f"{args.baseline_ref}:tests/DriveByControls.hpp"],
                                  cwd=repo, text=True)
    install = function(old, "static bool install(const std::filesystem::path&ini)")
    source = prefix + r'''
static unsigned disabledOption(const wchar_t*,const wchar_t*,int,const wchar_t*){return 0;}
#define GetPrivateProfileIntW disabledOption
static void event(const char*,int,int,std::string){}
namespace drive_by_controls {
static void stub(){}
static void *nativeFire=nullptr,*nativeHandbrake=nullptr,*nativeLookLeft=nullptr,*nativeLookRight=nullptr,
    *nativeVehicleAxisX=nullptr,*nativeVehicleAxisY=nullptr,*nativeHydraulics=nullptr,*nativeDerivedCamera=nullptr,
    *nativeDriveBy=nullptr,*nativeCarBullet=nullptr,*nativeCrosshair=nullptr;
static bool recognizedPadEntry(uintptr_t){return true;}
static auto fire=stub,handbrake=stub,left=stub,right=stub,vehicleAxisX=stub,vehicleAxisY=stub,
    hydraulics=stub,deriveCamera=stub,driveBy=stub,carBullet=stub,crosshair=stub;
''' + install + r'''
}
int main(){
    if(!drive_by_controls::install(L"disabled-drive-by.ini"))return 3;
    if(!queued){puts("REPRODUCED: drive-by off leaves cover camera hook missing");return 2;}
    return 0;
}
'''
else:
    # Validate the actual build overlay connects the independent hook before
    # consulting drive-by options, so testing the helper also tests packaging.
    lab = (repo / "tests/VehicleLab.hpp").read_text()
    assert lab.index("camera_pose_hooks::install()") < lab.index("drive_by_controls::install(ini)")
    controls = (repo / "tests/DriveByControls.hpp").read_text()
    assert "0x5150E0" not in controls
    prepare = (repo / "tests/Prepare-VehicleLabRuntime.py").read_text()
    assert 'shutil.copyfile(here / "CameraPoseHooks.hpp", args.out / "CameraPoseHooks.hpp")' in prepare
    source = prefix + '#include "CameraPoseHooks.hpp"\n' + r'''
int main(){
    auto* block=VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(block);
    auto* entry=block;
    assert(!camera_pose_hooks::install(entry)&&created==0&&queued==0);
    const unsigned char signature[]={0x6a,0xff,0x68,0x78,0xc7,0x83,0,0x64};
    std::memcpy(entry,signature,sizeof(signature));
    failCreate=true;
    assert(!camera_pose_hooks::install(entry)&&created==0&&queued==0);
    reset();failQueue=true;
    assert(!camera_pose_hooks::install(entry)&&created==1&&queued==0&&removed==1);
    reset();
    assert(camera_pose_hooks::install(entry)&&created==1&&queued==1&&removed==0&&detour);
    const auto call=reinterpret_cast<void(__fastcall*)(void*,void*,bool,bool)>(detour);
    auto* main=reinterpret_cast<void*>(drive_by_controls::mainCamera);
    // Option off (or rejected drive-by hooks): cover still runs, native camera
    // always chains, and no uninstalled drive-by feature is invoked.
    call(main,nullptr,true,false);
    assert(coverCalls==1&&driveCalls==0&&nativeCalls==1&&receivedCamera==main&&receivedMirror&&!receivedOriented);
    // Option on: retain the existing order and both camera adjustments.
    drive_by_controls::installed=true;
    call(main,nullptr,false,true);
    assert(coverCalls==2&&driveCalls==1&&nativeCalls==2&&!receivedMirror&&receivedOriented);
    returnAddress=0x123456;
    call(main,nullptr,false,false);
    assert(coverCalls==2&&driveCalls==1&&nativeCalls==3);
    returnAddress=0x52C988;
    call(reinterpret_cast<void*>(0x123400),nullptr,false,false);
    assert(coverCalls==2&&driveCalls==1&&nativeCalls==4&&receivedCamera==reinterpret_cast<void*>(0x123400));
    VirtualFree(block,0,MEM_RELEASE);
    puts("PASS independent cover camera; drive-by off/on; foreign camera/caller; signature and hook failures");
}
'''

cpp = args.out / "camera-hooks.cpp"
exe = args.out / "camera-hooks.exe"
cmd = args.out / "build.cmd"
cpp.write_text(source, encoding="utf-8")
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\Tools\\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul\n'
               + f'cl /nologo /EHsc /std:c++20 /I"{repo / "tests"}" "{cpp.resolve()}" /Fe:"{exe.resolve()}" /link /BASE:0x10000000\n',
               encoding="utf-8")
subprocess.run(["cmd", "/d", "/c", str(cmd.resolve())], cwd=args.out, check=True)
result = subprocess.run([str(exe.resolve())], capture_output=True, text=True)
print(result.stdout, end="")
if result.stderr:
    print(result.stderr, end="")
if args.baseline_ref:
    assert result.returncode == 2 and "REPRODUCED:" in result.stdout
else:
    result.check_returncode()
