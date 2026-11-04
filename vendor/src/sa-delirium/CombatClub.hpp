#pragma once
// CVisibilityPlugins::RenderWeaponPedsForPC attaches the weapon clump
// directly to the right-hand HAnim matrix. Read the actual model's bounds in
// that clump space, including any child frame transforms.
struct CombatClubMesh {
    void*root=nullptr;
    V min{},max{};
    bool seen=false;
};
static void* __cdecl combatClubAtomic(void*atomic,void*context){
    auto&m=*static_cast<CombatClubMesh*>(context);
    auto*geo=at<void*>(atomic,0x18);
    if(!geo)return atomic;
    const int count=at<int>(geo,0x14);
    auto*morph=at<void*>(geo,0x5C);
    auto*vertices=morph?at<V*>(morph,0x14):nullptr;
    if(!vertices||count<1||count>65535)return atomic;
    M relative{};write(relative,{});
    auto*frame=at<void*>(atomic,4);
    for(int depth=0;frame&&frame!=m.root&&depth<16;depth++){
        relative=affineProduct(at<M>(frame,0x10),relative);frame=at<void*>(frame,4);
    }
    if(frame!=m.root)return atomic;
    for(int i=0;i<count;i++){
        const V v=point(relative,vertices[i]);if(!finite(v))continue;
        if(!m.seen){m.min=m.max=v;m.seen=true;continue;}
        m.min={std::min(m.min.x,v.x),std::min(m.min.y,v.y),std::min(m.min.z,v.z)};
        m.max={std::max(m.max.x,v.x),std::max(m.max.y,v.y),std::max(m.max.z,v.z)};
    }
    return atomic;
}
static bool combatClubBounds(void*clump,std::array<float,8>&out){
    if(!clump)return false;
    CombatClubMesh m{at<void*>(clump,4)};if(!m.root)return false;
    reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(clump,&combatClubAtomic,&m);
    if(!m.seen)return false;
    const V size=m.max-m.min;
    const V axis=size.x>=size.y&&size.x>=size.z?V{1,0,0}:size.y>=size.z?V{0,1,0}:V{0,0,1};
    const float low=dot(m.min,axis),high=dot(m.max,axis),extent=high-low;
    if(!(extent>.01f)||extent>5.f)return false;
    const V center=(m.min+m.max)*.5f;
    const V transverse=center-axis*dot(center,axis);
    const V a=transverse+axis*low,b=transverse+axis*high;
    out={a.x,a.y,a.z,b.x,b.y,b.z,length(size)*.5f,std::clamp(-low/extent,0.f,1.f)};
    return true;
}
static void* nativeClubTarget(void*p){
    auto*manager=taskManager(p);if(!manager)return nullptr;
    auto*primary=reinterpret_cast<void*(__thiscall*)(void*)>(0x6819D0)(manager);
    for(auto*t:{at<void*>(manager,0x14),primary}){
        if(t&&reinterpret_cast<int(__thiscall*)(void*)>(at<void**>(t,0)[4])(t)==TASK_SIMPLE_FIGHT)
            if(auto*target=at<void*>(t,0x18);reference(target)>=0)return target;
    }
    return nullptr;
}
