#pragma once
#include <cmath>

// Original GTA IV TU8 CPedMoveBlendOnFoot::vf4 (824138D0), called by
// CPed's pre-physics 822F1288. Pure data adapter, NOT an installed SA hook.
// desired is the evaluated authored world velocity, not bone-point velocity.
// All quantities are metres/seconds. The returned delta is applied as
// force = delta * mass / substepSeconds before the common contact solve.
namespace original_ped_motor {
struct Vec { float x{}, y{}, z{}; };
inline Vec operator+(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator-(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec operator*(Vec a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float dot(Vec a,Vec b){return std::fma(a.z,b.z,std::fma(a.y,b.y,a.x*b.x));}
enum class Support { None, Ped, Vehicle, Other };
struct Facts {
    Vec authored{}, actual{};
    Support support{Support::None};
    int vehicleType{-1}; // IV: 2 boat, 3 train; never pass an SA subtype enum.
    float supportRadius{};
    // The actual support velocity at ped+A10. If the support has a collider,
    // include its vf23(5C) predictor contribution for this same substep.
    Vec supportPointVelocity{};
    Vec groundPoint{}, groundNormal{0,0,1};
    bool player{};
    bool slopeProjectionSuppressed{}; // original ped+14C bit0; host mapping pending
    bool drivesVertical{};            // original blender+50 bit25
    bool slideIntoCover{};            // original ped+270 bit19, writer 8269D640
    bool fallOrGetUp{};               // original ped+26C bit31, task-owned
    float health{100};
    bool doorPushMemory{};            // original ped+269 !=0, writer 823C392C
};
struct State { Vec slopeAnchor{}; };
struct Result {
    Vec deltaVelocity{}, desiredVelocity{};
    float acceleration{};
    bool projectedAgainstSlope{}; // writes original ped+270 bit28
};
inline bool zero(Vec v){return v.x==0 && v.y==0 && v.z==0;}
inline Result step(const Facts& f,State& s,float seconds){
    Result r{};r.desiredVelocity=f.authored;
    const bool supported=f.support!=Support::None;
    const bool vehicle=f.support==Support::Vehicle;
    if(supported&&f.support!=Support::Ped&&f.supportRadius>0.25f)
        r.desiredVelocity=r.desiredVelocity+f.supportPointVelocity;
    // 82413A20..BFC: slope contact history belongs to the motor, not a timer.
    if(f.groundNormal.z<0.707099974155426f&&f.player&&!f.slopeProjectionSuppressed){
        if(vehicle&&f.vehicleType==2){s.slopeAnchor={};}
        else if(zero(s.slopeAnchor)){s.slopeAnchor=f.groundPoint;}
        else {
            Vec n{f.groundNormal.x,f.groundNormal.y,0};
            const float l=std::sqrt(dot(n,n));
            if(l>0)n=n*(1.0f/l);
            if(dot(f.groundPoint-s.slopeAnchor,n)<-0.2f){
                const float against=-dot(r.desiredVelocity,n);
                if(against>0){
                    r.desiredVelocity={std::fma(n.x,against,r.desiredVelocity.x),
                        std::fma(n.y,against,r.desiredVelocity.y),std::fma(n.z,against,r.desiredVelocity.z)};
                    r.projectedAgainstSlope=true;
                }
            }
        }
    }else s.slopeAnchor={};
    r.deltaVelocity=r.desiredVelocity-f.actual;
    if(!f.drivesVertical)r.deltaVelocity.z=0;
    float acceleration=f.slideIntoCover?100.0f:30.0f;
    if(f.fallOrGetUp)acceleration=vehicle&&f.vehicleType!=2&&f.vehicleType!=3?1.0f:10.0f;
    if(f.health<100&&vehicle&&f.vehicleType!=2)acceleration=0.5f;
    else if(f.doorPushMemory)acceleration=0.1f;
    r.acceleration=acceleration;
    // 82413D28..D7C: magnitude bound, NOT independent per-axis clamps.
    const float maxDelta=acceleration*seconds;
    const float limitSquared=((acceleration*acceleration)*seconds)*seconds;
    const float lengthSquared=dot(r.deltaVelocity,r.deltaVelocity);
    if(lengthSquared>limitSquared)
        r.deltaVelocity=r.deltaVelocity*(maxDelta/std::sqrt(lengthSquared));
    return r;
}
struct Authority {
    bool grounded{}, drivesVertical{}, dyingOrDead{}, naturalMotionInstance{};
};
inline bool hasForceAuthority(const Authority& f){
    // CPed 822F1288 gates: +23C bit31 OR blender+50 bit25, not dying/dead
    // (+1E2 low nibble>=2), not the ped's NM fragment (+38 == +704).
    return (f.grounded||f.drivesVertical)&&!f.dyingOrDead&&!f.naturalMotionInstance;
}
inline Vec force(const Result&r,float mass,float seconds){
    return r.deltaVelocity*(mass/seconds); // caller supplies positive native substep
}
}
