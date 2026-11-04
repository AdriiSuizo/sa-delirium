#pragma once
#include "MeleeSweep.hpp"
static ContactSegment meleeSegment(const PeerCapsule&r,bool head=false){
    return {head?V{r[3],r[4],r[5]}:V{r[0],r[1],r[2]},V{r[3],r[4],r[5]},r[6]};
}
static bool meleeTargetAt(const AnimatedContactPose&p,unsigned time,size_t row,ContactSegment&out){
    const auto&h=p.history;if(h.empty())return false;
    for(size_t n=1;n<h.size();n++)if(time>=h[n-1].time&&time<=h[n].time&&h[n].time-h[n-1].time<=50){
        if(row>=h[n-1].rows.size()||row>=h[n].rows.size())return false;
        out=contactLerp(meleeSegment(h[n-1].rows[row]),meleeSegment(h[n].rows[row]),float(time-h[n-1].time)/float(h[n].time-h[n-1].time));return true;
    }
    // Ped render order can put the other palette one frame behind. Do not
    // extrapolate a moving head or query its hidden native flinch hierarchy.
    const auto&f=time<h.front().time?h.front():h.back();
    if(std::abs(int(time)-int(f.time))>20||row>=f.rows.size())return false;
    out=meleeSegment(f.rows[row]);return true;
}
// Derive a contact only inside an accepted native damage event. Mere proximity
// during a wind-up must never create damage or a reaction. Bone velocities are
// the last completed animation sample; stale/ambiguous contacts are labelled.
static void configureMeleeRequest(Request&r,void*victim,float amount,bool finalize=false){
    const auto now=gameTime();const int ref=reference(victim);
    const auto attack=meleeAttack(pedFromRef(r.attackerRef));const unsigned allowed=attack.allowed;
    auto target=animatedContacts.find(ref),source=animatedContacts.find(r.attackerRef);
    const bool freshTarget=target!=animatedContacts.end()&&target->second.ped==victim&&now-target->second.time<=80;
    if(!finalize&&!agents.contains(ref)&&freshTarget&&length(position(victim)-target->second.root)<.15f){
        auto pose=std::make_shared<PoseSeed>();pose->bones=target->second.bones;pose->deriveParts=true;
        const V delta=position(victim)-target->second.root;for(auto&bone:pose->bones)bone.p=bone.p+delta;
        pose->velocity=r.velocityOverride?r.velocity:at<V>(victim,0x44);r.pose=pose;
    }
    // Native query scopes can raise a prone entity root temporarily. That
    // height difference is NOT an upward fist velocity.
    if(!finalize){
        r.nativeMeleePart=r.part;r.impulse=normalized(V{r.direction.x,r.direction.y,0})*(amount*.3f);
        r.reactionDraw=reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(0.f,1.f);
        return; // keep pre-hit pose, then resolve once both render samples finish
    }
    r.part=r.nativeMeleePart;r.contactKnown=false;
    bool push=false;int limb=-1;float best=1.e9f,first=1.e20f;V strikeVelocity{},toward{},bestPoint{};
    unsigned contactAge=0;
    float handGap[2]={99,99};V handVelocity[2]{};
    if(freshTarget&&source!=animatedContacts.end()&&source->second.ped==pedFromRef(r.attackerRef)&&now-source->second.time<=80){
        const auto&attacker=source->second;
        // Same ordering as animatedPedCapsules. The target's actual segment,
        // not the attacker's entity centre, determines the torque application.
        static constexpr int parts[]={8,0,12,14,15,18,19,16,20,1,2,4,5,3,6,10};
        // Includes the headbutt in the installed combo. Hands may be preparing
        // or holding during that move: they are not automatically its striker.
        for(size_t frame=1;frame<attacker.history.size();frame++){
            const auto&previous=attacker.history[frame-1];const auto&current=attacker.history[frame];
            if(now-previous.time>60||current.anim!=attack.anim||previous.anim!=attack.anim||
               current.group!=attack.group||previous.group!=attack.group||current.target!=ref||previous.target!=ref||
               current.phase<previous.phase||current.phase>attack.time+.002f)continue;
            if(attack.contactTime>0&&(current.phase<attack.contactTime-.045f||previous.phase>attack.contactTime+.06f))continue;
            const float dt=(current.time-previous.time)*.001f;
            for(int row:{2,7,8,13,14}){
                if(!(allowed&(1u<<row))||row>=int(previous.rows.size())||row>=int(current.rows.size()))continue;
                const auto before=meleeSegment(previous.rows[row],row==2),after=meleeSegment(current.rows[row],row==2);
                for(size_t n=0;n<std::size(parts);n++){
                    ContactSegment body0,body1;SweptMeleeContact contact;
                    if(!meleeTargetAt(target->second,previous.time,n,body0)||!meleeTargetAt(target->second,current.time,n,body1)||
                       !sweptMeleeContact(before,after,body0,body1,dt,contact))continue;
                    const float when=float(previous.time)+contact.fraction*float(current.time-previous.time);
                    if(row==7||row==8){handGap[row-7]=contact.gap;handVelocity[row-7]=contact.relative;}
                    // First touched surface, not the deepest later penetration.
                    if(when<first){
                        first=when;best=contact.gap;limb=row;strikeVelocity=contact.relative;toward=contact.normal;
                        bestPoint=contact.point;r.part=parts[n];contactAge=now-unsigned(when);
                    }
                }
            }
        }
        push=r.weapon==0&&(attack.profile=="push"||coherentPush(handVelocity[0],handVelocity[1],handGap[0],handGap[1]))&&(limb==7||limb==8);
        if(limb>=0){
            r.contactKnown=true;r.contact=bestPoint;
            const float bodyMass=at<float>(source->second.ped,0x8C);
            r.strikingMass=std::isfinite(bodyMass)&&bodyMass>0?bodyMass*(push?.10f:limb==2?.08f:.045f):0;
            r.strikeVelocity=strikeVelocity;r.contactNormal=normalized(toward);
            r.direction=r.contactNormal;
        }
    }
    r.melee=meleeResponse(r.reactionDraw,push);
    r.meleeContactReady=true;
    event("melee_contact_response",ref,at<short>(victim,0x22),
        std::string("response=")+(r.melee==MeleeResponse::Balance?"balance":r.melee==MeleeResponse::Shove?"push":"flinch")+
        " measured="+std::to_string(r.contactKnown)+" limb="+std::to_string(limb)+" striker="+(limb==2?"head":limb==7?"left_hand":limb==8?"right_hand":limb==13?"left_foot":limb==14?"right_foot":"unknown")+
        " anim="+std::to_string(attack.anim)+" group="+std::to_string(attack.group)+" profile="+attack.profile+" move="+std::to_string(attack.move)+" part="+std::to_string(r.part)+
        " native_fallback_impulse="+std::to_string(length(r.impulse))+" draw="+std::to_string(r.reactionDraw)+" gap="+std::to_string(best)+
        " contact_age_ms="+std::to_string(contactAge)+" closing_mps="+std::to_string(dot(strikeVelocity,normalized(toward)))+
        " resolve_delay_ms="+std::to_string(now-r.time)+" hit_ms="+std::to_string(r.time)+" attacker="+std::to_string(r.attackerRef)+" native_part="+std::to_string(r.nativeMeleePart)+
        " contact_x="+std::to_string(r.contact.x)+" contact_y="+std::to_string(r.contact.y)+" contact_z="+std::to_string(r.contact.z)+" selection=nm_only_sa_adaptation_action_table_not_ported");
}
static void finishMeleeContacts(){
    for(auto&[ref,r]:pending)if(r.kind==Reaction::Melee&&!r.meleeContactReady){
        auto*p=pedFromRef(ref);if(!p||!matrix(p))continue;
        const auto a=animatedContacts.find(r.attackerRef),b=animatedContacts.find(ref);
        const bool sampled=a!=animatedContacts.end()&&b!=animatedContacts.end()&&a->second.time>=r.time&&b->second.time>=r.time;
        if(sampled||gameTime()-r.time>=50)configureMeleeRequest(r,p,0,true);
    }
}
