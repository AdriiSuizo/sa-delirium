#pragma once
static float aimZoomOffset=0,appliedZoom=0;static void*zoomCam=nullptr;
static bool cameraQuery=false;
static void* activeCamera(){void*c=reinterpret_cast<void*>(0xB6F028);unsigned i=at<unsigned char>(c,0x59);return i<3?static_cast<unsigned char*>(c)+0x174+i*0x238:nullptr;}
static bool aimCamera(void*cam){if(!cam)return false;int mode=at<unsigned short>(cam,0xC);return mode==0x35||mode==0x37||mode==0x41||mode==0x24||mode==0xB;}
static void updateAimWheel(int wheel){
    if(!wheel||!aimWheelZoom||!playing()||cutscene()||!aimCamera(activeCamera()))return;
    aimZoomOffset=std::clamp(aimZoomOffset-float(wheel)*5.f,-25.f,20.f);
    event("aim_wheel_zoom",reference(player()),0,"offset_degrees="+std::to_string(aimZoomOffset));
}
static void(__thiscall* originalMouseUpdate)(void*)=nullptr;
static void consumeAimMouse(unsigned char*mouse){
    if(aimWheelZoom&&playing()&&!cutscene()&&aimCamera(activeCamera())){
        updateAimWheel((mouse[3]?1:0)-(mouse[4]?1:0));mouse[3]=mouse[4]=0;
    }
}
static void __fastcall mouseUpdate(void*pad,void*){
    originalMouseUpdate(pad);
    consumeAimMouse(reinterpret_cast<unsigned char*>(0xB73418));
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeAimWheel(int wheel){
    auto*cam=activeCamera();if(!recoveryAllowed()||!aimCamera(cam))return -1;
    unsigned char sample[0x14]{};sample[3]=wheel>0;sample[4]=wheel<0;consumeAimMouse(sample);
    std::ofstream f(folder/"aim-probe.json");f<<"{\"pid\":"<<GetCurrentProcessId()<<",\"mode\":"<<at<unsigned short>(cam,0xC)<<",\"fov\":"<<at<float>(cam,0xB4)<<",\"offset\":"<<aimZoomOffset<<",\"wheel_consumed\":"<<(!sample[3]&&!sample[4]?"true":"false")<<'}';
    return 0;
}
static void(__thiscall* originalBonePosition)(void*,V&,unsigned,bool)=nullptr;
static void __fastcall bonePosition(void*ped,void*,V&out,unsigned bone,bool update){
    if(cameraQuery){auto it=agents.find(reference(ped));if(it!=agents.end()&&it->second->ped==ped&&!it->second->lastRendered.empty()){
        const auto&a=*it->second;int index=a.rig->index(int(bone));if(index>=0){out=a.lastRendered[index].p;return;}
    }}
    originalBonePosition(ped,out,bone,update);
}
static void(__thiscall* originalCameraProcess)(void*)=nullptr;
static void __fastcall cameraProcess(void*camera,void*){
    // Restore the unmodified input before native smoothing; avoid accumulating
    // our offset every frame. Native camera and wall obstruction remain owners.
    if(zoomCam)at<float>(zoomCam,0xB4)-=appliedZoom;
    appliedZoom=0;zoomCam=nullptr;
    cameraQuery=true;{NativeHitScope scope;originalCameraProcess(camera);}cameraQuery=false;
    void*cam=activeCamera();
    if(aimWheelZoom&&playing()&&!cutscene()&&aimCamera(cam)){
        const float base=at<float>(cam,0xB4);const float adjusted=std::clamp(base+aimZoomOffset,25.f,80.f);
        appliedZoom=adjusted-base;zoomCam=cam;at<float>(cam,0xB4)=adjusted;
        reinterpret_cast<void(__cdecl*)(float)>(0x6FF410)(adjusted);
    }
}
