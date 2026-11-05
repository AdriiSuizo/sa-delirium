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
#include <array>
#include <utility>

// Opt-in lab observer for the SHA-pinned original Steam executable.
// Chains the central CALL. Separate opt-ins fit, simulate and render a lab body;
// this fixture does not replace native AI, actor-root or damage ownership.
namespace {
std::byte* image{};
FILE* trace{};
HANDLE crashTrace=INVALID_HANDLE_VALUE;
LONG CALLBACK observeCrash(EXCEPTION_POINTERS* exception){
    if(!exception||crashTrace==INVALID_HANDLE_VALUE)return EXCEPTION_CONTINUE_SEARCH;
    const auto code=exception->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_STACK_OVERFLOW)return EXCEPTION_CONTINUE_SEARCH;
    const auto*c=exception->ContextRecord;char line[512];DWORD written{};
    const int n=sprintf_s(line,"exception code=%08lx ip=%08lx rva=%08lx eax=%08lx ecx=%08lx edx=%08lx esp=%08lx ebp=%08lx damage_count=%u simple_count=%u time_count=%u esi=%08lx address=%08lx\r\n",
        code,c->Eip,c->Eip-DWORD(reinterpret_cast<uintptr_t>(image)),c->Eax,c->Ecx,c->Edx,c->Esp,c->Ebp,
        *reinterpret_cast<const unsigned*>(image+0x7936d0),*reinterpret_cast<const unsigned*>(image+0x7260c8),*reinterpret_cast<const unsigned*>(image+0x7940d8),c->Esi,DWORD(exception->ExceptionRecord->ExceptionInformation[1]));
    if(n>0)WriteFile(crashTrace,line,DWORD(n),&written,nullptr);
    for(const auto&item:std::array{std::pair{"col",std::size_t(0x8018bc)},std::pair{"building",std::size_t(0x8018b0)}}){
    DWORD pool{},layout[4]{};SIZE_T got{};
    if(ReadProcessMemory(GetCurrentProcess(),image+item.second,&pool,4,&got)&&got==4&&pool&&
       ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(pool),layout,sizeof(layout),&got)&&got==sizeof(layout)){
        unsigned used=0;bool complete=layout[2]<=100000;unsigned char flags[1024];
        for(unsigned i=0;complete&&i<layout[2];){
            const unsigned count=(layout[2]-i<sizeof(flags))?layout[2]-i:sizeof(flags);
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(layout[1]+i),flags,count,&got)||got!=count){complete=false;break;}
            for(unsigned j=0;j<count;j++)used+=(flags[j]&0x80)==0;i+=count;
        }
        const int size=sprintf_s(line,"native_%s_pool capacity=%lu occupied=%u complete=%u first_free=%ld\r\n",item.first,layout[2],used,unsigned(complete),LONG(layout[3]));
        if(size>0)WriteFile(crashTrace,line,DWORD(size),&written,nullptr);
    }
    }
    DWORD frame=c->Ebp;
    for(unsigned i=0;i<12&&frame;i++){
        DWORD values[6]{};SIZE_T got{};
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(frame),values,sizeof values,&got)||got!=sizeof values)break;
        const int size=sprintf_s(line,"frame index=%u ebp=%08lx return=%08lx rva=%08lx args=%08lx,%08lx,%08lx,%08lx\r\n",i,frame,values[1],values[1]-DWORD(reinterpret_cast<uintptr_t>(image)),values[2],values[3],values[4],values[5]);
        if(size>0)WriteFile(crashTrace,line,DWORD(size),&written,nullptr);
        if(values[0]<=frame||values[0]-frame>0x10000)break;frame=values[0];
    }
    FlushFileBuffers(crashTrace);return EXCEPTION_CONTINUE_SEARCH;
}
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
std::atomic_bool gameplayProbe{};
std::atomic_bool meleeProbe{};
std::atomic_bool roadProbe{};
unsigned roadStage{};
ULONGLONG roadClock{},lastRoadSample{};
void* roadCar{};
void* roadPed{};
int roadPedRef=-1,roadCarRef=-1;
void* roadBike{};
int roadBikeRef=-1,roadBikeModel{};
float roadSpeed=15.f;
unsigned gameplayStage{};
ULONGLONG gameplayClock{};
void* gameplayPed{};
int gameplayRef{-1};
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
void observeRoad(){
    if(!roadProbe||processCalls<150)return;
    auto*player=field<std::byte*>(image,0x8100d0);
    if(!readable(player,0x7c4))return;
    const auto now=GetTickCount64();
    if(!roadStage){
        if(field<unsigned char>(player,0x2f)!=3)return;
        // Verified owner-native entry points, main-thread scene setup only.
        // CPed::Teleport takes CVector BY VALUE, plus the unused reset bool
        // (native RET16). No saved checkpoint or script is modified.
        const V scene{2430.f,-1658.f,20.f};
        *reinterpret_cast<int*>(image+0x7ffbe4)=0;
        player[0x2f]=std::byte{0};
        reinterpret_cast<void(__cdecl*)(const V&)>(image+0xff50)(scene);
        bool found=false;void*entity=nullptr;
        const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(image+0x182a50)
            (scene.x,scene.y,scene.z,&found,&entity);
        if(!found||!std::isfinite(ground)){std::fprintf(trace,"native_road setup_failed=ground\n");roadProbe=false;return;}
        reinterpret_cast<void(__thiscall*)(void*,V,bool)>(image+0x1fff30)(player,V{scene.x,scene.y,ground+1.f},false);
        roadClock=now;roadStage=1;
        std::fprintf(trace,"native_road scene_loaded=1 ground=%.5f save_written=0\n",ground);std::fflush(trace);return;
    }
    if(roadStage==1&&now-roadClock>=3000){
        const auto*pm=field<const M*>(player,0x14);if(!readable(pm,sizeof(M)))return;
        using Request=void(__cdecl*)(int,int);using Load=void(__cdecl*)(bool);
        reinterpret_cast<Request>(image+0x8b00)(7,2);reinterpret_cast<Request>(image+0x8b00)(400,2);
        char lamp[4]{};const bool lampProbe=GetEnvironmentVariableA("SA_STEAM_LAMP_PROBE",lamp,4)==1&&lamp[0]=='1';
        if(lampProbe)reinterpret_cast<Request>(image+0x8b00)(1297,2);
        char worldSetting[4]{};
        const bool worldVisualProbe=GetEnvironmentVariableA("SA_STEAM_VISUAL_WORLD_PROBE",worldSetting,4)==1&&worldSetting[0]=='1';
        const std::array<int,5> worldModels{12004,19000,7393,3222,2634};
        if(worldVisualProbe)for(const int id:worldModels)reinterpret_cast<Request>(image+0x8b00)(id,2);
        if(roadBikeModel){reinterpret_cast<Request>(image+0x8b00)(roadBikeModel,2);reinterpret_cast<Request>(image+0x8b00)(284,2);}
        reinterpret_cast<Load>(image+0xfc00)(false);
        if(lampProbe){
            const auto*model=field<const std::byte*>(image,0x712818+1297*4);
            if(readable(model,0x20)){
                const int txd=field<short>(model,0xa);const auto*stream=image+0x55c8a0+1297*0x14;
                const auto loaded=(txd>=0&&txd<5000)?field<unsigned char>(image,0x55c8a0+(20000+txd)*0x14+16):0;
                const auto txdSector=(txd>=0&&txd<5000)?field<unsigned>(image,0x55c8a0+(20000+txd)*0x14+8):0;
                const auto txdSectors=(txd>=0&&txd<5000)?field<unsigned>(image,0x55c8a0+(20000+txd)*0x14+12):0;
                std::fprintf(trace,"native_visual_lamp model=1297 txd_slot=%d txd_loaded=%u model_loaded=%u sectors=%u rw_object=%u txd_sector=%u txd_sectors=%u\n",
                    txd,loaded,field<unsigned char>(stream,16),field<unsigned>(stream,12),field<void*>(model,0x1c)!=nullptr,txdSector,txdSectors);
            }
        }
        if(worldVisualProbe)for(const int id:worldModels){
            const auto*model=field<const std::byte*>(image,0x712818+id*4);
            if(!readable(model,0x20)){std::fprintf(trace,"native_visual_world model=%d missing=1\n",id);continue;}
            const int txd=field<short>(model,0xa);const auto*stream=image+0x55c8a0+id*0x14;
            const auto loaded=(txd>=0&&txd<5000)?field<unsigned char>(image,0x55c8a0+(20000+txd)*0x14+16):0;
            const auto sector=(txd>=0&&txd<5000)?field<unsigned>(image,0x55c8a0+(20000+txd)*0x14+8):0;
            std::fprintf(trace,"native_visual_world model=%d txd_slot=%d txd_loaded=%u model_loaded=%u rw_object=%u col_model=%u txd_sector=%u\n",
                id,txd,loaded,field<unsigned char>(stream,16),field<void*>(model,0x1c)!=nullptr,field<void*>(model,0x14)!=nullptr,sector);
        }
        const V pedestrian=pm->pos+V{1.7f,6.f,0};
        if(roadBikeModel){
            roadBike=reinterpret_cast<void*(__cdecl*)(int,V,bool)>(image+0x34170)(roadBikeModel,pedestrian+V{0,0,-1.f},false);
            if(!readable(roadBike,0xa18)){std::fprintf(trace,"native_road setup_failed=bike\n");roadProbe=false;return;}
            roadPed=reinterpret_cast<void*(__cdecl*)(void*,bool,int,int,bool,bool)>(image+0x231570)(roadBike,true,0,-1,true,false);
        }else roadPed=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(image+0x2301e0)(4,7,pedestrian,true);
        const float distance=roadSpeed<=4.f?4.f:8.f;
        const V origin=pedestrian+V{0,-distance,-1.f};
        roadCar=reinterpret_cast<void*(__cdecl*)(int,V,bool)>(image+0x34170)(400,origin,false);
        if(!readable(roadPed,0x7c4)||!readable(roadCar,0xa18)){std::fprintf(trace,"native_road setup_failed=spawn\n");roadProbe=false;return;}
        const auto reference=[](void*object,std::size_t poolRva,std::ptrdiff_t stride){
            const auto*pool=field<const std::byte*>(image,poolRva);if(!readable(pool,12))return -1;
            const auto*objects=field<const std::byte*>(pool,0);const auto*flags=field<const unsigned char*>(pool,4);
            const auto delta=static_cast<std::byte*>(object)-objects;
            if(delta<0||delta%stride||delta/stride>=field<int>(pool,8))return -1;
            return int(delta/stride)*256+flags[delta/stride];
        };
        roadPedRef=reference(roadPed,0x8018a8,0x7c4);roadCarRef=reference(roadCar,0x8018ac,0xa18);
        if(roadBike)roadBikeRef=reference(roadBike,0x8018ac,0xa18);
        if(roadPedRef<0||roadCarRef<0){std::fprintf(trace,"native_road setup_failed=generation\n");roadProbe=false;return;}
        // ONE initial native velocity. No repeated position/velocity prescription,
        // impulse on the ped, injected damage, knock-off event or ragdoll seed.
        *reinterpret_cast<V*>(static_cast<std::byte*>(roadCar)+0x44)=V{0,roadSpeed/50.f,0};
        roadClock=now;roadStage=2;
        std::fprintf(trace,"native_road spawned=1 car_model=400 ped_model=%d bike_model=%d bike_ref=%d initial_speed=%.3f ped_ref=%d car_ref=%d single_velocity_seed=1 scripted_ped_force=0\n",
            field<short>(roadPed,0x22),roadBikeModel,roadBikeRef,roadSpeed,roadPedRef,roadCarRef);
    }
    if(roadStage<2||now-lastRoadSample<50)return;lastRoadSample=now;
    for(const auto&item:std::array<std::pair<std::size_t,int>,3>{{{0x8018a8,roadPedRef},{0x8018ac,roadCarRef},{0x8018ac,roadBikeRef}}}){
        if(item.second<0)continue;
        const auto*pool=field<const std::byte*>(image,item.first);const auto*flags=field<const unsigned char*>(pool,4);
        if(!readable(flags+(unsigned(item.second)>>8),1)||flags[unsigned(item.second)>>8]!=(item.second&255)){
            std::fprintf(trace,"native_road generation_lost=1 ref=%d\n",item.second);roadProbe=false;return;
        }
    }
    const auto*pm=field<const M*>(roadPed,0x14);const auto*cm=field<const M*>(roadCar,0x14);
    if(!readable(pm,sizeof(M))||!readable(cm,sizeof(M)))return;
    const auto pv=field<V>(roadPed,0x44)*50.f,cv=field<V>(roadCar,0x44)*50.f;
    std::fprintf(trace,"native_road sample elapsed=%llu px=%.5f py=%.5f pz=%.5f pvx=%.5f pvy=%.5f pvz=%.5f cx=%.5f cy=%.5f cz=%.5f cvx=%.5f cvy=%.5f cvz=%.5f health=%.3f in_vehicle=%u finite=%u\n",
        now-roadClock,pm->pos.x,pm->pos.y,pm->pos.z,pv.x,pv.y,pv.z,cm->pos.x,cm->pos.y,cm->pos.z,cv.x,cv.y,cv.z,
        field<float>(roadPed,0x540),(field<unsigned>(roadPed,0x46c)>>8)&1,finite(pm->pos)&&finite(cm->pos)&&finite(pv)&&finite(cv));std::fflush(trace);
}
void observeGameplay(){
    if((!gameplayProbe&&!meleeProbe)||processCalls<150)return;
    auto*player=field<std::byte*>(image,0x8100d0);
    if(!readable(player,0x7c4)||field<unsigned char>(player,0x2f)!=3)return;
    auto*playerMatrix=field<M*>(player,0x14);if(!readable(playerMatrix,sizeof(M)))return;
    const auto now=GetTickCount64();
    if(!gameplayStage){
        using Request=void(__cdecl*)(int,int);using Load=void(__cdecl*)(bool);
        using Spawn=void*(__cdecl*)(int,unsigned,const V&,bool);
        reinterpret_cast<Request>(image+0x8b00)(7,2);
        reinterpret_cast<Load>(image+0xfc00)(false);
        const auto* stream=image+0x55c8a0+7*0x14;
        std::fprintf(trace,"native_visual_stream model=7 sector=%u sectors=%u img=%u loaded=%u\n",
            field<unsigned>(stream,8),field<unsigned>(stream,12),field<unsigned char>(stream,7),field<unsigned char>(stream,16));
        const V position=playerMatrix->pos+V{1.7f,0,0};
        gameplayPed=reinterpret_cast<Spawn>(image+0x2301e0)(4,7,position,true);
        if(!readable(gameplayPed,0x7c4)){std::fprintf(trace,"native_gameplay spawn_failed=1\n");gameplayProbe=false;return;}
        // An ordinary native ped in this actual interior. No save is written.
        static_cast<std::byte*>(gameplayPed)[0x2f]=std::byte{3};
        const auto*pool=field<const std::byte*>(image,0x8018a8);
        const auto*objects=field<const std::byte*>(pool,0);const auto*flags=field<const unsigned char*>(pool,4);
        const auto offset=static_cast<std::byte*>(gameplayPed)-objects;
        if(offset<0||offset%0x7c4||offset/0x7c4>=field<int>(pool,8)){gameplayProbe=false;return;}
        gameplayRef=int(offset/0x7c4)*256+flags[offset/0x7c4];
        gameplayClock=now;gameplayStage=1;
        std::fprintf(trace,"native_gameplay spawned=1 ref=%d model=7 area=3 scripted_pose=0\n",gameplayRef);
        if(meleeProbe){
            // Native aggression task only: subsequent pursuit, attacks, contact,
            // reactions and damage belong to the production combat integration.
            const auto intelligence=field<std::byte*>(gameplayPed,0x47c);
            void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(image+0x239c00)(0x38);
            if(!task||!readable(intelligence,0x30)){std::fprintf(trace,"native_melee setup_failed=task\n");meleeProbe=false;return;}
            reinterpret_cast<void*(__thiscall*)(void*,void*,int,int,int,int,unsigned char)>(image+0x240c70)
                (task,player,6000,0,0,0,1);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(image+0x2add10)(intelligence+4,task,3,false);
            gameplayStage=10;
            std::fprintf(trace,"native_melee task_assigned=1 ref=%d target_player=1 time_ms=6000 scripted_damage=0 scripted_contact=0\n",gameplayRef);
        }
    }
    const auto*pool=field<const std::byte*>(image,0x8018a8);
    const auto*flags=field<const unsigned char*>(pool,4);
    if(flags[unsigned(gameplayRef)>>8]!=unsigned char(gameplayRef&255)){
        std::fprintf(trace,"native_gameplay generation_lost=1 ref=%d\n",gameplayRef);gameplayProbe=false;return;
    }
    const auto elapsed=now-gameplayClock;
    using Damage=bool(__cdecl*)(void*,void*,int,int,int,unsigned char);
    const auto damage=reinterpret_cast<Damage>(image+0x36d6f0);
    if(gameplayStage==1&&elapsed>=2000){
        const auto delivered=damage(gameplayPed,player,22,12,3,0);gameplayStage=2;
        std::fprintf(trace,"native_gameplay shot=nonlethal delivered=%u elapsed=%llu\n",delivered,elapsed);
    }else if(gameplayStage==2&&elapsed>=12000){
        const auto delivered=damage(gameplayPed,player,22,500,3,0);gameplayStage=3;
        std::fprintf(trace,"native_gameplay shot=lethal delivered=%u elapsed=%llu\n",delivered,elapsed);
    }else if(gameplayStage==3&&elapsed>=16000){
        const auto delivered=damage(gameplayPed,player,22,12,3,0);gameplayStage=4;
        std::fprintf(trace,"native_gameplay shot=corpse delivered=%u elapsed=%llu\n",delivered,elapsed);
    }
    const auto*matrix=field<const M*>(gameplayPed,0x14);
    if(readable(matrix,sizeof(M)))std::fprintf(trace,"native_gameplay sample elapsed=%llu stage=%u ref=%d health=%.3f state=%d x=%.5f y=%.5f z=%.5f finite=%u\n",
        elapsed,gameplayStage,gameplayRef,field<float>(gameplayPed,0x540),field<int>(gameplayPed,0x530),matrix->pos.x,matrix->pos.y,matrix->pos.z,finite(matrix->pos));
    std::fflush(trace);
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
    observeRoad();
    const auto now=GetTickCount64();if(now-lastSample<250)return;lastSample=now;
    observeGameplay();
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
    std::fprintf(trace,"process %llu calls=%ld timestep=%.6f pool_capacity=%d occupied=%u finite=%u players=%u streaming_budget=%u streaming_used=%u native_radiosity=%u\n",
        now-started,processCalls,delta,count,occupied,finite,players,field<unsigned>(image,0x514074),field<unsigned>(image,0x55c898),field<unsigned char>(image,0x8db1f6));
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
    gameplayProbe=GetEnvironmentVariableA("SA_STEAM_GAMEPLAY_PROBE",enabledRig,4)==1&&enabledRig[0]=='1';
    meleeProbe=GetEnvironmentVariableA("SA_STEAM_MELEE_PROBE",enabledRig,4)==1&&enabledRig[0]=='1';
    if(meleeProbe&&(gameplayProbe||simulationEnabled||renderSimulationEnabled))return 13;
    roadProbe=GetEnvironmentVariableA("SA_STEAM_ROAD_PROBE",enabledRig,4)==1&&enabledRig[0]=='1';
    if(roadProbe){
        if(gameplayProbe||meleeProbe||simulationEnabled||renderSimulationEnabled)return 10;
        char speed[16]{};GetEnvironmentVariableA("SA_STEAM_ROAD_SPEED",speed,16);
        if(!std::strcmp(speed,"4"))roadSpeed=4.f;else if(!std::strcmp(speed,"8"))roadSpeed=8.f;
        else if(!std::strcmp(speed,"25"))roadSpeed=25.f;else if(speed[0]&&std::strcmp(speed,"15"))return 11;
        char kind[16]{};GetEnvironmentVariableA("SA_STEAM_ROAD_KIND",kind,16);
        if(!std::strcmp(kind,"bike"))roadBikeModel=523;else if(!std::strcmp(kind,"bmx"))roadBikeModel=481;
        else if(kind[0]&&std::strcmp(kind,"ped"))return 12;
    }
    if(gameplayProbe&&(simulationEnabled||renderSimulationEnabled))return 9; // No overlapping fixture authority.
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
        char combatPath[MAX_PATH]{};
        const auto combatLength=GetEnvironmentVariableA("SA_STEAM_COMBAT_ROOT",combatPath,MAX_PATH);
        if(combatLength){
            if(combatLength>=MAX_PATH)return 7;
            auto* combat=nm_combat_load(combatPath);
            const int enabled=combat?nm_combat_enable(combat,combatPath):-1;
            std::fprintf(trace,"native_combat_bank loaded=%u enabled=%d gameplay_ownership=0\n",combat!=nullptr,enabled);
            std::fflush(trace);
            if(combat)nm_combat_destroy(combat);
            if(enabled<0)return 8;
        }
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
    char crashSetting[4]{};
    if(GetEnvironmentVariableA("SA_STEAM_CRASH_OBSERVER",crashSetting,4)==1&&crashSetting[0]=='1'){
        wcscpy_s(end+1,MAX_PATH-size_t(end+1-executable),L"steam-crash-probe.log");
        crashTrace=CreateFileW(executable,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(crashTrace!=INVALID_HANDLE_VALUE)AddVectoredExceptionHandler(1,observeCrash);
    }
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
