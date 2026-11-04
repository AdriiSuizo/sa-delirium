#pragma once
#include "Math.hpp"
#include <string_view>
// SA adaptation boundary, not a replacement for GTA IV's ActionTable. The
// original dispatch separates balanced responses from NM flinch/fall. Never
// make a damage notification itself an instruction to knock a person down.
enum class MeleeResponse { Balance, Flinch, Shove };
inline unsigned strikerMask(std::string_view name){
    if(name=="head")return 1u<<2;
    if(name=="left_hand")return 1u<<7;
    if(name=="right_hand")return 1u<<8;
    if(name=="hands"||name=="push")return (1u<<7)|(1u<<8);
    if(name=="left_foot")return 1u<<13;
    if(name=="right_foot")return 1u<<14;
    return 0; // unprofiled clips retain native body-part attribution
}
inline V poseAngularVelocity(Q previous,Q current,float dt){
    if(!std::isfinite(dt)||dt<.002f||dt>.05f)return {};
    Q d=mul(current,conjugate(previous));
    if(d.w<0){d.x=-d.x;d.y=-d.y;d.z=-d.z;d.w=-d.w;}
    const V axis{d.x,d.y,d.z};const float sine=length(axis);
    if(sine<1.e-6f)return {};
    const V spin=axis*(2.f*std::atan2(sine,d.w)/(sine*dt));
    return finite(spin)&&length(spin)<=30.f?spin:V{};
}
inline MeleeResponse meleeResponse(float draw,bool twoHands){
    if(twoHands)return MeleeResponse::Shove;
    // GTA IV dispatcher 8226B618 (LSC4 peds.rs): after the NM gate,
    // rand(0..1)>0.7 selects Balance DEFAULT; otherwise NMFlinch MELEE.
    // Health belongs to the earlier animation/NM gate, NOT this choice.
    return std::isfinite(draw)&&draw>.7f?MeleeResponse::Balance:MeleeResponse::Flinch;
}
inline V contactMomentum(V relative,V toward,float strikingMass,float receivingMass){
    if(!finite(relative)||!finite(toward)||!std::isfinite(strikingMass)||!std::isfinite(receivingMass)||strikingMass<=0||receivingMass<=0)return {};
    const float closing=dot(relative,normalized(toward));
    if(closing<=0)return {}; // a withdrawing/preparatory limb cannot push
    // Inelastic finite-mass normal impulse, once per native accepted hit.
    // Health points are not Newton seconds. No health-scaled ceiling, minimum
    // kick, imposed vertical direction or post-solver pose/velocity clamp.
    return normalized(toward)*(closing/(1.f/strikingMass+1.f/receivingMass));
}
inline bool coherentPush(V left,V right,float leftGap,float rightGap){
    return finite(left)&&finite(right)&&leftGap<.12f&&rightGap<.12f&&
        length(left)>.3f&&length(right)>.3f&&
        dot(normalized(left),normalized(right))>.8f&&
        std::min(length(left),length(right))>.55f*std::max(length(left),length(right));
}
