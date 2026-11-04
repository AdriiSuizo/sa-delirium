#pragma once
#include <cstdint>

// Native contact-damage event identity, not a time-window heuristic. This is
// only the ownership bridge; health, crime and native Die/Death tasks remain.
namespace physical_death_continuity {
struct Identity {
    int ref=-1,model=-1;
    std::uintptr_t ped=0,clump=0,agent=0;
    unsigned admittedFrame=0;
    bool operator==(const Identity&)const=default;
};
struct Damage {
    unsigned time=0;
    int vehicle=-1,weapon=-1,piece=0;
    bool operator==(const Damage&)const=default;
};
struct Facts {
    Identity owner{};
    Damage accepted{},delivered{};
    bool enabled=false,protectedContext=false,inVehicle=false;
    bool preview=false,mounted=false,impact=false,healthZero=false;
    bool calculated=false,killed=false,physicalOwner=false,ownCarrier=false,matchedShot=false;
};
inline bool acceptedPhysicalDeath(const Facts&f){
    return f.enabled&&!f.protectedContext&&!f.inVehicle&&!f.preview&&!f.mounted&&
        f.healthZero&&f.calculated&&f.killed&&f.physicalOwner&&
        f.owner.ref>=0&&f.owner.model>=0&&f.owner.ped&&f.owner.clump&&f.owner.agent&&
        (f.matchedShot||(f.impact&&f.accepted.vehicle>=0&&(f.accepted.weapon==49||f.accepted.weapon==50)&&
        f.accepted==f.delivered));
}
inline bool acceptedImpactDeath(const Facts&f){return f.impact&&acceptedPhysicalDeath(f);}
struct Record {
    Identity owner{};
    Damage damage{};
    std::uintptr_t event=0,complexTask=0;
    bool consumed=false,bookkeeping=false;
    bool begin(const Facts&f,std::uintptr_t nativeEvent){
        if(!nativeEvent||!acceptedPhysicalDeath(f))return false;
        if(owner==f.owner&&damage==f.delivered&&complexTask)return false;
        *this={f.owner,f.delivered,nativeEvent};return true;
    }
    bool selected(const Identity&id,std::uintptr_t nativeEvent)const{
        return owner==id&&event==nativeEvent&&event!=0;
    }
    bool taskCreated(const Identity&id,std::uintptr_t task,bool nativeDie){
        if(owner!=id||!nativeDie||!task)return false;
        complexTask=task;return true;
    }
    bool initializeTask(const Identity&id,std::uintptr_t task,bool nativeSimpleDie){
        if(owner!=id||!complexTask||complexTask!=task||bookkeeping||!nativeSimpleDie)return false;
        bookkeeping=true;return true;
    }
    bool takeOver(const Identity&id,bool healthZero){
        if(owner!=id||!complexTask||consumed||!healthZero)return false;
        consumed=true;return true;
    }
};
}
