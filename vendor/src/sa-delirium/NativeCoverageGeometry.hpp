#pragma once
#include "Math.hpp"
#include <limits>
// Local proof for an exact triangle/box mesh that also contains unsupported
// native spheres. This does not replace a sphere with a polygon or ignore it:
// it certifies strict separation from the WHOLE swept enclosing body volume.
namespace native_coverage_geometry {
inline double dot3(const double*a,const double*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline bool sphereOutsideSweep(const M&m,V local,float sphereRadius,V start,V end,float bodyRadius){
    if(!finite(local)||!finite(start)||!finite(end)||!finite(m.right)||!finite(m.up)||!finite(m.at)||!finite(m.pos)||
       !std::isfinite(sphereRadius)||sphereRadius<0||!std::isfinite(bodyRadius)||bodyRadius<=0)return false;
    const double a[3][3]={{m.right.x,m.up.x,m.at.x},{m.right.y,m.up.y,m.at.y},{m.right.z,m.up.z,m.at.z}};
    const double p[3]={local.x,local.y,local.z},origin[3]={m.pos.x,m.pos.y,m.pos.z},s[3]={start.x,start.y,start.z},e[3]={end.x,end.y,end.z};
    double c[3],d[3],v[3],norm1=0,normInf=0,error2=0;
    for(int j=0;j<3;j++){
        double row=0,col=0,magnitude=std::abs(origin[j]);c[j]=origin[j];
        for(int k=0;k<3;k++){row+=std::abs(a[j][k]);col+=std::abs(a[k][j]);c[j]+=a[j][k]*p[k];magnitude+=std::abs(a[j][k]*p[k]);}
        norm1=std::max(norm1,col);normInf=std::max(normInf,row);
        // Outward arithmetic uncertainty for the corresponding float affine
        // transform (three products/adds). This is an error bound, not a
        // contact tolerance, and cannot turn an overlap into permission.
        const double error=8.*std::numeric_limits<float>::epsilon()*magnitude;error2+=error*error;
        d[j]=e[j]-s[j];v[j]=c[j]-s[j];
    }
    const double segment2=dot3(d,d);const double t=segment2>0?std::clamp(dot3(v,d)/segment2,0.,1.):0.;
    for(int j=0;j<3;j++)v[j]-=t*d[j];
    // ||A||2 <= sqrt(||A||1 ||A||inf), including scaled/sheared native frames.
    const double expanded=bodyRadius+double(sphereRadius)*std::sqrt(norm1*normInf)+std::sqrt(error2);
    return std::isfinite(expanded)&&dot3(v,v)>expanded*expanded;
}
inline float originalNormalRadius(float rootShift){
    if(!std::isfinite(rootShift))return std::numeric_limits<float>::infinity();
    // 822E5250's exact four root-space parts, including .04 composite margin
    // and .02 manifold breaking distance. Rotation is yaw-only, so each is
    // enclosed for every heading. RootShift adapts IV ground+1 to SA's rig.
    constexpr float z[4]={.085f,-.875f,.085f,-.55f},half[4]={.635f,.325f,.485f,.15f},core[4]={.15f,0,0,.15f},round[4]={.1f,.1f,.25f,.1f};
    double radius=0;for(int i=0;i<4;i++){const double axial=std::abs(double(z[i])+rootShift)+half[i];radius=std::max(radius,std::sqrt(axial*axial+double(core[i])*core[i])+round[i]+.04+.02);}
    return std::nextafter(float(radius),std::numeric_limits<float>::infinity());
}
}
