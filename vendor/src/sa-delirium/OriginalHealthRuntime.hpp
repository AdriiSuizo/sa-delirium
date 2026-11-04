#pragma once
#include "OriginalHealthOwner.hpp"
#include "OriginalContactProof.hpp"
#include <map>
// Runtime registry/transaction, no hook installation. Source calculations for
// contact, melee and bullets must enter the SAME owner through Prepared.
// A native calculator result is deliberately not an admissible source result.
namespace original_health_runtime {
using Identity=original_health_owner::Identity;
using Owner=original_health_owner::Owner;
using NativeValues=original_health_owner::NativeValues;
using Health=original_contact_health::Health;
using Outcome=original_contact_health::Outcome;
enum class Producer { ContactInitial,ContactSolved,OriginalMelee,OriginalBullet,OriginalExplosion };
struct Key {
    std::uint64_t victim=0,source=0,epoch=0,serial=0;
    unsigned sourceFrame=0,time=0,originalWeapon=0,nativeWeapon=0,piece=0;
    Producer producer=Producer::ContactSolved;
    bool operator==(const Key&)const=default;
};
struct Prepared {
    Identity identity{};Key key{};std::uint64_t revision=0;
    Health before{},after{};Outcome outcome{};
    NativeValues nativeBefore{},nativeAfter{};
    // Source controller selection is separate from the health proof. A denied
    // original health hit can still have a source physical reaction.
    bool sourceTaskReady=false,sourcePermissionKnown=false;
};
enum class Status { Invalid,UnknownSource,Stale,Busy,Prepared,QueueUnavailable,Committed,PublicationViolation };
struct Entry {
    Identity identity{};Owner owner{};bool busy=false,poisoned=false;
    Key last{};bool committed=false;
};
class Registry {
    std::map<std::uint64_t,Entry> entries;
public:
    void clear(){entries.clear();}
    void forget(Identity id){auto i=entries.find(id.poolGeneration);if(i!=entries.end()&&i->second.identity==id&&!i->second.busy)entries.erase(i);}
    Entry*find(Identity id){auto i=entries.find(id.poolGeneration);return i!=entries.end()&&i->second.identity==id?&i->second:nullptr;}
    bool admit(Identity id,original_health_owner::Calibration c,NativeValues v){
        if(auto e=find(id))return !e->busy&&!e->poisoned&&e->owner.ready(id);
        Entry e;e.identity=id;if(e.owner.admit(id,c,v)!=original_health_owner::Status::Ready)return false;
        auto old=entries.find(id.poolGeneration);if(old!=entries.end()&&old->second.busy)return false;
        entries.insert_or_assign(id.poolGeneration,e);return true;
    }
    // This must wrap native script/progression/refill/save/respawn writers.
    // Unmapped damage cannot quietly bypass the original health owner.
    original_health_owner::Status nativeWrite(Identity id,std::uint64_t serial,original_health_owner::Writer why,NativeValues v){
        auto e=find(id);if(!e||e->busy)return original_health_owner::Status::WrongIdentity;
        return e->owner.externalWrite(id,serial,why,v);
    }
    Status contact(Identity id,Key key,original_contact_health::Facts f,const original_contact_health::ProofFacts&proof,bool taskReady,Prepared&out){
        auto e=find(id);if(!e||e->poisoned||!e->owner.ready(id)||key.victim!=id.poolGeneration||!key.serial)return Status::Invalid;
        if(e->busy)return Status::Busy;
        if(key.producer!=Producer::ContactInitial&&key.producer!=Producer::ContactSolved)return Status::UnknownSource;
        const bool checksProof=e->owner.health().health>=100;
        if(key.originalWeapon!=f.weapon||(checksProof&&proof.weapon!=f.weapon))return Status::UnknownSource;
        f.proof=original_contact_health::permitted(proof);
        // Actual824DC670 bypasses proof below100. Missing proof facts must not
        // make an already-injured body immune to subsequent physical damage.
        if(checksProof&&f.proof==original_contact_health::Proof::Unknown)return Status::UnknownSource;
        Prepared p;p.identity=id;p.key=key;p.revision=e->owner.version();p.before=e->owner.health();p.after=p.before;
        auto status=original_contact_health::apply(p.after,f,p.outcome);
        if(status!=original_contact_health::Status::Calculated&&status!=original_contact_health::Status::Protected)return Status::UnknownSource;
        p.nativeBefore=e->owner.mirror();auto copy=e->owner;
        if(!copy.commit(id,p.revision,p.after,p.outcome))return Status::Invalid;
        p.nativeAfter=copy.mirror();p.sourcePermissionKnown=true;p.sourceTaskReady=taskReady;out=p;return Status::Prepared;
    }
    // Original melee/bullet/explosion code calls this after calculating from
    // the exact snapshot/version. It must supply source outcome, never SA loss.
    Status source(Identity id,Key key,std::uint64_t version,Health before,Health after,Outcome outcome,bool taskReady,Prepared&out){
        auto e=find(id);if(!e||e->poisoned||!e->owner.ready(id)||key.victim!=id.poolGeneration||!key.serial)return Status::Invalid;
        if(e->busy)return Status::Busy;
        if(key.producer!=Producer::OriginalMelee&&key.producer!=Producer::OriginalBullet&&key.producer!=Producer::OriginalExplosion)return Status::UnknownSource;
        const auto now=e->owner.health();if(version!=e->owner.version()||before.health!=now.health||before.armour!=now.armour)return Status::Stale;
        auto copy=e->owner;if(!copy.commit(id,version,after,outcome))return Status::Invalid;
        out={id,key,version,before,after,outcome,e->owner.mirror(),copy.mirror(),taskReady,true};return Status::Prepared;
    }
    // Source-order transaction: original health is synchronous with the hit,
    // independent of SA EventGroup capacity/arbitration. reserve() allocates
    // original response and native-accounting storage BEFORE the commit. It
    // must NOT reject merely because either event queue is full. The original
    // queue's own add/replace/reject policy runs in commitOriginalResponse().
    // Native accounting is retained separately and later published once.
    template<class Backend>Status commitOriginal(const Prepared&p,Backend&b){
        auto e=find(p.identity);if(!e||e->poisoned)return Status::Invalid;
        if(e->busy)return Status::Busy;
        if(e->committed&&e->last==p.key)return Status::Committed;
        if(!p.sourceTaskReady||!p.sourcePermissionKnown)return Status::UnknownSource;
        if(p.key.victim!=p.identity.poolGeneration||!p.key.serial||!e->owner.ready(p.identity))return Status::Invalid;
        if(e->committed&&(p.key.serial<=e->last.serial||p.key.epoch<e->last.epoch))return Status::Stale;
        const auto current=e->owner.health();
        if(p.revision!=e->owner.version()||p.before.health!=current.health||p.before.armour!=current.armour||p.nativeBefore!=e->owner.mirror())return Status::Stale;
        auto check=e->owner;
        if(!check.commit(p.identity,p.revision,p.after,p.outcome)||check.mirror()!=p.nativeAfter)return Status::Invalid;
        if(!b.fresh(p))return Status::Invalid;
        e->busy=true;struct Unlock{Entry&e;~Unlock(){e.busy=false;}}unlock{*e};
        auto token=b.reserve(p);if(!token)return Status::QueueUnavailable;
        if(!b.fresh(p)||!e->owner.commit(p.identity,p.revision,p.after,p.outcome)){
            b.cancelReserve(token);return Status::Invalid;
        }
        b.writeMirror(p.nativeAfter);
        // These two methods consume already-reserved storage, cannot fail or
        // throw, and must never recalculate damage, add force or execute SA's
        // incompatible injury/death task. Original queue rejection is a source
        // decision (health remains applied), not a transaction failure.
        b.commitOriginalResponse(p,token);b.commitAccounting(p,token);
        e->last=p.key;e->committed=true;return Status::Committed;
    }
    // Isolated transport proof retained for the native ABI tests. This waits
    // for native publication and is NOT the production source-order path.
    template<class Backend>Status deliver(const Prepared&p,Backend&b){
        auto e=find(p.identity);if(!e||e->poisoned)return Status::Invalid;
        if(e->busy)return Status::Busy;
        if(e->committed&&e->last==p.key)return Status::Committed;
        if(!p.sourceTaskReady||!p.sourcePermissionKnown)return Status::UnknownSource;
        if(p.key.victim!=p.identity.poolGeneration||!p.key.serial||!e->owner.ready(p.identity))return Status::Invalid;
        if(e->committed&&(p.key.serial<=e->last.serial||p.key.epoch<e->last.epoch))return Status::Stale;
        if(p.revision!=e->owner.version())return Status::Stale;
        const auto current=e->owner.health();
        if(p.before.health!=current.health||p.before.armour!=current.armour||p.nativeBefore!=e->owner.mirror())return Status::Stale;
        auto check=e->owner;
        if(!check.commit(p.identity,p.revision,p.after,p.outcome)||check.mirror()!=p.nativeAfter)return Status::Invalid;
        if(!b.fresh(p))return Status::Invalid;
        e->busy=true;struct Unlock{Entry&e;~Unlock(){e.busy=false;}}unlock{*e};
        // Backend preflights native queue capacity BEFORE Add: Add can notify
        // friends/occupants before its own capacity check. It then constructs a
        // calculated response and invokes Add without running SA's calculator.
        auto published=b.publish(p);
        if(!published)return Status::QueueUnavailable;
        if(!b.fresh(p)||p.revision!=e->owner.version()||!e->owner.commit(p.identity,p.revision,p.after,p.outcome)){
            // No retry or pretend success after a partial external publication.
            // Host must remove the exact queued token and surface the invariant
            // violation. Production admission requires proving this impossible.
            b.cancel(published);e->poisoned=true;return Status::PublicationViolation;
        }
        b.writeMirror(p.nativeAfter);b.attachSourceResponse(p,published);
        e->last=p.key;e->committed=true;return Status::Committed;
    }
};
}
