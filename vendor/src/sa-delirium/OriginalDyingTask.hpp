#pragma once
#include <cmath>
// TU8 CTaskComplexDie::CreateNextSubTask 824CA7F0, NM cases at824CAB90.
// This is the original decision, not gta4-viewer's shortened corpse path.
// Non-NM/on-vehicle/task override branches must be handled separately.
namespace original_dying_task {
enum class Next { Unsupported,DieClip,Relax,OnFire,InjuredOnGround };
struct Facts {
    unsigned finished=0,deathState=0;float health=0;
    bool inVehicle=false,nmOwnsBody=false,nmShotFlag5=false,entityBit10=false,deathNotified=false;
};
struct Result {
    Next next=Next::Unsupported;unsigned minMs=0,maxMs=0,group=0,clip=0;
    bool queueDeath=false;
};
inline bool isDead(const Facts&f){return f.health<=0&&(f.deathState==1||f.deathState==2);}
inline Result next(const Facts&f){
    if(!std::isfinite(f.health)||f.inVehicle||
        (f.finished!=2104&&f.finished!=2107&&f.finished!=2110&&f.finished!=2111))return {};
    if(!f.nmOwnsBody)return {Next::DieClip,0,0,0x2C,0xBE};
    if(f.finished==2107&&f.nmShotFlag5)return {Next::Relax,2000,10000};
    if(isDead(f))return {Next::Relax,1000,10000,0,0,!f.deathNotified};
    if(f.finished==2110)return {Next::Relax,1000,10000};
    if(f.finished==2111)return f.entityBit10?Result{Next::Relax,250,500}:Result{Next::Relax,1000,10000};
    if(f.finished==2104&&f.entityBit10)return {Next::OnFire,5000,15000};
    return {Next::InjuredOnGround};
}
}
