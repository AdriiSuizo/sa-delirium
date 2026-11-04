#pragma once
// Read-only Win32 views of the fields consumed by our vehicle contact importer.
// Field offsets are checked against the installed native API; no SDK function
// bodies or complete RenderWare headers are required or redistributed here.
#include "Math.hpp"
#include <cstdint>
#include <cstddef>
struct RwObject {std::uint8_t type,subType,flags,privateFlags;void* parent;};
using RwMatrix=M;
struct RwFrame {
    RwObject object;void* dirtyLinks[2];RwMatrix modelling,ltm;
    void* objectLinks[2];RwFrame *child,*next,*root;
};
struct RwObjectHasFrame {RwObject object;void* frameLinks[2];void* syncCallback;};
struct RpTriangle {std::uint16_t vertIndex[3],materialIndex;};
struct RpGeometry;
struct RpMorphTarget {
    RpGeometry* geometry;V boundingCentre;float boundingRadius;V *verts,*normals;
};
struct RpGeometry {
    RwObject object;std::uint32_t flags;std::uint16_t locked;std::int16_t references;
    std::int32_t numTriangles,numVertices,numMorphTargets,numTexCoordSets;
    void* materials;std::int32_t materialCount,materialCapacity;
    RpTriangle* triangles;void* prelit;void* textureCoordinates[8];
    void *mesh,*instance;RpMorphTarget* morphTarget;
};
struct RpAtomic {
    RwObjectHasFrame object;void* instance;RpGeometry* geometry;
    std::byte unusedAfterGeometry[0x70-0x1c];
};
struct RpClump {RwObject object;std::byte unusedAfterObject[0x2c-8];};
using RwObjectCallBack=RwObject*(__cdecl*)(RwObject*,void*);
using RpAtomicCallBack=RpAtomic*(__cdecl*)(RpAtomic*,void*);
inline constexpr unsigned rpATOMICRENDER=4;
static_assert(sizeof(void*)==4);
static_assert(sizeof(RwObject)==8&&offsetof(RwObject,parent)==4);
static_assert(sizeof(RwMatrix)==64&&offsetof(RwFrame,modelling)==0x10);
static_assert(offsetof(RwFrame,ltm)==0x50&&offsetof(RwFrame,root)==0xa0);
static_assert(sizeof(RpTriangle)==8&&sizeof(RpMorphTarget)==0x1c&&offsetof(RpMorphTarget,verts)==0x14);
static_assert(sizeof(RpAtomic)==0x70&&offsetof(RpAtomic,geometry)==0x18);
static_assert(sizeof(RpGeometry)==0x60&&offsetof(RpGeometry,numTriangles)==0x10&&offsetof(RpGeometry,numMorphTargets)==0x18);
static_assert(offsetof(RpGeometry,triangles)==0x2c&&offsetof(RpGeometry,morphTarget)==0x5c);
