#pragma once
#include "Math.hpp"
#include <vector>

// Fit an evaluated native animation to the physical pose. Only yaw and XY
// translation are free: the native collision support determines height.
struct RecoveryFit {T delta{};float error=1.e9f;bool valid=false;};
inline RecoveryFit fitRecovery(const std::vector<T>& native,const std::vector<T>& body,const std::vector<int>& nodes){
    RecoveryFit fit;if(native.size()!=body.size()||nodes.empty())return fit;
    V ca{},cb{};for(int i:nodes){if(i<0||size_t(i)>=native.size()||!finite(native[i].p)||!finite(body[i].p))return fit;ca=ca+native[i].p;cb=cb+body[i].p;}
    ca=ca*(1.f/nodes.size());cb=cb*(1.f/nodes.size());float d=0,c=0;
    for(int i:nodes){V a=native[i].p-ca,b=body[i].p-cb;d+=a.x*b.x+a.y*b.y;c+=a.x*b.y-a.y*b.x;}
    if(d*d+c*c<1.e-8f)return fit;
    float yaw=std::atan2(c,d);fit.delta.q={0,0,std::sin(yaw*.5f),std::cos(yaw*.5f)};
    fit.delta.p=cb-rotate(fit.delta.q,ca);fit.delta.p.z=0;fit.error=0;
    for(int i:nodes){V e=mul(fit.delta,native[i]).p-body[i].p;fit.error+=dot(e,e);}
    fit.error/=nodes.size();fit.valid=std::isfinite(fit.error);return fit;
}
// Original NM response completion (8261E688): Blend returns a standing
// physical owner to animation; a bent spine does not request a GetUp clip.
// Unported response kinds retain their existing geometric fallback.
inline bool originalStandingRecovery(bool sourceResponse,bool balancing,bool fallen,bool geometric){
    return sourceResponse&&balancing?!fallen:geometric;
}
inline float recoveryEase(float t){t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
