#pragma once
#include "ArmedStep.hpp"

namespace police_cover {
using namespace player_cover;
enum class Phase { Approach, Enter, Hidden, Expose, Fire, Return };
struct Officer {
    Wall wall{};
    V hide{},fire{};
    int threat=-1,side=1,poseWeapon=22;
    void* duck=nullptr;
    void* gun=nullptr;
    Entry entry{}, facing{};
    unsigned since=0;
    float exposeSeconds=0.f, returnSeconds=0.f;
    Phase phase=Phase::Approach;
    bool unarmed=false;
    npc_gait::Binding approachLegs;
    const Node* approachNodes=nullptr;
    V approachPos{};
    unsigned approachFrame=~0u;
    float approachPhase=0;
    PoseReader pose;
    PoseTransition transition;
};
static std::map<int,Officer> officers;
static std::map<int,unsigned> gunfire;
static unsigned nextSearch=0;
static bool copUnarmedFlee(void* ped){
    return ped&&at<int>(ped,0x598)==6&&(pedWasDisarmed(ped)||(!copHasFirearm(ped)&&combatWeapon(ped)<=1));
}
static bool hot(int ref,unsigned now);
static void* nearestHotThreat(void* ped,unsigned now){
    void* best=nullptr;float bestDist=24.f;
    const V pos=position(ped);
    for(const auto&[ref,atTime]:gunfire){
        if(!hot(ref,now))continue;
        auto* shooter=pedFromRef(ref);
        if(!shooter||shooter==ped||at<int>(shooter,0x598)==6)continue;
        const float d=length(position(shooter)-pos);
        if(d<bestDist){bestDist=d;best=shooter;}
    }
    return best;
}
static void assignCopFlee(void* ped,void* enemy){
    if(!ped||!enemy||inVehicle(ped)||at<float>(ped,0x540)<=0.f)return;
    if(const auto intent=combatReadNativeIntent(ped);
       intent.type==TASK_COMPLEX_SMART_FLEE_ENTITY&&intent.source==enemy)return;
    auto* manager=taskManager(ped);if(!manager)return;
    void* task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x40);if(!task)return;
    reinterpret_cast<void*(__thiscall*)(void*,void*,bool,float,int,int,float)>(0x65C430)
        (task,enemy,false,40.f,15000,1000,1.f);
    reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(manager,task,1,true);
    reinterpret_cast<void(__thiscall*)(void*,void*,int)>(0x681B60)(manager,nullptr,0);
}
static void fired(void* shooter){
    const int ref=reference(shooter);
    if(ref>=0)gunfire[ref]=gameTime();
}
static void releaseGun(void* ped, Officer& officer){
    if(ped&&officer.gun){
        auto* intelligence=at<void*>(ped,0x47C);
        auto* gun=intelligence?reinterpret_cast<void*(__thiscall*)(void*)>(0x600F70)(intelligence):nullptr;
        if(gun==officer.gun)
            reinterpret_cast<bool(__thiscall*)(void*,void*,void*,signed char)>(0x61E040)(gun,ped,pedFromRef(officer.threat),6);
    }
    officer.gun=nullptr;
}
static void clear(){
    for(auto&[ref, officer]:officers){
        auto* ped=pedFromRef(ref);
        setDuck(ped,officer.duck,false);
        releaseGun(ped,officer);
    }
    officers.clear();gunfire.clear();disarmedPeds.clear();nextSearch=0;
}
static void* target(void* ped){
    auto* manager=taskManager(ped);
    auto* task=manager?reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(manager):nullptr;
    const bool unarmed=copUnarmedFlee(ped);
    for(int depth=0;task&&depth<12;++depth){
        auto** vtable=at<void**>(task,0);
        const int type=reinterpret_cast<int(__thiscall*)(void*)>(vtable[4])(task);
        if(type==TASK_COMPLEX_KILL_PED_ON_FOOT_ARMED)return at<void*>(task,0xC);
        if(type==TASK_COMPLEX_KILL_PED_ON_FOOT)return at<void*>(task,0x10);
        if(unarmed&&(type==TASK_COMPLEX_SMART_FLEE_ENTITY||type==TASK_COMPLEX_KILL_PED_ON_FOOT_MELEE))
            return at<void*>(task,0xC);
        task=reinterpret_cast<void*(__thiscall*)(void*)>(vtable[2])(task);
    }
    if(unarmed){
        if(auto* p=pedFromRef(pedDisarmedThreat(ped)))return p;
        if(auto* shooter=nearestHotThreat(ped,gameTime()))return shooter;
    }
    return nullptr;
}
static bool available(void* ped){
    if(!ped||at<unsigned char>(ped,0x484)==2||
       !validPed(ped)||!nativeOnFootControl(at<unsigned char>(ped,0x46D)))return false;
    const int ref=reference(ped),weapon=combatWeapon(ped);
    const bool armed=weapon>=22&&weapon<=34&&!pedWasDisarmed(ped);
    return (armed||copUnarmedFlee(ped))&&ivHealth(ped)>=combat_health::injured&&
        !agents.contains(ref)&&!pending.contains(ref)&&!native_stealth::ownsPose(ped);
}
static bool hot(int ref,unsigned now){
    const auto found=gunfire.find(ref);
    return found!=gunfire.end()&&now-found->second<=5200;
}
static bool screen(V root,void* enemy,float height){
    float floor;
    if(!ground(root,floor))return false;
    V from{root.x,root.y,floor+height};
    ColPoint hit{};void* entity=nullptr;
    const V target=position(enemy)+V{0,0,.3f};
    return ray(from,target,hit,entity);
}
static bool find(void* ped,void* enemy,Wall& wall,V& hide,V& fire,int& side){
    const V root=position(ped);
    const V ahead=flat(position(enemy)-root);
    const float reach=copUnarmedFlee(ped)?8.f:6.5f;
    bool found=findWall(ped,ahead,reach,wall,hide);
    if(!found){
        for(float a:{.65f,-.65f,1.15f,-1.15f}){
            const float c=std::cos(a),s=std::sin(a);
            const V dir{ahead.x*c-ahead.y*s,ahead.x*s+ahead.y*c,0.f};
            if(findWall(ped,dir,reach,wall,hide)){found=true;break;}
        }
    }
    if(!found||!screen(hide,enemy,.6f))return false;
    const V along=tangent(wall.normal);
    side=dot(along,matrix(ped)->right)>=0.f?1:-1;
    fire=hide;
    if(wall.low&&!screen(hide,enemy,1.4f))return true;
    for(float distance:{.48f,.85f,1.2f,1.6f})for(int direction:{1,-1}){
        const V edge=hide+along*(distance*direction);
        if(sameWall(edge,wall)||screen(edge,enemy,wall.low?1.15f:1.4f))continue;
        const V tucked=edge-along*(.42f*direction);
        if(!sameWall(tucked,wall)||!screen(tucked,enemy,.6f))continue;
        float floor;
        if(!ground(edge,floor))continue;
        side=direction;hide=tucked;fire=edge;
        return true;
    }
    return false;
}
// SA adapter for the expose/fire handoff. The normal primary kill task
// can have retired its secondary gun task while the muzzle was hidden.
static void aimGun(void* ped, void* enemy, Officer& officer, bool fire){
    auto* intelligence=at<void*>(ped,0x47C);
    auto* manager=taskManager(ped);
    if(!intelligence||!manager)return;
    auto* gun=reinterpret_cast<void*(__thiscall*)(void*)>(0x600F70)(intelligence);
    if(!gun){
        // TaskManager.h: five primary pointers followed by secondary attack.
        if(at<void*>(manager,0x14))return;
        gun=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x3C);
        if(!gun)return;
        reinterpret_cast<void*(__thiscall*)(void*,void*,V,signed char,unsigned short,bool)>(0x61DE60)
            (gun,enemy,V{},1,1,false);
        reinterpret_cast<void(__thiscall*)(void*,void*,int)>(0x681B60)(manager,gun,0);
        officer.gun=gun;
    }
    reinterpret_cast<bool(__thiscall*)(void*,void*,void*,signed char)>(0x61E040)
        (gun,ped,enemy,fire?2:1);
}
static void setPhase(int ref,Officer& officer,Phase phase,unsigned now){
    if(phase==Phase::Return)releaseGun(pedFromRef(ref),officer);
    officer.phase=phase;officer.since=now;
    if (phase==Phase::Expose || phase==Phase::Return) {
        if (auto* ped=pedFromRef(ref))
            officer.facing=startEntry(ped,now,phase==Phase::Expose?officer.exposeSeconds:officer.returnSeconds);
    }
    static constexpr const char* names[]={"approach","enter","hidden","expose","fire","return"};
    event("police_cover_phase",ref,-1,names[int(phase)]);
}
static void tick(){
    if(!combatAllowed()||!ivPoliceResponse){if(!officers.empty()||!gunfire.empty())clear();return;}
    const unsigned now=gameTime();
    for(auto it=officers.begin();it!=officers.end();){
        auto* ped=pedFromRef(it->first);
        auto& officer=it->second;
        auto* enemy=pedFromRef(officer.threat);
        const unsigned elapsed=now-officer.since;
        const bool unarmedNow=copUnarmedFlee(ped);
        const bool expired=!hot(officer.threat,now)&&!(officer.unarmed&&pedWasDisarmed(ped));
        const bool tooClose=enemy&&length(position(ped)-position(enemy))<(officer.unarmed?6.5f:4.f);
        const bool badState=unarmedNow!=officer.unarmed||(officer.phase==Phase::Hidden&&expired)||
            (officer.unarmed&&officer.phase==Phase::Hidden&&(elapsed>5500u||!enemy||!screen(officer.hide,enemy,.6f)));
        void* seenTarget=ped?target(ped):nullptr;
        const bool targetLost=!validPed(enemy,true,false,enemy==player())||
            (officer.phase==Phase::Approach?seenTarget!=enemy:(seenTarget!=nullptr&&seenTarget!=enemy));
        if(!available(ped)||!enemy||targetLost||tooClose||badState||!sameWall(officer.hide,officer.wall)){
            setDuck(ped,officer.duck,false);
            releaseGun(ped,officer);
            if((officer.unarmed||unarmedNow)&&ped&&enemy&&available(ped))assignCopFlee(ped,enemy);
            event("police_cover_left",it->first,-1);
            it=officers.erase(it);continue;
        }
        const V pos=position(ped);
        const auto arrived=[&](V at){const V d=pos-at;return std::hypot(d.x,d.y)<.16f;};
        switch(officer.phase){
            case Phase::Approach:
                if(std::hypot(pos.x-officer.hide.x,pos.y-officer.hide.y)<.72f){
                    officer.entry=startEntry(ped,now,.38f);
                    setPhase(it->first,officer,Phase::Enter,now);
                }
                else if(elapsed>1750){setDuck(ped,officer.duck,false);if(officer.unarmed)assignCopFlee(ped,enemy);it=officers.erase(it);continue;}
                break;
            case Phase::Enter:
                if((arrived(officer.hide)||(std::hypot(pos.x-officer.hide.x,pos.y-officer.hide.y)<.24f&&sameWall(pos,officer.wall)))&&entryAge(officer.entry)>=officer.entry.seconds)setPhase(it->first,officer,Phase::Hidden,now);
                else if(elapsed>1100){setDuck(ped,officer.duck,false);if(officer.unarmed)assignCopFlee(ped,enemy);it=officers.erase(it);continue;}
                break;
            case Phase::Hidden:
                if(!officer.unarmed&&elapsed>650u+unsigned(it->first%520))setPhase(it->first,officer,Phase::Expose,now);
                break;
            case Phase::Expose:
                // TU8 0x824A8960 tests completion of the subtask. In SA
                // wait for both the imported clip and resolved movement.
                if(arrived(officer.fire)&&elapsed*.001f>=officer.exposeSeconds)setPhase(it->first,officer,Phase::Fire,now);
                else if(elapsed*.001f>=officer.exposeSeconds+1.f)setPhase(it->first,officer,Phase::Return,now);
                break;
            case Phase::Fire:
                if(elapsed>900u+unsigned(it->first%410))setPhase(it->first,officer,Phase::Return,now);
                break;
            case Phase::Return:
                if(arrived(officer.hide)&&elapsed*.001f>=officer.returnSeconds)setPhase(it->first,officer,Phase::Hidden,now);
                else if(elapsed*.001f>=officer.returnSeconds+1.f){setDuck(ped,officer.duck,false);it=officers.erase(it);continue;}
                break;
        }
        setDuck(ped,officer.duck,officer.wall.low&&officer.phase==Phase::Hidden);
        if(officer.phase==Phase::Expose||officer.phase==Phase::Fire)
            aimGun(ped,enemy,officer,officer.phase==Phase::Fire&&!screen(position(ped),enemy,1.4f));
        ++it;
    }
    if(now<nextSearch)return;
    nextSearch=now+210;
    std::erase_if(gunfire,[&](const auto& row){return !hot(row.first,now)||!pedFromRef(row.first);});
    std::erase_if(disarmedPeds,[&](const auto& row){return now-row.second.at>20000u||!pedFromRef(row.first);});
    if(gunfire.empty()&&disarmedPeds.empty())return;
    auto* pool=pedPool();
    for(int n=0;pool&&n<pool->size;++n)if(!(pool->flags[n]&128)){
        auto* ped=pool->objects+n*0x7C4;
        if(!available(ped))continue;
        const bool unarmed=copUnarmedFlee(ped);
        if(length(position(ped)-position(player()))>(unarmed?28.f:26.f))continue;
        const int ref=reference(ped);
        if(officers.contains(ref))continue;
        auto* enemy=target(ped);
        const int threat=reference(enemy);
        if(!enemy||threat<0)continue;
        const bool disarmed=unarmed&&pedWasDisarmed(ped);
        if(!unarmed&&(officers.size()>=3||!hot(threat,now)))continue;
        if(unarmed&&!hot(threat,now)&&!disarmed)continue;
        const float dist=length(position(ped)-position(enemy));
        if(dist<7.f){if(unarmed)assignCopFlee(ped,enemy);continue;}
        Wall wall;
        V hide,fire;
        int side=1;
        if(officers.size()>=3||!find(ped,enemy,wall,hide,fire,side)||
           std::any_of(officers.begin(),officers.end(),[&](const auto& item){return length(item.second.hide-hide)<.85f;})){
            if(unarmed)assignCopFlee(ped,enemy);
            continue;
        }
        auto& officer=officers.try_emplace(ref).first->second;
        officer.wall=wall;officer.hide=hide;officer.fire=fire;officer.threat=threat;officer.side=side;
        officer.unarmed=unarmed;
        const int weapon=combatWeapon(ped);
        if(unarmed){
            officer.poseWeapon=clipMeta(officer.pose,ped,animationKey(wall.low,false,side,0,"_idle")).duration>0.f?0:22;
            if(!(clipMeta(officer.pose,ped,animationKey(wall.low,false,side,officer.poseWeapon,"_idle")).duration>0.f)){
                officers.erase(ref);
                assignCopFlee(ped,enemy);
                continue;
            }
        }else{
            officer.poseWeapon=weapon;
            const bool corner=length(hide-fire)>.1f;
            officer.exposeSeconds=clipMeta(officer.pose,ped,animationKey(wall.low,corner,side,weapon,"_normal_fire_intro")).duration;
            officer.returnSeconds=clipMeta(officer.pose,ped,animationKey(wall.low,corner,side,weapon,"_normal_fire_outro")).duration;
            if(!(officer.exposeSeconds>0.f&&officer.returnSeconds>0.f)){officers.erase(ref);continue;}
        }
        if(at<int>(ped,0x598)!=6)
            event("armed_npc_cover",ref,at<short>(ped,0x22),"weapon="+std::to_string(weapon)+" threat="+std::to_string(threat));
        setPhase(ref,officer,Phase::Approach,now);
    }
}
static bool holdsFire(void* ped){
    if(officers.empty())return false;
    const auto found=officers.find(reference(ped));
    return found!=officers.end()&&found->second.phase!=Phase::Fire;
}
static bool move(void* ped){
    if(officers.empty())return false;
    const auto found=officers.find(reference(ped));
    if(found==officers.end()||!available(ped))return false;
    auto& officer=found->second;
    const bool expose=officer.phase==Phase::Expose||officer.phase==Phase::Fire;
    const bool entering=officer.phase==Phase::Enter;
    const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    V goal=expose?officer.fire:officer.hide;
    if(entering)goal=entryGoal(officer.entry,goal,dt);
    if(officer.phase==Phase::Expose||officer.phase==Phase::Return)goal=entryGoal(officer.facing,goal,dt);
    V delta=goal-position(ped);
    delta.z=0;
    const float distance=length(delta);
    const float speed=officer.phase==Phase::Approach||entering?3.5f:2.f;
    const V velocity=dt>0.f&&distance>.03f?delta*(std::min(speed,distance*7.f)/distance):V{};
    at<float>(ped,0x550)=velocity.x*.02f;at<float>(ped,0x554)=velocity.y*.02f;
    if(!expose&&officer.phase!=Phase::Approach){
        const V direction=officer.phase==Phase::Return
            ?V{-officer.wall.normal.x,-officer.wall.normal.y,0.f}
            :tangent(officer.wall.normal)*float(officer.side);
        face(ped,direction,entering?&officer.entry:officer.phase==Phase::Return?&officer.facing:nullptr);
    }else if(officer.phase==Phase::Expose){
        const V sideDir=tangent(officer.wall.normal)*float(officer.side);
        V rootTarget=sideDir;
        if(auto* enemy=pedFromRef(officer.threat)){
            const float normalOutYaw=std::atan2(officer.wall.normal.x,-officer.wall.normal.y);
            const V enemyDir=flat(position(enemy)-position(ped));
            const float aimYaw=std::atan2(-enemyDir.x,enemyDir.y);
            const float sideYaw=std::atan2(-sideDir.x,sideDir.y);
            const float targetRootYaw=sideYaw+std::remainder(aimYaw-normalOutYaw,6.2831853f);
            rootTarget={-std::sin(targetRootYaw),std::cos(targetRootYaw),0.f};
        }
        face(ped,rootTarget,&officer.facing);
    }else if(officer.phase==Phase::Fire){
        if(auto* enemy=pedFromRef(officer.threat))face(ped,flat(position(enemy)-position(ped)));
    }
    return true;
}
static bool drawApproach(Officer& officer,void* ped){
    auto* h=hierarchy(ped);
    if(!h||!h->nodes||!h->matrices||!matrix(ped))return false;
    auto& binding=officer.approachLegs;
    if(h->nodes!=officer.approachNodes||h->num!=binding.count){
        static const std::vector<int16_t> tags={1,2,3,5,22,23,32,33,41,42,43,44,51,52,53,54};
        if(!binding.init(h->nodes,h->num,tags))return false;
        if(officer.approachNodes)officer.pose.reset();
        officer.approachNodes=h->nodes;
        officer.approachPos=position(ped);
        officer.approachFrame=~0u;
        officer.approachPhase=0;
    }
    const V pos=position(ped),direction=flat(officer.hide-pos);
    const auto* body=matrix(ped);
    const unsigned dir=armed_step::sector(dot(direction,flat(body->right)),dot(direction,flat(body->up)));
    const char* key=armed_step::clip(true,dir);
    const auto meta=clipMeta(officer.pose,ped,key);
    if(!(meta.duration>0&&meta.moverSpeed>.05f))return false;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(frame!=officer.approachFrame){
        const V travelled=pos-officer.approachPos;
        if(frame-officer.approachFrame<=2 && finite(travelled)&&length(travelled)<.8f){
            const float forward=std::max(0.f,dot(travelled,direction));
            officer.approachPhase=std::fmod(officer.approachPhase+forward/(meta.duration*meta.moverSpeed),1.f);
        }
        officer.approachFrame=frame;officer.approachPos=pos;
    }
    float info[2]{};
    if(nm_locomotion_clip_pose_masked(combat,officer.pose.reader,key,officer.approachPhase,
       officer.pose.rows.data(),h->num,info,0x600)!=1)return false;
    // CPed already consumed the horizontal displacement. Keep its root and
    // gun task's upper body while the legs step towards the wall.
    const float weight=smooth(float(gameTime()-officer.since)*.008f);
    return armed_step::legs(binding,officer.pose.rows.data(),*body,h->matrices,weight);
}
static bool draw(void* ped){
    if(officers.empty())return false;
    const auto found=officers.find(reference(ped));
    if(found==officers.end()||!available(ped))return false;
    auto& officer=found->second;
    const float elapsed=float(gameTime()-officer.since)*.001f;
    const int weapon=officer.unarmed?officer.poseWeapon:combatWeapon(ped);
    const bool corner=length(officer.hide-officer.fire)>.1f;
    Motion motion=Motion::Rest;
    bool authored=false;
    switch(officer.phase){
        case Phase::Approach:
            motion=Motion::Walk;
            authored=drawApproach(officer,ped);
            break;
        case Phase::Enter:
            motion=Motion::Entry;
            authored=drawEntry(officer.pose,ped,officer.wall,officer.side,weapon,officer.entry);
            break;
        case Phase::Hidden:
            authored=drawClip(officer.pose,ped,animationKey(officer.wall.low,false,officer.side,weapon,"_idle"),
                elapsed,Clip::Loop,1.f);
            break;
        case Phase::Expose:
            motion=Motion::AimIntro;
            authored=drawClip(officer.pose,ped,animationKey(officer.wall.low,corner,officer.side,weapon,"_normal_fire_intro"),
                elapsed,Clip::Hold,1.f,0x600);
            break;
        case Phase::Return:
            motion=Motion::Return;
            authored=drawClip(officer.pose,ped,animationKey(officer.wall.low,corner,officer.side,weapon,"_normal_fire_outro"),
                elapsed,Clip::Hold,1.f,0x600);
            break;
        case Phase::Fire:
            motion=Motion::AimNative; // Native gun task supplies firing and reload.
            break;
    }
    const bool blending=blendCoverPose(ped,officer.pose,officer.transition,motion,officer.since,.125f);
    return authored||blending||officer.phase==Phase::Fire;
}
}
