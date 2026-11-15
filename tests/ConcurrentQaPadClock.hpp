#pragma once
#include <cstdint>
#include <cctype>
#include <string>

// Pure clock policy for the disposable QA bridge. No game/OS reads here.
namespace qa_pad {
enum class DurationField { Missing, Valid, Invalid };
inline DurationField durationField(const std::string& text,std::uint32_t& duration){
    constexpr const char* key="\"game_ms\"";
    auto at=text.find(key);
    if(at==std::string::npos)return DurationField::Missing;
    if(text.find(key,at+9)!=std::string::npos)return DurationField::Invalid;
    at+=9;
    const auto whitespace=[&](){while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;};
    whitespace();if(at==text.size()||text[at++]!=':')return DurationField::Invalid;
    whitespace();const auto start=at;std::uint32_t value=0;
    while(at<text.size()&&text[at]>='0'&&text[at]<='9'){
        value=value*10+unsigned(text[at++]-'0');
        if(value>5000)return DurationField::Invalid;
    }
    if(at==start||value<1)return DurationField::Invalid;
    if(at-start>1&&text[start]=='0')return DurationField::Invalid;
    whitespace();if(at==text.size()||(text[at]!=','&&text[at]!='}'))return DurationField::Invalid;
    duration=value;return DurationField::Valid;
}
struct Clock {
    bool tracked=false,pending=false,active=false;
    std::uint32_t requested=0,start=0,deadline=0,stopped=0,startFrame=0,stopFrame=0,lastGame=0;
    const char* reason="none";
    void stop(std::uint32_t now,std::uint32_t frame,const char* why){
        pending=active=false;stopped=now;stopFrame=frame;reason=why;
    }
    void arm(std::uint32_t duration){*this={};tracked=true;pending=true;requested=duration;reason="pending";}
    void legacy(std::uint32_t now,std::uint32_t frame){
        if(pending||active)stop(now,frame,"replaced");
        tracked=false;
    }
    void reject(std::uint32_t now,std::uint32_t frame,const char* why){
        if(!tracked)*this={};
        tracked=true;stop(now,frame,why);
    }
    bool poll(bool ready,std::uint32_t now,std::uint32_t frame,std::uint64_t wall,std::uint64_t wallDeadline){
        if(!tracked)return wall<wallDeadline;
        if(!pending&&!active)return false;
        if(!ready){stop(lastGame,frame,"game_not_ready");return false;}
        lastGame=now;
        if(wall>=wallDeadline){stop(now,frame,"wall_ttl");return false;}
        if(pending){pending=false;active=true;start=now;deadline=now+requested;startFrame=frame;reason="active";}
        const std::uint32_t elapsed=now-start;
        if(elapsed>0x7fffffffu){stop(now,frame,"clock_reset");return false;}
        if(elapsed>=requested){stop(now,frame,"simulation_deadline");return false;}
        return true;
    }
};
}
