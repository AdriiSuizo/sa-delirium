#pragma once
// Live code patching from the adapter's setup thread.
//
// setup() runs on its own thread (InitializeASI -> CreateThread) while the
// game's main thread is already executing WinMain/Idle. The previous manual
// patchers assumed "before gameplay starts" and wrote 5-7 instruction bytes
// with memcpy while the main thread could fetch them: a torn CALL/JMP or a
// thread parked mid-way through a replaced instruction sequence crashes.
//
// FrozenThreads suspends every other thread of the process (as MinHook does
// for its own hooks), waits until each suspension is effective
// (GetThreadContext), and retries while any thread's EIP lies strictly inside
// a range being rewritten. Nothing allocates while threads are frozen: a
// suspended thread may hold the process heap lock. Callers must therefore do
// only raw byte copies and FlushInstructionCache inside the frozen scope and
// log/report after it ends.
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdint>

struct CodeRange {uintptr_t begin,end;};

class FrozenThreads {
public:
    static constexpr size_t MaxThreads=512;
    template<size_t N>
    explicit FrozenThreads(const std::array<CodeRange,N>&ranges){freeze(ranges.data(),N);}
    FrozenThreads(const CodeRange*ranges,size_t count){freeze(ranges,count);}
    ~FrozenThreads(){release();}
    FrozenThreads(const FrozenThreads&)=delete;FrozenThreads&operator=(const FrozenThreads&)=delete;
    // false: a thread stayed inside a rewritten range, a thread could not be
    // suspended, or the thread table overflowed. Do NOT write in that case.
    bool ok() const {return ok_;}
    unsigned attempts() const {return attempts_;}
private:
    HANDLE handles_[MaxThreads]{};size_t count_=0;bool ok_=false;unsigned attempts_=0;
    void release(){for(size_t i=0;i<count_;i++){ResumeThread(handles_[i]);CloseHandle(handles_[i]);}count_=0;}
    bool suspendAll(){
        const DWORD self=GetCurrentThreadId(),pid=GetCurrentProcessId();
        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 e{};e.dwSize=sizeof(e);bool complete=true;
        for(BOOL more=Thread32First(snap,&e);more;more=Thread32Next(snap,&e)){
            if(e.th32OwnerProcessID!=pid||e.th32ThreadID==self)continue;
            if(count_>=MaxThreads){complete=false;break;}
            HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
            if(!h)continue; // thread already exited
            if(SuspendThread(h)==DWORD(-1)){CloseHandle(h);continue;}
            handles_[count_++]=h;
        }
        CloseHandle(snap);return complete;
    }
    bool anyInside(const CodeRange*ranges,size_t n){
        for(size_t i=0;i<count_;i++){
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(handles_[i],&c))return true; // suspension not confirmed: treat as unsafe
            for(size_t r=0;r<n;r++)if(c.Eip>ranges[r].begin&&c.Eip<ranges[r].end)return true;
        }
        return false;
    }
    void freeze(const CodeRange*ranges,size_t n){
        for(attempts_=1;attempts_<=64;attempts_++){
            if(!suspendAll()){release();return;}
            if(!anyInside(ranges,n)){ok_=true;return;}
            release();Sleep(1);
        }
    }
};
