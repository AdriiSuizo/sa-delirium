#pragma once
static void fitNpcFootwear(void* ped,const npc_gait::Binding& binding,npc_gait::Footwear& shape){
    SkinGeometrySearch mesh;
    using Visit=void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*);
    reinterpret_cast<Visit>(0x749B70)(at<void*>(ped,0x18),&findSkinGeometry,&mesh);
    if(!mesh.skin||!mesh.geometry||reinterpret_cast<unsigned(__cdecl*)(void*)>(0x7C77E0)(mesh.skin)<unsigned(binding.count))return;
    void* morph=at<void*>(mesh.geometry,0x5C);
    if(!morph)return;
    shape.fit(binding,at<V*>(morph,0x14),reinterpret_cast<const float*(__cdecl*)(void*)>(0x7C77F0)(mesh.skin),
        reinterpret_cast<const uint32_t*(__cdecl*)(void*)>(0x7C7800)(mesh.skin),at<int>(mesh.geometry,0x14),
        reinterpret_cast<const M*(__cdecl*)(void*)>(0x7C7810)(mesh.skin));
}
