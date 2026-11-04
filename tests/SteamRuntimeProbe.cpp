#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <xinput.h>
#include "CodePatch.hpp"
#include "Rig.hpp"
#include "World.hpp"
#include <memory>
#include <atomic>

// Opt-in lab observer for the SHA-pinned original Steam executable.
// Chains the central CALL. Separate opt-ins fit, simulate and render a lab body;
// this fixture does not replace native AI, actor-root or damage ownership.
namespace {
std::byte* image{};
FILE* trace{};
ULONGLONG started{},lastSample{};
volatile LONG processCalls{};
using Process=void(__cdecl*)();
Process originalProcess{};
template<class T>T field(const void* object,size_t offset);
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
GetState originalGetState{};
unsigned menuStep{},packet{};
unsigned menuStage{};
ULONGLONG lastPulse{};
WORD pulseButton{};
std::atomic_bool rigEnabled{},worldEnabled{},simulationEnabled{},renderSimulationEnabled{},capturePhysics{};
bool rigTried{},worldTried{},simulationTried{},simulationValid=true;
bool bootstrapCore{};
void* inspectedPed{};
using PreRender=void(__thiscall*)(void*);
PreRender originalPreRender{};
unsigned renderedPhysicalFrames{};
NmAgent* inspectedAgent{};
unsigned simulationSteps{};
double simulationCredit{};
float initialComZ{};
std::unique_ptr<CollisionWorld> inspectedWorld;
std::atomic<NmBuild*> sourceAnatomy{};
std::unique_ptr<Rig> inspectedRig;
void __fastcall renderSimulation(void*ped,void*){
    originalPreRender(ped);
    if(ped!=inspectedPed||!inspectedAgent||!simulationValid||!inspectedRig)return;
    auto*h=hierarchy(ped);if(!h||h->num!=int(inspectedRig->ids.size())||!h->matrices)return;
    std::array<T,21> parts{};
    for(int part=0;part<21;part++)if(nm_agent_get_part_transform(inspectedAgent,part,&parts[part].p.x,&parts[part].q.x)<0)return;
    const auto bones=inspectedRig->physicalBones(parts);
    for(const auto&bone:bones)if(!finite(bone.p))return;
    const auto palette=inspectedRig->renderPalette(h->matrices,bones);
    std::copy(palette.begin(),palette.end(),h->matrices);
    if(++renderedPhysicalFrames%60==0){
        std::fprintf(trace,"native_physical_palette frames=%u nodes=%d step=%u actor_state_changed=0\n",renderedPhysicalFrames,h->num,simulationSteps);
        std::fflush(trace);
    }
}
bool installRenderSimulation(){
    auto**slot=reinterpret_cast<void**>(image+0x4b7eb4)+17;
    originalPreRender=reinterpret_cast<PreRender>(image+0x204f20);
    DWORD protection{};if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection))return false;
    const bool installed=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&renderSimulation),
        reinterpret_cast<void*>(originalPreRender))==reinterpret_cast<void*>(originalPreRender);
    DWORD ignored{};VirtualProtect(slot,sizeof(*slot),protection,&ignored);
    return installed;
}
DWORD WINAPI observePad(DWORD index,XINPUT_STATE* state){
    if(index!=0)return originalGetState(index,state);
    std::memset(state,0,sizeof(*state));state->dwPacketNumber=++packet;
    const auto elapsed=GetTickCount64()-started;
    if(menuStep==1&&elapsed>=20000&&elapsed<20150)state->Gamepad.wButtons=XINPUT_GAMEPAD_A;
    if(menuStep==2&&elapsed>=18000){
        const auto page=field<unsigned char>(image+0x833938,0x159);
        const auto entry=field<int>(image+0x833938,0x54);
        if(elapsed-lastPulse>=700){
            WORD button=0;unsigned next=menuStage;
            if(menuStage==0&&page==34&&entry==0){button=XINPUT_GAMEPAD_A;next=1;}
            else if(menuStage==1&&page==1&&entry==0){button=XINPUT_GAMEPAD_DPAD_DOWN;next=2;}
            else if(menuStage==2&&page==1&&entry==1){button=XINPUT_GAMEPAD_A;next=3;}
            else if(menuStage==3&&page==9&&entry==1){button=XINPUT_GAMEPAD_DPAD_DOWN;next=4;}
            else if(menuStage==4&&page==9&&entry==2){button=XINPUT_GAMEPAD_A;next=5;}
            else if(menuStage==5&&page==11&&entry==1){button=XINPUT_GAMEPAD_DPAD_DOWN;next=6;}
            else if((menuStage==5||menuStage==6)&&page==11&&entry==2){button=XINPUT_GAMEPAD_A;next=7;}
            if(button){
                menuStage=next;lastPulse=elapsed;pulseButton=button;
                std::fprintf(trace,"save_menu_input %llu stage=%u page=%u entry=%d button=%u\n",elapsed,menuStage,page,entry,button);
                std::fflush(trace);
            }
        }
        if(elapsed-lastPulse<100)state->Gamepad.wButtons=pulseButton;
    }
    // Optional camera-only input in this owned game process for the review
    // frame. Never sends input to Windows or moves the native player.
    if(capturePhysics&&simulationSteps==240&&elapsed>=35500&&elapsed<36100)
        state->Gamepad.sThumbRY=-12000;
    return ERROR_SUCCESS;
}
template<class T>T field(const void* object,size_t offset){T value{};std::memcpy(&value,static_cast<const std::byte*>(object)+offset,sizeof value);return value;}
bool readable(const void* pointer,size_t length){
    if(!pointer||length>0x2000000)return false;
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(pointer,&memory,sizeof memory)||memory.State!=MEM_COMMIT||
        (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const auto at=reinterpret_cast<uintptr_t>(pointer),end=at+length;
    return end>=at&&end<=reinterpret_cast<uintptr_t>(memory.BaseAddress)+memory.RegionSize;
}
void reportMenu(const char* phase){
    const auto* menu=image+0x833938;
    std::fprintf(trace,"menu %llu phase=%s calls=%ld page=%u entry=%d active=%u unloaded_raw=%u\n",
        GetTickCount64()-started,phase,processCalls,field<unsigned char>(menu,0x159),
        field<int>(menu,0x54),field<unsigned char>(menu,0x5c),field<unsigned char>(menu,0xe9));
    std::fflush(trace);
}
void advanceSimulation(float nativeDelta){
    if(!inspectedAgent||!simulationValid||simulationSteps>=240)return;
    if(!std::isfinite(nativeDelta)||nativeDelta<=0){simulationValid=false;return;}
    simulationCredit+=double(nativeDelta)/50.;
    constexpr float step=1.f/60.f;
    while(simulationCredit>=double(step)&&simulationSteps<240){
        const int result=nm_agent_step(inspectedAgent,inspectedWorld->world,step);
        ++simulationSteps;simulationCredit-=step;
        float lowest=INFINITY;unsigned contacts=0;
        for(int i=0;i<21;i++){
            V p{};Q q{};
            simulationValid&=nm_agent_get_part_transform(inspectedAgent,i,&p.x,&q.x)==0&&finite(p)&&
                std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w);
            lowest=std::min(lowest,p.z);contacts+=nm_agent_get_part_contact(inspectedAgent,i)>0;
        }
        V com{},velocity{};nm_agent_get_com(inspectedAgent,&com.x,&velocity.x);
        const float energy=nm_agent_get_kinetic_energy(inspectedAgent);
        simulationValid&=result==0&&finite(com)&&finite(velocity)&&std::isfinite(energy);
        if(!simulationValid||simulationSteps%30==0){
            std::fprintf(trace,"native_simulation step=%u finite=%u com_z=%.6f drop=%.6f lowest_centre=%.6f contact_parts=%u energy=%.6f palette_enabled=%u\n",
                simulationSteps,simulationValid,com.z,initialComZ-com.z,lowest,contacts,energy,unsigned(renderSimulationEnabled.load()));
            std::fflush(trace);
        }
        if(!simulationValid)break;
    }
}
void __cdecl observeProcess(){
    originalProcess();
    InterlockedIncrement(&processCalls);
    const float delta=field<float>(image,0x80fd50);
    advanceSimulation(delta);
    const auto now=GetTickCount64();if(now-lastSample<250)return;lastSample=now;
    const auto* pool=field<const std::byte*>(image,0x8018a8);
    unsigned occupied=0,finite=0,players=0;int count=0;
    if(readable(pool,20)){
        const auto* objects=field<const std::byte*>(pool,0);
        const auto* flags=field<const unsigned char*>(pool,4);count=field<int>(pool,8);
        if(count>0&&count<=1000&&readable(flags,count)&&readable(objects,size_t(count)*0x7c4)){
            for(int i=0;i<count;i++)if(!(flags[i]&0x80)){
                ++occupied;const auto* ped=objects+size_t(i)*0x7c4;
                const auto* matrix=field<const std::byte*>(ped,0x14);
                const float health=field<float>(ped,0x540);
                if(readable(matrix,64)&&std::isfinite(health)){
                    const float x=field<float>(matrix,48),y=field<float>(matrix,52),z=field<float>(matrix,56);
                    if(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z))++finite;
                }
                if(field<const void*>(ped,0)==image+0x4b7eb4){
                    ++players;
                    if(worldEnabled&&!worldTried&&processCalls>40&&sourceAnatomy){
                        worldTried=true;
                        try{
                            inspectedWorld=std::make_unique<CollisionWorld>();
                            inspectedWorld->rebuild(position(const_cast<std::byte*>(ped)));
                            const V from=position(const_cast<std::byte*>(ped))+V{0,0,1},down{0,0,-1};
                            float distance=-1;V normal{};
                            const int hit=nm_world_raycast(inspectedWorld->world,&from.x,&down.x,5,&distance,&normal.x);
                            std::fprintf(trace,"native_world entities=%d triangles=%d simple=%d complete=%u area=%d floor_hit=%d distance=%.6f normal=%.5f,%.5f,%.5f\n",
                                inspectedWorld->entities,inspectedWorld->triangles,inspectedWorld->simpleTransforms,
                                inspectedWorld->nativeCoverage.complete,inspectedWorld->currentArea,hit,distance,normal.x,normal.y,normal.z);
                        }catch(const std::exception&error){std::fprintf(trace,"native_world rejected=%s\n",error.what());}
                        std::fflush(trace);
                    }
                    if(rigEnabled&&!rigTried&&processCalls>20&&sourceAnatomy){
                        rigTried=true;
                        try{
                            inspectedRig=std::make_unique<Rig>(const_cast<std::byte*>(ped),sourceAnatomy.load());
                            std::array<T,21> parts{};std::vector<T> bones;
                            const bool incoming=inspectedRig->incoming(const_cast<std::byte*>(ped),parts,bones);
                            std::fprintf(trace,"native_rig fitted=1 nodes=%zu incoming=%u parts=%d\n",inspectedRig->ids.size(),incoming,nm_build_part_count(inspectedRig->build));
                        }catch(const std::exception&error){std::fprintf(trace,"native_rig rejected=%s\n",error.what());}
                    }
                    if(simulationEnabled&&!simulationTried&&inspectedRig&&inspectedWorld&&inspectedWorld->triangles>0){
                        simulationTried=true;
                        std::array<T,21> parts{};std::vector<T> bones;
                        if(inspectedRig->incoming(const_cast<std::byte*>(ped),parts,bones)){
                            const V origin=position(const_cast<std::byte*>(ped)),zero{};
                            // Camera-review fixture only: place the independent
                            // simulated body in front of the native player root,
                            // so a backward fall is visible instead of under the
                            // camera. No shape, pose or floor substitute is used.
                            const V reviewOffset=capturePhysics?::matrix(const_cast<std::byte*>(ped))->up*2.f:zero;
                            for(auto&part:parts)part.p=part.p+reviewOffset;
                            const V velocity=field<V>(ped,0x44)*50.f;
                            inspectedAgent=nm_agent_create(inspectedRig->build,&origin.x,0,.3f);
                            if(inspectedAgent){
                                simulationValid=nm_agent_set_mode(inspectedAgent,NM_MODE_FOLLOW)==0;
                                for(int part=0;part<21;part++)simulationValid&=nm_agent_set_part_state(inspectedAgent,part,&parts[part].p.x,&parts[part].q.x,&velocity.x,&zero.x)==0;
                                simulationValid&=nm_agent_set_mode(inspectedAgent,NM_MODE_LEAD)==0;
                                V com{},v{};nm_agent_get_com(inspectedAgent,&com.x,&v.x);initialComZ=com.z;
                                std::fprintf(trace,"native_simulation_seed parts=21 finite=%u mass=%.6f com_z=%.6f applied=0\n",simulationValid,nm_agent_get_mass(inspectedAgent),com.z);
                                if(capturePhysics)std::fprintf(trace,"native_simulation_fixture_translation x=%.6f y=%.6f z=%.6f\n",reviewOffset.x,reviewOffset.y,reviewOffset.z);
                                if(renderSimulationEnabled){
                                    inspectedPed=const_cast<std::byte*>(ped);
                                    const bool installed=installRenderSimulation();
                                    std::fprintf(trace,"native_physical_palette_hook installed=%u\n",installed);
                                }
                            }
                        }
                    }
                    if(readable(matrix,64))std::fprintf(trace,"player_pose %llu x=%.5f y=%.5f z=%.5f health=%.3f area=%u\n",
                        now-started,field<float>(matrix,48),field<float>(matrix,52),field<float>(matrix,56),health,field<unsigned char>(ped,0x2f));
                }
            }
        }
    }
    std::fprintf(trace,"process %llu calls=%ld timestep=%.6f pool_capacity=%d occupied=%u finite=%u players=%u\n",
        now-started,processCalls,delta,count,occupied,finite,players);
    reportMenu("process");
}
DWORD WINAPI setup(void*){
    const unsigned char expected[]={0xe8,0x26,0xcf,0xff,0xff};
    auto* site=image+0x14ed25;
    if(std::memcmp(site,expected,sizeof expected)){
        std::fprintf(trace,"rejected central CALL bytes\n");std::fflush(trace);return 1;
    }
    originalProcess=reinterpret_cast<Process>(image+0x14bc50);
    const auto offset=int32_t(reinterpret_cast<std::byte*>(&observeProcess)-(site+5));
    bool installed=false;
    {FrozenThreads freeze(std::array<CodeRange,1>{{{uintptr_t(site),uintptr_t(site)+5}}});
        if(freeze.ok()){
            DWORD protection{};
            if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)){
                std::memcpy(site+1,&offset,4);FlushInstructionCache(GetCurrentProcess(),site,5);
                DWORD ignored{};VirtualProtect(site,5,protection,&ignored);installed=true;
            }
        }
    }
    std::fprintf(trace,"central_call_installed=%u\n",installed);std::fflush(trace);
    if(!installed)return 2;
    char enabledRig[4]{};
    rigEnabled=GetEnvironmentVariableA("SA_STEAM_RIG_PROBE",enabledRig,4)==1&&enabledRig[0]=='1';
    worldEnabled=GetEnvironmentVariableA("SA_STEAM_WORLD_PROBE",enabledRig,4)==1&&enabledRig[0]=='1';
    simulationEnabled=GetEnvironmentVariableA("SA_STEAM_SIMULATION_PROBE",enabledRig,4)==1&&enabledRig[0]=='1';
    renderSimulationEnabled=GetEnvironmentVariableA("SA_STEAM_RENDER_SIMULATION",enabledRig,4)==1&&enabledRig[0]=='1';
    capturePhysics=GetEnvironmentVariableA("SA_STEAM_CAPTURE_PHYSICS",enabledRig,4)==1&&enabledRig[0]=='1';
    if(rigEnabled){
        char character[MAX_PATH]{},body[MAX_PATH]{};
        const auto xrt=GetEnvironmentVariableA("SA_STEAM_RIG_XRT",character,MAX_PATH);
        const auto build=GetEnvironmentVariableA("SA_STEAM_RIG_BUILD",body,MAX_PATH);
        if(!xrt||xrt>=MAX_PATH||!build||build>=MAX_PATH)return 4;
        bootstrapCore=GetEnvironmentVariableA("SA_STEAM_CORE_FROM_BOOTSTRAP",enabledRig,4)==1&&enabledRig[0]=='1';
        int initialized=-1;
        if(bootstrapCore){
            using Ready=int(__cdecl*)();
            const auto module=GetModuleHandleW(L"sa_delirium_bootstrap.dll");
            const auto ready=module?reinterpret_cast<Ready>(GetProcAddress(module,"sa_delirium_runtime_ready")):nullptr;
            if(!ready)return 6;
            const auto deadline=GetTickCount64()+30000;
            while(!ready()&&GetTickCount64()<deadline)Sleep(50);
            initialized=ready()?0:-1;
            std::fprintf(trace,"shared_bootstrap_core ready=%u independent_program_env=%u\n",initialized==0,
                GetEnvironmentVariableA("SA_DELIRIUM_PROGRAM",nullptr,0)>0);
        }else initialized=nm_init(character);
        if(initialized>=0)sourceAnatomy=nm_build_load(body);
        std::fprintf(trace,"rig_sdk initialized=%d source_parts=%d\n",initialized,sourceAnatomy?nm_build_part_count(sourceAnatomy.load()):-1);
        std::fflush(trace);
        if(!sourceAnatomy)return 5;
    }
    char setting[4]{};
    if(GetEnvironmentVariableA("SA_STEAM_NATIVE_MENU_STEP",setting,4)==1&&(setting[0]=='1'||setting[0]=='2')){
        menuStep=unsigned(setting[0]-'0');
        if(menuStep==2){char slot[4]{};if(GetEnvironmentVariableA("GTASA_TEST_SAVE_SLOT",slot,4)!=1||slot[0]!='2')return 3;}
        const auto xinput=GetModuleHandleW(L"xinput1_3.dll");
        originalGetState=xinput?reinterpret_cast<GetState>(GetProcAddress(xinput,"XInputGetState")):nullptr;
        auto** entry=reinterpret_cast<void**>(image+0x4a036c);DWORD protection{};bool padInstalled=false;
        if(originalGetState&&VirtualProtect(entry,sizeof(*entry),PAGE_READWRITE,&protection)){
            padInstalled=InterlockedCompareExchangePointer(entry,reinterpret_cast<void*>(&observePad),
                reinterpret_cast<void*>(originalGetState))==reinterpret_cast<void*>(originalGetState);
            DWORD ignored{};VirtualProtect(entry,sizeof(*entry),protection,&ignored);
        }
        std::fprintf(trace,"native_menu_pad_installed=%u\n",padInstalled);std::fflush(trace);
    }
    bool before=false,after=false;
    for(unsigned i=0;i<240;i++){
        const auto elapsed=GetTickCount64()-started;
        if((!before&&elapsed>=18500)||(!after&&elapsed>=24000)){
            const bool previous=elapsed<20000;
            std::fprintf(trace,"menu_bytes_%s ",previous?"before":"after");
            for(unsigned n=0;n<0x200;n++)std::fprintf(trace,"%02x",field<unsigned char>(image+0x833938,n));
            std::fprintf(trace,"\n");std::fflush(trace);
            if(previous)before=true;else after=true;
        }
        reportMenu("observer");Sleep(250);
    }
    return 0;
}
}
extern "C" __declspec(dllexport) void InitializeASI(){
    char enabled[4]{};
    if(trace)return;
    const auto setting=GetEnvironmentVariableA("SA_STEAM_RUNTIME_PROBE",enabled,4);
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    wchar_t* end=wcsrchr(executable,L'\\');if(!end)return;
    wcscpy_s(end+1,MAX_PATH-size_t(end+1-executable),L"steam-runtime-probe.log");
    if(_wfopen_s(&trace,executable,L"w"))return;
    image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+dos->e_lfanew);
    std::fprintf(trace,"probe_entry pid=%lu setting=%lu value=%d image=%08x\n",GetCurrentProcessId(),setting,int(enabled[0]),nt->OptionalHeader.SizeOfImage);
    std::fflush(trace);
    if(setting!=1||enabled[0]!='1')return;
    if(nt->OptionalHeader.SizeOfImage!=0x985000)return;
    started=GetTickCount64();
    HANDLE thread=CreateThread(nullptr,0,setup,nullptr,0,nullptr);if(thread)CloseHandle(thread);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);InitializeASI();}
    return TRUE;
}
