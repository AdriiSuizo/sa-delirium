#pragma once
#include <cstdint>
#include <map>
#include <cmath>
#include <utility>
// Health/event delivery only. The physical solver already applied the contact.
// No velocity, position or force API is present in this transport contract.
// Native backend must use ctor4AD830, AffectsPed4B35A0, calculator4B5AC0,
// EventGroup::Add4AB420 and destructor4AD960, and retain source references.
namespace contact_damage_transport {
struct Key {
    std::uint64_t victim=0,source=0,epoch=0,serial=0;
    auto operator<=>(const Key&)const=default;
};
struct Request {
    Key key{};
    float nativeBaseAmount=0;
    int weapon=50,piece=3;
    unsigned time=0;
    unsigned char direction=0;
};
struct Response {float health=0,armour=0;bool zero=false,force=false,calculated=false,check=false;};
enum class Status { Invalid,Busy,Protected,CalculatedEventRejected,Delivered,IdentityLostAfterCalculation };
struct Result {Status status=Status::Invalid;Response response{};bool duplicate=false;};
// The host's acceptedDamage hook must inspect this scope and avoid scheduling
// a second, amount-derived physical reaction. Native health and crime remain.
inline thread_local unsigned depth=0;
inline bool delivering(){return depth!=0;}
struct Scope {Scope(){++depth;}~Scope(){--depth;}};
struct Entry {Key key{};bool running=true;Result result{};};
class Ledger {
    // One monotonically numbered consumed contact record per live victim.
    // An old receipt cannot be replayed after a newer one or accumulate memory.
    std::map<std::uint64_t,Entry> records;
public:
    void clear(){records.clear();}
    std::size_t trackedVictims()const{return records.size();}
    // Entity lifetime/generation cleanup, not an arbitrary retry timeout.
    void forget(std::uint64_t entity){
        for(auto i=records.begin();i!=records.end();)if(!i->second.running&&(i->second.key.victim==entity||i->second.key.source==entity))i=records.erase(i);else ++i;
    }
    template<class Backend>Result deliver(const Request&r,Backend&b){
        if(!r.key.victim||!r.key.serial||!std::isfinite(r.nativeBaseAmount)||r.nativeBaseAmount<0||
           (r.weapon!=49&&r.weapon!=50&&r.weapon!=54)||r.piece<3||r.piece>9)return {};
        if(auto i=records.find(r.key.victim);i!=records.end()){
            if(i->second.key==r.key){auto out=i->second.running?Result{Status::Busy}:i->second.result;out.duplicate=true;return out;}
            if(i->second.running)return {Status::Busy};
            if(r.key.serial<=i->second.key.serial||r.key.epoch<i->second.key.epoch)return {Status::Invalid,{},true};
        }
        if(!b.fresh(r))return {};
        auto [it,inserted]=records.insert_or_assign(r.key.victim,Entry{r.key});
        auto finish=[&](Result out){it->second.running=false;it->second.result=out;return out;};
        Scope scope;
        typename Backend::Event event{};
        b.construct(event,r);
        struct Destroy {Backend&b;typename Backend::Event&e;~Destroy(){b.destroy(e);}} destroy{b,event};
        if(!b.fresh(r))return finish({Status::Invalid});
        if(!b.affects(event,r))return finish({Status::Protected});
        if(!b.fresh(r))return finish({Status::Invalid});
        Response response=b.calculate(event,r);
        if(!response.calculated)return finish({Status::Invalid,response});
        // A callback may invalidate an entity. Health has already been committed;
        // never calculate again on retry, even if queueing was not possible.
        if(!b.fresh(r))return finish({Status::IdentityLostAfterCalculation,response});
        const bool accepted=b.enqueue(event,r,response);
        return finish({accepted?Status::Delivered:Status::CalculatedEventRejected,response});
    }
};
}
