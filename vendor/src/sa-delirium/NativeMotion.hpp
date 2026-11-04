#pragma once
// CPed::Teleport (5E4110) flushes LeaveCar task trees. Physics must update
// world membership without that gameplay side effect. CWorld Add/Remove and
// CEntity reference cleanup are verified in the SDK and native Ped.cpp.
static unsigned bodyRelocations=0,relocationTaskChanges=0;
static bool movePhysicalRoot(void*p,V destination,bool playerVehicle=false){
    if(!p||(p==player()&&!playerVehicle)||!matrix(p)||!finite(destination))return false;
    void* intelligence=at<void*>(p,0x47C);void* tasks=intelligence?static_cast<unsigned char*>(intelligence)+4:nullptr;
    void* before[11]{};if(tasks)for(int i=0;i<11;i++)before[i]=at<void*>(tasks,i*4);
    reinterpret_cast<void(__cdecl*)(void*)>(0x563280)(p);
    matrix(p)->pos=destination;at<unsigned char>(p,0x46C)&=~1u;
    if(void* damageEntity=at<void*>(p,0xDC))
        reinterpret_cast<void(__thiscall*)(void*,void**)>(0x571A00)(damageEntity,&at<void*>(p,0xDC));
    at<void*>(p,0xDC)=nullptr;
    reinterpret_cast<void(__cdecl*)(void*)>(0x563220)(p);
    at<V>(p,0x44)=V{};at<V>(p,0x50)=V{};
    if(tasks)for(int i=0;i<11;i++)if(before[i]!=at<void*>(tasks,i*4)){relocationTaskChanges++;break;}
    bodyRelocations++;return true;
}
