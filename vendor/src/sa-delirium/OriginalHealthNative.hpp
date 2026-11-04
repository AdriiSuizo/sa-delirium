#pragma once
#include "OriginalHealthRuntime.hpp"
// ABI.hpp must precede this file. This backend installs no hook and chooses no
// executable addresses. Functions must be pinned originals verified by host.
// This is the isolated queue-first transport proof, NOT the runtime source
// order. Runtime must use commitOriginal and a retained accounting outbox;
// otherwise a full native queue delays source health/reaction incorrectly.
// It is not admissible unless original source task handling is already ready.
namespace original_health_native {
using Prepared=original_health_runtime::Prepared;
using NativeValues=original_health_runtime::NativeValues;
struct Functions {
    void*(__thiscall*construct)(void*,void*,unsigned,int,int,unsigned char,bool,bool)=nullptr;
    void(__thiscall*destroy)(void*)=nullptr;
    void*(__thiscall*add)(void*,void*,bool)=nullptr;
    void(__thiscall*remove)(void*,void*)=nullptr;
};
struct Context {
    void*ped=nullptr;void*source=nullptr;void*eventGroup=nullptr;void*user=nullptr;
    unsigned char direction=0;bool inVehicle=false;
    // Includes victim/source generation, current reservation, queue owner,
    // native mirror acknowledgement, pinned image, source task capability and
    // the explicit original permission/accounting decision. AffectsPed's
    // calculated/check=false fast path CANNOT replace that source decision.
    bool(*validate)(const Prepared&,const Context&)=nullptr;
    // This reserves source response storage without beginning the response or
    // altering health. It must also arrange exact native-clone lifecycle
    // tracking so a later SA handler cannot create an incompatible second task.
    bool(*reserveResponse)(const Prepared&,const Context&)=nullptr;
    void(*releaseResponse)(const Prepared&,const Context&)=nullptr;
    void(*committedResponse)(const Prepared&,void*clone,const Context&)=nullptr;
    // Mirror policy is already calculated by the source Owner; host performs
    // exactly these writes, with its known-writer guard. Never calls calculator.
    void(*writeMirror)(const NativeValues&,const Context&)=nullptr;
};
class Backend {
    Functions fn;Context context;const Prepared*reserved=nullptr;
    bool queueRoom()const{
        // CEventGroup: vptr0,ped4,count8,eventsC; Add4AB420 caps at16.
        // Check BEFORE invoking editable-response notifications in Add.
        return context.eventGroup&&at<void*>(context.eventGroup,4)==context.ped&&
            at<unsigned>(context.eventGroup,8)<16;
    }
public:
    Backend(Functions f,Context c):fn(f),context(c){}
    bool fresh(const Prepared&p){
        return context.ped==reinterpret_cast<void*>(p.identity.ped)&&context.eventGroup&&
            context.validate&&context.reserveResponse&&context.releaseResponse&&context.committedResponse&&context.writeMirror&&
            fn.construct&&fn.destroy&&fn.add&&fn.remove&&context.validate(p,context);
    }
    void*publish(const Prepared&p){
        if(reserved||!queueRoom()||!fresh(p)||!context.reserveResponse(p,context))return nullptr;
        reserved=&p;
        DamageEvent e{};
        fn.construct(&e,context.source,p.key.time,int(p.key.nativeWeapon),int(p.key.piece),context.direction,false,context.inVehicle);
        e.response.health=p.nativeBefore.health-p.nativeAfter.health;
        e.response.armour=p.nativeBefore.armour-p.nativeAfter.armour;
        // INJURED0x10 retains positive health and is NOT native death. The
        // source response owner must handle it, without SA ComplexDie zeroing.
        e.response.zero=(p.outcome.flags&0x20)!=0;
        e.response.force=false;e.response.calculated=true;e.response.check=false;
        // No source task maps to SA's canned damage/fall animation here. This
        // is only a carrier; the reserved response bridge owns later dispatch.
        e.flags&=~2u;
        void*clone=nullptr;
        if(fresh(p)&&queueRoom())clone=fn.add(context.eventGroup,&e,false);
        fn.destroy(&e);
        if(!clone){context.releaseResponse(p,context);reserved=nullptr;}
        return clone;
    }
    void cancel(void*clone){
        if(clone)fn.remove(context.eventGroup,clone);
        if(reserved)context.releaseResponse(*reserved,context);
        reserved=nullptr;
    }
    void writeMirror(const NativeValues&v){context.writeMirror(v,context);}
    void attachSourceResponse(const Prepared&p,void*clone){
        // Commit immediately: SA HandleEvents4C3F10 can remove equal Damage or
        // postpone it on abort refusal. Source response cannot wait for that.
        context.committedResponse(p,clone,context);reserved=nullptr;
    }
};
}
