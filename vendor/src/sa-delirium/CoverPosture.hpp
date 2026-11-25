#pragma once
#include <string>

namespace player_cover {
constexpr bool coverBodyLow(bool lowWall, bool nativeDucking) {
    return lowWall || nativeDucking;
}
static std::string keyFor(bool low,bool corner,int side,const char* name){
    return std::string("cover_")+(side>0?"r_":"l_")+(low?"low_":"high_")+
        (corner?"corner/":"centre/")+name;
}
}
