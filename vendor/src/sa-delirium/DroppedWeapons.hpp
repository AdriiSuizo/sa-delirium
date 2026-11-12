#pragma once
#include <optional>

namespace dropped_weapons {
struct Drop {
    int handle;
    V hand, landing, spot;
    unsigned started;
    float yaw;
    float fall=0;
};
static std::vector<Drop> drops;
static void(__cdecl* originalPickupEffects)(void*)=nullptr;
static void(__thiscall* originalDeathPickups)(void*)=nullptr;
constexpr uintptr_t pickupArray=0x9788C0;
constexpr int pickupCount=620, pickupSize=0x20, timeoutPickup=4;

static void* pickup(int handle){
    if(handle==-1)return nullptr;
    const unsigned index=unsigned(handle)&0xffffu;
    if(index>=pickupCount)return nullptr;
    auto* entry=reinterpret_cast<void*>(pickupArray+index*pickupSize);
    const auto type=at<unsigned char>(entry,0x1C);
    return at<unsigned short>(entry,0x1A)==(unsigned(handle)>>16)&&(type==timeoutPickup||type==22)?entry:nullptr;
}
static std::optional<V> rightHand(void* ped){
    if(auto*h=hierarchy(ped);h&&h->num>0&&h->num<=256)for(int i=0;h->nodes&&h->matrices&&i<h->num;i++){
        constexpr int rightHand=24;
        const V candidate=h->matrices[i].pos;
        if(h->nodes[i].id==rightHand&&finite(candidate)&&length(candidate-position(ped))<2.f)return candidate;
    }
    return std::nullopt;
}
static bool spawn(void* ped,V hand,int type,unsigned ammo){
    V landing=hand+normalized(matrix(ped)->right)*.42f;
    bool found=false;
    const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(
        landing.x,landing.y,hand.z+.5f,&found,nullptr);
    landing.z=found&&std::isfinite(ground)&&ground<=hand.z?ground+.12f:position(ped).z-.55f;
    if(!finite(landing)||!finite(hand)||length(landing-hand)>4.f)return false;
    const auto generate=reinterpret_cast<int(__cdecl*)(V,int,int,unsigned,bool,char*)>(0x457380);
    const int handle=generate(hand,type,timeoutPickup,ammo,false,nullptr);
    if(!pickup(handle))return false;
    const V right=matrix(ped)->right;
    drops.push_back({handle,hand,landing,hand,gameTime(),std::atan2(right.y,right.x)});
    return true;
}
// Keep SA's death bookkeeping, pickup lifetime, merging and ammunition intact.
// Adopt only weapon pickups created/merged by this specific native death call.
static void __fastcall deathPickups(void* ped,void*){
    if(!ready||!ped||ped==player()||!matrix(ped)){originalDeathPickups(ped);return;}
    struct Before {unsigned short generation;unsigned char type;unsigned ammo;};
    std::array<Before,pickupCount> before{};
    for(int n=0;n<pickupCount;++n){
        auto* p=reinterpret_cast<void*>(pickupArray+n*pickupSize);
        before[n]={at<unsigned short>(p,0x1A),at<unsigned char>(p,0x1C),at<unsigned>(p,8)};
    }
    const V hand=rightHand(ped).value_or(position(ped));
    const V right=matrix(ped)->right;
    originalDeathPickups(ped);
    for(int n=0;n<pickupCount;++n){
        auto* p=reinterpret_cast<void*>(pickupArray+n*pickupSize);
        const unsigned generation=at<unsigned short>(p,0x1A);
        const int handle=int((generation<<16)|unsigned(n));
        if(!pickup(handle))continue;
        if(generation==before[n].generation&&at<unsigned char>(p,0x1C)==before[n].type&&
           at<unsigned>(p,8)==before[n].ammo)continue;
        if(std::any_of(drops.begin(),drops.end(),[&](const Drop& d){return d.handle==handle;}))continue;
        const auto* packed=&at<short>(p,0x10);
        V landing{packed[0]/8.f,packed[1]/8.f,packed[2]/8.f};
        bool found=false;
        const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(
            landing.x,landing.y,std::max(hand.z,landing.z)+.5f,&found,nullptr);
        if(!found||!std::isfinite(ground)||length(landing-position(ped))>4.f)continue;
        landing.z=ground+.075f;
        const V start=hand.z>=ground?hand:landing;
        drops.push_back({handle,start,landing,start,gameTime(),std::atan2(right.y,right.x)});
        event("death_weapon_dropped",reference(ped),at<short>(p,0x18),"pickup="+std::to_string(handle));
    }
}
static void tick(unsigned now){
    for(auto it=drops.begin();it!=drops.end();){
        void* entry=pickup(it->handle);
        if(!entry){it=drops.erase(it);continue;}
        if(it->fall>=1.f){++it;continue;}
        const float elapsed=float(now-it->started)*.001f;
        const float fallSeconds=std::clamp(std::sqrt(2.f*std::max(.04f,it->hand.z-it->landing.z)/9.81f),.16f,.8f);
        const float t=std::min(1.f,elapsed/fallSeconds);
        it->spot=it->hand+(it->landing-it->hand)*t;
        it->spot.z=it->hand.z+(it->landing.z-it->hand.z)*t*t;
        it->fall=t;
        auto* packed=&at<short>(entry,0x10);
        const auto encode=[](float value){return short(std::clamp(std::round(value*8.f),-32768.f,32767.f));};
        packed[0]=encode(it->spot.x);packed[1]=encode(it->spot.y);packed[2]=encode(it->spot.z);
        if(t>=1.f)
            event("shot_weapon_landed",-1,-1,"pickup="+std::to_string(it->handle)+" orange_glow=1");
        ++it;
    }
}
static void __cdecl pickupEffects(void* object){
    for(const auto& drop:drops){
        auto* entry=pickup(drop.handle);
        if(!entry||at<void*>(entry,4)!=object)continue;
        if(auto*m=matrix(object)){
            const float halfYaw=drop.yaw*.5f;
            const float halfRoll=drop.fall*.78539816f;
            write(*m,{mul(Q{0,0,std::sin(halfYaw),std::cos(halfYaw)},
                         Q{std::sin(halfRoll),0,0,std::cos(halfRoll)}),drop.spot});
        }
        const float pulse=.88f+.12f*std::sin(float(gameTime()%1200)*.005236f);
        const auto glow=reinterpret_cast<void(__cdecl*)(unsigned,void*,unsigned char,unsigned char,unsigned char,
            unsigned char,const V&,float,float,int,unsigned char,unsigned char,unsigned char,unsigned char,
            float,bool,float,bool,float,bool,bool)>(0x6FC580);
        glow(0xD5000000u+unsigned(drop.handle),nullptr,255,125,29,static_cast<unsigned char>(165.f*pulse),
             drop.spot+V{0,0,.08f},.34f*pulse,26.f,1,0,0,1,0,0.f,false,.05f,true,15.f,false,false);
        // Additive pool on the actual ground, in addition to the small corona.
        if(drop.fall>=1.f)if(auto* texture=*reinterpret_cast<void**>(0xC403F4)){
            reinterpret_cast<void(__cdecl*)(unsigned char,void*,const V&,float,float,float,float,short,
                unsigned char,unsigned char,unsigned char,float,bool,float,void*,bool)>(0x707390)(
                2,texture,drop.spot+V{0,0,.15f},.65f,0,0,-.65f,110,
                125,58,12,.65f,false,1.f,nullptr,false);
        }
        return;
    }
    originalPickupEffects(object);
}
static bool install(){
    auto*address=reinterpret_cast<void*>(0x455720);
    auto*death=reinterpret_cast<void*>(0x4591D0);
    if(std::memcmp(address,"\x83\xEC\x28\x53\x55\x56",6)!=0||
       std::memcmp(death,"\x83\xEC\x14\x53\x55\x56",6)!=0||
       MH_CreateHook(address,reinterpret_cast<void*>(&pickupEffects),reinterpret_cast<void**>(&originalPickupEffects))!=MH_OK)return false;
    if(MH_CreateHook(death,reinterpret_cast<void*>(&deathPickups),reinterpret_cast<void**>(&originalDeathPickups))==MH_OK){
        if(MH_QueueEnableHook(address)==MH_OK&&MH_QueueEnableHook(death)==MH_OK)return true;
        MH_QueueDisableHook(address);MH_QueueDisableHook(death);MH_RemoveHook(death);
    }
    MH_RemoveHook(address);
    return false;
}
}
