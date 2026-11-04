#pragma once
#include <unordered_map>
#include <limits>

// SA host adaptation: native wheel-particle emission/update plus swept contacts
// against the actual native vehicle COL, tagged through AddWheelDust (FxsFuncs
// relocates static globals). Experimental CENTRE contacts; billboard clipping
// remains open. Default OFF until visual acceptance. Never changes bodies.
namespace vehicleparticles {
struct Particle {void*next;void*previous;float totalLife,currentLife;V position,velocity;void*system;};
static_assert(sizeof(Particle)==0x2C&&offsetof(Particle,position)==0x10&&offsetof(Particle,velocity)==0x1C&&offsetof(Particle,system)==0x28);
struct Line {V from;float fromSize;V to;float toSize;};
static_assert(sizeof(Line)==0x20);
struct Track {void*system;float life;};
struct Vehicle {int ref;void*entity;M now,inverse,previousInverse;bool previousValid;ColModel*col;V lo,hi;};
struct Sample {void*entity;M matrix;unsigned frame;};
static std::unordered_map<Particle*,Track> tracked;
static std::map<int,Sample> samples;
static std::vector<Vehicle> scene;
static unsigned sceneFrame=~0u,reportedAt=0;
static uint64_t births=0,updates=0,queries=0,hits=0,lastReportedHits=0,createCalls=0,selectedLocal=0,lastReportedBirths=0;
static bool installed=false,attempted=false,featureEnabled=true;
static unsigned creating=0,wheelEmission=0;
static void(__thiscall*dustOriginal)(void*,void*,V,unsigned char,float)=nullptr;
static bool routeAudit(){static const bool on=GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&GetEnvironmentVariableA("GTASA_PARTICLE_ROUTE_AUDIT",nullptr,0);return on;}
static void*(__thiscall*createOriginal)(void*,const void*,M*,const V*,float,const void*,float,bool)=nullptr;
static bool(__thiscall*updateOriginal)(void*,float,Particle*)=nullptr;
static bool baseline(){static const bool on=GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&GetEnvironmentVariableA("GTASA_WHEEL_SMOKE_BASELINE",nullptr,0);return on;}
static void __fastcall dust(void*self,void*,void*car,V position,unsigned char spinning,float light){
    // FxsFuncs relocates Fx_c's static references inside the native call sites.
    // Tag the synchronous source path, not an obsolete hard-coded global array.
    wheelEmission++;dustOriginal(self,car,position,spinning,light);wheelEmission--;
}
static bool identitySystem(void*system){
    if(!system||at<void*>(system,0xC))return false;
    const M&m=at<M>(system,0x10);
    return m.pos.x==0&&m.pos.y==0&&m.pos.z==0&&m.right.x==1&&m.right.y==0&&m.right.z==0&&
        m.up.x==0&&m.up.y==1&&m.up.z==0&&m.at.x==0&&m.at.y==0&&m.at.z==1;
}
static void refreshScene(){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);if(sceneFrame==frame)return;sceneFrame=frame;scene.clear();
    auto*pool=vehiclePool();if(!pool||pool->size<1||pool->size>10000)return;
    std::erase_if(samples,[](const auto&e){return vehicleFromRef(e.first)!=e.second.entity;});
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        void*v=pool->objects+i*0xA18;auto*m=matrix(v);if(!m||at<unsigned char>(v,0x2F)!=area()||!(at<unsigned>(v,0x1C)&1u))continue;
        auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v);if(!col||!col->data)continue;
        if(!finite(col->box.min)||!finite(col->box.max)||col->box.min.x>col->box.max.x||col->box.min.y>col->box.max.y||col->box.min.z>col->box.max.z)continue;
        const int ref=(i<<8)|pool->flags[i];M previousInverse{};const auto old=samples.find(ref);
        const bool previousValid=old!=samples.end()&&old->second.entity==v&&old->second.frame+1==frame&&affineInverse(old->second.matrix,previousInverse);
        M inverse{};if(!affineInverse(*m,inverse))continue;
        const float extent=length(V{std::max(std::abs(col->box.min.x),std::abs(col->box.max.x)),std::max(std::abs(col->box.min.y),std::abs(col->box.max.y)),std::max(std::abs(col->box.min.z),std::abs(col->box.max.z))})*std::max({length(m->right),length(m->up),length(m->at)});
        const V oldPosition=previousValid?old->second.matrix.pos:m->pos;
        const V lo{std::min(oldPosition.x,m->pos.x)-extent,std::min(oldPosition.y,m->pos.y)-extent,std::min(oldPosition.z,m->pos.z)-extent};
        const V hi{std::max(oldPosition.x,m->pos.x)+extent,std::max(oldPosition.y,m->pos.y)+extent,std::max(oldPosition.z,m->pos.z)+extent};
        scene.push_back({ref,v,*m,inverse,previousInverse,previousValid,col,lo,hi});samples[ref]={v,*m,frame};
    }
}
static bool boundsMeet(V a,V b,const Box&box){
    const V d=b-a;float lo=0,hi=1;
    const float av[]={a.x,a.y,a.z},dv[]={d.x,d.y,d.z},mn[]={box.min.x,box.min.y,box.min.z},mx[]={box.max.x,box.max.y,box.max.z};
    for(int axis=0;axis<3;axis++){
        if(dv[axis]==0){if(av[axis]<mn[axis]||av[axis]>mx[axis])return false;continue;}
        float x=(mn[axis]-av[axis])/dv[axis],y=(mx[axis]-av[axis])/dv[axis];if(x>y)std::swap(x,y);lo=std::max(lo,x);hi=std::min(hi,y);if(lo>hi)return false;
    }return true;
}
static bool collide(Particle*p,V before,bool moving){
    if(!finite(before)||!finite(p->position)||!finite(p->velocity))return false;
    refreshScene();float fraction=1.f;ColPoint best{};const Vehicle*owner=nullptr;V travel{};
    for(const auto&v:scene){
        if(vehicleFromRef(v.ref)!=v.entity)continue;
        if(std::max(before.x,p->position.x)<v.lo.x||std::min(before.x,p->position.x)>v.hi.x||
           std::max(before.y,p->position.y)<v.lo.y||std::min(before.y,p->position.y)>v.hi.y||
           std::max(before.z,p->position.z)<v.lo.z||std::min(before.z,p->position.z)>v.hi.z)continue;
        const V from=moving&&v.previousValid?point(v.now,point(v.previousInverse,before)):before;
        if(!boundsMeet(point(v.inverse,from),point(v.inverse,p->position),v.col->box))continue;
        auto*nativeMatrix=matrix(v.entity);if(!nativeMatrix)continue;
        const V delta=p->position-from;if(dot(delta,delta)==0)continue;
        Line line{from,0,p->position,0};ColPoint hit{};float limit=fraction;queries++;
        // CCollision::ProcessLineOfSight0x417950, same verified native ABI used
        // by vehicleBracePoint. No CWorld ignore-entity/global mutation.
        if(reinterpret_cast<bool(__cdecl*)(const Line&,const M&,ColModel&,ColPoint&,float&,bool,bool)>(0x417950)
            (line,*nativeMatrix,*v.col,hit,limit,false,false)&&limit<=fraction&&finite(hit.point)&&finite(hit.normal)){
            fraction=limit;best=hit;owner=&v;travel=delta;
        }
    }
    if(!owner)return false;
    V normal=normalized(best.normal);if(dot(normal,travel)>0)normal=normal*-1.f;if(dot(normal,normal)==0)return false;
    // One representable coordinate outward avoids re-entering the same native
    // surface through roundoff; no guessed body radius or visual offset.
    auto outward=[](float x,float n){return n==0?x:std::nextafter(x,n>0?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity());};
    p->position={outward(best.point.x,normal.x),outward(best.point.y,normal.y),outward(best.point.z,normal.z)};
    const V com=point(owner->now,at<V>(owner->entity,0xA4));
    const V surfaceVelocity=at<V>(owner->entity,0x44)*50.f+cross(at<V>(owner->entity,0x50)*50.f,best.point-com);
    const float inward=dot(p->velocity-surfaceVelocity,normal);
    if(inward<0)p->velocity=p->velocity-normal*inward;
    hits++;return true;
}
static void*__fastcall create(void*self,void*,const void*emission,M*world,const V*velocity,float since,const void*mult,float brightness,bool local){
    creating++;void*result=createOriginal(self,emission,world,velocity,since,mult,brightness,local);creating--;
    auto*p=static_cast<Particle*>(result);if(!p)return result;tracked.erase(p);
    createCalls++;
    if(routeAudit()){
        static std::set<std::pair<void*,void*>> routes;
        const auto route=std::make_pair(at<void*>(self,8),p->system);
        if(routes.size()<32&&routes.insert(route).second){
            std::string globals;
            for(unsigned off:{0x18u,0x1Cu,0x20u,0x24u,0x3Cu})globals+=" global_"+std::to_string(off)+"="+std::to_string(uintptr_t(*reinterpret_cast<void**>(0xA9AE00+off)));
            event("particle_route_audit",-1,-1,"emitter_system="+std::to_string(uintptr_t(route.first))+" particle_system="+std::to_string(uintptr_t(route.second))+" local="+std::to_string(local)+" life="+std::to_string(p->totalLife)+globals);
        }
    }
    if(!featureEnabled||baseline()||!wheelEmission||!p->system)return result;
    if(local){selectedLocal++;if(!identitySystem(p->system))return result;}
    if(!finite(p->position)||!finite(p->velocity))return result;
    tracked[p]={p->system,p->currentLife};births++;
    // EmissionInfo_t::m_Pos+44, size68 (primary header). The native factory
    // may replace world with the primitive matrix for local particles; use its
    // actual final transform. Local coordinates are world only for proven
    // identity static systems, never inferred from proximity or magnitudes.
    if(world&&emission)collide(p,point(*world,at<V>(const_cast<void*>(emission),0x44)),false);
    return result;
}
static bool __fastcall update(void*self,void*,float dt,Particle*p){
    auto old=tracked.find(p);
    if(creating||baseline()||!featureEnabled)return updateOriginal(self,dt,p);
    if(old==tracked.end())return updateOriginal(self,dt,p);
    if(old->second.system!=p->system||p->currentLife<old->second.life){tracked.erase(old);return updateOriginal(self,dt,p);}
    const V before=p->position;const bool dead=updateOriginal(self,dt,p);
    if(dead){tracked.erase(p);return dead;}
    updates++;collide(p,before,true);tracked[p].life=p->currentLife;return dead;
}
static void maintain(bool inGame,const fs::path&ini){
    if(!inGame){if(!player())tracked.clear();samples.clear();scene.clear();sceneFrame=~0u;return;}
    if(!attempted&&*reinterpret_cast<void**>(0xA9AE24)){
        attempted=true;featureEnabled=GetPrivateProfileIntW(L"ParticleContacts",L"WheelSmokeVehicleCollision",0,ini.c_str())!=0||
            (GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&GetEnvironmentVariableA("GTASA_WHEEL_PARTICLE_CONTACTS",nullptr,0));
        if(!featureEnabled)return;
        // FxEmitter::CreateParticle RET1C and FxEmitterBP::UpdateParticle RET8
        // verified in this executable and primary gta-reversed source. Install
        // after static systems exist so FxsFuncs' existing entry routes chain.
        const auto a=MH_CreateHook(reinterpret_cast<void*>(0x4A2580),&create,reinterpret_cast<void**>(&createOriginal));
        const auto b=a==MH_OK?MH_CreateHook(reinterpret_cast<void*>(0x4A21D0),&update,reinterpret_cast<void**>(&updateOriginal)):MH_ERROR_NOT_CREATED;
        if(a!=MH_OK||b!=MH_OK){if(a==MH_OK)MH_RemoveHook(reinterpret_cast<void*>(0x4A2580));event("wheel_particle_hook_failed",-1,-1,"create="+std::to_string(a)+" update="+std::to_string(b));return;}
        const auto c=MH_CreateHook(reinterpret_cast<void*>(0x4A09C0),&dust,reinterpret_cast<void**>(&dustOriginal));
        if(c!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(0x4A2580));MH_RemoveHook(reinterpret_cast<void*>(0x4A21D0));event("wheel_particle_hook_failed",-1,-1,"dust="+std::to_string(c));return;}
        const auto ea=MH_EnableHook(reinterpret_cast<void*>(0x4A2580)),eb=MH_EnableHook(reinterpret_cast<void*>(0x4A21D0)),ec=MH_EnableHook(reinterpret_cast<void*>(0x4A09C0));
        if(ea!=MH_OK||eb!=MH_OK||ec!=MH_OK){for(auto p:{0x4A2580,0x4A21D0,0x4A09C0}){MH_DisableHook(reinterpret_cast<void*>(p));MH_RemoveHook(reinterpret_cast<void*>(p));}event("wheel_particle_hook_failed",-1,-1,"enable_create="+std::to_string(ea)+" enable_update="+std::to_string(eb)+" enable_dust="+std::to_string(ec));return;}
        installed=true;tracked.reserve(1024);event("wheel_particle_contacts_ready",-1,-1,"native_emit_update_chained=1 native_vehicle_col=1 depth_unchanged=1");
    }
    if(installed&&(routeAudit()||hits!=lastReportedHits||births!=lastReportedBirths)&&gameTime()-reportedAt>=1000){
        reportedAt=gameTime();lastReportedHits=hits;lastReportedBirths=births;
        event("wheel_particle_contacts",-1,-1,"create_calls="+std::to_string(createCalls)+" selected_local="+std::to_string(selectedLocal)+" births="+std::to_string(births)+" updates="+std::to_string(updates)+" native_queries="+std::to_string(queries)+" hits="+std::to_string(hits)+" live="+std::to_string(tracked.size()));
    }
}
}
