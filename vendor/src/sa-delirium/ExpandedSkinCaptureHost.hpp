#pragma once
// Test-only observer, no render replacement. Call from the completed physics /
// palette barrier. Both gates must be present in the owned process environment.
#include "ExpandedSkinCapture.hpp"
#include <map>
#include <set>
#include <cctype>

static bool expandedSkinCaptureEnabled(){
    char value[8]{};return GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&
        GetEnvironmentVariableA("GTASA_CAPTURE_EXPANDED_SKIN",value,sizeof value)==1&&value[0]=='1';
}
static bool expandedSkinCaptureAbi(){
    struct Signature {uintptr_t address;std::initializer_list<unsigned char>bytes;};
    static const Signature expected[]={
        {0x7C7550,{0x8b,0x44,0x24,0x04,0x8b,0x0d,0xa8,0x78,0xc9,0x00,0x8b,0x04,0x01,0xc3}},
        {0x7C77E0,{0x8b,0x44,0x24,0x04,0x8b,0x00,0xc3}},
        {0x7C77F0,{0x8b,0x44,0x24,0x04,0x8b,0x40,0x18,0xc3}},
        {0x7C7800,{0x8b,0x44,0x24,0x04,0x8b,0x40,0x14,0xc3}},
        {0x7C7810,{0x8b,0x44,0x24,0x04,0x8b,0x40,0x0c,0xc3}},
        {0x7C7540,{0x8b,0x44,0x24,0x04,0x8b,0x0d,0xa4,0x78,0xc9,0x00,0x8b,0x04,0x01,0xc3}},
        {0x812140,{0x8b,0x44,0x24,0x04,0x8b,0x0d,0x74,0xab,0xc9,0x00,0x8b,0x04,0x01,0x85,0xc0,0x75,0x01,0xc3,0x8b,0x40,0x30,0xc3}},
        {0x7C85FA,{0xa1,0xe0,0x78,0xc9,0x00,0x85,0xc0}},
        {0x7CB256,{0x39,0x0d,0xe4,0x78,0xc9,0x00}},
        {0x749B70,{0x8b,0x44,0x24,0x04,0x53,0x55,0x56,0x57,0x8d,0x78,0x08}},
        {0x7491CC,{0xa1,0x24,0x7b,0xc9,0x00,0x8b,0x0d,0x60,0xbc,0xc9,0x00,0x8b,0x44,0x01,0x3c}}
    };
    for(const auto&s:expected){unsigned char actual[32]{};
        if(!expandedSkinCapture::readSelf(actual,reinterpret_cast<void*>(s.address),s.bytes.size())||std::memcmp(actual,s.bytes.begin(),s.bytes.size())){
            event("expanded_skin_capture_refused",-1,-1,"native_abi_modified address="+std::to_string(s.address)+" no_mutation=1");return false;
        }
    }
    return true;
}
static void*expandedSkinRuntimeDefaultPipeline(){
    // Exact original 7491CC..7491DA: *(engine + pluginGlobalsOffset + 0x3c).
    // 0xC9BC60 stores an OFFSET, not another engine pointer.
    uintptr_t engine=0;int32_t globalsOffset=0;void*pipeline=nullptr;
    if(!expandedSkinCapture::readSelf(&engine,reinterpret_cast<void*>(0xC97B24),4)||
       !expandedSkinCapture::readSelf(&globalsOffset,reinterpret_cast<void*>(0xC9BC60),4)||!engine)return nullptr;
    const int64_t slot=int64_t(engine)+globalsOffset+0x3c;
    if(slot<=0||slot>0xfffffffb||!expandedSkinCapture::readSelf(&pipeline,reinterpret_cast<void*>(uintptr_t(slot)),4))return nullptr;
    return pipeline;
}
struct ExpandedSkinAtomicList {std::vector<expandedSkinHost::Atomic*>atoms;bool overflow=false;};
static void*__cdecl collectExpandedSkinAtomics(void*atomic,void*data){
    auto&list=*static_cast<ExpandedSkinAtomicList*>(data);
    if(list.atoms.size()==32){list.overflow=true;return nullptr;}list.atoms.push_back(static_cast<expandedSkinHost::Atomic*>(atomic));return atomic;
}
struct ExpandedSkinCaptureIdentity {int ref=-1,model=-1;void*ped=nullptr,*clump=nullptr;uint64_t generation=0;};
static std::map<std::string,ExpandedSkinCaptureIdentity> expandedSkinCaptureIdentities;
static std::set<std::string> expandedSkinCaptureCompleted;
static uint64_t expandedSkinCaptureGeneration=0;
static void captureExpandedSkinRuntime(void*ped,const char*rawLabel){
    if(!expandedSkinCaptureEnabled()||!ready||!ped||!rawLabel)return;
    std::string label;for(unsigned i=0;rawLabel[i]&&i<32;i++){const unsigned char c=rawLabel[i];label+=(std::isalnum(c)||c=='-'||c=='_')?char(c):'_';}
    if(label.empty()||expandedSkinCaptureCompleted.count(label)||expandedSkinCaptureCompleted.size()>=8)return;
    const int ref=reference(ped);const auto*evaluation=currentNativePaletteEvaluation(ped);
    if(ref<0||pedFromRef(ref)!=ped||!evaluation||!evaluation->serial||!at<void*>(ped,0x18))return;
    static const bool abiOkay=expandedSkinCaptureAbi();if(!abiOkay){expandedSkinCaptureCompleted.insert(label);return;}
    auto&id=expandedSkinCaptureIdentities[label];
    if(id.ref!=ref||id.model!=at<short>(ped,0x22)||id.ped!=ped||id.clump!=at<void*>(ped,0x18))
        id={ref,at<short>(ped,0x22),ped,at<void*>(ped,0x18),++expandedSkinCaptureGeneration};
    ExpandedSkinAtomicList atoms;reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(id.clump,&collectExpandedSkinAtomics,&atoms);
    expandedSkinCaptureCompleted.insert(label); // One bounded diagnostic attempt, not per-frame IO.
    if(atoms.overflow){event("expanded_skin_capture_refused",ref,id.model,"atomic_count_exceeds32 label="+label);return;}
    const auto api=expandedSkinCapture::pinnedApi();const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    const uint64_t serial=evaluation->serial;void*defaultPipeline=expandedSkinRuntimeDefaultPipeline();unsigned captured=0;
    LARGE_INTEGER begin{},end{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);
    double captureMilliseconds=0,writeMilliseconds=0;
    for(size_t i=0;i<atoms.atoms.size();i++){
        expandedSkinCapture::Token token{ref,id.model,frame,serial,id.generation,id.clump,atoms.atoms[i]};expandedSkinCapture::Snapshot snapshot;
        LARGE_INTEGER captureBegin{},captureEnd{},writeEnd{};QueryPerformanceCounter(&captureBegin);
        if(!expandedSkinCapture::capture(api,token,frame,defaultPipeline,snapshot)){
            event("expanded_skin_capture_atomic_refused",ref,id.model,"label="+label+" atomic="+std::to_string(i)+" reason="+snapshot.failure);continue;
        }
        snapshot.normaliseGlobalReadable=api.read(&snapshot.normaliseGlobal,reinterpret_cast<void*>(0xC978E0),4);
        snapshot.hardwareGlobalReadable=api.read(&snapshot.hardwareGlobal,reinterpret_cast<void*>(0xC978E4),4);
        QueryPerformanceCounter(&captureEnd);
        const auto*current=currentNativePaletteEvaluation(ped);
        if(pedFromRef(ref)!=ped||at<void*>(ped,0x18)!=id.clump||at<short>(ped,0x22)!=id.model||!current||current->serial!=serial||current->frame!=frame){
            event("expanded_skin_capture_refused",ref,id.model,"actor_epoch_changed_during_capture label="+label);break;
        }
        const auto path=folder/("expanded-skin-"+std::to_string(GetCurrentProcessId())+"-"+label+"-"+std::to_string(frame)+"-"+std::to_string(i)+".json");
        const bool written=expandedSkinCapture::write(snapshot,path.wstring());if(written)captured++;
        QueryPerformanceCounter(&writeEnd);
        const double captureMs=frequency.QuadPart?1000.*(captureEnd.QuadPart-captureBegin.QuadPart)/frequency.QuadPart:0;
        const double writeMs=frequency.QuadPart?1000.*(writeEnd.QuadPart-captureEnd.QuadPart)/frequency.QuadPart:0;
        captureMilliseconds+=captureMs;writeMilliseconds+=writeMs;
        event("expanded_skin_capture",ref,id.model,"label="+label+" atomic="+std::to_string(i)+" frame="+std::to_string(frame)+" serial="+std::to_string(serial)+" generation="+std::to_string(id.generation)+" vertices="+std::to_string(snapshot.source.size())+" bones="+std::to_string(snapshot.nodes.size())+" ltm_current="+std::to_string(snapshot.frameWorldVerified)+" derived_ltm="+std::to_string(snapshot.derivedWorldVerified)+" frame_chain="+std::to_string(snapshot.frameChain.size())+" output_ok="+std::to_string(written)+" capture_ms="+std::to_string(captureMs)+" serialize_write_ms="+std::to_string(writeMs)+" file="+path.filename().string()+" render_enabled=0");
    }
    QueryPerformanceCounter(&end);const double elapsed=frequency.QuadPart?1000.*(end.QuadPart-begin.QuadPart)/frequency.QuadPart:0;
    event("expanded_skin_capture_complete",ref,id.model,"label="+label+" atomics="+std::to_string(atoms.atoms.size())+" captured="+std::to_string(captured)+" total_ms="+std::to_string(elapsed)+" capture_ms="+std::to_string(captureMilliseconds)+" serialize_write_ms="+std::to_string(writeMilliseconds)+" one_shot=1 render_enabled=0");
}
