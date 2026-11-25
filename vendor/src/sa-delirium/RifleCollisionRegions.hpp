#pragma once
// A distant confirmed rifle victim needs nearby map geometry without moving
// the normal traffic and pedestrian region away from CJ.
struct RifleCollisionRegions {
    std::map<int,V> centers;
    bool dirty=false;
    static bool primaryInterior(V point,V center){
        return std::abs(point.x-center.x)<32.f && std::abs(point.y-center.y)<32.f &&
               point.z-center.z<12.f && point.z-center.z>-21.f;
    }
    static bool overlaps(V center,V lo,V hi){
        return center.x+14.f>=lo.x && center.x-14.f<=hi.x &&
               center.y+14.f>=lo.y && center.y-14.f<=hi.y &&
               center.z+12.f>=lo.z && center.z-16.f<=hi.z;
    }
    bool covers(int ref,V p)const{
        const auto it=centers.find(ref);
        if(it==centers.end())return false;
        const V c=it->second;
        return std::abs(p.x-c.x)<12.f && std::abs(p.y-c.y)<12.f &&
               p.z>c.z-14.f && p.z<c.z+10.f;
    }
    void sync(const std::map<int,V>& wanted){
        for(auto it=centers.begin();it!=centers.end();){
            if(wanted.contains(it->first)){++it;continue;}
            it=centers.erase(it);dirty=true;
        }
        for(const auto&[ref,p]:wanted){
            auto [it,added]=centers.try_emplace(ref,p);
            if(added||length(it->second-p)>5.f){it->second=p;dirty=true;}
        }
    }
};
