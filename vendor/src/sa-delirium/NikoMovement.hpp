#pragma once
// Native SA tasks keep intent, geometry/collision and the excluded mechanics.
// The original IV movement controller owns only its on-foot mover and pose.
static NmLocomotion* nikoMovement=nullptr;
static std::unique_ptr<Rig> nikoRig;
static int nikoRef=-1,nikoModel=-1,nikoTask=-1,nikoArea=-1;
static int nikoTaskMove=-1;
static unsigned nikoFrame=UINT32_MAX,nikoTrace=0;
static bool nikoActive=false;
static float nikoState[8]{};
static std::vector<float> nikoPose;
static bool nikoLastRejectWasPureJump=false,nikoJumpSuspended=false,nikoLastRejectWasGunAim=false,nikoAimSuspended=false;
static unsigned nikoJumpResumedAt=0;
static float nikoPreAimRatio=0.f;
static std::vector<T> nikoJumpLandLocal;
static void nikoClear(){
    if(nikoMovement)nm_locomotion_destroy(nikoMovement);
    nikoMovement=nullptr;nikoRig.reset();nikoRef=-1;nikoArea=-1;nikoActive=false;nikoFrame=UINT32_MAX;
    nikoLastRejectWasPureJump=false;nikoJumpSuspended=false;nikoLastRejectWasGunAim=false;nikoAimSuspended=false;nikoJumpResumedAt=0;nikoPreAimRatio=0.f;nikoJumpLandLocal.clear();
}
static bool nikoIsJumpOrAirTask(int task,unsigned char airFlags){
    if(task==TASK_SIMPLE_JUMP||task==TASK_COMPLEX_JUMP||
       task==TASK_COMPLEX_IN_AIR_AND_LAND||task==TASK_SIMPLE_IN_AIR||
       task==TASK_SIMPLE_LAND||task==TASK_COMPLEX_MOVE_BACK_AND_JUMP)return true;
    return (airFlags&6u)!=0&&task==TASK_SIMPLE_PLAYER_ON_FOOT;
}
static bool nikoEligible(void*p){
    nikoTask=-1;nikoTaskMove=-1;nikoLastRejectWasPureJump=false;nikoLastRejectWasGunAim=false;
    if(p)if(auto*m=taskManager(p)){
        auto*t=reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m);
        for(int depth=0;t&&depth<16;depth++){
            auto*v=at<void**>(t,0);nikoTask=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t);
            auto*sub=reinterpret_cast<void*(__thiscall*)(void*)>(v[2])(t);
            if(!sub){if(nikoTask==TASK_SIMPLE_GO_TO_POINT)nikoTaskMove=at<int>(t,8);break;}t=sub;
        }
    }
    if(p!=player()||!combatAllowed()||player_cover::owns(p)||!validPed(p,false,false,true)||combatLocomotionOwns(p)||
       agents.contains(reference(p))||pending.contains(reference(p))||at<float>(p,0x540)<=0)return false;
    if(auto*pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);!pad||at<short>(pad,0x10E))return false;
    if(const int w=combatWeapon(p);w>1&&(at<unsigned char>(p,0x46C)&0x10)){if(w>=22&&w<=38)nikoLastRejectWasGunAim=true;return false;}
    // Guns: SA's player aim lives in the secondary attack slot (CTaskManager
    // +0x14, CTaskSimpleUseGun from CTaskSimplePlayerOnFoot::ProcessPlayerWeapon)
    // and bIsAimingGun is never set for it (06/11 session: a shotgun shot with
    // this layer still turning CJ to the stick). Aiming strafes natively, as
    // IV turns the body to the camera while aiming (gta4-viewer main.rs:2519).
    if(const int w=combatWeapon(p);w>=22&&w<=38)if(auto*m=taskManager(p);m&&at<void*>(m,0x14)){nikoLastRejectWasGunAim=true;return false;}
    // CPed::IsPedInControl (0x5E3960) tests airborne/landing, not bIsStanding.
    // Support can disappear for a frame when stepping off a curb without a
    // jump/fall task: that must not cancel the original move/start/stop clock.
    const unsigned char airFlags=at<unsigned char>(p,0x46D);
    if(!nativeOnFootControl(airFlags)){nikoLastRejectWasPureJump=nikoIsJumpOrAirTask(nikoTask,airFlags);return false;}
    // Positive list: swimming, jumping, getting up, climbing, phone and vehicle
    // tasks cannot accidentally acquire this palette. Physical calls still work.
    if(nikoTask==TASK_SIMPLE_PLAYER_ON_FOOT||nikoTask==TASK_SIMPLE_GO_TO_POINT||
       nikoTask==TASK_SIMPLE_GO_TO_POINT_FINE||nikoTask==TASK_SIMPLE_STAND_STILL)return true;
    nikoLastRejectWasPureJump=nikoIsJumpOrAirTask(nikoTask,airFlags);
    return false;
}
static bool nikoPadActive(){
    if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return true;
    using Get=DWORD(WINAPI*)(DWORD,void*);
    static Get get=[]()->Get{
        for(const wchar_t*n:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"})
            if(HMODULE m=GetModuleHandleW(n))if(auto fn=GetProcAddress(m,"XInputGetState"))return reinterpret_cast<Get>(fn);
        return nullptr;
    }();
    if(!get)return false;
    for(DWORD i=0;i<4;i++){
        unsigned char s[16]{};
        if(get(i,s)!=ERROR_SUCCESS)continue;
        const WORD buttons=*reinterpret_cast<const WORD*>(s+4);
        const short lx=*reinterpret_cast<const short*>(s+8),ly=*reinterpret_cast<const short*>(s+10);
        if(buttons||std::abs(int(lx))>7849||std::abs(int(ly))>7849)return true;
    }
    return false;
}
static bool nikoWalkToggle=false,nikoWalkToggleWasDown=false;
static bool nikoKeyboardWalk(void*pad,float push){
    if(push>0.f&&push<=.65f)return true;
    static int walkHoldKey=-1,walkToggleKey=-1;
    if(walkHoldKey<0){
        const auto iniPath=(folder.parent_path()/"DeliriumSA.ini").string();
        walkHoldKey=GetPrivateProfileIntA("Movement","WalkKey",VK_MENU,iniPath.c_str());
        walkToggleKey=GetPrivateProfileIntA("Movement","WalkToggleKey",VK_CAPITAL,iniPath.c_str());
        nikoWalkToggle=GetPrivateProfileIntA("Movement","DefaultWalk",0,iniPath.c_str())!=0;
    }
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId()||!pad||at<short>(pad,0x10E)){
        nikoWalkToggleWasDown=false;return nikoWalkToggle;
    }
    const auto*newKeys=reinterpret_cast<const short*>(0xB73190+0x18);
    const auto keyDown=[&](int k){return k>0&&k<=255&&(newKeys[k]!=0||(GetAsyncKeyState(k)&0x8000)!=0);};
    const bool toggleDown=keyDown(walkToggleKey);
    if(toggleDown&&!nikoWalkToggleWasDown){
        nikoWalkToggle=!nikoWalkToggle;
        event("niko_walk_toggle",nikoRef,nikoModel,nikoWalkToggle?"walk=1":"walk=0");
    }
    nikoWalkToggleWasDown=toggleDown;
    const bool holdDown=keyDown(walkHoldKey)||(walkHoldKey==VK_MENU&&(keyDown(VK_LMENU)||keyDown(VK_RMENU)));
    return holdDown||nikoWalkToggle;
}
static void nikoUpdatePosition(void*p){
    if(p!=player())return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(!nikoEligible(p)){
        const bool pureJump=(nikoActive||nikoJumpSuspended)&&nikoLastRejectWasPureJump;
        const bool gunAim=nikoLastRejectWasGunAim;
        const bool keepAimSuspend=gunAim||(nikoAimSuspended&&!pureJump&&nikoMovement&&(nikoTask==TASK_SIMPLE_PLAYER_ON_FOOT||nikoTask==TASK_SIMPLE_STAND_STILL||nikoTask==TASK_SIMPLE_GO_TO_POINT));
        if(nikoActive){
            event("niko_movement_yield",reference(p),nikoModel,"native_task="+std::to_string(nikoTask)+" support_flags="+std::to_string(at<unsigned char>(p,0x46C))+" air_flags="+std::to_string(at<unsigned char>(p,0x46D))+" jump_suspend="+std::to_string(pureJump));
            if(nikoMovement)nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),pureJump?-2.f:-1.f);
            nikoJumpSuspended=pureJump;nikoAimSuspended=gunAim&&!pureJump;
            if(!pureJump&&!nikoAimSuspended){nikoJumpLandLocal.clear();nikoPreAimRatio=0.f;}
        }else{
            if(nikoJumpSuspended&&!pureJump){
                if(nikoMovement)nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-1.f);
                nikoJumpSuspended=false;
            }
            nikoAimSuspended=!nikoJumpSuspended&&keepAimSuspend;
            if(!nikoJumpSuspended&&!nikoAimSuspended){nikoJumpLandLocal.clear();nikoPreAimRatio=0.f;}
        }
        nikoActive=false;return;
    }
    const auto ini=(folder.parent_path()/"DeliriumSA.ini").string();
    static int enabled=GetPrivateProfileIntA("Movement","NikoOnFoot",0,ini.c_str());if(!enabled)return;
    const bool areaChanged=nikoArea>=0&&area()!=nikoArea;
    if(reference(p)!=nikoRef||at<short>(p,0x22)!=nikoModel||areaChanged){nikoClear();}
    if(nikoJumpSuspended){nikoJumpSuspended=false;nikoJumpResumedAt=gameTime();}
    const bool wasAimSuspended=nikoAimSuspended;nikoAimSuspended=false;
    if(!nikoMovement){
        try{
            nikoRig=std::make_unique<Rig>(p,sourceBuild);nikoRef=reference(p);nikoModel=at<short>(p,0x22);nikoArea=area();
            std::vector<uint32_t> ids(nikoRig->ids.begin(),nikoRig->ids.end()),parents(nikoRig->parents.begin(),nikoRig->parents.end());
            std::vector<float> bind(ids.size()*7);for(size_t i=0;i<ids.size();i++){const auto&t=nikoRig->bind[i];float*r=bind.data()+7*i;r[0]=t.p.x;r[1]=t.p.y;r[2]=t.p.z;r[3]=t.q.x;r[4]=t.q.y;r[5]=t.q.z;r[6]=t.q.w;}
            nikoMovement=nm_locomotion_create(combat,ids.data(),parents.data(),bind.data(),int(ids.size()),at<float>(p,0x558));
            if(!nikoMovement){ok(-1);nikoClear();return;}nikoPose.resize(bind.size());
            event("niko_movement_registered",nikoRef,nikoModel,"source=380c8cb7d49a native_collision=1");
        }catch(const std::exception&e){event("niko_rig_error",reference(p),0,e.what());nikoClear();return;}
    }
    float padPush=0.f;bool padWalkMode=false;
    if(nikoTask==TASK_SIMPLE_PLAYER_ON_FOOT||nikoTask==TASK_SIMPLE_STAND_STILL){
        if(auto*pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0)){
            const float x=reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad)/128.f,y=reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad)/128.f;
            padPush=std::min(1.f,std::sqrt(x*x+y*y));
            const bool padActive=nikoPadActive();
            padWalkMode=!padActive&&nikoKeyboardWalk(pad,padPush);
            if(padPush>0.f&&!padActive)padPush=1.f;
        }
    }
    if(wasAimSuspended&&nikoMovement){
        const V vel=at<V>(p,0x44);const float horiz=std::hypot(vel.x,vel.y)*50.f;
        auto*pd=at<void*>(p,0x480);const float nativeBlend=pd?std::clamp(at<float>(pd,0x14),0.f,3.f):0.f;
        const bool stickMoving=padPush>0.1f;
        if(stickMoving||horiz>=0.30f||nativeBlend>=0.25f||at<int>(p,0x534)>=4){
            float resumeRatio=std::max(nativeBlend,horiz>=2.0f?2.f:1.f);
            if(stickMoving){
                if(padWalkMode)resumeRatio=std::clamp(std::max(resumeRatio,1.f),0.85f,1.25f);
                else resumeRatio=std::clamp(std::max({resumeRatio,nikoPreAimRatio,1.85f}),1.5f,2.5f);
            }else{
                resumeRatio=std::clamp(std::max(resumeRatio,horiz>=1.5f?1.5f:1.f),0.85f,2.f);
            }
            nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-3.f-resumeRatio);
            nikoJumpResumedAt=gameTime();
        }else{
            nikoJumpLandLocal.clear();
        }
        nikoPreAimRatio=0.f;
    }
    const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    if(dt<=0)return;
    if(nikoFrame!=frame){
        // GoTo's ProcessPed calls SetRealMoveAnim for the player, which can
        // overwrite CPed::m_nMoveState with RUN. Read its authored task intent.
        const int move=nikoTaskMove>=0?nikoTaskMove:at<int>(p,0x534);auto*data=at<void*>(p,0x480);
        float wanted=move==7?3.f:move==6||move==5?2.f:move==4?1.f:0.f;
        if(nikoTask==TASK_SIMPLE_PLAYER_ON_FOOT){
            // Read GInput/SA's mapped controls, without changing CPad. Native
            // scripted go-to tasks retain their explicit movement intention.
            auto*pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
            if(pad){const float x=reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad)/128.f,y=reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad)/128.f;
                float push=std::min(1.f,std::sqrt(x*x+y*y));
                bool held=reinterpret_cast<bool(__thiscall*)(void*)>(0x5407A0)(pad),tap=reinterpret_cast<bool(__thiscall*)(void*)>(0x5407F0)(pad);
                if(tap)event("niko_run_tap",nikoRef,nikoModel,"held="+std::to_string(held));
                const bool padActive=nikoPadActive();
                const bool walkMode=!padActive&&nikoKeyboardWalk(pad,push);
                if(push>0.f&&!padActive){
                    push=1.f;
                    if(held){tap=true;}
                    else if(!walkMode){held=true;tap=false;}
                    else{held=false;tap=false;}
                }
                wanted=nm_locomotion_control(nikoMovement,push,tap,held,gameTime(),dt);
                if(wanted<0){nikoActive=false;return;}
            }else if(data)wanted=std::clamp(at<float>(data,0x14),0.f,3.f);
        }
        const bool crouch=(at<unsigned char>(p,0x46F)&4)!=0;
        const int stepped=nm_locomotion_step(combat,nikoMovement,wanted,at<float>(p,0x55C),nikoActive?nikoState[0]:at<float>(p,0x558),crouch,0,gameTime(),dt);
        if(!ok(stepped)){nikoActive=false;return;}
        nikoActive=nm_locomotion_pose(nikoMovement,nikoPose.data(),int(nikoRig->ids.size()),nikoState)==1;nikoFrame=frame;
        if(nikoActive)nikoPreAimRatio=std::max(nikoState[3],wanted>=1.f?wanted:0.f);
    }
    if(!nikoActive)return;
    const float yaw=nikoState[0],c=std::cos(yaw),s=std::sin(yaw),scale=combatMotionScale(dt,*reinterpret_cast<float*>(0xB7CB5C));
    at<float>(p,0x558)=yaw;reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(p,yaw);
    at<float>(p,0x550)=(c*nikoState[1]-s*nikoState[2])*scale;
    at<float>(p,0x554)=(s*nikoState[1]+c*nikoState[2])*scale;
    if(gameTime()-nikoTrace>=100){char clip[160]{};nm_locomotion_clip(nikoMovement,clip,sizeof(clip));nikoTrace=gameTime();
        event("niko_movement",nikoRef,nikoModel,"clip="+std::string(clip)+" ratio="+std::to_string(nikoState[3])+" phase="+std::to_string(nikoState[4])+" vx="+std::to_string(nikoState[1])+" vy="+std::to_string(nikoState[2])+" task="+std::to_string(nikoTask));}
}
// SA's own gait runs under Niko's. In the plain PLAYER group it is run_player and sprint_civi: X plays
// sprint_civi at once, and copying the arms from it pumped them as a sprint over a jog (07/11). Only a real
// weapon layer may give the arms: an armed group, or anything aiming or firing.
static bool nikoBaseLocomotionGroup(unsigned group){
    return group==0||(group>=54&&group<=56)||(group>=63&&group<=65)||group==69;
}
static bool nikoTwoHandedLocomotionGroup(unsigned group){
    return (group>=57&&group<=62)||(group>=66&&group<=68);
}
static bool nikoHasPartialOrCombatAnim(void*p){
    void*clump=at<void*>(p,0x18);void*assoc=clump?reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump):nullptr;
    for(int count=0;assoc&&count<24;count++){
        const unsigned group=at<unsigned short>(assoc,0xE);
        const unsigned flags=at<unsigned short>(assoc,0x2E);
        const float blend=at<float>(assoc,0x18);
        if(blend>.05f&&(!nikoBaseLocomotionGroup(group)&&!nikoTwoHandedLocomotionGroup(group)||(flags&0x10u)!=0))return true;
        assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
    }
    return false;
}
static bool nikoNativeRunningGait(void*p){
    void*clump=at<void*>(p,0x18);void*assoc=clump?reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump):nullptr;
    for(int count=0;assoc&&count<24;count++){
        const unsigned group=at<unsigned short>(assoc,0xE);
        const short animId=at<short>(assoc,0x2C);
        const unsigned flags=at<unsigned short>(assoc,0x2E);
        const float blend=at<float>(assoc,0x18);
        if(blend>.15f&&(flags&0x10u)==0&&group>=54&&group<=69&&(animId==1||animId==2))return true;
        assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
    }
    return false;
}
static int nikoDominantTwoHandedGroup(void*p,short*outAnimId=nullptr,float*outBlend=nullptr){
    void*clump=at<void*>(p,0x18);void*assoc=clump?reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump):nullptr;
    int bestGroup=-1;short bestAnim=-1;float bestBlend=0.f;
    for(int count=0;assoc&&count<24;count++){
        const unsigned group=at<unsigned short>(assoc,0xE);
        const short animId=at<short>(assoc,0x2C);
        const unsigned flags=at<unsigned short>(assoc,0x2E);
        const float blend=at<float>(assoc,0x18);
        if(blend>.05f&&(flags&0x10u)==0&&nikoTwoHandedLocomotionGroup(group)&&blend>bestBlend){
            bestGroup=int(group);bestAnim=animId;bestBlend=blend;
        }
        assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
    }
    if(outAnimId)*outAnimId=bestAnim;if(outBlend)*outBlend=bestBlend;return bestGroup;
}
static bool nikoNativeHoldsWeapon(void*p){
    void*clump=at<void*>(p,0x18);void*assoc=clump?reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump):nullptr;
    for(int count=0;assoc&&count<24;count++){
        const unsigned group=at<unsigned short>(assoc,0xE);
        const unsigned flags=at<unsigned short>(assoc,0x2E);
        if(at<float>(assoc,0x18)>.05f&&(!nikoBaseLocomotionGroup(group)||(flags&0x10u)!=0))return true;
        assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
    }
    return false;
}
static void nikoRender(void*p){
    if(p!=player()||reference(p)!=nikoRef||!nikoRig)return;
    if(!nikoActive){
        if((nikoJumpSuspended||nikoAimSuspended)&&matrix(p)){
            std::array<T,21> parts{};std::vector<T> native;
            if(nikoRig->incoming(p,parts,native)){
                const T root=inverse(transform(*matrix(p)));
                nikoJumpLandLocal.clear();nikoJumpLandLocal.reserve(native.size());
                for(const auto&t:native)nikoJumpLandLocal.push_back(mul(root,t));
            }
        }
        return;
    }
    if(!nikoEligible(p)||nikoFrame!=*reinterpret_cast<unsigned*>(0xB7CB4C))return;
    auto*h=hierarchy(p);if(!h||h->num!=int(nikoRig->ids.size()))return;
    const T root=transform(*matrix(p));std::vector<T> bones;for(size_t i=0;i<nikoRig->ids.size();i++){const float*r=nikoPose.data()+7*i;bones.push_back(mul(root,T{{r[3],r[4],r[5],r[6]},{r[0],r[1],r[2]}}));}
    if(nikoJumpResumedAt){
        const float blend=(gameTime()-nikoJumpResumedAt)/120.f;
        if(blend>=1.f||nikoJumpLandLocal.size()!=bones.size()){nikoJumpResumedAt=0;nikoJumpLandLocal.clear();}
        else{
            std::vector<T> from;from.reserve(bones.size());
            for(const auto&l:nikoJumpLandLocal)from.push_back(mul(root,l));
            bones=nikoRig->blendBones(from,bones,blend);
        }
    }
    // Preserve SA's weapon/aim upper layer until its own IV retarget is complete.
    // Arms cannot inherit locomotion rotations while holding a two-handed gun.
    const int weapon=combatWeapon(p);
    const float nikoSpeed=std::hypot(nikoState[1],nikoState[2]);
    const bool combatUpper=weapon>1&&nikoHasPartialOrCombatAnim(p);
    const bool runningGait=nikoNativeRunningGait(p);
    const bool pistol=weapon>=22&&weapon<=24&&at<void*>(p,0x4F4)&&(combatUpper||!runningGait);
    const bool armedUpper=weapon>1&&nikoNativeHoldsWeapon(p);
    static int calmRef=-1,calmGroup=-1,calmWeapon=-1;static std::vector<T> calmLocal;
    if(calmRef!=nikoRef){calmRef=nikoRef;calmGroup=-1;calmWeapon=-1;calmLocal.clear();}
    if(pistol||armedUpper){
        std::array<T,21> parts{};std::vector<T> native;
        if(nikoRig->incoming(p,parts,native)){
            short twoHandAnim=-1;float twoHandBlend=0.f;
            const int twoHandGroup=armedUpper&&!combatUpper?nikoDominantTwoHandedGroup(p,&twoHandAnim,&twoHandBlend):-1;
            const bool calmTwoHand=twoHandGroup>=0&&(twoHandAnim==0||twoHandAnim==3||twoHandAnim==4||twoHandAnim==5)&&twoHandBlend>=.7f&&!runningGait;
            if(calmTwoHand&&native.size()==bones.size()){
                calmLocal.resize(bones.size());calmGroup=twoHandGroup;calmWeapon=weapon;
                for(size_t i=0;i<bones.size();i++){const int parent=nikoRig->parents[i];calmLocal[i]=(parent>=0&&parent<int(i))?mul(inverse(native[parent]),native[i]):T{};}
            }
            const bool useCalmCache=twoHandGroup>=0&&!combatUpper&&runningGait&&nikoSpeed<2.4f&&calmGroup==twoHandGroup&&calmWeapon==weapon&&calmLocal.size()==bones.size();
            for(size_t i=0;i<bones.size();i++){
                const int id=nikoRig->ids[i],parent=nikoRig->parents[i];
                // Pistols can use SA's default locomotion group. Preserve
                // the weapon hand and its arm even in that group.
                if(parent>=0&&parent<int(i)&&((armedUpper&&id>=3&&id<41)||(pistol&&id>=21&&id<=26)))
                    bones[i]=mul(bones[parent],useCalmCache?calmLocal[i]:mul(inverse(native[parent]),native[i]));
                else if(parent>=0&&parent<int(i)&&(id==301||id==302))
                    bones[i]=mul(bones[parent],nikoRig->bindLocal[i]);
            }
        }
    }
    const auto palette=nikoRig->renderPalette(h->matrices,bones);std::copy(palette.begin(),palette.end(),h->matrices);
}
