#pragma once
// Temporarily expose simulated peds only to native HIT queries. Movement
// collision stays off, so SA and NM cannot both push a car/body in one tick.
struct NativeHitScope {
    struct Entry {int ref;void* ped;bool collision;bool fallen;float rootZ;std::vector<M> matrices;};
    std::vector<Entry> entries;
    NativeHitScope(){
        if(!applyBones)return;
        // Bullet/melee queries must use the same authored skeleton as the skin,
        // including fighters who have not transferred to an NM agent.
        auto*pool=pedPool();for(int n=0;pool&&n<pool->size;n++)if(!(pool->flags[n]&128)){
            void*p=pool->objects+n*0x7C4;std::vector<T> pose;
            if(!combatQueryPose(p,pose))continue;auto*h=hierarchy(p);
            if(!h||h->num!=int(pose.size())||!matrix(p))continue;
            entries.push_back({reference(p),p,(at<unsigned char>(p,0x1C)&1)!=0,(at<unsigned char>(p,0x476)&16)!=0,matrix(p)->pos.z,{h->matrices,h->matrices+h->num}});
            for(int i=0;i<h->num;i++)write(h->matrices[i],pose[i]);
        }
        for(auto&[ref,a]:agents){
            if(a->preview||a->mounted||pedFromRef(ref)!=a->ped||a->lastRendered.empty())continue;
            auto*h=hierarchy(a->ped);if(!h||h->num!=int(a->lastRendered.size()))continue;
            if(!matrix(a->ped))continue;
            Entry e{ref,a->ped,(at<unsigned char>(a->ped,0x1C)&1)!=0,(at<unsigned char>(a->ped,0x476)&16)!=0,matrix(a->ped)->pos.z,{h->matrices,h->matrices+h->num}};
            entries.push_back(std::move(e));at<unsigned char>(a->ped,0x1C)|=1u;
            // SA queries assume a standing root even when its visual body is
            // horizontal. A ragdoll's pelvis-minus-standing-offset can be below
            // the road: expose its actual body centre during targeting only.
            const auto&pose=contactPose(*a);
            matrix(a->ped)->pos.z=std::max(matrix(a->ped)->pos.z,pose[0].p.z-.1f);
            if((pose[10].p-pose[0].p).z<.25f)at<unsigned char>(a->ped,0x476)|=16u;
            for(int n=0;n<h->num;n++)write(h->matrices[n],a->lastRendered[n]);
        }
    }
    ~NativeHitScope(){for(auto&e:entries)if(pedFromRef(e.ref)==e.ped){
        auto&flags=at<unsigned char>(e.ped,0x1C);flags=(flags&~1u)|(e.collision?1u:0u);
        auto&fallen=at<unsigned char>(e.ped,0x476);fallen=(fallen&~16u)|(e.fallen?16u:0u);
        if(matrix(e.ped))matrix(e.ped)->pos.z=e.rootZ;
        auto*h=hierarchy(e.ped);if(h&&h->num==int(e.matrices.size()))std::copy(e.matrices.begin(),e.matrices.end(),h->matrices);
    }}
};
static bool(__cdecl* originalLineOfSight)(const V&,const V&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool)=nullptr;
static bool __cdecl lineOfSight(const V&a,const V&b,ColPoint&hit,void*&entity,bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool see,bool camera,bool shoot){
    if(peds){NativeHitScope scope;return originalLineOfSight(a,b,hit,entity,buildings,vehicles,peds,objects,dummies,see,camera,shoot);}
    return originalLineOfSight(a,b,hit,entity,buildings,vehicles,peds,objects,dummies,see,camera,shoot);
}
static bool(__thiscall* originalFightProcess)(void*,void*)=nullptr;
static bool __fastcall fightProcess(void* task,void*,void* ped){
    if(combatFightTask(task,ped))return false;
    void*target=at<void*>(task,0x18);const int ref=reference(target);
    const auto active=agents.find(ref);const bool prone=active!=agents.end()&&!active->second->preview&&!active->second->recovering&&active->second->current[10].p.z-active->second->current[0].p.z<.25f;
    const float health=ref>=0?at<float>(target,0x540):0;
    bool done;{NativeHitScope scope;done=originalFightProcess(task,ped);}
    if(prone&&pedFromRef(ref)==target&&at<float>(target,0x540)<health)
        event("native_melee_prone_contact",ref,at<short>(target,0x22),"damage="+std::to_string(health-at<float>(target,0x540)));
    return done;
}
