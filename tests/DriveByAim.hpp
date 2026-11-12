#pragma once

// Included inside drive_by_controls. CCamera supplies the ordinary driving
// pose every frame; the aim pose is applied before native view derivation.
static constexpr uintptr_t mainCamera=0xB6F028;
static void(__thiscall* nativeDerivedCamera)(void*,bool,bool)=nullptr;
static void(__thiscall* nativeDriveBy)(void*)=nullptr;
static void(__thiscall* nativeCarBullet)(void*,V,V,void*,void*)=nullptr;
static void(__cdecl* nativeCrosshair)()=nullptr;
static int animationSide=0;

struct AimView {
    int vehicle=-1;
    unsigned frame=0;
    float yaw=0, pitch=0, blend=0;
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
    if(!cam)return;
    at<V>(cam,0x190)=nativeCamPose.front;
    at<V>(cam,0x19C)=nativeCamPose.source;
    at<V>(cam,0x1B4)=nativeCamPose.up;
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

static void applyAimView(void*camera){
    void*car=armedDriverVehicle(reinterpret_cast<void*>(primaryPad));
    const int ref=vehicleRef(car);
    if(ref<0||!matrix(car)||at<void*>(camera,0x958)!=car||at<bool>(camera,0x3D)){
        view={};
        return;
    }
    const bool aiming=aimHeld();
    if(view.vehicle!=ref)view={};
    if(!aiming&&view.blend<=0.f)return;
    auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    if(!col)return;
    auto&pose=at<M>(camera,0x974);
    const float stockYaw=std::atan2(pose.up.y,pose.up.x);
    const float stockPitch=std::asin(std::clamp(pose.up.z,-1.f,1.f));
    if(view.blend<=0.f){
        view.vehicle=ref;
        view.yaw=stockYaw;
        view.pitch=std::clamp(stockPitch,-1.18f,1.0f);
        view.lookRate={};
    }
    const float dt=std::clamp(*reinterpret_cast<float*>(0xB7CB5C)*.02f,0.f,.05f);
    if(aiming){
        const V desired=lookStick();
        view.lookRate=view.lookRate+(desired-view.lookRate)*(1.f-std::exp(-dt*24.f));
        view.yaw=std::remainder(view.yaw-view.lookRate.x*dt*3.1f,6.28318531f);
        view.pitch=std::clamp(view.pitch+view.lookRate.y*dt*2.05f,-1.18f,1.0f);
    }
    view.blend=std::clamp(view.blend+(aiming?dt/.16f:-dt/.22f),0.f,1.f);
    if(view.blend<=0.f){view={};return;}
    const float blend=view.blend*view.blend*(3.f-2.f*view.blend);
    const float yaw=stockYaw+std::remainder(view.yaw-stockYaw,6.28318531f)*blend;
    const float pitch=stockPitch+(view.pitch-stockPitch)*blend;
    const V front{std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),std::sin(pitch)};
    // RenderWare builds its camera basis in left, up, forward order.
    const V left=normalized(cross({0,0,1},front));
    const V up=cross(front,left);
    const float radius=std::clamp((col->box.max.y-col->box.min.y)*.79f,3.5f,7.2f);
    const V pivot=position(car)+V{0,0,std::clamp(col->box.max.z,.45f,2.9f)+.45f};
    const V shoulder=pivot-left*.65f;
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

static void __fastcall deriveCamera(void*camera,void*,bool mirror,bool oriented){
    if(reinterpret_cast<uintptr_t>(camera)==mainCamera&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==0x52C988){
        applyAimView(camera);
        player_cover::applyCamera(camera);
    }
    nativeDerivedCamera(camera,mirror,oriented);
}

static void __fastcall driveBy(void*car,void*){
    const bool aiming=aimHeld();
    if(car!=armedDriverVehicle(reinterpret_cast<void*>(primaryPad))||
       (!aiming&&!(connected&&(input.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)))){
        nativeDriveBy(car);
        return;
    }
    void*cam=::activeCamera();
    if(!cam||!matrix(car)){nativeDriveBy(car);return;}
    const V direction=hasAimView(car)?view.forward:
        at<M>(reinterpret_cast<void*>(mainCamera),0x974).up;
    const float side=dot(direction,matrix(car)->right);
    const int savedSide=animationSide;
    const bool savedLeft=at<bool>(cam,0x8),savedRight=at<bool>(cam,0x9);
    animationSide=side>.15f?1:side<-.15f?-1:savedRight?1:-1;
    at<bool>(cam,0x8)=animationSide<0;
    at<bool>(cam,0x9)=animationSide>0;
    nativeDriveBy(car);
    at<bool>(cam,0x8)=savedLeft;
    at<bool>(cam,0x9)=savedRight;
    animationSide=savedSide;
}

static void __fastcall carBullet(void*weapon,void*,V start,V end,void*car,void*owner){
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())==0x73F3EB&&owner==player()&&
       car==armedDriverVehicle(reinterpret_cast<void*>(primaryPad))&&aimHeld()&&hasAimView(car)){
        const float range=length(end-start);
        if(std::isfinite(range)&&range>1.f){
            const float muzzleDepth=dot(start-view.source,view.forward);
            const V rayEnd=view.source+view.forward*std::max(1.f,muzzleDepth+range);
            ColPoint target{};
            const bool visible=traceDriveBy(view.source,rayEnd,car,target,true);
            const V desired=(visible?target.point:rayEnd)-start;
            const float distance=length(desired);
            if(distance>.01f){
                // The camera picks the point. The native bullet still travels
                // from the muzzle, so cover between it and that point works.
                end=start+desired*(std::min(range,distance+.12f)/distance);
                if(testInput){
                    const V toAim=normalized((visible?target.point:rayEnd)-view.source);
                    const V heading=normalized({view.forward.x,view.forward.y,0});
                    const V carForward=matrix(car)->up;
                    const V carHeading=normalized({carForward.x,carForward.y,0});
                    auto*rwCamera=at<void*>(reinterpret_cast<void*>(mainCamera),0x954);
                    auto*rwFrame=rwCamera?at<void*>(rwCamera,4):nullptr;
                    const M rendered=rwFrame?at<M>(rwFrame,0x10):M{};
                    event("probe_driveby_ray",vehicleRef(car),at<short>(car,0x22),
                          "aim_dot="+std::to_string(dot(toAim,view.forward))+
                          " heading_dot="+std::to_string(dot(heading,carHeading))+
                          " pitch="+std::to_string(view.forward.z)+
                          " speed_mps="+std::to_string(length(at<V>(car,0x44))*50.f)+
                          " render_dot="+std::to_string(dot(toAim,rendered.at))+
                          " render_up_z="+std::to_string(rendered.up.z)+
                          " camera_hit="+std::to_string(visible));
                }
            }
        }
    }
    nativeCarBullet(weapon,start,end,car,owner);
}

static void __cdecl crosshair(){
    nativeCrosshair();
    void*car=armedDriverVehicle(reinterpret_cast<void*>(primaryPad));
    if(!hasAimView(car)||!aimHeld())return;
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
