#pragma once
#include "VisibleLegPose.hpp"

namespace player_pose_draw {
enum class Mode { None, Cover, LowCover, Duck, Car, Bike, Walk };
static bool installed=false,mountedInstalled=false;
static void drawMountedEquipment();
static bool lowCover(void* ped);
static unsigned frame();

struct BodyPose {
    Mode mode=Mode::None;
    void *ped=nullptr,*clump=nullptr;
    Hierarchy* hierarchy=nullptr;
    M* palette=nullptr;
    int ref=-1,vehicle=-1,weapon=-1,count=0;
    unsigned frame=0;
    M root{},inverseRoot{};
    std::array<M,128> bones{};
};
static BodyPose ground,seated;
struct ArmedSnapshot {
    VisibleLegPose legs;
    unsigned frame=0,time=0;
    void* clump=nullptr;Hierarchy* h=nullptr;Node* nodes=nullptr;M* palette=nullptr;
    int count=0,ref=-1,model=-1,weapon=-1;
};
static std::map<void*,ArmedSnapshot> armed;
static void rememberArmed(void* ped,int ref){
    if(!installed)return;
    auto* h=hierarchy(ped);
    if(!h)return;
    auto& s=armed[ped];
    if(!s.legs.capture(*h)){armed.erase(ped);return;}
    s.frame=frame();s.time=gameTime();s.clump=at<void*>(ped,0x18);s.h=h;s.nodes=h->nodes;
    s.palette=h->matrices;s.count=h->num;s.ref=ref;s.model=at<short>(ped,0x22);s.weapon=combatWeapon(ped);
}
static void pruneArmed(unsigned now){
    for(auto it=armed.begin();it!=armed.end();)
        if(now-it->second.time>500)it=armed.erase(it);else ++it;
}
static bool publishArmed(void* p){
    const auto it=armed.find(p);if(it==armed.end())return false;
    const auto& s=it->second;
    if(s.frame!=frame()||at<void*>(p,0x18)!=s.clump||reference(p)!=s.ref||at<short>(p,0x22)!=s.model||
       combatWeapon(p)!=s.weapon||!armed_step::visible(p,s.ref,s.frame))return false;
    auto* h=hierarchy(p);
    if(h!=s.h||!h||h->nodes!=s.nodes||h->matrices!=s.palette||h->num!=s.count)return false;
    s.legs.publish(s.palette);return true;
}
static std::array<M,128> seatedInput;
static bool supported(Hierarchy* h){
    return h&&h->nodes&&h->matrices&&h->num>=8&&h->num<=128;
}
static bool sameRoot(const M& a,const M& b){
    return std::memcmp(&a.right,&b.right,sizeof(V))==0&&std::memcmp(&a.up,&b.up,sizeof(V))==0&&
        std::memcmp(&a.at,&b.at,sizeof(V))==0&&std::memcmp(&a.pos,&b.pos,sizeof(V))==0;
}
static bool identity(const BodyPose& s,void* p,int ref,Hierarchy* h){
    return s.mode!=Mode::None&&s.ped==p&&s.ref==ref&&s.clump==at<void*>(p,0x18)&&
        s.hierarchy==h&&h&&s.count==h->num&&s.palette==h->matrices;
}
static bool capture(BodyPose& s,Mode mode,void* p,int ref,Hierarchy* h){
    s.mode=Mode::None;
    if(!supported(h)||!matrix(p)||!finite(matrix(p)->pos)||!affineInverse(*matrix(p),s.inverseRoot))return false;
    for(int i=0;i<h->num;++i){
        const M& m=h->matrices[i];
        if(!finite(m.pos)||!finite(m.up)||!finite(m.right)||!finite(m.at))return false;
    }
    s.ped=p;s.ref=ref;s.clump=at<void*>(p,0x18);s.hierarchy=h;s.palette=h->matrices;s.count=h->num;
    s.frame=frame();s.root=*matrix(p);s.mode=mode;
    std::copy_n(h->matrices,h->num,s.bones.begin());
    return true;
}
static void rememberGroundPose(void* p,int ref,bool cover){
    if(!installed)return;
    capture(ground,cover?(lowCover(p)?Mode::LowCover:Mode::Cover):Mode::Duck,p,ref,hierarchy(p));
}
static bool publish(const BodyPose& s,const M& root,M* dest){
    if(sameRoot(s.root,root)){
        if(std::memcmp(dest,s.bones.data(),s.count*sizeof(M))==0)return false;
        std::copy_n(s.bones.begin(),s.count,dest);
    }else{
        const M rebase=affineProduct(root,s.inverseRoot);
        for(int i=0;i<s.count;++i)dest[i]=affineProduct(rebase,s.bones[i]);
    }
    return true;
}
static void sampleSeated(void* p,int ref){
    if(!installed){drawVehicleAim(p,ref);return;}
    void* car=at<void*>(p,0x58C);
    if(!car||agents.contains(ref)||pending.contains(ref))return;
    const int vehicle=vehicleRef(car),weapon=combatWeapon(p);
    if(vehicle<0)return;
    // A visible preceding frame already provides a car-relative muzzle for this
    // update. Offscreen and first-frame shots still get a fresh seat sample.
    auto* h=hierarchy(p);
    if(!supported(h)||(identity(seated,p,ref,h)&&frame()-seated.frame<=1&&
        seated.vehicle==vehicle&&seated.weapon==weapon))return;
    float direction[3],weight=0;
    if(!DeliriumVehicleAimPose(p,car,direction,&weight))return;
    std::array<M,128> original;
    std::copy_n(h->matrices,h->num,original.begin());
    drawVehicleAim(p,ref);
    std::copy_n(original.begin(),h->num,h->matrices);
}
static void logDraw(const BodyPose& s,bool republished){
    static unsigned next[7]{};
    const unsigned mode=unsigned(s.mode),now=gameTime();
    if(mode==0||mode>=7||(next[mode]&&int(now-next[mode])<0&&next[mode]-now<=5000))return;
    next[mode]=now+5000;
    static constexpr const char* names[]={"none","cover","low_cover","crouch","car","bike","walk"};
    std::string detail=std::string("mode=")+names[mode]+" frame="+std::to_string(frame())+
        " republished="+std::to_string(republished)+" bones="+std::to_string(s.count);
    for(int i=0;i<s.count;++i){
        const int id=s.hierarchy->nodes[i].id;
        if(id==2||id==5||id==24||id==34)
            detail+=" z"+std::to_string(id)+"="+std::to_string(s.palette[i].pos.z-matrix(s.ped)->pos.z);
    }
    event("player_visible_body_pose",s.ref,at<short>(s.ped,0x22),detail);
}
using EntityRender=void(__thiscall*)(void*);
static EntityRender nativeRender[2]{};
static thread_local bool insideSeatedDraw=false;
template<int Site> static void __fastcall render(void* p,void*){
    const auto original=nativeRender[Site];
    if(ready && p && !insideSeatedDraw)observeInteriorPed(p);
    if(ready&&p&&!insideSeatedDraw&&publishArmed(p)){original(p);return;}
    if(!ready||!p||p!=player()||insideSeatedDraw||!matrix(p)){original(p);return;}
    const int ref=reference(p);
    if(ref<0||agents.contains(ref)||pending.contains(ref)){original(p);return;}
    if(!inVehicle(p)){
        if(ground.frame==frame()&&identity(ground,p,ref,hierarchy(p))){
            if(native_stealth::ownsPose(p)){
                // The native task can start after the crouch snapshot was
                // captured. Republish SA's evaluated bones without ticking it.
                publishCurrentNativePalette(p);
                ground.mode=Mode::None;
            }else if(ground.mode!=Mode::Walk||(taskManager(p)&&!at<void*>(taskManager(p),0x14)&&combatWeapon(p)<=1)){
                const bool changed=publish(ground,*matrix(p),ground.palette);
                logDraw(ground,changed);
            }
        }
        original(p);return;
    }
    auto* car=at<void*>(p,0x58C);
    float dir[3],weight=0;
    if(!DeliriumVehicleAimPose(p,car,dir,&weight)){original(p);return;}
    auto* h=hierarchy(p);
    if(!supported(h)){original(p);return;}
    const int vehicle=vehicleRef(car),weapon=combatWeapon(p);
    const size_t bytes=h->num*sizeof(M);
    // A second mirror/shadow pass may use another seated basis. Reuse only
    // when both the actual incoming palette and the entity transform match.
    if(identity(seated,p,ref,h)&&seated.frame==frame()&&seated.vehicle==vehicle&&
       seated.weapon==weapon&&sameRoot(seated.root,*matrix(p))&&
       std::memcmp(h->matrices,seatedInput.data(),bytes)==0){
        std::copy_n(seated.bones.begin(),h->num,h->matrices);
    }else{
        std::copy_n(h->matrices,h->num,seatedInput.begin());
        if(!drawVehicleAim(p,ref)||!capture(seated,ridingBike(p)?Mode::Bike:Mode::Car,p,ref,h)){
            seated.mode=Mode::None;
            std::copy_n(seatedInput.begin(),h->num,h->matrices);
            original(p);return;
        }
        seated.vehicle=vehicle;seated.weapon=weapon;
    }
    logDraw(seated,false);
    insideSeatedDraw=true;original(p);insideSeatedDraw=false;
    drawMountedEquipment();
    // Seat animation and the next frame's IK must never ingest our previous
    // arm rotation. The deferred equipment pass has its own sampled grip.
    std::copy_n(seatedInput.begin(),h->num,h->matrices);
}

using MatrixArray=M*(__cdecl*)(Hierarchy*);
using WeaponInfo=void*(__cdecl*)(int,int);
using WeaponFrame=int(__cdecl*)(void*,void*,int,M*);
static MatrixArray nativeArray=nullptr;
static WeaponInfo nativeInfo=nullptr;
static WeaponFrame sampleWeapon=nullptr;
static thread_local bool singleGun=false;
static M* __cdecl equipmentPalette(Hierarchy* h){
    singleGun=false;
    M* original=nativeArray(h);
    void* p=player();
    if(!ready||mountedInstalled||!original||!p||!h||!inVehicle(p)||!at<void*>(p,0x4F4))return original;
    if(seated.frame!=frame()||!identity(seated,p,reference(p),h))return original;
    auto* car=at<void*>(p,0x58C);
    if(!car||vehicleRef(car)!=seated.vehicle||combatWeapon(p)!=seated.weapon)return original;
    if(!sampleWeapon)if(auto dll=GetModuleHandleW(L"IVDriverPose.asi"))
        sampleWeapon=reinterpret_cast<WeaponFrame>(GetProcAddress(dll,"IVVehicleWeaponFrame"));
    M grip{};
    if(!sampleWeapon||!sampleWeapon(p,car,seated.weapon,&grip)||!finite(grip.pos)||!finite(grip.right)||!finite(grip.up)||!finite(grip.at))return original;
    for(int i=0;i<h->num;++i)if(h->nodes[i].id==24){
        // Only the primary gun's matrix-array CALL in RenderWeaponPedsForPC
        // sees this buffer. The actual right hand stays on the body/steering.
        static std::array<M,128> equipment;
        std::copy_n(original,h->num,equipment.begin());equipment[i]=grip;
        singleGun=true;
        return equipment.data();
    }
    return original;
}
static void* __cdecl equipmentWeaponInfo(int weapon,int skill){
    void* info=nativeInfo(weapon,skill);
    const bool single=singleGun;singleGun=false;
    constexpr unsigned twinPistol=1u<<11;
    if(!single||!info||!(at<unsigned>(info,0x18)&twinPistol))return info;
    // This one native read decides whether to draw the second pistol. Keep the
    // shared CWeaponInfo and every character's on-foot skills intact.
    alignas(4) static unsigned char drawInfo[0x70];
    std::memcpy(drawInfo,info,sizeof(drawInfo));at<unsigned>(drawInfo,0x18)&=~twinPistol;
    return drawInfo;
}



struct Site{uintptr_t at,target,replacement;void** chain;};
template<size_t N> static bool installSites(const Site (&sites)[N]){
    // Each caller is verified in the installed 1.0 US image. Preserve a
    // pre-existing direct CALL to another loaded ASI and restore every page's
    // protection, including the matrix-array CALL that straddles two pages.
    std::array<CodeRange,N> ranges{};
    std::array<std::array<unsigned char,5>,N> before{};
    std::map<uintptr_t,DWORD> pages;
    SYSTEM_INFO system{};GetSystemInfo(&system);
    for(unsigned i=0;i<N;++i){
        const auto& s=sites[i];auto* code=reinterpret_cast<const unsigned char*>(s.at);
        std::memcpy(before[i].data(),code,5);
        if(code[0]!=0xE8)return false;
        int32_t rel;std::memcpy(&rel,code+1,4);const uintptr_t target=s.at+5+rel;
        if(target!=s.target){
            HMODULE owner{};
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(target),&owner)||owner==GetModuleHandleW(nullptr)||target==s.replacement)return false;
        }
        *s.chain=reinterpret_cast<void*>(target);
        ranges[i]={s.at,s.at+5};
        for(uintptr_t p=s.at/system.dwPageSize*system.dwPageSize;p<=s.at+4;p+=system.dwPageSize)pages.emplace(p,0);
    }
    bool writable=true;
    for(auto& [page,old]:pages)if(!VirtualProtect(reinterpret_cast<void*>(page),system.dwPageSize,PAGE_EXECUTE_READWRITE,&old)){
        writable=false;break;
    }
    bool patched=false;
    if(writable){
        FrozenThreads frozen(ranges);
        patched=frozen.ok();
        for(unsigned i=0;patched&&i<N;++i)
            patched=std::memcmp(reinterpret_cast<const void*>(sites[i].at),before[i].data(),5)==0;
        if(patched)for(const auto& s:sites){
            const int32_t rel=int32_t(s.replacement-(s.at+5));
            std::memcpy(reinterpret_cast<void*>(s.at+1),&rel,4);
            FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(s.at),5);
        }
    }
    for(const auto& [page,old]:pages)if(old){
        DWORD unused;VirtualProtect(reinterpret_cast<void*>(page),system.dwPageSize,old,&unused);
    }
    return patched;
}
static bool install(){
    const Site sites[]={
        {0x5E77FC,0x534310,uintptr_t(&render<0>),reinterpret_cast<void**>(&nativeRender[0])},
        {0x5E780A,0x534310,uintptr_t(&render<1>),reinterpret_cast<void**>(&nativeRender[1])},
        {0x732FFF,0x7C5120,uintptr_t(&equipmentPalette),reinterpret_cast<void**>(&nativeArray)},
        {0x7330A9,0x743C60,uintptr_t(&equipmentWeaponInfo),reinterpret_cast<void**>(&nativeInfo)}
    };
    return installSites(sites);
}
#include "MountedDraw.hpp"
} // namespace player_pose_draw
