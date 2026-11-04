#include "SpineRetargetMath.hpp"
#include <cassert>
#include <iostream>
static Q turn(V a,float angle){const float s=std::sin(angle*.5f);return {a.x*s,a.y*s,a.z*s,std::cos(angle*.5f)};}
int main(){
    constexpr float lo=.479f,hi=.325f;
    const T spine{{},{0,0,.0015f}},chest{{},{0,0,.1915f}},neck{{},{0,0,.4815f}};
    const auto rest=nativeTorsoJoints(spine,chest,neck,lo,hi);
    int cases=0;
    for(V axis:{V{1,0,0},V{0,1,0},V{0,0,1}})for(float a:{-.7f,-.25f,0.f,.4f,.8f})for(float b:{-.6f,0.f,.5f})for(float yaw:{0.f,1.4f,3.1f}){
        const T root{turn({0,0,1},yaw),{12,-3,4}};
        const T s=mul(root,T{turn(axis,a),spine.p});
        const T c=mul(s,T{turn(axis,b),chest.p-spine.p});
        const T n=mul(c,T{{},neck.p-chest.p});
        const auto posed=nativeTorsoJoints(s,c,n,lo,hi);
        assert(length(posed[0].p-s.p)<1.e-6f&&length(posed[2].p-c.p)<1.e-6f);
        for(int i=1;i<4;i++){
            const V local=rotate(conjugate(rest[i-1].q),rest[i].p-rest[i-1].p);
            assert(length(posed[i-1].p+rotate(posed[i-1].q,local)-posed[i].p)<3.e-6f);
            assert(length(posed[i].p-posed[i-1].p)>.01f);
        }
        // The actual SA bones retain their own articulation and rigid length.
        // Virtual subdivision does not alter the skin's local segment lengths.
        assert(std::abs(length(c.p-s.p)-.19f)<3.e-6f);
        assert(std::abs(length(n.p-c.p)-.29f)<3.e-6f);
        cases++;
    }
    std::cout<<"native torso seed: "<<cases<<" compound bends; preserved real joints, closed virtual anchors and native FK lengths\n";
}
