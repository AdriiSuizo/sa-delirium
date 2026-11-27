#pragma once

// Included inside drive_by_controls. CCamera supplies the ordinary driving
// pose every frame; the aim pose is applied before native view derivation.
static constexpr uintptr_t mainCamera=0xB6F028;
static void(__thiscall* nativeDriveBy)(void*)=nullptr;
static void(__thiscall* nativeCarBullet)(void*,V,V,void*,void*)=nullptr;
static void(__cdecl* nativeCrosshair)()=nullptr;
static int animationSide=0;

struct AimView {
    int vehicle=-1;
    unsigned frame=0;
    float yaw=0, pitch=0, blend=0, poseBlend=0, entryTime=.35f, exitTime=.35f;
    int handTag=24, seat=-2;
    V lookRate{}, source{}, forward{};
    bool ready=false;
};
static AimView view;

struct NativeCamPose {
    void* cam=nullptr;
    V front{}, source{}, up{};
};
static NativeCamPose nativeCamPose;

static void restoreNativeCamPose(){
    auto*cam=nativeCamPose.cam;
    if(cam){
        at<V>(cam,0x190)=nativeCamPose.front;
        at<V>(cam,0x19C)=nativeCamPose.source;
        at<V>(cam,0x1B4)=nativeCamPose.up;
    }
    nativeCamPose={};
}

static bool traceDriveBy(V start,V end,void*car,ColPoint&hit,bool characters){
    void*entity=nullptr;
    auto*&ignore=*reinterpret_cast<void**>(0xB7CD68);
    void*previous=ignore;
    ignore=car;
    const bool found=reinterpret_cast<bool(__cdecl*)(const V&,const V&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool)>(
        0x56BA00)(start,end,hit,entity,true,true,characters,true,false,false,false,characters);
    ignore=previous;
    return found;
}

static V lookStick(){
    const V stick{input.sThumbRX/32768.f,input.sThumbRY/32768.f,0};
    const float radius=length(stick);
    constexpr float deadzone=XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE/32768.f;
    if(radius<=deadzone)return {};
    const float response=std::clamp((radius-deadzone)/(1.f-deadzone),0.f,1.f);
    return stick*(response*response/radius);
}

