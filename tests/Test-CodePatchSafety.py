"""Run FrozenThreads against injected Win32 thread failures, without GTA."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--baseline-ref")
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=True)
header = (subprocess.check_output(["git", "show", f"{args.baseline_ref}:vendor/src/sa-delirium/CodePatch.hpp"], cwd=repo, text=True)
          if args.baseline_ref else (repo / "vendor/src/sa-delirium/CodePatch.hpp").read_text())
(args.out / "CodePatch.hpp").write_text(header)
source = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cassert>
#include <cstdio>
#include <vector>
static unsigned cursor=0,resumes=0,closes=0;
static DWORD error=0,openFailure=0,suspendFailure=0;
static bool enumeratorFailure=false;
static std::vector<DWORD> threadIds;
static HANDLE snapshot(DWORD,DWORD){cursor=0;return reinterpret_cast<HANDLE>(1);}
static BOOL next(HANDLE,THREADENTRY32* e){
    if(cursor==threadIds.size()){error=enumeratorFailure?ERROR_ACCESS_DENIED:ERROR_NO_MORE_FILES;return FALSE;}
    e->th32ThreadID=threadIds[cursor++];e->th32OwnerProcessID=100;return TRUE;
}
static HANDLE open(DWORD,BOOL,DWORD id){
    if(id==21&&openFailure){error=openFailure;return nullptr;}
    return reinterpret_cast<HANDLE>(id);
}
static DWORD suspend(HANDLE handle){return reinterpret_cast<uintptr_t>(handle)==22&&suspendFailure?DWORD(-1):0;}
static DWORD resume(HANDLE){++resumes;return 0;}
static BOOL close(HANDLE){++closes;return TRUE;}
static BOOL context(HANDLE,CONTEXT* c){c->Eip=0;return TRUE;}
static DWORD tid(){return 10;}
static DWORD processId(){return 100;}
static DWORD lastError(){return error;}
#define CreateToolhelp32Snapshot snapshot
#define Thread32First next
#define Thread32Next next
#define OpenThread open
#define SuspendThread suspend
#define ResumeThread resume
#define CloseHandle close
#define GetThreadContext context
#define GetCurrentThreadId tid
#define GetCurrentProcessId processId
#define GetLastError lastError
#include "CodePatch.hpp"
static bool safe(){
    const std::array ranges{CodeRange{0x1234,0x1239}};
    FrozenThreads freeze(ranges);return freeze.ok();
}
int main(){
    threadIds={10,21};openFailure=ERROR_ACCESS_DENIED;
    if(safe()){puts("REPRODUCED: live inaccessible thread is ignored; code patch incorrectly allowed");return 2;}
    threadIds={10,20,21};resumes=0;
    assert(!safe()&&resumes==1); // release earlier suspended threads after failure
    openFailure=ERROR_INVALID_PARAMETER;
    assert(safe()); // vanished thread id is safe to skip
    openFailure=0;suspendFailure=1;threadIds={10,20,22};resumes=0;
    assert(!safe()&&resumes==1);
    suspendFailure=0;threadIds={10,20,21,22};resumes=0;
    assert(safe()&&resumes==3);
    enumeratorFailure=true;threadIds={10,20};resumes=0;
    assert(!safe()&&resumes==1);
    puts("PASS fail closed on open/suspend/enumeration errors; dead-thread tolerance; cleanup and normal path");
}
'''
cpp = args.out / "patch-safety.cpp"
exe = args.out / "native-thread-test.exe"
cmd = args.out / "build.cmd"
cpp.write_text(source)
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\Tools\\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul\n'
               + f'cl /nologo /EHsc /std:c++20 "{cpp.resolve()}" /Fe:"{exe.resolve()}"\n')
subprocess.run(["cmd", "/d", "/c", str(cmd.resolve())], cwd=args.out, check=True)
result = subprocess.run([str(exe.resolve())], capture_output=True, text=True)
print(result.stdout, end="")
if result.stderr:
    print(result.stderr, end="")
if args.baseline_ref:
    assert result.returncode == 2 and "REPRODUCED:" in result.stdout
else:
    result.check_returncode()
