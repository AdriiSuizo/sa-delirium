#pragma once
#include <algorithm>
#include <map>
#include <vector>
#include <cstring>
#include "NativeCoverageGeometry.hpp"
// Evidence for replacing native map support, separate from render geometry.
// A tessellated sphere, moved object or truncated query is NOT exact coverage.
struct NativeWorldCoverage {
    struct Row {
        int model=-1,type=0;
        const ColModel*col=nullptr;
        const ColData*data=nullptr;
        M frame{};
        size_t offset=0,vertices=0;
        unsigned short triangles=0,boxes=0,spheres=0;
    };
    std::map<void*,Row> rows;
    std::vector<unsigned char> geometry;
    bool complete=false;V centre{};int area=-1;
    static M frame(void*e){M f{};if(auto*m=matrix(e))f=*m;else reinterpret_cast<void(__thiscall*)(void*,M*)>(0x54EF40)(static_cast<unsigned char*>(e)+4,&f);return f;}
    static bool sameFrame(const M&a,const M&b){return std::memcmp(&a.right,&b.right,sizeof(V))==0&&std::memcmp(&a.up,&b.up,sizeof(V))==0&&std::memcmp(&a.at,&b.at,sizeof(V))==0&&std::memcmp(&a.pos,&b.pos,sizeof(V))==0;}
    static bool comparable(const ColModel*c){
        if(!c||!c->data)return false;
        const auto*d=c->data;
        return !d->lines&&(d->triangles||d->boxes||d->spheres)&&
            (!d->triangles||(d->vertices&&d->tri))&&(!d->boxes||d->box)&&(!d->spheres||d->sphere);
    }
    void begin(V origin,int currentArea){rows.clear();geometry.clear();complete=true;centre=origin;area=currentArea;}
    void record(void*e,const ColModel*c,const M&m){
        const int type=at<unsigned char>(e,0x36)&7;
        if((type!=1&&type!=5)||!comparable(c))return;
        const auto*d=c->data;
        size_t count=0;
        for(unsigned i=0;i<d->triangles;i++){
            const auto&t=d->tri[i];
            count=std::max(count,size_t(std::max({t.a,t.b,t.c}))+1);
        }
        rows[e]={at<short>(e,0x22),type,c,d,m,geometry.size(),count,d->triangles,d->boxes,d->spheres};
        auto append=[&](const void*values,size_t bytes){
            if(!bytes)return;
            const auto*data=static_cast<const unsigned char*>(values);
            geometry.insert(geometry.end(),data,data+bytes);
        };
        append(d->tri,d->triangles*sizeof(Tri));
        append(d->vertices,count*sizeof(Compressed));
        append(d->box,d->boxes*sizeof(ColBox));
        append(d->sphere,d->spheres*sizeof(ColSphere));
    }
    bool sameGeometry(const ColModel*c,const Row&r)const{
        if(!comparable(c))return false;
        const auto*d=c->data;
        if(d->triangles!=r.triangles||d->boxes!=r.boxes||d->spheres!=r.spheres)return false;
        const auto*bytes=geometry.data()+r.offset;
        const auto equal=[&](const void*current,size_t length){
            const bool same=!length||std::memcmp(bytes,current,length)==0;
            bytes+=length;
            return same;
        };
        if(!equal(d->tri,d->triangles*sizeof(Tri)))return false;
        const auto*vertices=bytes;
        if(!equal(d->vertices,r.vertices*sizeof(Compressed))){
            // The native COL may have unused vertices. As before, only the
            // indices actually referenced by collision triangles affect coverage.
            for(unsigned i=0;i<d->triangles;i++){
                const auto&t=d->tri[i];
                for(unsigned k:{t.a,t.b,t.c})
                    if(std::memcmp(vertices+k*sizeof(Compressed),d->vertices+k,sizeof(Compressed)))return false;
            }
        }
        return equal(d->box,d->boxes*sizeof(ColBox))&&equal(d->sphere,d->spheres*sizeof(ColSphere));
    }
    bool currentSnapshot(void*e)const{
        const auto i=rows.find(e);if(!complete||i==rows.end())return false;const auto&r=i->second;
        if((at<unsigned char>(e,0x36)&7)!=r.type||at<short>(e,0x22)!=r.model||
           (at<unsigned char>(e,0x2F)!=area&&at<unsigned char>(e,0x2F)!=13)||!sameFrame(frame(e),r.frame))return false;
        const auto*c=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(e);
        return c==r.col&&c&&c->data==r.data&&sameGeometry(c,r);
    }
    bool current(void*e)const{return currentSnapshot(e)&&rows.at(e).data->spheres==0;}
    bool currentLocal(void*e,V start,V end,float radius)const{
        if(!currentSnapshot(e)||!contains(start,radius)||!contains(end,radius))return false;
        const auto&r=rows.at(e);for(unsigned n=0;n<r.data->spheres;n++){
            const auto&s=r.data->sphere[n];if(!native_coverage_geometry::sphereOutsideSweep(r.frame,s.pos,s.radius,start,end,radius))return false;
        }return true;
    }
    bool contains(V p,float radius)const{
        return complete&&finite(p)&&std::isfinite(radius)&&radius>0&&
            std::abs(p.x-centre.x)+radius<45&&std::abs(p.y-centre.y)+radius<45&&
            p.z-radius>centre.z-30&&p.z+radius<centre.z+20;
    }
};
