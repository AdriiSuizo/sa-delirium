#pragma once
#include <cstdint>
inline bool firearmDamage(int weapon){return (weapon>=22&&weapon<=34)||weapon==38||weapon==52;}
inline bool clubWeapon(int weapon){return weapon==3||weapon==5;}
struct ShotDamageReceipt {
    std::uint32_t time=0;
    std::uintptr_t source=0;
    int weapon=-1,piece=-1;
};
inline bool sameDeliveredShot(const ShotDamageReceipt&a,const ShotDamageReceipt&b,std::uint32_t now){
    return firearmDamage(a.weapon)&&a.time==b.time&&a.source==b.source&&a.weapon==b.weapon&&a.piece==b.piece&&now-a.time<=150u;
}
// Native damage calculation and event-task delivery can happen on different
// frames. Match values, never an event pointer (SA clones/deletes events).
// The vehicle reference includes its pool generation.
struct RoadDamageReceipt {
    std::uint32_t time=0;
    int vehicle=-1,weapon=-1,piece=-1;
};
inline bool sameDeliveredRoadDamage(const RoadDamageReceipt& accepted,const RoadDamageReceipt& delivered,std::uint32_t now){
    return accepted.vehicle>=0&&(accepted.weapon==49||accepted.weapon==50)&&
        accepted.time==delivered.time&&accepted.vehicle==delivered.vehicle&&
        accepted.weapon==delivered.weapon&&accepted.piece==delivered.piece&&
        now-accepted.time<=150u;
}
// Authored melee also queues a normal SA damage event after its sweep has
// already started physics. Only that exact event may rebind the same body.
// Ped references include pool generations; never match only a raw pointer.
struct CombatDamageReceipt {
    std::uint32_t time=0;
    int source=-1,weapon=-1,piece=-1;
};
inline bool sameDeliveredCombatDamage(const CombatDamageReceipt& accepted,const CombatDamageReceipt& delivered,std::uint32_t now){
    return accepted.source>=0&&((accepted.weapon>=0&&accepted.weapon<=1)||clubWeapon(accepted.weapon))&&
        accepted.time==delivered.time&&accepted.source==delivered.source&&
        accepted.weapon==delivered.weapon&&accepted.piece==delivered.piece&&
        now-accepted.time<=150u;
}
