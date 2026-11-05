// Test bridge only: exercise a shipping adapter through SA's native damage
// event. No calls to Delirium exports, pending reactions or solver state.
// ABI sources: verified vendor Native.hpp, Adapter.cpp ProbeGameDamage and
// VehicleProbe.hpp recoveryFixture; these are the existing SA1.0 entries.
struct NativeImpactVec { float x,y,z; };
struct NativeImpactPool { unsigned char* objects; unsigned char* flags; int size; };
static int nativeImpactRef=-1;
static bool nativeImpactReady();
static void nativeAmmoStatus(){
    if(!nativeImpactReady()){return;}
    // plugin-sdk CPed::m_aWeapons+0x5A0, CWeapon size0x1C,
    // pistol slot2, CWeapon::m_nTotalAmmo+0xC. Read only.
    result="native_ammo_"+std::to_string(field<unsigned>(player(),0x5A0+2*0x1C+0xC));
}
static void* nativeImpactPed(){
    auto* pool=*reinterpret_cast<NativeImpactPool**>(0xB74490);
    int index=nativeImpactRef>>8;
    if(!pool||nativeImpactRef<0||index<0||index>=pool->size||
       (pool->flags[index]&0x80)||pool->flags[index]!=(nativeImpactRef&255))return nullptr;
    return pool->objects+index*0x7C4;
}
static bool nativeImpactReady(){
    auto* p=player();
    const bool ready=testSession&&passiveWindow&&gameState()==9&&p&&!menu().m_bMenuActive&&
        field<float>(p,0x540)>0&&field<unsigned char>(p,0x2F)==0&&
        !reinterpret_cast<bool(__cdecl*)()>(0x464D50)()&&
        !*reinterpret_cast<bool*>(0xB5F851)&&!*reinterpret_cast<int*>(0x96A7CC);
    if(!ready)result="native_impact_boundary session="+std::to_string(testSession)+" passive="+std::to_string(passiveWindow)+
        " area="+std::to_string(p?field<unsigned char>(p,0x2F):-1)+" cutscene="+std::to_string(*reinterpret_cast<bool*>(0xB5F851))+
        " enex="+std::to_string(*reinterpret_cast<int*>(0x96A7CC));
    return ready;
}
static void prepareNativeImpact(){
    if(!nativeImpactReady())return;
    if(nativeImpactRef>=0){result="native_impact_already_created";return;}
    auto* matrix=field<unsigned char*>(player(),0x14);
    if(!matrix){result="native_impact_no_matrix";return;}
    auto start=*reinterpret_cast<NativeImpactVec*>(matrix+0x30);
    start.x-=4;start.y-=4;
    bool found=false;void* support=nullptr;
    float floor=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(start.x,start.y,start.z+4,&found,&support);
    if(!found||!std::isfinite(floor)||std::abs(floor-start.z)>3){result="native_impact_no_floor";return;}
    start.z=floor+1;
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    auto* ped=reinterpret_cast<void*(__cdecl*)(int,unsigned,const NativeImpactVec&,bool)>(0x612710)(4,7,start,false);
    auto* pool=*reinterpret_cast<NativeImpactPool**>(0xB74490);
    if(!ped||!pool){result="native_impact_spawn_refused";return;}
    auto offset=reinterpret_cast<unsigned char*>(ped)-pool->objects;
    if(offset<0||offset%0x7C4||offset/0x7C4>=pool->size){result="native_impact_invalid_pool";return;}
    int index=int(offset/0x7C4);
    nativeImpactRef=(index<<8)|pool->flags[index];
    NativeImpactVec look{start.x,start.y,start.z+.2f},source{look.x-3,look.y-4,look.z+1.4f},up{};
    auto* camera=reinterpret_cast<void*>(0xB6F028);
    reinterpret_cast<void(__thiscall*)(void*,const NativeImpactVec*,const NativeImpactVec*)>(0x50BEC0)(camera,&source,&up);
    reinterpret_cast<void(__thiscall*)(void*,const NativeImpactVec*,short,int)>(0x50C8B0)(camera,&look,2,1);
    result="native_impact_result_"+std::to_string(nativeImpactRef);
}
static void fireNativeImpact(){
    auto* ped=nativeImpactPed();
    if(!nativeImpactReady()||!ped||field<float>(ped,0x540)<=0){result="native_impact_not_ready";return;}
    reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(ped,player(),22,12,3,0);
    result="native_impact_result_"+std::to_string(nativeImpactRef);
}
