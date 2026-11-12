#pragma once
#include "Math.hpp"
#include <array>
#include <cctype>
#include <charconv>
#include <string_view>
#include <utility>

namespace firearm {
struct Profile {
    float force=0, accuracy=0, reach=0, damage=0, scopedDamage=0;
    unsigned interval=0, pellets=0;
    bool shotgun=false, penetratesArmour=false;
};
using Profiles=std::array<Profile, 39>;

inline std::string_view attribute(std::string_view tag,std::string_view name){
    for(size_t at=tag.find(name);at!=tag.npos;at=tag.find(name,at+name.size())){
        if(at==0||!std::isspace(static_cast<unsigned char>(tag[at-1])))continue;
        auto rest=tag.substr(at+name.size());
        while(!rest.empty()&&std::isspace(static_cast<unsigned char>(rest.front())))rest.remove_prefix(1);
        if(rest.empty()||rest.front()!='=')continue;
        rest.remove_prefix(1);
        while(!rest.empty()&&std::isspace(static_cast<unsigned char>(rest.front())))rest.remove_prefix(1);
        if(rest.empty()||(rest.front()!='"'&&rest.front()!=39))continue;
        const auto end=rest.find(rest.front(),1);
        if(end!=rest.npos)return rest.substr(1,end-1);
    }
    return {};
}
inline std::string_view tag(std::string_view xml,std::string_view opening){
    for(size_t at=xml.find(opening);at!=xml.npos;at=xml.find(opening,at+opening.size())){
        const auto next=at+opening.size();
        if(next>=xml.size()||(!std::isspace(static_cast<unsigned char>(xml[next]))&&xml[next]!='>'&&xml[next]!='/'))continue;
        const auto end=xml.find('>',next);
        if(end!=xml.npos)return xml.substr(at,end-at+1);
    }
    return {};
}
template<class T>inline T number(std::string_view text){
    if(text.empty())return {};
    T value{};
    const auto end=text.data()+text.size();
    const auto parsed=std::from_chars(text.data(),end,value);
    return parsed.ec==std::errc{}&&parsed.ptr==end?value:T{};
}
inline Profiles parseProfiles(std::string_view xml){
    const std::pair<int,std::string_view> names[]={{22,"PISTOL"},{23,"PISTOL"},{24,"DEAGLE"},
        {25,"SHOTGUN"},{26,"SHOTGUN"},{27,"BARETTA"},{28,"MICRO_UZI"},{29,"MP5"},{30,"AK47"},{31,"M4"},
        {32,"MICRO_UZI"},{33,"M40a1"},{34,"SNIPERRIFLE"},{38,"MINIGUN"}};
    Profiles out{};
    while(!xml.empty()){
        const auto opening=tag(xml,"<weapon");
        if(opening.empty())break;
        const size_t offset=size_t(opening.data()-xml.data());
        const auto end=xml.find("</weapon>",offset+opening.size());
        if(end==xml.npos)break;
        const auto row=xml.substr(offset,end-offset);
        const auto data=tag(row,"<data"),physics=tag(row,"<physics"),aim=tag(row,"<aiming");
        const auto damage=tag(row,"<damage");
        Profile profile;
        profile.force=number<float>(attribute(physics,"force"));
        profile.accuracy=number<float>(attribute(aim,"accuracy"));
        profile.reach=number<float>(attribute(data,"weaponrange"));
        profile.interval=number<unsigned>(attribute(data,"timebetweenshots"));
        profile.pellets=number<unsigned>(attribute(aim,"pellets"));
        profile.shotgun=attribute(data,"slot")=="SHOTGUN";
        profile.damage=number<float>(attribute(damage,"base"));
        profile.scopedDamage=number<float>(attribute(damage,"fps"));
        profile.penetratesArmour=row.find("<flag>ARMOUR_PENETRATING</flag>")!=row.npos;
        if(std::isfinite(profile.force)&&profile.force>0&&profile.force<=1000&&profile.interval>=16&&profile.interval<=5000&&profile.pellets<=32){
            for(const auto& [id,name]:names)if(attribute(opening,"type")==name)out[id]=profile;
        }
        xml.remove_prefix(end+9);
    }
    return out;
}
inline V shotDirection(V from,V hit){
    V direction=normalized(hit-from);
    const float flat=std::hypot(direction.x,direction.y);
    if(flat>.05f){direction.x/=flat;direction.y/=flat;}
    return direction;
}
inline V pelletDirection(V forward,float accuracy,float across,float up){
    const V horizontal=normalized(cross(std::abs(forward.z)>.95f?V{0,1,0}:V{0,0,1},forward));
    const V vertical=cross(forward,horizontal);
    const float spread=.4f*accuracy;
    return normalized(forward+horizontal*(spread*across)+vertical*(spread*up));
}
}
