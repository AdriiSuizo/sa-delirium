#pragma once
// Pure, unwired renderer investigation. No native hierarchy/geometry ownership.
// Original SA four-slot skin inputs remain immutable. Expanded results live in
// a caller-owned staging stream, published only after the whole batch succeeds.
#include "Math.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace expandedSkin {
struct Weight { uint16_t bone=0; float value=0; };
struct Influences { std::array<Weight,8> weights{}; uint8_t count=0; };
struct Split { uint16_t first=0,second=0; float secondFraction=0; };
struct Vertex { V position{},normal{}; };
struct Bounds { V min{},max{}; };

// A finite original influence may be split without discarding even a tiny
// positive weight. No top-four reduction, renormalization or inferred bone ID.
inline bool expand(const std::array<Weight,4>& original,const std::array<Split,4>& split,
                   size_t paletteSize,Influences& result){
    Influences staged{};
    const auto add=[&](uint16_t bone,float value){
        if(value==0)return true;
        for(unsigned n=0;n<staged.count;n++)if(staged.weights[n].bone==bone){
            staged.weights[n].value+=value;return std::isfinite(staged.weights[n].value);
        }
        if(staged.count==staged.weights.size())return false;
        staged.weights[staged.count++]={bone,value};return true;
    };
    for(unsigned n=0;n<original.size();n++){
        const auto w=original[n];const auto s=split[n];
        if(!std::isfinite(w.value)||w.value<0||!std::isfinite(s.secondFraction)||
           s.secondFraction<0||s.secondFraction>1)return false;
        if(w.value==0)continue;
        if(w.bone>=paletteSize||s.first>=paletteSize||s.second>=paletteSize)return false;
        const float second=w.value*s.secondFraction;
        if(!add(s.first,w.value-second)||!add(s.second,second))return false;
    }
    if(!staged.count)return false;
    result=staged;return true;
}

inline bool affineFinite(const M&m){return finite(m.right)&&finite(m.up)&&finite(m.at)&&finite(m.pos);}
inline bool valid(const Influences& w,size_t n){
    if(!w.count||w.count>w.weights.size())return false;
    for(unsigned i=0;i<w.count;i++)if(w.weights[i].bone>=n||!std::isfinite(w.weights[i].value)||w.weights[i].value<=0)return false;
    return true;
}

// Native-mode insertion identity, INCLUDING native affine scale/shear:
// virtualWorld = parentWorld * parentInverseBind * virtualBind.
// Hence virtualWorld*virtualInverseBind == parentWorld*parentInverseBind.
// Do not convert these matrices to rigid T/quaternions (CJ helpers lose scale).
inline bool inheritVirtual(const M&parentWorld,const M&parentInverseBind,
                           const M&virtualBind,M&virtualWorld){
    if(!affineFinite(parentWorld)||!affineFinite(parentInverseBind)||!affineFinite(virtualBind))return false;
    const M out=affineProduct(affineProduct(parentWorld,parentInverseBind),virtualBind);
    if(!affineFinite(out))return false;virtualWorld=out;return true;
}

// Matrices already include inverse bind and the render atomic's local-space
// conversion. Translation affects positions only. Normals are raw linear-blend
// outputs: the native rendering pipeline's normalization policy is a separate
// integration requirement, not silently changed by this pure deformation stage.
inline bool vertex(const Vertex&in,const Influences&w,const M*palette,size_t n,Vertex&out){
    if(!palette||!finite(in.position)||!finite(in.normal)||!valid(w,n))return false;
    Vertex staged{};
    for(unsigned i=0;i<w.count;i++){
        const auto& weight=w.weights[i];const M&m=palette[weight.bone];
        if(!affineFinite(m))return false;
        staged.position=staged.position+point(m,in.position)*weight.value;
        staged.normal=staged.normal+vector(m,in.normal)*weight.value;
    }
    if(!finite(staged.position)||!finite(staged.normal))return false;
    out=staged;return true;
}

// Caller must not attach staging to a render resource unless this returns true.
// Bounds include the actual deformed stream (not the old undisturbed sphere).
inline bool mesh(const Vertex*source,const Influences*weights,size_t count,
                 const M*palette,size_t bones,Vertex*staging,Bounds&bounds){
    if(!source||!weights||!staging||!palette||!count||!bones)return false;
    Bounds result{};
    for(size_t i=0;i<count;i++){
        if(!vertex(source[i],weights[i],palette,bones,staging[i]))return false;
        const V p=staging[i].position;
        if(i==0)result={p,p};else{
            result.min={std::min(result.min.x,p.x),std::min(result.min.y,p.y),std::min(result.min.z,p.z)};
            result.max={std::max(result.max.x,p.x),std::max(result.max.y,p.y),std::max(result.max.z,p.z)};
        }
    }
    bounds=result;return true;
}

// Immutable compiled copy, prepared on a new geometry/clothes generation.
// Removing repeated per-influence validity checks here does not change the
// summation, weights, palette, precision, or output acceptance criteria.
class Binding {
    std::vector<Vertex> source_;
    std::vector<Influences> weights_;
    size_t bones_=0;
public:
    size_t vertices()const{return source_.size();}
    bool prepare(const Vertex*source,const Influences*weights,size_t count,size_t bones){
        if(!source||!weights||!count||!bones)return false;
        for(size_t i=0;i<count;i++)if(!finite(source[i].position)||!finite(source[i].normal)||!valid(weights[i],bones))return false;
        std::vector<Vertex> stagedSource(source,source+count);
        std::vector<Influences> stagedWeights(weights,weights+count);
        source_.swap(stagedSource);weights_.swap(stagedWeights);bones_=bones;return true;
    }
    bool deform(const M*palette,size_t bones,Vertex*staging,size_t capacity,Bounds&bounds)const{
        if(!palette||!staging||source_.empty()||bones!=bones_||capacity<source_.size())return false;
        for(size_t i=0;i<bones;i++)if(!affineFinite(palette[i]))return false;
        Bounds result{};
        for(size_t i=0;i<source_.size();i++){
            Vertex out{};const auto&in=source_[i];const auto&w=weights_[i];
            for(unsigned n=0;n<w.count;n++){
                const auto&weight=w.weights[n];const auto&m=palette[weight.bone];
                out.position=out.position+point(m,in.position)*weight.value;
                out.normal=out.normal+vector(m,in.normal)*weight.value;
            }
            if(!finite(out.position)||!finite(out.normal))return false;
            staging[i]=out;const V p=out.position;
            if(i==0)result={p,p};else{
                result.min={std::min(result.min.x,p.x),std::min(result.min.y,p.y),std::min(result.min.z,p.z)};
                result.max={std::max(result.max.x,p.x),std::max(result.max.y,p.y),std::max(result.max.z,p.z)};
            }
        }
        bounds=result;return true;
    }
};
} // namespace expandedSkin