static bool needsAimView();
static void applyAimView(void*camera){
    sample();
    auto*cj=player();
    if(!cj||!inVehicle(cj)){
        view={};
        ivTargetFov=0.f;ivFovBlend=0.f;
        return;
    }
    void*car=armedDriverVehicle(reinterpret_cast<void*>(primaryPad));
    const int ref=vehicleRef(car);
    if(ref<0||!matrix(car)||(at<void*>(camera,0x958)!=car&&at<void*>(camera,0x958)!=cj)||at<bool>(camera,0x3D)){
        view={};
        ivTargetFov=0.f;ivFovBlend=0.f;
        return;
    }
    const bool aiming=aimHeld()||needsAimView();
    if(view.vehicle!=ref)view={};
    if(!aiming&&view.blend<=0.f&&view.poseBlend<=0.f){
        ivTargetFov=0.f;ivFovBlend=0.f;
        return;
    }
    auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    if(!col)return;
    auto&pose=at<M>(camera,0x974);
    const float stockYaw=std::atan2(pose.up.y,pose.up.x);
    const float stockPitch=std::asin(std::clamp(pose.up.z,-1.f,1.f));
    if(view.blend<=0.f&&view.poseBlend<=0.f){
        view.vehicle=ref;
        view.yaw=stockYaw;
        view.pitch=std::clamp(stockPitch,-1.18f,1.0f);
        view.lookRate={};
    }
    int seat=-1;
    if(at<void*>(car,0x460)==cj)seat=0;
    else for(int i=0;i<3;++i)if(at<void*>(car,0x464+i*sizeof(void*))==cj){seat=i+1;break;}
    if(view.seat!=seat){
        view.seat=seat;
        using Setup=int(__cdecl*)(void*,void*,float*,float*,int*);
        if(auto module=GetModuleHandleW(L"IVDriverPose.asi")){
            if(auto setup=reinterpret_cast<Setup>(GetProcAddress(module,"IVVehicleAimSetup")))
                setup(cj,car,&view.entryTime,&view.exitTime,&view.handTag);
        }
    }
    const float dt=std::clamp(*reinterpret_cast<float*>(0xB7CB5C)*.02f,0.f,.05f);
    if(aiming){
        const V desired=lookStick();
        view.lookRate=view.lookRate+(desired-view.lookRate)*(1.f-std::exp(-dt*24.f));
        view.yaw=std::remainder(view.yaw-view.lookRate.x*dt*3.1f,6.28318531f);
        view.pitch=std::clamp(view.pitch+view.lookRate.y*dt*2.05f,-1.18f,1.0f);
        if(!testInput){
            const auto&m=mouse();
            const float fov=::activeCamera()?std::clamp(at<float>(::activeCamera(),0xB4),25.f,80.f):50.f;
            const float sens=fov*0.0125f*(*reinterpret_cast<const float*>(0xB6EC1C))*2.5f;
            view.yaw=std::remainder(view.yaw-m.x*sens,6.28318531f);
            view.pitch=std::clamp(view.pitch+m.y*sens,-1.18f,1.0f);
        }
    }
    view.blend=std::clamp(view.blend+(aiming?dt/.16f:-dt/.22f),0.f,1.f);
    view.poseBlend=std::clamp(view.poseBlend+(aiming?dt/view.entryTime:-dt/view.exitTime),0.f,1.f);
    if(view.blend<=0.f&&view.poseBlend<=0.f){view={};ivTargetFov=0.f;ivFovBlend=0.f;return;}
    const float blend=view.blend*view.blend*(3.f-2.f*view.blend);
    ivTargetFov=50.f;ivFovBlend=blend;
    const float yaw=stockYaw+std::remainder(view.yaw-stockYaw,6.28318531f)*blend;
    const float pitch=stockPitch+(view.pitch-stockPitch)*blend;
    const V front{std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),std::sin(pitch)};
    // RenderWare builds its camera basis in left, up, forward order.
    const V left=normalized(cross({0,0,1},front));
    const V up=cross(front,left);
    const int vtype=at<int>(car,0x594);
    const bool bike=vtype==9||vtype==10;
    const float radius=bike?std::clamp((col->box.max.y-col->box.min.y)*.88f,2.3f,3.6f):
        std::clamp((col->box.max.y-col->box.min.y)*.76f,3.4f,6.6f);
    const V pivot=position(car)+V{0,0,std::clamp(col->box.max.z,.45f,2.9f)+(bike?.40f:.45f)};
    const V shoulder=pivot-left*(bike?.35f:.65f);
    V source=pose.pos+(shoulder-front*radius-pose.pos)*blend;
    ColPoint wall{};
    if(traceDriveBy(pivot,source,car,wall,false)){
        const V offset=source-pivot;
        const float distance=length(offset);
        if(distance>.01f)source=pivot+offset*(std::clamp(length(wall.point-pivot)-.28f,0.f,distance)/distance);
    }
    pose.pos=source;
    pose.up=front;
    pose.right=left;
    pose.at=up;
    // The renderer also reads the active CCam directly. Keep it in sync
    // through rendering, then restore the native follow-camera inputs.
    if(auto*cam=::activeCamera()){
        nativeCamPose={cam,at<V>(cam,0x190),at<V>(cam,0x19C),at<V>(cam,0x1B4)};
        at<V>(cam,0x190)=front;
        at<V>(cam,0x19C)=source;
        at<V>(cam,0x1B4)=up;
    }
    view.frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    view.source=source;
    view.forward=front;
    view.ready=aiming;
}

static bool hasAimView(void*car){
    return car&&view.ready&&view.vehicle==vehicleRef(car)&&
        *reinterpret_cast<unsigned*>(0xB7CB4C)-view.frame<=2;
}

struct ThrowSequence{
    int vehicle=-1,actor=-1,weapon=-1;
    unsigned pressedAt=0,releasedAt=0;
    float duration=0.f,release=.5f,force=0.f;
    bool held=false,released=false,submitted=false,queued=false;
};
static ThrowSequence sequence;
static bool needsAimView(){return sequence.released||sequence.queued;}
static float vehicleThrowPhase(){
    if(!sequence.released||!(sequence.duration>0.f))return -1.f;
    return std::clamp(float(gameTime()-sequence.releasedAt)*.001f/sequence.duration,0.f,1.f);
}

