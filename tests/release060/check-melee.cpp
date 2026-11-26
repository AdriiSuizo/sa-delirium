#include "CombatHealth.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace combat_health;
int main(){
    constexpr unsigned head=0x4B5, wrist=0x4C3, chest=0x4B3;
    constexpr auto applied=[](float sourceDamage,unsigned bone,bool player,bool blunt,bool blocked){
        return nativeDamage(meleeDamage(sourceDamage,1.f,player,bone,blunt,blocked),player);
    };
    static_assert(applied(30,head,true,true,false)==30);
    static_assert(applied(30,head,true,true,true)==15);
    static_assert(applied(30,head,false,true,false)==60);
    static_assert(applied(30,head,false,false,false)==15);
    static_assert(applied(30,chest,true,true,false)==30);
    static_assert(applied(30,wrist,true,true,false)==15);
    static_assert(applied(0,head,true,true,false)==0);
    for(float initial:{100.f,95.6008f}){
        const float health=initial-applied(30,head,true,true,false);
        assert(health>0&&std::abs(health-(initial-30.f))<.001f);
        std::printf("Baton critical on CJ: %.4f -> %.4f\n",initial,health);
    }
    assert(25.f-applied(30,head,true,true,false)<=0);
}
