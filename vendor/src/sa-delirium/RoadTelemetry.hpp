#pragma once
// Short automatic windows around real road contacts. No input or physics writes.
struct RoadWatch {unsigned until=0,sampled=0;V hullImpulse{},hullTorque{};int ped=-1;};
static std::map<int,RoadWatch> roadWatches;
static int roadProbePed=-1;static bool roadProbeCaptured=false;
static int roadProbeVehicle=-1;static V roadProbeSide{},roadProbeForward{};static unsigned roadProbeCameraUntil=0;
static bool roadProbeCamera(V&look,V&source){
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||gameTime()>roadProbeCameraUntil||!recoveryAllowed())return false;
    void*p=pedFromRef(roadProbePed),*car=vehicleFromRef(roadProbeVehicle);if(!p||!car||!matrix(car))return false;
    V body=position(p);if(auto a=agents.find(roadProbePed);a!=agents.end())body=a->second->current[10].p;
    const V centre=position(car)+V{0,0,.5f};look=(body+centre)*.5f;
    const float span=length(body-centre),distance=std::max(7.f,span*.75f+4.f);
    source=look+roadProbeSide*distance+roadProbeForward*1.f+V{0,0,2.5f};return true;
}
static void updateRoadProbeCamera(){
    V look{},source{},up{};if(!roadProbeCamera(look,source))return;
    reinterpret_cast<void(__thiscall*)(void*,const V*,const V*)>(0x50BEC0)(reinterpret_cast<void*>(0xB6F028),&source,&up);
    reinterpret_cast<void(__thiscall*)(void*,const V*,short,int)>(0x50C8B0)(reinterpret_cast<void*>(0xB6F028),&look,2,1);
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeFrame();
static std::vector<unsigned> roadFrames;
static void queueRoadFrames(int ref){
    if(ref!=roadProbePed||roadProbeCaptured)return;roadProbeCaptured=true;
    for(unsigned delay:{17u,50u,100u,150u,250u,400u,600u,900u})roadFrames.push_back(gameTime()+delay);
}
static void captureRoadFrames(){
    if(!recoveryAllowed()){roadFrames.clear();return;}
    updateRoadProbeCamera();
    if(!roadFrames.empty()&&gameTime()>=roadFrames.front()){
        const int result=DeliriumSA_ProbeFrame();roadFrames.erase(roadFrames.begin());
        event("probe_road_contact_frame",roadProbePed,-1,"capture_ms="+std::to_string(gameTime())+" result="+std::to_string(result));
    }
}
static void watchRoadVehicle(int ref,int ped=-1){
    if(ref<0)return;
    if(!roadWatches.contains(ref)&&roadWatches.size()>=12)roadWatches.erase(roadWatches.begin());
    roadWatches[ref].until=gameTime()+4000;
    if(ped>=0)roadWatches[ref].ped=ped;
}
static void recordRoadHull(int ref,V impulse,V torque){
    auto i=roadWatches.find(ref);if(i==roadWatches.end())return;
    i->second.hullImpulse=i->second.hullImpulse+impulse;i->second.hullTorque=i->second.hullTorque+torque;
}
static void traceRoadVehicles(){
    const unsigned now=gameTime();
    if(!recoveryAllowed()){roadWatches.clear();return;}
    for(auto i=roadWatches.begin();i!=roadWatches.end();){
        void*car=vehicleFromRef(i->first);auto&w=i->second;
        if(!car||!matrix(car)||now>w.until){i=roadWatches.erase(i);continue;}
        if(now-w.sampled>=50){w.sampled=now;const V v=at<V>(car,0x44)*50.f,s=at<V>(car,0x50)*50.f;
            std::string body;
            if(auto a=agents.find(w.ped);a!=agents.end()){
                const V local=mul(inverse(transform(*matrix(car))),a->second->current[10]).p;
                body=" body_ref="+std::to_string(w.ped)+" chest_x="+std::to_string(local.x)+" chest_y="+std::to_string(local.y)+" chest_z="+std::to_string(local.z);
            }
            event("road_car_motion",-1,at<short>(car,0x22),"vehicle_ref="+std::to_string(i->first)+
                " speed_mps="+std::to_string(length(v))+" vz="+std::to_string(v.z)+" z="+std::to_string(position(car).z)+
                " nose_degrees="+std::to_string(std::asin(std::clamp(matrix(car)->up.z,-1.f,1.f))*57.29578f)+
                " spin_x="+std::to_string(s.x)+" spin_y="+std::to_string(s.y)+" spin_z="+std::to_string(s.z)+
                " hull_jx="+std::to_string(w.hullImpulse.x)+" hull_jy="+std::to_string(w.hullImpulse.y)+" hull_jz="+std::to_string(w.hullImpulse.z)+
                " hull_tx="+std::to_string(w.hullTorque.x)+" hull_ty="+std::to_string(w.hullTorque.y)+" hull_tz="+std::to_string(w.hullTorque.z)+body);
            w.hullImpulse={};w.hullTorque={};
        }++i;
    }
}
