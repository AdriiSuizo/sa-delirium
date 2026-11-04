#pragma once
// Experimental, pure pose/skin bridge. No RW pointers, hooks or native writes.
// Keep the two missing torso joints and their clavicle branches in a separate
// topology. The original HAnim receives only its existing-node subset.
#include "CpuExpandedSkin.hpp"
#include <functional>

namespace expandedSkin {
inline M rigidMatrix(T t){M m{};write(m,t);return m;}
inline bool rigidFinite(T t){return finite(t.p)&&std::isfinite(t.q.x)&&std::isfinite(t.q.y)&&std::isfinite(t.q.z)&&std::isfinite(t.q.w)&&
    std::abs(t.q.x*t.q.x+t.q.y*t.q.y+t.q.z*t.q.z+t.q.w*t.q.w-1.f)<.002f;}
struct NativeJointBinding {
    int tag=-1,parent=-1,part=-1;bool authoredHelper=false;
    T anatomyRest{},bodyToBone{};
};
struct ExpandedPose {
    std::vector<M> world; // native nodes, then physical joints8 and10
    float mappedOriginResidual=0; // constraint residual is observed, not hidden
};
// Preserve the complete native affine residual (including shear), rather than
// extracting three scales or normalizing all normals/vertices by assumption.
inline bool blendAffineLocal(const M&authored,T physical,float weight,M&out){
    if(!affineFinite(authored)||!rigidFinite(physical)||!std::isfinite(weight)||weight<0||weight>1)return false;
    if(weight==0){out=authored;return true;}
    const T rigid=transform(authored);
    M residual=affineProduct(rigidMatrix(inverse(T{rigid.q,{}})),authored);residual.pos={};
    M result=affineProduct(rigidMatrix(T{mix(rigid.q,physical.q,weight),{}}),residual);
    result.pos=authored.pos*(1-weight)+physical.p*weight;result.flags=0;
    if(!affineFinite(result))return false;out=result;return true;
}
class PoseBridge {
    std::vector<NativeJointBinding> native_;
    std::vector<int> parents_,order_;
    std::vector<T> rest_,restLocal_,bodyToBone_;
    std::vector<M> inverseBind_,bindMesh_;
    std::array<int,2> sourceParent_{};
    bool valid_=false;
public:
    size_t nativeCount()const{return native_.size();}
    size_t count()const{return rest_.size();}
    const std::vector<int>&parents()const{return parents_;}
    const std::vector<M>&inverseBind()const{return inverseBind_;}
    const std::vector<M>&bindMesh()const{return bindMesh_;}
    bool prepare(const std::vector<NativeJointBinding>&native,const M*sourceInverseBind,
                 const std::array<T,2>&virtualRest,const std::array<T,2>&virtualBodyToJoint,
                 const M&anatomyToMesh){
        valid_=false;if(native.size()<20||native.size()>128||!sourceInverseBind||!affineFinite(anatomyToMesh))return false;
        const auto index=[&](int tag){int found=-1;for(size_t i=0;i<native.size();i++)if(native[i].tag==tag){if(found>=0)return -1;found=int(i);}return found;};
        const int spine=index(2),chest=index(3),neck=index(4),left=index(31),right=index(21);
        if(spine<0||chest<0||neck<0||left<0||right<0||native[chest].parent!=spine||native[neck].parent!=chest||native[left].parent!=neck||native[right].parent!=neck)return false;
        std::vector<int> parents;std::vector<T>rest,body;std::vector<M>inv,mesh;
        for(size_t i=0;i<native.size();i++){
            const auto&n=native[i];M b{};
            if(n.parent>=int(native.size())||n.parent==int(i)||n.parent< -1||n.part<0||n.part>=21||
               !rigidFinite(n.anatomyRest)||!rigidFinite(n.bodyToBone)||!affineInverse(sourceInverseBind[i],b)||!affineFinite(b))return false;
            parents.push_back(n.parent);rest.push_back(n.anatomyRest);body.push_back(n.bodyToBone);inv.push_back(sourceInverseBind[i]);mesh.push_back(b);
        }
        for(size_t i=0;i<2;i++){
            if(!rigidFinite(virtualRest[i])||!rigidFinite(virtualBodyToJoint[i]))return false;
            const M b=affineProduct(anatomyToMesh,rigidMatrix(virtualRest[i]));M inverse{};
            if(!affineInverse(b,inverse)||!affineFinite(b))return false;
            parents.push_back(i?chest:spine);rest.push_back(virtualRest[i]);body.push_back(virtualBodyToJoint[i]);inv.push_back(inverse);mesh.push_back(b);
        }
        const int lower=int(native.size()),upper=lower+1;
        parents[chest]=lower;parents[neck]=upper;parents[left]=parents[right]=upper;
        // The inserted joints are appended, so native storage order is no
        // longer topological. Reject cycles instead of using stale parents.
        std::vector<int>order;std::vector<unsigned char>state(rest.size());
        std::function<bool(int)>visit=[&](int i){if(state[i]==1)return false;if(state[i]==2)return true;state[i]=1;if(parents[i]>=0&&!visit(parents[i]))return false;state[i]=2;order.push_back(i);return true;};
        for(size_t i=0;i<rest.size();i++)if(!visit(int(i)))return false;
        std::vector<T>locals;for(size_t i=0;i<rest.size();i++)locals.push_back(parents[i]<0?rest[i]:mul(inverse(rest[parents[i]]),rest[i]));
        native_=native;parents_.swap(parents);rest_.swap(rest);restLocal_.swap(locals);bodyToBone_.swap(body);inverseBind_.swap(inv);bindMesh_.swap(mesh);order_.swap(order);sourceParent_={spine,chest};valid_=true;return true;
    }
    bool authoredExpanded(const M*authored,std::vector<M>&out)const{
        if(!valid_||!authored)return false;std::vector<M>staged(count());
        for(size_t i=0;i<native_.size();i++){if(!affineFinite(authored[i]))return false;staged[i]=authored[i];}
        for(size_t i=0;i<2;i++){const size_t v=native_.size()+i;const int p=sourceParent_[i];if(!inheritVirtual(authored[p],inverseBind_[p],bindMesh_[v],staged[v]))return false;}
        out.swap(staged);return true;
    }
    bool pose(const M*authored,const std::array<T,21>&parts,float weight,bool upperOnly,ExpandedPose&out)const{
        if(!valid_||!std::isfinite(weight)||weight<0||weight>1)return false;
        std::vector<M>animation;if(!authoredExpanded(authored,animation))return false;
        if(weight==0){out={std::move(animation),0};return true;}
        for(const auto&t:parts)if(!rigidFinite(t))return false;
        std::vector<T>desired(count());
        for(size_t i=0;i<count();i++){const int part=i<native_.size()?native_[i].part:(i==native_.size()?8:10);desired[i]=mul(parts[part],bodyToBone_[i]);}
        ExpandedPose staged;staged.world.resize(count());
        for(int i:order_){
            const int parent=parents_[i];M animationLocal=animation[i];
            if(parent>=0){M inverseParent{};if(!affineInverse(animation[parent],inverseParent))return false;animationLocal=affineProduct(inverseParent,animation[i]);}
            T physicalLocal=desired[i];if(parent>=0){physicalLocal.q=mul(conjugate(desired[parent].q),desired[i].q);physicalLocal.p=restLocal_[i].p;}
            const bool authoredOnly=i<int(native_.size())&&(native_[i].authoredHelper||(upperOnly&&native_[i].part<=6));
            M local{};if(!blendAffineLocal(animationLocal,physicalLocal,authoredOnly?0:weight,local))return false;
            staged.world[i]=parent<0?local:affineProduct(staged.world[parent],local);staged.world[i].flags=0;
            if(!affineFinite(staged.world[i]))return false;
            if(!authoredOnly&&weight==1)staged.mappedOriginResidual=std::max(staged.mappedOriginResidual,length(staged.world[i].pos-desired[i].p));
        }
        out=std::move(staged);return true;
    }
    bool skinPalette(const ExpandedPose&pose,const M&atomicWorld,std::vector<M>&out)const{
        M worldInverse{};if(!valid_||pose.world.size()!=count()||!affineInverse(atomicWorld,worldInverse))return false;
        std::vector<M>staged(count());
        for(size_t i=0;i<count();i++){staged[i]=affineProduct(worldInverse,affineProduct(pose.world[i],inverseBind_[i]));staged[i].flags=0;if(!affineFinite(staged[i]))return false;}
        out.swap(staged);return true;
    }
    // Explicit experimental bind-space longitudinal distribution. Original
    // weights are conserved exactly and all positive contributions survive.
    // Acceptance of this skin authoring policy still requires the live views.
    bool expandWeights(const Vertex*source,const std::array<Weight,4>*original,size_t vertices,std::vector<Influences>&out)const{
        if(!valid_||!source||!original||!vertices)return false;std::vector<Influences>staged(vertices);
        for(size_t i=0;i<vertices;i++){
            if(!finite(source[i].position))return false;std::array<Split,4>splits;
            for(size_t slot=0;slot<4;slot++){
                const auto bone=original[i][slot].bone;if(bone>=native_.size()&&original[i][slot].value>0)return false;
                splits[slot]={bone,bone,0};
                for(size_t k=0;k<2;k++)if(bone==sourceParent_[k]){
                    const size_t v=native_.size()+k;const V start=bindMesh_[bone].pos,axis=bindMesh_[v].pos-start;const float square=dot(axis,axis);
                    if(!(square>1.e-10f))return false;splits[slot]={bone,uint16_t(v),std::clamp(dot(source[i].position-start,axis)/square,0.f,1.f)};
                }
            }
            if(!expand(original[i],splits,count(),staged[i]))return false;
        }
        out.swap(staged);return true;
    }
};
} // namespace expandedSkin
