#pragma once
#include <cstdint>
// TU8 ChargeMelee(431) vf5=822C3328 and MeleeAction(433) vf5=822C1708.
// Original event kinds and urgencies only. No SA task/event ids are accepted.
// Pure decision + effects receipt: animation services remain the host's source
// association owner; this does not call SA MakeAbortable or alter body velocity.
namespace original_melee_abort {
struct Event {bool present=false;unsigned kind=0;int priority=0;};
struct ChargeFacts {unsigned urgency=0;Event event{};bool aimedProtected=false;bool childAccepted=false;};
struct ChargeResult {bool delegate=false,accepted=false,cleanup=false;};
inline ChargeResult charge(const ChargeFacts&f){
    if(f.urgency!=2&&f.event.present){
        if(f.event.kind==125||f.event.kind==126||(f.event.kind==31&&f.aimedProtected)||f.event.priority<=40||f.event.priority==83)return {};
    }
    return {true,f.childAccepted,f.childAccepted};
}
struct ActionFacts {
    unsigned urgency=0;Event event{};bool association=false;
    unsigned associationType=0;bool associationHierarchy=false;
    bool episode=false,pairedWithGlobal=false;unsigned taskFlags=0,associationFlags=0;
    float ordinaryBlend=0,immediateBlend=0;
};
struct ActionResult {
    bool accepted=false,blend=false,release=false,finished=false,clearPairGlobal=false,resetTimeScale=false;
    unsigned taskFlags=0,associationFlags=0;float blendRate=0;
};
inline ActionResult action(const ActionFacts&f){
    ActionResult r;r.taskFlags=f.taskFlags;r.associationFlags=f.associationFlags;
    if(f.association&&!(f.event.present&&f.event.kind==10)&&f.associationType==1&&f.associationHierarchy){
        r.blend=true;r.blendRate=f.urgency==2?f.immediateBlend:f.ordinaryBlend;
        if(f.episode&&f.pairedWithGlobal)r.associationFlags|=0x00400000;
        r.associationFlags|=0x4000;
    }
    if(r.taskFlags&0x40){r.clearPairGlobal=true;r.taskFlags&=~0x40u;}
    if(r.taskFlags&0x20){r.resetTimeScale=true;r.taskFlags&=~0x20u;}
    if(f.urgency==0){r.accepted=!f.association;return r;}
    r.accepted=true;r.release=f.association;r.finished=f.association;return r;
}
}
