#pragma once
// PID-scoped fixtures only. Native commands create/seat the player; production
// requests still have to come through actual contact or the native event handler.
static int playerProbeBike=-1;
static int urbanizePairGuardFixture(){
    std::ifstream f(folder.parent_path()/"cleo/Urbanize/Urbanize - Root.cs",std::ios::binary);
    std::vector<unsigned char> program((std::istreambuf_iterator<char>(f)),{});
    if(program.size()!=8097)return -80;
    for(int ref:{-1,reference(player())}){
        ProbeScript test(0);test.code=program;auto*base=test.code.data();
        at<unsigned char*>(test.thread,0x10)=base;at<unsigned char*>(test.thread,0x14)=base+0xCFB;
        at<int>(test.thread,0x3C+11*4)=ref;
        auto step=[&](){reinterpret_cast<void(__thiscall*)(void*)>(0x469EB0)(test.thread);};
        step();step();step();
        const auto pc=at<unsigned char*>(test.thread,0x14)-base;
        if(ref<0){if(pc!=0xECA)return -81;}
        else{
            if(pc!=0x1F92)return -82;step();step();
            if(at<unsigned char*>(test.thread,0x14)!=base+0xD03||at<void*>(test.thread,0x3C+8*4)!=player())return -83;
        }
    }
    event("probe_urbanize_pair_guard",-1,-1,"native_VM_invalid_handle_skips_read_valid_handle_preserves_pointer");return 0;
}
static int playerFixture(int mode){
    if(mode==77)return urbanizePairGuardFixture();
    void*p=player();if(!p||!matrix(p))return -70;const int ref=reference(p);
    if(mode==76){probeRecoveryPed=ref;return recoveryAuditFixture(45);}
    if(agents.contains(ref)||pending.contains(ref)||at<float>(p,0x540)<=30)return -71;
    if(mode==70||mode==71){
        if(inVehicle(p))return -72;
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,nullptr,mode==70?22:0,5,3,0);
        event("probe_player_native_damage",ref,0,"weapon="+std::to_string(mode==70?22:0));return ref;
    }
    if(mode==72){
        if(inVehicle(p))return -72;
        const int carRef=carFixture();collision->update(position(p));const auto*car=collision->findVehicle(carRef);
        if(!car)return -73;
        V dest=mul(car->frame,T{{},{0,car->half.y+.85f,0}}).p;dest.z+=4;
        bool found=false;void*support=nullptr;
        float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(dest.x,dest.y,dest.z,&found,&support);
        if(!found)return -74;dest.z=ground+1;
        teleport(p,dest);
        at<V>(p,0x44)=V{};at<V>(car->entity,0x44)=matrix(car->entity)->up*.08f;
        event("probe_player_car_contact",ref,0,"native_moving_car="+std::to_string(carRef));return ref;
    }
    if(mode==73||mode==74){
        if(inVehicle(p))return -72;
        const int model=mode==73?461:481;V here=position(p);
        if(here.x<2390||here.x>2470||here.y<-1710||here.y>-1600)return -75;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        ProbeScript car(0x00A5);car.integer(model);car.number(2436.f);car.number(-1660.f);car.number(here.z);car.result();playerProbeBike=car.run();
        if(!vehicleFromRef(playerProbeBike))return -76;
        // SDK eScriptCommands and primary CharacterCommands.cpp: (ped, vehicle).
        ProbeScript seat(0x036A);seat.integer(ref);seat.integer(playerProbeBike);seat.run();
        if(!ridingBike(p))return -77;
        event("probe_player_seated",ref,0,"model="+std::to_string(model)+" bike="+std::to_string(playerProbeBike));return ref;
    }
    if(mode==75){
        void*v=vehicleFromRef(playerProbeBike);if(!v||!ridingBike(p)||at<void*>(p,0x58C)!=v)return -78;
        alignas(4) unsigned char ev[0x3C]{};
        V velocity=matrix(v)->up*.14f,impact=matrix(v)->up*-.10f;at<V>(v,0x44)=velocity;
        reinterpret_cast<void*(__thiscall*)(void*,void*,V*,V*,float,float,unsigned char,unsigned char,int,void*,bool,bool)>(0x4AFCF0)
            (ev,v,&velocity,&impact,20.f,0.f,49,1,int(gameTime()),nullptr,true,true);
        auto* intelligence=at<unsigned char*>(p,0x47C);
        void* accepted=reinterpret_cast<void*(__thiscall*)(void*,void*,bool)>(0x4AB420)(intelligence+0x68,ev,true);
        reinterpret_cast<void(__thiscall*)(void*,unsigned)>((*reinterpret_cast<void***>(ev))[0])(ev,0);
        event("probe_player_collision_ejection_event",ref,0,accepted?"accepted":"rejected");return accepted?ref:-79;
    }
    return -2;
}
