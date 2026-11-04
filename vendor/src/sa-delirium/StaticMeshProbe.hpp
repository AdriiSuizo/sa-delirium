#pragma once
// Opt-in real COL/body test: native floor and wall, one initial velocity,
// ordinary live response/contacts/recovery afterwards. No repeated body writes.
struct StaticMeshProbe {int ref=-1,phase=0,model=-1;V point{},normal{};unsigned created=0,started=0,last=0;float speed=0;};
static StaticMeshProbe staticMeshProbe;
static int staticMeshFixture(int mode){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!recoveryAllowed()||staticMeshProbe.phase||!agents.empty()||!pending.empty())return -232;
    const V from=position(player())+V{0,0,.3f};ColPoint wall{};void*entity=nullptr;
    if(!lineOfSight(from,from+V{-20,0,0},wall,entity,true,false,false,true,true,false,false,false)||!entity||std::abs(wall.normal.z)>.05f)return -233;
    V normal=normalized(wall.normal);if(dot(normal,from-wall.point)<0)normal=normal*-1.f;
    V place=wall.point+normal*1.1f;ColPoint floor{};void*support=nullptr;
    const V top=place+V{0,0,3};
    if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)(top,top.z-8,floor,support,true,false,false,true,true,false,nullptr))return -234;
    place.z=floor.point.z+1.f;
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    void*p=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,place,false);if(!p||!solidProbeIdle(p))return -235;
    const int ref=reference(p);watchSessionPed(p);const float heading=std::atan2(normal.x,-normal.y);
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(p,heading);at<float>(p,0x558)=at<float>(p,0x55C)=heading;
    staticMeshProbe={ref,1,at<short>(entity,0x22),wall.point,normal,gameTime(),0,0,mode==232?8.f:2.f};
    event("probe_static_mesh_setup",ref,7,"surface_model="+std::to_string(staticMeshProbe.model)+" point="+std::to_string(wall.point.x)+","+std::to_string(wall.point.y)+","+std::to_string(wall.point.z)+" normal="+std::to_string(normal.x)+","+std::to_string(normal.y)+","+std::to_string(normal.z)+" initial_mps="+std::to_string(staticMeshProbe.speed));return ref;
}
static void tickStaticMeshProbe(){
    auto&s=staticMeshProbe;if(!s.phase||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return;
    const unsigned now=gameTime();void*p=pedFromRef(s.ref);if(!p){s.phase=0;return;}
    if(s.phase==1&&now-s.created>=500&&animatedContacts.contains(s.ref)){
        Request r;r.test=true;r.kind=Reaction::Melee;r.time=now;r.direction=s.normal*-1.f;r.part=10;r.meleeContactReady=true;r.melee=MeleeResponse::Balance;r.velocityOverride=true;r.velocity=s.normal*(-s.speed/50.f);
        pending[s.ref]=r;s.phase=2;s.started=now;event("probe_static_mesh_queued",s.ref,7,"velocity_once=1 continuous_forces_unchanged=1");
    }
    if(s.phase<2||now==s.last)return;s.last=now;
    const auto it=agents.find(s.ref);float least=100,chest=100,pelvis=100;int part=-1;
    if(it!=agents.end())for(int i=0;i<21;i++){
        const float d=dot(contactPose(*it->second)[i].p-s.point,s.normal);if(d<least){least=d;part=i;}if(i==0)pelvis=d;if(i==10)chest=d;
    }
    event("probe_static_mesh_frame",s.ref,7,"elapsed_ms="+std::to_string(now-s.started)+" physical="+std::to_string(it!=agents.end())+" minimum_center="+std::to_string(least)+" part="+std::to_string(part)+" chest="+std::to_string(chest)+" pelvis="+std::to_string(pelvis)+" native_root="+std::to_string(dot(position(p)-s.point,s.normal)));
    if(now-s.started>12000){event("probe_static_mesh_finished",s.ref,7,"continuous_observation_ms=12000");s.phase=0;}
}
