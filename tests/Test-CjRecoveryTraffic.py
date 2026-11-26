from pathlib import Path
import sys
p=Path(sys.argv[2]).resolve();runtime=Path(sys.argv[1]).resolve()
src=(runtime/'Adapter.cpp').read_text()
gate=src[src.index('static bool recoveryVehicleThreat('):src.index('static std::vector<int> stepPhysicsActors',src.index('static bool recoveryVehicleThreat('))]
fixture=r"""
#include "CjRecoveryTraffic.hpp"
#include "NpcRecoverySpace.hpp"
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
struct Entity {V velocity{},com{},root{};M mat{};int type=0;};
Entity cj, npc, car;
template<class T>T& at(void*p,int offset){
    auto&e=*static_cast<Entity*>(p);
    if constexpr(std::is_same_v<T,int>){assert(offset==0x594);return e.type;}
    else{assert(offset==0x44||offset==0xA4);return offset==0x44?e.velocity:e.com;}
}
void* player(){return &cj;}
V position(void*p){return static_cast<Entity*>(p)->root;}
M* matrix(void*p){return &static_cast<Entity*>(p)->mat;}
unsigned now=100;unsigned gameTime(){return now;}
struct Rig{std::array<float,21> partHalf{},partRadius{};} rig;
struct Active{std::vector<T> lastRendered{{}};unsigned lastRenderTime=100;std::array<T,21> renderedParts{};std::array<V,21> renderVelocity{},renderSpin{};Rig*rig;int ref=1,model=0;};
struct Shape{std::vector<std::array<float,10>> primitives;};
struct Vehicle{V velocity{},spin{},half{1,2,.7f};int ref=12,component=0;void*entity=&car;T frame{};Shape*shape=nullptr;bool velocityAtFrame=true;};
struct World{std::vector<Vehicle> vehicles;} world;auto*collision=&world;
bool recoveryThreatTest=false,playerVehicleImpacts=true;
int receipts=0;void event(const char* t,int,int,const std::string&){assert(std::string(t)=="player_getup_vehicle_hit");++receipts;}
"""
tests=r"""
int main(){
    Active a;a.rig=&rig;rig.partRadius[0]=.18f;rig.partHalf[0]=.14f;
    Shape shape{{{1,0,0,0,1,2,.7f,0,0,0}}};
    Vehicle vehicle;vehicle.velocity={0,12,0};vehicle.shape=&shape;
    world.vehicles.push_back(vehicle);auto&v=world.vehicles.front();
    // A stream of 13 cars: sufficient lateral clearance even at closest approach.
    // The previous isotropic margin interrupts recovery during each pass.
    int oldTrips=0;
    for(int pass=0;pass<13;++pass){float speed=6.3f+pass*.52f;v.velocity={0,speed,0};bool old=false;
        for(int i=-40;i<=40;i++){const float y=i*speed/60.f;
            v.frame.p={1.48f,y,0};const float reach=.35f+speed*.12f;
            old|=1.48f<1+reach&&std::abs(y)<2+reach;
            assert(!recoveryVehicleThreat(a,&cj));}
        oldTrips+=old;
    }
    assert(oldTrips==13&&receipts==0);
    cj.velocity={0,.15f,0};world.vehicles.clear();assert(!recoveryVehicleThreat(a,&cj));
    world.vehicles.push_back(vehicle);
    auto&carShape=world.vehicles.front();
    carShape.frame.p={0,-2.17f,0};carShape.velocity={0,10,0};assert(recoveryVehicleThreat(a,&cj));
    carShape.velocity={0,-10,0};assert(!recoveryVehicleThreat(a,&cj));
    carShape.velocity={12,0,0};assert(!recoveryVehicleThreat(a,&cj));
    carShape.velocity={};assert(!recoveryVehicleThreat(a,&cj)); // residual CJ velocity
    carShape.frame.p={-1.1f,0,0};carShape.velocity={8,0,0};assert(recoveryVehicleThreat(a,&cj));
    a.renderVelocity[0]={10,0,0};assert(!recoveryVehicleThreat(a,&cj));a.renderVelocity[0]={};
    shape.primitives={{{2,0,0,0,1,0,0,0,0,0}}};assert(recoveryVehicleThreat(a,&cj));
    shape.primitives={{{0,1,-2,-.7f,1,2,-.7f,1,0,.7f}}};assert(recoveryVehicleThreat(a,&cj));
    carShape.frame.p={-1.1f,0,-2};assert(!recoveryVehicleThreat(a,&cj));
    carShape.frame.p={-1.1f,0,0};shape.primitives.clear();assert(!recoveryVehicleThreat(a,&cj));
    shape.primitives={{{1,0,0,0,.4f,1,.35f,0,0,0}}};assert(!recoveryVehicleThreat(a,&cj)); // empty space inside bounds
    shape.primitives={{{1,0,0,0,1,2,.7f,0,0,0}}};
    car.type=4;assert(!recoveryVehicleThreat(a,&cj));car.type=0;
    playerVehicleImpacts=false;assert(!recoveryVehicleThreat(a,&cj));playerVehicleImpacts=true;
    now=500;assert(!recoveryVehicleThreat(a,&cj));a.lastRenderTime=500;
    assert(recoveryVehicleThreat(a,&cj));a.lastRendered.clear();assert(!recoveryVehicleThreat(a,&cj));
    carShape.frame.p={-1.8f,0,0};assert(recoveryVehicleThreat(a,&npc)); // existing NPC prediction
    std::cout<<"PASS: 13 parallel passes (1053 frames), real front/side, moving away, tangential, residual speed, stationary, relative speed, vertical clearance, COL primitives, empty geometry, player settings, pose freshness, NPC prediction.\n";
}
"""
(p/'TestTraffic.cpp').write_text(fixture+gate+tests)
