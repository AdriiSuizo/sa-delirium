#pragma once
// Diagnostic JSON formatting only. No RW state, deferred pointers or worker.
// The snapshot's original nine significant digits are retained, with a fixed
// locale-independent decimal representation and one final file write.
#include <charconv>
#include <string>
#include <type_traits>
#include <system_error>

namespace expandedSkinCapture {
class JsonBuffer {
    std::string bytes_;bool okay_=true;
    template<class Number>void number(Number value){
        char buffer[64];
        const auto end=[&]{
            if constexpr(std::is_floating_point_v<Number>)
                return std::to_chars(buffer,buffer+sizeof buffer,value,std::chars_format::general,9);
            else return std::to_chars(buffer,buffer+sizeof buffer,value);
        }();
        if(end.ec!=std::errc{}){okay_=false;return;}
        bytes_.append(buffer,end.ptr);
    }
public:
    explicit JsonBuffer(size_t reserve=0){bytes_.reserve(reserve);}
    JsonBuffer&operator<<(char value){bytes_+=value;return *this;}
    JsonBuffer&operator<<(const char*value){bytes_+=value;return *this;}
    JsonBuffer&operator<<(const std::string&value){bytes_+=value;return *this;}
    template<class Number,std::enable_if_t<std::is_arithmetic_v<Number>&&!std::is_same_v<Number,char>,int> =0>
    JsonBuffer&operator<<(Number value){number(value);return *this;}
    bool good()const{return okay_;}
    const std::string&bytes()const{return bytes_;}
};
}
