#include "ArmedStep.hpp"
#include "NpcRecoverySpace.hpp"
#include <cassert>
#include <cmath>
#include <string>

int main() {
    armed_step::Blend legs;
    for (int i=0;i<60;i++) legs.select(.4f,.8f,1.f/60);
    float total=0;
    bool ahead=false,oblique=false;
    for (int i=0;i<legs.count;i++) {
        const auto& layer=legs.layers[i];
        total+=layer.weight;
        const std::string key=layer.key;
        if (key=="move_combat_strafe/walk") ahead=true;
        if (key=="move_combat_strafe/walk_strafe_r45") oblique=true;
    }
    assert(ahead && oblique && std::abs(total-1)<.0001f);
    const auto last=legs.phase;
    legs.tick(1.f/60,1.f,total);
    assert(legs.phase>last);
    V origin{0,0,0},result;
    assert(npc_recovery::nearby(origin,result,[](V v){return v.x>.24f;}));
    assert(result.x==.30f && result.z==0.f);
    assert(!npc_recovery::nearby(origin,result,[](V){return false;}));
    const V half{1,2,.65f};
    assert(npc_recovery::vehicleApproaches({1.6f,0,0},{-5,0,0},half));
    assert(!npc_recovery::vehicleApproaches({1.6f,0,0},{5,0,0},half));
    assert(npc_recovery::shotDelay(true,1499,3)==3 && npc_recovery::shotDelay(true,1500,3)==1.5f);
}
