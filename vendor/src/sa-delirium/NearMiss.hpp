#pragma once
#include <map>
#include <utility>
#include <vector>
// A bullet passing close. CBirds::HandleGunShot (0x712E40) receives the start and end of every
// instant-hit line, pellets and misses included (CWeapon::ProcessLineOfSight 0x73B300). Peds that
// are still animation keep SA's own whizzed-by event; a body on its feet looks and flinches, the
// reaction Delirium 0.7 builds from IV's upperBodyFlinch and headLook (1.5 m, once per 0.5 s).
namespace nearmiss {
struct Line {V from,to;};
inline std::vector<Line> lines;
inline void(__cdecl* originalGunShot)(const V*,const V*)=nullptr;
inline void __cdecl gunShot(const V* from,const V* to){
    if(from&&to){
        firearm::observeShotLine(*from,*to,reinterpret_cast<uintptr_t>(_ReturnAddress()));
        if(lines.size()<32)lines.push_back({*from,*to});
    }
    originalGunShot(from,to);
}
constexpr float whizz=1.5f,hitItself=.2f,clearOfShooter=.5f;
constexpr unsigned burstMs=500;
}
static void reactToNearMisses(const std::vector<Active*>& live,unsigned now){
    const auto lines=std::move(nearmiss::lines);nearmiss::lines.clear();
    for(const auto&line:lines){
        const V path=line.to-line.from;const float reach=length(path);if(reach<1.f)continue;
        const V dir=path*(1.f/reach);
        for(auto*a:live){
            if(!a->agent||a->mounted||a->preview||a->ped==player()||a->current.empty())continue;
            if(at<float>(a->ped,0x540)<=0||nm_agent_is_balancing(a->agent)<=0||now-a->nearMissAt<nearmiss::burstMs)continue;
            // A body already reacting to a hit is not frightened by the same burst: a flinch restarted its reaction (07/11).
            if(a->kind==Reaction::Shot||a->kind==Reaction::Melee||a->kind==Reaction::Bump||now-a->lastShotAt<1500)continue;
            const V chest=bodyPoint(*a);const float along=dot(chest-line.from,dir);
            if(along<nearmiss::clearOfShooter||along>reach)continue;
            const V nearest=line.from+dir*along;const float gap=length(chest-nearest);
            if(gap>nearmiss::whizz||gap<nearmiss::hitItself)continue;
            a->nearMissAt=now;
            ok(nm_msg_begin(a->agent,"upperBodyFlinch"));ok(nm_msg_set_bool(a->agent,"start",true));
            ok(nm_msg_set_vec3(a->agent,"pos",&nearest.x));ok(nm_msg_set_bool(a->agent,"newHit",true));sendMessage(a->agent);
            ok(nm_msg_begin(a->agent,"headLook"));ok(nm_msg_set_bool(a->agent,"start",true));
            ok(nm_msg_set_vec3(a->agent,"pos",&nearest.x));ok(nm_msg_set_bool(a->agent,"alwaysLook",true));sendMessage(a->agent);
            event("bullet_near_miss",a->ref,a->model,"gap_m="+std::to_string(gap));
        }
    }
}
