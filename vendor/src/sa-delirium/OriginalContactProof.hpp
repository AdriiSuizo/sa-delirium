#pragma once
#include "OriginalContactHealth.hpp"
// TU8 824DA160 / 82482238 / 8247F1D8 for contact weapon rows NONE.
// Facts are explicit original semantics. This does not reinterpret SA bitfields.
namespace original_contact_health {
struct ProofFacts {
    unsigned weapon=50,flags=0,victimGroup=0,sourceGroup=0;
    Attacker source=Attacker::None;
    bool sourceGroupKnown=false,sourcePopulation=false,samePedGroup=false;
    bool player=false,playerHurtable=false,ragdolling=false;
    bool factsKnown=false,singlePlayer=true,sourceOwnedByVictim=false;
};
inline Proof permitted(const ProofFacts&f){
    if(!f.factsKnown||!f.singlePlayer||f.victimGroup>0xFFFF||
        (f.weapon!=49&&f.weapon!=50&&f.weapon!=54))return Proof::Unknown;
    if(f.player&&!f.playerHurtable)return Proof::Denied;
    if(f.source==Attacker::Ped&&f.samePedGroup)return Proof::Denied;
    if(f.flags&0x00200000)return Proof::Denied; // invincible
    // 82482238: source +1BC names victim AND source +1E2 low nibble>=2.
    // FIRE/EXPLOSIVE are exempt there, but contact rows here are NONE.
    if(f.source!=Attacker::None&&f.sourceOwnedByVictim)return Proof::Denied;
    if((f.flags&0x00100000)&&!(f.source==Attacker::Player||
        (f.source==Attacker::Vehicle&&!f.sourcePopulation)))return Proof::Denied;
    const bool hasGroup=f.source==Attacker::Ped||f.source==Attacker::Player||
        (f.source==Attacker::Vehicle&&f.sourceGroupKnown);
    const unsigned sourceGroup=f.sourceGroupKnown?f.sourceGroup:0;
    if((f.flags&0x00040000)&&(!hasGroup||(sourceGroup&0xFFFF)!=f.victimGroup))return Proof::Denied;
    if((f.flags&0x00020000)&&hasGroup&&sourceGroup==f.victimGroup)return Proof::Denied;
    if(f.flags&0x00800000)return Proof::Denied; // collision proof
    if(f.weapon==54&&f.ragdolling)return Proof::Denied;
    return Proof::Allowed;
}
}
