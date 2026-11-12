#pragma once
#include "ABI.hpp"
#include <array>
#include <vector>
#include <map>
#include "ContactMath.hpp"
using VehiclePrimitive=std::array<float,10>;
inline V triangleNear(V p,V a,V b,V c){
    // Project onto the plane, accepting it only inside all three edges.
    const V ab=b-a,ac=c-a,n=cross(ab,ac);const float nn=dot(n,n);
    if(nn>1.e-10f){V q=p-n*(dot(p-a,n)/nn);
        if(dot(cross(b-a,q-a),n)>=0&&dot(cross(c-b,q-b),n)>=0&&dot(cross(a-c,q-c),n)>=0)return q;}
    V q=segmentNear(p,a,b);for(V x:{segmentNear(p,b,c),segmentNear(p,c,a)})if(length(x-p)<length(q-p))q=x;return q;
}
struct VehicleShape {int id=-1;std::vector<VehiclePrimitive> primitives;};
static std::map<int,VehicleShape> vehicleShapes;
// SurfaceInfos_c::IsGlass 0x55E790 on g_surfaceInfos 0xB79538: the windscreen's two triangles (CGlass::CarWindscreenShatters 0x71C2B0).
inline bool glassSurface(unsigned char material){return reinterpret_cast<bool(__thiscall*)(void*,unsigned)>(0x55E790)(reinterpret_cast<void*>(0xB79538),material);}
inline std::vector<VehiclePrimitive> readVehiclePrimitives(const ColModel*col,bool withoutGlass=false){
    std::vector<VehiclePrimitive> primitives;
    auto*d=col?col->data:nullptr;if(!d)return primitives;const V center=(col->box.min+col->box.max)*.5f;
    primitives.reserve(std::min<int>(d->triangles,2048)+std::min<int>(d->boxes,128)+std::min<int>(d->spheres,128));
    if(d->tri&&d->vertices&&d->triangles<=2048)for(int i=0;i<d->triangles;i++){
        const auto t=d->tri[i];if(withoutGlass&&glassSurface(t.material))continue;
        // CColData::GetTrianglePoint decodes a packed FixedVector<int16,128>.
        auto vertex=[&](int k){const auto&v=d->vertices[k];return V{v.x/128.f,v.y/128.f,v.z/128.f}-center;};
        const V a=vertex(t.a),b=vertex(t.c),c=vertex(t.b);
        if(!finite(a)||!finite(b)||!finite(c)||length(cross(b-a,c-a))<1.e-6f)continue;
        primitives.push_back({0,a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z});
    }
    if(d->box&&d->boxes<=128)for(int i=0;i<d->boxes;i++){
        const V c=(d->box[i].box.min+d->box[i].box.max)*.5f-center,h=(d->box[i].box.max-d->box[i].box.min)*.5f;
        if(finite(c)&&finite(h)&&std::min({h.x,h.y,h.z})>0)primitives.push_back({1,c.x,c.y,c.z,h.x,h.y,h.z,0,0,0});
    }
    if(d->sphere&&d->spheres<=128)for(int i=0;i<d->spheres;i++){
        const V c=d->sphere[i].pos-center;const float r=d->sphere[i].radius;
        if(finite(c)&&r>0&&r<10)primitives.push_back({2,c.x,c.y,c.z,r,0,0,0,0,0});
    }
    return primitives;
}
// withoutGlass: the car's windscreen is gone (panel status 3, SetPanelDamage 0x6B1480 hid it): its glass no longer collides.
inline const VehicleShape& vehicleShape(int model,const ColModel*col,bool withoutGlass=false){
    auto [it,inserted]=vehicleShapes.try_emplace(withoutGlass?model|0x10000:model);auto&shape=it->second;
    if(!inserted)return shape;
    shape.primitives=readVehiclePrimitives(col,withoutGlass);
    if(!shape.primitives.empty())shape.id=nm_vehicle_shape_register(shape.primitives[0].data(),int(shape.primitives.size()));
    return shape;
}
inline bool closestVehicleSurface(const VehicleShape&shape,V p,V&out){
    float best=10000;bool found=false;
    for(auto&r:shape.primitives){const V a{r[1],r[2],r[3]};V q{};
        if(r[0]==0)q=triangleNear(p,a,{r[4],r[5],r[6]},{r[7],r[8],r[9]});
        else if(r[0]==2)q=a+normalized(p-a)*r[4];
        else {V h{r[4],r[5],r[6]},v=p-a;q={std::clamp(v.x,-h.x,h.x),std::clamp(v.y,-h.y,h.y),std::clamp(v.z,-h.z,h.z)};
            if(length(q-v)<1.e-6f){float dx=h.x-std::abs(v.x),dy=h.y-std::abs(v.y),dz=h.z-std::abs(v.z);if(dx<dy&&dx<dz)q.x=std::copysign(h.x,v.x);else if(dy<dz)q.y=std::copysign(h.y,v.y);else q.z=std::copysign(h.z,v.z);}q=q+a;}
        float d=length(q-p);if(d<best){best=d;out=q;found=true;}
    }return found;
}
