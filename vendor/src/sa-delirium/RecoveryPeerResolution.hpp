#pragma once
#include "RecoveryContactPolicy.hpp"
// A manifold does not manufacture a ComplexNM response: original BlendFromNM
// 82615070/8260F970 requires an actual response or NewTask. Geometry still
// resolves against peers while native animation owns recovery.
struct RecoveryContactSnapshot {
    void*ped=nullptr;NmAgent*agent=nullptr;unsigned frame=0,start=0;bool recovering=false;
    std::array<float,21*13> state{};
};
struct RecoveryPoseCollider {
    NmAgent*owner=nullptr;NmPoseCollider*shape=nullptr;
    ~RecoveryPoseCollider(){if(shape)nm_pose_collider_destroy(shape);}
};
static std::map<int,RecoveryContactSnapshot> recoveryContactHistory;
static std::map<int,std::unique_ptr<RecoveryPoseCollider>> recoveryPoseColliders;
static std::map<std::pair<int,int>,unsigned> recoveryResolvedLog;
static unsigned recoveryContactProcessTime=0;static bool recoveryContactPrimed=false;
static void clearRecoveryContacts(){recoveryContactHistory.clear();recoveryPoseColliders.clear();recoveryResolvedLog.clear();recoveryContactProcessTime=0;recoveryContactPrimed=false;}
static NmPoseCollider* recoveryCollider(Active&a,const RecoveryContactSnapshot&s){
    auto it=recoveryPoseColliders.find(a.ref);
    if(it!=recoveryPoseColliders.end()&&it->second->owner!=a.agent){recoveryPoseColliders.erase(it);it=recoveryPoseColliders.end();}
    if(it==recoveryPoseColliders.end()){
        auto c=std::make_unique<RecoveryPoseCollider>();c->owner=a.agent;c->shape=nm_pose_collider_create(a.rig->build);
        if(!c->shape)return nullptr;it=recoveryPoseColliders.emplace(a.ref,std::move(c)).first;
    }
    return ok(nm_pose_collider_set_pose(it->second->shape,s.state.data(),21))?it->second->shape:nullptr;
}
static V recoveryContactVelocity(Active&a,const RecoveryContactSnapshot&s,int part,V point){
    const float*r=s.state.data()+part*13;
    if(s.recovering)return V{r[7],r[8],r[9]}+cross(V{r[10],r[11],r[12]},point-V{r[0],r[1],r[2]});
    const V at=physicalContactPoint(a,part,point);V v{};
    if(!ok(nm_agent_get_predicted_contact_velocity(a.agent,part,&at.x,&v.x)))return {};
    return v; // Includes earlier queued impulses, never spends stale closing twice.
}
static float recoveryContactMobility(Active&a,bool animated,int part,V point,V normal){
    if(animated){const float mass=at<float>(a.ped,0x8C);return std::isfinite(mass)&&mass>0?1.f/mass:0;}
    const V at=physicalContactPoint(a,part,point);float mobility=0;
    return ok(nm_agent_get_contact_inverse_mass(a.agent,part,&at.x,&normal.x,&mobility))?mobility:0;
}
static V moveRecoveringContact(Active&a,RecoveryContactSnapshot&s,V wish,V deltaVelocity,bool*resolved=nullptr){
    if(resolved)*resolved=false;
    auto*shape=recoveryCollider(a,s);if(!shape||!collision||!collision->world)return {};
    V move{};const int blocked=nm_pose_collider_sweep(shape,collision->world,&wish.x,&move.x);
    if(blocked<0){ok(blocked);return {};}
    if(resolved)*resolved=true;
    matrix(a.ped)->pos=matrix(a.ped)->pos+move;reinterpret_cast<void(__thiscall*)(void*)>(0x542560)(a.ped);
    at<V>(a.ped,0x44)=at<V>(a.ped,0x44)+deltaVelocity*(1.f/50.f);
    if(a.uprightRecovery)a.recoveryVelocity=a.recoveryVelocity+deltaVelocity;
    // World-space frozen/render palettes follow the same corrected root.
    for(auto&t:a.renderedParts)t.p=t.p+move;
    for(auto&t:a.lastRendered)t.p=t.p+move;
    for(auto&t:a.frozenBones)t.p=t.p+move;
    for(auto&t:a.bones)t.p=t.p+move;
    for(int k=0;k<21;k++){
        a.renderVelocity[k]=a.renderVelocity[k]+deltaVelocity;
        auto*r=s.state.data()+k*13;r[0]+=move.x;r[1]+=move.y;r[2]+=move.z;r[7]+=deltaVelocity.x;r[8]+=deltaVelocity.y;r[9]+=deltaVelocity.z;
    }return move;
}
static bool resolveRecoveringWalkerContact(Active&a,V correction,V deltaVelocity,V&moved){
    if(!a.recovering||!contactBodyValid(a)||a.lastRenderTime!=gameTime())return false;
    RecoveryContactSnapshot s{a.ped,a.agent,*reinterpret_cast<unsigned*>(0xB7CB4C),a.start,true};
    if(!physicalContactStates(a,s.state))return false;
    bool resolved=false;moved=moveRecoveringContact(a,s,correction,deltaVelocity,&resolved);
    return resolved; // Keep the recovery task, animation phase and same body.
}
static void promoteRecoveryContacts(std::vector<Active*>&live,float dt,unsigned now){
    (void)live;(void)dt;(void)now;
    // Superseded by original normal-owner rows in the shared group. Animated
    // hands are damage/targeting geometry, not 21 independent locomotor masses.
    // No NPC geometry grants permission to interrupt BlendFromNM.
}
