#include "ArmedStep.hpp"
#include <cassert>
#include <cstdio>

static bool close(float a,float b){return std::abs(a-b)<1e-5f;}

int main(){
    using namespace armed_step;
    assert(usesArmedStride(41,true)&&usesArmedStride(42,true));
    assert(!usesArmedStride(41,false)&&!usesArmedStride(43,true));
    assert(usesArmedStride(22,true)&&usesArmedStride(30,false));

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

    assert(close(playerStrideCap(41,1.f,true),1.f));
    assert(close(playerStrideCap(22,.50f,false),1.f));
    assert(close(playerStrideCap(30,1.f,true),2.f));
    // Full forward and back pressure use the matching walk/jog clips.
    for(float sign:{1.f,-1.f}){
        Blend blend;
        blend.select(0,sign*playerStrideCap(30,1.f,false),1.f);
        assert(blend.count==2&&close(blend.layers[0].weight,.55f)&&close(blend.layers[1].weight,.45f));
        assert(std::string(blend.layers[0].key)==clip(false,sign>0?0:4));
        assert(std::string(blend.layers[1].key)==clip(true,sign>0?0:4));
    }
    puts("PASS: spray tool ownership, camera-relative directions and CJ aim gait blend");
}
