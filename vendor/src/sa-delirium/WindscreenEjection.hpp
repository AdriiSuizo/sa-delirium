#pragma once
// GTA IV windscreen ejection: an occupant of a car that crashes head-on hard enough flies out through the glass as a
// ragdoll. Included by Adapter.cpp AFTER VehicleExits.hpp / BikeTransitions.hpp / BikeHitFalls.hpp.
//
// IV mechanism (Xbox 360 TU8 exe, re/image.bin disassembled; the LSC4 viewer has NO port of it, only the script flag):
//  * Every occupant runs CTaskComplexCarDriveBasic (task 709); its vf20 0x823EEEA0 calls the driver's crash check
//    0x823E8048 each frame (call at 0x823EF5A4; task state +0x40 := the kind it returns; leaf by state 0x823EC1D8).
//    Kind 3 = shunt clip (dv > 2.0, 0x82AA274C), kind 6 = THROUGH WINDSCREEN (this file).
//  * 0x823E8048: dv = vehicle +0x170 (the frame's impact impulse) x archetype vf+0x28 (inverse mass) = the car's speed
//    change this frame (m/s); direction = vehicle +0x180 (direction of the impulse ON the car). docs: audio/speech.md:409.
//  * Who may fly (+0x234 bit 0x800000, SET_CHAR_WILL_FLY_THROUGH_WINDSCREEN 0x825C8650, native_meanings.csv:164,
//    scripts.rs:3024). Default is set; becoming a mission ped (+0x9B0 = 2, 0x822F3778 at 0x822F3838) clears it unless
//    the ped is the player. Player or +0x9B0 = 2 (vf+0x114 = player): flag only, threshold 18.0 (0x82AA2760); a network
//    player also needs a draw > 0.9 (0x82AA2754). Ambient ped (+0x9B0 = 1): flag AND rand(0,1) > 0.75 (0x82AA2758,
//    drawn every frame the check runs, 0x823E8230..0x823E826C), threshold 12.0 (0x82AA275C).
//  * Kind 6 needs also: vehicle +0x12E4 == 0 (a car, not bike/boat/heli), model not "apc" / "n_apc" (globals 0x82A9A77C /
//    0x82A9A7F4, names at 0x8200D80C / 0x8200D8A0), dv > threshold, seat 0 (driver) or 2 (front passenger) by
//    0x8237ACF0, and dot(vehicle forward row +0x10, impact dir) < -0.707 (0x8200A81C), i.e. a frontal hit
//    (0x823E8390..0x823E843C). Then out kind := 6 and task +0x50 := 5.0 x dv (0x82AA2750); task +0x60 := -dv x impact dir
//    (0x823E8454..0x823E8468). The real player (+0x211 set, +0x210 clear) with health > 101 also queues a CEventDamage of
//    weapon 0x31, damage 0 (0x823E84A0..0x823E8514, the same record as the bike throw, bikethrow.rs:18).
//  * State 6 leaf = CTaskComplexNewExitVehicle(vehicle, flags 0x2440, wait 0) with +0x50 := that vector
//    (0x823EC358..0x823EC390); exit-vehicle.md:67: flag 0x2000 -> CTaskSimpleCarJumpOut(vehicle, door 0x1C = windscreen,
//    vector) (ctor 0x824B80E0 keeps it at +0x30). Its clip is THROUGH_WINDSCREEN_DS/PS 0x15A/0x15B (vehclip.rs:330,
//    player.rs:2762).
//  * CarJumpOut vf17 0x824BD3F0 at the clip event (+0x74 bit 0x80000): SetPedOut (0x824B82E0/0x824BA668), ped velocity :=
//    vehicle velocity (0x8247F840), no-collision with the vehicle (0x8247EEA8, CPhysical +0x1F4), NM gate reason 15
//    (0x826174F0) -> NMJumpRollFromRoadVehicle 0x82613AA8 with 3000..6000 ms (0x824BD96C..0x824BD974), as the bail in
//    player.rs:2759-2790; and for door 0x1C an extra impulse mass x (0.25 x vector + (0,0,3)) through 0x82480480
//    (0x824BD9D0..0x824BDA50; constants 0x82AA6064 = 0.25, 0x82AA6068 = 3.0, 0x82B071E0 = up).
// SA mapping: the car's per-frame velocity change (beforeVehicleVelocity -> after the world process) stands for +0x170/mass and
// its direction for +0x180; SA's CTaskSimpleCarSetPedOut (the task IV runs inside CarJumpOut) removes the ped; the ragdoll goes
// through the existing Reaction::VehicleExit pipeline (jump roll 3..6 s, no-collision with the car, player rules) with IV's launch.
static bool windscreenEjection=true;
constexpr float windscreenPlayerDv=18.f,windscreenPedDv=12.f,windscreenChance=.75f,windscreenForwardDot=-.707f,windscreenAlong=.25f,windscreenUp=3.f;
struct WindscreenRun {V last{};unsigned lastFrame=0,decided[2]{0,0};};
static std::map<int,WindscreenRun> windscreenRuns; // per vehicle ref
struct WindscreenMark {V launch{};unsigned time=0;};
static std::map<int,WindscreenMark> windscreenMarks; // per ejected ped ref, until its NM task is verified
static bool windscreenAudit(){static const bool on=GetEnvironmentVariableA("GTASA_WINDSCREEN_AUDIT",nullptr,0)!=0;return on;}
// CTaskSimpleCarSetPedOut (ctor 0x6478B0, 0x18 bytes, +0x12 m_bFallingOutOfCar like the jump/knock-off users), run through its
// vtable ProcessPed (slot 7) and destroyed without freeing: IV's 0x824B82E0 / 0x824BA668 / 0x824B8360 sequence (0x824BD62C).
static bool windscreenSetPedOut(void*ped,void*v){
    alignas(4) unsigned char task[0x18]{};
    int door=reinterpret_cast<int(__cdecl*)(const void*,const void*)>(0x64F110)(v,ped); // CCarEnterExit::ComputeTargetDoorToExit
    if(door<8||door>11)door=at<void*>(v,0x460)==ped?10:8; // front left / front right
    reinterpret_cast<void*(__thiscall*)(void*,void*,int,bool)>(0x6478B0)(task,v,door,false);
    at<bool>(task,0x12)=true;
    reinterpret_cast<bool(__thiscall*)(void*,void*)>((*reinterpret_cast<void***>(task))[7])(task,ped);
    reinterpret_cast<void(__thiscall*)(void*,unsigned)>((*reinterpret_cast<void***>(task))[0])(task,0);
    return !inVehicle(ped);
}
static bool windscreenOccupant(void*ped,void*v){
    const int ref=reference(ped);
    if(ref<0||!matrix(ped)||!inVehicle(ped)||at<void*>(ped,0x58C)!=v||at<float>(ped,0x540)<=0||at<unsigned char>(ped,0x2F)!=area())return false;
    // IV +0x9B0 == 1 (ambient) keeps +0x234 bit 23; mission peds lose it (0x822F3838). SA 0x484 == 1 is the ambient ped.
    if(ped==player()){if(!physicalVehicleExits)return false;}
    else if(at<void*>(ped,0x480)||at<unsigned char>(ped,0x484)!=1||int(agents.size()+pending.size())>=maxActive)return false;
    if(length(position(ped)-position(player()))>radius)return false;
    return !agents.contains(ref)&&!pending.contains(ref)&&!exitTransits.contains(ref);
}
// vehicle_exit_start picks NMBalance at or below 7.5 m/s; IV's CarJumpOut always runs the jump roll (0x824BD96C). Replace a freshly
// started balance by the SDK's always-jump-roll start (its finish window is 2..30 s there, 3..6 s in IV: engine follow-up noted).
static void windscreenNmFix(){
    const unsigned now=gameTime();
    for(auto it=windscreenMarks.begin();it!=windscreenMarks.end();){
        const auto a=agents.find(it->first);
        if(now-it->second.time>4000||(a==agents.end()&&now-it->second.time>1500)){it=windscreenMarks.erase(it);continue;}
        if(a!=agents.end()&&a->second->agent&&a->second->kind==Reaction::VehicleExit&&!a->second->mounted){
            float s[4]{};const int phase=nm_agent_vehicle_exit_status(a->second->agent,s);
            if(phase==1){
                const V launch=it->second.launch;
                if(ok(nm_agent_bike_throw_start(a->second->agent,collision->world,&launch.x,at<float>(a->second->ped,0x540)*2.f,a->second->ped==player()?1u:0u)))
                    event("windscreen_jump_roll_forced",a->first,a->second->model,"task=jump_roll speed_mps="+std::to_string(length(launch))+" original_times=3_6 sdk_times=2_30 reason=balance_below_7_5_mps");
            }
            if(phase>=1){it=windscreenMarks.erase(it);continue;}
        }
        ++it;
    }
}
static void tickWindscreenEjection(){
    if(!windscreenEjection||!ready||!recoveryAllowed()||!player()){windscreenRuns.clear();windscreenMarks.clear();return;}
    windscreenNmFix();
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(auto it=windscreenRuns.begin();it!=windscreenRuns.end();)if(!vehicleFromRef(it->first))it=windscreenRuns.erase(it);else ++it;
    for(const auto&[ref,before]:beforeVehicleVelocity){
        void*v=vehicleFromRef(ref);if(!v||!matrix(v))continue;
        const int type=at<int>(v,0x594),status=at<unsigned char>(v,0x36)>>3;
        if((type!=0&&type!=1)||at<short>(v,0x22)==432||status==1||status==5)continue; // car/monster only; no Rhino (IV apc), recorded or wrecked
        void*seat[2]={at<void*>(v,0x460),at<void*>(v,0x464)}; // IV seats 0 and 2: driver and front passenger
        if(!seat[0]&&!seat[1]){windscreenRuns.erase(ref);continue;}
        const V now=at<V>(v,0x44)*50.f,d=now-before;
        auto&run=windscreenRuns[ref];
        // IV's impact is one frame's impulse; our contact can spread over two frames, so that pair counts once.
        V best=d;if(run.lastFrame&&frame-run.lastFrame==1&&length(run.last+d)>length(d))best=run.last+d;
        run.last=d;run.lastFrame=frame;
        // Tests only: GTASA_WINDSCREEN_TEST_DV=x replaces both thresholds (exercise the detach on a small frontal hit).
        static const float testDv=[]{char b[16]{};return GetEnvironmentVariableA("GTASA_WINDSCREEN_TEST_DV",b,16)?float(std::atof(b)):0.f;}();
        const float pedDv=testDv>0?testDv:windscreenPedDv,playerDv=testDv>0?testDv:windscreenPlayerDv;
        const float dv=length(best);
        if(!(dv>pedDv)||dv>200.f||!finite(best))continue;
        const V dir=best*(1.f/dv);const float along=dot(normalized(matrix(v)->up),dir);
        if(windscreenAudit())event("windscreen_eval",-1,at<short>(v,0x22),"vehicle_ref="+std::to_string(ref)+" dv_mps="+std::to_string(dv)+" forward_dot="+std::to_string(along)+
            " frontal="+std::to_string(along<windscreenForwardDot)+" driver="+std::to_string(reference(seat[0]))+" passenger="+std::to_string(reference(seat[1])));
        if(!(along<windscreenForwardDot))continue;
        for(int s=0;s<2;s++){
            void*ped=seat[s];if(!ped||!windscreenOccupant(ped,v))continue;
            const bool isPlayer=ped==player();
            if(dv<=(isPlayer?playerDv:pedDv))continue;
            if(!isPlayer){
                // 0x823E8238..0x823E8264: a draw over 0.75 per check frame; one draw per crash here (consecutive frames are one crash).
                const bool again=run.decided[s]&&frame-run.decided[s]<=1;run.decided[s]=frame;if(again)continue;
                // Tests only: GTASA_WINDSCREEN_TEST_DRAW=1 makes every draw pass.
                static const bool forceDraw=GetEnvironmentVariableA("GTASA_WINDSCREEN_TEST_DRAW",nullptr,0)!=0;
                const float draw=forceDraw?1.f:reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(0.f,1.f);
                if(windscreenAudit())event("windscreen_draw",reference(ped),at<short>(ped,0x22),"draw="+std::to_string(draw)+" need_over="+std::to_string(windscreenChance));
                if(!(draw>windscreenChance))continue;
            }
            const auto pose=captureVehiclePose(ped,v);
            if(!pose){event("windscreen_pose_unavailable",reference(ped),at<short>(ped,0x22),"seated_pose_rejected no_ejection=1");continue;}
            // Jump vector -dv x impact dir (0x823E8454..0x823E8468); ped velocity = vehicle velocity (0x8247F840) plus the door-0x1C
            // impulse mass x (0.25 x vector + (0,0,3)) (0x824BD9D0..0x824BDA50).
            const V jump=V{}-dir*dv;const V launch=now+jump*windscreenAlong+V{0,0,windscreenUp};
            const int pedRef=reference(ped);const short model=at<short>(ped,0x22);
            if(!windscreenSetPedOut(ped,v)){event("windscreen_detach_failed",pedRef,model,"still_in_vehicle=1 no_ejection=1");continue;}
            // SA's SetPedOut places the ped at the door (1.3-1.7 m): the body leaves the cabin from its seat, as IV's clip does.
            const bool moved=movePhysicalRoot(ped,pose->sampledRoot.p,isPlayer);
            clearBikeNativeIgnore(ped,v);
            pose->velocity=launch*(1.f/50.f);pose->angular=V{};
            queuePhysicalExit(ped,pose,moved?position(ped):pose->sampledRoot.p,Reaction::VehicleExit);
            windscreenMarks[pedRef]={launch,gameTime()};
            event("windscreen_ejection",pedRef,model,"vehicle_ref="+std::to_string(ref)+" vehicle_model="+std::to_string(at<short>(v,0x22))+" seat="+std::to_string(s?2:0)+
                " dv_mps="+std::to_string(dv)+" forward_dot="+std::to_string(along)+" threshold="+std::to_string(isPlayer?playerDv:pedDv)+
                " vehicle_mps="+std::to_string(length(now))+" launch_mps="+std::to_string(length(launch))+" launch_vz="+std::to_string(launch.z)+" root_kept="+std::to_string(moved)+
                " iv_kind=6 door=0x1C added_launch=iv_0.25_vector_plus_3_up");
        }
    }
}
