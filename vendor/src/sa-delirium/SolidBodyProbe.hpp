#pragma once
#include <limits>
// Diagnostic only. Native static geometry and go-to motion are real; each NM
// peer receives exactly one setup velocity. No force/pose is injected later.
// This tests the animated-vs-NM/world contact bridge, not NPC decision parity.
struct SolidProbePlane {V point{},normal{};void*entity=nullptr;int model=-1;};
struct SolidBodyProbe {
    int walker=-1;std::array<int,2> peers{-1,-1};std::array<SolidProbePlane,2> planes{};
    int count=0,phase=0,mode=0;bool run=false;float speed=0;V goal{},lastRoot{};
    unsigned created=0,queued=0,started=0,lastSample=0,samples=0,contacts=0,blocked=0;
};
static SolidBodyProbe solidBodyProbe;
static bool solidProbeFloor(V&place,float clearance,float referenceFloor){
    const V from=place+V{0,0,3};ColPoint floor{};void*support=nullptr;
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    if(!vertical(from,from.z-8,floor,support,true,false,false,true,true,false,nullptr)||
       !support||floor.normal.z<.98f||(std::isfinite(referenceFloor)&&std::abs(floor.point.z-referenceFloor)>.15f))return false;
    place.z=floor.point.z+clearance;return true;
}
static bool solidProbePlaneAt(const V&root,const SolidProbePlane&p){
    // Verify a continuous static face across the actor's body, not just one
    // ray at root height. The face normal must point into this free space.
    const V tangent{-p.normal.y,p.normal.x,0};
    for(float height:{-.65f,0.f,.55f})for(float side:{-.30f,0.f,.30f}){
        const V from=root+V{0,0,height}+tangent*side;ColPoint hit{};void*entity=nullptr;
        if(!lineOfSight(from,from-p.normal*1.0f,hit,entity,true,false,false,true,true,false,false,false)||
           entity!=p.entity||std::abs(dot(hit.point-p.point,p.normal))>.04f||
           std::abs(dot(hit.normal,p.normal))<.98f)return false;
    }return true;
}
static bool solidProbeFind(bool corner,float clearance,float floorHeight,V&walker,std::array<SolidProbePlane,2>&planes){
    const V origin=position(player());std::array<V,16> axes{};
    for(size_t i=0;i<axes.size();i++){const float a=float(i)*6.28318530718f/float(axes.size());axes[i]={std::cos(a),std::sin(a),0};}
    unsigned floors=0,faces=0,pairs=0,planeRejects=0,peerRejects=0;
    // Search the currently streamed neighbourhood. Candidates are measured
    // from native collisions, never hard-coded invisible wall coordinates.
    for(int ring=0;ring<=5;ring++)for(int gx=-ring;gx<=ring;gx++)for(int gy=-ring;gy<=ring;gy++){
        if(std::max(std::abs(gx),std::abs(gy))!=ring)continue;
        // A valid nearby pavement need not be at the player's road height.
        // Its own measured floor is the reference for every member of this
        // fixture; this does not relax the level-ground or nine-ray checks.
        V originProbe=origin+V{float(gx)*3,float(gy)*3,0};if(!solidProbeFloor(originProbe,clearance,std::numeric_limits<float>::quiet_NaN()))continue;
        ++floors;const float localFloor=originProbe.z-clearance;
        std::vector<SolidProbePlane> hits;
        for(const V&axis:axes){ColPoint hit{};void*entity=nullptr;
            if(!lineOfSight(originProbe,originProbe+axis*7,hit,entity,true,false,false,true,true,false,false,false)||!entity||std::abs(hit.normal.z)>.02f)continue;
            V normal=normalized(V{hit.normal.x,hit.normal.y,0});if(dot(normal,axis)>0)normal=normal*-1;
            if(dot(normal,axis)>-.05f)continue;
            if(std::any_of(hits.begin(),hits.end(),[&](const auto&p){return p.entity==entity&&dot(p.normal,normal)>.999f&&std::abs(dot(hit.point-p.point,normal))<.01f;}))continue;
            ++faces;
            hits.push_back({hit.point,normal,entity,at<short>(entity,0x22)});
        }
        for(size_t i=0;i<hits.size();i++)for(size_t j=0;j<(corner?hits.size():1);j++){
            if(corner&&(i==j||std::abs(dot(hits[i].normal,hits[j].normal))>.05f))continue;
            ++pairs;
            V candidate=hits[i].point+hits[i].normal*.50f;candidate.z=originProbe.z;
            if(corner){const auto&a=hits[i];const auto&b=hits[j];const float det=a.normal.x*b.normal.y-a.normal.y*b.normal.x;
                if(std::abs(det)<.9f)continue;const float ca=dot(a.point,a.normal)+.50f,cb=dot(b.point,b.normal)+.50f;
                candidate.x=(ca*b.normal.y-a.normal.y*cb)/det;candidate.y=(a.normal.x*cb-ca*b.normal.x)/det;
            }
            if(length(candidate-origin)<3||length(candidate-origin)>23||!solidProbeFloor(candidate,clearance,localFloor))continue;
            if(!solidProbePlaneAt(candidate,hits[i])||(corner&&!solidProbePlaneAt(candidate,hits[j]))){++planeRejects;continue;}
            bool clear=true;const int count=corner?2:1;
            for(int k=0;k<count;k++){const auto&plane=k?hits[j]:hits[i];V peer=candidate+plane.normal*1.05f;
                if(!solidProbeFloor(peer,clearance,localFloor)){clear=false;break;}
                ColPoint cp{};void*obstacle=nullptr;
                if(lineOfSight(candidate,peer,cp,obstacle,true,true,false,true,true,false,false,false)){clear=false;break;}
            }
            if(!clear){++peerRejects;continue;}walker=candidate;planes[0]=hits[i];if(corner)planes[1]=hits[j];return true;
        }
    }event("probe_solid_surface_search",-1,-1,"floors="+std::to_string(floors)+" faces="+std::to_string(faces)+" candidate_pairs="+std::to_string(pairs)+" plane_rejects="+std::to_string(planeRejects)+" peer_rejects="+std::to_string(peerRejects));return false;
}
static bool solidProbeIdle(void*p){
    void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);if(!t)return false;
    reinterpret_cast<void*(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(t,30000,false,false,8.f);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,3,true);return true;
}
static void solidProbeFail(const std::string&reason){
    disarmOriginalGroundFixture();
    event("probe_solid_failed",solidBodyProbe.walker,7,reason);solidBodyProbe.phase=0;
}
static int solidBodyFixture(int mode){
    if(mode<220||mode>227||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||
       !player()||area()!=0||!recoveryAllowed()||solidBodyProbe.phase||!agents.empty()||!pending.empty())return -220;
    const int bits=mode-220;const bool corner=bits&1;const int count=corner?2:1;if(maxActive<count)return -221;
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    ColPoint floor{};void*support=nullptr;V from=position(player())+V{0,0,2};
    if(!vertical(from,from.z-8,floor,support,true,false,false,true,true,false,nullptr))return -222;
    const float clearance=position(player()).z-floor.point.z;if(clearance<.65f||clearance>1.5f)return -223;
    SolidBodyProbe next;next.mode=mode;next.count=count;next.run=(bits&2)!=0;next.speed=(bits&4)?8.f:3.f;
    V start{};if(!solidProbeFind(corner,clearance,floor.point.z,start,next.planes)){
        event("probe_solid_failed",-1,7,"no_verified_static_face_or_corner=1");return -224;
    }
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    const auto spawn=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710);
    void*w=spawn(4,7,start,false);if(!w||!solidProbeIdle(w))return -225;next.walker=reference(w);watchSessionPed(w);
    V forward=next.planes[0].normal;if(corner)forward=normalized(forward+next.planes[1].normal);
    next.goal=start+forward*3.f;next.lastRoot=start;const float heading=std::atan2(-forward.x,forward.y);
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(w,heading);at<float>(w,0x558)=at<float>(w,0x55C)=heading;
    for(int i=0;i<count;i++){
        V place=start+next.planes[i].normal*1.05f;if(!solidProbeFloor(place,clearance,start.z-clearance))return -226;
        void*p=spawn(4,7,place,false);if(!p||!solidProbeIdle(p))return -227;next.peers[i]=reference(p);watchSessionPed(p);
        event("probe_solid_plane",next.walker,7,"plane="+std::to_string(i)+" entity_model="+std::to_string(next.planes[i].model)+
            " x="+std::to_string(next.planes[i].point.x)+" y="+std::to_string(next.planes[i].point.y)+" z="+std::to_string(next.planes[i].point.z)+
            " nx="+std::to_string(next.planes[i].normal.x)+" ny="+std::to_string(next.planes[i].normal.y)+" native_verified_rays=9 peer="+std::to_string(next.peers[i]));
    }
    next.created=gameTime();next.phase=1;solidBodyProbe=next;DeliriumSA_ProbeFocus(next.walker);
    armOriginalGroundFixture(next.walker);
    event("probe_solid_setup",next.walker,7,"mode="+std::to_string(mode)+" peers="+std::to_string(count)+" run="+std::to_string(next.run)+
        " seed_mps="+std::to_string(next.speed)+" root_x="+std::to_string(start.x)+" root_y="+std::to_string(start.y)+" root_z="+std::to_string(start.z)+" no_desktop_input=1 gameplay_acceptance=0");
    return next.walker;
}
// Call immediately after each shared-owner sweep/root write. This is an
// observer, not a second contact solve; disabled outside this bounded fixture.
static void traceSolidBodyContact(int walker,int peer,const V&before,const V&wish,const V&applied,int solids,float impulse,float closing,float depth){
    auto&p=solidBodyProbe;if(p.phase!=3||walker!=p.walker)return;
    // -1 identifies the aggregate shared-owner result: several NM contacts
    // can contribute to this single commit. Do not invent a per-peer impulse.
    bool owned=peer==-1;for(int i=0;i<p.count;i++)owned|=peer==p.peers[i];if(!owned)return;
    ++p.contacts;if(solids>0&&length(applied-wish)>.00001f)++p.blocked;
    event("probe_solid_contact",walker,7,"elapsed_ms="+std::to_string(gameTime()-p.started)+" peer="+std::to_string(peer)+
        " before_x="+std::to_string(before.x)+" before_y="+std::to_string(before.y)+" before_z="+std::to_string(before.z)+
        " wanted_x="+std::to_string(wish.x)+" wanted_y="+std::to_string(wish.y)+" wanted_z="+std::to_string(wish.z)+
        " applied_x="+std::to_string(applied.x)+" applied_y="+std::to_string(applied.y)+" applied_z="+std::to_string(applied.z)+
        " solid_contacts="+std::to_string(solids)+" impulse_ns="+std::to_string(impulse)+" closing_mps="+std::to_string(closing)+" depth="+std::to_string(depth)+" aggregate_owner_commit="+std::to_string(peer==-1));
}
static void tickSolidBodyProbe(){
    auto&p=solidBodyProbe;if(!p.phase||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return;const unsigned now=gameTime();
    void*w=pedFromRef(p.walker);if(!w||agents.contains(p.walker)){solidProbeFail("native_walker_owner_lost=1");return;}
    for(int i=0;i<p.count;i++)if(!pedFromRef(p.peers[i])){solidProbeFail("peer_entity_lost=1");return;}
    if(p.phase==1&&now-p.created>=500){
        if(!animatedContacts.contains(p.walker)){if(now-p.created>2500)solidProbeFail("walker_has_no_captured_native_pose=1");return;}
        for(int i=0;i<p.count;i++){const V direction=p.planes[i].normal*-1.f;Request r{{},direction,now,10,true,Reaction::Melee};
            r.melee=MeleeResponse::Balance;r.velocityOverride=true;r.velocity=direction*(p.speed/50.f);r.meleeContactReady=true;pending[p.peers[i]]=r;}
        p.queued=now;p.phase=2;event("probe_solid_queued",p.walker,7,"initial_velocity_once=1 no_damage=1");
    }
    if(p.phase==2){bool ready=true;for(int i=0;i<p.count;i++)ready&=agents.contains(p.peers[i]);
        if(!ready){if(now-p.queued>2500)solidProbeFail("not_all_peers_admitted=1");return;}
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x30);if(!t){solidProbeFail("go_to_allocation_failed=1");return;}
        reinterpret_cast<void*(__thiscall*)(void*,int,const V&,float,bool,bool)>(0x667CD0)(t,p.run?6:4,p.goal,.15f,p.run,false);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(w),t,3,true);
        p.phase=3;p.started=now;p.lastSample=0;p.lastRoot=position(w);
        event("probe_solid_started",p.walker,7,"native_go_to_once=1 repeated_force=0 observation_ms=15000");
        for(unsigned delay:{17u,150u,500u,1000u,3000u,7000u,12000u,15000u})meleeFrames.push_back({now+delay,now,p.walker,"probe_solid_frame"});
    }
    if(p.phase!=3||now==p.lastSample)return;p.lastSample=now;++p.samples;
    const V root=position(w),v=at<V>(w,0x44)*50.f;const float step=length(root-p.lastRoot);p.lastRoot=root;
    const auto cache=animatedContacts.find(p.walker);auto*body=bodyCollider(w);bool pose=body&&cache!=animatedContacts.end()&&now-cache->second.time<=100&&setAnimatedBodyPose(*body,cache->second);
    event("probe_solid_frame_state",p.walker,7,"elapsed_ms="+std::to_string(now-p.started)+" sample="+std::to_string(p.samples)+
        " root_x="+std::to_string(root.x)+" root_y="+std::to_string(root.y)+" root_z="+std::to_string(root.z)+" step_m="+std::to_string(step)+
        " vx="+std::to_string(v.x)+" vy="+std::to_string(v.y)+" vz="+std::to_string(v.z)+" pose_valid="+std::to_string(pose));
    for(int i=0;i<p.count;i++){
        const auto&plane=p.planes[i];const V toward=plane.normal*-.75f;V safe{};const int solids=pose&&collision?nm_pose_collider_sweep(body->shape,collision->world,&toward.x,&safe.x):-1;
        int contacts=-1,ownerContacts=-1,ownerChild=-1,physicalPart=-1;float depth=0,ownerDepth=0,closing=0,centre=100;bool recovering=false,peerNmOwner=false;const auto peer=agents.find(p.peers[i]);
        if(peer!=agents.end()){
            auto&a=*peer->second;recovering=a.recovering;peerNmOwner=!a.recoveryAnimatedOwner;for(const auto&t:contactPose(a))centre=std::min(centre,dot(t.p-plane.point,plane.normal));
            std::array<float,21*13> states{};std::array<float,21*21*13> hits{};
            if(pose&&physicalContactStates(a,states)){contacts=nm_pose_collider_contacts(body->shape,a.agent,states.data(),21,hits.data(),21*21);
                for(int j=0;j<contacts;j++){depth=std::max(depth,-hits[j*13+11]);closing=std::max(closing,hits[j*13+12]);}
                OriginalAnimatedOwnerSample owner;
                if(peerNmOwner&&animatedExternalEligible(w)&&captureOriginalAnimatedOwner(w,body->rig->rootClearance,owner)){
                    // `physicalContactStates` deliberately follows the skin
                    // for targeting/foot placement. Solver penetration must
                    // instead use its authoritative part frames, even while
                    // an NM body is waiting for room to begin recovery.
                    bool measured=true;for(int k=0;k<21;k++){
                        V linear{},spin{};if(nm_agent_get_part_velocity(a.agent,k,&linear.x,&spin.x)<0){measured=false;break;}
                        const auto&t=a.current[k];const float row[]={t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w,linear.x,linear.y,linear.z,spin.x,spin.y,spin.z};
                        std::copy(std::begin(row),std::end(row),states.begin()+13*k);
                    }
                    if(measured){
                    ownerContacts=nm_animated_owner_pair_contacts(&owner.root.x,owner.heading,&owner.velocity.x,a.agent,states.data(),21,hits.data(),21*21);
                    for(int j=0;j<ownerContacts;j++)if(-hits[j*13+11]>ownerDepth){ownerDepth=-hits[j*13+11];physicalPart=int(hits[j*13]);ownerChild=int(hits[j*13+1]);}
                    }
                }
            }
        }
        event("probe_solid_surface_sample",p.walker,7,"elapsed_ms="+std::to_string(now-p.started)+" plane="+std::to_string(i)+
            " native_plane_root_distance="+std::to_string(dot(root-plane.point,plane.normal))+" exact_sweep_contacts="+std::to_string(solids)+
            " exact_free_translation_m="+std::to_string(solids>=0?length(safe):-1.f)+" exact_query_max_m=0.75 penetration_not_measured=1 peer="+std::to_string(p.peers[i])+
            " peer_physical="+std::to_string(peer!=agents.end())+" peer_recovering="+std::to_string(recovering)+" peer_centres_min_plane="+std::to_string(centre)+
            " exact_pair_contacts="+std::to_string(contacts)+" exact_pair_depth="+std::to_string(depth)+" closing_max_mps="+std::to_string(closing)+
            " exact_pair_geometry=rendered21_vs_nm21 physical_owner_contacts="+std::to_string(ownerContacts)+" physical_owner_depth="+std::to_string(ownerDepth)+
            " physical_owner_child="+std::to_string(ownerChild)+" physical_nm_part="+std::to_string(physicalPart)+" physical_owner_geometry=original4_vs_nm21 physical_pose=solver_parts peer_nm_authority="+std::to_string(peerNmOwner));
    }
    if(now-p.started>=15000){event("probe_solid_finished",p.walker,7,"observed_ms="+std::to_string(now-p.started)+" samples="+std::to_string(p.samples)+
        " contact_solves="+std::to_string(p.contacts)+" world_clipped_solves="+std::to_string(p.blocked)+" gameplay_acceptance=0");disarmOriginalGroundFixture();p.phase=0;}
}
