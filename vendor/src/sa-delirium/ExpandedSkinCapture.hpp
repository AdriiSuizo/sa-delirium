#pragma once
// Read-only, one-shot input for the experimental render investigation. This
// header installs no hooks, advances no animation and changes no RW resources.
// Capture the actual assembled atomic; clothing source DFF indices are NOT the
// indices of CJ's assembled 32-node hierarchy.
#include "ExpandedSkinRenderHost.hpp"
#include <string>
#include <fstream>
#include "ExpandedSkinJsonBuffer.hpp"

namespace expandedSkinCapture {
using expandedSkinHost::Atomic;using expandedSkinHost::Geometry;
struct Node {int id,index,flags;void*frame;};
struct Hierarchy {int flags,count;M*matrices;void*unaligned;Node*nodes;void*parentFrame,*parentHierarchy;int rootParentOffset;void*anim;};
struct Frame {unsigned char object[8],dirtyLink[8];M local,world;unsigned char objectList[8];Frame*child,*next,*root;};
struct Material {void*texture;uint32_t colour;void*pipeline;float ambient,specular,diffuse;int16_t refs,pad;};
struct PipelineNode {void*definition;uint32_t outputs;void*output,*clusters,*slots,*privateData,*input,*sort,*init;uint32_t initSize;};
struct Pipeline {int locked;uint32_t count;PipelineNode*nodes;uint32_t slots,packetState;void*packet;uint32_t inputs;void*input,*block;uint32_t blockSize,entry,plugin,data;};
static_assert(sizeof(Node)==16&&sizeof(Hierarchy)==36&&offsetof(Frame,world)==0x50&&sizeof(Material)==28);
static_assert(sizeof(PipelineNode)==40&&sizeof(Pipeline)==52);
using Read=bool(*)(void*,const void*,size_t);
struct Api {
    Read read=nullptr;
    void*(__cdecl*skin)(Geometry*)=nullptr;
    uint32_t(__cdecl*bones)(void*)=nullptr;
    const float*(__cdecl*weights)(void*)=nullptr;
    const uint32_t*(__cdecl*indices)(void*)=nullptr;
    const M*(__cdecl*inverseBind)(void*)=nullptr;
    Hierarchy*(__cdecl*hierarchy)(Atomic*)=nullptr;
    int(__cdecl*effects)(const void*)=nullptr;
    bool complete()const{return read&&skin&&bones&&weights&&indices&&inverseBind&&hierarchy&&effects;}
};
inline bool readSelf(void*dst,const void*src,size_t bytes){
    SIZE_T copied=0;return bytes&&src&&dst&&ReadProcessMemory(GetCurrentProcess(),src,dst,bytes,&copied)&&copied==bytes;
}
inline Api pinnedApi(){return {readSelf,
    reinterpret_cast<decltype(Api::skin)>(0x7C7550),reinterpret_cast<decltype(Api::bones)>(0x7C77E0),
    reinterpret_cast<decltype(Api::weights)>(0x7C77F0),reinterpret_cast<decltype(Api::indices)>(0x7C7800),
    reinterpret_cast<decltype(Api::inverseBind)>(0x7C7810),reinterpret_cast<decltype(Api::hierarchy)>(0x7C7540),
    reinterpret_cast<decltype(Api::effects)>(0x812140)};}

struct PipelineObservation {
    uintptr_t address=0;uint32_t plugin=0,data=0,entry=0;bool readable=false;
    struct Stage {uintptr_t definition=0,body=0,privateData=0;std::string name;
        unsigned privateExpectedWords=0;bool privateReadable=false;std::vector<uint32_t>privateWords;};
    std::vector<Stage> stages;
};
inline PipelineObservation observePipeline(const Api&api,void*ptr){
    PipelineObservation out;out.address=reinterpret_cast<uintptr_t>(ptr);if(!ptr)return out;
    Pipeline p{};if(!api.read(&p,ptr,sizeof p)||p.count>32||!p.count||!p.nodes||p.entry>=p.count)return out;
    std::vector<PipelineNode> nodes(p.count);if(!api.read(nodes.data(),p.nodes,nodes.size()*sizeof nodes[0]))return out;
    out.readable=true;out.plugin=p.plugin;out.data=p.data;out.entry=p.entry;
    for(const auto&node:nodes){
        PipelineObservation::Stage stage;stage.definition=reinterpret_cast<uintptr_t>(node.definition);stage.privateData=reinterpret_cast<uintptr_t>(node.privateData);
        void*prefix[2]{};if(api.read(prefix,node.definition,sizeof prefix)){
            stage.body=reinterpret_cast<uintptr_t>(prefix[1]);
            for(unsigned i=0;i<96;i++){char c=0;if(!api.read(&c,static_cast<const char*>(prefix[0])+i,1)||!c)break;stage.name+=c;}
        }
        // Bounds are the actual native node-private structures: skin init
        // 7CB240 writes seven callbacks; default init 757890 writes four.
        // Unknown/replaced node bodies are observed but never guessed/read.
        if(stage.body==0x7C7B90)stage.privateExpectedWords=7;
        else if(stage.body==0x7575F0)stage.privateExpectedWords=4;
        if(stage.privateExpectedWords&&node.privateData){
            stage.privateWords.resize(stage.privateExpectedWords);
            stage.privateReadable=api.read(stage.privateWords.data(),node.privateData,stage.privateWords.size()*sizeof(uint32_t));
            if(!stage.privateReadable)stage.privateWords.clear();
        }
        out.stages.push_back(std::move(stage));
    }
    return out;
}
struct Token {
    int ped=-1,model=-1;unsigned frame=0;uint64_t nativeSerial=0,generation=0;
    void*clump=nullptr;Atomic*atomic=nullptr;
};
struct FrameObservation {uintptr_t address=0;Frame frame{};};
// RwFrameGetLTM7F0990 calls SyncHierarchyLTM8097D0 only when the root's
// HIERARCHYSYNCLTM bit is set. Sync recomputes only the dirty subtree, carrying
// bit4 down the ancestry. A clean ancestor uses its stored world matrix; blindly
// multiplying every modelling matrix would not reproduce that contract.
inline bool deriveFrameWorld(const Api&api,Frame*leaf,M&world,std::vector<FrameObservation>&observations){
    observations.clear();if(!leaf)return false;Frame current{};
    if(!api.read(&current,leaf,sizeof current)||!current.root)return false;
    const Frame*root=current.root;Frame*ptr=leaf;
    for(unsigned depth=0;depth<128;depth++){
        if(std::any_of(observations.begin(),observations.end(),[&](const auto&o){return o.address==reinterpret_cast<uintptr_t>(ptr);}))return false;
        if(!api.read(&current,ptr,sizeof current)||current.root!=root||!expandedSkin::affineFinite(current.local)||!expandedSkin::affineFinite(current.world))return false;
        observations.push_back({reinterpret_cast<uintptr_t>(ptr),current});
        Frame*parent=nullptr;std::memcpy(&parent,current.object+4,4);
        if(ptr==root){if(parent)return false;break;}
        if(!parent||depth==127)return false;ptr=parent;
    }
    if(observations.empty()||observations.back().address!=reinterpret_cast<uintptr_t>(root))return false;
    const auto&rootFrame=observations.back().frame;
    if(!(rootFrame.object[3]&1)){world=observations.front().frame.world;return true;}
    unsigned inherited=rootFrame.object[3];world=(inherited&4)?rootFrame.local:rootFrame.world;
    for(size_t i=observations.size()-1;i>0;i--){const auto&f=observations[i-1].frame;inherited|=f.object[3];world=(inherited&4)?affineProduct(world,f.local):f.world;}
    return expandedSkin::affineFinite(world);
}
struct Snapshot {
    Token token;uintptr_t geometry=0,skin=0,hierarchy=0,renderCallback=0;uint32_t geometryFlags=0;
    int hierarchyFlags=0;uint32_t framePrivateFlags=0;bool frameWorldVerified=false;
    M atomicWorld{},derivedWorld{};bool derivedWorldVerified=false;std::vector<FrameObservation> frameChain;
    std::vector<Node> nodes;std::vector<M> inverseBind,palette;
    std::vector<expandedSkin::Vertex> source;std::vector<std::array<expandedSkin::Weight,4>> weights;
    std::vector<expandedSkinHost::Triangle> triangles;std::vector<uint32_t> prelight;
    std::vector<std::vector<float>> uvs;std::vector<Material> materials;std::vector<int> materialEffects;
    PipelineObservation sourcePipeline,defaultPipeline;
    bool skinHardwareReadable=false;uint32_t skinHardware=0;
    bool normaliseGlobalReadable=false,hardwareGlobalReadable=false;uint32_t normaliseGlobal=0,hardwareGlobal=0;
    std::string failure;
};
template<class T> inline bool copyArray(const Api&api,const T*source,size_t count,std::vector<T>&out){
    if(!source||!count||count>1000000)return false;std::vector<T> copy(count);
    if(!api.read(copy.data(),source,sizeof(T)*count))return false;out.swap(copy);return true;
}
// The supplied default pipeline is read from the verified runtime engine
// globals by the host, not assumed to be the appropriate CPU render pipeline.
// No snapshot result grants DrawContract approval.
inline bool capture(const Api&api,const Token&token,unsigned currentFrame,void*defaultPipeline,Snapshot&out){
    Snapshot staged;staged.token=token;const auto reject=[&](const char*why){out={};out.token=token;out.failure=why;return false;};
    if(!api.complete()||token.ped<0||!token.generation||!token.nativeSerial||token.frame!=currentFrame||!token.atomic||!token.clump)return reject("invalid_capture_epoch_or_identity");
    Atomic atomic{};if(!api.read(&atomic,token.atomic,sizeof atomic)||atomic.clump!=token.clump||!atomic.geometry)return reject("atomic_identity_changed");
    Geometry geometry{};if(!api.read(&geometry,atomic.geometry,sizeof geometry)||geometry.verticesCount<1||geometry.verticesCount>65535||geometry.trianglesCount<1||geometry.trianglesCount>200000||geometry.morphsCount!=1||geometry.uvCount<0||geometry.uvCount>8||geometry.materials.count<1||geometry.materials.count>1024)return reject("unsupported_geometry");
    auto*skin=api.skin(atomic.geometry);auto*hierarchy=api.hierarchy(token.atomic);Hierarchy h{};
    if(!skin||!hierarchy||!api.read(&h,hierarchy,sizeof h)||h.count<1||h.count>128||!h.nodes||!h.matrices)return reject("missing_actual_skin_hierarchy");
    const unsigned bones=api.bones(skin);if(bones!=unsigned(h.count))return reject("skin_hierarchy_count_mismatch");
    expandedSkinHost::Morph morph{};if(!api.read(&morph,geometry.morph,sizeof morph)||!morph.vertices||!morph.normals)return reject("source_positions_or_normals_missing");
    std::vector<V> positions,normals;std::vector<float> rawWeights;std::vector<uint32_t> rawIndices;
    if(!copyArray(api,morph.vertices,geometry.verticesCount,positions)||!copyArray(api,morph.normals,geometry.verticesCount,normals)||
       !copyArray(api,api.weights(skin),size_t(geometry.verticesCount)*4,rawWeights)||!copyArray(api,api.indices(skin),geometry.verticesCount,rawIndices)||
       !copyArray(api,api.inverseBind(skin),bones,staged.inverseBind)||!copyArray(api,h.matrices,bones,staged.palette)||!copyArray(api,h.nodes,bones,staged.nodes)||
       !copyArray(api,geometry.triangles,geometry.trianglesCount,staged.triangles))return reject("unreadable_actual_skin_data");
    for(unsigned i=0;i<bones;i++)if(!expandedSkin::affineFinite(staged.inverseBind[i])||!expandedSkin::affineFinite(staged.palette[i]))return reject("nonfinite_actual_matrix");
    for(int i=0;i<geometry.verticesCount;i++){
        if(!finite(positions[i])||!finite(normals[i]))return reject("nonfinite_source_vertex");
        std::array<expandedSkin::Weight,4>w{};float sum=0;
        for(unsigned j=0;j<4;j++){w[j]={uint16_t((rawIndices[i]>>(j*8))&255),rawWeights[i*4+j]};
            if(!std::isfinite(w[j].value)||w[j].value<0||(w[j].value>0&&w[j].bone>=bones))return reject("invalid_actual_skin_weight");sum+=w[j].value;}
        if(!(sum>0)||!std::isfinite(sum))return reject("zero_or_nonfinite_source_weight");
        staged.source.push_back({positions[i],normals[i]});staged.weights.push_back(w);
    }
    if(geometry.flags&8)if(!copyArray(api,geometry.prelight,geometry.verticesCount,staged.prelight))return reject("unreadable_prelight");
    for(int i=0;i<geometry.uvCount;i++){std::vector<float>uv;if(!copyArray(api,static_cast<float*>(geometry.uv[i]),size_t(geometry.verticesCount)*2,uv))return reject("unreadable_uv");for(float f:uv)if(!std::isfinite(f))return reject("nonfinite_uv");staged.uvs.push_back(std::move(uv));}
    std::vector<void*>materials;if(!copyArray(api,geometry.materials.items,geometry.materials.count,materials))return reject("unreadable_material_table");
    for(auto*ptr:materials){Material m{};if(!api.read(&m,ptr,sizeof m))return reject("unreadable_material");if(!std::isfinite(m.ambient)||!std::isfinite(m.specular)||!std::isfinite(m.diffuse))return reject("nonfinite_surface_property");staged.materials.push_back(m);staged.materialEffects.push_back(api.effects(ptr));}
    for(const auto&t:staged.triangles){if(t.material>=staged.materials.size())return reject("invalid_material_index");for(unsigned v:t.vertex)if(v>=staged.source.size())return reject("invalid_triangle_index");}
    // Read existing LTM only. RwFrameGetLTM is deliberately not called: it may
    // update a dirty hierarchy and would hide an incorrect capture phase.
    Frame*frame=nullptr;std::memcpy(&frame,atomic.frameObject+4,sizeof frame);Frame f{};
    if(!frame||!api.read(&f,frame,sizeof f)||!expandedSkin::affineFinite(f.world))return reject("unreadable_atomic_frame");
    staged.atomicWorld=f.world;staged.framePrivateFlags=f.object[3];
    Frame root{};if(f.root&&api.read(&root,f.root,sizeof root))staged.frameWorldVerified=!(f.object[3]&5)&&!(root.object[3]&1);
    staged.derivedWorldVerified=deriveFrameWorld(api,frame,staged.derivedWorld,staged.frameChain);
    staged.geometry=reinterpret_cast<uintptr_t>(atomic.geometry);staged.skin=reinterpret_cast<uintptr_t>(skin);staged.hierarchy=reinterpret_cast<uintptr_t>(hierarchy);
    staged.geometryFlags=geometry.flags;staged.hierarchyFlags=h.flags;staged.renderCallback=reinterpret_cast<uintptr_t>(atomic.renderCallback);
    staged.skinHardwareReadable=api.read(&staged.skinHardware,reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(skin)+0x20),4);
    staged.sourcePipeline=observePipeline(api,atomic.pipeline);staged.defaultPipeline=observePipeline(api,defaultPipeline);
    Atomic after{};if(!api.read(&after,token.atomic,sizeof after)||after.geometry!=atomic.geometry||after.clump!=atomic.clump||api.skin(after.geometry)!=skin||api.hierarchy(token.atomic)!=hierarchy)return reject("identity_changed_during_capture");
    out=std::move(staged);return true;
}

