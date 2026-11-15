// Runs without GTA, window creation, OS input or real MinHook installation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstring>
#include <intrin.h>
#include <cassert>
#include "MinHook.h"
namespace fs=std::filesystem;
static int createHooks=0,enableHooks=0;
static bool failCreate=false,failEnable=false;
extern "C" MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){
    ++createHooks;return failCreate?MH_ERROR_UNSUPPORTED_FUNCTION:MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){
    ++enableHooks;return failEnable?MH_ERROR_MEMORY_PROTECT:MH_OK;
}
#include "ConcurrentQaIsolation.hpp"
static int calls=0;
static std::string receivedName;
static HANDLE WINAPI fakeCreateEvent(LPSECURITY_ATTRIBUTES a,BOOL manual,BOOL initial,LPCSTR name){
    ++calls;assert(a==nullptr&&manual==FALSE&&initial==TRUE);
    receivedName=name?name:"<null>";SetLastError(1234);
    return reinterpret_cast<HANDLE>(123);
}
static BOOL WINAPI forbiddenShow(HWND,int){assert(false);return FALSE;}
static BOOL WINAPI forbiddenPos(HWND,HWND,int,int,int,int,UINT){assert(false);return FALSE;}
int main(int argc,char** argv){
    if(argc>1){
        SetEnvironmentVariableA("GTASA_CONCURRENT_QA","1");
        SetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",std::strcmp(argv[1],"missing-passive")==0?"0":"1");
        failCreate=std::strcmp(argv[1],"create-failure")==0;
        failEnable=std::strcmp(argv[1],"enable-failure")==0;
        installPassiveWindow();
        // Every requested failure must terminate this child, never reach here.
        return 99;
    }
    SetEnvironmentVariableA("GTASA_CONCURRENT_QA",nullptr);
    SetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW","10");
    assert(!qaEnvironmentEnabled("GTASA_PASSIVE_TEST_WINDOW"));
    installPassiveWindow();assert(createHooks==0&&enableHooks==0);
    SetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW","1");
    SetEnvironmentVariableA("GTASA_CONCURRENT_QA","1");
    installPassiveWindow();assert(createHooks==9&&enableHooks==9&&concurrentQa);
    originalCreateEvent=&fakeCreateEvent;
    concurrentQa=false;
    concurrentCreateEvent(nullptr,FALSE,TRUE,"Grand theft auto San Andreas");
    assert(receivedName=="Grand theft auto San Andreas"&&GetLastError()==1234);
    concurrentQa=true;
    concurrentCreateEvent(nullptr,FALSE,TRUE,"Unrelated event");assert(receivedName=="Unrelated event");
    concurrentCreateEvent(nullptr,FALSE,TRUE,nullptr);assert(receivedName=="<null>");
    concurrentCreateEvent(nullptr,FALSE,TRUE,"grand theft auto San Andreas");assert(receivedName=="grand theft auto San Andreas");
    const auto result=concurrentCreateEvent(nullptr,FALSE,TRUE,"Grand theft auto San Andreas");
    assert(result==reinterpret_cast<HANDLE>(123)&&GetLastError()==1234);
    assert(receivedName=="Local\\SA-Delirium-Concurrent-QA-"+std::to_string(GetCurrentProcessId()));
    assert(calls==5&&singletonRedirected);
    originalShowWindow=&forbiddenShow;originalWindowPos=&forbiddenPos;
    // An invalid/non-owned HWND must never be forwarded to a window-mutating API.
    assert(passiveShowWindow(nullptr,SW_SHOW)==FALSE);
    assert(passiveWindowPos(nullptr,HWND_TOP,0,0,1,1,0)==TRUE);
    assert(passiveForeground(nullptr)==TRUE);
    passiveActive(nullptr);passiveFocus(nullptr);
    return 0;
}
