#pragma once
// Diagnostics only (GTASA_SKIN_GROUND_AUDIT=1): what is DRAWN, not what is
// simulated. Every 100 ms per live agent, skin the actual geometry with the
// palette just published to the HAnim hierarchy (palette * inverse bind *
// vertex, RpSkin weights) and measure how far each body region's lowest
// vertices lie below the collision world straight beneath them. Pair with
// GTASA_SOLVER_GAP_AUDIT (solver rows) to separate shape/render mismatch
// from physical penetration.
#include "ExpandedSkinRenderHost.hpp"
struct SkinGroundWindow{unsigned last=0;};
static std::map<int,SkinGroundWindow> skinGroundWindows;
static bool skinGroundAuditEnabled(){static const bool on=GetEnvironmentVariableA("GTASA_SKIN_GROUND_AUDIT",nullptr,0)>0;return on;}
static int skinGroundRegion(int id){
    if(id>=5&&id<=8)return 0;                                  // head
    if((id>=24&&id<=26)||(id>=34&&id<=36))return 1;            // hands
    if(id==23||id==33)return 2;                                // forearms
    if(id==22||id==32)return 3;                                // upper arms
    if(id>=41&&id<=54)return 5;                                // legs
    return 4;                                                  // torso/neck/clavicles/other
}
struct SkinAtomicSearch{expandedSkinHost::Atomic*atomic=nullptr;void*skin=nullptr;};
static void* __cdecl findSkinAtomic(void*atomic,void*arg){
    auto&s=*static_cast<SkinAtomicSearch*>(arg);if(s.skin)return atomic;
    auto*a=static_cast<expandedSkinHost::Atomic*>(atomic);
    if(a->geometry)if(void*skin=reinterpret_cast<void*(__cdecl*)(void*)>(0x7C7550)(a->geometry)){s.atomic=a;s.skin=skin;}
    return atomic;
}
static void noteSkinGroundAudit(void*ped,const Active&a,const Hierarchy*h,unsigned now){
    if(!skinGroundAuditEnabled()||!collision||!collision->world||!h||!h->matrices)return;
    auto&w=skinGroundWindows[a.ref];if(w.last&&now-w.last<100)return;w.last=now;
    SkinAtomicSearch search;
    reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(at<void*>(ped,0x18),&findSkinAtomic,&search);
    if(!search.skin)return;const auto*g=search.atomic->geometry;
    if(!g||g->morphsCount<1||!g->morph||!g->morph->vertices||g->verticesCount<1||g->verticesCount>65535)return;
    const unsigned bones=reinterpret_cast<unsigned(__cdecl*)(void*)>(0x7C77E0)(search.skin);
    const float*weights=reinterpret_cast<const float*(__cdecl*)(void*)>(0x7C77F0)(search.skin);
    const uint32_t*indices=reinterpret_cast<const uint32_t*(__cdecl*)(void*)>(0x7C7800)(search.skin);
    const M*inverseBind=reinterpret_cast<const M*(__cdecl*)(void*)>(0x7C7810)(search.skin);
    if(!weights||!indices||!inverseBind||bones<unsigned(h->num)||size_t(h->num)!=a.rig->ids.size())return;
    constexpr int R=6,K=6;static const char*names[R]={"head","hands","forearms","upperarms","torso","legs"};
    struct Low{float z;V p;int bone;};std::array<std::array<Low,K>,R> low;
    for(auto&r:low)for(auto&l:r)l={INFINITY,{},-1};
    std::array<unsigned,R> counts{};
    for(int i=0;i<g->verticesCount;i++){
        const V v=g->morph->vertices[i];V p{};float total=0;int dominant=-1;float best=0;
        for(int k=0;k<4;k++){
            const float wt=weights[4*i+k];if(wt<=0)continue;const int b=(indices[i]>>(8*k))&255;
            if(b>=h->num)continue;
            p=p+point(h->matrices[b],point(inverseBind[b],v))*wt;total+=wt;if(wt>best){best=wt;dominant=b;}
        }
        if(total<.5f||dominant<0)continue;p=p*(1.f/total);if(!finite(p))continue;
        const int r=skinGroundRegion(a.rig->ids[dominant]);counts[r]++;
        auto&slot=low[r];int worst=0;for(int k=1;k<K;k++)if(slot[k].z>slot[worst].z)worst=k;
        if(p.z<slot[worst].z)slot[worst]={p.z,p,a.rig->ids[dominant]};
    }
    std::string detail="recovering="+std::to_string(a.recovering)+" kind="+std::to_string(int(a.kind))+" vertices="+std::to_string(g->verticesCount);
    for(int r=0;r<R;r++){
        if(!counts[r])continue;float depth=-INFINITY;int bone=-1;unsigned below=0;
        for(const auto&l:low[r]){
            if(l.bone<0)continue;
            const V from=l.p+V{0,0,.35f},down{0,0,-1};float distance=0;V normal{};
            if(nm_world_raycast(collision->world,&from.x,&down.x,1.5f,&distance,&normal.x)<=0)continue;
            const float d=(from.z-distance)-l.p.z;if(d>0)below++;if(d>depth){depth=d;bone=l.bone;}
        }
        if(bone<0)continue;
        detail+=std::string(" ")+names[r]+"_depth="+std::to_string(depth)+" "+names[r]+"_bone="+std::to_string(bone)+" "+names[r]+"_lowest_below="+std::to_string(below);
    }
    event("skin_ground_sample",a.ref,a.model,detail+" source=skinned_published_palette");
}
