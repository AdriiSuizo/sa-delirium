#pragma once
#include "ABI.hpp"
#include <cstring>
#include <vector>

// Later skin updates may reevaluate a gun pose after PreRender. Rebase
// only the two leg subtrees on the live pelvis used by the weapon IK.
struct VisibleLegPose {
    struct Bone {int index;M world;};
    std::vector<Bone> bones;
    M pelvis{},inversePelvis{};
    int anchor=-1;
    bool capture(const Hierarchy& h){
        bones.clear();anchor=-1;
        if(!h.nodes||!h.matrices||h.num<1||h.num>128)return false;
        bool stack[129]{},leg=false;int depth=1;
        for(int i=0;i<h.num;++i){
            const auto& n=h.nodes[i];
            if(n.flags&2)stack[depth++]=leg;
            leg=leg||n.id==41||n.id==51;
            if(n.id==1)anchor=i;
            if(leg)bones.push_back({i,h.matrices[i]});
            if(n.flags&1){if(!depth)return false;leg=stack[--depth];}
        }
        if(anchor<0||bones.empty())return false;
        pelvis=h.matrices[anchor];return affineInverse(pelvis,inversePelvis);
    }
    bool publish(M* pose)const {
        const bool unchanged=std::memcmp(&pose[anchor],&pelvis,sizeof(M))==0;
        if(unchanged){
            bool replaced=false;
            for(const auto& b:bones)if(std::memcmp(&pose[b.index],&b.world,sizeof(M))){
                pose[b.index]=b.world;replaced=true;
            }
            return replaced;
        }
        const M delta=affineProduct(pose[anchor],inversePelvis);
        for(const auto& b:bones)pose[b.index]=affineProduct(delta,b.world);
        return true;
    }
};
