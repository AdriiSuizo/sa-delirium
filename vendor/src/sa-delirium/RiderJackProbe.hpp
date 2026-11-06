#pragma once
// A real stock enter-as-driver task against a disposable occupied bicycle.
// No damage, knock-off event, detach, impulse or simulated input is injected.
static int riderJackAttacker=-1;
static int riderJackFixture(int mode){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -245;
    // GTASA_JACK_MOTORCYCLE=1: the same stock jack against a PCJ-600 (461).
    const bool motorcycle=GetEnvironmentVariableA("GTASA_JACK_MOTORCYCLE",nullptr,0)>0;const int bikeModel=motorcycle?461:481;
    if(mode==247){
        if(inVehicle(player())||vehicleFromRef(probeBike))return -247;
        for(int model:{bikeModel,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        ProbeScript bike(0x00A5);bike.integer(bikeModel);bike.number(2430.f);bike.number(-1661.f);bike.number(position(player()).z);bike.result();probeBike=bike.run();
        void*v=vehicleFromRef(probeBike);if(!v||!matrix(v))return -247;
        ProbeScript seat(0x036A);seat.integer(reference(player()));seat.integer(probeBike);seat.run();
        if(!ridingBike(player())||at<void*>(v,0x460)!=player())return -247;
        V approach{};reinterpret_cast<V*(__cdecl*)(V*,const void*,int)>(0x64E740)(&approach,v,10);approach=approach-matrix(v)->right*.65f;
        void*attacker=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,approach,true);if(!attacker)return -247;
        riderJackAttacker=reference(attacker);probeBikePed=reference(player());watchSessionPed(player());watchSessionPed(attacker);
        event("probe_bicycle_jack_prepared",probeBikePed,0,"player_victim=1 attacker_ref="+std::to_string(riderJackAttacker)+" vehicle_ref="+std::to_string(probeBike));return probeBikePed;
    }
    if(mode!=248&&inVehicle(player()))return -245;
    if(mode==245)return bikeHitFixture(motorcycle?20:21);
    void*v=vehicleFromRef(probeBike),*p=pedFromRef(probeBikePed);
    void*attacker=mode==248?pedFromRef(riderJackAttacker):player();
    if((mode!=246&&mode!=248)||!v||!p||!attacker||!ridingBike(p)||!matrix(v))return -246;
    V approach{};reinterpret_cast<V*(__cdecl*)(V*,const void*,int)>(0x64E740)(&approach,v,10);
    teleport(attacker,approach-matrix(v)->right*.65f);
    void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x50);if(!t)return -246;
    // plugin-sdk CTaskComplexEnterCarAsDriver ctor 6402F0, size50.
    reinterpret_cast<void*(__thiscall*)(void*,void*)>(0x6402F0)(t,v);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(attacker),t,3,true);
    watchSessionPed(p);watchSessionPed(player());
    event("probe_bicycle_jack_task",probeBikePed,at<short>(p,0x22),"native_enter_as_driver=1 vehicle_ref="+std::to_string(probeBike)+" attacker_ref="+std::to_string(reference(attacker))+" no_injected_detach_or_damage=1");
    return probeBikePed;
}
