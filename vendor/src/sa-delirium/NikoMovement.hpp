#pragma once
// Native SA tasks keep intent, geometry/collision and the excluded mechanics.
// The original IV movement controller owns only its on-foot mover and pose.
static NmLocomotion* nikoMovement=nullptr;
static std::unique_ptr<Rig> nikoRig;
static int nikoRef=-1,nikoModel=-1,nikoTask=-1;
static int nikoTaskMove=-1;
static unsigned nikoFrame=UINT32_MAX,nikoTrace=0;
static bool nikoActive=false;
static float nikoState[8]{};
static std::vector<float> nikoPose;
static void nikoClear(){
    if(nikoMovement)nm_locomotion_destroy(nikoMovement);
    nikoMovement=nullptr;nikoRig.reset();nikoRef=-1;nikoActive=false;nikoFrame=UINT32_MAX;
}
static bool nikoEligible(void*p){
    nikoTask=-1;nikoTaskMove=-1;
    if(p)if(auto*m=taskManager(p)){
        auto*t=reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m);
        for(int depth=0;t&&depth<16;depth++){
            auto*v=at<void**>(t,0);nikoTask=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t);
            auto*sub=reinterpret_cast<void*(__thiscall*)(void*)>(v[2])(t);
            if(!sub){if(nikoTask==TASK_SIMPLE_GO_TO_POINT)nikoTaskMove=at<int>(t,8);break;}t=sub;
        }
    }
    if(p!=player()||!combatAllowed()||!validPed(p,false,false,true)||combatLocomotionOwns(p)||
       agents.contains(reference(p))||pending.contains(reference(p))||at<float>(p,0x540)<=0)return false;
    // CPed::IsPedInControl (0x5E3960) tests airborne/landing, not bIsStanding.
    // Support can disappear for a frame when stepping off a curb without a
    // jump/fall task: that must not cancel the original move/start/stop clock.
    if(!nativeOnFootControl(at<unsigned char>(p,0x46D)))return false;
    if(combatWeapon(p)>1&&(at<unsigned char>(p,0x46C)&0x10))return false;
    // Positive list: swimming, jumping, getting up, climbing, phone and vehicle
    // tasks cannot accidentally acquire this palette. Physical calls still work.
    return nikoTask==TASK_SIMPLE_PLAYER_ON_FOOT||nikoTask==TASK_SIMPLE_GO_TO_POINT||
        nikoTask==TASK_SIMPLE_GO_TO_POINT_FINE||nikoTask==TASK_SIMPLE_STAND_STILL;
}
static void nikoUpdatePosition(void*p){
    if(p!=player())return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(!nikoEligible(p)){
        if(nikoActive){event("niko_movement_yield",reference(p),nikoModel,"native_task="+std::to_string(nikoTask)+" support_flags="+std::to_string(at<unsigned char>(p,0x46C))+" air_flags="+std::to_string(at<unsigned char>(p,0x46D)));nm_locomotion_step(combat,nikoMovement,0,0,0,0,0,gameTime(),-1);}
        nikoActive=false;return;
    }
    const auto ini=(folder.parent_path()/"DeliriumSA.ini").string();
    static int enabled=GetPrivateProfileIntA("Movement","NikoOnFoot",0,ini.c_str());if(!enabled)return;
    if(reference(p)!=nikoRef||at<short>(p,0x22)!=nikoModel)nikoClear();
    if(!nikoMovement){
        try{
            nikoRig=std::make_unique<Rig>(p,sourceBuild);nikoRef=reference(p);nikoModel=at<short>(p,0x22);
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
                const bool held=reinterpret_cast<bool(__thiscall*)(void*)>(0x5407A0)(pad),tap=reinterpret_cast<bool(__thiscall*)(void*)>(0x5407F0)(pad);
                wanted=nm_locomotion_control(nikoMovement,std::min(1.f,std::sqrt(x*x+y*y)),tap,held,gameTime(),dt);
                if(wanted<0){nikoActive=false;return;}
            }else if(data)wanted=std::clamp(at<float>(data,0x14),0.f,3.f);
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
static void nikoRender(void*p){
    if(!nikoActive||reference(p)!=nikoRef||!nikoRig||!nikoEligible(p)||nikoFrame!=*reinterpret_cast<unsigned*>(0xB7CB4C))return;
    auto*h=hierarchy(p);if(!h||h->num!=int(nikoRig->ids.size()))return;
    const T root=transform(*matrix(p));std::vector<T> bones;for(size_t i=0;i<nikoRig->ids.size();i++){const float*r=nikoPose.data()+7*i;bones.push_back(mul(root,T{{r[3],r[4],r[5],r[6]},{r[0],r[1],r[2]}}));}
    // Preserve SA's weapon/aim upper layer until its own IV retarget is complete.
    // Arms cannot inherit locomotion rotations while holding a two-handed gun.
    if(combatWeapon(p)>1){std::array<T,21> parts{};std::vector<T> native;if(nikoRig->incoming(p,parts,native)){
        for(size_t i=0;i<bones.size();i++)if(nikoRig->ids[i]>=3&&nikoRig->ids[i]<41){const int par=nikoRig->parents[i];if(par>=0&&par<int(i))bones[i]=mul(bones[par],mul(inverse(native[par]),native[i]));}
    }}
    const auto palette=nikoRig->renderPalette(h->matrices,bones);std::copy(palette.begin(),palette.end(),h->matrices);
}
