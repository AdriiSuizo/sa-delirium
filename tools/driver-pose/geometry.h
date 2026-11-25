// Geometry shared by the in-game adapter and the offline skeleton check.
#pragma once
#include <algorithm>
#include <cmath>
namespace pose {
struct V { float x,y,z; };
inline V operator+(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V operator-(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V operator*(V a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(V a){return std::sqrt(dot(a,a));}
inline V unit(V a){float n=length(a);return n>1e-6f?a*(1/n):V{};}
struct Turn {
    V axis; float s,c;
    V apply(V v)const{return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));}
};
inline Turn toward(V a,V b,float weight,float limit){
    a=unit(a); b=unit(b);
    V axis=cross(a,b);float n=length(axis);
    if(n<1e-5f)return {{},0,1};
    float angle=std::min(limit,std::atan2(n,dot(a,b)))*weight;
    return {axis*(1/n),std::sin(angle),std::cos(angle)};
}
// Keep both the hand target and the two segment lengths from the current SA animation.
inline bool elbow(V start,V hand,float upper,float lower,V pole,V& result){
    V span=hand-start;float d=length(span);
    if(d<1e-4f||d>upper+lower+1e-5f||d<std::abs(upper-lower)-1e-5f)return false;
    V axis=span*(1/d), normal=unit(pole-axis*dot(pole,axis));
    if(length(normal)<.99f)return false;
    float along=(upper*upper-lower*lower+d*d)/(2*d);
    result=start+axis*along+normal*std::sqrt(std::max(0.f,upper*upper-along*along));
    return true;
}
}
