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
static unsigned nikoHandoffStarted=0;
static unsigned nikoSnapshotFrame=UINT32_MAX;
static std::vector<T> nikoHandoffPose;
static void nikoClear(){
    if(nikoMovement)nm_locomotion_destroy(nikoMovement);
    nikoMovement=nullptr;nikoRig.reset();nikoRef=-1;nikoArea=-1;nikoActive=false;nikoFrame=UINT32_MAX;
    nikoLastRejectWasPureJump=false;nikoJumpSuspended=false;nikoLastRejectWasGunAim=false;nikoAimSuspended=false;nikoHandoffStarted=0;nikoSnapshotFrame=UINT32_MAX;nikoHandoffPose.clear();
}
static void nikoYieldForStealth(){
    if(nikoMovement&&(nikoActive||nikoJumpSuspended||nikoAimSuspended))
        nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-1.f);
    nikoActive=false;
    nikoJumpSuspended=false;
    nikoAimSuspended=false;
    nikoHandoffStarted=0;
    nikoSnapshotFrame=UINT32_MAX;
    nikoFrame=UINT32_MAX;
    nikoHandoffPose.clear();
}
static void nikoSuspendForCover(void* p){
    if(!nikoMovement||!p||p!=player()||reference(p)!=nikoRef||!player_cover::owns(p))return;
    // The cover mover bypasses nikoUpdatePosition. Cancel an active free gait
    // here so its old heading and sprint cannot survive the whole cover task.
    if(nikoActive||nikoJumpSuspended)
        nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-1.f);
    nikoActive=false;nikoAimSuspended=true;nikoJumpSuspended=false;nikoFrame=UINT32_MAX;
}
static bool nikoIsJumpOrAirTask(int task,unsigned char airFlags){
    if(task==TASK_SIMPLE_JUMP||task==TASK_COMPLEX_JUMP||
       task==TASK_COMPLEX_IN_AIR_AND_LAND||task==TASK_SIMPLE_IN_AIR||
       task==TASK_SIMPLE_LAND||task==TASK_COMPLEX_MOVE_BACK_AND_JUMP)return true;
    return (airFlags&6u)!=0&&task==TASK_SIMPLE_PLAYER_ON_FOOT;
}
static bool nikoEligible(void*p){
    nikoTask=-1;nikoTaskMove=-1;nikoLastRejectWasPureJump=false;nikoLastRejectWasGunAim=false;
    if(p!=player()||!p||native_stealth::ownsPose(p))return false;
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
    // A jump with a gun still equipped belongs to the jump/land controller.
    const unsigned char airFlags=at<unsigned char>(p,0x46D);
    if(!nativeOnFootControl(airFlags)){nikoLastRejectWasPureJump=nikoIsJumpOrAirTask(nikoTask,airFlags);return false;}
    const int weapon=combatWeapon(p);
    const bool usesGunTask=(weapon>=22&&weapon<=38)||aimedMovementTool(weapon);
    if(weapon>1&&(at<unsigned char>(p,0x46C)&0x10)){nikoLastRejectWasGunAim=usesGunTask;return false;}
    // Guns: SA's player aim lives in the secondary attack slot (CTaskManager
    // +0x14, CTaskSimpleUseGun from CTaskSimplePlayerOnFoot::ProcessPlayerWeapon)
    // and bIsAimingGun is never set for it (06/11 session: a shotgun shot with
    // this layer still turning CJ to the stick). Aiming strafes natively, as
    // IV turns the body to the camera while aiming (gta4-viewer main.rs:2519).
    if(usesGunTask)if(auto*m=taskManager(p);m&&at<void*>(m,0x14)){
        void* intel=at<void*>(p,0x47C);
        nikoLastRejectWasGunAim=intel&&reinterpret_cast<void*(__thiscall*)(void*)>(0x600F70)(intel);
        return false;
    }
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
static float nikoAimResumeRatio(float wanted,float speed){
    if(!(wanted>0.f)||!std::isfinite(speed)||speed<.30f)return 0.f;
    return std::min(wanted,speed>=2.f?2.f:1.f);
}
static void nikoUpdatePosition(void*p){
    if(p!=player())return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(!nikoEligible(p)){
        const bool pureJump=(nikoActive||nikoJumpSuspended)&&nikoLastRejectWasPureJump;
        const bool gunAim=nikoLastRejectWasGunAim;
        if(nikoActive){
            event("niko_movement_yield",reference(p),nikoModel,"native_task="+std::to_string(nikoTask)+" support_flags="+std::to_string(at<unsigned char>(p,0x46C))+" air_flags="+std::to_string(at<unsigned char>(p,0x46D))+" jump_suspend="+std::to_string(pureJump));
            if(nikoMovement)nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),pureJump?-2.f:-1.f);
            nikoJumpSuspended=pureJump;nikoAimSuspended=gunAim&&!pureJump;
            if(!pureJump&&!nikoAimSuspended){nikoHandoffPose.clear();nikoSnapshotFrame=UINT32_MAX;}
        }else{
            if(nikoJumpSuspended&&!pureJump){
                if(nikoMovement)nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-1.f);
                nikoJumpSuspended=false;
            }
            nikoAimSuspended=!nikoJumpSuspended&&gunAim;
            if(!nikoJumpSuspended&&!nikoAimSuspended){nikoHandoffPose.clear();nikoSnapshotFrame=UINT32_MAX;}
        }
        nikoActive=false;return;
    }
    const auto ini=(folder.parent_path()/"DeliriumSA.ini").string();
    static int enabled=GetPrivateProfileIntA("Movement","NikoOnFoot",0,ini.c_str());if(!enabled)return;
    const bool areaChanged=nikoArea>=0&&area()!=nikoArea;
    if(reference(p)!=nikoRef||at<short>(p,0x22)!=nikoModel||areaChanged){nikoClear();}
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
        if(nikoJumpSuspended||nikoAimSuspended){
            // The SDK resumes a moving gait with -3 - ratio. Feed the measured
            // gait, limited by THIS frame's IV control decision (including
            // analog pressure), even after a long aim following a sprint.
            if(nikoAimSuspended){
                const V vel=at<V>(p,0x44);
                const float ratio=nikoAimResumeRatio(wanted,50.f*std::hypot(vel.x,vel.y));
                if(ratio>0.f)
                    nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-3.f-ratio);
            }
            nikoHandoffStarted=!nikoHandoffPose.empty()&&frame-nikoSnapshotFrame<=2u?gameTime():0;
            nikoAimSuspended=false;nikoJumpSuspended=false;
        }
        const bool crouch=(at<unsigned char>(p,0x46F)&4)!=0;
        const int stepped=nm_locomotion_step(combat,nikoMovement,wanted,at<float>(p,0x55C),nikoActive?nikoState[0]:at<float>(p,0x558),crouch,0,gameTime(),dt);
        if(!ok(stepped)){nikoActive=false;return;}
        nikoActive=nm_locomotion_pose(nikoMovement,nikoPose.data(),int(nikoRig->ids.size()),nikoState)==1;nikoFrame=frame;
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
// Record the published palette after the gun, feet and weapon layers have
// finished. Sampling in nikoRender() itself would capture SA's underlying
// pose instead of the armed locomotion that was visible on the last frame.
static void nikoRememberSuspendedPose(void* p){
    if(p!=player()||!p||!nikoRig||reference(p)!=nikoRef||nikoActive||
       (!nikoJumpSuspended&&!nikoAimSuspended)||!matrix(p)||
       agents.contains(nikoRef)||pending.contains(nikoRef))return;
    std::array<T,21> parts{};std::vector<T> bones;
    if(!nikoRig->incoming(p,parts,bones))return;
    const T invRoot=inverse(transform(*matrix(p)));
    nikoHandoffPose.resize(bones.size());
    for(size_t i=0;i<bones.size();i++)nikoHandoffPose[i]=mul(invRoot,bones[i]);
    nikoSnapshotFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);
}
// Blend against the finished target (including SA's armed upper body). Work
// in joint-local space to preserve offsets through a change of direction.
static void nikoBlendHandoff(const std::vector<int>& parents,const std::vector<T>& start,
                             const T& root,float blend,std::vector<T>& target){
    for(size_t i=target.size();i-->0;){
        const int p=parents[i];
        const T initial=p>=0?mul(inverse(start[p]),start[i]):start[i];
        const T final=mul(inverse(p>=0?target[p]:root),target[i]);
        target[i]=mix(initial,final,blend);
    }
    for(size_t i=0;i<target.size();i++)
        target[i]=mul(parents[i]>=0?target[parents[i]]:root,target[i]);
}
static bool nikoRender(void*p){
    if(p!=player()||!p||reference(p)!=nikoRef||!nikoRig||!nikoActive)return false;
    if(!nikoEligible(p)||nikoFrame!=*reinterpret_cast<unsigned*>(0xB7CB4C))return false;
    auto*h=hierarchy(p);if(!h||!h->matrices||h->num!=int(nikoRig->ids.size()))return false;
    const T root=transform(*matrix(p));std::vector<T> bones;for(size_t i=0;i<nikoRig->ids.size();i++){const float*r=nikoPose.data()+7*i;bones.push_back(mul(root,T{{r[3],r[4],r[5],r[6]},{r[0],r[1],r[2]}}));}
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
            // The SA hold supplies torso, shoulder and hand transforms so the
            // gun stays in both hands. Its locomotor head swing does not match
            // IV's stride. Keep the authored IV head orientation while the
            // head remains attached to SA's neck and the face follows it.
            const bool longGun=(weapon>=25&&weapon<=27)||(weapon>=29&&weapon<=31)||weapon==33||weapon==34;
            const float headBlend=longGun&&twoHandGroup>=0?std::clamp(nikoSpeed/.75f,0.f,1.f):0.f;
            for(size_t i=0;i<bones.size();i++){
                const int id=nikoRig->ids[i],parent=nikoRig->parents[i];
                // Pistols can use SA's default locomotion group. Preserve
                // the weapon hand and its arm even in that group.
                if(parent>=0&&parent<int(i)&&((armedUpper&&id>=3&&id<41)||(pistol&&id>=21&&id<=26))){
                    const Q gaitOrientation=bones[i].q;
                    bones[i]=mul(bones[parent],useCalmCache?calmLocal[i]:mul(inverse(native[parent]),native[i]));
                    if(id==saBone::head&&headBlend>0.f)bones[i].q=mix(bones[i].q,gaitOrientation,headBlend);
                }else if(parent>=0&&parent<int(i)&&(id==301||id==302))
                    bones[i]=mul(bones[parent],nikoRig->bindLocal[i]);
            }
        }
    }
    if(nikoHandoffStarted){
        const float blend=(gameTime()-nikoHandoffStarted)/120.f;
        if(blend>=1.f||nikoHandoffPose.size()!=bones.size()){nikoHandoffStarted=0;nikoHandoffPose.clear();}
        else nikoBlendHandoff(nikoRig->parents,nikoHandoffPose,root,blend,bones);
    }
    const auto palette=nikoRig->renderPalette(h->matrices,bones);std::copy(palette.begin(),palette.end(),h->matrices);
    return true;
}
