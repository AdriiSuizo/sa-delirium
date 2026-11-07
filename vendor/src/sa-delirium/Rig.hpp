#pragma once
#include "ABI.hpp"
#include "ReactionPolicy.hpp"
#include "RestSkeleton.hpp"
#include "SABones.hpp"
#include "SpineRetargetMath.hpp"
#include "delirium_physics.h"
#include <array>
#include <vector>
#include <stdexcept>
#include <string>
inline void* __cdecl findSkin(void* atomic,void* arg){
    if(*static_cast<void**>(arg))return atomic;
    auto* geometry=at<void*>(atomic,0x18);
    if(geometry)*static_cast<void**>(arg)=reinterpret_cast<void*(__cdecl*)(void*)>(0x7C7550)(geometry);
    return atomic;
}
// Skinned geometry and its RpSkin (first skinned atomic of the clump).
struct SkinGeometrySearch{void*geometry=nullptr,*skin=nullptr;};
inline void* pedSkin(void* ped){void* skin=nullptr;
    if(void* clump=at<void*>(ped,0x18))reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(clump,&findSkin,&skin);
    return skin;}
inline void* __cdecl findSkinGeometry(void* atomic,void* arg){
    auto&s=*static_cast<SkinGeometrySearch*>(arg);if(s.skin)return atomic;
    if(auto*geometry=at<void*>(atomic,0x18))if(void*skin=reinterpret_cast<void*(__cdecl*)(void*)>(0x7C7550)(geometry)){s.geometry=geometry;s.skin=skin;}
    return atomic;
}
// Smallest circle enclosing 2D points (Welzl, iterative, deterministic order).
inline void enclosingCircle(std::vector<std::array<float,2>> pts,float&cx,float&cy,float&r){
    unsigned seed=12345u;for(size_t i=pts.size();i>1;i--){seed=seed*1664525u+1013904223u;std::swap(pts[i-1],pts[seed%i]);}
    auto inside=[&](const std::array<float,2>&p){return std::hypot(p[0]-cx,p[1]-cy)<=r*1.00001f+1e-6f;};
    auto two=[&](const std::array<float,2>&a,const std::array<float,2>&b){cx=(a[0]+b[0])*.5f;cy=(a[1]+b[1])*.5f;r=std::hypot(a[0]-cx,a[1]-cy);};
    auto three=[&](const std::array<float,2>&a,const std::array<float,2>&b,const std::array<float,2>&c){
        const float bx=b[0]-a[0],by=b[1]-a[1],qx=c[0]-a[0],qy=c[1]-a[1],d=2*(bx*qy-by*qx);
        if(std::abs(d)<1e-12f){two(a,b);if(!inside(c)){two(a,c);if(!inside(b))two(b,c);}return;}
        const float ux=(qy*(bx*bx+by*by)-by*(qx*qx+qy*qy))/d,uy=(bx*(qx*qx+qy*qy)-qx*(bx*bx+by*by))/d;
        cx=a[0]+ux;cy=a[1]+uy;r=std::hypot(ux,uy);};
    cx=cy=r=0;if(pts.empty())return;cx=pts[0][0];cy=pts[0][1];
    for(size_t i=1;i<pts.size();i++)if(!inside(pts[i])){
        cx=pts[i][0];cy=pts[i][1];r=0;
        for(size_t j=0;j<i;j++)if(!inside(pts[j])){
            two(pts[i],pts[j]);
            for(size_t k=0;k<j;k++)if(!inside(pts[k]))three(pts[i],pts[j],pts[k]);
        }
    }
}
struct Rig {
    NmBuild* build=nullptr;
    std::vector<int> ids,partForBone,parents;
    std::vector<T> bind;
    // The neutral skeleton the bodies are fitted to: bind, or for a model exported
    // bent at the waist its own bones in the straight rest pose (restFrames).
    std::vector<T> anatomy;bool straightened=false;
    std::vector<V> restLocal;
    void* skinSource=nullptr; // the RpSkin this rig was fitted from
    std::vector<T> bodyToBone,bindLocal;
    std::array<int,21> node{};
    std::array<T,21> offsets{};
    std::array<float,21> partRadius{},partHalf{};
    std::array<float,63> fitPoints{};
    std::array<float,4> spineFractions{};
    T headOffset{};
    float rootClearance=1.f;
    // Per reshaped part: 0 kept, 1 fitted to this skin, <0 refused (reason).
    std::array<int,21> skinShape{};std::array<int,21> skinVertices{};
    V rootToPelvis{};
    ~Rig(){if(build)nm_build_destroy(build);}
    static int mapped(int id){
        if(id<=1)return 0;if(id==2)return 7;if(id==3)return 9;if(id==4)return 11;if(id>=5&&id<=8)return 12;
        if(id==41)return 1;if(id==42)return 2;if(id==43||id==44)return 3;
        if(id==51)return 4;if(id==52)return 5;if(id==53||id==54)return 6;
        if(id==31)return 13;if(id==32)return 14;if(id==33)return 15;if(id>=34&&id<=36)return 16;
        if(id==21)return 17;if(id==22)return 18;if(id==23)return 19;if(id>=24&&id<=26)return 20;
        return -1;
    }
    int index(int id)const{for(size_t n=0;n<ids.size();n++)if(ids[n]==id)return int(n);return -1;}
    // Animated bones of a straightened model, with their joints where the bodies
    // were fitted: same rotations, the rest directions instead of the bent ones.
    std::array<T,21> jointFrames(const std::vector<T>& animated)const{
        std::vector<T> straight;
        if(straightened&&&animated!=&anatomy){straight=animated;for(size_t i=0;i<straight.size();i++)if(parents[i]>=0&&ids[i]!=saBone::pelvis)straight[i].p=straight[parents[i]].p+rotate(straight[parents[i]].q,restLocal[i]);}
        const auto& bones=straight.empty()?animated:straight;
        std::array<T,21> j;
        for(int i=0;i<21;i++)if(node[i]>=0)j[i]=bones[node[i]];
        // Preserve SA's actual two joints and flexion. The former straight
        // pelvis-to-neck replacement discarded both torso rotations and opened
        // the neck/clavicle anchors before the first physics step.
        const float lower=(spineFractions[1]-spineFractions[0])/(spineFractions[2]-spineFractions[0]);
        const float upper=(spineFractions[3]-spineFractions[2])/(1.f-spineFractions[2]);
        const auto torso=nativeTorsoJoints(bones[index(saBone::spine)],bones[index(saBone::chest)],j[11],lower,upper);
        for(int i=0;i<4;i++)j[7+i]=torso[i];
        j[12]=mul(j[12],headOffset);
        return j;
    }
    static bool restPoseDisabled(){static const bool off=GetEnvironmentVariableA("GTASA_REST_POSE_OFF",nullptr,0)>0;return off;} // A/B only
    // Clean skeletons measure about 0.4 degrees here and keep their own bind.
    bool bentAtWaist()const{
        const V torso=normalized(bind[index(saBone::chest)].p-bind[node[0]].p);
        const V legs=normalized((bind[node[1]].p+bind[node[4]].p-bind[node[3]].p-bind[node[6]].p));
        return dot(torso,legs)<.9962f;
    }
    // SA skin mesh space is X-up. Fit in an upright, right-handed NM frame.
    // Offsets remain joint-local, so incoming world matrices need no extra rotation.
    T bodyBasis(const std::vector<T>&b,int top)const{
        const V up=normalized(b[index(top)].p-b[node[0]].p);
        V right=b[node[4]].p-b[node[1]].p;
        right=normalized(right-up*dot(up,right));
        const V forward=normalized(cross(up,right));
        if(length(up)<.9f||length(right)<.9f||length(forward)<.9f)throw std::runtime_error("degenerate skin body basis");
        M basis{};basis.right=right;basis.up=forward;basis.at=up;
        return inverse(transform(basis));
    }
    Rig(void* ped,NmBuild* original){
        auto*h=hierarchy(ped);if(!h||h->num<20||h->num>128||!h->matrices||!h->nodes)throw std::runtime_error("unsupported HAnim hierarchy");
        void* skin=nullptr;
        reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(at<void*>(ped,0x18),&findSkin,&skin);
        if(!skin||reinterpret_cast<unsigned(__cdecl*)(void*)>(0x7C77E0)(skin)<unsigned(h->num))throw std::runtime_error("missing skin bind matrices");
        skinSource=skin;
        const auto* inverseBind=reinterpret_cast<const M*(__cdecl*)(void*)>(0x7C7810)(skin);
        if(!inverseBind)throw std::runtime_error("no inverse bind data");
        for(int i=0;i<h->num;i++){
            ids.push_back(h->nodes[i].id);T b=inverse(transform(inverseBind[i]));
            if(!finite(b.p)||length(b.p)>10)throw std::runtime_error("invalid skin bind transform");bind.push_back(b);
        }
        // RpHAnim's verified traversal: PUSH saves the previous parent, then the
        // current node becomes parent, and POP restores the saved parent.
        // Preserve this actual skin hierarchy rather than guessing bone parents.
        int hierarchyParent=-1;std::vector<int> stack{-1};
        for(int i=0;i<h->num;i++){
            parents.push_back(hierarchyParent);
            if(h->nodes[i].flags&2)stack.push_back(hierarchyParent);
            hierarchyParent=i;
            if(h->nodes[i].flags&1){if(stack.empty())throw std::runtime_error("invalid HAnim parent stack");hierarchyParent=stack.back();stack.pop_back();}
        }
        constexpr int boneIDs[21]={saBone::pelvis,41,42,43,51,52,53,saBone::spine,-1,saBone::chest,-1,saBone::neck,saBone::head,31,32,33,34,21,22,23,24};
        for(int i=0;i<21;i++){node[i]=boneIDs[i]<0?-1:index(boneIDs[i]);if(boneIDs[i]>=0&&node[i]<0)throw std::runtime_error("required SA bone missing "+std::to_string(boneIDs[i]));}
        anatomy=bind;restLocal.resize(bind.size());straightened=bentAtWaist()&&!restPoseDisabled();
        if(straightened)for(size_t i=0;i<bind.size();i++)if(parents[i]>=0){
            T local=mul(inverse(bind[parents[i]]),bind[i]);
            for(const auto&r:restFrames)if(r.id==ids[i]){
                M m{};m.right={r.m[0],r.m[1],r.m[2]};m.up={r.m[3],r.m[4],r.m[5]};m.at={r.m[6],r.m[7],r.m[8]};local.q=rotation(m);
                const V along{r.t[0],r.t[1],r.t[2]};local.p=length(along)<.01f?along:normalized(along)*length(local.p);
            }
            restLocal[i]=local.p;anatomy[i]=mul(anatomy[parents[i]],local);
        }
        const T canonical=bodyBasis(bind,saBone::chest),neutral=straightened?bodyBasis(anatomy,saBone::neck):canonical;
        for(auto& b:bind)b=mul(canonical,b);
        for(auto& b:anatomy)b=mul(neutral,b);
        V zero{};auto* reference=nm_agent_create(original,&zero.x,0,.3f);
        if(!reference)throw std::runtime_error("reference anatomy unavailable");
        V root,neck,head;Q q;int parent;float limits[3];
        nm_agent_get_part_transform(reference,0,&root.x,&q.x);
        nm_agent_get_joint(reference,11,&parent,&neck.x,&q.x,limits,nullptr);
        nm_agent_get_joint(reference,12,&parent,&head.x,&q.x,limits,nullptr);
        const float torso=neck.z-root.z;
        if(torso<.1f){nm_agent_destroy(reference);throw std::runtime_error("invalid reference torso");}
        for(int i=0;i<4;i++){V pivot;nm_agent_get_joint(reference,7+i,&parent,&pivot.x,&q.x,limits,nullptr);spineFractions[i]=(pivot.z-root.z)/torso;}
        nm_agent_destroy(reference);
        // Fit the physical head pivot relative to SA's actual neck/head. Keep
        // a virtual physical head joint at the reference neck/head spacing, attached
        // rigidly to the real SA head bone, to avoid a near-zero neck inertia.
        T headJoint=anatomy[node[12]];
        headJoint.p.z=anatomy[node[11]].p.z+(head.z-neck.z)*(anatomy[node[11]].p.z-anatomy[node[0]].p.z)/torso;
        headOffset=mul(inverse(anatomy[node[12]]),headJoint);
        auto joints=jointFrames(anatomy);
        for(int i=0;i<21;i++){fitPoints[3*i]=joints[i].p.x;fitPoints[3*i+1]=joints[i].p.y;fitPoints[3*i+2]=joints[i].p.z;}
        build=nm_build_fit(original,fitPoints.data());if(!build)throw std::runtime_error("nm_build_fit rejected skeleton");
        fitSkinShapes(ped,canonical);
        for(int i=0;i<21;i++){
            T body;nm_build_part_frame(build,i,&body.p.x,&body.q.x);offsets[i]=mul(inverse(joints[i]),body);
            nm_build_part_info(build,i,nullptr,&partRadius[i],&partHalf[i],nullptr);
            if(i==0)rootToPelvis=body.p;
        }
        for(int id:ids){int part=mapped(id);if(part<0){part=0;float best=1.e9f;const V p=anatomy[index(id)].p;for(int i=0;i<21;i++){float d=length(p-joints[i].p);if(d<best){best=d;part=i;}}}partForBone.push_back(part);}
        for(size_t i=0;i<ids.size();i++){
            T body;nm_build_part_frame(build,partForBone[i],&body.p.x,&body.q.x);
            bodyToBone.push_back(mul(inverse(body),anatomy[i]));
            bindLocal.push_back(parents[i]<0?bind[i]:mul(inverse(bind[parents[i]]),bind[i]));
        }
        rootClearance=-std::min(anatomy[node[3]].p.z,anatomy[node[6]].p.z)+.12f;
        rootClearance=std::clamp(rootClearance,.65f,1.5f);
    }
    // The fitted NM shapes are GTA IV's, scaled by the skeleton: a leaf part
    // (head, hand) inherits its parent's scale and nothing measures the SA
    // skin, so HD heads/hands were drawn 4-16 cm outside their shapes and
    // sank into the ground while the shapes rested on it. Adrii chose (2026-11-02)
    // to make what touches what is drawn: size the head and forearm capsules
    // and hand boxes to cover this model's own bind skin. Axes stay those of
    // the fitted bodies; masses/inertia stay GTA IV's (nm_build_reshape_part).
    static bool skinShapesDisabled(){static const bool off=GetEnvironmentVariableA("GTASA_SKIN_SHAPES_OFF",nullptr,0)>0;return off;} // A/B only
    void fitSkinShapes(void*ped,const T&canonical){
        if(skinShapesDisabled())return;
        SkinGeometrySearch search;
        reinterpret_cast<void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*)>(0x749B70)(at<void*>(ped,0x18),&findSkinGeometry,&search);
        if(!search.skin||!search.geometry){for(int k:{12,15,16,19,20})skinShape[k]=-1;return;}
        const int count=at<int>(search.geometry,0x14);void*morph=at<void*>(search.geometry,0x5C);
        const V*vertices=morph?at<V*>(morph,0x14):nullptr;
        const float*weights=reinterpret_cast<const float*(__cdecl*)(void*)>(0x7C77F0)(search.skin);
        const uint32_t*indices=reinterpret_cast<const uint32_t*(__cdecl*)(void*)>(0x7C7800)(search.skin);
        if(count<1||count>65535||!vertices||!weights||!indices){for(int k:{12,15,16,19,20})skinShape[k]=-2;return;}
        std::array<std::vector<V>,21> local;std::array<T,21> body;
        for(int k:{12,15,16,19,20}){nm_build_part_frame(build,k,&body[k].p.x,&body[k].q.x);}
        for(int i=0;i<count;i++){
            int dominant=-1;float best=0;
            for(int k=0;k<4;k++){const float w=weights[4*i+k];const int b=(indices[i]>>(8*k))&255;if(w>best&&b<int(ids.size())){best=w;dominant=b;}}
            if(dominant<0||!finite(vertices[i]))continue;
            const int part=mapped(ids[dominant]);
            if(part!=12&&part!=15&&part!=16&&part!=19&&part!=20)continue;
            V c=canonical.p+rotate(canonical.q,vertices[i]);
            if(straightened){const T shift=mul(anatomy[dominant],inverse(bind[dominant]));c=shift.p+rotate(shift.q,c);}
            local[part].push_back(rotate(conjugate(body[part].q),c-body[part].p));
        }
        for(int k:{12,15,16,19,20}){
            const auto&pts=local[k];skinVertices[k]=int(pts.size());
            if(pts.size()<20){skinShape[k]=-3;continue;}
            float box[3];int result=-4;
            if(nm_build_part_box(build,k,box)==1){
                V lo=pts[0],hi=pts[0];for(const V&v:pts){lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
                const V centre=(lo+hi)*.5f,half=(hi-lo)*.5f;
                result=nm_build_reshape_part(build,k,&centre.x,0,0,&half.x);
            }else{
                // Capsule axis is body-local Y: smallest circle in X/Z, then the
                // shortest segment along Y that keeps every vertex inside.
                std::vector<std::array<float,2>> flat;flat.reserve(pts.size());for(const V&v:pts)flat.push_back({v.x,v.z});
                float cx,cz,r;enclosingCircle(flat,cx,cz,r);
                float top=-INFINITY,bottom=INFINITY;
                for(const V&v:pts){const float d=std::hypot(v.x-cx,v.z-cz),slack=std::sqrt(std::max(r*r-d*d,0.f));top=std::max(top,v.y-slack);bottom=std::min(bottom,v.y+slack);}
                const V centre{cx,(top+bottom)*.5f,cz};
                result=nm_build_reshape_part(build,k,&centre.x,r,std::max(0.f,(top-bottom)*.5f),nullptr);
            }
            skinShape[k]=result==0?1:-4;
        }
    }
    std::vector<T> physicalBones(const std::array<T,21>& parts)const{
        std::vector<T> out(ids.size());
        // Drive rotations, then reconstruct the original skeletal segments.
        // Independent body-centre translations used to shorten the skinned torso
        // and limbs when the constraint solver compressed its physical joints.
        // NM bodies 8 and 10 (spine1/spine3) have no SA bone: SA tag 2 spans
        // NM 7+8 and tag 3 spans 9+10 (nativeTorsoJoints). Copying only the
        // rotation of 7/9 discarded the 8/10 flexion, so the neck, head,
        // clavicles and arms were drawn off the physical bodies (head and
        // hands under the road while their shapes rested on it). Swing each
        // SA torso bone so its child pivot lands on the physical joint pivot
        // (NM joint 9, NM joint 11); twist stays that of the mapped body.
        const int spineBone=index(saBone::spine),chestBone=index(saBone::chest),neckBone=index(saBone::neck);
        for(size_t i=0;i<ids.size();i++){
            const T desired=mul(parts[partForBone[i]],bodyToBone[i]);
            out[i].q=desired.q;
            out[i].p=parents[i]<0?desired.p:out[parents[i]].p+rotate(out[parents[i]].q,bindLocal[i].p);
            const int child=int(i)==spineBone?chestBone:int(i)==chestBone?neckBone:-1;
            if(child<0||parents[child]!=int(i)||spineAimDisabled())continue;
            const int joint=int(i)==spineBone?9:11;
            const V pivot=mul(parts[joint],inverse(offsets[joint])).p;
            out[i].q=mul(swingBetween(rotate(out[i].q,bindLocal[child].p),pivot-out[i].p),out[i].q);
        }
        // IV draws half of the hand's roll on a forearm roll bone (gta4-data
        // neckroll.rs:60-61, bones 0x1B/0x1E: forearm roll + 0.5 x hand roll).
        // SA has none, so catchFall's full wrist twist (IV target 1.0 rad,
        // nmcf.rs:383, held at the 0.9 limit) read as a broken wrist. Roll the
        // forearm by half the hand's twist about the forearm axis; the hand keeps
        // its physical orientation and position.
        if(!wristRollDisabled())for(const auto[fore,hand]:{std::pair{33,34},std::pair{23,24}}){
            const int f=index(fore),h=index(hand);if(f<0||h<0||parents[h]!=f)continue;
            const V axis=normalized(bindLocal[h].p);
            const Q r=mul(mul(conjugate(out[f].q),out[h].q),conjugate(bindLocal[h].q));
            const float along=r.x*axis.x+r.y*axis.y+r.z*axis.z;
            const Q twist=unit(Q{axis.x*along,axis.y*along,axis.z*along,r.w});
            out[f].q=mul(out[f].q,mix(Q{},twist,.5f));
            // The wrist's swing (bend) is drawn at most 30 deg from SA's bind
            // relation: IV's +-0.9 rad soft wrist hangs the hand 60-90 deg
            // sideways under gravity in Balance/catchFall, read as broken
            // (Adrii, 06/11 captures). Fingers follow the drawn hand.
            const Q local=mul(conjugate(out[f].q),out[h].q),rel=mul(local,conjugate(bindLocal[h].q));
            const float along2=rel.x*axis.x+rel.y*axis.y+rel.z*axis.z;
            const Q relTwist=unit(Q{axis.x*along2,axis.y*along2,axis.z*along2,rel.w}),swing=mul(rel,conjugate(relTwist));
            const float angle=2.f*std::acos(std::clamp(std::abs(swing.w),0.f,1.f)),limit=30.f*3.14159265f/180.f;
            if(angle>limit){
                const Q kept=mix(Q{},swing,limit/angle);
                const Q drawn=mul(out[f].q,mul(mul(kept,relTwist),bindLocal[h].q)),delta=mul(drawn,conjugate(out[h].q));
                for(size_t i=size_t(h);i<ids.size();i++){
                    int up=int(i);while(up>=0&&up!=h)up=parents[up];if(up!=h)continue;
                    out[i].q=mul(delta,out[i].q);
                    if(int(i)!=h)out[i].p=out[parents[i]].p+rotate(out[parents[i]].q,bindLocal[i].p);
                }
            }
        }
        return out;
    }
    static bool wristRollDisabled(){static const bool off=GetEnvironmentVariableA("GTASA_RENDER_WRIST_ROLL_OFF",nullptr,0)>0;return off;}
    // A/B diagnostics only: GTASA_RENDER_SPINE_AIM_OFF=1 restores the old write-back.
    static bool spineAimDisabled(){static const bool off=GetEnvironmentVariableA("GTASA_RENDER_SPINE_AIM_OFF",nullptr,0)>0;return off;}
    static Q swingBetween(V from,V to){
        const float lf=length(from),lt=length(to);if(lf<1.e-5f||lt<1.e-5f)return {0,0,0,1};
        const V c=cross(from,to);const float w=lf*lt+dot(from,to);
        if(w<1.e-6f*lf*lt)return {0,0,0,1}; // opposite: no defined swing; keep body rotation
        return unit(Q{c.x,c.y,c.z,w});
    }
    float relativeTurnDegrees(const std::vector<T>& pose,int parentID,int childID)const{
        const int p=index(parentID),c=index(childID);
        if(p<0||c<0||pose.size()!=bind.size())return 180.f;
        const Q rest=mul(conjugate(bind[p].q),bind[c].q);
        const Q now=mul(conjugate(pose[p].q),pose[c].q);
        const Q delta=mul(conjugate(rest),now);
        return 2.f*std::acos(std::clamp(std::abs(delta.w),0.f,1.f))*57.2957795f;
    }
    std::vector<M> renderPalette(const M*native,const std::vector<T>&pose)const{
        std::vector<M> out(ids.size());
        for(size_t i=0;i<ids.size();i++){
            out[i]=native[i];write(out[i],pose[i]);
            // Animation matrices can include facial/clothing scale. Normalizing
            // every axis to a rigid rotation erased that information on CJ.
            out[i].right=out[i].right*length(native[i].right);
            out[i].up=out[i].up*length(native[i].up);
            out[i].at=out[i].at*length(native[i].at);
            M parentInverse{};
            if(articulatedHelper(ids[i])&&parents[i]>=0&&affineInverse(native[parents[i]],parentInverse))
                out[i]=affineProduct(out[parents[i]],affineProduct(parentInverse,native[i]));
        }
        return out;
    }
    std::vector<T> blendBones(const std::vector<T>& native,const std::vector<T>& physical,float weight,bool upperOnly=false)const{
        std::vector<T> out(ids.size());
        for(size_t i=0;i<ids.size();i++){
            const int parent=parents[i];
            const float w=articulatedHelper(ids[i])||(upperOnly&&partForBone[i]<=6)?0.f:weight;
            if(parent<0){out[i]=mix(native[i],physical[i],w);continue;}
            T localNative=mul(inverse(native[parent]),native[i]);
            const T localPhysics=mul(inverse(physical[parent]),physical[i]);
            // Blend local rotations, not world positions: a 180-degree turn must
            // not shrink the midpoint of a limb towards its parent's centre.
            const V localPosition=localNative.p*(1-w)+bindLocal[i].p*w;
            out[i]=mul(out[parent],T{mix(localNative.q,localPhysics.q,w),localPosition});
        }return out;
    }
    bool incoming(void* ped,std::array<T,21>& parts,std::vector<T>& bones)const{
        auto*h=hierarchy(ped);if(!h||h->num!=int(ids.size())||!h->matrices||!h->nodes)return false;
        bones.clear();for(int i=0;i<h->num;i++){if(h->nodes[i].id!=ids[i])return false;auto t=transform(h->matrices[i]);if(!finite(t.p)||length(t.p-position(ped))>6)return false;bones.push_back(t);}
        // Offscreen riders can expose a finite but unevaluated palette: every
        // bone equals the ped root. Never seed a physical body from that pose.
        for(size_t i=0;i<ids.size();i++)if(parents[i]>=0){
            const float expected=length(bindLocal[i].p),actual=length(bones[i].p-bones[parents[i]].p);
            if(expected>.03f&&(actual<expected*.35f||actual>expected*2.f))return false;
        }
        auto j=jointFrames(bones);for(int i=0;i<21;i++)parts[i]=mul(j[i],offsets[i]);return true;
    }
    bool evaluatedIncoming(void* ped,std::array<T,21>& parts,std::vector<T>& bones,bool& refreshed)const{
        refreshed=false;if(incoming(ped,parts,bones))return true;
        void*clump=at<void*>(ped,0x18);if(!clump)return false;
        // All entry paths can see an offscreen/newly created unevaluated pose.
        // Evaluate at the current animation time; do not advance gameplay time.
        reinterpret_cast<void(__cdecl*)(void*,float,bool)>(0x4D34F0)(clump,0.f,true);
        auto&flags=at<unsigned char>(ped,0x1E);const auto previous=flags;flags&=~128u;
        reinterpret_cast<void(__thiscall*)(void*)>(0x532B20)(ped);flags=previous;
        refreshed=incoming(ped,parts,bones);return refreshed;
    }
};
