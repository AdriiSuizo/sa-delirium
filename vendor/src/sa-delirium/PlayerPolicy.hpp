#pragma once
#include <cmath>
// Traffic-event predicates. Player explosions and moving exits are separately
// authorized in Explosions.hpp/VehicleExits.hpp; player gunshots/melee stay native.
constexpr bool playerGroundVehicle(int type){return type==0||type==1||type==2||type==6||type==9||type==10||type==11;}
constexpr bool playerRoadDamage(int weapon,bool vehicleSource){return vehicleSource&&(weapon==49||weapon==50);}
inline bool playerMovingContact(float vehicleClosing,float relativeClosing){
    return std::isfinite(vehicleClosing)&&std::isfinite(relativeClosing)&&vehicleClosing>=.65f&&relativeClosing>=.65f;
}
inline bool playerCrashEjection(unsigned type,float impact,float damage){
    return (type==49||type==53||type==54)&&std::isfinite(impact)&&std::isfinite(damage)&&(impact>.0001f||damage>0.f);
}
inline bool playerForcedRiderRelease(unsigned type,bool knownOtherPed,float damage){
    // Native accepted pull-off/entry event (NONE/NONE_1), not an on-foot
    // melee/gunshot request. It carries the real jacker and no damage.
    return (type==0||type==55)&&knownOtherPed&&std::isfinite(damage)&&damage==0.f;
}
