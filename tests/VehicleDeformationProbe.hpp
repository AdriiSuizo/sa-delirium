#pragma once

static int deformationFixture(int mode) {
    using namespace vehicle_deformation;
    if (mode == 384) return enabled && installed;
    if (mode == 385) return int(vehicle_lab::vehicle_menu::active);
    if (mode == 386) return int(GetModuleHandleW(L"LivelyVehiclePhysics.asi") != nullptr);
    if (!ready || !playing() || !player() || area() != 0 || scriptMissionActive() ||
        !GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW", nullptr, 0)) return -380;
    static int target = -1, twin = -1;
    static bool sideTest = false;
    if (mode == 381) {
        const auto found = cars.find(target);
        if (found == cars.end()) return 0;
        return int(std::count_if(found->second.surfaces.begin(), found->second.surfaces.end(),
            [](const auto& part) { return part.visible; }));
    }
    if (mode == 382) {
        auto* car = vehicleFromRef(twin);
        return car && !cars.contains(twin) && at<float>(car, 0x4C0) == 1000.f;
    }
    if (mode == 383) {
        if (auto* car = vehicleFromRef(target)) {
            reinterpret_cast<void(__thiscall*)(void*)>(fixAddress)(car);
            return !cars.contains(target);
        }
        return -383;
    }
    if (mode == 388) {
        auto* car = vehicleFromRef(target);
        if (car && sideTest) {
            // Move the already damaged car away from the striker and its dust.
            teleport(car, {2432.f, -1658.f, position(car).z});
            at<V>(car, 0x44) = {};
            at<V>(car, 0x50) = {};
        }
        const auto* frame = car ? matrix(car) : nullptr;
        if (!frame) return -388;
        const V look = frame->pos + (sideTest ? V{0,0,.45f} : frame->up * 1.1f);
        const V camera = look + frame->up * (sideTest ? 1.f : 4.f) + frame->right * (sideTest ? -4.5f : 2.5f) + V{0,0,1.35f};
        const V up{};
        auto* nativeCamera = reinterpret_cast<void*>(0xB6F028);
        reinterpret_cast<void(__thiscall*)(void*,const V*,const V*)>(0x50BEC0)(nativeCamera, &camera, &up);
        reinterpret_cast<void(__thiscall*)(void*,const V*,short,int)>(0x50C8B0)(nativeCamera, &look, 2, 1);
        return target;
    }

    const auto vertical = reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    sideTest = mode == 387;
    const int model = sideTest || mode == 389 ? 429 : 445;
    const auto spawn = [vertical, model](float x, float y, float heading) {
        ColPoint ground{};
        void* support = nullptr;
        if (!vertical(V{x,y,20.f}, 8.f, ground, support, true,false,false,true,true,false,nullptr)) return -1;
        ProbeScript command(0x00A5);
        command.integer(model); command.number(x); command.number(y); command.number(ground.point.z + .65f);
        command.result();
        const int ref = command.run();
        if (auto* car = vehicleFromRef(ref))
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car, heading);
        return ref;
    };
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
    if (mode == 389) reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(2932,2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    if (mode == 389) {
        const V centre{2411.f,-1659.5f,20.f};
        ColPoint ground{}; void* support=nullptr;
        if (!vertical(centre,8.f,ground,support,true,false,false,true,true,false,nullptr)) return -389;
        ProbeScript create(0x029B);
        create.integer(2932); create.number(centre.x); create.number(centre.y);
        create.number(ground.point.z+1.f); create.result();
        const int objectRef=create.run();
        auto* object=reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(objectRef);
        if (!object || !matrix(object)) return -389;
        const auto colModel=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300);
        const auto* col=colModel(object);
        if (!col || !col->data) return -389;
        teleport(object,{centre.x,centre.y,ground.point.z-col->box.min.z});
        ProbeScript freeze(0x0392); freeze.integer(objectRef); freeze.integer(0); freeze.run();
        const V edge=point(*matrix(object), {col->box.max.x,col->box.max.y,col->box.min.z+.65f});
        const V forward=normalized(matrix(object)->right*(-1.f)+matrix(object)->up*(-1.f));
        const V right{forward.y,-forward.x,0.f};
        const float heading=std::atan2(-forward.x,forward.y);
        const V spawnAt=edge-forward*6.8f-right*.65f;
        target=spawn(spawnAt.x,spawnAt.y,heading);
        twin=spawn(2413.f,-1651.f,-1.5707963f);
        auto* car=vehicleFromRef(target);
        if (!car || !vehicleFromRef(twin) || !(at<unsigned char>(object,0x1C)&4u)) return -389;
        teleport(player(), {spawnAt.x+1.f,spawnAt.y-4.5f,position(car).z+.6f});
        at<unsigned char>(car,0xB8)=0;
        at<V>(car,0x44)=forward*.44f;
        event("probe_deformation_corner",target,model,"object_ref="+std::to_string(objectRef)+
            " twin="+std::to_string(twin)+" native_collision=1");
        return target;
    }
    target = spawn(sideTest ? 2420.f : 2410.f, -1658.f, sideTest ? 0.f : -1.5707963f);
    const int obstacle = spawn(sideTest ? 2404.f : 2418.f, -1658.f, sideTest ? -1.5707963f : 1.5707963f);
    twin = spawn(2415.f, -1651.f, -1.5707963f);
    auto* car = vehicleFromRef(target);
    if (!car || !vehicleFromRef(obstacle) || !vehicleFromRef(twin)) return -381;
    teleport(player(), {2415.f, -1654.f, position(car).z + .6f});
    ProbeScript seat(0x036A);
    seat.integer(reference(player())); seat.integer(target); seat.run();
    if (at<void*>(car,0x460) != player()) return -382;
    reinterpret_cast<void(__thiscall*)(void*)>(0x50BAB0)(reinterpret_cast<void*>(0xB6F028));
    auto* moving = sideTest ? vehicleFromRef(obstacle) : car;
    at<unsigned char>(moving,0xB8) = 0;
    at<V>(moving,0x44) = matrix(moving)->up * .68f;
    event("probe_deformation_scene", target, model, "twin=" + std::to_string(twin) + " live_collision=1");
    return target;
}
