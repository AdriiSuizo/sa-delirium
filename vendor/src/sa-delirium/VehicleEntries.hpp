#pragma once
#include "../plugin-sdk-sa/plugin_sa/game_sa/enums/eTaskType.h"
#include "GripFrameMath.hpp"
#include "CombatMovement.hpp"
#include <cstdlib>
static int entryProbeRef=-1,entryProbeCar=-1;static float entryProbeSpeed=0;static unsigned entryProbeStarted=0,entryProbeReport=0;static bool entryProbeAccelerated=false;
static V entryProbeOrigin{};
static float entryProbeSimulationSeconds=0;
static float entryProbeRampSeconds=.75f,entryProbeStartClipSeconds=.15f;
struct EntrySolvedClock {void*agent=nullptr;unsigned start=0;uint64_t steps=0;double seconds=0;};
static std::map<int,EntrySolvedClock> entrySolvedClocks;
static double entrySolvedSeconds(const Active&a){
    const auto it=entrySolvedClocks.find(a.ref);
    return it!=entrySolvedClocks.end()&&it->second.agent==a.agent&&it->second.start==a.start?it->second.seconds:0;
}
// Call once after a successful shared solve/commit, only for participants that
// actually consumed this substep. Independent of game milliseconds and task
// phase clocks: Balance->Relax and later FallDown cannot reset this observer.
static void observeVehicleEntrySolvedStep(Active&a,float dt){
    if(a.kind!=Reaction::DoorEntry||!std::isfinite(dt)||dt<=0)return;
    auto&clock=entrySolvedClocks[a.ref];
    if(clock.agent!=a.agent||clock.start!=a.start)clock={a.agent,a.start,0,0};
    clock.seconds+=dt;++clock.steps;
    float points[12]{};const int mask=nm_agent_grab_points(a.agent,points);
    event("vehicle_entry_solver_clock",a.ref,a.model,"entry_epoch="+std::to_string(clock.start)+" solved_step="+std::to_string(clock.steps)+
        " simulated_seconds="+std::to_string(clock.seconds)+" step_seconds="+std::to_string(dt)+" held_mask="+std::to_string(mask)+" actual_solved_step=1");
}
// Parameters belong to an owned test process only. The real entry controller,
// its speed gate, contact query and grip strength never read them.
static bool entryProbeNumber(const char*name,float fallback,float lo,float hi,float&out){
    out=fallback;char value[64]{};const DWORD n=GetEnvironmentVariableA(name,value,sizeof(value));
    if(!n)return true;if(n>=sizeof(value))return false;
    char*end=nullptr;out=std::strtof(value,&end);
    return end&&end!=value&&!*end&&std::isfinite(out)&&out>=lo&&out<=hi;
}
static std::map<int,ExitTransit> entryMotion;
// The native line-up task moves the entity matrix while deliberately zeroing
// moveSpeed. Observe both frames; zero velocity alone does not mean a stuck ped.
static void observeNativeVehicleOpening(void*p,void*v,void*entry,void*leaf,const PoseSeed&pose,ExitTransit&motion,unsigned frame){
    const bool fixture=GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)!=0;
    if(!fixture&&gameTime()-motion.lastSample<100)return;
    motion.lastSample=gameTime();
    auto vec=[](V x){return std::to_string(x.x)+","+std::to_string(x.y)+","+std::to_string(x.z);};
    void*anim=at<void*>(leaf,0xC);void*line=at<void*>(leaf,0x1C);const int door=at<int>(leaf,0x14);
    std::string detail="frame="+std::to_string(frame)+" entry="+std::to_string(reinterpret_cast<uintptr_t>(entry))+" leaf="+std::to_string(reinterpret_cast<uintptr_t>(leaf))+
        " vehicle_ref="+std::to_string(vehicleRef(v))+" leaf_vehicle_ref="+std::to_string(vehicleRef(at<void*>(leaf,0x10)))+" requested_door="+std::to_string(door)+
        " root="+vec(position(p))+" native_velocity="+vec(at<V>(p,0x44)*50.f)+" car_root="+vec(position(v))+" car_velocity="+vec(at<V>(v,0x44)*50.f)+
        " part0="+vec(pose.parts[0].p)+" part16="+vec(pose.parts[16].p)+" part20="+vec(pose.parts[20].p);
    if(anim)detail+=" animation="+std::to_string(reinterpret_cast<uintptr_t>(anim))+" clip="+std::to_string(at<short>(anim,0x2C))+" group="+std::to_string(at<short>(anim,0xE))+
        " clip_seconds="+std::to_string(at<float>(anim,0x20))+" clip_weight="+std::to_string(at<float>(anim,0x18));
    if(line)detail+=" lineup="+std::to_string(reinterpret_cast<uintptr_t>(line))+" lineup_type="+std::to_string(at<int>(line,0x14))+" lineup_door="+std::to_string(at<int>(line,0x18))+
        " lineup_offset="+vec(at<V>(line,0))+" lineup_until="+std::to_string(at<unsigned>(line,0x10))+" lineup_z="+std::to_string(at<float>(line,0xC));
    if(door>=8&&door<=11)if(void*f=at<void*>(v,0x648+door*4)){
        const int i=door==10?2:door==8?3:door==11?4:5;void*d=static_cast<unsigned char*>(v)+0x5B8+i*0x18;
        detail+=" hinge_angle="+std::to_string(at<float>(d,0xC))+" hinge_previous="+std::to_string(at<float>(d,0x10));
        if(auto*fm=reinterpret_cast<M*(__cdecl*)(void*)>(0x7F0990)(f))detail+=" requested_door_origin="+vec(fm->pos)+" requested_door_right="+vec(fm->right)+" requested_door_up="+vec(fm->up);
    }
    if(auto*h=hierarchy(p))for(int n=0;h->nodes&&n<h->num&&size_t(n)<pose.bones.size();n++)
        if(h->nodes[n].id==1||h->nodes[n].id==34||h->nodes[n].id==24)detail+=" bone"+std::to_string(h->nodes[n].id)+"="+vec(pose.bones[n].p);
    event("vehicle_entry_native_frame",reference(p),at<short>(p,0x22),detail);
    if(fixture&&anim&&door>=8&&door<=11)if(void*f=at<void*>(v,0x648+door*4)){
        const auto*shape=doorShape(v,door,f);
        const auto*fm=reinterpret_cast<M*(__cdecl*)(void*)>(0x7F0990)(f);
        if(shape&&shape->shape.id>=0&&fm){
            T surface=transform(*fm);surface.p=point(*fm,shape->centre);
            const T inv=inverse(surface);
            auto gap=[&](V world){const V local=mul(inv,T{{},world}).p;V closest{};
                return closestVehicleSurface(shape->shape,local,closest)?length(closest-local):-1.f;};
            float left=-1,right=-1;
            if(auto*h=hierarchy(p))for(int n=0;h->nodes&&n<h->num&&size_t(n)<pose.bones.size();n++){
                if(h->nodes[n].id==34)left=gap(pose.bones[n].p);
                if(h->nodes[n].id==24)right=gap(pose.bones[n].p);
            }
            event("probe_entry_authored_contact",reference(p),at<short>(p,0x22),
                "frame="+std::to_string(frame)+" group="+std::to_string(at<short>(anim,0xE))+" clip="+std::to_string(at<short>(anim,0x2C))+
                " clip_seconds="+std::to_string(at<float>(anim,0x20))+" clip_weight="+std::to_string(at<float>(anim,0x18))+
                " requested_door="+std::to_string(door)+" speed_mps="+std::to_string(length(at<V>(v,0x44))*50.f)+
                " root_gap="+std::to_string(gap(position(p)))+" left_wrist_gap="+std::to_string(left)+" right_wrist_gap="+std::to_string(right)+
                " left_physics_palm_gap="+std::to_string(gap(pose.parts[16].p+rotate(pose.parts[16].q,V{.02f,0,0})))+
                " right_physics_palm_gap="+std::to_string(gap(pose.parts[20].p+rotate(pose.parts[20].q,V{.02f,0,0})))+
                " source=actual_visible_door_triangles test_only=1");
        }
    }
}
static void tickVehicleEntryProbe(){
    if(entryProbeRef<0||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return;
    void*p=pedFromRef(entryProbeRef),*v=vehicleFromRef(entryProbeCar);
    if(!p||!v){event("probe_entry_drive_aborted",entryProbeRef,7,"participant_missing=1");entryProbeRef=-1;return;}
    // Setup has a separate timeout. Once driving, the bounded source-step
    // profile below owns completion;18 game seconds may be only8 simulation
    // seconds under capture/GPU load and must not silently cut that profile.
    if(!entryProbeAccelerated&&gameTime()-entryProbeStarted>18000){event("probe_entry_drive_aborted",entryProbeRef,7,"opening_timeout=1");entryProbeRef=-1;return;}
    if(entryProbeAccelerated){
        const unsigned age=gameTime()-entryProbeStarted;
        const bool ramp=GetEnvironmentVariableA("GTASA_ENTRY_PRESCRIBED_ACCEL",nullptr,0)!=0;
        entryProbeSimulationSeconds+=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
        const float profileSeconds=ramp?entryProbeSimulationSeconds:age*.001f;
        const float fullSpeedUntil=ramp?10.f+entryProbeRampSeconds:10.f;
        // A prescribed longitudinal speed isolates the grip lifetime from the
        // empty SA car's immediate automatic braking. TEST ONLY: no actor
        // constraint, grip strength, joint or animation is overridden.
        // Lateral/vertical/spin response and every body collision stay live.
        const float rampWeight=ramp?std::min(profileSeconds/entryProbeRampSeconds,1.f):1.f;
        const float target=entryProbeSpeed*rampWeight*(profileSeconds<fullSpeedUntil?1.f:std::max(0.f,1.f-(profileSeconds-fullSpeedUntil)));
        const V forward=normalized(V{matrix(v)->up.x,matrix(v)->up.y,0});
        if(gameTime()-entryProbeReport>=100){
            entryProbeReport=gameTime();
            event("probe_entry_drive_sample",entryProbeRef,7,"elapsed_ms="+std::to_string(age)+" profile_ms="+std::to_string(profileSeconds*1000.f)+" ramp_profile="+std::to_string(ramp)+" ramp_ms="+std::to_string(ramp?entryProbeRampSeconds*1000.f:0.f)+" measured_mps="+std::to_string(length(at<V>(v,0x44))*50.f)+" prescribed_mps="+std::to_string(target)+" travel_m="+std::to_string(length(position(v)-entryProbeOrigin))+" x="+std::to_string(position(v).x)+" y="+std::to_string(position(v).y)+" car_health="+std::to_string(at<float>(v,0x4C0)));
        }
        at<V>(v,0x44)=at<V>(v,0x44)+forward*(target/50.f-dot(at<V>(v,0x44),forward));
        if(profileSeconds>=fullSpeedUntil+1.f){event("probe_entry_car_stopped",entryProbeRef,7,"elapsed_ms="+std::to_string(age)+" prescribed_drive_finished=1");entryProbeRef=-1;}
        return;
    }
    auto*m=taskManager(p);auto*t=m?reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m):nullptr;
    for(int d=0;t&&d<16;d++){
        auto*vt=at<void**>(t,0);const int type=reinterpret_cast<int(__thiscall*)(void*)>(vt[4])(t);
        auto*anim=type==TASK_SIMPLE_CAR_OPEN_DOOR_FROM_OUTSIDE?at<void*>(t,0xC):nullptr;
        const int doorNode=anim?at<int>(t,0x14):0;
        const int doorIndex=doorNode==10?2:doorNode==8?3:doorNode==11?4:doorNode==9?5:-1;
        const float doorAngle=doorIndex>=0?at<float>(v,0x5B8+doorIndex*0x18+0xC)-at<float>(v,0x5B8+doorIndex*0x18+4):0.f;
        if(anim){static unsigned trace=0;if(gameTime()-trace>200){trace=gameTime();event("probe_entry_opening",entryProbeRef,7,"animation_seconds="+std::to_string(at<float>(anim,0x20))+" node="+std::to_string(doorNode)+" angle="+std::to_string(doorAngle));}}
        const bool ramp=GetEnvironmentVariableA("GTASA_ENTRY_PRESCRIBED_ACCEL",nullptr,0)!=0;
        if(ramp&&(!entryProbeNumber("GTASA_ENTRY_RAMP_SECONDS",.75f,.05f,3.f,entryProbeRampSeconds)||
                   !entryProbeNumber("GTASA_ENTRY_START_CLIP_SECONDS",.15f,0.f,1.05f,entryProbeStartClipSeconds))){
            event("probe_entry_parameters_rejected",entryProbeRef,7,"invalid_test_profile=1");entryProbeRef=-1;return;
        }
        if(anim&&(ramp?at<float>(anim,0x20)>=entryProbeStartClipSeconds:std::abs(doorAngle)>.4f)){
            entryProbeAccelerated=true;entryProbeStarted=gameTime();
            entryProbeOrigin=position(v);entryProbeReport=0;entryProbeSimulationSeconds=0;
            if(!ramp)at<V>(v,0x44)=matrix(v)->up*(entryProbeSpeed/50.f);at<unsigned char>(v,0xB8)=0;
            event("probe_entry_car_accelerated",entryProbeRef,7,"speed_mps="+std::to_string(entryProbeSpeed)+" actual_open_door_task=1 ramp_profile="+std::to_string(ramp)+" prescribed_ramp_ms="+std::to_string(ramp?entryProbeRampSeconds*1000.f:0.f)+
                " requested_start_clip_seconds="+std::to_string(ramp?entryProbeStartClipSeconds:-1.f)+" actual_start_clip_seconds="+std::to_string(at<float>(anim,0x20))+
                " group="+std::to_string(at<short>(anim,0xE))+" clip="+std::to_string(at<short>(anim,0x2C))+" door="+std::to_string(doorNode)+
                " prescribed_drive_ms=10000 prescribed_braking_ms=1000 native_propulsion=0");return;
        }
        t=reinterpret_cast<void*(__thiscall*)(void*)>(vt[2])(t);
    }
}
// Native task identity, not proximity or an animation name, grants ownership.
// SA keeps approach/alignment/opening. Once the original IV speed gate passes,
// finish the native task's cleanup and continue its exact evaluated pose in NM.
static void processVehicleEntries(){
    for(auto it=entrySolvedClocks.begin();it!=entrySolvedClocks.end();){const auto a=agents.find(it->first);
        if(a==agents.end()||a->second->agent!=it->second.agent||a->second->start!=it->second.start)it=entrySolvedClocks.erase(it);else ++it;}
    if(!physicalVehicleExits||!recoveryAllowed()||!player()){entryMotion.clear();return;}
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(auto it=entryMotion.begin();it!=entryMotion.end();)if(!pedFromRef(it->first)||frame-it->second.poseFrame>2)it=entryMotion.erase(it);else ++it;
    auto*pool=pedPool();if(!pool)return;
    for(int i=0;i<pool->size;i++){
        if(pool->flags[i]&0x80)continue;void*p=pool->objects+i*0x7C4;const int ref=reference(p);
        // NPC boarding is the requested route. Player entry requires the
        // additional mapped release control; leave that native until present.
        if(p==player()||!validPed(p)||agents.contains(ref)||pending.contains(ref))continue;
        auto*m=taskManager(p);if(!m)continue;
        void*root=reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m);if(!root)continue;
        int slot=-1;for(int k=0;k<5;k++)if(at<void*>(m,k*4)==root){slot=k;break;}if(slot<0)continue;
        void*entry=nullptr;void*leaf=root;int type=-1;
        for(int d=0;leaf&&d<16;d++){
            auto*vt=at<void**>(leaf,0);type=reinterpret_cast<int(__thiscall*)(void*)>(vt[4])(leaf);
            if(type==TASK_COMPLEX_ENTER_CAR_AS_DRIVER||type==TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)entry=leaf;
            auto*sub=reinterpret_cast<void*(__thiscall*)(void*)>(vt[2])(leaf);if(!sub)break;leaf=sub;
        }
        if(!entry||type!=TASK_SIMPLE_CAR_OPEN_DOOR_FROM_OUTSIDE)continue;
        void*v=at<void*>(entry,0xC);const int vr=vehicleRef(v);
        if(vr<0||at<int>(v,0x594)!=0)continue;
        collision->doorVehicles[vr]=gameTime();
        auto pose=captureVehiclePose(p,v);if(!pose)continue;
        pose->velocity=at<V>(p,0x44);pose->angular=at<V>(p,0x50);pose->centre=position(p);
        auto&motion=entryMotion[ref];if(motion.task!=entry||motion.vehicle!=vr)motion=ExitTransit{entry,vr,gameTime()};
        observeNativeVehicleOpening(p,v,entry,leaf,*pose,motion,frame);
        const bool animated=seedExitAnimationMotion(*pose,motion,frame);
        motion.previous=pose;motion.poseFrame=frame;motion.poseTime=gameTime();
        if(!nm_vehicle_entry_grabs(dot(at<V>(v,0x44),at<V>(v,0x44))*2500.f,0))continue;
        // Preflight before touching the native task: valid pose, physics slot,
        // target world/shape. A rejected admission leaves SA entirely in charge.
        if(agents.size()>=size_t(maxActive))continue;
        collision->update(position(player()));const auto*obstacle=collision->findVehicle(vr);if(!obstacle)continue;
        V surface{},normal{};float gap=999.f;int component=0;
        bool probeHit=false;float probeDepth=0,probeRank=0;
        V oldWorldSurface{},oldWorldNormal{};float oldGap=999.f;int oldComponent=-1;
        // IV 0x8241DFA8 -> 0x82367988 probes the ped matrix's translation
        // (+0x20 -> +0x30), NOT its current animated pelvis. SA's opening
        // clip bends that pelvis down while its entity place stays upright;
        // substituting parts[0] therefore lowers the selected door surface.
        const V probePlace=pose->centre;
        for(const auto&part:collision->vehicles)if(part.ref==vr&&part.shape){
            const V probe=mul(inverse(part.frame),T{{},probePlace}).p;V hit{};
            const bool closest=closestVehicleSurface(*part.shape,probe,hit);
            if(closest&&length(hit-probe)<oldGap){
                oldGap=length(hit-probe);oldWorldSurface=mul(part.frame,T{{},hit}).p;
                oldWorldNormal=rotate(part.frame.q,normalized(probe-hit));oldComponent=part.component;
            }
            float original[7]{};const int result=nm_vehicle_shape_sphere(part.shape->id,&probe.x,.6f,original);
            if(result<0){ok(result);continue;}
            if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)){
                const V worldClosest=mul(part.frame,T{{},hit}).p;
                event("vehicle_entry_probe_part",ref,at<short>(p,0x22),"component="+std::to_string(part.component)+" shape="+std::to_string(part.shape->id)+
                    " root_unsigned_gap="+std::to_string(closest?length(hit-probe):-1.f)+" source_hit="+std::to_string(result)+" source_depth="+std::to_string(result==1?original[6]:-1.f)+
                    " left_body_to_root_candidate="+std::to_string(closest?length(pose->parts[16].p-worldClosest):-1.f)+" right_body_to_root_candidate="+std::to_string(closest?length(pose->parts[20].p-worldClosest):-1.f));
            }
            if(result==1){
                const float depth=original[6],rank=depth>.6f?.6f-depth:depth;
                // Original sphere::Slot::offer ranks signed penetration, not
                // absolute distance. Keep its face normal, including backfaces.
                if(!probeHit||rank>probeRank){
                    probeHit=true;probeDepth=depth;probeRank=rank;
                    surface={original[0],original[1],original[2]};normal={original[3],original[4],original[5]};
                    gap=length(surface-probe);component=part.component;
                }
            }
        }
        // Root MakeAbortable also closes the door, a native exit side effect.
        // Snapshot its actual hinge state and restore it after ownership ends;
        // no repositioned hand, added hinge velocity or synthetic open angle.
        struct DoorSnapshot {void*state;std::array<unsigned char,24> bytes;void*frame;M local;};
        std::vector<DoorSnapshot> doors;
        for(int node:{8,9,10,11})if(void*f=at<void*>(v,0x648+node*4)){
            const int door=node==10?2:node==8?3:node==11?4:5;
            void*d=static_cast<unsigned char*>(v)+0x5B8+door*0x18;DoorSnapshot snapshot{d,{},f,at<M>(f,0x10)};
            memcpy(snapshot.bytes.data(),d,24);doors.push_back(snapshot);
        }
        const M nativeFrame=*matrix(p);const V speed=at<V>(p,0x44),spin=at<V>(p,0x50);
        void*opening=at<void*>(leaf,0xC);
        const float openSeconds=opening?at<float>(opening,0x20):-1.f;
        const int openClip=opening?at<short>(opening,0x2C):-1;
        const int openGroup=opening?at<short>(opening,0xE):-1;
        pose->velocity=speed;pose->angular=spin;pose->centre=nativeFrame.pos;
        auto vec=[](V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
        event("vehicle_entry_seed_motion",ref,at<short>(p,0x22),"native_root="+vec(nativeFrame.pos)+" part16="+vec(pose->parts[16].p)+" part16_velocity="+vec(pose->linear[16])+" part16_spin="+vec(pose->spin[16])+" has_part_motion="+std::to_string(pose->hasPartVelocities)+" native_dt="+std::to_string(*reinterpret_cast<float*>(0xB7CB5C)*.02f)+" source_frame="+std::to_string(frame));
        const auto abort=reinterpret_cast<bool(__thiscall*)(void*,void*,int,void*)>(at<void**>(root,0)[6]);
        if(!abort(root,p,2,nullptr)){event("vehicle_entry_native_refused",ref,at<short>(p,0x22));continue;}
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(m,nullptr,slot,true);
        for(auto&d:doors){memcpy(d.state,d.bytes.data(),24);at<M>(d.frame,0x10)=d.local;reinterpret_cast<void*(__cdecl*)(void*)>(0x7F0910)(d.frame);}
        // Abort's SetPedOut is bookkeeping for seated actors and may reposition.
        // This actor was never seated: preserve its pre-abort physical frame.
        *matrix(p)=nativeFrame;at<V>(p,0x44)=speed;at<V>(p,0x50)=spin;
        Request r{{},{0,1,0},gameTime(),0,false,Reaction::DoorEntry,vr,pose,false};
        // Probe at the handoff time, before the native car advances. Delay to
        // the next PreRender must not move the queried surface past the hand.
        r.contactKnown=probeHit;r.contact=surface;r.contactNormal=normal;
        r.entryComponent=component;
        if(const auto*part=collision->findVehicle(vr,component)){
            r.entryFrame=part->frame;
            const V worldHit=mul(part->frame,T{{},surface}).p;
            V wrist{};bool haveWrist=false;
            if(auto*h=hierarchy(p))for(int n=0;h->nodes&&n<h->num&&size_t(n)<pose->bones.size();n++)if(h->nodes[n].id==34){wrist=pose->bones[n].p;haveWrist=true;break;}
            event("vehicle_entry_seed_contact",ref,at<short>(p,0x22),"native_place_z="+std::to_string(probePlace.z)+" physical_pelvis_z="+std::to_string(pose->parts[0].p.z)+" wrist_z="+std::to_string(haveWrist?wrist.z:-999.f)+" wrist_gap="+std::to_string(haveWrist?length(wrist-worldHit):-1.f)+" target_z="+std::to_string(worldHit.z)+" target_local="+std::to_string(surface.x)+","+std::to_string(surface.y)+","+std::to_string(surface.z)+" open_group="+std::to_string(openGroup)+" open_clip="+std::to_string(openClip)+" open_seconds="+std::to_string(openSeconds)+" replay_measured_door_frame=1");
            event("vehicle_entry_probe_comparison",ref,at<short>(p,0x22),"source_hit="+std::to_string(probeHit)+" source_depth="+std::to_string(probeDepth)+" source_rank="+std::to_string(probeRank)+" source_component="+std::to_string(component)+" unsigned_gap="+std::to_string(oldGap)+" unsigned_component="+std::to_string(oldComponent)+" surface_difference="+std::to_string(probeHit&&oldComponent>=0?length(worldHit-oldWorldSurface):-1.f)+" normal_dot="+std::to_string(probeHit&&oldComponent>=0?dot(rotate(part->frame.q,normal),oldWorldNormal):-2.f));
        }
        pending[ref]=r;
        entryMotion.erase(ref);
        event("vehicle_entry_animation_motion",ref,at<short>(p,0x22),"original_start="+std::to_string(animated)+" observed_limb_velocity=1");
        event("vehicle_entry_handoff",ref,at<short>(p,0x22),"native_task="+std::to_string(type)+" vehicle_ref="+std::to_string(vr)+" car_mps="+std::to_string(length(at<V>(v,0x44))*50.f)+" ped_mps="+std::to_string(length(speed)*50.f)+" probe_gap="+std::to_string(gap)+" component="+std::to_string(component)+" added_launch=0");
    }
}
static bool updateVehicleEntryTask(Active&a){
    if(a.kind!=Reaction::DoorEntry)return true;
    float status[4]{};const int ready=nm_agent_vehicle_entry_status(a.agent,status);if(ready<0){ok(ready);return false;}
    const int phase=int(status[0]),holding=int(status[2]);
    // Bound the dense diagnostics to the initial transition. They observe the
    // solved point and actual articulated-door velocity, including the frame
    // where the finite grip breaks; no changes to physical state or lifetime.
    if(gameTime()-a.start<1500){
        float points[12]{},load[4]{};const int mask=nm_agent_grab_points(a.agent,points);
        nm_agent_vehicle_entry_load(a.agent,load);
        auto vec=[](V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
        std::string detail="age_ms="+std::to_string(gameTime()-a.start)+" simulated_seconds="+std::to_string(entrySolvedSeconds(a))+" held_mask="+std::to_string(mask)+" peak_impulse="+std::to_string(load[0])+" strength="+std::to_string(load[1])+" breaks="+std::to_string(int(load[3]))+" incoming_part16="+vec(a.incoming[16].p)+" current_part16="+vec(a.current[16].p)+" native_root="+vec(position(a.ped))+" residual_seconds="+std::to_string(a.accumulator)+" static_velocity_baseline="+std::to_string(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&GetEnvironmentVariableA("GTASA_GRIP_STATIC_BASELINE",nullptr,0));
        if(const auto*v=collision->findVehicle(a.vehicle,a.entryComponent)){
            T solver=v->frame;if(auto h=a.vehicleHistory.find(v->identity());h!=a.vehicleHistory.end())solver=h->second.frame;
            detail+=" door_centre="+vec(v->frame.p)+" solver_door_centre="+vec(solver.p)+" door_velocity="+vec(v->velocity)+" door_spin="+vec(v->spin);
            if(a.entryComponent>=8&&a.entryComponent<=11){const int node=a.entryComponent,door=node==10?2:node==8?3:node==11?4:5;void*d=static_cast<unsigned char*>(v->entity)+0x5B8+door*0x18;
                detail+=" hinge_angle="+std::to_string(at<float>(d,0xC))+" hinge_previous="+std::to_string(at<float>(d,0x10))+" native_dt="+std::to_string(*reinterpret_cast<float*>(0xB7CB5C)*.02f);}
            if(mask&1){
                const V held{points[0],points[1],points[2]},target{points[3],points[4],points[5]};V linear{},angular{};
                nm_agent_get_part_velocity(a.agent,16,&linear.x,&angular.x);
                V centreVelocity=v->velocity;
                if(!v->component&&matrix(v->entity))centreVelocity=centreVelocity+cross(v->spin,v->frame.p-point(*matrix(v->entity),at<V>(v->entity,0xA4)));
                const V anchorVelocity=centreVelocity+cross(v->spin,target-solver.p),handVelocity=linear+cross(angular,held-a.current[16].p);
                detail+=" held="+vec(held)+" target="+vec(target)+" gap="+std::to_string(length(held-target))+" hand_velocity="+vec(handVelocity)+" target_velocity="+vec(anchorVelocity)+" relative_speed="+std::to_string(length(handVelocity-anchorVelocity));
            }
        }
        event("vehicle_entry_transition_trace",a.ref,a.model,detail);
    }
    if(gameTime()-a.lastVehicleContactReport>=100&&!ready){
        a.lastVehicleContactReport=gameTime();float contact[10]{},load[4]{};
        // Decompose a skin/contact discrepancy into physical anchor separation
        // and native skeletal reconstruction. Virtual torso bends have no SA
        // skin joint; this measurement must precede any retargeting decision.
        const auto chainSkin=a.rig->physicalBones(a.current);
        auto vec=[](V x){return std::to_string(x.x)+","+std::to_string(x.y)+","+std::to_string(x.z);};
        for(int k:{0,7,8,9,10,11,13,14,15,16}){
            const T joint=mul(a.current[k],inverse(a.rig->offsets[k]));
            V parentPivot{};Q parentFrame{};int parent=-1;float limits[3]{};
            const int type=nm_agent_get_joint(a.agent,k,&parent,&parentPivot.x,&parentFrame.x,limits,nullptr);
            float virtualTurn=-1.f;
            if((k==8||k==10)&&parent>=0){
                const T parentJoint=mul(a.current[parent],inverse(a.rig->offsets[parent]));
                virtualTurn=2.f*std::acos(std::clamp(std::abs(mul(conjugate(parentJoint.q),joint.q).w),0.f,1.f))*57.2957795f;
            }
            const int node=a.rig->node[k];V residual{};float error=-1.f;
            if(node>=0&&size_t(node)<chainSkin.size()){
                residual=chainSkin[node].p-mul(a.current[k],a.rig->bodyToBone[node]).p;error=length(residual);
            }
            event("vehicle_entry_rig_part",a.ref,a.model,"part="+std::to_string(k)+" parent="+std::to_string(parent)+" body="+vec(a.current[k].p)+" joint="+vec(joint.p)+
                " parent_anchor="+vec(parentPivot)+" anchor_gap="+std::to_string(type>0?length(joint.p-parentPivot):-1.f)+" virtual_turn_deg="+std::to_string(virtualTurn)+
                " fk_error="+std::to_string(error)+" fk_residual="+vec(residual));
        }
        nm_agent_vehicle_entry_load(a.agent,load);
        if(nm_agent_vehicle_entry_contact(a.agent,contact)==0)
            event("vehicle_entry_contact",a.ref,a.model,"simulated_seconds="+std::to_string(entrySolvedSeconds(a))+" active="+std::to_string(int(contact[0]))+" grips="+std::to_string(int(contact[1]))+" left_gap="+std::to_string(contact[2])+" right_gap="+std::to_string(contact[3])+" target="+std::to_string(contact[4])+","+std::to_string(contact[5])+","+std::to_string(contact[6])+" peak_impulse="+std::to_string(load[0])+" strength="+std::to_string(load[1])+" break_impulse="+std::to_string(load[2])+" breaks="+std::to_string(int(load[3])));
        float points[12]{};const int mask=nm_agent_grab_points(a.agent,points);
        if(mask>0){
            const auto solvedSkin=a.rig->physicalBones(a.current);
            for(int side=0;side<2;side++)if(mask&(1<<side)){
                const int part=side?20:16,node=a.rig->node[part];const int base=side*6;
                if(node<0||size_t(node)>=solvedSkin.size())continue;
                const V held{points[base],points[base+1],points[base+2]},target{points[base+3],points[base+4],points[base+5]};
                const V reconstructed=gripPointOnSkin(held,a.current[part],a.rig->bodyToBone[node],solvedSkin[node]);
                float displayedGap=-1,displayedDelta=-1;
                if(a.lastRendered.size()==solvedSkin.size()){
                    const V displayed=gripPointOnSkin(held,a.current[part],a.rig->bodyToBone[node],a.lastRendered[node]);
                    displayedGap=length(displayed-target);displayedDelta=length(displayed-reconstructed);
                }
                event("vehicle_entry_skin_contact",a.ref,a.model,"side="+std::to_string(side)+" solver_gap="+std::to_string(length(held-target))+" fk_binding_error="+std::to_string(length(reconstructed-held))+" displayed_gap="+std::to_string(displayedGap)+" display_delta="+std::to_string(displayedDelta)+" displayed_age_ms="+std::to_string(gameTime()-a.lastRenderTime)+" held_z="+std::to_string(held.z)+" target_z="+std::to_string(target.z));
            }
        }
    }
    if(phase!=a.entryPhase||holding!=a.entryHolding||bool(ready)!=a.entryReady){
        if(holding&&!a.grabConfirmed){
            a.grabConfirmed=true;grabFeedback++;
            if(a.ref==entryProbeRef&&GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))
                for(unsigned delay:{17u,150u,500u,1000u,2000u,4000u,6000u,8000u,10000u})meleeFrames.push_back({gameTime()+delay,gameTime(),a.ref,"probe_entry_grip_frame"});
        }
        a.entryPhase=phase;a.entryHolding=holding;a.entryReady=ready!=0;
        event("vehicle_entry_state",a.ref,a.model,"phase="+std::to_string(phase)+" holding="+std::to_string(holding)+" ready="+std::to_string(ready)+" released="+std::to_string(int(status[3]))+" seconds="+std::to_string(status[1])+" simulated_seconds="+std::to_string(entrySolvedSeconds(a)));
    }
    return ready!=0;
}

