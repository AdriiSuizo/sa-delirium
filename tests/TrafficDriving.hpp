#pragma once

struct RoadLink {
    V point, direction;
};

static bool trafficLink(unsigned short address, signed char direction, RoadLink& link) {
    if (address == 0xffff || (direction != -1 && direction != 1)) return false;
    // CPathFind and CCarPathLink layouts verified against SA 1.0 US.
    auto* paths = reinterpret_cast<unsigned char*>(0x96F050);
    const unsigned area = address >> 10, index = address & 0x3ff;
    auto* links = at<unsigned char*>(paths, 0x924 + area * 4);
    if (!links || index >= at<unsigned>(paths, 0x1304 + area * 4)) return false;
    auto* item = links + index * 0xe;
    link.point = {at<short>(item, 0) * .125f, at<short>(item, 2) * .125f, 0};
    link.direction = normalized(V{float(at<signed char>(item, 8) * direction),
                                  float(at<signed char>(item, 9) * direction), 0});
    return length(link.direction) > .9f;
}

static bool pursuingPlayer(unsigned mission) {
    return (mission >= 2 && mission <= 5) || mission == 24 || mission == 25 || mission == 43 || mission == 44;
}

static bool pursuingVehicle(unsigned mission) {
    return (mission >= 15 && mission <= 18) || (mission >= 52 && mission <= 55) || mission == 60 || mission == 61;
}

// Native avoidance sees the animated pedestrian entity. Its normal collision
// proxy is inactive while Delirium owns CJ. Take the already solved limb
// centres into account when selecting a speed for an approaching NPC car.
static float trafficPhysicalPlayerLimit(void* car,const Box& bounds,float speed) {
    void* cj=player();
    if(!cj||inVehicle(cj)||at<unsigned char>(cj,0x2F)!=at<unsigned char>(car,0x2F))return INFINITY;
    const auto actor=agents.find(reference(cj));
    if(actor==agents.end())return INFINITY;
    const auto& body=*actor->second;
    if(body.ped!=cj||!body.rig||body.preview||body.mounted)return INFINITY;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    const T* parts=body.current.data();
    if(body.recoveryAnimatedOwner){
        // Drivers must keep yielding while CJ blends onto the get-up clip.
        if(body.lastRendered.empty()||gameTime()-body.lastRenderTime>100)return INFINITY;
        parts=body.renderedParts.data();
    }else if(!body.lastPhysicalRoadFrame||frame-*body.lastPhysicalRoadFrame>1){
        return INFINITY;
    }
    const auto m=matrix(car);
    if(!m||length(parts[0].p-m->pos)>std::min(30.f,5.f+speed*1.2f))return INFINITY;
    const V heading=normalized(V{m->up.x,m->up.y,0});
    if(length(heading)<.9f)return INFINITY;
    const V right{heading.y,-heading.x,0};
    float distance=INFINITY;
    for(unsigned part=0;part<body.current.size();++part){
        const V delta=parts[part].p-m->pos;
        if(!finite(delta))continue;
        const float radius=body.rig->partRadius[part];
        const float y=dot(delta,heading),x=dot(delta,right),z=dot(delta,m->at);
        if(y<bounds.max.y-.15f||x+radius<bounds.min.x||x-radius>bounds.max.x||
           z+radius<bounds.min.z-.28f||z-radius>bounds.max.z+.20f)continue;
        const float gap=std::max(0.f,y-bounds.max.y-radius-1.25f);
        distance=std::min(distance,gap);
    }
    return std::sqrt(2.f*4.8f*distance);
}


static void (__cdecl* nativeRailTraffic)(void*)=nullptr;
static constexpr uintptr_t railTrafficAddress=0x434790;
static constexpr uintptr_t modifyPathSpeedAddress=0x41B980;

static void __cdecl brakeRailTraffic(void* car) {
    nativeRailTraffic(car);
    if(!adaptNpcDriving||!playerVehicleImpacts||!recoveryAllowed()||
       carStatus(car)!=2||at<int>(car,0x594)!=0||at<unsigned char>(car,0x4A4)!=1||
       !at<void*>(car,0x460)||at<void*>(car,0x460)==player()||at<void*>(car,0xFC))return;
    const auto* m=matrix(car);
    if(!m)return;
    const V velocity=at<V>(car,0x44)*50.f;
    const float speed=dot(velocity,m->up);
    if(speed<0.f||!std::isfinite(speed))return;
    const auto* col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    if(!col)return;
    const float limit=trafficPhysicalPlayerLimit(car,col->box,speed);
    if(!std::isfinite(limit)||(limit>.05f&&speed<=limit+.05f))return;

    // The previous frame's world speed gives the local conversion for this
    // path segment. Native ModifySpeed maintains its interpolation phase.
    constexpr float brakingAcceleration=12.f;
    const float seconds=std::clamp(*reinterpret_cast<float*>(0xB7CB5C)*.02f,0.f,.05f);
    const float reduced=std::max(limit,speed-brakingAcceleration*seconds);
    const float pathSpeed=at<float>(car,0x3C8);
    if(!std::isfinite(pathSpeed)||pathSpeed<=0.f)return;
    const float next=speed>.2f ? pathSpeed*(reduced/speed) : 0.f;
    if(next>=pathSpeed)return;
    reinterpret_cast<void(__thiscall*)(void*,float)>(modifyPathSpeedAddress)(
        static_cast<unsigned char*>(car)+0x390,next);
}