template<class Stream>inline void writeVector(Stream&o,V p){o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
template<class Stream>inline void writeMatrix(Stream&o,const M&m){o<<'[';writeVector(o,m.right);o<<',';writeVector(o,m.up);o<<',';writeVector(o,m.at);o<<',';writeVector(o,m.pos);o<<']';}
template<class Stream>inline void writeString(Stream&o,const std::string&s){
    o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<char(c);else if(c<32)o<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else o<<char(c);}o<<'"';
}
template<class Stream>inline void writePipeline(Stream&o,const PipelineObservation&p){
    o<<"{\"address\":"<<p.address<<",\"readable\":"<<(p.readable?"true":"false")<<",\"plugin\":"<<p.plugin<<",\"data\":"<<p.data<<",\"entry\":"<<p.entry<<",\"nodes\":[";
    for(size_t i=0;i<p.stages.size();i++){if(i)o<<',';const auto&s=p.stages[i];o<<"{\"name\":";writeString(o,s.name);o<<",\"definition\":"<<s.definition<<",\"body\":"<<s.body<<",\"private\":"<<s.privateData<<",\"private_expected_words\":"<<s.privateExpectedWords<<",\"private_readable\":"<<(s.privateReadable?"true":"false")<<",\"private_words\":[";
        for(size_t j=0;j<s.privateWords.size();j++){if(j)o<<',';o<<s.privateWords[j];}o<<"]}";}o<<"]}";
}
inline bool write(const Snapshot&s,const std::wstring&path){
    if(!s.failure.empty()||s.source.empty())return false;
    JsonBuffer o(s.source.size()*160+s.triangles.size()*28+32768);
    o<<"{\"schema\":\"expanded_skin_capture_v1\",\"render_enabled\":false,\"ped\":"<<s.token.ped<<",\"model\":"<<s.token.model<<",\"frame\":"<<s.token.frame<<",\"native_serial\":"<<s.token.nativeSerial<<",\"generation\":"<<s.token.generation<<",\"atomic\":"<<reinterpret_cast<uintptr_t>(s.token.atomic)<<",\"clump\":"<<reinterpret_cast<uintptr_t>(s.token.clump)<<",\"geometry\":"<<s.geometry<<",\"skin\":"<<s.skin<<",\"hierarchy\":"<<s.hierarchy<<",\"geometry_flags\":"<<s.geometryFlags<<",\"hierarchy_flags\":"<<s.hierarchyFlags<<",\"render_callback\":"<<s.renderCallback<<",\"frame_private_flags\":"<<s.framePrivateFlags<<",\"frame_world_verified\":"<<(s.frameWorldVerified?"true":"false")<<",\"atomic_world\":";writeMatrix(o,s.atomicWorld);
    o<<",\"derived_world_verified\":"<<(s.derivedWorldVerified?"true":"false")<<",\"derived_world\":";writeMatrix(o,s.derivedWorld);
    o<<",\"atomic_matrix_flags\":"<<s.atomicWorld.flags<<",\"frame_chain_leaf_to_root\":[";
    for(size_t i=0;i<s.frameChain.size();i++){if(i)o<<',';const auto&f=s.frameChain[i];o<<"{\"address\":"<<f.address<<",\"private_flags\":"<<unsigned(f.frame.object[3])<<",\"local_flags\":"<<f.frame.local.flags<<",\"world_flags\":"<<f.frame.world.flags<<",\"local\":";writeMatrix(o,f.frame.local);o<<",\"world\":";writeMatrix(o,f.frame.world);o<<'}';}o<<']';
    o<<",\"pipeline\":";writePipeline(o,s.sourcePipeline);o<<",\"default_pipeline\":";writePipeline(o,s.defaultPipeline);
    o<<",\"skin_runtime_globals\":{\"skin_hardware_readable\":"<<(s.skinHardwareReadable?"true":"false")<<",\"skin_hardware\":"<<s.skinHardware<<",\"normalise_readable\":"<<(s.normaliseGlobalReadable?"true":"false")<<",\"normalise_value\":"<<s.normaliseGlobal<<",\"hardware_readable\":"<<(s.hardwareGlobalReadable?"true":"false")<<",\"hardware_value\":"<<s.hardwareGlobal<<'}';
    o<<",\"nodes\":[";for(size_t i=0;i<s.nodes.size();i++){if(i)o<<',';auto&n=s.nodes[i];o<<'['<<n.id<<','<<n.index<<','<<n.flags<<']';}
    o<<"],\"inverse_bind\":[";for(size_t i=0;i<s.inverseBind.size();i++){if(i)o<<',';writeMatrix(o,s.inverseBind[i]);}
    o<<"],\"inverse_bind_flags\":[";for(size_t i=0;i<s.inverseBind.size();i++){if(i)o<<',';o<<s.inverseBind[i].flags;}
    o<<"],\"palette\":[";for(size_t i=0;i<s.palette.size();i++){if(i)o<<',';writeMatrix(o,s.palette[i]);}
    o<<"],\"palette_flags\":[";for(size_t i=0;i<s.palette.size();i++){if(i)o<<',';o<<s.palette[i].flags;}
    o<<"],\"vertices\":[";for(size_t i=0;i<s.source.size();i++){if(i)o<<',';o<<'[';writeVector(o,s.source[i].position);o<<',';writeVector(o,s.source[i].normal);o<<",[";for(unsigned j=0;j<4;j++){if(j)o<<',';o<<'['<<s.weights[i][j].bone<<','<<s.weights[i][j].value<<']';}o<<"]]";}
    o<<"],\"triangles\":[";for(size_t i=0;i<s.triangles.size();i++){if(i)o<<',';auto&t=s.triangles[i];o<<'['<<t.vertex[0]<<','<<t.vertex[1]<<','<<t.vertex[2]<<','<<t.material<<']';}
    o<<"],\"prelight\":[";for(size_t i=0;i<s.prelight.size();i++){if(i)o<<',';o<<s.prelight[i];}o<<"],\"uvs\":[";for(size_t i=0;i<s.uvs.size();i++){if(i)o<<',';o<<'[';for(size_t j=0;j<s.uvs[i].size();j++){if(j)o<<',';o<<s.uvs[i][j];}o<<']';}
    o<<"],\"materials\":[";for(size_t i=0;i<s.materials.size();i++){if(i)o<<',';auto&m=s.materials[i];o<<"{\"texture\":"<<reinterpret_cast<uintptr_t>(m.texture)<<",\"colour\":"<<m.colour<<",\"pipeline\":"<<reinterpret_cast<uintptr_t>(m.pipeline)<<",\"surface\":["<<m.ambient<<','<<m.specular<<','<<m.diffuse<<"],\"matfx\":"<<s.materialEffects[i]<<'}';}o<<"]}\n";
    if(!o.good())return false;std::ofstream file(path,std::ios::binary);if(!file)return false;
    file.write(o.bytes().data(),static_cast<std::streamsize>(o.bytes().size()));file.flush();return bool(file);
}
} // namespace expandedSkinCapture
