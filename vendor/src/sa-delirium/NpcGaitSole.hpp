#pragma once
#include "NpcGaitData.hpp"
#include <limits>

namespace npc_gait {
// Sample the lower outline of the actual shoe at different pitches and rolls.
// The native and IV palettes then skin the exact same selected vertices.
struct Footwear {
    struct Vertex { std::array<V,4> local{}; std::array<uint8_t,4> joints{}; std::array<float,4> weights{}; };
    std::vector<Vertex> outline;
    void fit(const Binding& b,const V* vertices,const float* weights,const uint32_t* indices,int count,const M* inverseBind){
        outline.clear();
        if(!vertices||!weights||!indices||!inverseBind||count<1||count>65535)return;
        auto joint=[&](int id,V& pos){
            for(int i=0;i<b.count;++i)if(b.ids[i]==id){
                M rest;if(!affineInverse(inverseBind[i],rest))return false;
                pos=rest.pos;return finite(pos);
            }
            return false;
        };
        V ankle,toe,otherAnkle,head;
        if(!joint(43,ankle)||!joint(44,toe)||!joint(53,otherAnkle)||!joint(5,head))return;
        const V up=normalized(head-(ankle+otherAnkle)*.5f);
        const V side=normalized(cross(toe-ankle,up));
        const V forward=normalized(cross(up,side));
        if(length(forward)<.95f)return;
        std::array<V,15> directions;
        int nd=0;
        for(float pitch:{-1.6f,-.7f,0.f,.7f,1.6f})
            for(float roll:{-.5f,0.f,.5f})directions[nd++]=up+forward*pitch+side*roll;
        std::array<std::array<float,15>,4> low;
        std::array<std::array<int,15>,4> which;
        for(auto& row:low)row.fill(std::numeric_limits<float>::infinity());
        for(auto& row:which)row.fill(-1);
        for(int v=0;v<count;++v){
            const V pos=vertices[v];
            if(!finite(pos))continue;
            float sw[4]{},total=0;
            bool valid=true;
            for(int k=0;k<4;++k){
                const float w=weights[4*v+k];
                if(!std::isfinite(w)||w<0){valid=false;break;}
                if(w==0)continue;
                const unsigned n=(indices[v]>>(8*k))&255;
                if(n>=unsigned(b.count)){valid=false;break;}
                total+=w;
                const int tag=b.ids[n];
                if(tag==43||tag==44)sw[tag-43]+=w;
                else if(tag==53||tag==54)sw[2+tag-53]+=w;
            }
            if(!valid||std::abs(total-1.f)>.02f)continue;
            const int group=int(std::max_element(sw,sw+4)-sw);
            if(sw[group&~1]+sw[(group&~1)+1]<=.5f)continue;
            for(int k=0;k<nd;++k){
                const float h=dot(pos,directions[k]);
                if(h<low[group][k]){low[group][k]=h;which[group][k]=v;}
            }
        }
        if((which[0][0]<0&&which[1][0]<0)||(which[2][0]<0&&which[3][0]<0))return;
        std::vector<int> unique;
        for(const auto& group:which)for(int v:group){
            if(v<0||std::find(unique.begin(),unique.end(),v)!=unique.end())continue;
            unique.push_back(v);
            Vertex sample;
            for(int k=0;k<4;++k){
                const float w=weights[v*4+k];
                if(w<=0)continue;
                const int n=(indices[v]>>(8*k))&255;
                sample.weights[k]=w;
                sample.joints[k]=uint8_t(n);
                sample.local[k]=point(inverseBind[n],vertices[v]);
            }
            outline.push_back(sample);
        }
    }
    float height(const M* bones,float rootZ)const{
        float bottom=std::numeric_limits<float>::infinity();
        for(const Vertex& v:outline){
            float z=0;
            for(int k=0;k<4;++k)if(v.weights[k]>0){
                const M& m=bones[v.joints[k]];
                const V p=v.local[k];
                z+=v.weights[k]*(m.pos.z-rootZ+m.right.z*p.x+m.up.z*p.y+m.at.z*p.z);
            }
            bottom=std::min(bottom,z);
        }
        return bottom;
    }
};
static void (*fitFootwear)(void*,const Binding&,Footwear&)=nullptr;

// Compare the low part of both trajectories over a stance window. Applying
// this single slowly-changing height to the cycle preserves its flight phase.
struct SoleCalibration {
    Footwear shape;
    static constexpr int capacity=20;
    std::array<float,capacity> native{},adapted{};
    unsigned lastSample=0;
    int count=0,head=0;
    float bias=0,target=0;
    bool due(unsigned now,float blend)const{return !shape.outline.empty()&&blend>.95f&&now-lastSample>=32;}
    void observe(float host,float iv,float scale,unsigned now){
        if(!std::isfinite(host)||!std::isfinite(iv))return;
        native[head]=host;
        adapted[head]=iv;
        head=(head+1)%capacity;count=std::min(count+1,capacity);lastSample=now;
        if(count<6)return;
        const int q=count/10;
        auto a=native,b=adapted;
        std::nth_element(a.begin(),a.begin()+q,a.begin()+count);
        std::nth_element(b.begin(),b.begin()+q,b.begin()+count);
        target=std::clamp(a[q]-b[q],-.12f*scale,.12f*scale);
    }
    void advance(float dt){bias+=std::clamp(target-bias,-.14f*dt,.14f*dt);}
};
}
