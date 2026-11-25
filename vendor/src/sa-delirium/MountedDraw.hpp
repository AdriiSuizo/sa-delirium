// Inside player_pose_draw. SilentPatch draws the solid weapon during CPed::Render
// and uses SA's late equipment pass for the muzzle flash. Submit the equipped
// mesh alongside the posed body, while that camera and its lighting are active.
using SubmitEquipment=void(__cdecl*)(void*);
static SubmitEquipment originalEquipment=nullptr;
static void* currentView(){
    void* engine=*reinterpret_cast<void**>(0xC97B24);
    return engine?at<void*>(engine,0):nullptr;
}
struct SubmittedWeapon {
    void *ped=nullptr,*vehicle=nullptr,*view=nullptr;
    unsigned frame=0;
    int ref=-1,weapon=-1;
};
static thread_local SubmittedWeapon submitted{};
static void __cdecl normalEquipment(void* p){
    if(p&&p==submitted.ped&&submitted.frame==frame()&&submitted.view==currentView()&&
       submitted.ref==reference(p)&&at<void*>(p,0x58C)==submitted.vehicle&&combatWeapon(p)==submitted.weapon){
        submitted={};return;
    }
    originalEquipment(p);
}
static bool ownEquipmentSite(){
    // SilentPatch installs its renderer during startup. Chain its actual early
    // submission after loading, not the executable's original enqueue address.
    constexpr uintptr_t site=0x5E7859;
    if(mountedInstalled)return true;
    const auto* op=reinterpret_cast<const unsigned char*>(site);
    int32_t rel=0;std::memcpy(&rel,op+1,sizeof(rel));
    if(op[0]==0xE8&&site+5+rel==reinterpret_cast<uintptr_t>(&normalEquipment))return true;
    static unsigned lastTry=~0u;
    if(lastTry!=~0u&&frame()-lastTry<60)return false;
    lastTry=frame();
    const Site points[]={{site,0x5E46D0,uintptr_t(&normalEquipment),reinterpret_cast<void**>(&originalEquipment)}};
    const bool ok=installSites(points);
    event(ok?"mounted_body_weapon_hook_installed":"mounted_body_weapon_hook_unavailable");
    return ok;
}
struct MeshParts {void* flash;unsigned meshes=0;};
static void* __cdecl countMesh(void* atomic,void* data){
    auto& parts=*static_cast<MeshParts*>(data);
    if(atomic&&atomic!=parts.flash&&(at<unsigned char>(atomic,2)&4)&&at<void*>(atomic,0x18)&&at<void*>(atomic,0x48))
        ++parts.meshes;
    return atomic;
}
static unsigned meshCount(void* clump,void* flash){
    MeshParts parts{flash};
    reinterpret_cast<void*(__cdecl*)(void*,decltype(&countMesh),void*)>(0x749B70)(clump,&countMesh,&parts);
    return parts.meshes;
}
static void* flashAtomic(void* flash){
    void* object=flash?reinterpret_cast<void*(__cdecl*)(void*)>(0x7348C0)(flash):nullptr;
    return object&&at<unsigned char>(object,0)==1?object:nullptr;
}
static void drawFlash(void* p,void* flash,void* atomic){
    if(!atomic||!at<void*>(atomic,0x48)||at<short>(p,0x504)<=0)return;
    void* engine=*reinterpret_cast<void**>(0xC97B24);
    if(!engine)return;
    using RWState=int(__cdecl*)(unsigned,void*);
    auto get=at<RWState>(engine,0x24),set=at<RWState>(engine,0x20);
    unsigned zWrite=0;
    if(!get||!set||!get(8,&zWrite))return;
    void*& attached=at<void*>(p,0x4F8);
    void* saved=attached;
    attached=flash;
    reinterpret_cast<void(__thiscall*)(void*,bool)>(0x5DF400)(p,false);
    if(at<unsigned char>(atomic,2)&4){
        set(8,nullptr);
        at<void*(__cdecl*)(void*)>(atomic,0x48)(atomic);
        set(8,reinterpret_cast<void*>(zWrite));
    }
    reinterpret_cast<void(__thiscall*)(void*)>(0x5DF4E0)(p);
    attached=saved;
}

