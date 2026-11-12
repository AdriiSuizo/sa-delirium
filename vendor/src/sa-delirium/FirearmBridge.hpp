#pragma once
#include "WeaponProfiles.hpp"

static const std::array<T,21>& contactPose(const Active& body);
static V physicalContactPoint(const Active& body,int part,V renderedPoint);

namespace firearm {
static const Profile& profile(int weapon){
    static const Profiles profiles=[] {
        std::ifstream input(folder/"weaponinfo.xml",std::ios::binary);
        std::stringstream text;text<<input.rdbuf();
        const auto parsed=parseProfiles(text.str());
        const auto count=std::count_if(parsed.begin(),parsed.end(),[](const auto& p){return p.force>0;});
        event("weapon_table",-1,-1,"verified_profiles="+std::to_string(count)+" force_and_cadence=1");
        return parsed;
    }();
    static const Profile unknown{};
    return weapon>=0&&size_t(weapon)<profiles.size()?profiles[weapon]:unknown;
}
inline bool isShotgun(int weapon){return weapon>=25&&weapon<=27;}
inline float momentum(int weapon,float amount){
    const auto& spec=profile(weapon);
    // Keep the previously tuned push of the two SA pistol variants.
    if((weapon==22||weapon==23)&&spec.force>0)return spec.force*.125f;
    return spec.force>0?spec.force:std::clamp(amount*.25f,3.f,35.f);
}
inline int hitPart(int piece){
    switch(piece){case 3:return 8;case 4:return 0;case 5:return 14;case 6:return 18;case 7:return 1;
        case 8:return 4;case 9:return 12;default:return 10;}
}
struct Contact{
    V point{},direction{},normal{};
    int part=10;
    bool located=false;
};
struct CollisionRay{V end;ColPoint hit;void* victim=nullptr;};
static void tracePellets(V from,CollisionRay* rays,unsigned count);
static int physicalPart(const Active& body,int nativePart,V point){
    int first=nativePart,last=nativePart;
    switch(nativePart){
        case 1:first=1;last=3;break;
        case 4:first=4;last=6;break;
        case 8:first=7;last=11;break;
        case 14:first=13;last=16;break;
        case 18:first=17;last=20;break;
    }
    const auto& pose=contactPose(body);
    float distance=1.e9f;
    int best=nativePart;
    for(int part=first;part<=last;++part){
        const float gap=length(point-pose[part].p)-body.rig->partRadius[part];
        if(gap<distance){distance=gap;best=part;}
    }
    return best;
}
struct Hit{Contact contact;V impulse;bool pellet;unsigned at;};
struct TargetRound{
    std::vector<Contact> contacts;
    Contact aggregate{};
    unsigned reported=0;
    float damageShares=0;
    bool accepted=false;
};
struct Round{
    void* shooter;
    int weapon;
    std::map<int,TargetRound> shotgun;
    V from{},end{},explicitFrom{},explicitEnd{};
    bool replacePellets=false,lineCaptured=false,replaying=false,stockImpact=false,blind=false;
};
struct NativeImpact{void* shooter;void* victim;int weapon;Contact contact;float damageShares;};
static thread_local Round* round=nullptr;
static thread_local const NativeImpact* nativeImpact=nullptr;
static std::map<int,std::vector<Hit>> impacts;
static bool (__thiscall* originalFire)(void*,void*,V*,V*,void*,V*,V*)=nullptr;
static void (__thiscall* originalImpact)(void*,void*,void*,const V&,const V&,const ColPoint&,int)=nullptr;

static float damageShare(void* victim,int piece,V point){
    if(piece<5||piece>8)return 1.f;
    const int endBone=piece==5?34:piece==6?24:piece==7?43:53;
    if(auto*h=hierarchy(victim);h&&h->nodes&&h->matrices&&h->num>0&&h->num<=256){
        for(int n=0;n<h->num;++n)if(h->nodes[n].id==endBone)
            return length(h->matrices[n].pos-point)<.18f?.5f:.75f;
    }
    return .75f;
}
static float damageAmount(void* victim,void* shooter,int weapon,int piece,float native){
    const auto& spec=profile(weapon==52?28:weapon);
    if(!(std::isfinite(spec.damage)&&spec.damage>0&&spec.damage<=1000))return -1.f;
    const bool measured=nativeImpact&&nativeImpact->victim==victim&&nativeImpact->shooter==shooter&&nativeImpact->weapon==weapon;
    // The delayed sniper and the drive-by use their own native dispatchers.
    if(!measured&&((weapon!=34&&weapon!=52)||reference(shooter)<0||native>150.f))return -1.f;
    float damage=spec.damage;
    if(spec.scopedDamage>0&&spec.scopedDamage<=1000){
        auto* camera=reinterpret_cast<void*>(0xB6F028);
        const unsigned index=at<unsigned char>(camera,0x59);
        const int mode=index<3?at<unsigned short>(camera,0x174+index*0x238+0xC):-1;
        if(shooter!=player()||mode==7||mode==39)damage=spec.scopedDamage;
    }
    return damage*(measured?nativeImpact->damageShares:piece>=5&&piece<=8?.75f:1.f);
}
static Contact contactFor(void* victim,void* shooter,int weapon,int part,V direction){
    if(nativeImpact&&nativeImpact->victim==victim&&nativeImpact->shooter==shooter&&nativeImpact->weapon==weapon){
        auto contact=nativeImpact->contact;
        contact.part=part;
        if(length(contact.direction)<.01f)contact.direction=direction;
        return contact;
    }
    return {{},direction,{},part,false};
}
static void add(int ref,const Contact& contact,float strength,bool pellet){
    auto& list=impacts[ref];
    if(list.size()<48)list.push_back({contact,contact.direction*strength,pellet,gameTime()});
}
static bool accepted(void* victim,void* shooter,int weapon,const Contact& contact,float amount){
    const int ref=reference(victim);
    const bool pellet=isShotgun(weapon);
    if(pellet&&round&&round->weapon==weapon&&round->shooter==shooter){
        auto& group=round->shotgun[ref];
        const bool first=!group.accepted;
        group.accepted=true;
        if(!group.reported)group.aggregate=contact;
        return first;
    }
    add(ref,contact,momentum(weapon,amount),pellet);
    return true;
}
static void finish(Round& fired){
    const auto& spec=profile(fired.weapon);
    for(auto& [ref,group]:fired.shotgun){
        if(!group.accepted)continue;
        const unsigned budget=spec.pellets?spec.pellets:fired.shooter==player()?16u:8u;
        const unsigned actual=std::min(budget,std::max({1u,group.reported,unsigned(group.contacts.size())}));
        const unsigned known=std::min(actual,unsigned(group.contacts.size()));
        const float strength=momentum(fired.weapon,20.f);
        for(unsigned i=0;i<known;++i)add(ref,group.contacts[i],strength,true);
        if(actual>known)add(ref,group.aggregate,strength*float(actual-known),true);
        event("shotgun_native_contacts",ref,-1,"weapon="+std::to_string(fired.weapon)+
              " contacts="+std::to_string(known)+" reported="+std::to_string(group.reported)+
              " charged="+std::to_string(actual));
    }
}
static bool captureLine(Round& fired,V from,V end){
    if(!finite(from)||!finite(end))return false;
    const float reach=length(end-from);
    // A stock miss can leave an endpoint at the world origin. It cannot
    // represent a shotgun aim line across a large part of the map.
    if(reach<.3f||reach>160.f)return false;
    fired.from=from;fired.end=end;fired.lineCaptured=true;
    return true;
}
static void observeShotLine(const V& from,const V& end,uintptr_t caller){
    // The primary ray in each verified FireInstantHit branch, observed at
    // HandleGunShot or at its paired ProcessLineOfSight call.
    if(!round||(!round->replacePellets&&!round->blind)||round->lineCaptured||round->stockImpact||
       (caller!=0x73FD8C&&caller!=0x7406EB&&caller!=0x740B33&&
        caller!=0x73FDC7&&caller!=0x740726&&caller!=0x740B6E))return;
    captureLine(*round,from,end);
}
static void __fastcall impact(void*,void*,void*,void*,const V&,const V&,const ColPoint&,int);
static void castPellets(void* weapon,Round& fired,const Profile& spec){
    struct RayHit{ColPoint hit;V end;bool first;};
    struct WorldRayScope{
        void*& ignore=*reinterpret_cast<void**>(0xB7CD68);
        bool& bikers=*reinterpret_cast<bool*>(0xB7CD6F);
        bool& tyres=*reinterpret_cast<bool*>(0xB7CD70);
        bool& dead=*reinterpret_cast<bool*>(0xB7CD71);
        void* priorIgnore=ignore;
        bool priorBikers=bikers,priorTyres=tyres,priorDead=dead;
        explicit WorldRayScope(void* shooter){ignore=shooter;bikers=tyres=dead=true;}
        ~WorldRayScope(){ignore=priorIgnore;bikers=priorBikers;tyres=priorTyres;dead=priorDead;}
    };
    const unsigned budget=spec.pellets?spec.pellets:fired.shooter==player()?16u:8u;
    const float reach=std::isfinite(spec.reach)&&spec.reach>=2.f&&spec.reach<=150.f
        ?spec.reach:length(fired.end-fired.from);
    const V forward=normalized(fired.end-fired.from);
    const auto random=reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90);
    std::array<CollisionRay,32> rays{};
    for(unsigned n=0;n<budget;++n){
        const float across=random(-1.f,1.f),up=random(-1.f,1.f);
        rays[n].end=fired.from+pelletDirection(forward,spec.accuracy,across,up)*reach;
    }
    std::map<void*,std::vector<RayHit>> hits;
    {
        const WorldRayScope flags(fired.shooter);
        tracePellets(fired.from,rays.data(),budget);
    }
    for(unsigned n=0;n<budget;++n)if(rays[n].victim&&finite(rays[n].hit.point))
        hits[rays[n].victim].push_back({rays[n].hit,rays[n].end,n==0});
    fired.replaying=true;
    for(const auto&[victim,rays]:hits){
        const int ref=reference(victim);
        auto main=rays.begin();
        if(ref>=0){
            auto& group=fired.shotgun[ref];
            for(auto ray=rays.begin();ray!=rays.end();++ray){
                group.contacts.push_back({ray->hit.point,shotDirection(fired.from,ray->hit.point),
                    ray->hit.normal,hitPart(ray->hit.surfaces[4]),true});
                group.damageShares+=damageShare(victim,ray->hit.surfaces[4],ray->hit.point)*(ray->first?2.f:1.f);
                if(ray->hit.surfaces[4]==9)main=ray;
            }
            impact(weapon,nullptr,fired.shooter,victim,fired.from,main->end,main->hit,-int(rays.size()));
        }else impact(weapon,nullptr,fired.shooter,victim,fired.from,main->end,main->hit,0);
    }
    if(hits.empty()){
        ColPoint miss{};miss.point=fired.end;
        originalImpact(weapon,fired.shooter,nullptr,fired.from,fired.end,miss,0);
    }
    fired.replaying=false;
    event("shotgun_iv_ray_cast",reference(fired.shooter),-1,"weapon="+std::to_string(fired.weapon)+
          " rays="+std::to_string(budget)+" entities="+std::to_string(hits.size()));
}
static bool __fastcall fire(void* weapon,void*,void* shooter,V* start,V* muzzle,void* target,V* end,V* alt){
    const int id=at<int>(weapon,0);
    if(!ready||!firearmDamage(id)||!playing()||cutscene()||(protectMissions&&scriptMissionActive()))
        return originalFire(weapon,shooter,start,muzzle,target,end,alt);
    if(player_cover::holdsFire(shooter)||!player_cover::blindReady(shooter)||police_cover::holdsFire(shooter))return false;
    const auto& spec=profile(id);
    V exposedStart{},exposedMuzzle{},exposedEnd{};
    const bool blind=player_cover::blindShot(shooter,exposedStart,exposedEnd,
        spec.reach>=3.f&&spec.reach<=150.f?spec.reach:35.f);
    if(blind){
        exposedMuzzle=exposedStart;
        start=&exposedStart;
        muzzle=&exposedMuzzle;
        end=&exposedEnd;
        target=nullptr;
        alt=nullptr;
    }
    auto& next=at<unsigned>(weapon,0x10);
    const unsigned now=gameTime();
    // SA owns ammo, reloads, animation contact frames and weapon skill.
    // Enforce the imported spacing only after a successful native shot.
    const bool paced=spec.interval&&reference(shooter)>=0&&!inVehicle(shooter);
    if(paced&&at<int>(weapon,4)==1&&int(next-now)>0)return false;
    Round fired{shooter,id,{}};
    fired.blind=blind;
    fired.replacePellets=paced&&detectImpacts&&spec.shotgun&&std::isfinite(spec.accuracy)&&spec.accuracy>0.f&&spec.accuracy<=1.f;
    if(fired.replacePellets){
        fired.explicitFrom=muzzle?*muzzle:start?*start:position(shooter)+V{0,0,.45f};
        if(end)fired.explicitEnd=*end;
        else if(reference(target)>=0)fired.explicitEnd=position(target)+V{0,0,.25f};
        else if(shooter==player()){
            auto* camera=reinterpret_cast<void*>(0xB6F028);
            const auto index=at<unsigned char>(camera,0x59);
            if(index<3)fired.explicitEnd=fired.explicitFrom+at<V>(camera,0x174+index*0x238+0x190)*25.f;
        }
    }
    auto* previous=round;round=isShotgun(id)||blind?&fired:nullptr;
    const bool result=originalFire(weapon,shooter,start,muzzle,target,end,alt);
    if(result){
        police_cover::fired(shooter);
        if(blind){
            player_cover::confirmBlindShot(shooter);
            if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)){
                const float direction=fired.lineCaptured?dot(normalized(fired.end-fired.from),normalized(exposedEnd-exposedStart)):-1.f;
                event("cover_blind_native_ray",reference(shooter),id,"direction="+std::to_string(direction)+
                    " muzzle_error="+std::to_string(fired.lineCaptured?length(fired.from-exposedStart):-1.f));
            }
        }
    }
    // Some native paths carry an explicit target instead of publishing the
    // primary LOS. Use that line after Fire confirms a shot, provided its
    // stock path recorded no impact.
    if(result&&fired.replacePellets&&!fired.lineCaptured&&!fired.stockImpact&&fired.shotgun.empty())
        captureLine(fired,fired.explicitFrom,fired.explicitEnd);
    if(isShotgun(id)&&GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))
        event("shotgun_path",reference(shooter),-1,"replace="+std::to_string(fired.replacePellets)+
            " stock="+std::to_string(fired.stockImpact)+" captured="+std::to_string(fired.lineCaptured)+
            " ray_length="+std::to_string(length(fired.end-fired.from))+" success="+std::to_string(result));
    if(result&&fired.replacePellets&&fired.lineCaptured){
        fired.shotgun.clear(); // Ignore stock cosmetics recorded before the primary line.
        castPellets(weapon,fired,spec);
    }
    round=previous;
    if(paced&&result&&at<int>(weapon,4)==1)next=now+spec.interval;
    if(result&&!fired.shotgun.empty())finish(fired);
    return result;
}
static void __fastcall impact(void* weapon,void*,void* shooter,void* victim,const V& start,const V& end,const ColPoint& hit,int count){
    const int id=at<int>(weapon,0);
    if(round&&round->replacePellets&&round->shooter==shooter&&round->weapon==id&&!round->replaying){
        if(round->lineCaptured)return;
        if(count<=0){
            // The stock damage-bearing impact also has the shot's line.
            // Positive counts are pellet cosmetics, never a second damage event.
            if(round->replacePellets&&!round->stockImpact&&
               (captureLine(*round,start,end)||(victim&&captureLine(*round,start,hit.point))||
                captureLine(*round,round->explicitFrom,round->explicitEnd)))return;
            round->stockImpact=true;
        }
    }
    if(!ready||!firearmDamage(id)||reference(victim)<0||!finite(start)||!finite(hit.point)){
        originalImpact(weapon,shooter,victim,start,end,hit,count);return;
    }
    // CColPoint: surface B at +0x23, piece B at +0x24.
    const Contact contact{hit.point,shotDirection(start,hit.point),hit.normal,hitPart(hit.surfaces[4]),true};
    float shares=damageShare(victim,hit.surfaces[4],hit.point);
    if(isShotgun(id)&&round&&round->weapon==id&&round->shooter==shooter){
        auto& group=round->shotgun[reference(victim)];
        if(count<=0){
            group.aggregate=contact;
            group.reported=unsigned(-std::clamp(count,-32,-1));
            shares=group.damageShares>0?group.damageShares:shares*float(group.reported);
        }else if(group.contacts.size()<32)group.contacts.push_back(contact);
    }
    const NativeImpact scope{shooter,victim,id,contact,shares};
    const auto* previous=nativeImpact;nativeImpact=&scope;
    originalImpact(weapon,shooter,victim,start,end,hit,count);
    nativeImpact=previous;
}
static void apply(const std::vector<Active*>& live,unsigned now){
    for(auto* body:live){
        const auto found=impacts.find(body->ref);
        if(found==impacts.end()||!body->agent)continue;
        for(const auto& hit:found->second){
            if(now-hit.at>300)continue;
            const auto part=hit.contact.located?physicalPart(*body,hit.contact.part,hit.contact.point):hit.contact.part;
            if(part<0||size_t(part)>=body->current.size())continue;
            const auto centre=body->current[part].p;
            const bool located=hit.contact.located&&length(hit.contact.point-contactPose(*body)[part].p)<.65f;
            const V point=located?physicalContactPoint(*body,part,hit.contact.point):centre;
            if(hit.pellet){
                float mass=0;
                if(nm_build_part_info(body->rig->build,part,&mass,nullptr,nullptr,nullptr)<0)continue;
                const float mean=nm_agent_get_mass(body->agent)/float(body->current.size());
                if(!(mean>0))continue;
                const V impulse=hit.impulse*(.4f+.6f*std::clamp(mass/mean,.2f,1.5f));
                ok(nm_agent_apply_impulse(body->agent,part,&point.x,&impulse.x));
            }else{
                constexpr float seconds=.3f;
                body->bulletForce={part,point-body->current[0].p,hit.impulse*(1.f/seconds),seconds};
            }
        }
        impacts.erase(found);
    }
    std::erase_if(impacts,[&](const auto& item){return item.second.empty()||now-item.second.back().at>300;});
}
static void step(Active& body,float dt){
    auto& hit=body.bulletForce;
    if(hit.left<=0)return;
    const float step=std::min(dt,hit.left);
    const V point=body.current[0].p+hit.offset,impulse=hit.force*step;
    ok(nm_agent_apply_impulse(body.agent,hit.part,&point.x,&impulse.x));
    hit.left-=step;
}
static bool install(){
    constexpr uintptr_t fireAddr=0x742300,impactAddr=0x73B550;
    auto* f=reinterpret_cast<void*>(fireAddr);
    auto* i=reinterpret_cast<void*>(impactAddr);
    if(std::memcmp(f,"\x83\xEC\x3C\x53\x56\x57",6)||std::memcmp(i,"\x6A\xFF\x68\x50\x8E\x84",6))return false;
    if(MH_CreateHook(f,reinterpret_cast<void*>(&fire),reinterpret_cast<void**>(&originalFire))!=MH_OK)return false;
    if(MH_CreateHook(i,reinterpret_cast<void*>(&impact),reinterpret_cast<void**>(&originalImpact))==MH_OK){
        if(MH_QueueEnableHook(f)==MH_OK&&MH_QueueEnableHook(i)==MH_OK)return true;
        MH_RemoveHook(i);
    }
    MH_RemoveHook(f);
    return false;
}
}
