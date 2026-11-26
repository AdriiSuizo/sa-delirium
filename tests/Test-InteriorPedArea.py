from pathlib import Path
import json,re,subprocess,sys
here=Path(sys.argv[2]).resolve();runtime=Path(sys.argv[1]).resolve()
source=(runtime/'Adapter.cpp').read_text()
hits=[(7186, 124), (6670, 126), (6928, 127), (7442, 125), (7442, 125), (7442, 125), (6670, 126), (6670, 126), (6670, 126), (6670, 126), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (7695, 127), (8720, 125), (7954, 125), (8206, 124), (8206, 124), (8462, 126), (8462, 126), (8462, 126), (8462, 126), (8462, 126), (9234, 124), (11275, 127), (11275, 127), (11275, 127), (11275, 127), (11275, 127), (11275, 127), (11275, 127), (9486, 125), (9486, 125), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126), (9742, 126)]
s=r"""
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include "InteriorPedArea.hpp"
struct alignas(16) Ped {char bytes[0x800]{};int ref{};bool hasMatrix=true;bool mounted=false;float x=2;};
static Ped cj, npc;static int room=1,lastArea=1; static float radius=40;static bool allowScripted=true;
static void* player(){return &cj;}
static int area(){return room;}
static int reference(void*p){return static_cast<Ped*>(p)->ref;}
template<class T> T& at(void*p,int offset){return *reinterpret_cast<T*>(static_cast<Ped*>(p)->bytes+offset);}
static bool matrix(void*p){return static_cast<Ped*>(p)->hasMatrix;}
static float position(void*p){return static_cast<Ped*>(p)->x;}
static float length(float x){return std::abs(x);}
static bool inVehicle(void*p){return static_cast<Ped*>(p)->mounted;}
static bool ridingBike(void*){return false;}
"""
s+=source[source.index('static interior_peds::Membership'):source.index('#include "NativePaletteEpoch.hpp"')]
s+=source[source.index('static const char* pedRejection'):source.index('enum class Reaction')]
s+='\nint hits[][2]={'+','.join('{%d,%d}'%x for x in hits)+'};\n'
s+=r"""
int main(){
    at<void*>(&npc,0x18)=&npc; at<float>(&npc,0x540)=100;
    at<unsigned char>(&npc,0x484)=2;
    auto& tag=at<unsigned char>(&npc,0x2F);
    for(auto& h:hits){
        npc.ref=h[0];at<short>(&npc,0x22)=static_cast<short>(h[1]);
        interiorPeds.clear();tag=0;assert(!validPed(&npc));observeInteriorPed(&npc);assert(validPed(&npc));
        tag=13;interiorPeds.clear();assert(validPed(&npc));tag=1;assert(validPed(&npc));
        tag=2;observeInteriorPed(&npc);assert(!validPed(&npc));
    }
    tag=0;observeInteriorPed(&npc);assert(validPed(&npc));
    ++npc.ref;assert(!validPed(&npc));--npc.ref;
    ++at<short>(&npc,0x22);assert(!validPed(&npc));--at<short>(&npc,0x22);
    at<void*>(&npc,0x18)=&cj;assert(!validPed(&npc));at<void*>(&npc,0x18)=&npc;
    room=2;assert(!validPed(&npc));observeInteriorPed(&npc);assert(!validPed(&npc));
    lastArea=room;observeInteriorPed(&npc);assert(validPed(&npc));interiorPeds.clear();assert(!validPed(&npc));
    room=lastArea=0;assert(validPed(&npc));tag=1;assert(!validPed(&npc));tag=13;assert(validPed(&npc));
    room=lastArea=1;tag=1;npc.x=80;assert(!validPed(&npc));assert(validPed(&npc,false,false,false,100));npc.x=2;
    at<void*>(&npc,0xFC)=&cj;assert(!validPed(&npc));at<void*>(&npc,0xFC)=nullptr;
    npc.mounted=true;assert(!validPed(&npc));npc.mounted=false;
    allowScripted=false;assert(!validPed(&npc));allowScripted=true;
    at<float>(&npc,0x540)=0;assert(!validPed(&npc));assert(validPed(&npc,true));assert(!validPed(&cj));
    std::cout<<"PASS: 60 recorded hit identities / 13 NPCs, under explicit test area tags. Identity, transitions, exterior, distance, attachment, mounted, scripted, death and player checks passed.\n";
}
"""
(here/'interior_test.cpp').write_text(s)
