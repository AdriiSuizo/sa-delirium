#pragma once
#include "Math.hpp"
#include "SABones.hpp"
// All values use metres, seconds and kg*m/s, never SA's animation launch kicks.
inline bool articulatedHelper(int id){return saBone::face(id)||id==35||id==36||id==25||id==26||id==44||id==54||id>=200;}
inline V inheritedVelocity(V linear,V angular,V point,V centre){return linear+cross(angular,point-centre);}
inline bool movingExit(float speed,float thresholdKmh){return std::isfinite(speed)&&speed>=thresholdKmh/3.6f;}
inline float lateralExitClearance(float x,float minX,float maxX,int side,float radius){return side>0?x-maxX-radius:minX-x-radius;}
inline float pedMomentumCorrection(float before,float after,float carMass,float pedMass,bool rigid){
    if(rigid||!std::isfinite(before)||!std::isfinite(after)||!std::isfinite(carMass)||!std::isfinite(pedMass)||before<.65f||carMass<250.f||pedMass<=0)return 0;
    // A bounded inelastic mass ratio, never acceleration above pre-impact speed.
    return std::clamp(before*carMass/(carMass+pedMass)-after,0.f,before);
}
inline float blastDamage(float distance,float radius){
    if(!std::isfinite(distance)||!std::isfinite(radius)||radius<=0)return 0;
    float t=std::clamp(distance/radius,0.f,1.f);
    // Close blasts remain dangerous; the middle band injures instead of the
    // native plateau of 250 damage. Armour and native player scaling still apply.
    return t<.20f?250.f:110.f*std::pow(1.f-t,1.25f);
}
inline V directedHit(V direction,float magnitude){
    if(!finite(direction)||!std::isfinite(magnitude))return {};
    direction=normalized(direction);
    // Do not invent vertical lift for bullets or punches; their direction is
    // measured from the hit/source, with a modest vertical component bound.
    direction.z=std::clamp(direction.z,-.2f,.2f);
    const float flat=std::sqrt(direction.x*direction.x+direction.y*direction.y);
    if(flat>1.e-6f){const float scale=std::sqrt(1.f-direction.z*direction.z)/flat;direction.x*=scale;direction.y*=scale;}
    return direction*magnitude;
}
