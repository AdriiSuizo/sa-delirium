#include "ExpandedSkinJsonBuffer.hpp"
#include <sstream>
#include <iomanip>
#include <bit>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
int main(){
    using expandedSkinCapture::JsonBuffer;
    unsigned tested=0;uint32_t bits=0x32af4912;
    auto test=[&](float value){
        if(!std::isfinite(value))return;
        std::ostringstream old;old<<std::setprecision(9)<<value;
        JsonBuffer fast;fast<<value;
        const double a=std::strtod(old.str().c_str(),nullptr),b=std::strtod(fast.bytes().c_str(),nullptr);
        if(!fast.good()||a!=b||std::signbit(a)!=std::signbit(b)){
            std::fprintf(stderr,"decimal parity failed old=%s new=%s\n",old.str().c_str(),fast.bytes().c_str());std::exit(1);
        }++tested;
    };
    for(float value:{0.f,-0.f,1.f,-1.f,13.382775f,2430.082764f,1.e-10f,std::numeric_limits<float>::max(),std::numeric_limits<float>::lowest(),std::numeric_limits<float>::denorm_min()})test(value);
    for(int i=0;i<10000;i++){bits=bits*1664525+1013904223;test(std::bit_cast<float>(bits));}
    JsonBuffer integers;integers<<'['<<std::numeric_limits<uint64_t>::max()<<','<<std::numeric_limits<int64_t>::min()<<']';
    if(integers.bytes()!="[18446744073709551615,-9223372036854775808]")return 2;
    std::printf("PASS %u finite float values match old nine-digit decimal JSON exactly, including signed zero; full64bit integer range preserved\n",tested);
}
