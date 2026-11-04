// SPDX-License-Identifier: GPL-3.0-only
// CEntity::UpdateRwMatrix is inlined in several Steam callers. Reconstruct the
// host boundary using the real native matrix/simple-transform implementations.
// Reference: gta-reversed Entity.cpp UpdateRwMatrix; native CWorld::Add 57C410.
#include "SteamNativeBindings.hpp"
#include "NativeGeometryViews.hpp"
namespace sa_steam_glue {
template<class T>static T& field(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+n);}
void __fastcall updateRwMatrix(void* entity){
    auto* object=field<RwObject*>(entity,0x18);
    if(!object)return;
    auto* frame=static_cast<RwFrame*>(object->parent);
    if(!frame)return;
    if(auto* matrix=field<void*>(entity,0x14)){
        using Update=void(__thiscall*)(void*,RwMatrix*);
        reinterpret_cast<Update>(sa_steam_native::address<0x59ad70>())(matrix,&frame->modelling);
    }else{
        using Update=void(__thiscall*)(void*,RwMatrix*);
        reinterpret_cast<Update>(sa_steam_native::address<0x54ef40>())(static_cast<unsigned char*>(entity)+4,&frame->modelling);
    }
}
}
