#pragma once
// Drunk CJ as GTA IV's drunk Niko (08/11). SA's drunk level (CPlayerData +0x40, set by script opcode 052C: missions, or
// Grinch Trainer's "Drunk effect", which sets 100 every frame from the scripts' process) starts IV's drunk at level 5,
// the most drinks: 180 s on a script-controlled ragdoll with the drink scripts' bodyBalance and the stick on it
// (romandrink 0x4CD5, 0x4BA0; GIVE_PLAYER_RAGDOLL_CONTROL), the drunk camera for the same 180 s and then a 10 s fade
// (SET_DRUNK_CAM level 5 and the sober-up, 0x4A88). The sway itself stays SA's own effect: IV's (0x82AAA238 through
// the shake code) has not been traced. A fall is watched as romandrink 0x104CD does: down when balance fails or the
// head is 1.2 m or less above the ground; still for 1 s, or 3 s at most, then 100 ms later he gets up; back on his
// feet, he goes back on the ragdoll for the time left. In a car he stays off it. Missions keep SA's own effect.
static bool drunkReplace=true;
static bool drunkHookInstalled=false,drunkLatched=false;
static void(__cdecl* drunkNextScripts)()=nullptr;
static unsigned drunkEnd=0,drunkZeroSince=0,drunkAgentGone=0;static unsigned char drunkSource=0;static bool drunkAgentSeen=false,drunkStopSent=false;
struct DrunkWatch {bool fallen=false,rising=false,up=false;unsigned t=0,cap=0;};
static DrunkWatch drunkWatch;
static constexpr unsigned drunkRagdollMs=180000,drunkFadeMs=10000; // level 5 (0x4BA0) and the sober-up's -10,000 (0x4A88)
static unsigned char* drunkPlayerData(){void*p=player();return p?at<unsigned char*>(p,0x480):nullptr;}
static bool drunkMission(){return protectMissions&&scriptMissionActive();}
static bool drunkActive(unsigned now){return drunkEnd&&now<drunkEnd;}
static void __cdecl drunkScripts(){
    auto*d=drunkPlayerData();const bool manage=drunkReplace&&d&&!drunkMission();
    if(manage)d[0x40]=0; // a setter this frame shows as non-zero after the scripts
    drunkNextScripts();
    d=drunkPlayerData();if(!d||!drunkReplace)return;
    const unsigned now=gameTime();
    // Test only (GTASA_DRUNK_TEST plus DeliriumSA\drunk-test-go): set 100 every frame after the scripts, as Grinch does.
    static const bool drunkTest=GetEnvironmentVariableA("GTASA_DRUNK_TEST",nullptr,0)>0;
    if(drunkTest){static unsigned checked=0;static bool go=false;
        if(now-checked>1000){checked=now;go=GetFileAttributesW((folder/"drunk-test-go").c_str())!=INVALID_FILE_ATTRIBUTES;}
        if(go)d[0x40]=100;}
    if(drunkMission()){if(drunkActive(now))drunkEnd=now;return;}
    const unsigned char raw=d[0x40];
    if(raw){
        drunkZeroSince=0;drunkSource=raw;
        if(!drunkLatched){
            drunkLatched=true;drunkEnd=now+drunkRagdollMs;drunkStopSent=false;drunkWatch={};
            event("drunk_started",reference(player()),0,"sa_level="+std::to_string(raw)+" iv_level=5 ragdoll_ms=180000 camera_ms=180000 fade_ms=10000");
        }
    }else{
        if(!drunkZeroSince)drunkZeroSince=now;
        if(drunkLatched&&now>=drunkEnd+drunkFadeMs&&now-drunkZeroSince>2000)drunkLatched=false;
    }
    float level=0;
    if(drunkActive(now))level=drunkSource;
    else if(drunkEnd&&now<drunkEnd+drunkFadeMs)level=drunkSource*(1.f-float(now-drunkEnd)/drunkFadeMs);
    d[0x40]=static_cast<unsigned char>(std::clamp(level,0.f,255.f));d[0x41]=0;
}
// CGame::Process's call of CTheScripts::Process (0x53C077), where plugin-sdk's processScriptsEvent runs its handlers
// after the scripts: wrapped once in play, from the main thread, after every plugin has installed its own hook.
static void installDrunkHook(){
    if(drunkHookInstalled||!drunkReplace)return;drunkHookInstalled=true;
    auto*site=reinterpret_cast<unsigned char*>(0x53C077);
    if(site[0]!=0xE8){event("drunk_hook_refused",-1,-1,"opcode="+std::to_string(site[0]));return;}
    const uintptr_t target=reinterpret_cast<uintptr_t>(site+5)+*reinterpret_cast<int32_t*>(site+1);
    drunkNextScripts=reinterpret_cast<void(__cdecl*)()>(target);
    DWORD old=0;if(!VirtualProtect(site+1,4,PAGE_EXECUTE_READWRITE,&old)){event("drunk_hook_refused",-1,-1,"protect");return;}
    *reinterpret_cast<int32_t*>(site+1)=int32_t(reinterpret_cast<uintptr_t>(&drunkScripts)-reinterpret_cast<uintptr_t>(site+5));
    VirtualProtect(site+1,4,old,&old);FlushInstructionCache(GetCurrentProcess(),site,5);
    event("drunk_hook_installed",-1,-1,"previous_target="+std::to_string(target));
}
// Once a frame: put CJ back on the drunk ragdoll while the time lasts (0x4CD5 with the time left).
static void tickDrunk(){
    installDrunkHook();
    auto*p=player();if(!p)return;const int ref=reference(p);const unsigned now=gameTime();
    const bool agent=agents.contains(ref);
    if(drunkAgentSeen&&!agent)drunkAgentGone=now;
    drunkAgentSeen=agent&&agents.at(ref)->kind==Reaction::Drunk;
    if(!drunkActive(now)||agent||pending.contains(ref)||!recoveryAllowed()||inVehicle(p)||at<float>(p,0x540)<=0)return;
    if(now-drunkAgentGone<400||(at<unsigned char>(p,0x46D)&2)||combatOwns(p)||!validPed(p,true,true,true))return;
    Request r;r.kind=Reaction::Drunk;r.time=now;r.direction=matrix(p)->up;r.playerVehicle=true;r.part=0;
    pending[ref]=r;drunkWatch={};
    event("drunk_ragdoll_requested",ref,0,"ms_left="+std::to_string(drunkEnd-now));
}
// The watch (0x104CD) on CJ's drunk body, once a frame; true when he should come off the ragdoll (a fall waited out,
// or the time over), which the caller hands to the get-up.
static bool drunkAgentFrame(Active&a,unsigned now){
    if(!drunkActive(now)){
        if(!drunkStopSent){drunkStopSent=true;ok(nm_agent_drunk_balance(a.agent,collision->world,0));event("drunk_ragdoll_finished",a.ref,a.model,"original_stop=0x4C24");}
        return true;
    }
    auto&w=drunkWatch;
    if(w.up)return true;
    if(w.rising){if(now>w.t){w.up=true;event("drunk_getup",a.ref,a.model,"ms_left="+std::to_string(drunkEnd-now));return true;}return false;}
    V linear{},spin{};nm_agent_get_part_velocity(a.agent,0,&linear.x,&spin.x);const float speed=length(linear);
    if(w.fallen){
        if(w.cap<now){w.fallen=false;w.rising=true;w.t=now+100;}
        else if(speed<.1f){if(!w.t)w.t=now+1000;else if(w.t<now){w.fallen=false;w.rising=true;w.t=now+100;}}
        else w.t=0;
        return false;
    }
    const V head=a.current[12].p;bool found=false;
    const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(head.x,head.y,head.z+2.f,&found,nullptr);
    const float above=found?head.z-ground:0.f;
    const bool low=!(std::abs(above)>1.2f),failed=nm_agent_has_fallen(a.agent)==1;
    if(failed||low){w.fallen=true;w.t=0;w.cap=now+3000;event("drunk_fall",a.ref,a.model,"balance_failed="+std::to_string(failed)+" head_above_ground_m="+std::to_string(above));return false;}
    // GIVE_PLAYER_RAGDOLL_CONTROL: the stick (pad 0's left stick, 0xB73458) camera-relative, the camera's forward row
    // (TheCamera 0xB6F028, m_mCameraMatrix +0x974, forward at +0x10).
    const auto*pad=reinterpret_cast<const short*>(0xB73458);
    const V forward=at<V>(reinterpret_cast<void*>(0xB6F028),0x974+0x10);
    const float heading=std::atan2(-forward.x,forward.y);
    const V place=a.current[0].p;
    ok(nm_agent_player_control(a.agent,collision->world,std::clamp(pad[0]+128,0,255),std::clamp(pad[1]+128,0,255),heading,&place.x));
    return false;
}
