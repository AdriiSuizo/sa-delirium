#pragma once
#include "AnimatedBodyCollider.hpp"
static std::map<std::pair<int,int>,unsigned> walkingContactLog;
static int bodyContactProbe=-1;static unsigned bodyContactProbeUntil=0,bodyContactProbeLogged=0;
static bool resolveRecoveringWalkerContact(Active&,V,V,V&);
static void processWalkingContacts(float dt){
    (void)dt;
    // Replaced by shared external mobility: original animated compound, 25kg,
    // one root velocity, reciprocal rows. The former 21-limb one-way path
    // injected animation energy and would now apply each contact twice.
    for(auto it=animatedBodyColliders.begin();it!=animatedBodyColliders.end();)
        if(pedFromRef(it->first)!=it->second->ped||gameTime()-it->second->used>500)it=animatedBodyColliders.erase(it);else ++it;
}
#include "WheelBodyContacts.hpp"
