#pragma once
// Experimental GPU material-render bridge, unwired. CPU already applied every
// expanded influence. The GPU sees position/normal/UV without blend indices.
// Ports the material/render portion of owned SA7C8060; native skin instancing
// and bone uploads are deliberately outside this unskinned stream's contract.
#include "ExpandedSkinRenderHost.hpp"
#include <array>

namespace expandedGpu {
using expandedSkinHost::Atomic;
struct Vector4 {float x,y,z,w;};
struct Descriptor {uint8_t lights=0,spotsTex=0,vertex=0,fogEffect=0;};
struct Dispatch {uint8_t material=255,fog=255,effect=255,morph=255,bones=255,pad[3]{};};
struct Material {void*texture;uint32_t colour;void*pipeline;float ambient,specular,diffuse;int16_t refs,pad;};
struct Stream {void*buffer;uint32_t offset,stride;uint16_t geometryFlags;uint8_t managed,dynamicLock;};
struct Header {uint32_t serial,meshes;void*indices;uint32_t primitive;Stream stream[2];int useOffsets;void*declaration;uint32_t totalIndex,totalVertex;};
struct Mesh {uint32_t numIndex,minVertex;Material*material;int vertexAlpha;void*shader;uint32_t baseIndex,vertices,startIndex,primitives;};
static_assert(sizeof(Descriptor)==4&&sizeof(Dispatch)==8&&sizeof(Header)==64&&sizeof(Mesh)==36&&sizeof(Material)==28);
struct Api {
    void(__cdecl*begin)(void*,uint32_t,Descriptor*)=nullptr;
    const M*(__cdecl*world)(void*)=nullptr;
    void(__cdecl*activeWorld)(const M*)=nullptr;
    void(__cdecl*composed)(Vector4*)=nullptr;
    Vector4*(__cdecl*lighting)(void*,uint32_t,Vector4*,Descriptor*)=nullptr;
    void(__cdecl*constants)(uint32_t,const void*,uint32_t)=nullptr;
    void(__cdecl*state)(uint32_t,uint32_t)=nullptr;
    void(__cdecl*indices)(void*)=nullptr;
    void(__cdecl*streams)(Stream*,int)=nullptr;
    void(__cdecl*declaration)(void*)=nullptr;
    int(__cdecl*alpha)(int)=nullptr;
    void*(__cdecl*shader)(const Material*,Descriptor*,Dispatch*)=nullptr;
    uint32_t(__cdecl*numConstants)(const Descriptor*)=nullptr;
    void(__cdecl*lightColours)(Vector4*,const Descriptor*,float,float)=nullptr;
    void(__cdecl*materialColour)(const uint32_t*,const Dispatch*)=nullptr;
    void(__cdecl*fog)(const Descriptor*,const Dispatch*)=nullptr;
    void(__cdecl*mesh)(Header*,Mesh*,const Descriptor*,const Dispatch*)=nullptr;
    void(__cdecl*end)(void*,uint32_t,Descriptor*)=nullptr;
    bool complete()const{return begin&&world&&activeWorld&&composed&&lighting&&constants&&state&&indices&&streams&&declaration&&alpha&&shader&&numConstants&&lightColours&&materialColour&&fog&&mesh&&end;}
};
inline Api pinnedApi(){return{
    reinterpret_cast<decltype(Api::begin)>(0x7CB190),reinterpret_cast<decltype(Api::world)>(0x7F0990),
    reinterpret_cast<decltype(Api::activeWorld)>(0x764650),reinterpret_cast<decltype(Api::composed)>(0x7646E0),
    reinterpret_cast<decltype(Api::lighting)>(0x761170),reinterpret_cast<decltype(Api::constants)>(0x7FACA0),
    reinterpret_cast<decltype(Api::state)>(0x7FC2D0),reinterpret_cast<decltype(Api::indices)>(0x7FA1C0),
    reinterpret_cast<decltype(Api::streams)>(0x7FA090),reinterpret_cast<decltype(Api::declaration)>(0x7F9F70),
    reinterpret_cast<decltype(Api::alpha)>(0x7FE0A0),reinterpret_cast<decltype(Api::shader)>(0x761010),
    reinterpret_cast<decltype(Api::numConstants)>(0x75EDD0),reinterpret_cast<decltype(Api::lightColours)>(0x761720),
    reinterpret_cast<decltype(Api::materialColour)>(0x761820),reinterpret_cast<decltype(Api::fog)>(0x7618B0),
    reinterpret_cast<decltype(Api::mesh)>(0x761030),reinterpret_cast<decltype(Api::end)>(0x761000)};}
struct Budget {uint32_t originalMeshBones=0,maxConstants=0;};
// Same source priority: spot, point, directional, then fog. Do not grant extra
// lights merely because CPU skinning no longer needs GPU bone constants.
inline bool reduce(Descriptor&d){
    if(d.spotsTex&15){--d.spotsTex;return true;}
    if(d.lights&0xf0){d.lights-=0x10;return true;}
    if(d.lights&15){--d.lights;return true;}
    if(d.fogEffect&3){d.fogEffect&=0xfc;return true;}
    return false;
}
struct Result {bool okay=false;unsigned drawn=0,lightReductions=0;const char*failure="not_started";};
inline Result render(const Api&api,Atomic*atomic,Header*header,Mesh*meshes,Budget budget){
    Result result;const auto fail=[&](const char*s){result.failure=s;return result;};
    if(!api.complete()||!atomic||!atomic->geometry||!header||!meshes||!header->meshes||header->meshes>65535||!header->declaration||!budget.maxConstants||budget.maxConstants>256||budget.originalMeshBones>128)return fail("invalid_render_inputs");
    for(unsigned i=0;i<header->meshes;i++){
        const auto*m=meshes[i].material;
        if(!m||m->pipeline||!std::isfinite(m->ambient)||!std::isfinite(m->diffuse))return fail("unverified_material");
    }
    void*frame=nullptr;std::memcpy(&frame,atomic->frameObject+4,sizeof frame);if(!frame)return fail("missing_frame");
    Descriptor desc{};api.begin(atomic,1,&desc);
    struct DrawScope {const Api&api;Atomic*atomic;Descriptor&descriptor;Mesh*meshes;unsigned count;bool ended=false;
        ~DrawScope(){for(unsigned i=0;i<count;i++)meshes[i].shader=nullptr;if(!ended)api.end(atomic,1,&descriptor);}
    }scope{api,atomic,desc,meshes,header->meshes};
    // Source7CB190 already sets normals+normalizeNormals together. Keep every
    // other bit, including prelight/modulate/fog. Zero applies ONLY to the GPU
    // blend-count field: CPU deformation has already consumed all8 influences.
    desc.vertex&=uint8_t(~0x0e);
    api.state(0x88,1);
    const auto*world=api.world(frame);if(!world||!expandedSkin::affineFinite(*world))return fail("invalid_atomic_world");api.activeWorld(world);
    std::array<Vector4,256>constants{};api.composed(constants.data());
    Vector4*end=api.lighting(atomic,1,constants.data()+4,&desc);
    const uintptr_t first=reinterpret_cast<uintptr_t>(constants.data()),last=reinterpret_cast<uintptr_t>(end);
    if(last<first+64||last>first+sizeof(constants)||(last-first)%16)return fail("invalid_lighting_constant_span");
    api.constants(0,constants.data(),uint32_t((last-first)/16));
    if(header->indices)api.indices(header->indices);api.streams(header->stream,header->useOffsets);api.declaration(header->declaration);
    float oldAmbient=1,oldDiffuse=1;uint8_t oldMaterialOffset=255,oldFogOffset=255;uint32_t oldColour=meshes[0].material->colour;
    for(unsigned i=0;i<header->meshes;i++){
        auto&mesh=meshes[i];const auto&material=*mesh.material;
        api.alpha((material.colour>>24)!=255||mesh.vertexAlpha);
        Dispatch dispatch{};mesh.shader=api.shader(&material,&desc,&dispatch);
        if(!mesh.shader)return fail("native_shader_unavailable");
        if(uint64_t(api.numConstants(&desc))+uint64_t(budget.originalMeshBones)*3>budget.maxConstants){
            do{if(!reduce(desc)){mesh.shader=nullptr;return fail("original_constant_budget_unsatisfied");}++result.lightReductions;}
            while(uint64_t(api.numConstants(&desc))+uint64_t(budget.originalMeshBones)*3>budget.maxConstants);
            mesh.shader=api.shader(&material,&desc,&dispatch);oldAmbient=1-material.ambient;
            if(!mesh.shader)return fail("native_reduced_shader_unavailable");
        }
        if(oldAmbient!=material.ambient||oldDiffuse!=material.diffuse){
            // Same accumulated colour update as7C82D8..7C830C. Zero previous
            // coefficients are not silently substituted; refuse instead.
            if(oldAmbient==0||oldDiffuse==0){mesh.shader=nullptr;return fail("zero_native_surface_ratio");}
            api.lightColours(constants.data()+4,&desc,material.ambient/oldAmbient,material.diffuse/oldDiffuse);
            oldAmbient=material.ambient;oldDiffuse=material.diffuse;
        }
        if(oldMaterialOffset!=dispatch.material||oldColour!=material.colour){oldMaterialOffset=dispatch.material;oldColour=material.colour;api.materialColour(&material.colour,&dispatch);}
        if(oldFogOffset!=dispatch.fog){oldFogOffset=dispatch.fog;api.fog(&desc,&dispatch);}
        // No bone upload or palette access: these streams have no skin data.
        api.mesh(header,&mesh,&desc,&dispatch);mesh.shader=nullptr;++result.drawn;
    }
    api.end(atomic,1,&desc);scope.ended=true;result.okay=true;result.failure="none";return result;
}
} // namespace expandedGpu
