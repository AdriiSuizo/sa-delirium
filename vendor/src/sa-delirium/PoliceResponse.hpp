#pragma once
// GTA IV police answer to an NPC attacker. Included by Adapter.cpp after CombatRuntime.hpp.
// IV Cop.ped (common/data/decision, the PC copy of the file the 360 store loads, events/decisions.md): damage (event 9),
// gunshots (15), gun aimed (31), bullet impact / whizz-by (116 / 49) from anyone who is not a friend get Combat (1900, or the
// 1700 communicate); Cop.ped has no flee response at all (R_Norm.ped flees, a cop never does).
// SA instead lets a cop whose held weapon is melee flee an armed ped: CEventHandler::IsKillTaskAppropriate (0x4BC3E0) is false
// for ped1 holding a melee weapon against ped2 holding a non-melee one, and city cops hold fists (CCopPed gives the nightstick
// and a delayed pistol without SetCurrentWeapon). For an ambient cop attacked by another ambient ped this answers true, so SA's
// own kill task runs (it draws the pistol: checked with Test-CombatRuntime -Scenario cop-punch). Mission peds already get true,
// the player stays with SA's wanted system, cop on cop is untouched.
// Not ported: the 1700 communicate to nearby cops (IV's intelligence +0xE8 count / +0xEC range defaults are not known here).
static bool ivPoliceResponse=true;
// 0x4BC3E0 is __cdecl in the exe (ped1 [esp+4], ped2 [esp+8], plain ret), not the member gta-reversed shows.
static bool(__cdecl*originalKillTaskAppropriate)(void*,void*,const void*)=nullptr;
static bool __cdecl killTaskAppropriate(void*ped,void*source,const void*e){
    const bool native=originalKillTaskAppropriate(ped,source,e);
    if(native||!ivPoliceResponse||!ped||!source||source==player())return native;
    // +0x598 ped type (6 cop), +0x484 created by (2 mission).
    if(at<int>(ped,0x598)!=6||at<int>(source,0x598)==6||at<unsigned char>(ped,0x484)==2)return native;
    event("iv_police_response",reference(ped),at<short>(ped,0x22),"source="+std::to_string(reference(source))+" source_weapon="+std::to_string(combatWeapon(source))+" cop_weapon="+std::to_string(combatWeapon(ped))+" native_flee=1 combat=1");
    return true;
}
