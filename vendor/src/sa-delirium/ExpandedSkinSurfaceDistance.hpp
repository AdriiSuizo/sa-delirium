#pragma once
#include "Math.hpp"
namespace expandedSkinTest {
inline V segmentClosest(V p,V a,V b){const V d=b-a;const float square=dot(d,d);return square>0?a+d*std::clamp(dot(p-a,d)/square,0.f,1.f):a;}
// Closest point on the actual deformed triangle, with exact degeneracy fallback.
inline V triangleClosest(V p,V a,V b,V c){
    const V ab=b-a,ac=c-a,ap=p-a;const float d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return a;const V bp=p-b;const float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return b;
    const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0&&d1!=d3)return a+ab*(d1/(d1-d3));
    const V cp=p-c;const float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return c;
    const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0&&d2!=d6)return a+ac*(d2/(d2-d6));
    const float va=d3*d6-d5*d4;const float edge=(d4-d3)+(d5-d6);
    if(va<=0&&d4>=d3&&d5>=d6&&edge!=0)return b+(c-b)*((d4-d3)/edge);
    const float total=va+vb+vc;if(total>0&&std::isfinite(total))return a+ab*(vb/total)+ac*(vc/total);
    const V choices[]={segmentClosest(p,a,b),segmentClosest(p,a,c),segmentClosest(p,b,c)};V best=choices[0];for(V q:choices)if(dot(q-p,q-p)<dot(best-p,best-p))best=q;return best;
}
}
