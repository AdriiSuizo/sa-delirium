#pragma once
// Pre-frame motion and pose seed the articulated replay of a native collision.
static std::map<int,V> beforeVehicleVelocity;
static std::map<int,V> beforePedVelocity;
static std::map<int,T> beforeVehicleFrame;
static std::map<int,V> beforePedRoot;
static std::map<int,std::shared_ptr<PoseSeed>> beforePedPose;
static std::map<int,unsigned> lastContactReaction;
static unsigned nativeVehicleContacts=0;
static bool legacyRoadKickBaseline(){
    static const bool value=[](){char passive[8]{},baseline[8]{};
        GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",passive,sizeof(passive));
        GetEnvironmentVariableA("GTASA_ROAD_KICK_BASELINE",baseline,sizeof(baseline));
        return passive[0]=='1'&&baseline[0]=='1';}();
    return value;
}
struct RoadForceContext {void*car=nullptr;int ped=-1,vehicle=-1;bool eligible=false,replaceAnimation=false;};
static thread_local RoadForceContext roadForceContext;
static void(__thiscall* originalRoadApplyForce)(void*,V,V,bool)=nullptr;
static void __fastcall roadApplyForce(void*car,void*,V force,V point,bool turn){
    // Only the independently disassembled KillPedWithCar call, on this thread
    // and this confirmed ped/car pair. All wall, tyre, suspension, crash and
    // Delirium feedback calls use the untouched original function.
    const auto c=roadForceContext;
    if(!c.eligible||c.car!=car||reinterpret_cast<uintptr_t>(_ReturnAddress())!=0x5F12CF){originalRoadApplyForce(car,force,point,turn);return;}
    const auto p=pending.find(c.ped);const auto a=agents.find(c.ped);
    const bool owns=(p!=pending.end()&&p->second.kind==Reaction::Impact&&p->second.vehicle==c.vehicle)||
        (a!=agents.end()&&a->second->kind==Reaction::Impact&&a->second->vehicle==c.vehicle);
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())==0x5F12CF&&c.eligible&&c.car==car&&owns&&
        vehicleFromRef(c.vehicle)==car&&pedFromRef(c.ped)){
        const V v=at<V>(car,0x44),spin=at<V>(car,0x50);
        const bool baseline=legacyRoadKickBaseline();
        if(baseline)originalRoadApplyForce(car,force,point,turn);
        const V dv=(at<V>(car,0x44)-v)*50.f,dw=(at<V>(car,0x50)-spin)*50.f;
        event(baseline?"native_ped_kick_observed":"native_ped_kick_removed",c.ped,-1,
            "vehicle_ref="+std::to_string(c.vehicle)+" native_jx="+std::to_string(force.x*50.f)+
            " native_jy="+std::to_string(force.y*50.f)+" native_jz="+std::to_string(force.z*50.f)+
            " dv_mps="+std::to_string(length(dv))+" spin_delta="+std::to_string(length(dw))+
            " point_x="+std::to_string(point.x)+" point_y="+std::to_string(point.y)+" point_z="+std::to_string(point.z));
        return;
    }
    originalRoadApplyForce(car,force,point,turn);
}
static void captureVehicleVelocities(){
    beforeVehicleVelocity.clear();beforePedVelocity.clear();beforeVehicleFrame.clear();beforePedRoot.clear();beforePedPose.clear();if(!ready||!playing()||!player())return;
    captureRoadPoseSeeds();
    if(auto*peds=pedPool())for(int i=0;i<peds->size;i++)if(!(peds->flags[i]&0x80)){
        void*p=peds->objects+i*0x7C4;if(matrix(p)&&length(position(p)-position(player()))<=radius+5)beforePedVelocity[(i<<8)|peds->flags[i]]=at<V>(p,0x44);
    }
    auto*pool=vehiclePool();if(!pool||pool->size<1||pool->size>10000)return;
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        void*v=pool->objects+i*0xA18;if(!matrix(v)||length(position(v)-position(player()))>radius+15)continue;
        beforeVehicleVelocity[(i<<8)|pool->flags[i]]=at<V>(v,0x44)*50.f;
        beforeVehicleFrame[(i<<8)|pool->flags[i]]=transform(*matrix(v));
    }
}
static bool roadCanReplaceAnimation(void*ped,void*car){
    if(!ready||!applyBones||!detectImpacts||!playing()||cutscene()||
       (protectMissions&&scriptMissionActive())||ped==player()||!validPed(ped,true))return false;
    const int carRef=vehicleRef(car);
    if(carRef<0||!playerGroundVehicle(at<int>(car,0x594)))return false;
    const auto prior=beforeVehicleVelocity.find(carRef);
    const V speed=prior!=beforeVehicleVelocity.end()?prior->second:at<V>(car,0x44)*50.f;
    if(!finite(speed)||std::hypot(speed.x,speed.y)<6.f)return false;
    const int ref=reference(ped);
    if(auto a=agents.find(ref);a!=agents.end())
        return a->second->ped==ped&&!a->second->mounted&&!a->second->recoveryAnimatedOwner;
    return beforePedPose.contains(ref);
}

