#pragma once
#include "ContactMath.hpp"
#include "PlayerPolicy.hpp"
#include <array>

// All coordinates and point velocities are in the vehicle component frame.
// The AABB only excludes distant components. Only its actual COL primitives
// can witness a contact. Normal speed uses the same gate as native road hits.
namespace cj_traffic {
inline bool outsideBox(V p,V h,float r){
    return std::abs(p.x)>h.x+r || std::abs(p.y)>h.y+r || std::abs(p.z)>h.z+r;
}
struct Contact {V point{},normal{};};
inline bool overlaps(const std::array<float,10>& row,V centre,float radius,Contact& hit){
    const V a{row[1],row[2],row[3]};
    const V b{row[4],row[5],row[6]};
    const V c{row[7],row[8],row[9]};
    if(row[0]==2){
        const V delta=centre-a;
        const float gap=length(delta);
        if(gap>=radius+row[4] || gap<1.e-6f)return false;
        hit.normal=delta*(1.f/gap);hit.point=a+hit.normal*row[4];return true;
    }
    if(row[0]==1){
        const V local=centre-a;
        if(outsideBox(local,b,radius))return false;
        V closest{std::clamp(local.x,-b.x,b.x),std::clamp(local.y,-b.y,b.y),std::clamp(local.z,-b.z,b.z)};
        const V delta=local-closest;
        const float gap=length(delta);
        if(gap>=radius)return false;
        if(gap>1.e-6f)hit.normal=delta*(1.f/gap);
        else{
            const V clear{b.x-std::abs(local.x),b.y-std::abs(local.y),b.z-std::abs(local.z)};
            if(clear.x<=clear.y && clear.x<=clear.z){hit.normal={std::copysign(1.f,local.x),0,0};closest.x=hit.normal.x*b.x;}
            else if(clear.y<=clear.z){hit.normal={0,std::copysign(1.f,local.y),0};closest.y=hit.normal.y*b.y;}
            else{hit.normal={0,0,std::copysign(1.f,local.z)};closest.z=hit.normal.z*b.z;}
        }
        hit.point=a+closest;return true;
    }
    if(row[0]!=0)return false;
    if(centre.x<std::min({a.x,b.x,c.x})-radius || centre.x>std::max({a.x,b.x,c.x})+radius ||
       centre.y<std::min({a.y,b.y,c.y})-radius || centre.y>std::max({a.y,b.y,c.y})+radius ||
       centre.z<std::min({a.z,b.z,c.z})-radius || centre.z>std::max({a.z,b.z,c.z})+radius)return false;
    hit.point=triangleNear(centre,a,b,c);
    const V delta=centre-hit.point;
    const float gap=length(delta);
    if(gap>=radius)return false;
    hit.normal=gap>1.e-6f?delta*(1.f/gap):normalized(cross(b-a,c-a));
    return true;
}
template<class Rows>
bool hitsPart(const Rows& rows,V half,V centre,V axis,float segmentHalf,float radius,
              V vehicleOrigin,V vehicleLinear,V vehicleSpin,V bodyLinear,V bodySpin){
    if(!(radius>0) || outsideBox(centre,half,segmentHalf+radius))return false;
    // Inscribed spheres along the rendered capsule; none can reach outside it.
    const int intervals=std::max(1,int(std::ceil(2.f*segmentHalf/radius)));
    for(int j=0;j<=intervals;++j){
        const V sample=centre+axis*(segmentHalf*(2.f*j/intervals-1.f));
        if(outsideBox(sample,half,radius))continue;
        for(const auto& row:rows){
            Contact hit;
            if(!overlaps(row,sample,radius,hit))continue;
            const V car=vehicleLinear+cross(vehicleSpin,hit.point-vehicleOrigin);
            const V body=bodyLinear+cross(bodySpin,hit.point-centre);
            if(playerMovingContact(dot(car,hit.normal),dot(car-body,hit.normal)))return true;
        }
    }
    return false;
}
}
