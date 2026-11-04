#pragma once
#include "ContactMath.hpp"
// Contact correction on an animated barehand attacker during a confirmed hit.
// Victim physics and native damage remain authoritative. Never move the root,
// replace an attack task or apply this to a weapon/vehicle/player ragdoll.
static void constrainMeleeHands(void*ped){
    const int ref=reference(ped);if(ref<0||agents.contains(ref)||!matrix(ped)||at<float>(ped,0x540)<=0)return;
    auto*m=taskManager(ped);auto*task=m?at<void*>(m,0x14):nullptr;
    if(!task||at<unsigned>(task,0)!=0x86D684)return; // verified CTaskSimpleFight
    auto*h=hierarchy(ped);if(!h||!h->nodes||!h->matrices||h->num<20||h->num>128)return;
    Active* victim=nullptr;
    for(auto&[id,a]:agents)if(a->kind==Reaction::Melee&&a->meleeAttacker==ref&&a->meleeWeapon<=1&&a->meleeWeapon>=0
        &&!a->recovering&&!a->preview&&pedFromRef(id)==a->ped&&gameTime()-a->meleeReceived<650
        &&at<void*>(task,0x18)==a->ped&&!a->lastRendered.empty()&&gameTime()-a->lastRenderTime<100){victim=a.get();break;}
    if(!victim)return;
    const auto bodies=animatedPedCapsules(victim->ped,victim);if(bodies.empty())return;
    auto index=[&](int id){for(int n=0;n<h->num;n++)if(h->nodes[n].id==id)return n;return -1;};
    float largest=0;
    for(int base:{22,32}){
        int u=index(base),e=index(base+1),w=index(base+2),f=index(base+3);if(std::min({u,e,w,f})<0)continue;
        const V originalWrist=h->matrices[w].pos;
        // A few bounded IK projections handle multiple torso/head contacts.
        for(int iteration=0;iteration<4;iteration++){
            V wrist=h->matrices[w].pos,finger=h->matrices[f].pos,correction{};float deepest=0;
            for(V hand:{wrist,(wrist+finger)*.5f,finger})for(const auto&row:bodies){
                V a{row[0],row[1],row[2]},b{row[3],row[4],row[5]},nearestPoint=segmentNear(hand,a,b),delta=hand-nearestPoint;
                const float distance=length(delta),depth=row[6]+.07f-distance;
                if(depth>deepest&&distance>1.e-5f){deepest=depth;correction=delta*((depth+.003f)/distance);}
            }
            if(deepest<=0)break;
            V goal=wrist+correction;V total=goal-originalWrist;
            if(length(total)>.18f)goal=originalWrist+normalized(total)*.18f;
            V newElbow,newWrist;const V shoulder=h->matrices[u].pos,oldElbow=h->matrices[e].pos;
            if(!solveContactArm(shoulder,oldElbow,wrist,goal,newElbow,newWrist))break;
            const Q upper=turnBetween(oldElbow-shoulder,newElbow-shoulder),lower=turnBetween(wrist-oldElbow,newWrist-newElbow);
            auto rotateMatrix=[&](int n,Q q,V origin,V dest){auto&b=h->matrices[n];b.right=rotate(q,b.right);b.up=rotate(q,b.up);b.at=rotate(q,b.at);b.pos=dest+rotate(q,b.pos-origin);};
            rotateMatrix(u,upper,shoulder,shoulder);rotateMatrix(e,lower,oldElbow,newElbow);
            // Hand/finger helpers preserve their native local transforms.
            for(int id=base+2;id<=base+4;id++){int n=index(id);if(n>=0)rotateMatrix(n,lower,wrist,newWrist);}
            largest=std::max(largest,length(newWrist-originalWrist));
        }
    }
    if(largest>.001f&&!victim->meleeHandContactLogged){
        event("melee_hand_surface_contact",victim->ref,victim->model,"attacker="+std::to_string(ref)+" wrist_correction_m="+std::to_string(largest)+" lengths_preserved=1");victim->meleeHandContactLogged=true;
    }
}
