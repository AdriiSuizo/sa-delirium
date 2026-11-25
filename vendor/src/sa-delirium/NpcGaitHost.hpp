#pragma once

// The native leaf task and evaluated association must both describe locomotion.
// A persistent WALK below an arrest, phone call or vehicle animation is common.
static bool npcHasGroundTask(void* ped,void*& leaf,int& task){
    auto* manager=taskManager(ped);
    if(!manager||at<void*>(manager,0x14)||at<void*>(manager,0x18))return false;
    using Leaf=void*(__thiscall*)(void*);
    leaf=reinterpret_cast<Leaf>(0x6819D0)(manager);
    if(!leaf)return false;
    using TaskType=int(__thiscall*)(void*);
    task=reinterpret_cast<TaskType>(at<void**>(leaf,0)[4])(leaf);
    // CTaskSimplePause, AchieveHeading and Turn180 are used at crossings.
    return task==TASK_SIMPLE_GO_TO_POINT||task==TASK_SIMPLE_GO_TO_POINT_FINE||
           task==TASK_SIMPLE_STAND_STILL||task==202||task==902||task==228;
}

static bool npcHasGroundState(void* ped){
    const int state=at<int>(ped,0x530);
    // ePedState: idling/looking, wander/seek/flee/pursue/follow (0..11),
    // PAUSE, AI_CONTROL, SEEK_CAR/BOAT, FOLLOW_ROUTE, INVESTIGATE_EVENT, JOG.
    return (state>=0&&state<=11)||state==15||(state>=23&&state<=26)||state==30||state==35;
}

static bool npcCanUseGait(void* ped,int ref,bool combatPose,void*& leaf,int& task){
    const auto physical=agents.find(ref);
    // A queued impact still needs the visible animated pose as its input.
    // Once admitted, the physical/recovery owner alone publishes the pose.
    return !(combatPose||(physical!=agents.end()&&!physical->second->preview)||injuredMoving(ref)||
       !playing()||cutscene()||(protectMissions&&authoredMissionProtected())||
       !matrix(ped)||!at<void*>(ped,0x18)||at<void*>(ped,0x480)||!player()||
       at<unsigned char>(ped,0x2F)!=area()||at<void*>(ped,0xFC)||at<float>(ped,0x540)<=0.f||
       (!allowScripted&&at<unsigned char>(ped,0x484)!=1)||inVehicle(ped)||
       !nativeOnFootControl(at<unsigned char>(ped,0x46D))||(at<unsigned char>(ped,0x46F)&4u)||
       (at<unsigned char>(ped,0x46C)&0x10u)||!npcHasGroundState(ped)||!npcHasGroundTask(ped,leaf,task));
}
static void drawNpcGait(void* ped,int ref,bool combatPose){
    void* leaf=nullptr;int task=-1;
    if(!npcCanUseGait(ped,ref,combatPose,leaf,task)){
        npc_gait::release(ref);
        return;
    }
    const int model=at<unsigned short>(ped,0x22);
    // Use SA's model lookup so a limit adjuster can relocate the model table.
    using ModelInfoById=void*(__cdecl*)(int);
    auto* info=model<20000?reinterpret_cast<ModelInfoById>(0x403DA0)(model):nullptr;
    if(info){
        using ModelType=unsigned char(__thiscall*)(void*);
        if(reinterpret_cast<ModelType>(at<void**>(info,0)[4])(info)!=7){
            npc_gait::release(ref);
            return;
        }
        const int group=at<int>(info,0x24);
        const char* profile=npc_gait::ResolveProfile(model,group,at<int>(info,0x28),at<int>(info,0x2C),at<unsigned>(info,4));
        // A new actor starts at zero blend. draw owns this cache even on its
        // first frame, before the overlay has changed any visible joint.
        if(profile){
            npc_gait::draw(ped,ref,profile,npc_gait::IsJoggerGroup(group),group);
            return;
        }
    }
    npc_gait::release(ref);
}

static void npcUpdateStride(void* ped){
    using namespace npc_gait;
    const int ref=reference(ped);
    auto it=actors.find(ref);
    if(it==actors.end())return;
    auto& a=it->second;
    void* leaf=nullptr;int task=-1;
    if(!a.mover||a.ped!=ped||a.clump!=at<void*>(ped,0x18)||a.model!=at<unsigned short>(ped,0x22)||
       gameTime()-a.lastSeen>150||pending.contains(ref)||agents.contains(ref)||
       !npcCanUseGait(ped,ref,false,leaf,task)||
       (task!=TASK_SIMPLE_GO_TO_POINT&&task!=TASK_SIMPLE_GO_TO_POINT_FINE)){
        a.drive={};return;
    }
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(a.driveFrame==frame)return;
    if(frame-a.driveFrame>1)a.drive={};
    a.driveFrame=frame;
    NativeMotion motion;
    if(!identify(a.clump,motion,a.modelGroup)||motion.partial){a.drive={};return;}
    float weight=0,known=0;
    int gait=Walk;
    for(int i=0;i<CycleCount;i++){
        known+=motion.layers[i].weight;
        if(i<=Sprint&&motion.layers[i].weight>weight){weight=motion.layers[i].weight;gait=i;}
    }
    if(known+.01f<motion.baseWeight){a.drive={};return;}
    const float dx=at<float>(ped,0x550),dy=at<float>(ped,0x554);
    const float requestedSpeed=50.f*std::hypot(dx,dy);
    const bool stop=npc_stride::stopIntent(a.drive,motion.layers[Idle].weight,motion.layers[WalkStart].weight,
                                            motion.layers[RunStopL].weight+motion.layers[RunStopR].weight);
    if(!weight&&motion.layers[WalkStart].weight<.01f&&!stop){a.drive={};return;}
    const float yaw=at<float>(ped,0x558),dest=at<float>(ped,0x55C);
    if(!std::isfinite(dest)||(-std::sin(yaw)*dx+std::cos(yaw)*dy)<-.0001f){a.drive={};return;}
    if(a.jogger&&gait==Walk)gait=Run;
    const V to=at<V>(leaf,0xC)-position(ped);
    const int curve=WalkTurnL+2*gait+(std::remainder(dest-yaw,6.2831853f)<0);
    const float nativeStep=*reinterpret_cast<float*>(0xB7CB5C);
    const float dt=combatSimulationSeconds(nativeStep);
    const V actual=at<V>(ped,0x44);
    const auto out=npc_stride::advance(a.drive,*a.mover,{gait,stop,a.movementPhase>=.5f,dt,yaw,dest,
        requestedSpeed,50.f*std::hypot(actual.x,actual.y),std::hypot(to.x,to.y),a.legScale,(*a.set)[curve].turnRate});
    if(!out.applied)return;
    const float travel=out.speed*combatMotionScale(dt,nativeStep);
    at<float>(ped,0x558)=out.yaw;
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(ped,out.yaw);
    at<float>(ped,0x550)=-std::sin(out.yaw)*travel;
    at<float>(ped,0x554)=std::cos(out.yaw)*travel;
}