static bool vehicleAimPose(void*ped,void*car,float* direction,float*weight){
    if(!ped||ped!=player()||!car||!direction||!weight||car!=armedDriverVehicle(reinterpret_cast<void*>(primaryPad))||
       view.vehicle!=vehicleRef(car)||view.poseBlend<=0||!finite(view.forward)||
       *reinterpret_cast<unsigned*>(0xB7CB4C)-view.frame>2)return false;
    direction[0]=view.forward.x;direction[1]=view.forward.y;direction[2]=view.forward.z;
    *weight=view.poseBlend;
    return true;
}

static void __fastcall driveBy(void*car,void*){
    // Do not let CVehicle::DoDriveByShootings start another shot or a stock
    // lateral animation for a driver under this controller. Passenger traffic
    // and mission-owned weapons keep the original routine.
    if(car&&car==armedDriverVehicle(reinterpret_cast<void*>(primaryPad))&&at<void*>(car,0x460)==player())return;
    nativeDriveBy(car);
}

struct VehicleRound {
    void* weapon;void* owner;void* vehicle;
    V muzzle,end;
    float force;
};
static thread_local VehicleRound* currentRound=nullptr;
static bool(__thiscall* nativeInstant)(void*,void*,V*,V*,void*,V*,V*,bool,bool)=nullptr;
static bool(__thiscall* nativeFirstPerson)(void*,void*)=nullptr;
static bool(__thiscall* nativeProjectile)(void*,void*,const V&,void*,const V*,float)=nullptr;
static bool ownRound(void*weapon,void*owner){return currentRound&&currentRound->weapon==weapon&&currentRound->owner==owner;}
static bool dispatchBullet(){
    const auto& s=*currentRound;
    nativeCarBullet(s.weapon,s.muzzle,s.end,s.vehicle,s.owner);
    if(at<void*>(s.owner,0x4F8))
        reinterpret_cast<bool(__thiscall*)(void*,int,bool)>(0x5DF340)(s.owner,250,true);
    return true;
}
static bool __fastcall vehicleInstant(void*weapon,void*,void* owner,V* start,V* muzzle,void* target,V* end,V* alt,bool a,bool b){
    return ownRound(weapon,owner)?dispatchBullet():nativeInstant(weapon,owner,start,muzzle,target,end,alt,a,b);
}
static bool __fastcall vehicleFirstPerson(void*weapon,void*,void* owner){
    return ownRound(weapon,owner)?dispatchBullet():nativeFirstPerson(weapon,owner);
}
static bool __fastcall vehicleProjectile(void*weapon,void*,void* owner,const V& origin,void* target,const V* point,float force){
    if(!ownRound(weapon,owner))return nativeProjectile(weapon,owner,origin,target,point,force);
    const auto& s=*currentRound;
    const int id=at<int>(weapon,0);
    if(!thrownVehicleWeapon(id))return false;
    // GetProjectileInfo supplies the effective records. Read the pointer
    // table from AddProjectile's own indexed load to honor pool relocation.
    const unsigned char instruction[]={0x8b,0x04,0x9d};
    auto* entry=reinterpret_cast<const unsigned char*>(0x738562);
    if(std::memcmp(entry,instruction,sizeof(instruction))!=0)return false;
    void** objects=nullptr;std::memcpy(&objects,entry+3,sizeof(objects));
    if(!objects)return false;
    auto getRecord=reinterpret_cast<void*(__cdecl*)(int)>(0x737BF0);
    int slot=-1;
    for(int i=0;i<32;i++){
        auto*r=getRecord(i);if(r&&!at<unsigned char>(r,0x10)){slot=i;break;}
    }
    if(slot<0)return false;
    auto add=reinterpret_cast<bool(__cdecl*)(void*,int,V,float,const V*,void*)>(0x737C80);
    if(!add(owner,id,s.muzzle,s.force,nullptr,nullptr))return false;
    void* obj=objects[slot];void* rec=getRecord(slot);
    if(obj&&rec&&at<unsigned char>(rec,0x10)&&at<int>(rec,0)==id&&at<void*>(rec,4)==owner){
        // SA chose the launch energy, upward arc and fuse. Orient that
        // impulse to the actual view and carry the vehicle's launch speed.
        const V aim=normalized(s.end-s.muzzle);
        V& v=at<V>(obj,0x44);
        if(finite(v)){
            const float speed=std::hypot(v.x,v.y);
            v=aim*speed+V{0,0,v.z}+at<V>(s.vehicle,0x44);
        }
    }
    return true;
}

