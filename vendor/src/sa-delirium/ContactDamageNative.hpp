#pragma once
#include "ContactDamageTransport.hpp"
// ABI.hpp must precede this file. No hook is installed by this backend.
namespace contact_damage_native {
struct Calculator {void*source;float amount;int piece,weapon;bool speak;};
static_assert(sizeof(Calculator)==0x14&&offsetof(Calculator,weapon)==0xC);
struct Functions {
    void*(__thiscall*construct)(void*,void*,unsigned,int,int,unsigned char,bool,bool)=nullptr;
    void(__thiscall*destroy)(void*)=nullptr;
    bool(__thiscall*affects)(void*,void*)=nullptr;
    // Must be the verified original trampoline, so acceptedDamage cannot
    // schedule a second road request while delivery commits the same contact.
    void(__thiscall*calculate)(Calculator*,void*,DamageResponse&,bool)=nullptr;
    void*(__thiscall*add)(void*,void*,bool)=nullptr;
};
struct Context {
    void*ped=nullptr;void*source=nullptr;void*eventGroup=nullptr;
    bool inVehicle=false;
    // Required: generation, clump and exact solved receipt still belong to this
    // ped. Also certifies physical-response ownership through event handling.
    bool(*validate)(const contact_damage_transport::Request&,const Context&)=nullptr;
    bool(*prepareEventResponse)(DamageEvent&,const contact_damage_transport::Request&,const Context&)=nullptr;
    bool responseBridgeReady=false;
};
class Backend {
    Functions fn;Context context;
public:
    using Event=DamageEvent;
    Backend(Functions f,Context c):fn(f),context(c){}
    bool fresh(const contact_damage_transport::Request&r){
        return context.responseBridgeReady&&context.ped&&context.eventGroup&&context.validate&&
            context.prepareEventResponse&&fn.construct&&fn.destroy&&fn.affects&&fn.calculate&&fn.add&&context.validate(r,context);
    }
    void construct(Event&e,const contact_damage_transport::Request&r){
        fn.construct(&e,context.source,r.time,r.weapon,r.piece,r.direction,false,context.inVehicle);
    }
    void destroy(Event&e){fn.destroy(&e);}
    bool affects(Event&e,const contact_damage_transport::Request&){return fn.affects(&e,context.ped);}
    contact_damage_transport::Response calculate(Event&e,const contact_damage_transport::Request&r){
        Calculator calculator{context.source,r.nativeBaseAmount,r.piece,r.weapon,false};
        fn.calculate(&calculator,context.ped,e.response,false);
        return {e.response.health,e.response.armour,e.response.zero,e.response.force,e.response.calculated,e.response.check};
    }
    bool enqueue(Event&e,const contact_damage_transport::Request&r,const contact_damage_transport::Response&){
        // This callback owns the explicit SA-event→IV-task bridge. Merely
        // clearing an animation ID is insufficient and is deliberately refused.
        if(!context.prepareEventResponse(e,r,context))return false;
        return fn.add(context.eventGroup,&e,false)!=nullptr;
    }
};
}
