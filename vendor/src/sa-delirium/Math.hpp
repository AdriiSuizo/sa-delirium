#pragma once
#include <cmath>
#include <algorithm>
struct V {float x=0,y=0,z=0; V operator+(V b)const{return{x+b.x,y+b.y,z+b.z};} V operator-(V b)const{return{x-b.x,y-b.y,z-b.z};} V operator*(float s)const{return{x*s,y*s,z*s};}};
inline float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(V v){return std::sqrt(dot(v,v));}
inline V normalized(V v){float n=length(v);return n>1.e-6f?v*(1.f/n):V{};}
inline bool finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct Q {float x=0,y=0,z=0,w=1;};
inline Q conjugate(Q a){return{-a.x,-a.y,-a.z,a.w};}
inline Q unit(Q a){float n=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z+a.w*a.w);return n>1.e-6f?Q{a.x/n,a.y/n,a.z/n,a.w/n}:Q{};}
inline Q mul(Q a,Q b){return unit({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});}
inline V rotate(Q q,V v){V u{q.x,q.y,q.z};return v+cross(u,v)*(2*q.w)+cross(u,cross(u,v))*2;}
inline Q mix(Q a,Q b,float t){if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)b={-b.x,-b.y,-b.z,-b.w};return unit({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});}
struct T {Q q{};V p{};};
inline T mul(T a,T b){return {mul(a.q,b.q),a.p+rotate(a.q,b.p)};}
inline T inverse(T a){Q q=conjugate(a.q);return{q,rotate(q,a.p*-1)};}
inline T mix(T a,T b,float t){return{mix(a.q,b.q,t),a.p+(b.p-a.p)*t};}
struct M {V right;unsigned flags;V up;unsigned pad1;V at;unsigned pad2;V pos;unsigned pad3;};
static_assert(sizeof(M)==64);
inline Q rotation(const M& m){
    V a=normalized(m.right),b=normalized(m.up),c=normalized(m.at);float tr=a.x+b.y+c.z;Q q;
    if(tr>0){float s=std::sqrt(tr+1)*2;q={ (b.z-c.y)/s,(c.x-a.z)/s,(a.y-b.x)/s,s*.25f};}
    else if(a.x>b.y&&a.x>c.z){float s=std::sqrt(1+a.x-b.y-c.z)*2;q={s*.25f,(b.x+a.y)/s,(c.x+a.z)/s,(b.z-c.y)/s};}
    else if(b.y>c.z){float s=std::sqrt(1+b.y-a.x-c.z)*2;q={(b.x+a.y)/s,s*.25f,(c.y+b.z)/s,(c.x-a.z)/s};}
    else{float s=std::sqrt(1+c.z-a.x-b.y)*2;q={(c.x+a.z)/s,(c.y+b.z)/s,s*.25f,(a.y-b.x)/s};}
    return unit(q);
}
inline T transform(const M& m){return{rotation(m),m.pos};}
inline void write(M& m,T t){m.right=rotate(t.q,{1,0,0});m.up=rotate(t.q,{0,1,0});m.at=rotate(t.q,{0,0,1});m.pos=t.p;}
inline V point(const M& m,V v){return m.pos+m.right*v.x+m.up*v.y+m.at*v.z;}
inline V vector(const M&m,V v){return m.right*v.x+m.up*v.y+m.at*v.z;}
inline M affineProduct(const M&a,const M&b){M o=b;o.right=vector(a,b.right);o.up=vector(a,b.up);o.at=vector(a,b.at);o.pos=point(a,b.pos);return o;}
inline bool affineInverse(const M&m,M&out){
    const V x=cross(m.up,m.at),y=cross(m.at,m.right),z=cross(m.right,m.up);const float determinant=dot(m.right,x);
    if(!std::isfinite(determinant)||std::abs(determinant)<1.e-8f)return false;
    const float s=1.f/determinant;out=m;
    out.right={x.x*s,y.x*s,z.x*s};out.up={x.y*s,y.y*s,z.y*s};out.at={x.z*s,y.z*s,z.z*s};out.pos=vector(out,m.pos)*-1.f;return true;
}
