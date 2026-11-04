#pragma once
// CEntity::UpdateAnim owns the only animation advance. Requests for a physical
// palette merely require its full native evaluation and later publish matrices.
// In particular, publishing after a root correction must not call UpdateBlend,
// UpdateTime, finish callbacks, or the animation node cursor a second time.
struct NativeEvaluatedClip {
    void*association=nullptr;void*hierarchy=nullptr;short group=-1,id=-1;
    float timeBefore=0,timeAfter=0,blendBefore=0,blendAfter=0;
};
struct NativePaletteEvaluation {
    int ref=-1,model=-1;void*ped=nullptr;void*clump=nullptr;
    unsigned frame=0;uint64_t serial=0;float seconds=0;
    std::vector<NativeEvaluatedClip> clips;
};
static std::map<int,NativePaletteEvaluation> nativePaletteEvaluations;
static uint64_t nativePaletteSerial=0;
static unsigned nativePaletteFrame=~0u;
static void(*nativePaletteSampleObserver)(void*)=nullptr;
static std::vector<NativeEvaluatedClip> nativePaletteClipIdentities(void*clump);
static bool nativePaletteRequested(void*p){
    return ready&&p&&(at<unsigned char>(p,0x36)&7)==3&&reference(p)>=0&&matrix(p)&&at<void*>(p,0x18)&&
        player()&&at<unsigned char>(p,0x2F)==area()&&length(position(p)-position(player()))<=radius+5.f;
}
static const NativePaletteEvaluation*currentNativePaletteEvaluation(void*p){
    if(!p)return nullptr;const auto i=nativePaletteEvaluations.find(reference(p));
    if(i==nativePaletteEvaluations.end())return nullptr;const auto&e=i->second;
    return e.ped==p&&e.clump==at<void*>(p,0x18)&&e.model==at<short>(p,0x22)&&
        e.frame==*reinterpret_cast<unsigned*>(0xB7CB4C)?&e:nullptr;
}
static bool nativePaletteEvaluatedClip(void*p,void*association,uint64_t afterSerial=0){
    const auto*e=currentNativePaletteEvaluation(p);if(!e||e->serial<=afterSerial||!association)return false;
    const auto live=nativePaletteClipIdentities(e->clump);
    for(const auto&clip:e->clips)if(clip.association==association&&std::any_of(live.begin(),live.end(),[&](const auto&c){
        return c.association==association&&clip.hierarchy==c.hierarchy&&clip.group==c.group&&clip.id==c.id;
    }))return true;
    return false;
}
static bool publishCurrentNativePalette(void*p){
    if(!currentNativePaletteEvaluation(p)||!matrix(p))return false;
    // Matrix/root publication only, using the interpolators written by the
    // single real evaluation. No association or gameplay clock is advanced.
    reinterpret_cast<void(__thiscall*)(void*)>(0x446F90)(p);
    reinterpret_cast<void(__thiscall*)(void*)>(0x532B00)(p);
    auto&flags=at<unsigned char>(p,0x1E);const auto old=flags;flags&=~128u;
    reinterpret_cast<void(__thiscall*)(void*)>(0x532B20)(p);flags=old;return true;
}
static std::vector<NativeEvaluatedClip> nativePaletteClipIdentities(void*clump){
    std::vector<NativeEvaluatedClip> clips;
    void*a=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump);
    // Bound/cycle detection protects observation if another ASI damages a list;
    // it never removes or repairs the game's association links.
    for(unsigned count=0;a&&count<128;count++){
        if(std::any_of(clips.begin(),clips.end(),[&](const auto&c){return c.association==a;})){clips.clear();return clips;}
        clips.push_back({a,at<void*>(a,0x14),at<short>(a,0xE),at<short>(a,0x2C),at<float>(a,0x20),0,at<float>(a,0x18),0});
        a=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(a);
    }
    if(a)clips.clear();return clips;
}
static void __cdecl nativeEntityAnimationEvaluation(void*entity,void*clump,float dt,bool onScreen){
    const bool wanted=nativePaletteRequested(entity)&&at<void*>(entity,0x18)==clump;
    const int ref=wanted?reference(entity):-1,model=wanted?at<short>(entity,0x22):-1;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    auto before=wanted?nativePaletteClipIdentities(clump):std::vector<NativeEvaluatedClip>{};
    // Calling UpdateAnimations(0) later is not equivalent: it can delete a
    // faded association and invoke its callback. Ask for complete evaluation
    // here, including offscreen bodies, without adding a second native call.
    reinterpret_cast<void(__cdecl*)(void*,float,bool)>(0x4D34F0)(clump,dt,onScreen||wanted);
    if(!wanted||pedFromRef(ref)!=entity||at<void*>(entity,0x18)!=clump||at<short>(entity,0x22)!=model||
       *reinterpret_cast<unsigned*>(0xB7CB4C)!=frame)return;
    if(nativePaletteFrame!=frame){nativePaletteEvaluations.clear();nativePaletteFrame=frame;}
    NativePaletteEvaluation evaluation{ref,model,entity,clump,frame,++nativePaletteSerial,dt};
    const auto after=nativePaletteClipIdentities(clump);
    for(auto clip:before){
        const auto same=std::find_if(after.begin(),after.end(),[&](const auto&c){return c.association==clip.association&&c.hierarchy==clip.hierarchy&&c.group==clip.group&&c.id==clip.id;});
        if(same==after.end())continue;
        // A callback-created association at the end of UpdateTime is absent
        // from 'before'; it must await the next genuine native evaluation.
        // Zero-weight clips likewise did not contribute to the sampled pose.
        if(!(same->blendBefore>0))continue;
        clip.timeAfter=same->timeBefore;clip.blendAfter=same->blendBefore;evaluation.clips.push_back(clip);
    }
    nativePaletteEvaluations[ref]=std::move(evaluation);
    if(nativePaletteSampleObserver)nativePaletteSampleObserver(entity);
}
// Verified caller 535F00 keeps its CEntity* in ESI through CALL535F94. Preserve
// the existing three-argument cdecl stack and forward the owner explicitly.
// This includes newly spawned peds without guessing owner from a clump cache.
static __declspec(naked) void nativeEntityAnimationsCall(){
    __asm {
        push ebp
        mov ebp,esp
        push dword ptr [ebp+16]
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
        push esi
        call nativeEntityAnimationEvaluation
        add esp,16
        pop ebp
        ret
    }
}
