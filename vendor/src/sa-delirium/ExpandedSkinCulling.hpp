#pragma once
// Conservative TEST_ONLY proof for the culling decision already made by SA.
// No visibility flag, model COL, occluder or original bound is modified.
#include "CpuExpandedSkin.hpp"
#include <cstring>

namespace expandedSkinTest {
struct NativeEnvelope {
    unsigned frame=0;uint64_t generation=0;bool valid=false;
    M entityWorld{};V boxMin{},boxMax{},sphereCentre{};float radius=0;
};
struct EnvelopeCheck {bool okay=false;float boxExcess=0,sphereExcess=0;size_t examined=0;};
inline bool validEnvelope(const NativeEnvelope&e){
    return e.valid&&e.generation&&expandedSkin::affineFinite(e.entityWorld)&&finite(e.boxMin)&&finite(e.boxMax)&&finite(e.sphereCentre)&&
        std::isfinite(e.radius)&&e.radius>0&&e.boxMin.x<=e.boxMax.x&&e.boxMin.y<=e.boxMax.y&&e.boxMin.z<=e.boxMax.z;
}
inline EnvelopeCheck withinNativeEnvelope(const NativeEnvelope&e,unsigned frame,uint64_t generation,
                                          const expandedSkin::Vertex*vertices,size_t count,const M&atomicWorld){
    EnvelopeCheck out;M inverse{};
    if(!validEnvelope(e)||e.frame!=frame||e.generation!=generation||!vertices||!count||
       !expandedSkin::affineFinite(atomicWorld)||!affineInverse(e.entityWorld,inverse))return out;
    const V centre=point(e.entityWorld,e.sphereCentre);
    for(size_t i=0;i<count;i++){
        const V world=point(atomicWorld,vertices[i].position),local=point(inverse,world);
        if(!finite(world)||!finite(local))return out;
        out.boxExcess=std::max({out.boxExcess,e.boxMin.x-local.x,local.x-e.boxMax.x,e.boxMin.y-local.y,local.y-e.boxMax.y,e.boxMin.z-local.z,local.z-e.boxMax.z});
        // Native camera culling transforms the centre but keeps COL radius.
        // Do not quietly scale that radius for our render stream.
        out.sphereExcess=std::max(out.sphereExcess,length(world-centre)-e.radius);++out.examined;
    }
    out.okay=out.boxExcess==0&&out.sphereExcess==0;return out;
}
inline bool sameAffine(const M&a,const M&b){
    return !std::memcmp(&a.right,&b.right,sizeof(V))&&!std::memcmp(&a.up,&b.up,sizeof(V))&&
           !std::memcmp(&a.at,&b.at,sizeof(V))&&!std::memcmp(&a.pos,&b.pos,sizeof(V));
}
} // namespace expandedSkinTest