static bool installRailTrafficBraking() {
    constexpr unsigned char railEntry[]={0x8B,0x15,0x4C,0xCB,0xB7,0,0x56};
    constexpr unsigned char modifyEntry[]={0xD9,0x44,0x24,4,0x83,0xEC,0x30};
    auto* entry=reinterpret_cast<void*>(railTrafficAddress);
    return memcmp(entry,railEntry,sizeof(railEntry))==0&&
        memcmp(reinterpret_cast<void*>(modifyPathSpeedAddress),modifyEntry,sizeof(modifyEntry))==0&&
        MH_CreateHook(entry,reinterpret_cast<void*>(&brakeRailTraffic),reinterpret_cast<void**>(&nativeRailTraffic))==MH_OK&&
        MH_QueueEnableHook(entry)==MH_OK;
}

static void adaptTrafficDriving(void* car, TrafficDriving& state, Frame& frame,
                                float wheelbase, float steeringScale, float traction,const Box& bounds) {
    const auto* driver = at<void*>(car, 0x460);
    if (!driver || driver == player() || carStatus(car) != 3 ||
        at<unsigned char>(car, 0x3BB) != 0 || frame.gas < 0.f ||
        frame.handbrake || wheelbase < .3f || !std::isfinite(traction)) return;
    const auto* m = matrix(car);
    const V velocity = vec(frame.velocity);
    const float speed = dot(velocity, m->up) > 0.f ? std::hypot(velocity.x, velocity.y) : 0.f;
    const unsigned mission = at<unsigned char>(car, 0x3BA);
    const bool law = (at<unsigned char>(car, 0x428) & 1) != 0 && pursuingPlayer(mission);
    void* target = law ? player() : pursuingVehicle(mission) ? at<void*>(car, 0x41C) : nullptr;
    const bool chase = (vehicleRef(target) >= 0 || reference(target) >= 0) && matrix(target);
    const float originalSteer = frame.steer;
    float steer = originalSteer;
    float limit = 100.f;
    const char* pursuit = "road";
    if (chase) {
        if (target == player() && inVehicle(target)) {
            if (auto* ride = at<void*>(target, 0x58C); vehicleRef(ride) >= 0 && matrix(ride)) target = ride;
        }
        const bool targetCar = vehicleRef(target) >= 0;
        const V delta = position(target) - m->pos;
        const float distance = std::hypot(delta.x, delta.y);
        const unsigned seed = at<unsigned short>(car, 0x20);
        unsigned wanted = 0;
        if (law) {
            if (auto* record = reinterpret_cast<void*(__cdecl*)(int)>(0x56E230)(-1))
                wanted = std::min(at<unsigned>(record, 0x2C), 6u);
        }
        const int model = at<short>(car, 0x22);
        const bool heavyPolice = model == 427 || model == 428 || model == 490 ||
                                 model == 528 || model == 599 || model == 601;
        const bool follow = law ? wanted <= 1 || heavyPolice : mission != 15 && mission != 16;
        const unsigned draw = seed & 3;
        const bool lead = law && !follow && (wanted == 2 ? draw != 1 : wanted == 3 ? draw < 3 :
                                      wanted == 4 ? draw < 2 : draw == 0);
        if (law && wanted) {
            limit = float(wanted == 1 ? 22 + draw : wanted == 2 ? 30 + draw :
                          wanted == 3 ? 36 + draw : (wanted == 6 ? 37 : 36) + (seed & 7));
            if (!targetCar && distance < 40.f) limit = std::min(limit, distance < 20.f ? 10.f : 15.f);
        }
        const V targetVelocity = at<V>(target, 0x44) * 50.f;
        if (distance < 40.f && distance > 2.f && std::abs(delta.z) < 3.f &&
            dot(delta, m->up) > .55f * distance && frame.brake < .15f) {
            if (!state.sightSample || frame.now - state.sightSample >= 200) {
                ColPoint hit{};void* obstruction=nullptr;
                const V eye{0,0,.6f};
                state.targetVisible = !reinterpret_cast<bool(__cdecl*)(const V&,const V&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool)>(0x56BA00)
                    (m->pos+eye,position(target)+eye,hit,obstruction,true,false,false,true,false,false,false,false);
                state.sightSample = frame.now;
            }
            if (state.targetVisible) {
                const V aim = delta + (lead ? targetVelocity * std::clamp(distance / std::max(speed, 8.f) * .4f, .1f, .7f) : V{});
                const float desired = -std::atan2(dot(aim, m->right), dot(aim, m->up));
                steer += std::clamp(desired - steer, -.18f, .18f) * .65f;
                pursuit = follow ? "follow" : lead ? "intercept" : "direct";
                if (follow && distance < 25.f) {
                    const float targetSpeed = std::max(0.f, dot(targetVelocity, normalized(delta)));
                    limit = std::min(limit, std::max(0.f, targetSpeed + (distance - 8.f) * .75f));
                }
            }
        }
    }

    const float grip = std::clamp(traction, .5f, 2.2f);
    // The route driver needs the acceleration the current tyres can use,
    // including reduced weight in lunar mode, before choosing corner speed.
    const float lateral = grip * 9.81f * frame.gravityScale * (chase ? .68f : .55f);
    const float curvature = std::abs(std::tan(std::clamp(steer, -.7f, .7f))) / wheelbase;
    float curveLimit = curvature > .003f ? std::sqrt(lateral / curvature) : 100.f;
    bool lookingAhead = false;
    RoadLink current{}, next{};
    if (trafficLink(at<unsigned short>(car, 0x3A4), at<signed char>(car, 0x3B5), current) &&
        trafficLink(at<unsigned short>(car, 0x3A6), at<signed char>(car, 0x3B6), next) &&
        dot(current.direction, m->up) > .5f) {
        V ahead = next.point - m->pos;ahead.z = 0;
        const float distance = length(ahead);
        const float corner = std::acos(std::clamp(dot(current.direction, next.direction), -1.f, 1.f));
        if (distance > 1.f && distance < 45.f && corner > .2f && dot(ahead, m->up) > 0.f) {
            const float arcLength = std::clamp(length(next.point-current.point)*.6f, wheelbase, 14.f);
            const float approaching = std::sqrt(lateral*arcLength/corner + 2.f*3.1f*std::max(0.f,distance-speed*frame.dt-5.f));
            lookingAhead = approaching < curveLimit;
            curveLimit = std::min(curveLimit, approaching);
        }
    }
    limit = std::min(limit, std::max(curveLimit, 6.f));
    const float bodyLimit=trafficPhysicalPlayerLimit(car,bounds,speed);
    limit=std::min(limit,bodyLimit);
    if (speed > limit - 1.f) {
        frame.gas = std::min(frame.gas, std::clamp((limit - speed) / 4.f, 0.f, 1.f));
        const float downhill = 1.f + std::max(0.f, -m->up.z * 4.f);
        const float braking = limit < .05f && speed < .5f ? 1.f :
            std::clamp((speed - limit) * (bodyLimit<=limit?.20f:.10f) * downhill, 0.f, .75f);
        frame.brake = std::max(frame.brake, braking);
    }
    // IV's AI steering envelope (TU8 0x82557DC8). Fade out the existing
    // full-lock assistance after low-speed turns and junction manoeuvres.
    const float maxSteer = speed > 42.f ? .2f : std::min(.7f, .9f-speed/60.f);
    const float assisted = std::clamp(steer * (quickerDriving ? 1.25f : 1.f), -.9f, .9f);
    const float manoeuvre = std::clamp((18.f-speed)/10.f, 0.f, 1.f);
    const float roadAngle = std::clamp(steer, -maxSteer, maxSteer) / steeringScale;
    frame.steer = roadAngle + (assisted-roadAngle)*manoeuvre;
    if (auditTraffic && frame.now - state.lastAudit >= 450 && (chase || speed > curveLimit - 1.f)) {
        state.lastAudit = frame.now;
        event("vehicle_iv_traffic_command", vehicleRef(car), at<short>(car, 0x22),
            std::string("policy=")+pursuit+" law="+std::to_string(law)+" mission="+std::to_string(mission)+
            " speed_mps="+std::to_string(speed)+" target_mps="+std::to_string(limit)+
            " curve_mps="+std::to_string(curveLimit)+" lookahead="+std::to_string(lookingAhead)+
            " brake="+std::to_string(frame.brake)+
            " original_steer="+std::to_string(originalSteer)+" road_steer="+std::to_string(frame.steer*steeringScale));
    }
}
