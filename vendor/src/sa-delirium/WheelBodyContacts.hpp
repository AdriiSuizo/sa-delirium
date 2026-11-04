#pragma once
// CAutomobile::ProcessControl converts line fractions to spring fractions
// BEFORE ProcessSuspension. See Automobile-20261029.cpp:634. Using raw line
// fractions here would silently produce the wrong wheel height and force.
struct SuspensionLine {V start;float pad0;V end;float pad1;};
static_assert(sizeof(SuspensionLine)==0x20&&offsetof(SuspensionLine,end)==0x10);
struct WheelBodyContact {int body=-1,part=-1;V world{},relative{},velocity{};float fraction=1,travel=0;};
struct WheelBodyLoad {int body=-1,part=-1,car=-1;V point{},impulse{};bool queued=false;};
static std::vector<WheelBodyLoad> wheelBodyLoads;
static void* suspensionCar=nullptr;
static std::array<WheelBodyContact,4> suspensionContacts;
static std::map<int,unsigned> wheelContactLog;
static int wheelProbeBody=-1,wheelProbeCar=-1;
static unsigned wheelProbeUntil=0,wheelProbeLogged=0;
static float wheelProbePeakSpeed=0,wheelProbePeakUp=0,wheelProbePeakEnergy=0;
static bool(__thiscall* originalSpringAlt)(void*,float,V&,V&,float,float,V&,float&)=nullptr;
static bool(__thiscall* originalSpringDamping)(void*,float,float,V&,V&,V&)=nullptr;
static void(__thiscall* originalSuspension)(void*)=nullptr;
static WheelBodyContact* wheelAt(void*car,V relative){
    if(car!=suspensionCar)return nullptr;
    for(auto&c:suspensionContacts)if(c.body>=0&&length(c.relative-relative)<.003f)return &c;
    return nullptr;
}
static void recordWheelReaction(void*car,const WheelBodyContact&c,V before){
    const V impulse=(before-at<V>(car,0x44))*(50.f*at<float>(car,0x8C));
    if(!finite(impulse)||length(impulse)<=1.e-5f)return;
    bool queued=false;
    if(auto it=agents.find(c.body);it!=agents.end()&&!it->second->recovering){
        const V p=physicalContactPoint(*it->second,c.part,c.world);
        queued=ok(nm_agent_apply_impulse(it->second->agent,c.part,&p.x,&impulse.x));
    }
    wheelBodyLoads.push_back({c.body,c.part,vehicleRef(car),c.world,impulse,queued});
}
static V carPointVelocity(void*car,V relative){
    return reinterpret_cast<V(__thiscall*)(void*,V)>(0x542CE0)(car,relative)*50.f;
}
static bool wheelBodyMobility(const WheelBodyContact&c,V normal,float&inverse,V&velocity){
    auto it=agents.find(c.body);if(it==agents.end())return false;auto&a=*it->second;
    const V p=physicalContactPoint(a,c.part,c.world);
    if(!ok(nm_agent_get_contact_inverse_mass(a.agent,c.part,&p.x,&normal.x,&inverse)))return false;
    if(a.recovering){velocity=c.velocity;return true;}
    return ok(nm_agent_get_predicted_contact_velocity(a.agent,c.part,&p.x,&velocity.x));
}
static bool __fastcall springAlt(void*car,void*,float force,V&direction,V&point,float fraction,float bias,V&normal,float&limit){
    auto*c=wheelAt(car,point);
    if(!c)return originalSpringAlt(car,force,direction,point,fraction,bias,normal,limit);
    const V before=at<V>(car,0x44),spin=at<V>(car,0x50),v=carPointVelocity(car,point);
    const bool result=originalSpringAlt(car,force,direction,point,fraction,bias,normal,limit);
    const V dv=at<V>(car,0x44)-before,dw=at<V>(car,0x50)-spin;
    const V j=dv*(50.f*at<float>(car,0x8C));const float nativeJ=length(j);
    if(nativeJ>1.e-5f){
        const V n=j*(1.f/nativeJ);float bodyInverse=0;V bodyVelocity{};
        const float carInverse=std::max(0.f,dot(carPointVelocity(car,point)-v,n)/nativeJ);
        const float dt=std::min(*reinterpret_cast<float*>(0xB7CB5C),3.f)*.02f;
        const float compression=std::max(0.f,1.f-fraction)*c->travel*std::abs(dot(normalized(direction),n));
        const float coupled=wheelBodyMobility(*c,n,bodyInverse,bodyVelocity)?
            coupledSpringImpulse(nativeJ,compression,dot(v-bodyVelocity,n),dt,carInverse+bodyInverse):0;
        const float scale=coupled/nativeJ;
        // Replace only this function's own impulse, preserving all other
        // world/car contacts and the native angular response at this point.
        at<V>(car,0x44)=before+dv*scale;at<V>(car,0x50)=spin+dw*scale;limit*=scale;
        recordWheelReaction(car,*c,before);
    }
    return result;
}
static bool __fastcall springDamping(void*car,void*,float force,float limit,V&direction,V&point,V&speed){
    auto*c=wheelAt(car,point);
    if(!c)return originalSpringDamping(car,force,limit,direction,point,speed);
    const V before=at<V>(car,0x44),spin=at<V>(car,0x50),v=carPointVelocity(car,point);
    float unused=0;V bodyVelocity=c->velocity;wheelBodyMobility(*c,normalized(direction),unused,bodyVelocity);
    V relative=(v-bodyVelocity)*.02f;
    const bool result=originalSpringDamping(car,force,limit,direction,point,relative);
    const V dv=at<V>(car,0x44)-before,dw=at<V>(car,0x50)-spin,j=dv*(50.f*at<float>(car,0x8C));
    const float nativeJ=length(j);
    if(nativeJ>1.e-5f){
        const V n=j*(1.f/nativeJ);float bodyInverse=0;
        const float carInverse=std::max(0.f,dot(carPointVelocity(car,point)-v,n)/nativeJ);
        const float coupled=wheelBodyMobility(*c,n,bodyInverse,bodyVelocity)?
            dissipativeImpulse(nativeJ,-dot(v-bodyVelocity,n),carInverse+bodyInverse):0;
        const float scale=coupled/nativeJ;
        at<V>(car,0x44)=before+dv*scale;at<V>(car,0x50)=spin+dw*scale;
        recordWheelReaction(car,*c,before);
    }
    return result;
}
static void __fastcall processSuspension(void*car,void*){
    if(!recoveryAllowed()||agents.empty()||vehicleRef(car)<0||at<int>(car,0x594)!=0||!matrix(car)||matrix(car)->at.z<.5f){originalSuspension(car);return;}
    auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    // Only ordinary four-line cars, not Rhino extra axles, bikes or disks.
    if(!col||!col->data||!col->data->line||col->data->lines!=4||(col->data->flags&1u)){originalSuspension(car);return;}
    suspensionContacts={};suspensionCar=car;
    for(int wheel=0;wheel<4;wheel++){
        const auto&line=static_cast<SuspensionLine*>(col->data->line)[wheel];
        const V from=point(*matrix(car),line.start),to=point(*matrix(car),line.end),delta=to-from;
        const float depth=length(delta);if(depth<.05f||depth>3.f)continue;
        auto&contact=suspensionContacts[wheel];
        contact.fraction=at<float>(car,0x7D4+4*wheel);
        for(auto&[ref,entry]:agents){auto&a=*entry;
            if(!contactBodyValid(a)||length(contactPose(a)[0].p-from)>2.5f)continue;
            const auto&pose=contactPose(a);
            for(int part=0;part<21;part++){
                const V axis=rotate(pose[part].q,{0,a.rig->partHalf[part],0});float distance=0;V normal{};
                if(!rayCapsule(from,delta*(1.f/depth),depth,pose[part].p-axis,pose[part].p+axis,a.rig->partRadius[part],distance,normal))continue;
                const float ratio=suspensionFraction(distance/depth,at<float>(car,0x878+4*wheel),at<float>(car,0x888+4*wheel));
                if(!std::isfinite(ratio)||ratio>=contact.fraction)continue;
                // Normal full compression is the physical travel limit. SA's
                // original spring supplies the impulse, never a hand-made lift.
                contact={ref,part,from+delta*(distance/depth),{}, {},std::max(0.f,ratio)};
                contact.travel=at<float>(car,0x878+4*wheel);
                contact.relative=contact.world-position(car);
                V linear{},spin{};nm_agent_get_part_velocity(a.agent,part,&linear.x,&spin.x);
                if(a.recovering){linear=a.renderVelocity[part];spin=a.renderSpin[part];}
                contact.velocity=linear+cross(spin,contact.world-pose[part].p);
                auto&cp=at<ColPoint>(car,0x724+sizeof(ColPoint)*wheel);
                cp.point=contact.world;cp.normal=normal;cp.depth=0;
                cp.surfaces[3]=62; // SURFACE_PED, checked against eSurfaceType.h
            }
        }
        if(contact.body>=0){
            at<float>(car,0x7D4+4*wheel)=contact.fraction;
            // These native temporary wheel refs are cleared later in this same
            // ProcessControl. Do not leave a previous platform's velocity here.
            at<void*>(car,0x8E8+sizeof(void*)*wheel)=nullptr;
            if(gameTime()-wheelContactLog[vehicleRef(car)]>200){wheelContactLog[vehicleRef(car)]=gameTime();
                event("wheel_body_support",contact.body,-1,"vehicle="+std::to_string(vehicleRef(car))+" wheel="+std::to_string(wheel)+
                    " part="+std::to_string(contact.part)+" compression="+std::to_string(contact.fraction));}
        }
    }
    originalSuspension(car);suspensionCar=nullptr;
}
static void applyWheelBodyLoads(){
    if(!recoveryAllowed()){wheelBodyLoads.clear();return;}
    for(const auto&load:wheelBodyLoads){auto it=agents.find(load.body);if(it==agents.end())continue;auto&a=*it->second;
        if(!contactBodyValid(a))continue;
        if(resumePhysicalContact(a,"recovery_interrupted_by_wheel")){
            stopBehaviour(a,"bodyBalance");ok(nm_agent_task_catch_fall(a.agent,"*"));ensureRecoveryTask(a);
        }
        const V contact=physicalContactPoint(a,load.part,load.point);
        if(!load.queued)ok(nm_agent_apply_impulse(a.agent,load.part,&contact.x,&load.impulse.x));a.stableFor=0;
        if(gameTime()-a.lastVehicleContactReport>200){a.lastVehicleContactReport=gameTime();
            event("wheel_body_impulse",a.ref,a.model,"vehicle="+std::to_string(load.car)+" impulse_ns="+std::to_string(length(load.impulse))+
                " iz="+std::to_string(load.impulse.z)+" coupled_spring_reaction=1");}
    }
    wheelBodyLoads.clear();
}
static void applyVehicleBodyFeedback(Active&a,float dt){
    std::array<int,256> ids;std::array<float,256*6> values;
    for(;;){
        const int count=nm_agent_take_obstacle_impulses(a.agent,ids.data(),values.data(),256);if(count<0){ok(count);break;}
        for(int i=0;i<count;i++){
            const V j{values[6*i],values[6*i+1],values[6*i+2]},p{values[6*i+3],values[6*i+4],values[6*i+5]};
            if(!finite(j)||!finite(p))continue;
            if(!a.preview){
                observeSolvedAnimatedContact(a,ids[i],j,p,dt);
                // Finite owners already received their aggregate result ONCE.
                // Per-agent contact records remain evidence, not another force.
                if(nm_world_obstacle_owner(collision->world,ids[i]))continue;
            }
            if(ids[i]<0||ids[i]>=int(collision->vehicles.size()))continue;
            auto&v=collision->vehicles[ids[i]];if(vehicleFromRef(v.ref)!=v.entity)continue;
            const V com=point(*matrix(v.entity),at<V>(v.entity,0xA4));
            recordRoadHull(v.ref,j,cross(p-com,j));
            if(dot(j,j)>1.e-10f)at<unsigned char>(v.entity,0xB8)=0;
            const V oldVelocity=at<V>(v.entity,0x44)*50.f,oldSpin=at<V>(v.entity,0x50)*50.f;
            reinterpret_cast<void(__thiscall*)(void*,V,V,bool)>(0x542B50)(v.entity,j*.02f,p-position(v.entity),true);
            const V velocity=at<V>(v.entity,0x44)*50.f,spin=at<V>(v.entity,0x50)*50.f;
            for(auto&part:collision->vehicles)if(part.ref==v.ref){
                if(part.velocityAtFrame||part.component){part.velocity=part.velocity+(velocity-oldVelocity)+cross(spin-oldSpin,part.frame.p-com);part.spin=part.spin+(spin-oldSpin);}
                else{part.velocity=velocity;part.spin=spin;}
            }
        }
        if(count<256)break;
    }
    // Feed the changed vehicle velocity into the next substep/agent too.
    if(a.preview)collision->applyVehicleBoxes();
    if(a.ref==wheelProbeBody&&gameTime()<wheelProbeUntil){
        V com{},vel{};nm_agent_get_com(a.agent,&com.x,&vel.x);
        const float energy=nm_agent_get_kinetic_energy(a.agent),mass=nm_agent_get_mass(a.agent);
        wheelProbePeakSpeed=std::max(wheelProbePeakSpeed,length(vel));wheelProbePeakUp=std::max(wheelProbePeakUp,vel.z);
        wheelProbePeakEnergy=std::max(wheelProbePeakEnergy,energy);
        if(gameTime()-wheelProbeLogged>=100){wheelProbeLogged=gameTime();
            void*car=vehicleFromRef(wheelProbeCar);
            event("body_vehicle_energy",a.ref,a.model,"body_mass_kg="+std::to_string(mass)+" body_energy_j_per_kg="+std::to_string(energy)+
                " peak_speed_mps="+std::to_string(wheelProbePeakSpeed)+" peak_up_mps="+std::to_string(wheelProbePeakUp)+
                " peak_energy_j_per_kg="+std::to_string(wheelProbePeakEnergy)+" car_speed_mps="+std::to_string(car?length(at<V>(car,0x44))*50.f:0));
        }
    }
}
