#include "GripFrameMath.hpp"
#include <cassert>
#include <iostream>

static bool near(V a,V b){return length(a-b)<2.e-5f;}
static Q turn(V axis,float radians){const float s=std::sin(radians*.5f);return {axis.x*s,axis.y*s,axis.z*s,std::cos(radians*.5f)};}

int main(){
    // Physical component axes differ from both skeleton axes and wrist origin.
    // A measured palm point must remain the identical material point through
    // the binding, including under a rotated moving door/world frame.
    const T body{turn({0,0,1},.7f),{2,3,4}};
    const T binding{turn({1,0,0},-.9f),{.14f,-.04f,.08f}};
    const T skin=mul(body,binding);
    const V palm=mul(body,T{{},{.06f,.02f,-.03f}}).p;
    assert(near(gripPointOnSkin(palm,body,binding,skin),palm));
    assert(length(palm-skin.p)>.05f); // Wrist origin is not the grip.
    const T world{turn(normalized({1,2,3}),1.3f),{17,-8,2}};
    const V moved=mul(world,T{{},palm}).p;
    assert(near(gripPointOnSkin(moved,mul(world,body),binding,mul(world,skin)),moved));

    // A length-preserving FK chain can place its rendered hand away from the
    // solved physical hand. The metric must expose that exact error instead
    // of reporting the solver's zero target error as visual success.
    const V fkError{-.03f,.12f,-.08f};
    T projected=skin;projected.p=projected.p+fkError;
    assert(near(gripPointOnSkin(palm,body,binding,projected)-palm,fkError));

    // Render interpolation is a separate observation from current solver FK.
    const T priorBody{body.q,body.p+V{-.075f,0,0}};
    const T priorSkin=mul(priorBody,binding);
    assert(near(gripPointOnSkin(palm,body,binding,priorSkin)-palm,{-.075f,0,0}));
    std::cout<<"grip material-point frame tests passed\n";
}