static bool roadHasImpact(int ref,int carRef){
    if(auto request=pending.find(ref);request!=pending.end()&&
       request->second.kind==Reaction::Impact&&request->second.vehicle==carRef)return true;
    auto body=agents.find(ref);
    return body!=agents.end()&&body->second->kind==Reaction::Impact&&body->second->vehicle==carRef;
}

static bool roadReplacesDamageAnimation(const DamageEvent&damage,void*ped){
    if(!applyBones||!ready||!playing()||cutscene()||(protectMissions&&scriptMissionActive()))return false;
    const int ref=reference(ped);
    const RoadDamageReceipt delivered{damage.time,vehicleRef(damage.source),damage.weapon,damage.piece};
    const auto matches=[&](const RoadDamageReceipt&accepted){
        return accepted.replaceAnimation&&sameDeliveredRoadDamage(accepted,delivered,gameTime());
    };
    if(auto request=pending.find(ref);request!=pending.end()&&matches(request->second.roadDamage))return true;
    auto body=agents.find(ref);
    return body!=agents.end()&&body->second->ped==ped&&matches(body->second->roadDamage);
}

static void(__thiscall* originalRoadCollisionResponse)(void*,void*,void*,void*)=nullptr;
static void __fastcall roadCollisionResponse(void*handler,void*,void*collisionEvent,void*active,void*simplest){
    auto*ped=at<void*>(handler,0);
    auto*car=at<void*>(collisionEvent,0x14);
    const int ref=reference(ped),carRef=vehicleRef(car);
    const bool physical=carRef>=0&&roadHasImpact(ref,carRef)&&roadCanReplaceAnimation(ped,car);
    originalRoadCollisionResponse(handler,collisionEvent,active,simplest);
    auto*&task=at<void*>(handler,0x24);
    // Keep the native collision and anger bookkeeping. Only retire the
    // newly constructed HitPedWithCar animation task, before it can start.
    if(physical&&task&&at<void*>(task,0)==reinterpret_cast<void*>(0x86F294)){
        auto*hit=task;
        task=nullptr;
        reinterpret_cast<void(__thiscall*)(void*,unsigned)>((*reinterpret_cast<void***>(hit))[0])(hit,1);
        event("native_road_collision_task_replaced",ref,at<short>(ped,0x22),"vehicle_ref="+std::to_string(carRef));
    }
}
struct RoadContact {
    void* ped=nullptr;
    void* car=nullptr;
    V point{},normal{},relative{};
    float closing=0,vehicleClosing=0;
    int vehiclePiece=-1,pedPiece=-1;
};
static V roadPointVelocity(void*body,V point){
    const M& m=*matrix(body);
    const V local=at<V>(body,0xA4);
    const V centre=(at<unsigned>(body,0x40)&0x40u)?m.pos:
        m.pos+m.right*local.x+m.up*local.y+m.at*local.z;
    return (at<V>(body,0x44)+at<V>(body,0x5C)+
        cross(at<V>(body,0x50)+at<V>(body,0x68),point-centre))*50.f;
}
static RoadContact beforeRoadContact(void*self,void*other,const ColPoint&point){
    if(!ready||!detectImpacts||!self||!other)return {};
    const int a=at<unsigned char>(self,0x36)&7,b=at<unsigned char>(other,0x36)&7;
    const bool pedFirst=a==3&&b==2;
    if(!pedFirst&&!(a==2&&b==3))return {};
    auto*ped=pedFirst?self:other;
    auto*car=pedFirst?other:self;
    if(!matrix(ped)||!matrix(car)||!finite(point.point)||!finite(point.normal)||length(point.normal)<.5f)return {};
    const V normal=normalized(point.normal)*(pedFirst?1.f:-1.f);
    const V vehicleVelocity=roadPointVelocity(car,point.point);
    const V relative=vehicleVelocity-roadPointVelocity(ped,point.point);
    return {ped,car,point.point,normal,relative,dot(relative,normal),dot(vehicleVelocity,normal),
        point.surfaces[pedFirst?4:1],point.surfaces[pedFirst?1:4]};
}
static void acceptRoadContact(const RoadContact& hit,unsigned path){
    if(!hit.ped||!finite(hit.relative)||hit.closing<.65f||length(hit.relative)>80.f||
       !playing()||cutscene()||(protectMissions&&scriptMissionActive()))return;
    void*ped=hit.ped;
    void*car=hit.car;
    const int ref=reference(ped),carRef=vehicleRef(car);
    if(ref<0||carRef<0)return;
    const bool playerImpact=ped==player()&&playerVehicleImpacts&&playerGroundVehicle(at<int>(car,0x594));
    if(!validPed(ped,true,false,playerImpact)||(!playerImpact&&at<int>(car,0x594)!=0))return;
    // Walking into a parked car is not a vehicle running CJ over.
    if(playerImpact&&!playerMovingContact(hit.vehicleClosing,hit.closing))return;
    if(at<float>(ped,0x540)<=0)return; // Native damage owns death.
    if(lastContactReaction.count(ref)&&gameTime()-lastContactReaction[ref]<1000)return;
    if(auto a=agents.find(ref);a!=agents.end()&&!a->second->preview&&!a->second->recovering)return;
    if(pending.count(ref)&&pending[ref].kind!=Reaction::Brace)return;
    const V dir=hit.normal;
    watchRoadVehicle(carRef,ref);queueRoadFrames(ref);
    pending[ref]={{},dir,gameTime(),0,false,Reaction::Impact,carRef};
    pending[ref].playerVehicle=playerImpact;
    if(auto seed=beforePedPose.find(ref);seed!=beforePedPose.end())pending[ref].pose=seed->second;
    if(beforePedVelocity.count(ref)){pending[ref].velocityOverride=true;pending[ref].velocity=beforePedVelocity[ref];}
    nativeVehicleContacts++;lastContactReaction[ref]=gameTime();roadHealthBefore[ref]={gameTime(),at<float>(ped,0x540)};
    const V local=rotate(conjugate(transform(*matrix(car)).q),hit.normal);
    const auto vec=[](V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
    event("accepted_vehicle_contact",ref,at<short>(ped,0x22),"vehicle_ref="+std::to_string(carRef)+
        " closing_mps="+std::to_string(hit.closing)+" native_health="+std::to_string(at<float>(ped,0x540))+
        " native_path="+std::to_string(path)+" vehicle_piece="+std::to_string(hit.vehiclePiece)+
        " ped_piece="+std::to_string(hit.pedPiece)+" point="+vec(hit.point)+
        " normal="+vec(hit.normal)+" vehicle_local_normal="+vec(local)+
        " relative_mps="+vec(hit.relative));
}
// KillPedWithCar applies an additional force to the car AFTER generic contact
// resolution (5F12CA calls ApplyForce). Observe that specific ped/car response;
// never restore all end-of-frame speed, which includes roads/walls and braking.
// Entry/signature verified in SDK; local disassembly records the force call.
static void(__thiscall* originalCarPedResponse)(void*,void*,float,bool)=nullptr;
static bool boardingGripActive(const Active&a){
    if(a.kind!=Reaction::DoorEntry||a.recovering||a.entryReady||at<float>(a.ped,0x540)<=0)return false;
    float grip[10]{};return nm_agent_vehicle_entry_contact(a.agent,grip)==0&&grip[0]!=0&&int(grip[1])!=0;
}
// General pair hooks were rejected after reproducible startup failures; see
// logs/contact-rework-20261029.json. ApplyForce is filtered to one verified call.
static void __fastcall carPedResponse(void*ped,void*,void*car,float intensity,bool deadAnimation){
    const int carRef=vehicleRef(car),pedRef=reference(ped);
    const bool eligible=ready&&detectImpacts&&playing()&&!cutscene()&&!(protectMissions&&scriptMissionActive())&&carRef>=0&&pedRef>=0&&at<int>(car,0x594)==0&&validPed(ped,true,false,ped==player()&&playerVehicleImpacts);
    if(eligible)if(auto a=agents.find(pedRef);a!=agents.end()&&a->second->vehicle==carRef&&boardingGripActive(*a->second)){
        // IV get-in state 31 uses its own dt*6.5 drag damage while holding.
        // SA's capsule-based KillPedWithCar must not additionally kill/launch
        // the same attached actor. Real hull/wheel contacts stay in the solver;
        // another car, a released hand or other damage retains its native path.
        event("vehicle_entry_legacy_runover_replaced",pedRef,at<short>(ped,0x22),"vehicle_ref="+std::to_string(carRef)+" original_state31_drag_damage=1");return;
    }
    const V before=eligible?at<V>(car,0x44)*50.f:V{};
    const V spin=eligible?at<V>(car,0x50)*50.f:V{};
    const float carMass=eligible?at<float>(car,0x8C):0,pedMass=eligible?std::clamp(at<float>(ped,0x8C),40.f,150.f):0;
    const auto priorContext=roadForceContext;
    const bool replaceAnimation=eligible&&roadHasImpact(pedRef,carRef)&&roadCanReplaceAnimation(ped,car);
    roadForceContext={car,pedRef,carRef,eligible,replaceAnimation};
    if(eligible)watchRoadVehicle(carRef);
    originalCarPedResponse(ped,car,intensity,deadAnimation&&!replaceAnimation);
    if(replaceAnimation&&deadAnimation)
        event("native_road_blend_disabled",pedRef,at<short>(ped,0x22),"vehicle_ref="+std::to_string(carRef));
    roadForceContext=priorContext;
    if(eligible&&vehicleFromRef(carRef)==car&&pedFromRef(pedRef)==ped){
        const V after=at<V>(car,0x44)*50.f,horizontal{before.x,before.y,0};
        const float speed=length(horizontal);const V dir=normalized(horizontal);
        event("road_native_response",pedRef,at<short>(ped,0x22),"vehicle_ref="+std::to_string(carRef)+
            " dv_mps="+std::to_string(length(after-before))+" dv_z="+std::to_string(after.z-before.z)+
            " spin_delta="+std::to_string(length(at<V>(car,0x50)*50.f-spin))+" baseline="+std::to_string(legacyRoadKickBaseline()));
        // Old behavior retained ONLY for the explicit owned comparison. Normal
        // play removes the redundant scripted kick at its source, including
        // torque, and leaves measured hull/suspension reactions to the solver.
        const float correction=legacyRoadKickBaseline()?pedMomentumCorrection(speed,dot(after,dir),carMass,pedMass,false):0.f;
        if(finite(after)&&correction>0){
            at<V>(car,0x44)=after*(1.f/50.f)+dir*(correction/50.f);
            event("ped_vehicle_momentum",pedRef,at<short>(ped,0x22),"vehicle_ref="+std::to_string(carRef)+" pair_only=1 native_routine=KillPedWithCar before_mps="+std::to_string(speed)+" native_after_mps="+std::to_string(dot(after,dir))+" restored_mps="+std::to_string(correction));
        }
    }
}
