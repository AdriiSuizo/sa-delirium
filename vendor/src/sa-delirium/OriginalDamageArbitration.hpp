#pragma once
#include <cstdint>
// Original event semantics, never compare SA task/event ids to these numbers.
// TU8 CEventDamage::AffectsPed824DBA50 and TakesPriorityOver824DAF60.
// Actual hits only for concerns(): the separate looked-at/1027 visibility path
// needs its original vector/camera facts and is deliberately not approximated.
namespace original_damage_arbitration {
struct Hit {std::uint8_t flags=0;bool real=false;};
struct Victim {bool dead=false;unsigned root=0;bool dying=false;bool known=false;};
struct Source {bool exists=false,ped=false,player=false,friendOfVictim=false,known=false;};
struct Concerns {bool known=false,yes=false,alert=false;};
inline Concerns concerns(Hit h,Victim v,Source s){
    if(!h.real||!v.known||!s.known||(v.dead&&v.root==0))return {};
    if((h.flags&0xC0)!=0xC0)return {true,false,false};
    if(v.dead&&(v.root==218||v.dying))return {true,false,false};
    return {true,true,s.exists&&s.ped&&!s.friendOfVictim};
}
struct Running {unsigned kind=0;bool sameSource=false,basePriority=false,known=false;};
struct Override {bool known=false,yes=false;};
inline Override overrides(Hit h,Source s,bool responseIsNM,Running r){
    if(!s.known||!r.known)return {};
    const bool killed=h.real&&(h.flags&0x20);
    if((r.kind==66||r.kind==58)&&killed)return {true,true};
    if(r.kind==9&&((s.exists&&s.ped&&s.player)||!r.sameSource||killed))return {true,true};
    if(r.kind==130&&responseIsNM)return {true,true};
    return {true,r.basePriority};
}
}
