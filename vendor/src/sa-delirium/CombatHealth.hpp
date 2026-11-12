#pragma once
#include <algorithm>

namespace combat_health {
constexpr float injured=100.f;
constexpr float healthy(float native,bool player){
    if(!(native>0.f))return 0.f;
    return player?injured+native:2.f*native;
}
constexpr float nativeDamage(float damage,bool player){return player?damage:damage*.5f;}
constexpr float visible(float health,float maximum){
    return maximum>injured?std::clamp((health-injured)/(maximum-injured),0.f,1.f):0.f;
}
constexpr float bulletDamage(float base,float weakness,bool player,bool populationSeated,bool friendly){
    float result=base*(player?.33f:weakness);
    if(populationSeated)result*=1.5f;
    return friendly?std::min(12.5f,result*.33f):result;
}
constexpr float boneShare(unsigned bone){
    switch(bone){
        case 0x4C1:case 0x4C2:case 0x4C8:case 0x4C9:
        case 0x1A2:case 0x1A3:case 0x1A7:case 0x1A8:return .75f;
        case 0x4C3:case 0x4D0:case 0x1A4:case 0x1A9:return .5f;
        default:return 1.f;
    }
}
constexpr float meleeDamage(float base,float weakness,bool player,unsigned bone,bool club,bool guard){
    return base*(player?1.f:weakness)*boneShare(bone)*(club&&bone==0x4B5?4.f:1.f)*(guard?.5f:1.f);
}
}
