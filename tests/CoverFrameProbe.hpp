#pragma once

namespace player_cover {
static void traceFrame(void* ped){
    if(!GetEnvironmentVariableA("GTASA_COVER_FRAME_TRACE",nullptr,0)||!matrix(ped))return;
    if(!owns(ped)){
        auto found=police_cover::officers.find(reference(ped));
        if(found==police_cover::officers.end())return;
        const auto& officer=found->second;
        auto* h=hierarchy(ped);
        if(!h||!h->nodes||!h->matrices)return;
        V head{},hand{};
        for(int n=0;n<h->num;++n){
            if(h->nodes[n].id==saBone::head)head=h->matrices[n].pos;
            if(h->nodes[n].id==24)hand=h->matrices[n].pos;
        }
        const auto xyz=[](V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
        event("police_cover_frame",found->first,at<short>(ped,0x22),
            "frame="+std::to_string(*reinterpret_cast<unsigned*>(0xB7CB4C))+" phase="+std::to_string(int(officer.phase))+
            " elapsed_ms="+std::to_string(gameTime()-officer.since)+" intro_s="+std::to_string(officer.exposeSeconds)+
            " outro_s="+std::to_string(officer.returnSeconds)+" root="+xyz(position(ped))+" head="+xyz(head)+" hand="+xyz(hand));
        return;
    }
    auto* h=hierarchy(ped);
    if(!h||!h->nodes||!h->matrices||h->num<1||h->num>256)return;
    const V root=position(ped);
    const V along=tangent(cover.wall.normal);
    const auto bone=[&](int id){
        for(int n=0;n<h->num;++n)if(h->nodes[n].id==id)return h->matrices[n].pos-root;
        return V{};
    };
    const auto xyz=[](V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
    const auto* cam=activeCamera();
    // QA reads only: distinguish a valid skeleton outside/behind the camera
    // from a pose failure. The production sweep fraction is a local variable,
    // so do not invent it from the shoulder offset or alter camera placement.
    const bool cameraApplied=cam&&viewState.cam==cam;
    const V cameraSource=cam?at<V>(const_cast<void*>(cam),0x19C):V{};
    const V cameraFront=cam?at<V>(const_cast<void*>(cam),0x190):V{};
    const V cameraUp=cam?at<V>(const_cast<void*>(cam),0x1B4):V{};
    const V cameraLeft=normalized(cross(cameraUp,cameraFront));
    const V pivot=root+V{0,0,.55f};
    const V headFromCamera=root+bone(saBone::head)-cameraSource;
    // This is the native CCam snapshot saved by applyCamera, not a claim
    // about the pre-sweep render matrix. Valid only when cameraApplied is 1.
    const V nativeCameraSource=cameraApplied?viewState.nativeSource:V{};
    const auto& collision=native_cover_camera_probe::sample;
    const unsigned collisionAge=*reinterpret_cast<const unsigned*>(0xB7CB4C)-collision.frame;
    const bool collisionValid=collision.valid&&collision.cam==cam&&collision.ref==cover.ref&&collisionAge<=1;
    V barrel{};
    for(int n=0;n<h->num;++n)if(h->nodes[n].id==24)barrel=normalized(h->matrices[n].right);
    V aimRay{};
    if(!cover.aiming&&transition.motion==Motion::Blind&&cam)
        aimRay=normalized(at<V>(const_cast<void*>(cam),0x19C)+normalized(at<V>(const_cast<void*>(cam),0x190))*35.f-(root+bone(24)));
    if(cover.aiming){
        V from{},end{};
        reinterpret_cast<void(__thiscall*)(void*,float,V,V&,V&)>(0x514970)
            (reinterpret_cast<void*>(0xB6F028),35.f,root+bone(24),from,end);
        aimRay=normalized(end-(root+bone(24)));
    }
    static constexpr const char* names[]={"entry","dive","rest","walk","turn","peek","blind","aim_intro","aim_native","return"};
    event("cover_animation_frame",cover.ref,0,
        "ms="+std::to_string(gameTime())+" root="+xyz(root)+
        " destination="+xyz(cover.destination)+" velocity="+xyz(at<V>(ped,0x44))+
        " blind="+std::to_string(cover.blind)+" blind_time="+std::to_string(blindTime(clipMeta(poseReader,ped,blindKey(combatWeapon(ped))).duration))+
        " frame="+std::to_string(*reinterpret_cast<unsigned*>(0xB7CB4C))+
        " entered="+std::to_string(cover.entered)+" low="+std::to_string(cover.wall.low)+
        " pose="+names[int(transition.motion)]+" blending="+std::to_string(!transition.previous.empty())+
        " normal="+std::to_string(dot(root-cover.wall.point,cover.wall.normal))+
        " tangent="+std::to_string(dot(root-cover.wall.point,along))+
        " yaw="+std::to_string(at<float>(ped,0x558))+
        " walk_time="+std::to_string(cover.slideTime)+
        " aiming="+std::to_string(cover.aiming)+" corner="+std::to_string(cover.corner)+
        " peeking="+std::to_string(cover.peeking)+" side="+std::to_string(cover.side)+
        " shoulder_m="+std::to_string(viewState.shoulder)+
        " camera_seeded="+std::to_string(viewState.seeded)+
        " cam="+xyz(cameraFront)+
        " camera_applied="+std::to_string(cameraApplied)+
        " camera_unoccluded_source="+std::to_string(cameraApplied&&viewState.unoccludedSource)+
        " cam_source="+xyz(cameraSource)+" cam_pivot="+xyz(pivot)+
        " cam_pivot_distance="+std::to_string(cam?length(cameraSource-pivot):-1.f)+
        " cam_native_source="+xyz(nativeCameraSource)+
        " cam_native_pivot_distance="+std::to_string(cameraApplied?length(nativeCameraSource-pivot):-1.f)+
        " cam_root_depth="+std::to_string(cam?dot(root-cameraSource,cameraFront):0.f)+
        " cam_head_depth="+std::to_string(cam?dot(headFromCamera,cameraFront):0.f)+
        " cam_head_lateral="+std::to_string(cam?dot(headFromCamera,cameraLeft):0.f)+
        " cam_head_vertical="+std::to_string(cam?dot(headFromCamera,cameraUp):0.f)+
        " collision_probe_installed="+std::to_string(native_cover_camera_probe::installed)+
        " collision_sample_valid="+std::to_string(collisionValid)+
        " collision_frame="+std::to_string(collision.frame)+" collision_frame_age="+std::to_string(collisionAge)+
        " collision_ref="+std::to_string(collision.ref)+
        " collision_pre_source="+xyz(collision.beforeSource)+" collision_pre_target="+xyz(collision.beforeTarget)+
        " collision_post_source="+xyz(collision.afterSource)+" collision_post_target="+xyz(collision.afterTarget)+
        " collision_pre_distance="+std::to_string(collisionValid?length(collision.beforeSource-collision.beforeTarget):-1.f)+
        " collision_post_distance="+std::to_string(collisionValid?length(collision.afterSource-collision.afterTarget):-1.f)+
        " collision_source_shift="+std::to_string(collisionValid?length(collision.afterSource-collision.beforeSource):-1.f)+
        " barrel="+xyz(barrel)+
        " aim_ray="+xyz(aimRay)+
        " chest="+xyz(bone(saBone::chest))+" shoulder="+xyz(bone(22))+" elbow="+xyz(bone(23))+
        " left_hand="+xyz(bone(34))+" head="+xyz(bone(saBone::head))+" pelvis="+xyz(bone(saBone::pelvis))+" hand="+xyz(bone(24)));
}
}
