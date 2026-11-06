#pragma once
// DisarmOpponent, the player's weapon take. IV (LSC4 wt/a1/rust/crates/gta4-viewer/src): meleetask.rs:1246-1251 (result
// flag +0x7D, def 0x822C4768 -> 0x822BF448: with a target, locked on, at the clip's 0x200 event) and weapons.rs:5359-5372
// ("his target's weapon is his (0x823D6988 with its ammo, then selected, 0x823D7D10) and it goes from the ped
// (0x823D7AE0)"; only a ped holding a melee weapon, not a gun). sa-combat decides all of it (Dodge_Club_* -> Disarm_Bat_Act ->
// Disarm_Bat_Res, runtime.rs hit kind 3 at the 0x200 event); this file is only the SA weapon change of hands.
// SDK eWeaponType: 2 golf club, 3 nightstick (police), 4 knife, 5 bat, 6 shovel, 7 pool cue, 8 katana, 9 chainsaw. 0/1 are
// fists (IV never takes knuckles), 10-15 are gifts. CWeapon (0x1C, at CPed+0x5A0 per slot): type +0, state +4, clip +8, total +0xC.
// Diagnostics (every refusal and the swap's read-back): GTASA_DISARM_TRACE.
inline bool meleeDisarmable(int weapon){return weapon>=2&&weapon<=9;}
static int meleeHeldWeapon(void*p,unsigned&slot){slot=at<unsigned char>(p,0x718);return slot<13?at<int>(p,0x5A0+slot*0x1C):-1;}
static bool meleeDisarm(void*by,void*victim){
    const bool trace=GetEnvironmentVariableA("GTASA_DISARM_TRACE",nullptr,0)!=0;
    unsigned vs=0,bs=0;const int type=by&&victim?meleeHeldWeapon(victim,vs):-1,mine=by&&victim?meleeHeldWeapon(by,bs):-1;
    if(by==victim||!meleeDisarmable(type)||mine>1){
        if(trace)event("combat_disarm_refused",victim?reference(victim):-1,victim?at<short>(victim,0x22):-1,"taker="+std::to_string(by?reference(by):-1)+" foe_weapon="+std::to_string(type)+" taker_weapon="+std::to_string(mine));
        return false;
    }
    // A melee weapon's ammo is its one round: keep what the foe had, never less than one.
    const unsigned ammo=std::max(1u,at<unsigned>(victim,0x5A0+vs*0x1C+0xC));
    // The taker first: its model reference is held before the foe's is dropped, so the clump is never unloaded in between.
    reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(by,type,ammo,false);   // CPed::GiveWeapon
    reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(by,type);                           // CPed::SetCurrentWeapon(type): selected, model attached
    reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E62B0)(victim,type);                       // CPed::ClearWeapon(type): slot emptied, model dropped
    unsigned s1=0,s2=0;const int taker=meleeHeldWeapon(by,s1),foe=meleeHeldWeapon(victim,s2);
    event("combat_disarm",reference(victim),at<short>(victim,0x22),"taker="+std::to_string(reference(by))+" weapon="+std::to_string(type)+" ammo="+std::to_string(ammo)+
          " taker_now="+std::to_string(taker)+" foe_now="+std::to_string(foe)+" foe_slot_total="+std::to_string(at<unsigned>(victim,0x5A0+vs*0x1C+0xC))+" original=meleetask.rs:1246,weapons.rs:5359");
    return taker==type&&foe!=type;
}
