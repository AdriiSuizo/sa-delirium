#pragma once
#include "OriginalEventQueueABI.hpp"
#include "OriginalHealthRuntime.hpp"
#include "OriginalDamageArbitration.hpp"
#include <map>
#include <memory>
#include <vector>
#include <set>
// Executable source-Damage adapter. Contact/melee/bullet/explosion producers all
// submit their Prepared ORIGINAL outcome to this same queue. It never calculates
// SA damage, calls SA AffectsPed or converts an SA event/task id to an IV id.
// Runtime activation still requires a fully mapped Tasks implementation.
namespace original_damage_queue {
using Prepared=original_health_runtime::Prepared;
using Identity=original_health_runtime::Identity;
namespace arbitration=original_damage_arbitration;
struct SourceIdentity {
    std::uint64_t generation=0,birth=0;
    bool operator==(const SourceIdentity&)const=default;
    bool valid()const{return (generation==0)==(birth==0);}
};
struct Current {
    arbitration::Victim victim{};arbitration::Source source{};SourceIdentity sourceIdentity{};
    bool onRagdoll=false,leafKnown=false,leafIsNM=false;unsigned leafKind=0;
};
struct Plan {
    // Prepared from the post-hit ORIGINAL health snapshot, before health commit.
    // The source dispatcher may create a response for protected physical contact
    // independently of event admission. This queue does not erase that response.
    bool known=false,networkRefused=false,groupTrial=false,friendsTrial=false,blocking=false;
    bool sourceTookNM=false,responseIsNM=false,keep=false,replace=false;
    int decisionResponse=200;
    std::uint64_t responseToken=0,addCallEvidence=0;
    Current postHit{};
};
struct Payload {Prepared hit{};Plan plan{};bool committed=false;};
// These are SOURCE operations. For example abort urgency0/1/2 is the IV task
// contract, not an integer forwarded to SA. Scope must also prove higher priority
// script/mission/event ownership is represented; a damage-only side queue must
// not bypass a protected external task or competing source event.
struct Tasks {
    virtual ~Tasks()=default;
    virtual bool alive(Identity)const=0;
    virtual bool prepare(const Prepared&,Plan&)=0;
    virtual bool current(const Payload&,Current&)const=0;
    virtual bool scope(Identity,const std::vector<const Payload*>&)const=0;
    virtual bool construct(const Payload&,unsigned&built)=0;
    virtual bool abort(const Payload&,unsigned task,unsigned urgency,unsigned built,int&age,bool&accepted,bool&dropMain)=0;
    virtual bool give(const Payload&,unsigned built)=0;
    virtual void clearBuilt()=0;
    virtual void release(const Payload&)=0;
    virtual void alert100(Identity)=0;
    virtual void social(const Payload&,const NmOriginalEventAdded&)=0;
};
class Queue {
    Identity id{};Tasks&tasks;NmOriginalEventQueue*q=nullptr;
    std::map<std::uint64_t,std::unique_ptr<Payload>>payloads;
    bool poisoned=false,handling=false,committing=false;
    Payload*get(std::uint64_t receipt){auto i=payloads.find(receipt);return i==payloads.end()?nullptr:i->second.get();}
    static bool currentValid(const Current&c){return c.victim.known&&c.source.known&&c.sourceIdentity.valid()&&
        c.source.exists==(c.sourceIdentity.generation!=0);}
    static arbitration::Hit eventHit(const Payload&p){return {p.hit.outcome.flags,true};}
    NmOriginalEventInput input(const Payload&p)const{
        const auto&f=p.plan;const auto c=arbitration::concerns(eventHit(p),f.postHit.victim,f.postHit.source);
        const unsigned admission=1|4|32|(f.networkRefused?2:0)|(f.groupTrial?8:0)|(f.friendsTrial?16:0)|
            ((p.hit.outcome.flags&0x80)?64:0)|(f.blocking?128:0)|(c.yes?256:0);
        return {sizeof(NmOriginalEventInput),9,p.hit.key.serial,f.postHit.sourceIdentity.generation,f.postHit.sourceIdentity.birth,
            f.sourceTookNM?99:88,0,2u|(f.keep?4u:0u)|(f.replace?8u:0u),admission,f.decisionResponse};
    }
    static int callback(void*ctx,const NmOriginalEventRequest*r,NmOriginalEventReply*out){return static_cast<Queue*>(ctx)->call(*r,*out);}
    int call(const NmOriginalEventRequest&r,NmOriginalEventReply&out){
        if(r.generation!=id.poolGeneration||r.birth!=id.birth||!tasks.alive(id))return OriginalEventIdentity;
        out.age=r.age;out.answer=1;
        if(r.operation==OriginalEventPreflight){
            std::vector<const Payload*>live;for(auto&[serial,p]:payloads)if(p->committed)live.push_back(p.get());
            out.answer=tasks.scope(id,live);return OriginalEventOK;
        }
        if(r.operation==OriginalEventClear){tasks.clearBuilt();return OriginalEventOK;}
        auto*p=get(r.receipt);if(!p||!p->committed||p->hit.identity!=id||r.kind!=9)return OriginalEventUnmapped;
        if(r.operation==OriginalEventReady)return OriginalEventOK; // Damage vf+14 is true.
        if(r.operation==OriginalEventValid){out.answer=(r.flags&4)||r.age<=0;return OriginalEventOK;}
        Current c;if(!tasks.current(*p,c)||!currentValid(c))return OriginalEventUnmapped;
        if(r.operation==OriginalEventConcerns){
            auto a=arbitration::concerns(eventHit(*p),c.victim,c.source);if(!a.known)return OriginalEventUnmapped;
            out.answer=a.yes;if(a.alert)tasks.alert100(id);return OriginalEventOK;
        }
        if(r.operation==OriginalEventOverrides){
            // Records retain registered source lifetimes, not a shared OTHER_PED
            // sentinel. Deleted sources become null through current(), including
            // the original null/null equality when both entities have gone.
            auto*old=get(r.record_receipt);Current previous;
            if(!old||!tasks.current(*old,previous)||!currentValid(previous))return OriginalEventUnmapped;
            arbitration::Running running{r.record_kind,c.sourceIdentity==previous.sourceIdentity,r.priority>=r.record_priority,true};
            auto a=arbitration::overrides(eventHit(*p),c.source,p->plan.responseIsNM,running);
            if(!a.known)return OriginalEventUnmapped;out.answer=a.yes;return OriginalEventOK;
        }
        if(r.operation==OriginalEventRespond){unsigned built=0;if(!tasks.construct(*p,built)||built&~7u)return OriginalEventUnmapped;out.flags=built;return OriginalEventOK;}
        if(r.operation==OriginalEventAbort){bool accepted=false,drop=false;int age=r.age;
            if(!tasks.abort(*p,r.task,r.urgency,r.built,age,accepted,drop))return OriginalEventUnmapped;
            out.age=age;out.answer=accepted;out.flags=drop?8:0;return OriginalEventOK;}
        if(r.operation==OriginalEventRagdollRetry){
            if(!c.leafKnown)return OriginalEventUnmapped;
            out.answer=c.onRagdoll&&p->plan.sourceTookNM&&!c.leafIsNM&&c.leafKind!=0x840&&c.leafKind!=218;
            return OriginalEventOK;
        }
        if(r.operation==OriginalEventGive)return tasks.give(*p,r.built)?OriginalEventOK:OriginalEventUnmapped;
        return OriginalEventUnmapped;
    }
public:
    Queue(Identity identity,Tasks&host):id(identity),tasks(host){if(id.ped)q=nm_original_event_create(id.poolGeneration,id.birth);}
    Queue(const Queue&)=delete;Queue&operator=(const Queue&)=delete;
    ~Queue(){for(auto&[n,p]:payloads)tasks.release(*p);if(q)nm_original_event_destroy(q,id.poolGeneration,id.birth);}
    bool ready()const{return q&&!poisoned&&tasks.alive(id);}
    Payload*reserve(const Prepared&p){
        if(!ready()||handling||committing||p.identity!=id||p.key.victim!=id.poolGeneration||!p.sourcePermissionKnown||!p.sourceTaskReady||payloads.contains(p.key.serial))return nullptr;
        auto n=std::make_unique<Payload>();n->hit=p;
        if(!tasks.prepare(p,n->plan)){tasks.release(*n);return nullptr;}
        const auto&f=n->plan;const auto c=arbitration::concerns(eventHit(*n),f.postHit.victim,f.postHit.source);
        if(!f.known||!f.addCallEvidence||!currentValid(f.postHit)||!c.known){tasks.release(*n);return nullptr;}
        std::vector<const Payload*>live;for(auto&[serial,old]:payloads)if(old->committed)live.push_back(old.get());live.push_back(n.get());
        if(!tasks.scope(id,live)){tasks.release(*n);return nullptr;}
        auto e=input(*n);if(nm_original_event_prepare(q,id.poolGeneration,id.birth,&e)!=OriginalEventOK){tasks.release(*n);return nullptr;}
        auto*result=n.get();payloads.emplace(p.key.serial,std::move(n));return result;
    }
    bool cancel(Payload*p){if(!p||p->committed||handling||committing)return false;auto i=payloads.find(p->hit.key.serial);
        if(i==payloads.end()||i->second.get()!=p)return false;tasks.release(*p);payloads.erase(i);return true;}
    // Called after original health/mirror commit. The original queue rejection
    // is an ordinary source result, NOT a failed health transaction or retry.
    bool commit(Payload*p,NmOriginalEventAdded*result=nullptr){
        if(!ready()||!p||handling||committing||get(p->hit.key.serial)!=p)return false;
        if(p->committed)return true;committing=true;auto e=input(*p);NmOriginalEventAdded o{};
        const int status=nm_original_event_add(q,id.poolGeneration,id.birth,&e,&o);
        if(status!=OriginalEventOK){poisoned=true;committing=false;return false;}
        p->committed=true;
        // AffectsPed's alert write is part of the source admission test, even
        // when the event queue subsequently refuses a full-slot event.
        if(!p->plan.networkRefused&&arbitration::concerns(eventHit(*p),p->plan.postHit.victim,p->plan.postHit.source).alert)tasks.alert100(id);
        tasks.social(*p,o);if(result)*result=o;committing=false;return true;
    }
    int handle(const std::uint32_t*originalRoots6){
        if(!ready())return OriginalEventIdentity;if(handling||committing)return OriginalEventBusy;
        handling=true;NmOriginalEventHost host{sizeof(NmOriginalEventHost),1,this,callback};
        int result=nm_original_event_handle(q,id.poolGeneration,id.birth,originalRoots6,&host);
        if(result==OriginalEventPoisoned)poisoned=true;handling=false;collect();return result;
    }
    void collect(){
        if(!q||handling||committing||poisoned)return;
        std::uint64_t refs[20]{};int n=nm_original_event_references(q,id.poolGeneration,id.birth,refs,20);
        if(n<0||n>20){poisoned=true;return;}std::set<std::uint64_t>live(refs,refs+n);
        for(auto i=payloads.begin();i!=payloads.end();){if(i->second->committed&&!live.contains(i->first)){
            tasks.release(*i->second);i=payloads.erase(i);}else ++i;}
    }
    NmOriginalEventSnapshot snapshot(){NmOriginalEventSnapshot s{};
        if(!q||nm_original_event_snapshot(q,id.poolGeneration,id.birth,&s)!=1)s.poisoned=1;return s;}
};
}
