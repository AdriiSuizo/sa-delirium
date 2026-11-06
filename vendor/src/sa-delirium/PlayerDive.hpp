#pragma once
// Requested by the user (06/11), not an IV move: Circle while CJ is in the air
// from a jump makes him dive head-first as a physical body. It reuses IV's
// jump roll from a road vehicle (catchFall in the air, roll and pedal on the
// ground, original 2..30 s finish) with his own airborne arc, a short
// forward lunge and a forward pitch; contacts with peds and cars are the
// normal physical ones (requestPeerBump knocks standing peds).
static bool diveCircleHeld=false;
static int playerLeafTask(void*p){
    int task=-1;
    if(auto*m=taskManager(p)){
        auto*t=reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m);
        for(int depth=0;t&&depth<16;depth++){
            auto*v=at<void**>(t,0);task=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t);
            auto*sub=reinterpret_cast<void*(__thiscall*)(void*)>(v[2])(t);if(!sub)break;t=sub;
        }
    }
    return task;
}
// One line per jump: SA's own launch multiplies by 10 with the Mega Jump cheat
// (CCheat::m_aCheatsActive[60] at 0x96916C), which the trainer can toggle.
static void auditPlayerJump(void*p){
    static int last=-1;static unsigned since=0;static float startZ=0,peakVz=0,peakRise=0,peakH=0;
    const int task=p&&validPed(p,false,false,true)&&!inVehicle(p)?playerLeafTask(p):-1;
    if(task==210&&last!=210&&!since){since=gameTime();startZ=position(p).z;peakVz=peakRise=peakH=0;}
    if(since){
        {const V v=at<V>(p,0x44)*50.f;peakVz=std::max(peakVz,v.z);peakH=std::max(peakH,std::sqrt(v.x*v.x+v.y*v.y));}peakRise=std::max(peakRise,position(p).z-startZ);
        if((task!=210&&task!=241)||gameTime()-since>3000){
            event("player_jump_audit",reference(p),at<short>(p,0x22),"mega_jump="+std::to_string(*reinterpret_cast<unsigned char*>(0x96916C))+
                " gravity="+std::to_string(*reinterpret_cast<float*>(0x863984))+" mass="+std::to_string(at<float>(p,0x8C))+
                " peak_vz_mps="+std::to_string(peakVz)+" peak_horizontal_mps="+std::to_string(peakH)+" rise_m="+std::to_string(peakRise));
            since=0;
        }
    }
    last=task;
}
// The physical Circle (XInput B) of a connected pad. With Adrii's GInput layout
// CPad::GetMeleeAttack(true) also answered to Square (jump), so a second Square
// in the air dived (06/11). Keyboard/mouse keeps SA's melee attack.
static int padCircle(){
    using Get=DWORD(WINAPI*)(DWORD,void*);static Get get=nullptr;
    if(!get)for(const wchar_t*n:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"})if(HMODULE m=GetModuleHandleW(n)){get=reinterpret_cast<Get>(GetProcAddress(m,"XInputGetState"));if(get)break;}
    if(!get)return -1;bool connected=false;
    for(DWORD i=0;i<4;i++){unsigned char state[16]{};if(get(i,state)!=ERROR_SUCCESS)continue;connected=true;if(*reinterpret_cast<WORD*>(state+4)&0x2000)return 1;}
    return connected?0:-1;
}
// Adrii (06/11): CJ "keeps repeating short animations" after getting up. For
// 5 s after each physical spell, one line per change of SA's leaf task and its
// weighted clips (id/group/blend), to name what repeats.
static void auditPlayerAfterPhysics(void*p){
    static bool was=false;static unsigned until=0;static std::string last;
    const bool now=agents.contains(reference(p))||pending.contains(reference(p));
    if(was&&!now){until=gameTime()+5000;last.clear();}was=now;
    if(now||!until||gameTime()>until||!validPed(p,false,false,true))return;
    std::string clips;auto*clump=at<void*>(p,0x18);
    void*assoc=clump?reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump):nullptr;
    for(int count=0;assoc&&count<32;count++){
        if(at<float>(assoc,0x18)>.1f)clips+=" "+std::to_string(at<unsigned short>(assoc,0xE))+"/"+std::to_string(at<short>(assoc,0x2C));
        assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
    }
    const std::string row="task="+std::to_string(playerLeafTask(p))+" niko="+std::to_string(nikoActive)+" clips="+clips;
    if(row!=last){last=row;event("player_after_physics",reference(p),at<short>(p,0x22),row+" ms_left="+std::to_string(until-gameTime()));}
}
static void tickPlayerDive(){
    void*p=player();if(p){auditPlayerJump(p);auditPlayerAfterPhysics(p);}auto*pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
    // CPad::GetMeleeAttack(true) only without a pad (it respects disabled
    // controls; a pad dive still needs SA's own jump/in-air task below).
    const int circle=padCircle();
    const bool held=p&&pad&&(circle<0?reinterpret_cast<unsigned char(__thiscall*)(void*,bool)>(0x540340)(pad,true)!=0:circle==1);
    const bool pressed=held&&!diveCircleHeld;diveCircleHeld=held;
    if(!pressed||!recoveryAllowed()||!playerDives||!validPed(p,false,false,true)||inVehicle(p)||
       agents.contains(reference(p))||pending.contains(reference(p))||at<float>(p,0x540)<=0)return;
    if(!(at<unsigned char>(p,0x46D)&2))return; // CPed::bIsInTheAir
    const int task=playerLeafTask(p);
    if(task!=210&&task!=241)return; // TASK_SIMPLE_JUMP / TASK_SIMPLE_IN_AIR (not climbing or falling hurt)
    V forward=matrix(p)->up;forward.z=0;forward=normalized(forward);
    // SA flies CJ under its own gravity (0x863984 per 1/50 s step, 20 m/s^2 by
    // default); the ragdoll falls at 9.81. Scaling the whole velocity by
    // sqrt(9.81/g_sa) keeps SA's arc (same apex, same range) instead of
    // doubling its height and length.
    const float gSa=*reinterpret_cast<float*>(0x863984)*2500.f;
    const float arc=gSa>1.f?std::sqrt(9.81f/gSa):1.f;
    const V velocity=at<V>(p,0x44)*(50.f*arc)+forward*diveLungeMps+V{0,0,diveLiftMps};
    Request r;r.kind=Reaction::Dive;r.time=gameTime();r.direction=forward;r.playerVehicle=true;
    r.velocityOverride=true;r.velocity=velocity*.02f;
    pending[reference(p)]=r;
    event("player_dive_requested",reference(p),at<short>(p,0x22),"task="+std::to_string(task)+" button="+(circle<0?"melee_attack":"pad_circle")+" speed_mps="+std::to_string(length(velocity))+" lunge_mps="+std::to_string(diveLungeMps));
}
// Forward pitch as a pure couple (no net push): upper spine forward, feet back.
// On the head alone it whipped the neck and threw the arms.
static void startDivePitch(Active&a,V forward){
    const V j=forward*diveCoupleNs;const V half=j*-.5f;
    ok(nm_agent_apply_impulse(a.agent,10,&a.current[10].p.x,&j.x));
    ok(nm_agent_apply_impulse(a.agent,3,&a.current[3].p.x,&half.x));
    ok(nm_agent_apply_impulse(a.agent,6,&a.current[6].p.x,&half.x));
}
