#pragma once
// Controlled mechanics fixture, not evidence of native AI/gameplay parity.
// Native actors/floor/skins enter via pending -> normal PreRender activation.
// Only actor0 receives one initial velocity; no repeated drive or pose forcing.
struct PhysicalPeerProbe {
    std::array<int,3> refs{-1,-1,-1};
    unsigned created=0,queued=0,started=0,lastReport=0;
    V axis{1,0,0};float speed=0;int phase=0;bool recoveryPair=false;unsigned recoveryStart=0;
};
static PhysicalPeerProbe physicalPeerProbe;
static int physicalPeerFixture(int mode){
    if((mode<190||mode>199)&&mode!=210)return -190;
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!player()||area()!=0||!recoveryAllowed())return -190;
    if(physicalPeerProbe.phase||!agents.empty()||!pending.empty()||maxActive<3)return -191;
    const int lane=mode==210?0:(mode-190)/3,rate=mode==210?1:(mode-190)%3;
    const V axis=lane==0?V{1,0,0}:lane==1?V{0,1,0}:lane==2?V{-1,0,0}:V{0,-1,0};
    const float speeds[3]={.5f,3.f,8.f};const float speed=mode==199?3.f:speeds[rate];
    const V centre{2430.f,-1666.f,position(player()).z};
    if(length(position(player())-centre)>25.f)return -192;
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    ColPoint playerFloor{};void*support=nullptr;V from=position(player())+V{0,0,2};
    if(!vertical(from,from.z-8,playerFloor,support,true,false,false,true,true,false,nullptr))return -193;
    const float clearance=position(player()).z-playerFloor.point.z;
    if(clearance<.65f||clearance>1.5f)return -194;
    std::array<V,3> places{};
    for(int i=0;i<3;i++){
        places[i]=centre+axis*(float(i-1)*.65f);ColPoint floor{};support=nullptr;from=places[i]+V{0,0,2};
        if(length(places[i]-position(player()))<2.f)return -198; // Keep CJ out of this controlled initial chain.
        if(!vertical(from,from.z-8,floor,support,true,false,false,true,true,false,nullptr)||floor.normal.z<.9f)return -195;
        places[i].z=floor.point.z+clearance;
    }
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    PhysicalPeerProbe next;next.axis=axis;next.speed=speed;next.created=gameTime();next.phase=1;next.recoveryPair=mode==210;
    for(int i=0;i<3;i++){
        void*p=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,places[i],false);
        if(!p){event("probe_peer_setup_failed",-1,7,"spawn_failed slot="+std::to_string(i));return -196;}
        next.refs[i]=reference(p);void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);
        if(!t){event("probe_peer_setup_failed",next.refs[i],7,"idle_allocation_failed");return -197;}
        reinterpret_cast<void*(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(t,30000,false,false,8.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,3,true);
        const float heading=std::atan2(-axis.x,axis.y);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(p,heading);at<float>(p,0x558)=at<float>(p,0x55C)=heading;
        watchSessionPed(p);
        event("probe_peer_created",next.refs[i],7,"slot="+std::to_string(i)+" root_x="+std::to_string(places[i].x)+" root_y="+std::to_string(places[i].y)+" root_z="+std::to_string(places[i].z));
    }
    physicalPeerProbe=next;DeliriumSA_ProbeFocus(next.refs[1]);
    event("probe_peer_setup",next.refs[1],7,"mode="+std::to_string(mode)+" count=3 seed_mps="+std::to_string(speed)+" axis_x="+std::to_string(axis.x)+" axis_y="+std::to_string(axis.y)+" initial_spacing_m=0.65 native_floor=1 balance_fixture=1 gameplay_acceptance=0");
    return next.refs[1];
}
static void tickPhysicalPeerProbe(){
    auto&p=physicalPeerProbe;if(!p.phase||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return;
    const unsigned now=gameTime();
    if(p.phase==1&&now-p.created>=500){
        for(int ref:p.refs)if(!pedFromRef(ref)||!validPed(pedFromRef(ref))){event("probe_peer_setup_failed",ref,7,"native_actor_lost_before_queue");p.phase=0;return;}
        for(int i=0;i<(p.recoveryPair?1:3);i++){
            Request r{{},p.axis,now,10,true,Reaction::Melee};
            r.melee=MeleeResponse::Balance;r.velocityOverride=true;r.velocity=(i||p.recoveryPair)?V{}:p.axis*(p.speed/50.f);
            // Diagnostic admission only: no native damage and no authored hit.
            // The original Balance controller runs normally after admission.
            r.meleeContactReady=true;pending[p.refs[i]]=r;
            event("probe_peer_queued",p.refs[i],7,"slot="+std::to_string(i)+" one_time_seed_mps="+std::to_string(i?0.f:p.speed)+" extra_impulse=0");
        }
        p.phase=2;p.queued=now;
    }
    if(p.phase==2){
        if(p.recoveryPair){
            if(auto it=agents.find(p.refs[0]);it!=agents.end()){
                auto&a=*it->second;stopBehaviour(a,"bodyBalance");ok(nm_agent_task_catch_fall(a.agent,"*"));
                p.phase=4;p.started=now;
                event("probe_peer_recovery_wait",p.refs[0],7,"fixture_requests_one_initial_catchfall=1 no_repeated_forcing=1");
            }else if(now-p.queued>2500){event("probe_peer_setup_failed",p.refs[0],7,"recovery_target_not_admitted");p.phase=0;}
            return;
        }
        bool all=true;for(int ref:p.refs)all&=agents.contains(ref);
        if(all){p.phase=3;p.started=now;p.lastReport=0;event("probe_peer_all_physical",p.refs[1],7,"count=3 seed_applied_once=1 observe_ms=15000");
            for(unsigned delay:{17u,150u,500u,1000u,3000u,7000u,12000u,15000u})meleeFrames.push_back({now+delay,now,p.refs[1],"probe_peer_frame"});}
        else if(now-p.queued>2500){event("probe_peer_setup_failed",p.refs[1],7,"not_all_native_skeletons_activated");p.phase=0;}
    }
    if(p.phase==4){
        auto it=agents.find(p.refs[0]);
        if(it==agents.end()){event("probe_peer_setup_failed",p.refs[0],7,"target_lost_before_recovery");p.phase=0;return;}
        auto&a=*it->second;
        if(a.recovering&&a.recoveryAnimatedOwner){
            void*other=pedFromRef(p.refs[1]);if(!other){p.phase=0;return;}
            const V offset=contactPose(a)[0].p-position(other);V direction=normalized(V{offset.x,offset.y,0});
            Request r{{},direction,now,10,true,Reaction::Melee};r.melee=MeleeResponse::Balance;
            r.velocityOverride=true;r.velocity=direction*(3.f/50.f);r.meleeContactReady=true;
            pending[p.refs[1]]=r;p.phase=5;p.recoveryStart=now;p.started=now;
            event("probe_peer_queued",p.refs[1],7,"slot=1 one_time_seed_mps=3.000000 extra_impulse=0 recovery_peer_fixture=1");
            event("probe_peer_recovery_approach",p.refs[0],7,"other="+std::to_string(p.refs[1])+" distance_m="+std::to_string(length(offset))+" target_recovering=1 animated_placement_committed=1 single_seed=1");
            for(unsigned delay:{17u,150u,500u,1000u,3000u,7000u,12000u,15000u})meleeFrames.push_back({now+delay,now,p.refs[0],"probe_recovery_peer_frame"});
        }else if(now-p.started>12000){event("probe_peer_setup_failed",p.refs[0],7,"target_did_not_start_recovery");p.phase=0;}
        return;
    }
    if(p.phase==5){
        if(agents.contains(p.refs[1])){p.phase=3;event("probe_peer_all_physical",p.refs[0],7,"count=2 target_recovering=1 seed_applied_once=1 observe_ms=30000");}
        else if(now-p.recoveryStart>2500){event("probe_peer_setup_failed",p.refs[1],7,"incoming_peer_not_admitted");p.phase=0;}
    }
    if(p.phase!=3)return;
    if(now-p.lastReport>=100){
        p.lastReport=now;
        for(int i=0;i<3;i++){
            const int ref=p.refs[i];void*ped=pedFromRef(ref);if(!ped){event("probe_peer_actor_lost",ref,7,"elapsed_ms="+std::to_string(now-p.started));continue;}
            V com=position(ped),velocity=at<V>(ped,0x44)*50.f;bool recovering=false;const auto a=agents.find(ref);
            if(a!=agents.end()){nm_agent_get_com(a->second->agent,&com.x,&velocity.x);recovering=a->second->recovering;}
            event("probe_peer_sample",ref,7,"elapsed_ms="+std::to_string(now-p.started)+" slot="+std::to_string(i)+" physical="+std::to_string(a!=agents.end())+" recovering="+std::to_string(recovering)+" native_collision="+std::to_string((at<unsigned char>(ped,0x1C)&1)!=0)+" health="+std::to_string(at<float>(ped,0x540))+" com_x="+std::to_string(com.x)+" com_y="+std::to_string(com.y)+" com_z="+std::to_string(com.z)+" vx="+std::to_string(velocity.x)+" vy="+std::to_string(velocity.y)+" vz="+std::to_string(velocity.z));
        }
    }
    if(now-p.started>=(p.recoveryPair?30000u:15000u)){event("probe_peer_observation_finished",p.refs[1],7,"observed_ms="+std::to_string(now-p.started)+" no_repeated_forcing=1 gameplay_acceptance=0");p.phase=0;}
}
