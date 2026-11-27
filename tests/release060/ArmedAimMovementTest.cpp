#include "ArmedStep.hpp"
#include <cassert>
#include <cstdio>

static bool close(float a,float b){return std::abs(a-b)<1e-5f;}

int main(){
    using namespace armed_step;
    assert(usesArmedStride(41,true)&&usesArmedStride(42,true));
    assert(!usesArmedStride(41,false)&&!usesArmedStride(43,true));
    for(int weapon=22;weapon<=38;++weapon)assert(!usesArmedStride(weapon,true));
    assert(usesArmedStride(30,false));

    // The can follows mapped inputs in all four directions even after a
    // camera turn. Diagonal input retains a one-unit speed ceiling.
    for(V front:{V{0,1,0},V{-1,0,1}}){
        const V flat{front.x,front.y,0},right{front.y,-front.x,0};
        assert(close(dot(cameraRelativeWish(0,1,front,{}),flat),1.f));
        assert(close(dot(cameraRelativeWish(0,-1,front,{}),flat),-1.f));
        assert(close(dot(cameraRelativeWish(1,0,front,{}),right),1.f));
        assert(close(dot(cameraRelativeWish(-1,0,front,{}),right),-1.f));
        assert(close(length(cameraRelativeWish(1,-1,front,{})),1.f));
        assert(close(length(cameraRelativeWish(.06f,0,front,{})),0.f));
    }

    puts("PASS: spray tool ownership, camera-relative directions and native CJ firearm movement");
}