// Use the real Direct3D depth test during an opt-in validation session. Poll
// results on a later frame without flushing or blocking the game's pipeline.
struct MeshQuery {
    IDirect3DQuery9* query=nullptr;
    unsigned next=0;
    int weapon=-1,model=-1;
    bool bike=false;
    bool begin(int w,int m,bool b){
        static const bool enabled=GetEnvironmentVariableA("GTASA_MOUNT_TEST",nullptr,0)!=0;
        if(!enabled)return false;
        if(query){
            DWORD samples=0;
            HRESULT hr=query->GetData(&samples,sizeof(samples),0);
            if(hr==S_FALSE)return false;
            if(hr==S_OK)event("mounted_model_gpu_samples",-1,model,std::string("vehicle=")+(bike?"bike":"car")+
                " weapon="+std::to_string(weapon)+" samples="+std::to_string(samples));
            query->Release();query=nullptr;
        }
        unsigned now=gameTime();
        if(w==weapon&&b==bike&&next&&int(now-next)<0&&next-now<=1000)return false;
        next=now+1000;weapon=w;model=m;bike=b;
        auto* device=*reinterpret_cast<IDirect3DDevice9**>(0xC97C28);
        if(!device||FAILED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query)))return false;
        if(FAILED(query->Issue(D3DISSUE_BEGIN))){query->Release();query=nullptr;return false;}
        return true;
    }
    void end(){if(FAILED(query->Issue(D3DISSUE_END))){query->Release();query=nullptr;}}
};
static MeshQuery meshQuery;
static void weaponStatus(const char* status,int model,unsigned meshes){
    static const char* last=nullptr;
    static unsigned lastTime=0;
    static int lastWeapon=-1;
    static bool lastBike=false;
    const bool bike=seated.mode==Mode::Bike;
    const unsigned now=gameTime();
    if(last==status&&lastWeapon==seated.weapon&&lastBike==bike&&now-lastTime<5000)return;
    last=status;lastTime=now;lastWeapon=seated.weapon;lastBike=bike;
    event("mounted_body_weapon",seated.ref,model,std::string("mode=")+(bike?"bike":"car")+
        " weapon="+std::to_string(seated.weapon)+" result="+status+" meshes="+std::to_string(meshes));
}
static void drawMountedEquipment(){
    submitted={};
    mountedInstalled=ownEquipmentSite();
    if(!mountedInstalled)return;
    void* p=seated.ped;
    void* car=at<void*>(p,0x58C);
    if(!sampleWeapon)if(auto dll=GetModuleHandleW(L"IVDriverPose.asi"))
        sampleWeapon=reinterpret_cast<WeaponFrame>(GetProcAddress(dll,"IVVehicleWeaponFrame"));
    M grip{};
    if(!sampleWeapon||!sampleWeapon(p,car,seated.weapon,&grip)||!finite(grip.pos)||!finite(grip.right)||
       !finite(grip.up)||!finite(grip.at)){weaponStatus("no_grip",-1,0);return;}
    const int model=mounted_model::idFor(seated.weapon);
    if(model<0){weaponStatus("model_info_missing",model,0);return;}
    mounted_model::Borrowed fallback;
    void* held=at<void*>(p,0x4F4);
    void* clump=mounted_model::matches(held,model)?held:nullptr;
    void* flash=clump?mounted_model::flashFor(clump):nullptr;
    void* spark=flashAtomic(flash);
    unsigned meshes=clump?meshCount(clump,spark):0;
    if(!meshes){
        clump=fallback.get(model);
        if(!clump){weaponStatus("waiting_for_model",model,0);return;}
        flash=mounted_model::flashFor(clump);spark=flashAtomic(flash);
        meshes=meshCount(clump,spark);
        if(!meshes){weaponStatus("no_visible_atomics",model,0);return;}
    }
    void* root=at<void*>(clump,4);
    auto update=reinterpret_cast<void*(__cdecl*)(void*)>(0x7F0910);
    M& transform=at<M>(root,0x10);
    const M previous=transform;
    transform=grip;update(root);
    unsigned char flashFlags=0;
    if(spark){flashFlags=at<unsigned char>(spark,2);at<unsigned char>(spark,2)&=~4u;}
    bool measuring=meshQuery.begin(seated.weapon,model,seated.mode==Mode::Bike);
    void* result=reinterpret_cast<void*(__cdecl*)(void*)>(0x749B20)(clump);
    if(measuring)meshQuery.end();
    if(spark)at<unsigned char>(spark,2)=flashFlags;
    if(result){
        drawFlash(p,flash,spark);
        submitted={p,car,currentView(),frame(),seated.ref,seated.weapon};
    }
    if(clump==held){transform=previous;update(root);}
    weaponStatus(result?"submitted":"clump_rejected",model,meshes);
}
