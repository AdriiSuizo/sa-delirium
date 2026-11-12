#pragma once

namespace police_cover {
using namespace player_cover;
enum class Phase { Approach, Enter, Hidden, Expose, Fire, Return };
struct Officer {
    Wall wall{};
    V hide{},fire{};
    int threat=-1,side=1;
    void* duck=nullptr;
    void* gun=nullptr;
    Entry entry{}, facing{};
    unsigned since=0;
    float exposeSeconds=0.f, returnSeconds=0.f;
    Phase phase=Phase::Approach;
    PoseReader pose;
    PoseTransition transition;
};
static std::map<int,Officer> officers;
static std::map<int,unsigned> gunfire;
static unsigned nextSearch=0;
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
    officers.clear();gunfire.clear();nextSearch=0;
}
static void* target(void* ped){
    auto* manager=taskManager(ped);
    auto* task=manager?reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(manager):nullptr;
    for(int depth=0;task&&depth<12;++depth){
        auto** vtable=at<void**>(task,0);
        const int type=reinterpret_cast<int(__thiscall*)(void*)>(vtable[4])(task);
        if(type==TASK_COMPLEX_KILL_PED_ON_FOOT_ARMED)return at<void*>(task,0xC);
        if(type==TASK_COMPLEX_KILL_PED_ON_FOOT)return at<void*>(task,0x10);
        task=reinterpret_cast<void*(__thiscall*)(void*)>(vtable[2])(task);
    }
    return nullptr;
}
static bool available(void* ped){
    if(!ped||at<int>(ped,0x598)!=6||at<unsigned char>(ped,0x484)==2||
       !validPed(ped)||!nativeOnFootControl(at<unsigned char>(ped,0x46D)))return false;
    const int ref=reference(ped),weapon=combatWeapon(ped);
    return weapon>=22&&weapon<=34&&ivHealth(ped)>=combat_health::injured&&
        !agents.contains(ref)&&!pending.contains(ref);
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
    if(!findWall(ped,ahead,5.f,wall,hide)||!screen(hide,enemy,.6f))return false;
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
        if(!available(ped)||!enemy||target(ped)!=enemy||(!hot(officer.threat,now)&&officer.phase==Phase::Hidden)||
           length(position(ped)-position(enemy))<4.f||!sameWall(officer.hide,officer.wall)){
            setDuck(ped,officer.duck,false);
            releaseGun(ped,officer);
            event("police_cover_left",it->first,-1);
            it=officers.erase(it);continue;
        }
        const unsigned elapsed=now-officer.since;
        const V pos=position(ped);
        const auto arrived=[&](V at){const V d=pos-at;return std::hypot(d.x,d.y)<.16f;};
        switch(officer.phase){
            case Phase::Approach:
                if(std::hypot(pos.x-officer.hide.x,pos.y-officer.hide.y)<.72f){
                    officer.entry=startEntry(ped,now,.38f);
                    setPhase(it->first,officer,Phase::Enter,now);
                }
                else if(elapsed>1750){setDuck(ped,officer.duck,false);it=officers.erase(it);continue;}
                break;
            case Phase::Enter:
                if(arrived(officer.hide)&&entryAge(officer.entry)>=officer.entry.seconds)setPhase(it->first,officer,Phase::Hidden,now);
                else if(elapsed>1100){setDuck(ped,officer.duck,false);it=officers.erase(it);continue;}
                break;
            case Phase::Hidden:
                if(elapsed>650u+unsigned(it->first%520))setPhase(it->first,officer,Phase::Expose,now);
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
    if(gunfire.empty()||officers.size()>=3)return;
    auto* pool=pedPool();
    for(int n=0;pool&&n<pool->size&&officers.size()<3;++n)if(!(pool->flags[n]&128)){
        auto* ped=pool->objects+n*0x7C4;
        if(!available(ped)||length(position(ped)-position(player()))>26.f)continue;
        const int ref=reference(ped);
        if(officers.contains(ref))continue;
        auto* enemy=target(ped);
        const int threat=reference(enemy);
        if(threat<0||!hot(threat,now)||length(position(ped)-position(enemy))<7.f)continue;
        Wall wall;
        V hide,fire;
        int side=1;
        if(!find(ped,enemy,wall,hide,fire,side))continue;
        if(std::any_of(officers.begin(),officers.end(),[&](const auto& item){return length(item.second.hide-hide)<.85f;}))continue;
        auto& officer=officers.try_emplace(ref).first->second;
        officer.wall=wall;officer.hide=hide;officer.fire=fire;officer.threat=threat;officer.side=side;
        const int weapon=combatWeapon(ped);
        const bool corner=length(hide-fire)>.1f;
        officer.exposeSeconds=clipMeta(officer.pose,ped,animationKey(wall.low,corner,side,weapon,"_normal_fire_intro")).duration;
        officer.returnSeconds=clipMeta(officer.pose,ped,animationKey(wall.low,corner,side,weapon,"_normal_fire_outro")).duration;
        if(!(officer.exposeSeconds>0.f&&officer.returnSeconds>0.f)){officers.erase(ref);continue;}
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
        const V direction=tangent(officer.wall.normal)*float(officer.side);
        face(ped,direction,entering?&officer.entry:officer.phase==Phase::Return?&officer.facing:nullptr);
    }else if(officer.phase==Phase::Expose){
        if(auto* enemy=pedFromRef(officer.threat))face(ped,flat(position(enemy)-position(ped)),&officer.facing);
    }
    return true;
}
static bool draw(void* ped){
    if(officers.empty())return false;
    const auto found=officers.find(reference(ped));
    if(found==officers.end()||!available(ped))return false;
    auto& officer=found->second;
    const float elapsed=float(gameTime()-officer.since)*.001f;
    const int weapon=combatWeapon(ped);
    const bool corner=length(officer.hide-officer.fire)>.1f;
    Motion motion=Motion::Rest;
    bool authored=false;
    switch(officer.phase){
        case Phase::Approach:
            motion=Motion::Walk;
            authored=drawClip(officer.pose,ped,"move_player/run",elapsed,Clip::Loop,
                .75f*smooth((length(officer.hide-position(ped))-.72f)/.45f));
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