static bool handMuzzle(void*ped,void*car,int id,void* info,V& muzzle){
    using RenderMuzzle=int(__cdecl*)(void*,void*,int,float*);
    if(auto module=GetModuleHandleW(L"IVDriverPose.asi")){
        const auto sample=reinterpret_cast<RenderMuzzle>(GetProcAddress(module,"IVVehicleShotMuzzle"));
        if(sample&&sample(ped,car,id,&muzzle.x)&&finite(muzzle)&&length(muzzle-position(ped))<2.5f)return true;
    }
    auto* h=hierarchy(ped);
    if(!h||!h->matrices||!h->nodes||h->num<=0||h->num>256)return false;
    for(int n=0;n<h->num;++n)if(h->nodes[n].id==view.handTag){
        const M& hand=h->matrices[n];
        const V look=view.forward, side=normalized(cross({0,0,1},look));
        const V offset=at<V>(info,0x24);
        muzzle=thrownVehicleWeapon(id)?hand.pos:hand.pos+look*offset.x+side*offset.y+cross(look,side)*offset.z;
        return finite(muzzle)&&length(muzzle-position(ped))<2.5f;
    }
    return false;
}

static void shootingTick(){
    auto* pad=reinterpret_cast<void*>(primaryPad);
    void*car=installed?armedDriverVehicle(pad):nullptr;
    if(!car){sequence={};return;}
    auto*ped=player();
    const int actor=reference(ped),vehicle=vehicleRef(car),id=combatWeapon(ped);
    if(sequence.vehicle!=vehicle||sequence.actor!=actor||sequence.weapon!=id){
        sequence={};sequence.vehicle=vehicle;sequence.actor=actor;sequence.weapon=id;
    }
    const bool held=triggerHeld();
    const unsigned now=gameTime();
    auto* weapon=static_cast<unsigned char*>(ped)+0x5A0+at<unsigned char>(ped,0x718)*0x1C;
    // SA does not update a handgun through its driver's SMG-only path.
    // Advance an expired native reload once if core weapon processing hasn't.
    if(at<int>(weapon,4)==2&&int(now-at<unsigned>(weapon,0x10))>0)
        reinterpret_cast<void(__thiscall*)(void*,void*)>(0x73DB40)(weapon,ped);
    if(thrownVehicleWeapon(id)){
        if(sequence.released&&vehicleThrowPhase()>=1.f)sequence.released=false;
        if(held&&!sequence.held&&!sequence.released)sequence.pressedAt=now;
        if(!held&&sequence.held&&!sequence.released&&hasAimView(car)){
            using Timing=int(__cdecl*)(void*,void*,float*,float*);
            auto module=GetModuleHandleW(L"IVDriverPose.asi");
            auto timing=module?reinterpret_cast<Timing>(GetProcAddress(module,"IVVehicleThrowTiming")):nullptr;
            float seconds=0.f,release=0.f;
            if(timing&&timing(ped,car,&seconds,&release)&&std::isfinite(seconds)&&seconds>.08f&&seconds<5.f&&release>0.f&&release<1.f){
                sequence.duration=seconds;sequence.release=release;sequence.releasedAt=now;
                sequence.force=std::clamp(float(now-sequence.pressedAt)/1000.f,.2f,1.f);
                sequence.released=true;sequence.submitted=false;
            }
        }
        sequence.held=held;
        if(!sequence.released||sequence.submitted||vehicleThrowPhase()<sequence.release)return;
        sequence.submitted=true;
    }else{
        // A one-frame click can arrive before the first camera pass. Keep it
        // only long enough to acquire a view, never across a seat/weapon change.
        if(held&&!sequence.held&&int(now-at<unsigned>(weapon,0x10))>=0){sequence.queued=true;sequence.pressedAt=now;}
        sequence.held=held;
        if(!held&&now-sequence.pressedAt>250)sequence.queued=false;
        if(!held&&!sequence.queued)return;
    }
    if(at<unsigned char>(pad,0x11B)||at<unsigned>(weapon,0xC)==0||!hasAimView(car)||
       int(now-at<unsigned>(weapon,0x10))<0)return;
    const int ws=at<int>(weapon,4);
    if(ws!=0&&ws!=1)return;
    const int skill=reinterpret_cast<unsigned char(__thiscall*)(void*)>(0x5E6580)(ped);
    auto*info=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(id,skill);
    if(!info)return;
    V muzzle{};
    if(!handMuzzle(ped,car,id,info,muzzle))return;
    const auto& profile=firearm::profile(id);
    const float range=profile.reach>1.f?profile.reach:at<float>(info,8);
    if(!std::isfinite(range)||range<=1.f)return;
    const float depth=dot(muzzle-view.source,view.forward);
    const V rayEnd=view.source+view.forward*std::max(1.f,range+depth);
    ColPoint pick{};
    const bool visible=traceDriveBy(view.source,rayEnd,car,pick,true);
    const V target=visible?pick.point:rayEnd;
    const V delta=target-muzzle;
    if(!finite(delta)||dot(delta,view.forward)<=.005f)return;
    V end=muzzle+normalized(delta)*std::min(range,length(delta)+.12f);
    VehicleRound shot{weapon,ped,car,muzzle,end,sequence.force};
    auto previous=currentRound;currentRound=&shot;
    const auto fireNative=reinterpret_cast<bool(__thiscall*)(void*,void*,V*,V*,void*,V*,V*)>(0x742300);
    const bool fired=fireNative(weapon,ped,&muzzle,&muzzle,nullptr,&end,nullptr);
    currentRound=previous;
    if(!fired)return;
    sequence.queued=false;
    if(at<int>(weapon,4)==1){
        const float nativeCycle=900.f*(at<float>(info,0x44)-at<float>(info,0x40));
        const unsigned interval=profile.interval?profile.interval:unsigned(std::clamp(nativeCycle,50.f,1500.f));
        at<unsigned>(weapon,0x10)=now+interval;
    }
    if(testInput){
        const V heading=normalized({view.forward.x,view.forward.y,0});
        const V carForward=matrix(car)->up;
        const V carHeading=normalized({carForward.x,carForward.y,0});
        auto*rwCamera=at<void*>(reinterpret_cast<void*>(mainCamera),0x954);
        auto*rwFrame=rwCamera?at<void*>(rwCamera,4):nullptr;
        const M rendered=rwFrame?at<M>(rwFrame,0x10):M{};
        const V toTarget=normalized(target-view.source);
        event("probe_driveby_ray",vehicle,at<short>(car,0x22),
            "aim_dot="+std::to_string(dot(toTarget,view.forward))+
            " heading_dot="+std::to_string(dot(heading,carHeading))+
            " pitch="+std::to_string(view.forward.z)+
            " speed_mps="+std::to_string(length(at<V>(car,0x44))*50.f)+
            " render_dot="+std::to_string(dot(toTarget,rendered.at))+
            " render_up_z="+std::to_string(rendered.up.z)+
            " camera_hit="+std::to_string(visible)+" weapon="+std::to_string(id));
    }
}

static void __fastcall carBullet(void*weapon,void*,V start,V end,void*car,void*owner){
    nativeCarBullet(weapon,start,end,car,owner);
}

static void __cdecl crosshair(){
    nativeCrosshair();
    void*car=armedDriverVehicle(reinterpret_cast<void*>(primaryPad));
    const bool vehicleAim=hasAimView(car)&&aimHeld();
    if(!vehicleAim)return;
    const float x=*reinterpret_cast<int*>(0xC17044)*.5f;
    const float y=*reinterpret_cast<int*>(0xC17048)*.5f;
    if(x<80.f||y<60.f)return;
    const float size=std::clamp(y/270.f,1.f,3.f);
    const auto rect=reinterpret_cast<void(__cdecl*)(const float*,const unsigned char*)>(0x727B60);
    const float outline[]={x-size*2,y-size*2,x+size*2,y+size*2};
    const float dot[]={x-size,y-size,x+size,y+size};
    const unsigned char dark[]={0,0,0,180},white[]={255,255,255,235};
    rect(outline,dark);
    rect(dot,white);
}
