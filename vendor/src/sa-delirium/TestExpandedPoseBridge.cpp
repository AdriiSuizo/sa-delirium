#include "ExpandedPoseBridge.hpp"
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace expandedSkin;
static unsigned checks=0;static void check(bool b,const char*s){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
template<class T>T read(std::ifstream&f){T x{};f.read(reinterpret_cast<char*>(&x),sizeof x);if(!f)std::exit(2);return x;}
static M matrix(std::ifstream&f){M m{};m.right=read<V>(f);m.up=read<V>(f);m.at=read<V>(f);m.pos=read<V>(f);return m;}
static float matrixError(const M&a,const M&b){return std::max({length(a.right-b.right),length(a.up-b.up),length(a.at-b.at),length(a.pos-b.pos)});}
int main(int argc,char**argv){
    if(argc!=2)return 2;std::ifstream f(argv[1],std::ios::binary);check(read<uint32_t>(f)==0x45504231,"fixture identity");
    const auto models=read<uint32_t>(f);float maxNative=0,maxPhysical=0,maxSkin=0;unsigned extraWeights=0;
    for(unsigned model=0;model<models;model++){
        const auto n=read<uint32_t>(f),nv=read<uint32_t>(f),nc=read<uint32_t>(f);
        std::vector<NativeJointBinding>nodes(n);std::vector<M>inverseBind(n),authored(n);
        for(unsigned i=0;i<n;i++){auto&b=nodes[i];b.tag=read<int32_t>(f);b.parent=read<int32_t>(f);b.part=read<int32_t>(f);b.authoredHelper=read<int32_t>(f)!=0;b.anatomyRest=transform(matrix(f));b.bodyToBone=transform(matrix(f));inverseBind[i]=matrix(f);authored[i]=matrix(f);}
        const M toMesh=matrix(f),world=matrix(f);std::array<T,2>virtualRest;for(auto&t:virtualRest)t=transform(matrix(f));
        PoseBridge bridge;check(bridge.prepare(nodes,inverseBind.data(),virtualRest,{T{},T{}},toMesh),"runtime assembled hierarchy accepted with two expanded joints");
        std::vector<Vertex>source(nv);std::vector<std::array<Weight,4>>weights(nv);
        for(unsigned i=0;i<nv;i++){source[i]=read<Vertex>(f);for(auto&w:weights[i]){w.bone=uint16_t(read<uint32_t>(f));w.value=read<float>(f);}}
        std::vector<Influences>expanded;check(bridge.expandWeights(source.data(),weights.data(),nv,expanded),"expanded weights compiled from actual assembled index map");
        for(unsigned i=0;i<nv;i++){float a=0,b=0;for(auto&w:weights[i])a+=w.value;for(unsigned j=0;j<expanded[i].count;j++)b+=expanded[i].weights[j].value;check(std::abs(a-b)<3.e-7f,"all original influence mass conserved");if(expanded[i].count>4)extraWeights++;}
        std::array<T,21>unused{};ExpandedPose native;check(bridge.pose(authored.data(),unused,0,false,native),"native mode publishes exact original world subset");
        for(unsigned i=0;i<n;i++){maxNative=std::max(maxNative,matrixError(native.world[i],authored[i]));check(!std::memcmp(&native.world[i],&authored[i],sizeof(M)),"zero-physics node affine bits unchanged");}
        std::vector<M>skin;check(bridge.skinPalette(native,world,skin),"native palette converted with actual atomic transform");M iw{};check(affineInverse(world,iw),"world invertible");
        std::vector<M>oldPalette(n);for(unsigned i=0;i<n;i++)oldPalette[i]=affineProduct(iw,affineProduct(authored[i],inverseBind[i]));
        for(unsigned i=0;i<nv;i++){Influences old{};for(auto&w:weights[i])if(w.value>0)old.weights[old.count++]=w;Vertex a{},b{};check(vertex(source[i],old,oldPalette.data(),n,a)&&vertex(source[i],expanded[i],skin.data(),skin.size(),b),"native source and expanded deformation valid");maxSkin=std::max(maxSkin,std::max(length(a.position-b.position),length(a.normal-b.normal)));}
        for(unsigned c=0;c<nc;c++){
            std::vector<M>seed(n),expected(n);std::array<T,21>parts;for(auto&m:seed)m=matrix(f);for(auto&t:parts)t=transform(matrix(f));for(auto&m:expected)m=matrix(f);
            ExpandedPose result;check(bridge.pose(seed.data(),parts,1,false,result),"full-body expanded pose succeeds");
            for(unsigned i=0;i<n;i++)maxPhysical=std::max(maxPhysical,matrixError(result.world[i],expected[i]));
            ExpandedPose partial;check(bridge.pose(seed.data(),parts,.5f,false,partial),"continuous native/physics pose blend is finite");
            for(unsigned i=0;i<n;i++)if(nodes[i].authoredHelper&&nodes[i].parent>=0){M before{},after{};check(affineInverse(seed[nodes[i].parent],before)&&affineInverse(partial.world[nodes[i].parent],after),"helper parent inverse");before=affineProduct(before,seed[i]);after=affineProduct(after,partial.world[i]);check(matrixError(before,after)<.001f,"face/finger affine local preserved during blend");}
        }
        auto corrupt=nodes;corrupt[0].parent=0;PoseBridge invalid;check(!invalid.prepare(corrupt,inverseBind.data(),virtualRest,{T{},T{}},toMesh),"malformed parent topology cannot publish");
        ExpandedPose untouched;untouched.mappedOriginResidual=123;check(!bridge.pose(authored.data(),unused,2,false,untouched)&&untouched.mappedOriginResidual==123,"invalid blend rejected atomically");
    }
    check(maxPhysical<.0015f,"expanded anchors/rotations agree with independent full21joint oracle");
    check(maxSkin<.002f,"native8influence skin agrees with original4weight affine path");
    check(extraWeights>0,"installed vertices exercise more than four influences");
    std::printf("{\"checks\":%u,\"models\":%u,\"max_native_matrix_error\":%.9g,\"max_physical_matrix_error\":%.9g,\"max_native_skin_error\":%.9g,\"vertices_above_four_weights\":%u,\"render_enabled\":false}\n",checks,models,maxNative,maxPhysical,maxSkin,extraWeights);
}
