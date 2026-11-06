#pragma once
// Messages drive the real NM behaviours. Vehicle identities always include the
// SA pool generation; array indices are refreshed before any solver step.
static bool sendMessage(NmAgent* agent){
    const int result=nm_msg_send(agent,collision->world);
    if(result==0)return true;
    if(result<0)return ok(result);
    errors++;event("unimplemented_message_parameter",-1,-1,std::to_string(result));return false;
}
static void stopBehaviour(Active& a,const char* name){
    ok(nm_msg_begin(a.agent,name));ok(nm_msg_set_bool(a.agent,"start",false));sendMessage(a.agent);
}
static void startReaction(Active& a,const Request& request){
    ok(nm_agent_vehicle_entry_cancel(a.agent));a.entryPhase=-1;a.entryHolding=-1;a.entryReady=false;
    ok(nm_agent_ped_bump_cancel(a.agent));a.bumpReady=false;
    a.meleeTaskOriginal=false;
    ok(nm_agent_vehicle_exit_cancel(a.agent));a.exitPhase=-1;a.exitReady=false;
    ok(nm_agent_road_cancel(a.agent,collision->world));
    a.roadReaction=0;a.roadRelaxLogged=false;
    a.stableFor=0;
    a.kind=request.kind;a.vehicle=request.vehicle;a.preview=request.kind==Reaction::Brace;
    a.physicalAnimation=request.physicalAnimation;a.animationDriveStatus=-1;
    a.roadDamage=request.roadDamage;a.resumeDeliveredRoadDamage=false;
    a.combatDamage=request.combatDamage;a.resumeDeliveredCombatDamage=false;
    a.roadFalling=false;
    if(applyBones&&!a.mounted&&!a.preview)at<unsigned char>(a.ped,0x1C)&=~1u;
    const V centre=a.current[std::clamp(request.part,0,20)].p;
    const V hit=request.contactKnown&&length(request.contact-centre)<.65f?request.contact:centre;
    V impulse=request.impulse;
    float inverseMass=0;
    if(request.kind==Reaction::Melee){
        a.meleeReceived=request.time;a.meleeRendered=false;
        const T head=mul(inverse(a.current[10]),a.current[12]);
        a.meleeMotionSample=0;a.meleeHeadLocal=head.p;a.meleeHeadRotation=head.q;a.meleePelvis=a.current[0].p;
        a.meleeAttacker=request.attackerRef;a.meleeWeapon=request.weapon;a.meleeHandContactLogged=false;
        a.melee=request.melee;
        V threat=hit-request.direction;
        if(void*attacker=pedFromRef(request.attackerRef))if(auto*h=hierarchy(attacker)){
            for(int i=0;h->nodes&&h->matrices&&i<h->num;i++)if(h->nodes[i].id==saBone::neck){
                const V neck=h->matrices[i].pos;
                if(finite(neck)&&length(neck-position(attacker))<3.f)threat=neck;
                break;
            }
        }
        // Preserve the original dispatch between Balance and Flinch. Forcing
        // every light hit to Balance made unrelated blows share its arms-out
        // posture. Neither path orders a fall; support and inertia decide it.
        const bool grounded=!a.preview&&(a.current[10].p.z-a.current[0].p.z<.25f||at<float>(a.ped,0x540)<=0);
        if(grounded){stopBehaviour(a,"bodyBalance");ok(nm_agent_task_catch_fall(a.agent,"*"));}
        // GTA IV's dispatcher passes the DEFAULT NMBalance set (peds.rs
        // 2955..2961), not NMBalance MELEE, whose arm/neck stiffness is zero.
        else {a.meleeTaskOriginal=true;ok(nm_agent_melee_task(a.agent,collision->world,&threat.x,at<float>(a.ped,0x540)*2.f,3,
            (request.melee==MeleeResponse::Flinch?1u:0u)|(a.ped==player()?2u:0u)));}
        event(grounded?"melee_ground_reaction":"melee_balance_reaction",a.ref,a.model,grounded?"fallen_body_keeps_inertia":request.melee==MeleeResponse::Balance?"standing_contact_balance":request.melee==MeleeResponse::Shove?"two_hand_push_balance":"full_body_nm_flinch");
    }else if(request.kind==Reaction::Bump){
        a.meleeAttacker=request.attackerRef;
        void*other=pedFromRef(request.attackerRef);const V peer=other?position(other):hit-request.direction;V look=peer;
        if(other)if(auto*h=hierarchy(other))for(int n=0;h->nodes&&h->matrices&&n<h->num;n++)if(h->nodes[n].id==saBone::head&&finite(h->matrices[n].pos)){look=h->matrices[n].pos;break;}
        const V own=position(a.ped);const float points[]={peer.x,peer.y,peer.z,look.x,look.y,look.z,own.x,own.y,own.z};
        const unsigned flags=(other==player()?1u:0u)|(a.ped==player()?2u:0u);
        const int answer=nm_agent_ped_bump(a.agent,collision->world,points,at<float>(a.ped,0x540)*2.f,request.bumpImpulse,request.bumpDt,flags);
        ok(answer);event("standing_bump_started",a.ref,a.model,"other="+std::to_string(request.attackerRef)+" original_bump_balance="+std::to_string(answer)+" added_impulse=0");
    }else if(request.kind==Reaction::Mounted){
        V threat=hit-request.direction*2.f;
        ok(nm_agent_task_flinch(a.agent,a.mountedWorld,"*",&threat.x));
    }else if(request.kind==Reaction::Shot){
        ok(nm_agent_task_hit(a.agent,collision->world,"POWERFUL_WEAPON",request.part,&hit.x,&request.impulse.x,std::clamp(at<float>(a.ped,0x540),0.f,100.f)));
    }else if(request.kind==Reaction::Impact){
        // The road task wakes on actual solved contact. Selecting catchFall
        // from car speed bypassed bump::react and original roll/relax entirely.
        ok(nm_agent_road_arm(a.agent,collision->world));
    }else if(request.kind==Reaction::DoorEntry){
        a.entryComponent=request.entryComponent;
        const auto*v=collision->findVehicle(request.vehicle,a.entryComponent);
        if(v){
            // Original boarding probe: 0.6m sphere about the native entity place,
            // on actual SA COL surfaces, expressed in the collider's frame.
            const V root=request.pose?request.pose->centre:position(a.ped);
            const V hit=request.contact,normal=request.contactNormal;const bool touched=request.contactKnown;
            const T frame=request.pose?request.entryFrame:v->frame;
            V direction=touched?mul(frame,T{{},hit}).p-root:rotate(frame.q,{0,1,0});direction.z=0;direction=normalized(direction);
            const unsigned slot=at<unsigned char>(a.ped,0x718);const int weapon=slot<13?at<int>(a.ped,0x5A0+slot*0x1C):-1;
            ok(nm_agent_vehicle_entry_start(a.agent,collision->world,v->index,touched?&hit.x:nullptr,touched?&normal.x:nullptr,&direction.x,
                at<float>(a.ped,0x540)*2.f,3.f,(a.ped==player()?1u:0u)|(weapon>=22&&weapon<=34?2u:0u)));
            if(touched)grabStarts++;
            event("vehicle_entry_task_started",a.ref,a.model,"vehicle_ref="+std::to_string(v->ref)+" probe_hit="+std::to_string(touched)+" original_controller=1 velocity_preserved=1 component="+std::to_string(a.entryComponent));
        }
    }else if(request.kind==Reaction::VehicleExit){
        const V inherited=request.pose?request.pose->velocity*50.f:at<V>(a.ped,0x44)*50.f;
        const unsigned slot=at<unsigned char>(a.ped,0x718);
        const int weapon=slot<13?at<int>(a.ped,0x5A0+slot*0x1C):-1;
        const unsigned flags=(a.ped==player()?1u:0u)|(weapon>=22&&weapon<=34?2u:0u);
        const int phase=nm_agent_vehicle_exit_start(a.agent,collision->world,&inherited.x,at<float>(a.ped,0x540)*2.f,3.f,flags);
        if(phase<0)ok(phase);
        event("vehicle_exit_task_started",a.ref,a.model,"task="+std::string(phase==1?"balance":"jump_roll")+" speed_mps="+std::to_string(length(inherited))+" velocity_seed_only=1 original_task_parameters=1");
    }else if(request.kind==Reaction::Brace){
        ok(nm_agent_road_preview(a.agent,collision->world,at<float>(a.ped,0x540)<55.f?1:0));
        a.brace=true;braceStarts++;
        event("vehicle_brace_started",a.ref,a.model,"vehicle_ref="+std::to_string(a.vehicle)+" original_task_parameters=1");
    }else if(request.kind==Reaction::BikeFall&&!a.mounted&&at<float>(a.ped,0x540)>0){
        // IV thrown off a bike (0x824662D0 past the NM gate, reason 14): the
        // jump roll from road vehicle with the bike's velocity, never balance,
        // 2..30 s; its finish (Flinch's) hands the body back.
        const V inherited=request.pose?request.pose->velocity*50.f:at<V>(a.ped,0x44)*50.f;
        if(ok(nm_agent_bike_throw_start(a.agent,collision->world,&inherited.x,at<float>(a.ped,0x540)*2.f,a.ped==player()?1u:0u)))
            event("bike_throw_task_started",a.ref,a.model,"task=jump_roll speed_mps="+std::to_string(length(inherited))+" original_times=2_30 added_launch=0");
    }else if(request.kind==Reaction::BikeFall&&at<float>(a.ped,0x540)<=0){
        // Dead coming off the bike, on its ragdoll with no NM reaction of its own: CTaskComplexDie relaxes it
        // (NMRelax, 0x824CAFF8) and CTaskSimpleDead relaxes at 97. catchFall held the corpse's arms out stiff.
        ok(nm_agent_dead_relax(a.agent));
        event("dead_rider_relax_started",a.ref,a.model,"relax=97 original_dead_task=1");
    }else if(request.kind==Reaction::BikeFall||request.kind==Reaction::Explosion||length(request.impulse)>85.f||at<float>(a.ped,0x540)<=0){
        stopBehaviour(a,"bodyBalance");
        ok(nm_agent_task_catch_fall(a.agent,"*"));
    }else{
        V look=hit+request.direction;
        ok(nm_agent_task_balance(a.agent,collision->world,"*",&look.x));
    }
    // NM's hit task uses bulletVel for behaviour direction. Original authored
    // blows use their force record below; legacy native events retain their
    // measured collision impulse until their event payload is fully mapped.
    if(request.kind==Reaction::Melee&&!request.originalMeleeBlow&&request.contactKnown&&request.strikingMass>0){
        if(ok(nm_agent_get_contact_inverse_mass(a.agent,request.part,&hit.x,&request.contactNormal.x,&inverseMass)))
            impulse=contactMomentum(request.strikeVelocity,request.contactNormal,request.strikingMass,1.f/inverseMass);
    }
    // LSC4 peds.rs: melee's accepted impulse is a force integrated for 0.15s.
    // Preserve total momentum and fixed-step accounting; do not add a second
    // one-frame kick, overwrite a pending hit, or clamp the solver afterwards.
    if(request.kind==Reaction::Melee){
        float seconds=.15f;
        if(request.originalMeleeBlow){
            seconds=originalMeleePushSeconds(a.meleePushes.empty()?0.f:a.meleePushes.back().left);
            impulse=originalMeleeImpulse(request.contactNormal);
            a.meleePushes.clear(); // one original force record, not additive hit timers
        }
        if(length(impulse)>0)a.meleePushes.push_back({request.part,mul(inverse(a.current[request.part]),T{{},hit}).p,impulse*(1.f/seconds),seconds,request.time,{},seconds});
    }else ok(nm_agent_apply_impulse(a.agent,request.part,&hit.x,&impulse.x));
    if(request.kind==Reaction::Melee)event("melee_scheduled_impulse",a.ref,a.model,"hit_ms="+std::to_string(request.time)+" part="+std::to_string(request.part)+" measured="+std::to_string(request.contactKnown)+" impulse="+std::to_string(length(impulse))+" ix="+std::to_string(impulse.x)+" iy="+std::to_string(impulse.y)+" iz="+std::to_string(impulse.z)+" inverse_effective_mass="+std::to_string(inverseMass)+" original_blow="+std::to_string(request.originalMeleeBlow));
    if(length(request.deltaVelocity)>0)for(int i=0;i<21;i++){
        V linear{},angular{};nm_agent_get_part_velocity(a.agent,i,&linear.x,&angular.x);linear=linear+request.deltaVelocity;
        ok(nm_agent_set_part_state(a.agent,i,&a.current[i].p.x,&a.current[i].q.x,&linear.x,&angular.x));
    }
}
static bool updatePedBumpTask(Active&a){
    if(a.kind!=Reaction::Bump&&!(a.kind==Reaction::Melee&&a.meleeTaskOriginal))return true;
    ok(nm_agent_contact_health(a.agent,at<float>(a.ped,0x540)*2.f));
    float status[4]{};const int ready=nm_agent_ped_bump_status(a.agent,status);
    if(ready<0){ok(ready);return false;}
    if(ready&&!a.bumpReady){a.bumpReady=true;event(a.kind==Reaction::Melee?"melee_original_task_finished":"standing_bump_task_finished",a.ref,a.model,"original_finish=1 seconds="+std::to_string(status[1])+" failed="+std::to_string(status[3]));}
    return ready!=0;
}
static void releaseVehicle(Active& a,const char* why){
    if(a.brace)stopBehaviour(a,"braceForImpact");
    if(a.brace||a.grabbing)stopBehaviour(a,"stayUpright");
    if(a.grabbing)stopBehaviour(a,"grab");
    if(a.brace||a.grabbing)event("vehicle_reaction_released",a.ref,a.model,why);
    a.brace=false;a.grabbing=false;a.grabConfirmed=false;a.vehicle=-1;
    if(a.preview)a.start=gameTime()-unsigned(std::max(0.f,activeSeconds-blendSeconds)*1000);
}
static void updateMeleeThreat(Active&a){
    if((a.kind!=Reaction::Melee&&a.kind!=Reaction::Bump)||a.recovering||at<float>(a.ped,0x540)<=0||a.current[10].p.z-a.current[0].p.z<.25f)return;
    int phase=-1;
    if(a.kind==Reaction::Bump||a.meleeTaskOriginal){
        float status[4]{};if(nm_agent_ped_bump_status(a.agent,status)<0)return;
        phase=int(status[0]);if(phase!=1&&!(a.kind==Reaction::Melee&&phase==3))return;
    }
    auto*p=pedFromRef(a.meleeAttacker);if(!p||!matrix(p)||length(position(p)-position(a.ped))>5.f)return;
    auto*h=hierarchy(p);if(!h||!h->nodes||!h->matrices)return;
    const bool flinch=phase>=0?phase==3:(a.kind==Reaction::Melee&&a.melee==MeleeResponse::Flinch);
    // IV Balance control (8261A530) only re-aims the head look each frame.
    // The bump's lean 0.4 keeps the one-second end set at its start
    // (82611900); re-arming it every frame while the player's contact held
    // the clock pushed the victim on for the whole task (side case: 1.9 ->
    // 2.7 m/s after the lean's end, then a fall; SDK test: 1.9 -> 3.0 m).
    const int bone=flinch?saBone::neck:saBone::head;
    for(int n=0;n<h->num;n++)if(h->nodes[n].id==bone){
        const V at=h->matrices[n].pos;if(!finite(at)||length(at-position(p))>3.f)return;
        // Same ongoing target update as NM task_control; no restart of the
        // flinch, random draw, stiffness change, or imposed limb pose.
        ok(nm_msg_begin(a.agent,flinch?"upperBodyFlinch":"bodyBalance"));
        ok(nm_msg_set_vec3(a.agent,flinch?"pos":"headLookPos",&at.x));sendMessage(a.agent);return;
    }
}
static void releaseLostRoadVehicle(Active& a){
    ok(nm_agent_road_cancel(a.agent,collision->world));
    ok(nm_agent_task_catch_fall(a.agent,"*"));
    a.roadReaction=3;a.roadFalling=true;a.brace=false;a.grabbing=false;
    event("road_vehicle_lost",a.ref,a.model,"brace_released_velocity_preserved=1");
}
static V vehicleLookPoint(const CollisionWorld::Vehicle& v){
    void*driver=at<void*>(v.entity,0x460);
    if(driver&&reference(driver)>=0)if(auto*h=hierarchy(driver))
        for(int n=0;h->nodes&&h->matrices&&n<h->num;n++)if(h->nodes[n].id==saBone::head&&finite(h->matrices[n].pos))return h->matrices[n].pos;
    return v.frame.p;
}
static V vehicleBracePoint(const Active& a,const CollisionWorld::Vehicle& v){
    // NMBrace probes this car alone from the ped root +0.4 toward its centre.
    // Nearest-point selection can jump between roof/bonnet/bumper surfaces and
    // is not the reference task's line-of-sight target. Let SA query its COL.
    struct Line {V from;float fromSize;V to;float toSize;};
    static_assert(sizeof(Line)==0x20);
    auto*m=matrix(v.entity);
    auto*c=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v.entity);
    Line line{a.current[0].p+V{0,0,.4f},0,v.frame.p,0};ColPoint hit{};float fraction=1.f;
    if(m&&c&&c->data&&reinterpret_cast<bool(__cdecl*)(const Line&,const M&,ColModel&,ColPoint&,float&,bool,bool)>(0x417950)
        (line,*m,*c,hit,fraction,false,false)&&finite(hit.point))return hit.point;
    return v.frame.p-V{0,0,.3f}; // original task fallback when the car probe misses
}
static void updateVehicleReaction(Active& a){
    if(a.mounted)return;
    if(a.vehicle<0)return;
    const auto* v=collision->findVehicle(a.vehicle);
    if(a.kind==Reaction::DoorEntry){
        collision->doorVehicles[a.vehicle]=gameTime();
        // Generation-checked indexes can change whenever the local COL world
        // rebuilds; never let a live grip transfer to another pooled vehicle.
        const auto*part=collision->findVehicle(a.vehicle,a.entryComponent);
        const int index=part?part->index:-1;
        if(index!=a.entryIndex){
            event("vehicle_entry_collider_rebound",a.ref,a.model,"vehicle="+std::to_string(a.vehicle)+
                " component="+std::to_string(a.entryComponent)+" old_index="+std::to_string(a.entryIndex)+" new_index="+std::to_string(index));
            a.entryIndex=index;
        }
        ok(nm_agent_vehicle_entry_follow(a.agent,part?part->index:-1,v?dot(v->velocity,v->velocity):0.f,!part,at<float>(a.ped,0x540)*2.f));
        return;
    }
    if(a.kind==Reaction::Impact){
        // Roll/relax stays in the SDK even after the car has driven away.
        if(a.recovering||a.roadReaction!=1)return;
        if(!v){releaseLostRoadVehicle(a);return;}
        const V hit=vehicleBracePoint(a,*v);
        const V look=vehicleLookPoint(*v);
        ok(nm_agent_road_brace_control(a.agent,collision->world,v->index,&hit.x,&v->frame.p.x,&look.x));
        return;
    }
    if(!v||a.recovering||at<float>(a.ped,0x540)<=0){releaseVehicle(a,"vehicle_lifecycle_or_recovery");return;}
    const float speed=length(v->velocity);
    const V local=mul(inverse(v->frame),T{{},a.current[10].p}).p;
    const float age=(gameTime()-a.start)*.001f;
    if(a.roadFalling)return;
    if(speed>9.f||std::abs(local.x)>v->half.x+2.5f||std::abs(local.y)>v->half.y+2.5f||age>5.f){releaseVehicle(a,"speed_distance_or_hold_limit");return;}
    if(a.kind!=Reaction::Brace&&a.kind!=Reaction::Impact)return;
    // Only approaching, upright peds brace; a fast collision stays catchFall.
    if(!a.brace&&!a.grabbing&&(speed>6.f||a.current[10].p.z-a.current[0].p.z<.2f))return;
    V target{std::clamp(local.x,-v->half.x,v->half.x),std::clamp(local.y,-v->half.y,v->half.y),std::clamp(local.z,-v->half.z,v->half.z)};
    // The bonnet is below the roof's enclosing box. Reach toward a real COL
    // surface; otherwise the behaviour holds both palms in empty space.
    if(!v->shape||!closestVehicleSurface(*v->shape,local,target))return;
    if(gameTime()-a.lastVehicleContactReport>=300){
        a.lastVehicleContactReport=gameTime();float gap[2]={99,99};int n=0;
        for(int hand:{16,20}){const V p=mul(inverse(v->frame),a.current[hand]).p;V surface;
            if(closestVehicleSurface(*v->shape,p,surface))gap[n]=length(surface-p)-a.rig->partRadius[hand];n++;}
        event("vehicle_surface_contact",a.ref,a.model,"shape="+std::to_string(v->shape->id)+" primitives="+std::to_string(v->shape->primitives.size())+" speed_mps="+std::to_string(speed)+" left_gap_m="+std::to_string(gap[0])+" right_gap_m="+std::to_string(gap[1]));
    }
    if(!a.grabbing){
    const V hit=vehicleBracePoint(a,*v),look=vehicleLookPoint(*v);
    ok(nm_agent_road_brace_control(a.agent,collision->world,v->index,&hit.x,&v->frame.p.x,&look.x));
    }
    // Door grabs belong to the real native opening task in VehicleEntries.
    // Bracing against a body panel must not acquire an invented handle.
}
static bool updateVehicleExitTask(Active& a){
    if(a.kind!=Reaction::VehicleExit&&a.kind!=Reaction::BikeFall)return true;
    float state[4]{};const int phase=nm_agent_vehicle_exit_status(a.agent,state);
    if(a.kind==Reaction::BikeFall&&phase==0)return true; // mounted or dead: no thrown-off task
    if(phase<0){ok(phase);return false;}
    const bool ready=state[2]>0;
    if(a.exitPhase!=phase||a.exitReady!=ready){
        a.exitPhase=phase;a.exitReady=ready;
        event("vehicle_exit_task_phase",a.ref,a.model,"phase="+std::to_string(phase)+" simulated_seconds="+std::to_string(state[1])+" recovery_ready="+std::to_string(ready)+" inherited_speed_mps="+std::to_string(state[3]));
    }
    return ready;
}
static void selectSolvedRoadReaction(Active& a,float dt){
    if(a.roadReaction==2&&!a.roadRelaxLogged&&nm_agent_is_relaxing(a.agent)==1){
        a.roadRelaxLogged=true;event("road_roll_relaxed",a.ref,a.model,"original_timed_controller=1 velocity_preserved=1");
    }
    if(a.kind!=Reaction::Impact||a.preview||a.recovering||a.roadReaction>=2)return;
    const auto*v=collision->findVehicle(a.vehicle);
    if(!v){
        // Never keep a brace's instance index after its pool identity vanished.
        releaseLostRoadVehicle(a);return;
    }
    // CVehicle::m_pDriver at 0x460; native ownership and native health remain SA's.
    const unsigned flags=(at<void*>(v->entity,0x460)==player()?1u:0u)|(a.ped==player()?2u:0u);
    float report[4]{};
    const int choice=nm_agent_road_contact(a.agent,collision->world,v->index,at<float>(a.ped,0x540)*2.f,flags,dt,report);
    if(choice<0){ok(choice);return;}if(!choice){updateVehicleReaction(a);return;}
    a.roadReaction=choice;a.roadFalling=choice>=2;a.brace=choice==1;a.grabbing=false;a.grabConfirmed=false;
    event("road_solved_reaction",a.ref,a.model,"task="+std::string(choice==1?"brace":choice==2?"roll_relax":"relax")+
        " vehicle_ref="+std::to_string(v->ref)+" solved_ns="+std::to_string(report[0])+" part="+std::to_string(int(report[1]))+
        " roll_ms="+std::to_string(int(report[2]))+" minimum_ms="+std::to_string(int(report[3]))+" health_sa="+std::to_string(at<float>(a.ped,0x540))+
        " native_damage_preserved=1 added_impulse=0");
    // The world still has the exact obstacle map that generated this contact.
    updateVehicleReaction(a);
}
static void anchorAnimation(Active& a,bool pinHands){
    // Transport the upper body with the evaluated animation. Before impact,
    // SA retains walking/avoidance; on a bike it retains the seat and hands.
    // Physical control is transferred only on a hit, ejection or confirmed grip.
    const T delta=mul(a.incoming[0],inverse(a.lastSeat));a.lastSeat=a.incoming[0];
    V velocity=at<V>(a.ped,0x44)*50.f,zero{};
    for(int i=0;i<21;i++){
        T pose=mul(delta,a.current[i]);V linear{},angular{};
        nm_agent_get_part_velocity(a.agent,i,&linear.x,&angular.x);
        linear=rotate(delta.q,linear);angular=rotate(delta.q,angular);
        if(i<=6||(pinHands&&(i==16||i==20))){pose=a.incoming[i];linear=velocity;angular=zero;}
        ok(nm_agent_set_part_state(a.agent,i,&pose.p.x,&pose.q.x,&linear.x,&angular.x));a.current[i]=pose;
    }
}
static void readVehicleFeedback(Active& a){
    if(!a.grabbing)return;
    for(int i=0;i<nm_agent_feedback_count(a.agent);i++){
        char name[128]={};int slot=0;float value=0;nm_agent_feedback_get(a.agent,i,name,128,&slot,&value);
        if((std::string(name).find("LeftArmGrab")!=std::string::npos||std::string(name).find("RightArmGrab")!=std::string::npos)&&!a.grabConfirmed){
            event("door_grab_feedback",a.ref,a.model,std::string(name)+" value="+std::to_string(value));grabFeedback++;a.grabConfirmed=true;
            if(a.preview){a.preview=false;if(applyBones)at<unsigned char>(a.ped,0x1C)&=~1u;event("grip_takes_physical_control",a.ref,a.model);}
        }
    }
}
static void scanApproachingCars(){
    static unsigned lastScan=0;static std::map<int,unsigned> cooldown;
    const auto now=gameTime();if(now-lastScan<150)return;lastScan=now;
    if(!detectImpacts||!player()||area()!=0||int(agents.size()+pending.size())>=maxActive)return;
    collision->update(physicsWorldOrigin());auto*pool=pedPool();if(!pool)return;
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        auto*p=pool->objects+i*0x7C4;const int ref=(i<<8)|pool->flags[i];
        if(!validPed(p)||at<unsigned char>(p,0x484)!=1||agents.contains(ref)||pending.contains(ref)||now-cooldown[ref]<6000)continue;
        for(const auto&v:collision->vehicles){
            // Cars, motorbikes and bicycles (subtype 0, 9, 10; each bike's chassis
            // row): a slow bike rolling into a ped gets the same hand on it.
            if(const int subtype=at<int>(v.entity,0x594);v.component||(subtype!=0&&subtype!=9&&subtype!=10))continue;
            const float speed=length(v.velocity);if(speed<.3f||speed>6.f)continue;
            const V local=mul(inverse(v.frame),T{{},position(p)}).p;
            if(std::abs(local.z)>v.half.z+1.2f)continue;
            const V closest{std::clamp(local.x,-v.half.x,v.half.x),std::clamp(local.y,-v.half.y,v.half.y),local.z};
            const V gap=local-closest;if(length(gap)>.9f)continue;
            const V relative=rotate(conjugate(v.frame.q),v.velocity-at<V>(p,0x44)*50.f);
            if(dot(gap,relative)<.1f)continue;
            // A ped facing away keeps SA's normal avoidance until an actual
            // hit. Reaching behind the torso destabilizes the bracing pose.
            if(dot(normalized(matrix(p)->up),normalized(v.frame.p-position(p)))<.25f)continue;
            pending[ref]={{},normalized(v.frame.p-position(p)),now,10,false,Reaction::Brace,v.ref};
            cooldown[ref]=now;event("approaching_vehicle",ref,at<short>(p,0x22));break;
        }
        if(int(agents.size()+pending.size())>=maxActive)break;
    }
}
