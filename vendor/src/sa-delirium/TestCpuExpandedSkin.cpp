#include "CpuExpandedSkin.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
using namespace expandedSkin;
static int checks=0;
static void check(bool ok,const char*why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
template<class T>static T read(std::istream&in){T out{};in.read(reinterpret_cast<char*>(&out),sizeof out);check(bool(in),"fixture read");return out;}
static M matrix(std::istream&in){M m{};m.right=read<V>(in);m.up=read<V>(in);m.at=read<V>(in);m.pos=read<V>(in);return m;}
static M identity(){M m{};m.right.x=m.up.y=m.at.z=1;return m;}
static void synthetic(){
    std::array<Weight,4>w{{{0,.1f},{1,.2f},{2,.3f},{3,.4f}}};
    std::array<Split,4>s{{{0,4,.2f},{1,5,.3f},{2,6,.4f},{3,7,.5f}}};Influences ex;
    check(expand(w,s,8,ex)&&ex.count==8,"all eight influences preserved");
    std::array<M,8> palette;for(unsigned n=0;n<8;n++){palette[n]=identity();palette[n].pos={float(n),float(n*n),float(n*3)};}
    Vertex v{{1,2,3},{0,0,1}},out;
    check(vertex(v,ex,palette.data(),8,out),"eight-weight skin vertex");
    double expected[3]{1,2,3};for(unsigned n=0;n<4;n++){
        expected[0]+=w[n].value*((1-s[n].secondFraction)*s[n].first+s[n].secondFraction*s[n].second);
        expected[1]+=w[n].value*((1-s[n].secondFraction)*s[n].first*s[n].first+s[n].secondFraction*s[n].second*s[n].second);
        expected[2]+=3*w[n].value*((1-s[n].secondFraction)*s[n].first+s[n].secondFraction*s[n].second);
    }
    check(std::abs(out.position.x-expected[0])<2e-6&&std::abs(out.position.y-expected[1])<3e-6&&std::abs(out.position.z-expected[2])<3e-6,"eight influences analytic translation");
    check(length(out.normal-V{0,0,1})<1e-6,"position translation must not alter normal");
    const Influences previous=ex;s[2].secondFraction=1.1f;
    check(!expand(w,s,8,ex)&&ex.count==previous.count,"invalid split rejects without partial output");
    s[2].secondFraction=.4f;w[2].value=-1;
    check(!expand(w,s,8,ex),"negative source weight rejected");w[2].value=.3f;
    ex.weights[7].bone=65535;Vertex sentinel{{123,456,789},{1,2,3}};out=sentinel;
    check(!vertex(v,ex,palette.data(),8,out)&&out.position.x==123,"absent palette index rejects without output");
    expand(w,s,8,ex);palette[0].pos.x=std::numeric_limits<float>::quiet_NaN();
    check(!vertex(v,ex,palette.data(),8,out),"nonfinite affine rejected");
    M parent=identity(),inv=identity(),virt=identity(),result;
    parent.right.x=1.2f;parent.up={.1f,.9f,0};parent.at.z=.8f;parent.pos={1,2,3};inv.pos={-.2f,.3f,-.7f};virt.pos={.6f,.8f,1.3f};
    check(inheritVirtual(parent,inv,virt,result),"full affine virtual seed");M inverseVirtual;
    check(affineInverse(virt,inverseVirtual),"inverse virtual rest");
    const M lhs=affineProduct(result,inverseVirtual),rhs=affineProduct(parent,inv);
    check(length(lhs.pos-rhs.pos)<1e-6&&length(lhs.right-rhs.right)<1e-6&&length(lhs.up-rhs.up)<1e-6,"affine scale and shear inherited exactly");
}
static double maxError=0,maxNormalError=0,elapsedMs=0;static uint64_t vertices=0,totalTimedVertices=0;static unsigned maxInfluences=0;
int main(int argc,char**argv){
    synthetic();check(argc==2,"fixture argument");std::ifstream input(argv[1],std::ios::binary);
    check(read<uint32_t>(input)==0x41385345,"ES8A fixture magic");const unsigned models=read<uint32_t>(input);
    for(unsigned model=0;model<models;model++){
        const auto nameSize=read<uint32_t>(input);std::string name(nameSize,' ');input.read(name.data(),nameSize);
        const auto count=read<uint32_t>(input),bones=read<uint32_t>(input),cases=read<uint32_t>(input);vertices+=count;
        std::vector<Vertex> source(count),staging(count);std::vector<Influences> expanded(count);
        std::vector<std::array<Weight,4>> original(count);
        for(unsigned i=0;i<count;i++){
            source[i]=read<Vertex>(input);std::array<uint32_t,4> indices=read<std::array<uint32_t,4>>(input);
            auto weights=read<std::array<float,4>>(input);auto seconds=read<std::array<uint32_t,4>>(input);auto fractions=read<std::array<float,4>>(input);std::array<Split,4> split;
            for(unsigned n=0;n<4;n++){original[i][n]={uint16_t(indices[n]),weights[n]};split[n]={uint16_t(indices[n]),uint16_t(seconds[n]),fractions[n]};}
            check(expand(original[i],split,bones,expanded[i]),"installed skin weight expansion");maxInfluences=std::max<unsigned>(maxInfluences,expanded[i].count);
        }
        Binding binding;check(binding.prepare(source.data(),expanded.data(),count,bones),"compile immutable skin binding");
        std::vector<Vertex> compiledOutput(count);std::vector<M> palette(bones);
        for(unsigned c=0;c<cases;c++){
            const auto nativeMode=read<uint32_t>(input);for(auto&m:palette)m=matrix(input);
            Bounds bounds;check(mesh(source.data(),expanded.data(),count,palette.data(),bones,staging.data(),bounds),"installed mesh staging");
            Bounds compiledBounds;check(binding.deform(palette.data(),bones,compiledOutput.data(),compiledOutput.size(),compiledBounds),"compiled immutable skin staging");
            for(unsigned i=0;i<count;i++){
                check(length(compiledOutput[i].position-staging[i].position)==0&&length(compiledOutput[i].normal-staging[i].normal)==0,"compiled and uncached arithmetic identical");
                if(nativeMode){
                    // Independent native four-weight oracle. No expansion or
                    // changes to the original palette enter this calculation.
                    double p[3]{},n[3]{};const auto&v=source[i];
                    for(const auto&w:original[i])if(w.value>0){const M&m=palette[w.bone];
                        p[0]+=w.value*(double(m.pos.x)+double(m.right.x)*v.position.x+double(m.up.x)*v.position.y+double(m.at.x)*v.position.z);
                        p[1]+=w.value*(double(m.pos.y)+double(m.right.y)*v.position.x+double(m.up.y)*v.position.y+double(m.at.y)*v.position.z);
                        p[2]+=w.value*(double(m.pos.z)+double(m.right.z)*v.position.x+double(m.up.z)*v.position.y+double(m.at.z)*v.position.z);
                        n[0]+=w.value*(double(m.right.x)*v.normal.x+double(m.up.x)*v.normal.y+double(m.at.x)*v.normal.z);
                        n[1]+=w.value*(double(m.right.y)*v.normal.x+double(m.up.y)*v.normal.y+double(m.at.y)*v.normal.z);
                        n[2]+=w.value*(double(m.right.z)*v.normal.x+double(m.up.z)*v.normal.y+double(m.at.z)*v.normal.z);
                    }
                    const V a=staging[i].position,b=staging[i].normal;
                    const double e=std::sqrt(std::pow(a.x-p[0],2)+std::pow(a.y-p[1],2)+std::pow(a.z-p[2],2));
                    const double ne=std::sqrt(std::pow(b.x-n[0],2)+std::pow(b.y-n[1],2)+std::pow(b.z-n[2],2));
                    maxError=std::max(maxError,e);maxNormalError=std::max(maxNormalError,ne);
                    check(e<1e-5&&ne<3e-6,"expanded native rendering equals original four weights");
                }
                const auto p=staging[i].position;
                check(p.x>=bounds.min.x&&p.y>=bounds.min.y&&p.z>=bounds.min.z&&p.x<=bounds.max.x&&p.y<=bounds.max.y&&p.z<=bounds.max.z,"deformed culling bounds include every vertex");
            }
        }
        // CPU only, actual installed data. Does not include buffer upload,
        // render callbacks, lights, triangle normals, or concurrent game cost.
        constexpr unsigned loops=50;Bounds bounds;auto before=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<loops;repeat++)check(binding.deform(palette.data(),bones,staging.data(),staging.size(),bounds),"timed skin batch");
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();elapsedMs+=ms;totalTimedVertices+=uint64_t(count)*loops;
        if(name=="model7/male01.dff")std::printf("model7_cpu_mesh_ms=%.6f vertices=%u\n",ms/loops,count);
    }
    std::printf("{\"checks\":%d,\"models\":%u,\"vertices\":%llu,\"maximum_influences\":%u,\"native_max_position_error_m\":%.12g,\"native_max_normal_error\":%.12g,\"cpu_ns_per_vertex\":%.6f,\"timed_vertices\":%llu}\n",checks,models,(unsigned long long)vertices,maxInfluences,maxError,maxNormalError,elapsedMs*1e6/double(totalTimedVertices),(unsigned long long)totalTimedVertices);
}
