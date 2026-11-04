#include "FootSupportMath.hpp"
#include <cassert>
#include <iostream>
int main(){
    // The reported upright calf: its upward cap is geometrically hittable,
    // but a normal Balance owner must not become a floor for CJ.
    FootSurface calf;assert(footCapsule({0,0,.8f},1.4f,{0,0,.05f},{0,0,.38f},.12f,calf));
    assert(calf.hit&&calf.z>.45f);
    for(bool native:{false,true})for(bool recovering:{false,true})
        assert(!physicalFootSupportEligible(false,true,native,recovering));
    assert(physicalFootSupportEligible(true,false,false,false));
    assert(!physicalFootSupportEligible(true,false,true,false));
    assert(!physicalFootSupportEligible(true,false,false,true));
    assert(physicalFootSupportEligible(false,false,false,false)); // lying Relax need not set fallen
    FootSurface prone;assert(footCapsule({0,0,.8f},1.4f,{-.3f,0,.08f},{.3f,0,.08f},.12f,prone));
    // An eligible corpse retains independent support, with no instant 40cm jump.
    for(float dt:{1.f/30,1.f/60,1.f/120,1.f/144}){
        LegSupportState s;std::array<FootSurface,2> f{prone,FootSurface{true,0}};
        for(int n=0;n<180;n++){
            const float before=s.lift[0];stepLegSupport(s,f,0,1,1.7f,dt);
            assert(std::abs(s.lift[0]-before)<=2*dt+1.e-6f);
            assert(std::abs(s.lift[1])<1.e-6f);
        }
        assert(s.lift[0]>.19f&&s.lift[0]<.21f);
        f[0]={true,0};
        for(int n=0;n<180;n++)stepLegSupport(s,f,0,1,1.7f,dt);
        assert(std::abs(s.lift[0])<1.e-5f);
    }
    std::cout<<"FootSupport: upright capsule exclusion, prone support, independent feet and 4-rate continuity PASS\n";
}
