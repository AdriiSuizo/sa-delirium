#pragma once
// Only the PID-scoped test bridge calls this route. SA walks/runs CJ through
// its real task/animation/collision path. No scene loads between waypoints.
static void* routeTask=nullptr;
static int streamingRoute(int mode){
    auto*p=player();if(!ready||!playing()||!p||area()!=0||cutscene())return -1;
    if(mode==57){
        // Existing SDK association lookup: read the evaluated locomotion, do
        // not force a clip or synthesize input to make the test appear correct.
        std::ofstream f(folder/"locomotion-probe.json");const V pos=position(p);
        f<<"{\"pid\":"<<GetCurrentProcessId()<<",\"game_ms\":"<<gameTime()<<",\"position\":["<<pos.x<<','<<pos.y<<','<<pos.z<<"],\"animations\":[";
        bool comma=false;for(unsigned id:{0u,1u,2u}){
            auto*a=reinterpret_cast<void*(__cdecl*)(void*,unsigned)>(0x4D68B0)(at<void*>(p,0x18),id);if(!a)continue;
            auto*h=at<void*>(a,0x14);if(!h)continue;if(comma)f<<',';comma=true;
            f<<"{\"id\":"<<id<<",\"group\":"<<at<unsigned short>(a,0xE)<<",\"time\":"<<at<float>(a,0x20)
             <<",\"duration\":"<<at<float>(h,0x10)<<",\"blend\":"<<at<float>(a,0x18)<<",\"hash\":"<<at<unsigned>(h,0)<<'}';
        }f<<"],\"niko_active\":"<<(nikoActive?"true":"false")<<",\"niko_state\":[";for(int i=0;i<8;i++){if(i)f<<',';f<<nikoState[i];}
        char clip[160]{};if(nikoMovement)nm_locomotion_clip(nikoMovement,clip,sizeof(clip));f<<"],\"niko_clip\":"<<sessionlog::quote(clip)
          <<",\"support_flags\":"<<unsigned(at<unsigned char>(p,0x46C))<<",\"air_flags\":"<<unsigned(at<unsigned char>(p,0x46D))<<",\"native_task\":"<<nikoTask<<"}";return 0;
    }
    auto*m=taskManager(p);if(!m)return -2;
    auto*current=physicalTask(p);
    if(mode==56){if(routeTask&&current==routeTask)reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(m,nullptr,0,true);routeTask=nullptr;return 0;}
    const V here=position(p);if(here.x<2230||here.x>2460||here.y<-1685||here.y>-1630)return -3;
    if(current&&current!=routeTask)return -4;
    if(mode==50&&scriptMissionActive())return -1;
    const float x[6]={2390,2330,2270,2330,2390,2430};
    const bool curb=mode>=58&&mode<=61;
    if(!curb&&(mode<50||mode>55))return -5;
    V destination=curb?V{2430.f,mode%2==0?-1645.f:-1658.f,here.z}:V{x[mode-50],-1658.f,here.z};
    void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x30);if(!t)return -6;
    reinterpret_cast<void*(__thiscall*)(void*,int,const V&,float,bool,bool)>(0x667CD0)(t,curb&&mode<60?4:6,destination,curb?.25f:1.f,true,false);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(m,t,0,true);routeTask=t;
    event("probe_streaming_waypoint",reference(p),0,"x="+std::to_string(destination.x)+" y="+std::to_string(destination.y)+" mode="+std::to_string(mode));return 0;
}
