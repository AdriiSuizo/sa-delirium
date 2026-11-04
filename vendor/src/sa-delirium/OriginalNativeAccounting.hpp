#pragma once
#include "OriginalHealthRuntime.hpp"
#include <memory>
#include <tuple>
// ABI.hpp precedes this header. Native accounting is downstream of the original
// health/task transaction. It cannot delay, recalculate or apply that response.
// All event objects have stable addresses while native registered refs exist.
namespace original_native_accounting {
using Prepared=original_health_runtime::Prepared;
using Identity=original_health_runtime::Identity;
struct Functions {
    void*(__thiscall*construct)(void*,void*,unsigned,int,int,unsigned char,bool,bool)=nullptr;
    void(__thiscall*destroy)(void*)=nullptr;
    void*(__thiscall*add)(void*,void*,bool)=nullptr;
};
struct Context {
    void*ped=nullptr;void*source=nullptr;void*eventGroup=nullptr;void*user=nullptr;
    unsigned char direction=0;bool inVehicle=false;
    // Called only at reservation, while the solved hit and original source
    // generation are fresh. May not import a native-health delta as damage.
    bool(*freshHit)(const Prepared&,const Context&)=nullptr;
    // Recipient lifetime/group validity only, NOT old health equality. More
    // source hits may already have committed while native queue was full.
    bool(*liveRecipient)(Identity,const Context&)=nullptr;
    // Must register exact clone allocation/lifecycle for original dispatch.
    // It must NOT apply the original task/health a second time. The native event
    // can later expire or be discarded without undoing the original response.
    void(*published)(const Prepared&,void*clone,const Context&)=nullptr;
    // Explicit source/accounting bridge decision. A damage proof denying health
    // is not by itself permission to emit a native crime, nor to skip reaction.
    bool accountingPolicyKnown=false,needsNativeAccounting=false;
};
enum class State { Reserved,Ready,Published,Rejected,LifeEnded };
struct Record {
    Prepared prepared{};Context context{};Functions fn{};DamageEvent event{};
    State state=State::Reserved;bool constructed=false;
    ~Record(){if(constructed)fn.destroy(&event);}
};
struct PumpResult {unsigned published=0,waiting=0,rejected=0,lifeEnded=0;};
class Outbox {
    using Index=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<Index,std::unique_ptr<Record>> records;bool pumping=false;
    static Index index(const Prepared&p){return {p.identity.poolGeneration,p.identity.birth,p.key.serial};}
public:
    std::size_t size()const{return records.size();}
    Record*reserve(const Prepared&p,Functions fn,Context c){
        if(!p.sourceTaskReady||!p.sourcePermissionKnown||!c.eventGroup||!c.accountingPolicyKnown||!c.freshHit||!c.liveRecipient||!c.published||
            !fn.construct||!fn.destroy||!fn.add||c.ped!=reinterpret_cast<void*>(p.identity.ped)||!c.freshHit(p,c)||
            !c.liveRecipient(p.identity,c)||records.contains(index(p)))return nullptr;
        auto r=std::make_unique<Record>();r->prepared=p;r->context=c;r->fn=fn;
        // Storage allocation precedes constructing a registered native ref.
        auto token=r.get();records.emplace(index(p),std::move(r));
        if(c.needsNativeAccounting){
            fn.construct(&token->event,c.source,p.key.time,int(p.key.nativeWeapon),int(p.key.piece),c.direction,false,c.inVehicle);
            token->constructed=true;
            token->event.response={p.nativeBefore.health-p.nativeAfter.health,p.nativeBefore.armour-p.nativeAfter.armour,
                (p.outcome.flags&0x20)!=0,false,true,false};
            token->event.flags&=~2u;
        }
        return token;
    }
    bool cancel(Record*r){
        if(!r||r->state!=State::Reserved)return false;
        auto i=records.find(index(r->prepared));if(i==records.end()||i->second.get()!=r)return false;
        records.erase(i);return true;
    }
    void commit(Record*r){
        // Host reserved this token as part of the source transaction; no native
        // function or allocation occurs on this commit boundary.
        if(r&&r->state==State::Reserved)r->state=State::Ready;
    }
    PumpResult pump(){
        PumpResult out;if(pumping)return out;
        pumping=true;struct Reset{bool&b;~Reset(){b=false;}}reset{pumping};
        // Do not overtake an earlier receipt for the same ped lifetime.
        std::tuple<std::uint64_t,std::uint64_t> blocked{};bool hasBlocked=false;
        for(auto i=records.begin();i!=records.end();){
            auto&r=*i->second;const auto&c=r.context;
            const auto who=std::tuple{r.prepared.identity.poolGeneration,r.prepared.identity.birth};
            if(hasBlocked&&who==blocked){++out.waiting;++i;continue;}
            if(!c.liveRecipient(r.prepared.identity,c)){
                r.state=State::LifeEnded;++out.lifeEnded;i=records.erase(i);continue;
            }
            if(r.state==State::Rejected){++out.rejected;blocked=who;hasBlocked=true;++i;continue;}
            if(r.state!=State::Ready){++out.waiting;blocked=who;hasBlocked=true;++i;continue;}
            if(!c.needsNativeAccounting){i=records.erase(i);continue;}
            if(at<void*>(c.eventGroup,4)!=c.ped||at<unsigned>(c.eventGroup,8)>=16){
                ++out.waiting;blocked=who;hasBlocked=true;++i;continue;
            }
            // Native source may have been deleted. Its registered event.source
            // is cleared by the game; NEVER reconstruct it from stale c.source.
            void*clone=r.fn.add(c.eventGroup,&r.event,false);
            if(!clone){
                // Unlike capacity preflight, a rejection inside Add may already
                // have notified friends. No blind retry of those side effects.
                r.state=State::Rejected;++out.rejected;blocked=who;hasBlocked=true;++i;continue;
            }
            r.state=State::Published;c.published(r.prepared,clone,c);++out.published;
            i=records.erase(i);
        }
        return out;
    }
};
}
