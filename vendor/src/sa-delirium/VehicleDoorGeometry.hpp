#pragma once
#include "../plugin-sdk-sa/plugin_sa/game_sa/rw/rwcore.h"
#include "../plugin-sdk-sa/plugin_sa/game_sa/rw/rpworld.h"
#include <tuple>
// Real rendered door triangles, with native hinge transform. Keep native door
// dynamics; this supplies their moving geometry to NM, not an invented handle.
static_assert(offsetof(RpAtomic,geometry)==0x18&&offsetof(RpGeometry,triangles)==0x2C&&offsetof(RpGeometry,morphTarget)==0x5C);
static_assert(offsetof(RwFrame,modelling)==0x10&&sizeof(RwMatrix)==sizeof(M));
struct DoorShape {VehicleShape shape;V centre{},half{};};
static std::map<std::tuple<int,int,void*>,DoorShape> doorShapes;
static RwObject* __cdecl visibleDoorObject(RwObject*object,void*arg){
    if(object&&object->type==1&&(object->flags&rpATOMICRENDER)){
        auto*a=reinterpret_cast<RpAtomic*>(object);
        if(a->geometry)*static_cast<RpGeometry**>(arg)=a->geometry;
    }return object;
}
static const DoorShape* doorShape(void*car,int node,void*frame){
    RpGeometry*g=nullptr;reinterpret_cast<void*(__cdecl*)(void*,RwObjectCallBack,void*)>(0x7F1200)(frame,visibleDoorObject,&g);
    if(!g||!g->morphTarget||!g->morphTarget[0].verts||!g->triangles||g->numVertices<3||g->numVertices>50000||g->numTriangles<1||g->numTriangles>2048)return nullptr;
    auto[it,fresh]=doorShapes.try_emplace({at<short>(car,0x22),node,g});auto&out=it->second;if(!fresh)return &out;
    V lo{1000,1000,1000},hi{-1000,-1000,-1000};
    auto vertex=[&](int i){const auto&p=g->morphTarget[0].verts[i];return V{p.x,p.y,p.z};};
    for(int i=0;i<g->numVertices;i++){const V p=vertex(i);if(!finite(p)||length(p)>20)return nullptr;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    out.centre=(lo+hi)*.5f;out.half=(hi-lo)*.5f;
    for(int i=0;i<g->numTriangles;i++){const auto&t=g->triangles[i];if(t.vertIndex[0]>=g->numVertices||t.vertIndex[1]>=g->numVertices||t.vertIndex[2]>=g->numVertices)continue;
        const V a=vertex(t.vertIndex[0])-out.centre,b=vertex(t.vertIndex[1])-out.centre,c=vertex(t.vertIndex[2])-out.centre;
        if(length(cross(b-a,c-a))<1.e-6f)continue;out.shape.primitives.push_back({0,a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z});}
    if(out.shape.primitives.empty())return nullptr;
    out.shape.id=nm_vehicle_shape_register(out.shape.primitives[0].data(),int(out.shape.primitives.size()));return out.shape.id>=0?&out:nullptr;
}
