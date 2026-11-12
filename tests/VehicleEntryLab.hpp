#pragma once

namespace vehicle_entry_probe {
static int carRef=-1;
static unsigned start=0, previousSample=0, nextAudit=0;
static int fixtureMode=0;
static V previousPosition{};
static bool seenDoor=false, seated=false, struck=false;

static int begin(int mode){
    if(!vehicle_lab::auditTraffic||inVehicle(player())||vehicleFromRef(carRef))return -338;
    const int model=mode==338?477:445;
    for(int asset:{model,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(asset,2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    const V road{2406.f,-1658.f,38.f};
    ColPoint ground{};void* support=nullptr;
    if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
        (road,road.z-50.f,ground,support,true,false,false,true,true,false,nullptr))return -339;
    ProbeScript spawn(0x00A5);spawn.integer(model);spawn.number(road.x);
    spawn.number(road.y);spawn.number(ground.point.z);spawn.result();carRef=spawn.run();
    auto*car=vehicleFromRef(carRef);
    if(!car||!matrix(car))return -338;
    const auto aboveRoad=reinterpret_cast<float(__thiscall*)(void*)>((*static_cast<void***>(car))[53]);
    teleport(car,ground.point+V{0,0,aboveRoad(car)+.01f});
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car,-1.5707963268f);
    if(mode==338){
        ProbeScript driver(0x0129);driver.integer(carRef);driver.integer(4);driver.integer(7);driver.result();
        if(!pedFromRef(driver.run()))return -340;
        ProbeScript speed(0x00AD);speed.integer(carRef);speed.number(0.f);speed.run();
    }
    fixtureMode=mode;start=gameTime();previousSample=0;
    return carRef;
}

static bool startEntry(void*car,unsigned now){
    V approach{};reinterpret_cast<V*(__cdecl*)(V*,const void*,int)>(0x64E740)(&approach,car,10);
    teleport(player(),approach-matrix(car)->right*.4f);
    void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x50);
    if(!task)return false;
    reinterpret_cast<void*(__thiscall*)(void*,void*)>(0x6402F0)(task,car);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(player()),task,0,true);
    previousSample=now;nextAudit=now;previousPosition=position(car);
    seenDoor=seated=struck=false;
    event("vehicle_entry_lab_start",carRef,at<short>(car,0x22),"native_enter_task=1 occupied="+std::to_string(fixtureMode==338));
    return true;
}

static void tick(){
    auto*car=vehicleFromRef(carRef);
    if(!car||gameTime()-start>12000)return;
    const auto now=gameTime();
    if(!previousSample){
        if(now-start>=1200)startEntry(car,now);
        return;
    }
    const auto pos=position(car);
    const float move=length(pos-previousPosition);
    const float delta=std::max(.001f,(now-previousSample)*.001f);
    const bool door=at<unsigned char>(car,0x485)!=0||at<unsigned char>(car,0x486)!=0;
    const bool playerDriver=at<void*>(car,0x460)==player()&&vehicle_lab::carStatus(car)==0;
    const auto it=vehicle_lab::trafficCars.find(carRef);
    const bool iv=it!=vehicle_lab::trafficCars.end()&&it->second.pose.valid&&
        vehicle_lab::frameCounter()-it->second.pose.frame<=1;
    if(door&&!seenDoor){seenDoor=true;event("vehicle_entry_lab_opening",carRef,at<short>(car,0x22));}
    if(playerDriver&&!seated){seated=true;event("vehicle_entry_lab_seated",carRef,at<short>(car,0x22),
        "iv="+std::to_string(iv));}
    if(seated&&iv&&!struck){struck=true;event("vehicle_entry_lab_iv",carRef,at<short>(car,0x22),
        "driver_still_seated="+std::to_string(playerDriver));}
    if(now>=nextAudit){nextAudit=now+30;event("vehicle_entry_lab_frame",carRef,at<short>(car,0x22),
        "door="+std::to_string(door)+" driver="+std::to_string(playerDriver)+" iv="+std::to_string(iv)+
        " speed="+std::to_string(length(at<V>(car,0x44))*50.f)+" move_m="+std::to_string(move)+
        " pose_mps="+std::to_string(move/delta)+" status="+std::to_string(vehicle_lab::carStatus(car))+
        " leaf="+std::to_string(playerLeafTask(player()))+" distance="+std::to_string(length(position(player())-pos))+
        " z="+std::to_string(pos.z)+" vz="+std::to_string(at<V>(car,0x44).z*50.f));}
    previousSample=now;previousPosition=pos;
}
}
