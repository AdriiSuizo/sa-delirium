#pragma once
#include <cmath>
#include <cstdint>
// Original TU8 82465360..824657FC, before CPedDamage. Source correspondence:
// Claude 0b0aef3a rust/crates/gta4-nm/src/bump.rs::damage_prop.
// These are original gameplay inputs; no SA flags/enums are silently reused.
// Not installed: the caller must prove one solved contact-record consumption.
namespace original_impact_damage {
struct Vec {float x=0,y=0,z=0;};
inline float dot(Vec a,Vec b){return std::fma(a.x,b.x,std::fma(a.y,b.y,a.z*b.z));}
enum class Kind { None,Building,Vehicle,AnimatedPed,PhysicalPed,Object,Other };
struct Train {
    bool known=false,hasBound=false;
    Vec right{1,0,0},forward{0,1,0},place{},velocity{},size{};
};
struct Facts {
    float impulse=0,dt=0,invMass=0,health=0,vz=0,drop=0;
    int component=0;
    Kind other=Kind::None;
    bool woke=false,dropKnown=false,tough=false,playerCut=false;
    Vec place{};
    Train train{};
    // The original one CRT draw when a vehicle wakes this body, supplied once
    // by the shared source RNG. No draw is made by this pure calculator.
    float propDraw=0;
};
struct Result {bool valid=false,hasDamageEvent=false,knockProp=false;float amountIV=0;int weapon=54;};
inline Result calculate(const Facts&f){
    Result r;
    if(!std::isfinite(f.impulse)||f.impulse<0||!std::isfinite(f.dt)||f.dt<=0||
       !std::isfinite(f.invMass)||f.invMass<=0||!std::isfinite(f.health)||
       !std::isfinite(f.vz)||(f.dropKnown&&!std::isfinite(f.drop)))return r;
    const bool car=f.other==Kind::Vehicle;
    r.valid=true;r.weapon=car?50:54;
    if(car&&f.woke){
        if(!std::isfinite(f.propDraw)||f.propDraw<0||f.propDraw>1)return {};
        r.knockProp=(f.impulse>30?f.propDraw*2.f:f.propDraw)>.9f;
    }
    if(f.dropKnown&&f.drop>(f.tough?15.f:10.f)&&!(f.vz<0))r.amountIV=f.health*100.f;
    else {
        float force=0;
        if(f.other!=Kind::AnimatedPed){
            const bool limb=f.component==2||f.component==3||f.component==5||f.component==6;
            const float share=f.other==Kind::PhysicalPed?10.f:
                ((f.other==Kind::None||f.other==Kind::Building)&&limb?7.f:2.5f);
            const float gravity=static_cast<float>(9.8*static_cast<double>(f.dt));
            force=std::fma(-gravity,share,f.invMass*f.impulse);
        }
        r.amountIV=(f.playerCut?.66f:1.f)*(force<0?0.f:force*(car?6.f:3.f));
    }
    if(car&&f.train.known&&f.train.hasBound){
        const auto&t=f.train;const Vec d{f.place.x-t.place.x,f.place.y-t.place.y,f.place.z-t.place.z};
        const float speed2=std::fma(t.velocity.z,t.velocity.z,std::fma(t.velocity.x,t.velocity.x,t.velocity.y*t.velocity.y));
        if(speed2>20.f&&dot(t.forward,d)+1.f>t.size.y*.5f&&std::fabs(dot(t.right,d))<t.size.x*.5f){
            const float forwardSpeed=std::fma(t.forward.x,t.velocity.x,std::fma(t.forward.z,t.velocity.z,t.forward.y*t.velocity.y));
            r.amountIV=((f.invMass*forwardSpeed)*f.impulse)*100.f;
        }
    }
    r.hasDamageEvent=r.amountIV>0||f.woke;
    return r;
}
}
