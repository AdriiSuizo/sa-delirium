#pragma once
#include "NpcGaitData.hpp"

namespace armed_step {
inline bool aims(int pending,int last){
    const int cmd=pending>0?pending:last;
    return cmd>=1&&cmd<=3;
}
inline unsigned sector(float right,float forward){
    const int octant=int(std::lround(std::atan2(right,forward)*1.273239545f));
    return unsigned(octant+8)%8;
}
inline const char* clip(bool fast,unsigned dir){
    static const char* walk[]={"move_combat_strafe/walk","move_combat_strafe/walk_strafe_r45",
        "move_combat_strafe/walk_strafe_r","move_combat_strafe/walk_strafe_br45","move_combat_strafe/walk_strafe_b",
        "move_combat_strafe/walk_strafe_bl45","move_combat_strafe/walk_strafe_l","move_combat_strafe/walk_strafe_l45"};
    static const char* run[]={"move_combat_strafe/run","move_combat_strafe/run_strafe_r45",
        "move_combat_strafe/run_strafe_r","move_combat_strafe/run_strafe_br45","move_combat_strafe/run_strafe_b",
        "move_combat_strafe/run_strafe_bl45","move_combat_strafe/run_strafe_l","move_combat_strafe/run_strafe_l45"};
    return fast?run[dir]:walk[dir];
}
struct SideStep {unsigned decision=0,clearance=0,variation=0;int side=0;bool brisk=false;};
struct ClipLayer {const char* key=nullptr;float weight=0;};
struct Blend {
    float right=0,forward=0,phase=0;
    std::array<ClipLayer,5> layers{};
    int count=0;
    void select(float x,float y,float dt){
        // lsc4-combat/strafe.rs: StrafeRatios::step and 8-way strafe_clips.
        const float change=8.f*dt;
        right+=std::clamp(x-right,-change,change);
        forward+=std::clamp(y-forward,-change,change);
        const float magnitude=std::hypot(right,forward);
        const float run=std::clamp(magnitude-1.f,0.f,1.f);
        const float walk=std::min(1.f,magnitude)-run;
        float way=std::atan2(right,forward)*(4.f/3.14159265f);
        if(way<0)way+=8.f;
        const unsigned first=unsigned(way);
        const float fraction=way-first;
        count=0;
        auto add=[&](const char* key,float weight){if(weight>1.e-5f)layers[count++]={key,weight};};
        for(int gait=0;gait<2;gait++){
            const float w=gait?run:walk;
            add(clip(gait!=0,first%8),w*(1.f-fraction));
            add(clip(gait!=0,(first+1)%8),w*fraction);
        }
        add("move_combat_strafe/idle",std::max(0.f,1.f-magnitude));
    }
    void tick(float dt,float weightedDuration,float sum){
        // animblend::sync_phases: all playing layers share one normalized phase.
        if(sum>0&&weightedDuration>0)phase=std::fmod(phase+dt*sum/weightedDuration,1.f);
    }
};
template<class Free> inline int plan(SideStep& a,unsigned now,Free available){
    if(now>=a.decision){
        a.variation=1664525u*a.variation+1013904223u;
        if(a.side){a.side=0;a.decision=now+400+(a.variation%850);return 0;}
        const int first=(a.variation&0x10000)?1:-1;
        a.brisk=(a.variation&0x700)==0;
        a.side=available(first,1.f)?first:available(-first,1.f)?-first:0;
        a.decision=now+(a.side?550:320)+(a.variation%390);
        a.clearance=now+120;
    }
    if(a.side&&now>=a.clearance){
        a.clearance=now+120;
        if(!available(a.side,.82f)){a.side=0;a.decision=now+260;}
    }
    return a.side;
}
// The retargeter returns model-space bone transforms. Keep the native hip
// sockets, weapon IK and joint offsets, even when the torso turns independently.
using Rotations=std::array<Q,npc_gait::maxTags>;
inline Rotations legTargets(const npc_gait::Binding& binding,const float* rows,const M& entity,const M* palette){
    std::array<Q,npc_gait::maxTags> targets{};
    for(int i=0;i<binding.count;++i){
        const int id=binding.ids[i],k=binding.tracks[i],parent=binding.parents[i];
        if(k<0||parent<0||!((id>=41&&id<=44)||(id>=51&&id<=54)))continue;
        auto q=[&](int j){const float* v=rows+j*7+3;return Q{v[0],v[1],v[2],v[3]};};
        targets[k]=(id==41||id==51)?mul(conjugate(rotation(palette[parent])),mul(rotation(entity),q(i)))
                                    :mul(conjugate(q(parent)),q(i));
    }
    return targets;
}
inline bool legs(const npc_gait::Binding& binding,const float* rows,const M& entity,M* palette,float weight){
    const auto targets=legTargets(binding,rows,entity,palette);
    return npc_gait::apply(binding,targets.data(),weight,true,palette);
}
template<class Sample>
inline bool blendedLegs(const Blend& blend,const npc_gait::Binding& binding,const M& entity,M* palette,
                        float weight,Sample read){
    Rotations target{};float total=0;
    for(int i=0;i<blend.count;i++){
        const auto& layer=blend.layers[i];
        const float* rows=read(layer.key,blend.phase);
        if(!rows)return false;
        const auto sample=legTargets(binding,rows,entity,palette);
        if(!i)target=sample;
        else for(size_t k=0;k<target.size();k++)target[k]=mix(target[k],sample[k],layer.weight/(total+layer.weight));
        total+=layer.weight;
    }
    return total>0&&npc_gait::apply(binding,target.data(),weight,true,palette);
}
}
