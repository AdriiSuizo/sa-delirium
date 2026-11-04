#pragma once
#include "BodyContactMath.hpp"
// Queries must follow the rendered SA hierarchy, including the get-up blend.
// Forces remain in the corresponding physical part's local frame.
static const std::array<T,21>& contactPose(const Active&a){
    return !a.lastRendered.empty()&&gameTime()-a.lastRenderTime<=100?a.renderedParts:a.current;
}
static V physicalContactPoint(const Active&a,int part,V renderedPoint){
    return mul(a.current[part],mul(inverse(contactPose(a)[part]),T{{},renderedPoint})).p;
}
static bool contactBodyValid(const Active&a){
    return pedFromRef(a.ref)==a.ped&&at<short>(a.ped,0x22)==a.model&&
        !a.mounted&&!a.preview&&at<unsigned char>(a.ped,0x2F)==area();
}
