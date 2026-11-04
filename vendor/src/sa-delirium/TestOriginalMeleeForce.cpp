#include "OriginalMeleeForce.hpp"
#include <iostream>
#include <iomanip>
int main(){float x,y,z,f,t;std::cout<<std::setprecision(10);while(std::cin>>x>>y>>z>>f>>t){V j=originalMeleeImpulse({x,y,z},f);const float left=originalMeleePushSeconds(t);std::cout<<j.x/left<<' '<<j.y/left<<' '<<j.z/left<<' '<<left<<'\n';}}
