#include "delirium_physics.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <fstream>
#include <string>
#include <array>
static bool check(int r){if(r>=0)return true;char e[1024]={};nm_last_error(e,1024);std::printf("error %d: %s\n",r,e);return false;}
int main(int argc,char**argv){
    if(argc<4||argc>7)return 2;
    if(!check(nm_init(argv[1]))||!check(nm_load_task_params(argv[3])))return 3;
    auto* b=nm_build_load(argv[2]);if(!b){check(-1);return 4;}
    if(argc>=5&&std::string(argv[4])!="large"){
        std::array<float,63> joints{};std::ifstream f(argv[4]);for(float&v:joints)if(!(f>>v))return 12;
        auto* fitted=nm_build_fit(b,joints.data());if(!fitted){check(-1);return 13;}nm_build_destroy(b);b=fitted;
    }
    float origin[3]={0,0,0};if(argc==5&&std::string(argv[4])=="large"){origin[0]=2500;origin[1]=-1700;origin[2]=13;}
    std::array<float,147> incoming{};float floorZ=0;
    if(argc>=6){
        std::ifstream f(argv[5]);if(!(f>>floorZ))return 14;for(float&v:incoming)if(!(f>>v))return 15;
        if(argc==7){origin[0]=incoming[0];origin[1]=incoming[1];origin[2]=floorZ;}
        else{float x=incoming[0],y=incoming[1];for(int i=0;i<21;i++){incoming[i*7]-=x;incoming[i*7+1]-=y;incoming[i*7+2]-=floorZ;}}
    }
    auto* w=nm_world_create();nm_world_add_floor(w,origin[0],origin[1],origin[2],30);
    if(argc>=5&&std::string(argv[4])!="large"){
        std::ofstream f(std::string(argv[4])+".parts.txt");f.precision(9);
        for(int i=0;i<21;i++){float p[3],q[4],mass,radius,half;int parent;nm_build_part_frame(b,i,p,q);nm_build_part_info(b,i,&mass,&radius,&half,&parent);f<<i<<' '<<parent<<' '<<mass<<' '<<radius<<' '<<half<<' '<<p[0]<<' '<<p[1]<<' '<<p[2]<<'\n';}
    }
    float pos[3]={origin[0],origin[1],origin[2]};auto* a=nm_agent_create(b,pos,0,0.3f);
    if(!a||nm_agent_part_count(a)!=21)return 5;
    if(argc==4){
        std::ofstream f(std::string(argv[2])+".probe-joints.txt");f.precision(9);
        float root[3],q[4];nm_agent_get_part_transform(a,0,root,q);
        for(int i=0;i<21;i++){float p[3],frame[4],limits[3];int parent=-1;if(i)nm_agent_get_joint(a,i,&parent,p,frame,limits,nullptr);else for(int k=0;k<3;k++)p[k]=root[k];f<<i<<' '<<parent<<' '<<p[0]-root[0]<<' '<<p[1]-root[1]<<' '<<p[2]-root[2]<<'\n';}
    }
    if(argc>=6){float zero[3]={};for(int i=0;i<21;i++){auto*p=incoming.data()+i*7;if(!check(nm_agent_set_part_state(a,i,p,p+3,zero,zero)))return 16;}}
    if(!check(nm_agent_task_balance(a,w,"*",nullptr)))return 6;
    float peakSpeed=0;
    int step=0;auto advance=[&](){
        if(!check(nm_agent_step(a,w,1.f/60)))return false;step++;
        float center[3],velocity[3];nm_agent_get_com(a,center,velocity);
        float speed=std::sqrt(velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2]);
        peakSpeed=std::max(peakSpeed,speed);
        if(!std::isfinite(speed)||speed>80){std::printf("unstable_speed step=%d speed=%g\n",step,speed);return false;}
        for(int i=0;i<21;i++){
            float p[3],q[4];nm_agent_get_part_transform(a,i,p,q);
            float distance=0,norm=0;
            for(int k=0;k<3;k++){if(!std::isfinite(p[k])){std::printf("non_finite step=%d part=%d\n",step,i);return false;}distance+=(p[k]-center[k])*(p[k]-center[k]);}
            for(float v:q)norm+=v*v;
            if(distance>16||!std::isfinite(norm)||norm<.9f||norm>1.1f){std::printf("invalid_body step=%d part=%d distance=%g quaternion_norm=%g\n",step,i,std::sqrt(distance),norm);return false;}
        }return true;
    };
    for(int i=0;i<120;i++)if(!advance())return 7;
    float q[4],hit[3],dir[3]={0,1,0},impulse[3]={0,45,0};
    nm_agent_get_part_transform(a,10,hit,q);
    if(!check(nm_agent_task_shot(a,w,"POWERFUL_WEAPON",10,hit,dir,100))||!check(nm_agent_apply_impulse(a,10,hit,impulse)))return 8;
    for(int i=0;i<240;i++)if(!advance())return 9;
    float vel[3];nm_agent_get_com(a,pos,vel);
    int contact=0;for(int i=0;i<21;i++)contact|=nm_agent_get_part_contact(a,i);std::printf("contact_mask=%d\n",contact);
    for(int i=0;i<3;i++)if(!std::isfinite(pos[i])||std::abs(pos[i]-origin[i])>15)return 10;
    if(contact<=0||pos[2]<origin[2]-.5f||pos[2]>origin[2]+5)return 11;
    std::printf("{\"sdk_version\":%d,\"parts\":%d,\"mass\":%.5f,\"steps\":360,\"com\":[%.4f,%.4f,%.4f],\"peak_speed\":%.4f,\"fallen\":%d,\"status\":\"real_asset_engine_probe_passed\"}\n",nm_version(),nm_agent_part_count(a),nm_agent_get_mass(a),pos[0],pos[1],pos[2],peakSpeed,nm_agent_has_fallen(a));
    nm_agent_destroy(a);nm_world_destroy(w);nm_build_destroy(b);return 0;
}
