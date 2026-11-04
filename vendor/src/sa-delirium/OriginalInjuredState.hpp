#pragma once
#include <algorithm>
#include <cmath>
// TU8 InjuredOnGround278, not SA's living/flee/get-up decision.
// 824CCB38 initial health/flag7; 824CA2C0 health portion and revived gate.
namespace original_injured_state {
struct Facts {float health=0,seconds=0;bool nmOwnsBody=false,bit2=false,flag7=false,medic=false,ambient291=false;unsigned deathState=0;};
struct Result {bool valid=false;float health=0;bool flag7=false,end=false,clearBit2=false;};
inline float initialHealth(float health){return health<100?std::min(health,75.0f):health;}
inline bool initialFlag7(bool bit2,float actualRandom){return bit2&&actualRandom<.5f;}
inline Result control(const Facts&f){
    if(!std::isfinite(f.health)||!std::isfinite(f.seconds)||f.seconds<0)return {};
    float h=f.health;bool flag=f.flag7;
    if(h<50)h+=f.seconds*-3.0f;
    else if(f.medic)flag=false;
    else if(!f.nmOwnsBody)h+=f.seconds*3.0f;
    flag=flag&&f.bit2;h=std::min(h,flag?95.0f:94.0f);
    const bool dead=h<=0&&(f.deathState==1||f.deathState==2);
    const bool end=f.ambient291&&(dead||h>=95);
    return {true,h,flag,end,end&&!dead};
}
}
