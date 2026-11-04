#pragma once
#include "FootSupportMath.hpp"
struct WalkingSupport {void*ped=nullptr;void*clump=nullptr;unsigned time=0,logged=0;LegSupportState legs;
    std::array<FootSurface,2> surfaces{};std::array<V,2> points{};std::array<float,2> planted{};};
static std::map<int,WalkingSupport> walkingSupports;
static bool footSupportEnabled=true;static float footSupportMaxLift=.4f;
static int footProbePed=-1,footProbeBody=-1;static unsigned footProbeUntil=0;
static unsigned footFrameDue=0,footFramesTaken=0;
static float footProbeLoadNs=0;
static void captureFootSupportFrame(){
    if(!footFrameDue||gameTime()<footFrameDue)return;
    if(recoveryAllowed()&&gameTime()<=footProbeUntil){const int result=DeliriumSA_ProbeFrame();
        event("probe_foot_support_frame",footProbePed,0,"capture_ms="+std::to_string(gameTime())+" result="+std::to_string(result));}
    footFrameDue=0;
}
static void applyFootLoads(Active&a,float dt){
    // An animated walker is not an infinite-mass moving collision shape. Give
    // the support only its finite share of body weight, not animation velocity.
    for(const auto&[ref,c]:walkingSupports){
        if(gameTime()-c.time>100||pedFromRef(ref)!=c.ped||agents.contains(ref))continue;
        // Source822F74F0 applies support only to the normal ped. Its contact
        // callback consumes child1 before solving. Do not add a second 80kg
        // native-weight force on NM while that exact source owns support.
        if(originalGroundActive(c.ped))continue;
        const float mass=at<float>(c.ped,0x8C);if(!std::isfinite(mass)||mass<=0||mass>300.f)continue;
        const float total=std::max(1.f,c.planted[0]+c.planted[1]);
        for(int i=0;i<2;i++)if(c.surfaces[i].body==a.ref&&c.surfaces[i].part>=0&&c.planted[i]>0){
            const V impulse{0,0,-mass*9.81f*dt*c.planted[i]/total};
            const V contact=physicalContactPoint(a,c.surfaces[i].part,c.points[i]);
            ok(nm_agent_apply_impulse(a.agent,c.surfaces[i].part,&contact.x,&impulse.x));
            if(ref==footProbePed&&a.ref==footProbeBody&&gameTime()<=footProbeUntil)footProbeLoadNs-=impulse.z;
        }
    }
}
static FootSurface supportSurface(void*ped,V atPoint,float base){
    FootSurface out;out.z=base;
    const V from=atPoint+V{0,0,.4f};ColPoint hit{};void*entity=nullptr;
    // Existing verified native vertical query: buildings, vehicles, objects;
    // ped collision is supplied by the actual physical capsules below.
    if(reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
        (from,from.z-1.4f,hit,entity,true,true,false,true,true,false,nullptr)&&hit.normal.z>.1f){
        out={true,hit.point.z,hit.normal};
    }
    for(const auto&[ref,entry]:agents){const auto&a=*entry;
        if(a.ped==ped||!contactBodyValid(a)||length(contactPose(a)[0].p-atPoint)>2.f||
           !physicalFootSupportEligible(nm_agent_has_fallen(a.agent)==1,nm_agent_is_balancing(a.agent)==1,a.recoveryAnimatedOwner,a.recovering&&a.uprightRecovery))continue;
        const auto&pose=contactPose(a);
        for(int i=0;i<21;i++){const V axis=rotate(pose[i].q,{0,a.rig->partHalf[i],0});FootSurface candidate;
            // Footprint rather than a zero-width ray: the toe/heel reaches an
            // obstacle before the middle of the foot has already crossed it.
            if(footCapsule(from,1.4f,pose[i].p-axis,pose[i].p+axis,a.rig->partRadius[i]+.08f,candidate)&&
               (!out.hit||candidate.z>out.z)){
                if(ped==player()&&GetEnvironmentVariableA("GTASA_FOOT_SUPPORT_AUDIT",nullptr,0))
                    event("foot_support_candidate",reference(ped),at<short>(ped,0x22),
                        "body="+std::to_string(ref)+" part="+std::to_string(i)+" surface="+std::to_string(candidate.z)+
                        " base="+std::to_string(base)+" fallen="+std::to_string(nm_agent_has_fallen(a.agent))+
                        " recovering="+std::to_string(a.recovering)+" upright_recovery="+std::to_string(a.uprightRecovery));
                candidate.body=ref;candidate.part=i;out=candidate;}}
    }
    // A wall/high object is not a small step. Native jumping/climbing retains it.
    if(!std::isfinite(out.z)||out.z-base>footSupportMaxLift+.05f||base-out.z>.45f)out={false,base};
    return out;
}
static void applyWalkingSupport(void*ped){
    const int ref=reference(ped);if(ref<0)return;
    static const bool audit=GetEnvironmentVariableA("GTASA_FOOT_SUPPORT_AUDIT",nullptr,0)!=0;
    const unsigned now=gameTime();
    const int state=at<int>(ped,0x530);
    // CPed flags from the checked-in SDK. Never modify ragdoll, attacks, seats,
    // airborne, crouched, climbing/swimming or protected-script poses.
    const bool allowed=footSupportEnabled&&recoveryAllowed()&&validPed(ped,false,false,ped==player())&&
        !agents.contains(ref)&&!pending.contains(ref)&&(at<unsigned char>(ped,0x46C)&1u)&&
        !(at<unsigned char>(ped,0x46D)&2u)&&!(at<unsigned char>(ped,0x46F)&4u)&&
        state<38&&state!=16&&state!=17&&state!=18&&state!=19&&state!=27&&meleeAttack(ped).anim<0;
    if(!allowed){walkingSupports.erase(ref);return;}
    auto*h=hierarchy(ped);if(!h||!h->nodes||!h->matrices||h->num<20||h->num>128)return;
    auto index=[&](int id){for(int n=0;n<h->num;n++)if(h->nodes[n].id==id)return n;return -1;};
    const int pelvis=index(saBone::pelvis);int leg[2][4];for(int j=0;j<2;j++)for(int k=0;k<4;k++)leg[j][k]=index(41+j*10+k);
    if(pelvis<0)return;for(const auto&l:leg)for(int n:l)if(n<0)return;
    auto&cache=walkingSupports[ref];const void*clump=at<void*>(ped,0x18);
    if(cache.ped!=ped||cache.clump!=clump||now<cache.time||now-cache.time>100)cache={ped,const_cast<void*>(clump),now};
    const float dt=std::clamp((now-cache.time)*.001f,0.f,.05f);cache.time=now;if(dt<=0)return;
    const V root=position(ped);ColPoint ground{};void*entity=nullptr;
    if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
        (root,root.z-2.f,ground,entity,true,true,false,true,true,false,nullptr)||ground.normal.z<=.1f)return;
    const float base=ground.point.z;
    std::array<FootSurface,2> feet;
    std::vector<M> before(h->matrices,h->matrices+h->num);
    for(int j=0;j<2;j++)feet[j]=supportSurface(ped,(before[leg[j][2]].pos+before[leg[j][3]].pos)*.5f,base);
    stepLegSupport(cache.legs,feet,base,before[pelvis].pos.z,length(at<V>(ped,0x44))*50.f,dt,footSupportMaxLift);
    // IV 8235A900 approaches the lift at 2 m/s. Body contacts and their
    // reciprocal load remain physical; IK must not jump onto a peer in one frame.
    const float drop=cache.legs.global*cache.legs.drop;
    for(int n=0;n<h->num;n++){const int id=h->nodes[n].id;
        if(id!=0&&!(id>=42&&id<=44)&&!(id>=52&&id<=54))h->matrices[n].pos.z+=drop;}
    for(int j=0;j<2;j++){
        const int hip=leg[j][0],knee=leg[j][1],foot=leg[j][2],toe=leg[j][3];
        const V hp=h->matrices[hip].pos,kp=before[knee].pos,fp=before[foot].pos;
        V nk,nf;if(!supportLeg(hp,kp,fp+V{0,0,cache.legs.lift[j]},normalized(matrix(ped)->up),
            length(before[knee].pos-before[hip].pos),length(fp-kp),nk,nf))continue;
        auto turn=[&](int n,Q q,V pos){auto&m=h->matrices[n];m.right=rotate(q,before[n].right);m.up=rotate(q,before[n].up);m.at=rotate(q,before[n].at);m.pos=pos;};
        turn(hip,turnBetween(before[knee].pos-before[hip].pos,nk-hp),hp);
        turn(knee,turnBetween(fp-kp,nf-nk),nk);
        const float plant=std::clamp(1.f-(before[toe].pos.z-base-.02f)/.10f,0.f,1.f)*cache.legs.weight[j];
        cache.surfaces[j]=feet[j];cache.points[j]={(before[foot].pos.x+before[toe].pos.x)*.5f,(before[foot].pos.y+before[toe].pos.y)*.5f,feet[j].z};
        cache.planted[j]=feet[j].hit?plant:0.f;
        const Q tilt=mix(Q{},turnBetween({0,0,1},feet[j].normal),plant);
        turn(foot,tilt,nf);turn(toe,tilt,nf+rotate(tilt,before[toe].pos-fp));
    }
    if(audit&&ped==player()){
        event("foot_support_audit",ref,at<short>(ped,0x22),
            "base="+std::to_string(base)+" root_z="+std::to_string(root.z)+" speed="+std::to_string(length(at<V>(ped,0x44))*50.f)+
            " dt="+std::to_string(dt)+" left_body="+std::to_string(feet[0].body)+" right_body="+std::to_string(feet[1].body)+
            " left_part="+std::to_string(feet[0].part)+" right_part="+std::to_string(feet[1].part)+
            " left_surface="+std::to_string(feet[0].z)+" right_surface="+std::to_string(feet[1].z)+
            " left_before="+std::to_string(before[leg[0][3]].pos.z)+" right_before="+std::to_string(before[leg[1][3]].pos.z)+
            " left_after="+std::to_string(h->matrices[leg[0][3]].pos.z)+" right_after="+std::to_string(h->matrices[leg[1][3]].pos.z)+
            " left_lift="+std::to_string(cache.legs.lift[0])+" right_lift="+std::to_string(cache.legs.lift[1]));
    }
    if(ref==footProbePed&&now<=footProbeUntil&&now-cache.logged>=100){
        cache.logged=now;float error=0;for(int j=0;j<2;j++)for(int k=0;k<2;k++)error=std::max(error,std::abs(
            length(h->matrices[leg[j][k+1]].pos-h->matrices[leg[j][k]].pos)-length(before[leg[j][k+1]].pos-before[leg[j][k]].pos)));
        const auto body=agents.find(footProbeBody);const V bp=body==agents.end()?V{}:body->second->current[0].p;
        event("foot_support_sample",ref,at<short>(ped,0x22),"left_lift="+std::to_string(cache.legs.lift[0])+" right_lift="+std::to_string(cache.legs.lift[1])+
            " left_body="+std::to_string(feet[0].body)+" right_body="+std::to_string(feet[1].body)+" pelvis_drop="+std::to_string(drop)+
            " segment_error="+std::to_string(error)+" body_x="+std::to_string(bp.x)+" body_y="+std::to_string(bp.y)+" body_z="+std::to_string(bp.z)+
            " support_load_ns="+std::to_string(footProbeLoadNs));
        if(footFramesTaken<3&&!footFrameDue&&std::abs(cache.legs.lift[0]-cache.legs.lift[1])>.12f&&
            (feet[0].body>=0||feet[1].body>=0)){footFrameDue=now+17;footFramesTaken++;}
    }
    for(auto it=walkingSupports.begin();it!=walkingSupports.end();)if(now-it->second.time>500||pedFromRef(it->first)!=it->second.ped)it=walkingSupports.erase(it);else ++it;
}
