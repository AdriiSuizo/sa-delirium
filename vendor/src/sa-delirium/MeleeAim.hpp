#pragma once
#include "ContactMath.hpp"
// Retarget only the authored striking arm near its contact phase. Never pull
// the preparatory arm, stretch a limb or translate either character's root.
static void aimMeleeStrike(void*ped){
    if(!recoveryAllowed())return;
    const int ref=reference(ped);if(ref<0||agents.contains(ref)||!matrix(ped)||at<float>(ped,0x540)<=0)return;
    const auto attack=meleeAttack(ped);
    if(attack.contactTime<=0||(attack.profile!="left_hand"&&attack.profile!="right_hand"))return;
    void*target=attack.target;if(reference(target)<0||!matrix(target)||length(position(target)-position(ped))>1.7f)return;
    const float phase=attack.time-attack.contactTime;
    if(phase<-.14f||phase>.16f)return;
    const float weight=recoveryEase(phase<0?1.f+phase/.14f:1.f-phase/.16f);
    if(weight<=0)return;
    auto*h=hierarchy(ped);auto*vh=hierarchy(target);if(!h||!vh||!h->nodes||!vh->nodes||!h->matrices||!vh->matrices)return;
    auto index=[&](int id){for(int n=0;n<h->num;n++)if(h->nodes[n].id==id)return n;return -1;};
    V head{};bool found=false;
    if(auto a=agents.find(reference(target));a!=agents.end()&&!a->second->lastRendered.empty()){
        const int n=a->second->rig->index(saBone::head);if(n>=0){head=a->second->lastRendered[n].p;found=true;}
    }
    if(!found)for(int n=0;n<vh->num;n++)if(vh->nodes[n].id==saBone::head){head=vh->matrices[n].pos;found=true;break;}
    if(!found||!finite(head)||length(head-position(target))>3.f)return;
    // A standing high punch aims at the front of the head. Fallen-body strikes
    // use their own kick profile and are not pulled up to this target.
    const V face=head+V{0,0,.10f}+normalized(V{position(ped).x-head.x,position(ped).y-head.y,0})*.12f;
    const int base=attack.profile=="left_hand"?32:22;
    const int u=index(base),e=index(base+1),w=index(base+2),f=index(base+3);
    if(std::min({u,e,w,f})<0)return;
    const V shoulder=h->matrices[u].pos,elbow=h->matrices[e].pos,wrist=h->matrices[w].pos;
    V correction=face-h->matrices[f].pos;
    const float reach=length(elbow-shoulder)+length(wrist-elbow)+length(h->matrices[f].pos-wrist);
    if(!finite(correction)||length(face-shoulder)>reach+.08f)return; // unreachable without moving/stetching the body
    const float requested=length(correction);
    V newElbow,newWrist;if(!solveContactArm(shoulder,elbow,wrist,wrist+correction*weight,newElbow,newWrist))return;
    const Q upper=turnBetween(elbow-shoulder,newElbow-shoulder),lower=turnBetween(wrist-elbow,newWrist-newElbow);
    auto transformBone=[&](int n,Q q,V origin,V destination){auto&b=h->matrices[n];b.right=rotate(q,b.right);b.up=rotate(q,b.up);b.at=rotate(q,b.at);b.pos=destination+rotate(q,b.pos-origin);};
    transformBone(u,upper,shoulder,shoulder);transformBone(e,lower,elbow,newElbow);
    for(int id=base+2;id<=base+4;id++)if(int n=index(id);n>=0)transformBone(n,lower,wrist,newWrist);
    // One diagnostic per strike, scoped to the native fixture only.
    static unsigned last=0;static int lastAnim=-1;
    if(ref==meleeProbeAttacker&&std::abs(phase)<.025f&&(gameTime()-last>200||lastAnim!=attack.anim)){
        event("melee_aim_contact",reference(target),at<short>(target,0x22),"anim="+std::to_string(attack.anim)+" arm="+attack.profile+" correction_m="+std::to_string(length(newWrist-wrist))+" requested_m="+std::to_string(requested)+" remaining_face_gap_m="+std::to_string(length(h->matrices[f].pos-face))+" max_arm_reach_m="+std::to_string(length(elbow-shoulder)+length(wrist-elbow)));last=gameTime();lastAnim=attack.anim;
    }
}
