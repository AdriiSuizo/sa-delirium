#pragma once
#include "OriginalContactHealth.hpp"
#include <cstdint>
#include <cmath>
// Host adaptation, NOT a TU8 rule. Original damage always operates in original
// units. A separate, explicitly calibrated mirror preserves SA script units.
// No production admission until every active writer/task boundary is mapped.
namespace original_health_owner {
using Health=original_contact_health::Health;
struct Identity {
    std::uint64_t poolGeneration=0,birth=0;std::uintptr_t ped=0;
    bool operator==(const Identity&)const=default;
};
struct Calibration {
    float nativeHealth=0,originalHealth=0,nativeArmour=0,originalArmour=0;
    // Host supplies source evidence for both baseline caps. A current max alone
    // does not identify the baseline or explain a script/progression mutation.
    std::uint64_t nativeBaselineEvidence=0,originalBaselineEvidence=0;
    bool valid()const{return nativeBaselineEvidence&&originalBaselineEvidence&&
        std::isfinite(nativeHealth)&&nativeHealth>0&&std::isfinite(originalHealth)&&originalHealth>=100&&
        std::isfinite(nativeArmour)&&nativeArmour>0&&std::isfinite(originalArmour)&&originalArmour>0;}
};
struct NativeValues {float health=0,maxHealth=0,armour=0,maxArmour=0;bool operator==(const NativeValues&)const=default;};
enum class Life { Standing,Injured,Killed };
enum class DeathTask { None,Dying,Dead };
enum class Writer { ScriptSet,Refill,ProgressionPreservePoints,ProgressionPreserveFraction,Respawn,UnmappedDamage,Unknown };
enum class Status { Invalid,WrongIdentity,Stale,UnsupportedWriter,Ready };
class Owner {
    Identity id{};Calibration scale{};Health hp{};float maxHealth=0,maxArmour=0;
    NativeValues written{};Life life=Life::Standing;DeathTask death=DeathTask::None;
    std::uint64_t revision=0,writerSerial=0;bool admitted=false,blocked=false;
    float hScale()const{return scale.originalHealth/scale.nativeHealth;}
    float aScale()const{return scale.originalArmour/scale.nativeArmour;}
    static bool validNative(const NativeValues&v){return std::isfinite(v.health)&&v.health>=0&&
        std::isfinite(v.maxHealth)&&v.maxHealth>0&&std::isfinite(v.armour)&&v.armour>=0&&
        std::isfinite(v.maxArmour)&&v.maxArmour>0;}
    Life classify()const{return hp.health<=0?Life::Killed:hp.health<100?Life::Injured:Life::Standing;}
public:
    Status admit(Identity i,Calibration c,NativeValues v){
        if(!i.poolGeneration||!i.birth||!i.ped||!c.valid()||!validNative(v))return Status::Invalid;
        id=i;scale=c;hp={v.health*hScale(),v.armour*aScale()};maxHealth=v.maxHealth*hScale();maxArmour=v.maxArmour*aScale();
        written=v;life=classify();death=DeathTask::None;admitted=true;blocked=false;revision=1;writerSerial=0;return Status::Ready;
    }
    bool ready(Identity i)const{return admitted&&!blocked&&id==i;}
    Health health()const{return hp;}
    Life state()const{return life;}
    DeathTask task()const{return death;}
    std::uint64_t version()const{return revision;}
    NativeValues mirror()const{return {hp.health/hScale(),maxHealth/hScale(),hp.armour/aScale(),maxArmour/aScale()};}
    // Unknown or unmapped damage writers invalidate admission. Re-reading a
    // changed float cannot tell damage from healing and must not resurrect a ped.
    Status externalWrite(Identity i,std::uint64_t serial,Writer why,NativeValues v){
        if(!admitted||id!=i)return Status::WrongIdentity;
        if(!serial||serial<=writerSerial)return Status::Stale;
        if(!validNative(v))return Status::Invalid;
        if(why==Writer::Unknown||why==Writer::UnmappedDamage){blocked=true;return Status::UnsupportedWriter;}
        if(blocked&&why!=Writer::Respawn)return Status::UnsupportedWriter;
        const float newMax=v.maxHealth*hScale(),newArmMax=v.maxArmour*aScale();
        auto next=hp;
        if(why==Writer::ProgressionPreserveFraction){next.health*=newMax/maxHealth;next.armour*=newArmMax/maxArmour;}
        else if(why==Writer::ProgressionPreservePoints){/* cap changes do not heal */}
        else if(why==Writer::Refill){next={newMax,newArmMax};}
        else next={v.health*hScale(),v.armour*aScale()};
        if(why!=Writer::Respawn&&death!=DeathTask::None&&next.health>=100){blocked=true;return Status::UnsupportedWriter;}
        hp=next;maxHealth=newMax;maxArmour=newArmMax;life=classify();
        if(why==Writer::Respawn){death=DeathTask::None;blocked=false;}
        writerSerial=serial;++revision;written=mirror();return Status::Ready;
    }
    // For the frame boundary: equality is only an acknowledgement of our last
    // write, never proof of an unidentified writer's intent.
    bool mirrorUnchanged(Identity i,const NativeValues&v){
        if(!ready(i))return false;
        if(v==written)return true;
        blocked=true;return false;
    }
    // Commit follows a successfully reserved event/task transaction. The
    // optimistic revision prevents two contacts committing the same snapshot.
    bool commit(Identity i,std::uint64_t expected,Health next,const original_contact_health::Outcome&o){
        if(!ready(i)||expected!=revision||!(o.flags&0x80)||!std::isfinite(next.health)||!std::isfinite(next.armour)||next.armour<0)return false;
        hp=next;life=classify();if(o.dies())death=DeathTask::Dying;
        ++revision;written=mirror();return true;
    }
    bool completeDeath(Identity i){if(!ready(i)||death!=DeathTask::Dying)return false;death=DeathTask::Dead;++revision;return true;}
};
}