// Called after the current render palette is computed, so the observation does
// not compare last frame's hand against this frame's car. Transform the actual
// solved grip point into the current rendered door component at the same time.
static void observeVehicleEntryRender(Active&a,const std::vector<T>&rendered){
    if(a.kind!=Reaction::DoorEntry||gameTime()!=a.lastVehicleContactReport)return;
    const auto*v=collision->findVehicle(a.vehicle,a.entryComponent);if(!v)return;
    const auto history=a.vehicleHistory.find(v->identity());if(history==a.vehicleHistory.end())return;
    float points[12]{};const int mask=nm_agent_grab_points(a.agent,points);if(mask<=0)return;
    T displayedDoor=v->frame;
    if(a.entryComponent>=8&&a.entryComponent<=11)if(void*car=vehicleFromRef(a.vehicle))if(void*f=at<void*>(car,0x648+a.entryComponent*4))
        if(const auto*s=doorShape(car,a.entryComponent,f))if(auto*m=reinterpret_cast<M*(__cdecl*)(void*)>(0x7F0990)(f))displayedDoor={rotation(*m),point(*m,s->centre)};
    for(int side=0;side<2;side++)if(mask&(1<<side)){
        const int part=side?20:16,node=a.rig->node[part],base=side*6;
        if(node<0||size_t(node)>=rendered.size())continue;
        const V held{points[base],points[base+1],points[base+2]},target{points[base+3],points[base+4],points[base+5]};
        const V local=mul(inverse(history->second.frame),T{{},target}).p;
        const V targetOnDoor=mul(displayedDoor,T{{},local}).p;
        const V onSkin=gripPointOnSkin(held,a.current[part],a.rig->bodyToBone[node],rendered[node]);
        event("vehicle_entry_render_contact",a.ref,a.model,"side="+std::to_string(side)+" same_frame_gap="+std::to_string(length(onSkin-targetOnDoor))+" solver_gap="+std::to_string(length(held-target))+" hand_vs_solver="+std::to_string(length(onSkin-held))+" door_vs_solver="+std::to_string(length(targetOnDoor-target))+" render_alpha="+std::to_string(1.f)+" native_component="+std::to_string(a.entryComponent));
    }
}
