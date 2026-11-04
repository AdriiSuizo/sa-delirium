#pragma once
// Adapter seam only. This header does not include/install itself in Adapter.
// Existing Rig supplies the fitted 21-body anatomy, not another hard-coded rig.
#include "ExpandedPoseBridge.hpp"
#include "SABones.hpp"

namespace expandedSkin {
inline bool nativeAuthoredHelper(int tag){return saBone::face(tag)||tag==25||tag==26||tag==35||tag==36||tag==44||tag==54||tag>=200;}
template<class RigType>bool prepareFromRig(const RigType&rig,const M*sourceInverseBind,size_t count,PoseBridge&bridge){
    if(!sourceInverseBind||count!=rig.ids.size()||rig.parents.size()!=count||rig.bind.size()!=count||rig.bodyToBone.size()!=count||rig.partForBone.size()!=count)return false;
    const int pelvis=rig.index(saBone::pelvis);if(pelvis<0||size_t(pelvis)>=count)return false;
    // Match Rig's existing rigid bind construction exactly. The original
    // affine inverse-bind matrices remain separate and unchanged for skinning.
    const T rawPelvis=inverse(transform(sourceInverseBind[pelvis]));
    const M anatomyToMesh=rigidMatrix(mul(rawPelvis,inverse(rig.bind[pelvis])));
    const auto joints=rig.jointFrames(rig.bind);std::vector<NativeJointBinding>native;native.reserve(count);
    for(size_t i=0;i<count;i++)native.push_back({rig.ids[i],rig.parents[i],rig.partForBone[i],nativeAuthoredHelper(rig.ids[i]),rig.bind[i],rig.bodyToBone[i]});
    return bridge.prepare(native,sourceInverseBind,{joints[8],joints[10]},
                          {inverse(rig.offsets[8]),inverse(rig.offsets[10])},anatomyToMesh);
}
struct FrameOutput {
    ExpandedPose pose;
    std::vector<M>nativeWorld,skinPalette;
    std::vector<Vertex>vertices;
    Bounds bounds{};
};
// All consumers (render stream, original-node HAnim subset and attachments)
// come from this one output. A failed batch publishes none of them.
inline bool prepareFrame(const PoseBridge&bridge,const Binding&binding,const M*authoredWorld,
                         const std::array<T,21>&physical,float blend,bool upperOnly,
                         const M&atomicWorld,FrameOutput&out){
    FrameOutput staged;
    if(!bridge.pose(authoredWorld,physical,blend,upperOnly,staged.pose)||
       !bridge.skinPalette(staged.pose,atomicWorld,staged.skinPalette))return false;
    staged.vertices.resize(binding.vertices());
    if(!binding.deform(staged.skinPalette.data(),staged.skinPalette.size(),staged.vertices.data(),staged.vertices.size(),staged.bounds))return false;
    staged.nativeWorld.assign(staged.pose.world.begin(),staged.pose.world.begin()+bridge.nativeCount());
    out=std::move(staged);return true;
}
} // namespace expandedSkin
