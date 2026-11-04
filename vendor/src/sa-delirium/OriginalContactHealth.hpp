#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Single-player TU8 CPedDamage, restricted to physical-contact weapon rows
// RAMMEDBYCAR49/RUNOVERBYCAR50/FALL54 (original blank rows: type NONE).
// Source: 824DC670 -> 824DA600 -> 824DB3E0 -> 824DB650. This module has
// no SA health conversion, native event dispatch, animation or impulse side effect.
namespace original_contact_health {
enum class Attacker : unsigned { None, Ped, Player, Vehicle };
enum class Proof : unsigned { Unknown, Denied, Allowed };
struct Health { float health=200,armour=0; };
struct Facts {
    float raw=0,modelDefendWeakness=0;
    unsigned weapon=50,bone=0;
    Attacker attacker=Attacker::None;
    Proof proof=Proof::Unknown; // actual 824DA160 policy, not SA AffectsPed.
    bool player=false,inVehicle=false,population=false,mission=false;
    bool sameGroup=false,friendToPlayer=false,selfSource=false;
    bool diesWhenInjured=false,deepWater=false,fallFloor=false,pedFloor=false;
    bool armourPenetrating=false,modelFactorKnown=false;
};
struct Outcome {
    // Original +4 bits, not SA eDamageResponse flags.
    std::uint8_t flags=0;
    float lost=0,absorbed=0,scaled=0;
    bool armourTimeWritten=false;
    bool dies()const{return (flags&0x30)!=0;}
};
enum class Status { Invalid,UnknownFacts,AlreadyCalculated,Protected,Calculated };
inline float boneShare(unsigned bone){
    switch(bone){
    case 0x4C1:case 0x4C2:case 0x4C8:case 0x4C9:
    case 0x1A2:case 0x1A3:case 0x1A7:case 0x1A8:return .75f;
    case 0x4C3:case 0x4D0:case 0x1A4:case 0x1A9:return .5f;
    default:return 1;
    }
}
inline Status apply(Health&h,const Facts&f,Outcome&o){
    if((f.weapon!=49&&f.weapon!=50&&f.weapon!=54)||!std::isfinite(f.raw)||f.raw<0||
       !std::isfinite(h.health)||!std::isfinite(h.armour)||h.armour<0||
       (f.population&&f.mission)) // one original population-type byte, not two simultaneous flags
        return Status::Invalid;
    // 824DC690 tests the calculated bit. Keep the caller's previous result.
    if(o.flags&0x80)return Status::AlreadyCalculated;
    // Bit1 is the network/prediction no-health-write path. Not a single-player
    // contact receipt; do not silently perform its distinct mutation contract.
    if(o.flags&2)return Status::UnknownFacts;
    if((h.health>=100&&f.proof==Proof::Unknown)||
       (!f.player&&(!f.modelFactorKnown||!std::isfinite(f.modelDefendWeakness)||f.modelDefendWeakness<0)))
        return Status::UnknownFacts;
    o.flags=std::uint8_t((o.flags&3)|0x80);o.lost=o.absorbed=0;o.armourTimeWritten=false;
    if(h.health>=100&&f.proof==Proof::Denied)return Status::Protected;
    o.flags|=0x40;
    float a=f.raw*(f.player?.33f:f.modelDefendWeakness);
    a*=boneShare(f.bone);
    if(f.inVehicle&&f.population)a*=1.5f;
    // Scale skips self-attribution; relationship facts must describe a source
    // ped, never a car merely because its driver is a friend.
    bool cut=!f.selfSource&&((f.attacker==Attacker::Player&&f.sameGroup&&f.raw!=5555.55f)||
        (f.attacker==Attacker::Ped&&((f.player&&f.friendToPlayer)||(f.mission&&f.sameGroup))));
    if(cut)a=std::min(a*.33f,12.5f);
    o.scaled=a;
    if(h.armour>0&&!f.armourPenetrating){
        o.armourTimeWritten=f.player;
        if(a>h.armour){o.absorbed=h.armour;h.armour=0;}
        else{o.absorbed=a;h.armour-=a;}
    }
    const float damage=std::max(a-o.absorbed,0.0f),remaining=h.health-damage;
    const bool floor=f.pedFloor||(f.weapon==54&&f.fallFloor&&h.health>100);
    if(floor){const float left=std::max(remaining,105.0f);o.lost=h.health-left;h.health=left;}
    else if(remaining>=100){o.lost=damage;h.health+=-damage;}
    else if(remaining>=1&&!(f.player||f.inVehicle||f.diesWhenInjured||f.deepWater)){
        o.flags|=0x10;o.lost=damage;h.health+=-damage;
    }else{o.flags|=0x20;o.lost=h.health;h.health=0;}
    return Status::Calculated;
}
}
