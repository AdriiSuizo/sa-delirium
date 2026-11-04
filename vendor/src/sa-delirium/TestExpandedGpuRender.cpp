#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ExpandedGpuRender.hpp"
#include "ExpandedSkinJsonBuffer.hpp"
#include <cstdio>
#include <cstdlib>
using namespace expandedGpu;
using expandedSkinCapture::JsonBuffer;
static JsonBuffer traceBuffer;static bool first=true;static int scenario=0;static M identity{};
static void ev(const char*name){if(!first)traceBuffer<<',';first=false;traceBuffer<<"[\""<<name<<'"';}
static void desc(const Descriptor*d){traceBuffer<<','<<unsigned(d->lights)<<','<<unsigned(d->spotsTex)<<','<<unsigned(d->vertex)<<','<<unsigned(d->fogEffect);}
static void __cdecl begin(void*,uint32_t,Descriptor*d){*d={0,16,uint8_t(scenario==3?0:0x60),1};ev("begin");desc(d);traceBuffer<<']';}
static const M*__cdecl world(void*){return &identity;}
static void __cdecl active(const M*){ev("active");traceBuffer<<']';}
static void __cdecl compose(Vector4*p){for(int i=0;i<4;i++)p[i]={float(i==0),float(i==1),float(i==2),float(i==3)};ev("composed");traceBuffer<<']';}
static Vector4*__cdecl light(void*,uint32_t,Vector4*p,Descriptor*d){d->lights=scenario==2?0x21:1;for(int i=0;i<8;i++)p[i]={1,1,1,1};ev("lighting");desc(d);traceBuffer<<']';return p+8;}
static void __cdecl constants(uint32_t start,const void*,uint32_t n){ev("constants");traceBuffer<<','<<start<<','<<n<<']';}
static void __cdecl state(uint32_t k,uint32_t v){ev("state");traceBuffer<<','<<k<<','<<v<<']';}
static void __cdecl indices(void*){ev("indices");traceBuffer<<']';}
static void __cdecl streams(Stream*,int offsets){ev("streams");traceBuffer<<','<<offsets<<']';}
static void __cdecl declaration(void*){ev("declaration");traceBuffer<<']';}
static int __cdecl alpha(int v){ev("alpha");traceBuffer<<','<<v<<']';return 1;}
static void*__cdecl shader(const Material*,Descriptor*d,Dispatch*x){*x={uint8_t(scenario==3?255:9),uint8_t(scenario==3?255:10),255,255,255,{}};ev("shader");desc(d);traceBuffer<<']';return reinterpret_cast<void*>(0x1234);}
static uint32_t __cdecl count(const Descriptor*d){const unsigned dir=d->lights&15,point=d->lights>>4,spot=d->spotsTex&15;return 5+2*dir+2*point+(point+3)/4+4*spot+(spot+3)/4+bool(d->vertex&128)+bool(d->fogEffect&3)+bool(d->vertex&1);}
static void __cdecl colours(Vector4*,const Descriptor*d,float ambient,float diffuse){ev("colours");desc(d);traceBuffer<<','<<ambient<<','<<diffuse<<']';}
static void __cdecl material(const uint32_t*c,const Dispatch*d){ev("material");traceBuffer<<','<<*c<<','<<unsigned(d->material)<<']';}
static void __cdecl fog(const Descriptor*d,const Dispatch*x){ev("fog");desc(d);traceBuffer<<','<<unsigned(x->fog)<<']';}
static void __cdecl draw(Header*,Mesh*m,const Descriptor*d,const Dispatch*){ev("mesh");traceBuffer<<','<<m->baseIndex;desc(d);traceBuffer<<']';}
static void __cdecl end(void*,uint32_t,Descriptor*d){ev("end");desc(d);traceBuffer<<']';}
int main(int argc,char**argv){
    if(argc!=2)return 2;scenario=std::atoi(argv[1]);write(identity,T{});
    Api api{begin,world,active,compose,light,constants,state,indices,streams,declaration,alpha,shader,count,colours,material,fog,draw,end};
    expandedSkinHost::Geometry geometry{};Atomic atom{};atom.geometry=&geometry;void*frame=reinterpret_cast<void*>(1);std::memcpy(atom.frameObject+4,&frame,4);
    Material materials[3]{};Mesh meshes[3]{};
    for(int i=0;i<3;i++){materials[i].colour=(scenario==1&&i==1)?0x80ffffff:0xffffffff;materials[i].ambient=(scenario==1||scenario==2)?(i==0&&scenario!=2?1.f:.5f):1;materials[i].diffuse=(scenario==1||scenario==2)?(i==0&&scenario!=2?1.f:.75f):1;meshes[i].material=&materials[i];meshes[i].baseIndex=i;meshes[i].vertexAlpha=(scenario==1&&i==2);}
    Header header{};header.meshes=3;header.indices=reinterpret_cast<void*>(1);header.declaration=reinterpret_cast<void*>(2);header.useOffsets=1;
    traceBuffer<<'[';const auto r=render(api,&atom,&header,meshes,{2,scenario==2?14u:256u});traceBuffer<<']';
    if(!r.okay){std::fprintf(stderr,"failed %s\n",r.failure);return 1;}
    for(auto&m:meshes)if(m.shader)return 3;
    std::printf("%s\n",traceBuffer.bytes().c_str());return 0;
}
