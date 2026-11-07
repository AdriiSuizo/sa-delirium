#pragma once
#include <cstring>
// Disposable native fixtures, invoked only by the PID-scoped diagnostic bridge.
// No script is linked into the mission list and no save command is provided.
struct ProbeScript {
    alignas(4) unsigned char thread[0xE0]{};
    std::vector<unsigned char> code;
    explicit ProbeScript(unsigned short op){append(op);std::memcpy(thread+8,"DLMTEST",7);}
    template<class A>void append(A value){const auto*p=reinterpret_cast<const unsigned char*>(&value);code.insert(code.end(),p,p+sizeof(value));}
    void integer(int value){append<unsigned char>(1);append(value);}
    void number(float value){append<unsigned char>(6);append(value);}
    void result(){append<unsigned char>(3);append<unsigned short>(0);}
    int run(){at<unsigned char*>(thread,0x10)=code.data();at<unsigned char*>(thread,0x14)=code.data();at<int>(thread,0x3C)=-1;reinterpret_cast<void(__thiscall*)(void*)>(0x469EB0)(thread);return at<int>(thread,0x3C);}
};
static int probeBikePed=-1,probeBike=-1;
static int probeCar=-1;
static int probeRecoveryPed=-1;
static int recoveryAuditFixture(int mode){
    void*p=pedFromRef(probeRecoveryPed);if(!p||!matrix(p))return -34;
    if(mode==46){
        auto it=agents.find(probeRecoveryPed);if(it==agents.end()||!it->second->recovering||it->second->recoveryStage<3)return -45;
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);if(!t)return -46;
        reinterpret_cast<void*(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(t,10000,false,false,8.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,0,true);
        event("probe_recovery_task_replaced",probeRecoveryPed,at<short>(p,0x22),"replacement="+std::to_string(uintptr_t(t)));
    }
    std::ofstream f(folder/"recovery-audit.json");
    f<<"{\"pid\":"<<GetCurrentProcessId()<<",\"ref\":"<<probeRecoveryPed<<",\"physical_task\":"<<uintptr_t(physicalTask(p))
     <<",\"active\":"<<(agents.contains(probeRecoveryPed)?"true":"false")<<",\"body_relocations\":"<<bodyRelocations
     <<",\"relocation_task_changes\":"<<relocationTaskChanges<<",\"animations\":[";
    bool comma=false;for(unsigned id:{112u,115u}){
        void*a=reinterpret_cast<void*(__cdecl*)(void*,unsigned)>(0x4D68B0)(at<void*>(p,0x18),id);if(!a)continue;
        if(comma)f<<',';comma=true;
        f<<"{\"id\":"<<id<<",\"speed\":"<<at<float>(a,0x24)<<",\"time\":"<<at<float>(a,0x20)<<",\"blend\":"<<at<float>(a,0x18)<<'}';
    }f<<"]}";return probeRecoveryPed;
}
static int recoveryFixture(int mode){
    if(mode==40){
        static int count=0;if(count++>=8)return -40;
        V dest=position(player())+V{7.f,float(count%3)*2.f,0};
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        void*p=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,dest,true);
        if(!p)return -37;probeRecoveryPed=reference(p);event("probe_recovery_ped",probeRecoveryPed,7);return probeRecoveryPed;
    }
    void*p=pedFromRef(probeRecoveryPed);if(!p||!validPed(p))return -34;
    const int weapon=(mode==41||mode==44)?0:22;
    reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,nullptr,weapon,mode==43?1000:mode==44?30:12,3,0);
    event("probe_recovery_damage",probeRecoveryPed,at<short>(p,0x22),"weapon="+std::to_string(weapon));return probeRecoveryPed;
}
static int armedCombatFixture(int mode){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -256;
    if(mode==260){
        const V start=position(player())+V{1.55f,0,0};
        for(int model:{280,334})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        auto*cop=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(6,280,start,true);
        if(!cop)return -260;
        reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(cop,3,1,true);
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(cop,3);
        auto*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x28);
        if(!t)return -261;
        reinterpret_cast<void*(__thiscall*)(void*,void*,int,unsigned)>(0x61C470)(t,player(),0,15000);
        reinterpret_cast<void(__thiscall*)(void*,void*,int)>(0x681B60)(taskManager(cop),t,0);
        event("probe_nightstick_native_task",reference(cop),280,"native_secondary_fight=1 weapon="+std::to_string(combatWeapon(cop)));
        return reference(cop);
    }
    auto*p=pedFromRef(probeRecoveryPed);
    if(!p)return -257;
    // Tests only: a bullet line beside the target, one Uzi round, one eight-pellet shotgun blast.
    if(mode==265){const V at=position(p)+V{.7f,0,.2f};nearmiss::lines.push_back({at-V{0,6.f,0},at+V{0,6.f,0}});return probeRecoveryPed;}
    if(mode==266){reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,player(),28,3,3,0);return probeRecoveryPed;}
    if(mode==267){for(int i=0;i<8;i++)reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,player(),25,2,i&1?3:4,0);return probeRecoveryPed;}
    // Tests only: CJ fires a real Uzi round through CWeapon::Fire; a shared step that fails; a solver launch.
    if(mode==268){
        static bool armed=false;if(!armed){armed=true;reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(player(),28,2000,false);reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(player(),28);}
        V to=position(p)+V{0,0,.25f};V from=position(player())+V{0,0,.5f};const V line=to-from;const float reach=length(line);if(reach>3.f)from=to-line*(3.f/reach);
        const unsigned slot=at<unsigned char>(player(),0x718);void*gun=reinterpret_cast<char*>(player())+0x5A0+slot*0x1C;
        const float heading=std::atan2(-(to.x-from.x),to.y-from.y);at<float>(player(),0x558)=heading;at<float>(player(),0x55C)=heading;
        const float before=at<float>(p,0x540);const int ammo=at<int>(gun,0x8);
        const bool fired=reinterpret_cast<bool(__thiscall*)(void*,void*,V*,V*,void*,V*,V*)>(0x742300)(gun,player(),&from,&from,nullptr,&to,nullptr);
        const bool hit=at<float>(p,0x540)<before;
        // The line of CWeapon::Fire follows the camera for CJ, which a hidden test does not aim: the round lands through the same impact call.
        if(!hit){
            alignas(4) unsigned char point[0x2C]{};const V normal=normalized(from-to);*reinterpret_cast<V*>(point)=to;*reinterpret_cast<V*>(point+0x10)=normal;point[0x25]=3;
            reinterpret_cast<void(__thiscall*)(void*,void*,void*,V*,V*,void*,int)>(0x73B550)(gun,player(),p,&from,&to,point,0);
        }
        event("probe_cj_round",probeRecoveryPed,at<short>(p,0x22),"fired="+std::to_string(fired)+" line_hit="+std::to_string(hit)+" ammo="+std::to_string(ammo)+" health="+std::to_string(at<float>(p,0x540)));
        return probeRecoveryPed;
    }
    if(mode==269){stepFailTest=3;return probeRecoveryPed;}
    if(mode==270){stepLaunchTest=true;return probeRecoveryPed;}
    if(mode==271){recoveryThreatTest=true;return probeRecoveryPed;}
    if(mode==256){
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,player(),22,24,3,0);
        return probeRecoveryPed;
    }
    if(!groundShotOwner(p))return -258;
    const bool dead=at<float>(p,0x540)<=0;
    if((mode==259)!=dead)return -259;
    if(dead){
        auto*manager=taskManager(p);
        auto*t=manager?reinterpret_cast<void*(__thiscall*)(void*)>(0x6819D0)(manager):nullptr;
        if(!t||reinterpret_cast<int(__thiscall*)(void*)>(at<void**>(t,0)[4])(t)!=TASK_SIMPLE_DEAD)return -259;
    }
    reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,player(),22,mode==258?1000:8,4,0);
    event("probe_grounded_shot",probeRecoveryPed,at<short>(p,0x22),"mode="+std::to_string(mode));
    return probeRecoveryPed;
}
static int carFixture(int model=400){
    if(vehicleFromRef(probeCar))return probeCar;
    {char name[8]{};if(model==400&&GetEnvironmentVariableA("GTASA_PROBE_CAR_MODEL",name,8))model=std::atoi(name);} // tests: 415 Cheetah
    const V here=position(player());if(here.x<2400||here.x>2460||here.y<-1700||here.y>-1600)return -1;
    if(model!=400&&model!=445&&model!=415)return -1;
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    ProbeScript car(0x00A5);car.integer(model);car.number(2430.f);car.number(-1668.f);car.number(here.z);car.result();probeCar=car.run();return probeCar;
}
static int bikeFixture(int mode,int vehicleModel=461){
    if(mode==4){
        if(vehicleFromRef(probeBike)||pedFromRef(probeBikePed))return -30;
        const V here=position(player());if(here.x<2400||here.x>2460||here.y<-1700||here.y>-1600)return -31;
        for(int model:{vehicleModel,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        static int sequence=0;sequence++;
        ProbeScript car(0x00A5);car.integer(vehicleModel);car.number(here.x+8+(sequence-1)*3);car.number(here.y);car.number(here.z);car.result();probeBike=car.run();
        if(!vehicleFromRef(probeBike))return -32;
        ProbeScript rider(0x0129);rider.integer(probeBike);rider.integer(4);rider.integer(7);rider.result();probeBikePed=rider.run();
        auto*p=pedFromRef(probeBikePed);if(!p||!ridingBike(p))return -33;
        // GTASA_BIKE_FIXTURE_CRUISE=<speed>: the rider drives on as a normal
        // civilian (00A8 CAR_WANDER_RANDOMLY, 00AD cruise speed) before the hit.
        // 00A9 is CAR_SET_IDLE: it kept the bike parked.
        char cruise[16]{};if(GetEnvironmentVariableA("GTASA_BIKE_FIXTURE_CRUISE",cruise,sizeof cruise)){
            ProbeScript wander(0x00A8);wander.integer(probeBike);wander.run();
            ProbeScript speed(0x00AD);speed.integer(probeBike);speed.number(float(std::atof(cruise)));speed.run();
            event("probe_bike_fixture_cruise",probeBikePed,at<short>(p,0x22),std::string("cruise=")+cruise);
        }
        event("probe_bike_rider_created",probeBikePed,at<short>(p,0x22),"bike_ref="+std::to_string(probeBike));return probeBikePed;
    }
    auto*p=pedFromRef(probeBikePed);auto*v=vehicleFromRef(probeBike);
    if(!p||!v||!ridingBike(p)||at<float>(p,0x540)<=0)return -34;
    if(mode==5){
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,nullptr,22,12,3,0);
        event("probe_mounted_damage",probeBikePed,at<short>(p,0x22));return probeBikePed;
    }
    // Submit the stock knock-off event through the stock event group. Its
    // accepted response calls SetPedOutCar; the production hook observes it.
    alignas(4) unsigned char ev[0x3C]{};V velocity=at<V>(v,0x44),impact{.10f,0,.04f};
    reinterpret_cast<void*(__thiscall*)(void*,void*,V*,V*,float,float,unsigned char,unsigned char,int,void*,bool,bool)>(0x4AFCF0)
        (ev,v,&velocity,&impact,30.f,0.f,53,1,int(gameTime()),nullptr,true,true);
    auto* intelligence=at<unsigned char*>(p,0x47C);
    void* accepted=reinterpret_cast<void*(__thiscall*)(void*,void*,bool)>(0x4AB420)(intelligence+0x68,ev,true);
    reinterpret_cast<void(__thiscall*)(void*,unsigned)>((*reinterpret_cast<void***>(ev))[0])(ev,0);
    event("probe_native_bike_event",probeBikePed,at<short>(p,0x22),accepted?"accepted":"rejected");
    return accepted?probeBikePed:-35;
}
static int bikeHitFixture(int mode){
    if(mode==20||mode==21){
        static unsigned count=0;if(count++>=6)return -40;
        // The previous case's bike and rider lay 3 m from the next spawn: the
        // next rider fell onto them, a two-bike pile instead of the case under
        // test. Remove them first (DELETE_CHAR 009B, DELETE_CAR 00A6).
        // GTASA_BIKE_FIXTURE_PILE keeps them: the pile case for a body between two lying vehicles.
        if(GetEnvironmentVariableA("GTASA_BIKE_FIXTURE_PILE",nullptr,0)){event("probe_bike_fixture_pile_kept",probeBikePed,-1,"bike_ref="+std::to_string(probeBike));probeBike=-1;probeBikePed=-1;return bikeFixture(4,mode==20?461:481);}
        if(auto*old=pedFromRef(probeBikePed);old&&old!=player()){ProbeScript d(0x009B);d.integer(probeBikePed);d.run();}
        if(vehicleFromRef(probeBike)){ProbeScript d(0x00A6);d.integer(probeBike);d.run();}
        event("probe_bike_fixture_cleared",probeBikePed,-1,"bike_ref="+std::to_string(probeBike)+" ped_left="+std::to_string(pedFromRef(probeBikePed)!=nullptr)+" bike_left="+std::to_string(vehicleFromRef(probeBike)!=nullptr));
        probeBike=-1;probeBikePed=-1;return bikeFixture(4,mode==20?461:481);
    }
    auto*p=pedFromRef(probeBikePed);if(!p||!ridingBike(p))return -34;
    const int weapon=mode==31?0:22;
    // CJ is the shooter, as in play: the damage event then carries his side.
    reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,player(),weapon,mode==32?1000:12,3,0);
    event("probe_bike_hit",probeBikePed,at<short>(p,0x22),"weapon="+std::to_string(weapon));return probeBikePed;
}
