#pragma once
// Disposable effect fixture; only available to the owned passive test process.
static int wheelSmokeProbeCar=-1;static unsigned wheelSmokeProbeUntil=0,wheelSmokeNext=0;
static void tickWheelSmokeProbe(){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!playing()||!wheelSmokeProbeUntil||gameTime()>wheelSmokeProbeUntil)return;
    void*car=vehicleFromRef(wheelSmokeProbeCar);if(!car||!matrix(car)||gameTime()<wheelSmokeNext)return;
    wheelSmokeNext=gameTime()+100; // sparse diagnostic pulses, no production rate change
    for(int i=0;i<4;i++){
        // CAutomobile::m_wheelColPoint0x724, four CColPoint0x2C; native evaluated contact.
        const V p=at<V>(car,0x724+0x2C*i);
        if(!finite(p)||length(p-position(car))>5.f)continue;
        reinterpret_cast<void(__thiscall*)(void*,void*,V,unsigned char,float)>(0x4A09C0)
            (reinterpret_cast<void*>(0xA9AE00),car,p,1,1.f);
    }
}
static int wheelSmokeFixture(int mode){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -240;
    if(mode==240||mode==241){
        if(inVehicle(player())||vehicleFromRef(wheelSmokeProbeCar))return -240;
        const int model=mode==240?422:445;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        ProbeScript car(0x00A5);car.integer(model);car.number(2430.f);car.number(-1662.f);car.number(position(player()).z);car.result();
        wheelSmokeProbeCar=probeCar=car.run();auto*v=vehicleFromRef(probeCar);if(!v||!matrix(v))return -240;
        ProbeScript heading(0x0175);heading.integer(probeCar);heading.number(90.f);heading.run();
        ProbeScript seat(0x036A);seat.integer(reference(player()));seat.integer(probeCar);seat.run();
        if(!inVehicle(player())||at<void*>(v,0x460)!=player())return -240;
        roadProbeCameraUntil=0;reinterpret_cast<void(__thiscall*)(void*)>(0x50BAB0)(reinterpret_cast<void*>(0xB6F028));
        event("probe_wheel_smoke_car",-1,model,"vehicle_ref="+std::to_string(probeCar)+" native_wheel_points=1");return probeCar;
    }
    auto*v=vehicleFromRef(wheelSmokeProbeCar);if(!v||!matrix(v))return -240;
    if(mode==242){wheelSmokeProbeUntil=gameTime()+12000;event("probe_wheel_smoke_emission",-1,at<short>(v,0x22),"duration_game_ms=12000 native_AddWheelDust=1");return wheelSmokeProbeCar;}
    if(mode==243){
        auto*c=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v);if(!c||!c->data)return -243;
        const V centre=(c->box.min+c->box.max)*.5f,half=(c->box.max-c->box.min)*.5f;
        int accepted=0;
        for(int side=0;side<6;side++){
            const int axis=side/2;V dir{};(&dir.x)[axis]=side%2?1.f:-1.f;
            const float extent=(&half.x)[axis];const V from=point(*matrix(v),centre+dir*(extent+1.f)),target=point(*matrix(v),centre);
            vehicleparticles::Particle p{};p.position=target;p.velocity=normalized(target-from)*2.f;
            const bool hit=vehicleparticles::collide(&p,from,false);if(hit)accepted++;
            event("probe_wheel_smoke_surface",side,at<short>(v,0x22),"hit="+std::to_string(hit)+" from_x="+std::to_string(from.x)+" from_y="+std::to_string(from.y)+" from_z="+std::to_string(from.z)+
                " target_x="+std::to_string(target.x)+" target_y="+std::to_string(target.y)+" target_z="+std::to_string(target.z)+" final_x="+std::to_string(p.position.x)+" final_y="+std::to_string(p.position.y)+" final_z="+std::to_string(p.position.z));
        }return accepted;
    }
    if(mode==244){at<unsigned char>(v,0xB8)=0;at<V>(v,0x44)=matrix(v)->up*.08f;event("probe_wheel_smoke_coast",-1,at<short>(v,0x22),"initial_mps=4 one_shot=1");return wheelSmokeProbeCar;}
    return -240;
}
