#pragma once
// Diagnostic only (GTASA_PASSIVE_TEST_WINDOW): does a physical spell leave the
// scripted tasks around it alone? A cop follows a criminal (05D9 TASK_GOTO_CHAR),
// the criminal flees the cop (05DB TASK_FLEE_CHAR); both sit in PRIMARY (slot 3).
// Mode 265 sets them up, 266 trips the running criminal onto the ragdoll,
// 267 trips him with SA's own fall instead (the control).
// GTASA_ANIM_TRACE then logs both peds' five primary slots (traceExtraRefs).
static int chaseCop=-1,chaseCriminal=-1;
static int chaseFixture(int mode){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -265;
    if(mode==265){
        for(int model:{280,28})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        const V here=position(player());
        auto create=[&](int type,int model,V at){ProbeScript c(0x009A);c.integer(type);c.integer(model);c.number(at.x);c.number(at.y);c.number(at.z);c.result();return c.run();};
        chaseCriminal=create(20,28,here+V{8,4,0});chaseCop=create(6,280,here+V{4,4,0});
        if(!pedFromRef(chaseCriminal)||!pedFromRef(chaseCop))return -266;
        ProbeScript flee(0x05DB);flee.integer(chaseCriminal);flee.integer(chaseCop);flee.number(100.f);flee.integer(-1);flee.run();
        ProbeScript follow(0x05D9);follow.integer(chaseCop);follow.integer(chaseCriminal);follow.integer(-1);follow.number(1.f);follow.run();
        traceExtraRefs={chaseCop,chaseCriminal};watchSessionPed(pedFromRef(chaseCop));watchSessionPed(pedFromRef(chaseCriminal));
        event("probe_chase_setup",chaseCriminal,at<short>(pedFromRef(chaseCriminal),0x22),"cop_ref="+std::to_string(chaseCop)+" flee=05DB follow=05D9");
        return chaseCriminal;
    }
    auto*p=pedFromRef(chaseCriminal);if(!p||!validPed(p))return -267;
    if(mode==267){
        // Control: SA's own CTaskComplexFallAndGetUp (0x678700, 0x18 bytes;
        // forward, 4 s down) in the event-response slot, as a native trip.
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x18);if(!t)return -268;
        reinterpret_cast<void*(__thiscall*)(void*,int,int)>(0x678700)(t,0,4000);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,1,true);
        event("probe_chase_trip",chaseCriminal,at<short>(p,0x22),"native_fall=1 speed_mps="+std::to_string(length(at<V>(p,0x44))*50.f));
        return chaseCriminal;
    }
    V way=at<V>(p,0x44);way.z=0;way=length(way)>.001f?normalized(way):matrix(p)->up;
    // A stumble while running: a forward shove on the shins, no damage event.
    pending[chaseCriminal]={way*30.f,way,gameTime(),12,true,Reaction::Shot,-1};
    event("probe_chase_trip",chaseCriminal,at<short>(p,0x22),"speed_mps="+std::to_string(length(at<V>(p,0x44))*50.f));
    return chaseCriminal;
}
