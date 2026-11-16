#pragma once
#include <xinput.h>
#include "CoverNativeCamera.hpp"

namespace player_cover {
struct Wall {
    int vehicle=-1;
    V point{},normal{};
    bool low=false;
};
struct Entry {
    V from{};
    float yaw=0,seconds=0;
    unsigned started=0;
};
struct State {
    int ref=-1;
    Wall wall{};
    V destination{},peekBase{},peekTarget{},lastSafe{};
    void* duck=nullptr;
    Entry entry{},turn{},aimFacing{},returnFacing{};
    int side=1,fireSide=1;
    bool fireCorner=false;
    unsigned entered=0,exposed=0,returning=0,peekStarted=0,blindStarted=0,blindShotAt=0,blindReleased=0,stalledSince=0;
    float diveSeconds=0,slideSpeed=0,slideTime=0,standOff=.46f,walkTime=0.f;
    V stallPos{};
    bool shortDive=false,runningEntry=false;
    bool approaching=false,aiming=false,peeking=false,corner=false,blind=false;
};
struct ClipMeta { float duration=0,moverSpeed=0; };
struct PoseReader {
    NmLocomotion* reader=nullptr;
    std::unique_ptr<Rig> rig;
    int ref=-1,model=-1;
    std::vector<float> rows;
    std::vector<T> native,authored,bones;
    std::map<std::string,ClipMeta> clips;
    ~PoseReader(){reset();}
    void reset(){
        if(reader)nm_locomotion_destroy(reader);
        reader=nullptr;rig.reset();ref=model=-1;rows.clear();clips.clear();
    }
};
enum class Motion { Entry, Dive, Rest, Walk, Turn, Peek, Blind, AimIntro, AimNative, Return };
struct PoseTransition {
    int ref=-1,weapon=-1;
    unsigned action=0;
    Motion motion=Motion::Rest;
    unsigned started=0,gripAt=0;
    M grip{};
    bool blindGrip=false;
    std::vector<T> previous,last,world;
};
static State cover;
static State departed;
static unsigned departedAt=0;
static bool wasDown=false;
static PoseReader poseReader;
static PoseTransition transition;
struct CoverView {
    void* cam=nullptr;
    V nativeFront{},nativeSource{},nativeUp{},previousFront{};
    Q offset{};
    float shoulder=0.f,pivotZ=.55f;
    bool seeded=false,unoccludedSource=false;
};
static CoverView viewState;

static void* duckTask(void* ped){
    auto* intelligence=at<void*>(ped,0x47C);
    return intelligence?reinterpret_cast<void*(__thiscall*)(void*,bool)>(0x6010A0)(intelligence,true):nullptr;
}
static void setDuck(void* ped,void*& owner,bool want){
    if(!ped)return;
    auto* intelligence=at<void*>(ped,0x47C);
    if(!intelligence)return;
    auto* current=duckTask(ped);
    if(want&&!current){
        reinterpret_cast<void(__thiscall*)(void*,unsigned short)>(0x601230)(intelligence,0);
        owner=duckTask(ped);
    }else if(!want&&owner){
        if(current==owner)reinterpret_cast<void(__thiscall*)(void*)>(0x601390)(intelligence);
        owner=nullptr;
    }
}
static void leave(const char* why){
    if(cover.ref<0)return;
    setDuck(pedFromRef(cover.ref),cover.duck,false);
    if(std::strcmp(why,"button")==0||std::strcmp(why,"step_away")==0){
        departed=cover;departedAt=gameTime();
    }
    event("player_cover_left",cover.ref,0,why);
    cover={};
    transition.ref=-1;
}
static void clear(){
    leave("world_boundary");
    departed={};departedAt=0;
    poseReader.reset();
}
static bool owns(void* ped){return ped&&ped==player()&&cover.ref>=0&&pedFromRef(cover.ref)==ped;}
static bool holdsFire(void* ped){
    return owns(ped)&&(cover.approaching||cover.turn.started||(!cover.wall.low&&!cover.corner&&!cover.peeking));
}
static bool blocksBullet(void* ped){
    if(holdsFire(ped))return true;
    if(!owns(ped)||!cover.aiming)return false;
    if(cover.aimFacing.seconds>0.f&&cover.exposed&&float(gameTime()-cover.exposed)*.001f<cover.aimFacing.seconds*.50f)return true;
    if(cover.fireCorner&&!cover.wall.low&&matrix(ped)){
        const V d=cover.destination-matrix(ped)->pos;
        if(std::hypot(d.x,d.y)>.28f)return true;
    }
    return false;
}
static Entry startEntry(void*,unsigned,float);
static ClipMeta clipMeta(PoseReader&,void*,const std::string&);
static std::string animationKey(bool,bool,int,int,const char*);
static void updateAim(bool aim,unsigned now){
    auto* ped=player();
    if(aim&&!cover.aiming){
        cover.exposed=now;cover.returning=0;cover.returnFacing={};cover.fireSide=cover.side;cover.fireCorner=cover.corner;
        float dur=.25f;
        if(ped&&(cover.wall.low||cover.fireCorner)){
            const float d=clipMeta(poseReader,ped,animationKey(cover.wall.low,cover.fireCorner,cover.fireSide,combatWeapon(ped),"_normal_fire_intro")).duration;
            if(d>0.f)dur=d;
        }
        cover.aimFacing=ped?startEntry(ped,now,dur):Entry{{},0.f,dur,now};
    }
    if(!aim&&cover.aiming){
        cover.returning=now;
        float dur=.30f;
        if(ped&&(cover.wall.low||cover.fireCorner)){
            const float d=clipMeta(poseReader,ped,animationKey(cover.wall.low,cover.fireCorner,cover.fireSide,combatWeapon(ped),"_normal_fire_outro")).duration;
            if(d>0.f)dur=d;
        }
        cover.returnFacing=ped?startEntry(ped,now,dur):Entry{{},0.f,dur,now};
    }
    cover.aiming=aim;
}
static bool available(void* ped){
    if(!combatAllowed()||!validPed(ped,false,false,true)||!nativeOnFootControl(at<unsigned char>(ped,0x46D))||
       agents.contains(reference(ped))||pending.contains(reference(ped)))return false;
    auto* manager=taskManager(ped);
    auto* task=manager?reinterpret_cast<void*(__thiscall*)(void*)>(0x6819D0)(manager):nullptr;
    if(!task)return false;
    const auto type=reinterpret_cast<int(__thiscall*)(void*)>(at<void**>(task,0)[4])(task);
    return type==TASK_SIMPLE_PLAYER_ON_FOOT;
}
static bool aimInput(void* pad){
    if(!pad)return false;
    return originalCombatGetTarget?originalCombatGetTarget(pad):
        reinterpret_cast<bool(__thiscall*)(void*)>(0x540670)(pad);
}
static Entry startEntry(void* ped,unsigned now,float seconds){
    return {position(ped),at<float>(ped,0x558),seconds,now};
}
static float entryAge(const Entry& entry){return float(gameTime()-entry.started)*.001f;}
static float smooth(float fraction){
    const float x=std::clamp(fraction,0.f,1.f);
    return x*x*(3.f-2.f*x);
}
static V entryGoal(const Entry& entry,V end,float step){
    return entry.from+(end-entry.from)*smooth((entryAge(entry)+step)/entry.seconds);
}
static void face(void* ped,V direction,const Entry* entry=nullptr,const V* outwardNormal=nullptr){
    float yaw=std::atan2(-direction.x,direction.y);
    if(entry){
        float dYaw=std::remainder(yaw-entry->yaw,6.2831853f);
        if(outwardNormal){
            const V startDir{-std::sin(entry->yaw),std::cos(entry->yaw),0};
            const float crossZ=startDir.x*outwardNormal->y-startDir.y*outwardNormal->x;
            dYaw=std::copysign(std::abs(dYaw),crossZ>=0.f?1.f:-1.f);
        }
        yaw=entry->yaw+dYaw*smooth(entryAge(*entry)/entry->seconds);
    }
    at<float>(ped,0x558)=at<float>(ped,0x55C)=yaw;
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(ped,yaw);
}
static V flat(V v){v.z=0;return normalized(v);}
static float dist2d(V a,V b){return std::hypot(a.x-b.x,a.y-b.y);}
static V tangent(V normal){return {-normal.y,normal.x,0};}
static void applyAimFacing(void* ped,V view,void* cam,bool syncRw){
    if(!ped||!finite(view)||std::hypot(view.x,view.y)<.01f)return;
    const bool introTurn=(cover.wall.low||cover.fireCorner)&&!cover.returning&&
        cover.aimFacing.seconds>0.f&&entryAge(cover.aimFacing)<cover.aimFacing.seconds;
    if(introTurn){
        const V along=tangent(cover.wall.normal);
        const float normalOutYaw=std::atan2(cover.wall.normal.x,-cover.wall.normal.y);
        const float viewYaw=std::atan2(-view.x,view.y);
        const V sideDir=along*float(cover.fireSide);
        const float sideYaw=std::atan2(-sideDir.x,sideDir.y);
        const float targetRootYaw=sideYaw+std::remainder(viewYaw-normalOutYaw,6.2831853f);
        face(ped,{-std::sin(targetRootYaw),std::cos(targetRootYaw),0.f},&cover.aimFacing);
    }else{
        const int weapon=combatWeapon(ped);
        const int skill=reinterpret_cast<int(__thiscall*)(void*)>(0x5E6580)(ped);
        auto* info=weapon>=0?reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(weapon,skill):nullptr;
        const bool armAim=info&&(at<unsigned>(info,0x18)&2u)!=0&&(at<unsigned>(ped,0x46C)&0x04000000u)==0;
        if(!armAim&&cam){
            const float fov=at<float>(cam,0xB4);
            const float cx=*reinterpret_cast<const float*>(0xB6EC14);
            const float hAngle=(std::isfinite(fov)&&fov>1.f&&fov<179.f&&std::isfinite(cx)&&cx>=0.f&&cx<=1.f)
                ?std::atan((cx-.5f)*2.f*std::tan(fov*.5f*.0174532925f)):0.f;
            const float bodyYaw=std::atan2(-view.x,view.y)-hAngle;
            at<float>(ped,0x558)=at<float>(ped,0x55C)=bodyYaw-.05f;
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(ped,bodyYaw);
        }else{
            face(ped,view,nullptr);
        }
    }
    if(syncRw)reinterpret_cast<void(__thiscall*)(void*)>(0x446F90)(ped);
}
static bool ground(V origin,float& height){
    ColPoint hit{};void* floor=nullptr;
    V from=origin+V{0,0,.35f};
    const auto trace=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    if(!trace(from,origin.z-2.6f,hit,floor,true,false,false,true,true,false,nullptr)||!finite(hit.point)||hit.normal.z<.5f)return false;
    height=hit.point.z;
    return true;
}
static bool ray(V from,V to,ColPoint& hit,void*& entity){
    return lineOfSight(from,to,hit,entity,true,true,false,true,true,false,false,false);
}
static void cameraBegin(){
    auto* cam=activeCamera();
    const V previous=cam?at<V>(cam,0x190):V{};
    const bool aiming=owns(player())&&cover.aiming;
    if(viewState.cam){
        if(aiming){
            at<V>(viewState.cam,0x190)=viewState.nativeFront;
            at<V>(viewState.cam,0x19C)=viewState.nativeSource;
            at<V>(viewState.cam,0x1B4)=viewState.nativeUp;
        }
        viewState.cam=nullptr;
    }
    viewState.previousFront=previous;
    if(!aiming)viewState.seeded=false;
}
static void applyCamera(void* camera){
    auto* ped=player();auto* cam=activeCamera();
    if(!owns(ped)||!cam||!aimCamera(cam))return;
    auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
    // Camera processing can observe L2 before the next cover movement tick.
    // Capture that very first aimed frame, before the native reset is visible.
    if(aimInput(pad)&&!holdsFire(ped))updateAim(true,gameTime());
    if(!cover.aiming)return;
    auto& pose=at<M>(camera,0x974);
    const bool enteringAim=!viewState.seeded;
    if(enteringAim){
        const V seed=viewState.previousFront;
        const float seedHoriz=std::hypot(seed.x,seed.y),nativeHoriz=std::hypot(pose.up.x,pose.up.y);
        if(finite(seed)&&seedHoriz>.2f&&nativeHoriz>.2f){
            const V a=flat(pose.up),b=flat(seed);
            const float c=std::clamp(dot(a,b),-1.f,1.f);
            viewState.offset=c<-.9999f?Q{0.f,0.f,1.f,0.f}:unit({0.f,0.f,a.x*b.y-a.y*b.x,1.f+c});
        }else{
            viewState.offset=Q{};
        }
        viewState.seeded=true;
    }
    const V front=normalized(rotate(viewState.offset,pose.up));
    const V left=normalized(cross(V{0,0,1},front)),up=cross(front,left);
    const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    const float wantPivotZ=((at<unsigned>(ped,0x46C)&0x04000000u)!=0||(cover.wall.low&&cover.fireCorner))?.24f:.55f;
    if(enteringAim)viewState.pivotZ=wantPivotZ;
    else viewState.pivotZ+=(wantPivotZ-viewState.pivotZ)*(1.f-std::exp(-dt*12.f));
    const V pivot=position(ped)+V{0,0,viewState.pivotZ};
    V source=pivot+rotate(viewState.offset,pose.pos-pivot);
    viewState.unoccludedSource=false;
    {
        // Native Process_AimWeapon runs wall collision along its pre-rotated heading
        // (up to 90 deg off). Always consume the pre-collision source when available
        // before running our 5-ray sweep along the true aimed camera vector.
        V unoccluded{};
        if(cover_native_camera::unoccludedSource(cam,reference(ped),
            *reinterpret_cast<const unsigned*>(0xB7CB4C),at<V>(cam,0x19C),pose.pos,unoccluded)){
            source=pivot+rotate(viewState.offset,unoccluded-pivot);
            viewState.unoccludedSource=true;
        }
    }
    const float openSign=cover.fireCorner
        ?(dot(tangent(cover.wall.normal)*float(cover.fireSide),left)>=0.f?1.f:-1.f)
        :(dot(source-pivot,left)>=0.f?1.f:-1.f);
    // Keep SA's native right shoulder for right corners and centre walls; mirror
    // onto the left shoulder when exposing a left corner so cover blocks nothing.
    const float nativeShoulder=dot(source-pivot,left);
    if(enteringAim)viewState.shoulder=nativeShoulder;
    const float wantedShoulder=std::copysign(std::abs(nativeShoulder),openSign);
    viewState.shoulder+=(wantedShoulder-viewState.shoulder)*(1.f-std::exp(-dt*16.f));
    source=source+left*(viewState.shoulder-nativeShoulder);
    // Native right-shoulder placement may be inside the wall on a left edge.
    // Sweep the centre and near-plane corners from CJ's exposed upper body.
    float fraction=1.f;
    for(const V offset:{V{},left*.18f,left*-.18f,up*.14f,up*-.14f}){
        ColPoint hit{};void* object=nullptr;
        const V delta=source+offset-pivot;
        const float distance=length(delta);
        if(distance>.01f&&ray(pivot,source+offset,hit,object))
            fraction=std::min(fraction,std::clamp((length(hit.point-pivot)-.22f)/distance,0.f,1.f));
    }
    source=pivot+(source-pivot)*fraction;
    pose.pos=source;pose.up=front;pose.right=left;pose.at=up;
    viewState.cam=cam;viewState.nativeFront=at<V>(cam,0x190);
    viewState.nativeSource=at<V>(cam,0x19C);viewState.nativeUp=at<V>(cam,0x1B4);
    at<V>(cam,0x190)=front;at<V>(cam,0x19C)=source;at<V>(cam,0x1B4)=up;
    // CCam::Process_AimWeapon writes CJ's heading/RW matrix from nativeFront before applyCamera
    // rotates the camera (for two-handed guns and crouched cover), so re-apply aimed facing here.
    applyAimFacing(ped,flat(front),cam,true);
}
static bool wallRay(V from,V direction,float reach,Wall& wall){
    ColPoint hit{};void* entity=nullptr;
    if(!ray(from,from+direction*reach,hit,entity)||!entity||!finite(hit.point)||!finite(hit.normal)||std::abs(hit.normal.z)>.4f)return false;
    const V normal=flat(hit.normal);
    if(dot(normal,direction)>-.55f)return false;
    const int vehicle=vehicleRef(entity);
    if(vehicle>=0&&length(at<V>(entity,0x44))*50.f>2.f)return false;
    wall={vehicle,hit.point,normal,false};
    return true;
}
#include "CoverWallContinuity.hpp"
static bool traceWall(V root,const Wall& expected,Wall& hit){
    float height;
    if(!finite(root)||!cover_wall_continuity::validPlane(expected.point,expected.normal)||
       !ground(root,height)||!std::isfinite(height))return false;
    root.z=height+.60f;
    const V from=root+expected.normal*.12f,to=from-expected.normal*.95f;
    ColPoint contact{};void* entity=nullptr;
    if(!ray(from,to,contact,entity)||!entity||!finite(contact.point)||!finite(contact.normal))return false;
    // Preserve the original nominal waist-ray predicates, including vehicles.
    // Empty rays never reach the fallback or query a different collider.
    if(std::abs(contact.normal.z)<=.4f){
        const V normal=flat(contact.normal);
        if(dot(normal,expected.normal)<.55f)return false;
        const int vehicle=vehicleRef(entity);
        if(vehicle>=0&&length(at<V>(entity,0x44))*50.f>2.f)return false;
        hit={vehicle,contact.point,normal,false};return true;
    }
    if(!cover_wall_continuity::retryRounded(expected.low,expected.vehicle,contact.point,contact.normal,
                                           expected.point,expected.normal))return false;
    const unsigned type=at<unsigned char>(entity,0x36)&7;
    if(!cover_wall_continuity::eligibleType(type)||!(at<unsigned char>(entity,0x1C)&1))return false;
    // Only CObject owns these physical fields. Never read them from a building,
    // dummy, pedestrian, vehicle, or an unrecognized entity type.
    if(type==4&&!cover_wall_continuity::stationary(type,(at<unsigned char>(entity,0x1C)&4)!=0,
                                                  at<V>(entity,0x44),at<V>(entity,0x50)))return false;
    auto* col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(entity);
    if(!col||!col->data)return false;
    M frame{};
    if(auto* current=matrix(entity))frame=*current;
    else reinterpret_cast<void(__thiscall*)(void*,M*)>(0x54EF40)(static_cast<unsigned char*>(entity)+4,&frame);
    if(!finite(frame.pos)||!finite(frame.right)||!finite(frame.up)||!finite(frame.at))return false;
    const float determinant=dot(frame.right,cross(frame.up,frame.at));
    if(!std::isfinite(determinant)||std::abs(determinant)<1.e-6f)return false;
    struct Line {V from;float fromSize;V to;float toSize;};
    static_assert(sizeof(Line)==0x20);
    for(float offset:cover_wall_continuity::probeOffsets){
        const Line line{from+V{0,0,offset},0,to+V{0,0,offset},0};
        ColPoint recovered{};float fraction=1.f;
        // Same verified CCollision ABI as probeVehicleSurface. This targets
        // the freshly hit collider without changing CWorld ignore/collision state.
        if(!reinterpret_cast<bool(__cdecl*)(const Line&,const M&,ColModel&,ColPoint&,float&,bool,bool)>(0x417950)
                (line,frame,*col,recovered,fraction,false,false)||!std::isfinite(fraction)||fraction<0.f||fraction>1.f)continue;
        if(!cover_wall_continuity::recovered(recovered.point,recovered.normal,expected.point,expected.normal))continue;
        hit={-1,recovered.point,flat(recovered.normal),false};return true;
    }
    return false;
}
static bool matches(const Wall& found,const Wall& wall){
    return dot(found.normal,wall.normal)>.82f&&
        (wall.vehicle<0?found.vehicle<0:found.vehicle==wall.vehicle);
}
static bool retainMatches(const Wall& found,const Wall& wall){
    return dot(found.normal,wall.normal)>.68f&&
        (wall.vehicle<0?found.vehicle<0:found.vehicle==wall.vehicle);
}
static bool supportWall(V root,const Wall& expected,Wall& hit){
    if(traceWall(root,expected,hit))return true;
    float height;
    if(!finite(root)||!cover_wall_continuity::validPlane(expected.point,expected.normal)||
       !ground(root,height)||!std::isfinite(height))return false;
    const bool lowOrVeh=expected.vehicle>=0||expected.low;
    const std::array<float,4> zProbes=lowOrVeh?std::array<float,4>{-.24f,-.12f,0.f,.14f}:
        std::array<float,4>{0.f,-.20f,.24f,.35f};
    const float maxDepth=lowOrVeh?.38f:.28f;
    for(float dz:zProbes){
        const V from=V{root.x,root.y,height+.60f+dz}+expected.normal*.22f;
        const V to=from-expected.normal*1.20f;
        ColPoint contact{};void* entity=nullptr;
        if(!ray(from,to,contact,entity)||!entity||!finite(contact.point)||!finite(contact.normal))continue;
        if(std::abs(contact.normal.z)>.45f)continue;
        const V normal=flat(contact.normal);
        if(dot(normal,expected.normal)<.55f)continue;
        if(std::abs(dot(contact.point-expected.point,expected.normal))>maxDepth)continue;
        const int vehicle=vehicleRef(entity);
        if(expected.vehicle>=0?vehicle!=expected.vehicle:vehicle>=0)continue;
        if(vehicle>=0&&length(at<V>(entity,0x44))*50.f>2.f)continue;
        hit={vehicle,contact.point,normal,false};return true;
    }
    return false;
}
static bool sameWall(V root,const Wall& wall){
    Wall found;
    return supportWall(root,wall,found)&&retainMatches(found,wall);
}
static void refreshCorner(V root,bool aim){
    const V along=tangent(cover.wall.normal);
    const auto edgeOpen=[&](int side){
        const auto clearAt=[&](float distance){return !sameWall(root+along*(side*distance),cover.wall);};
        // Retention must include the entry condition. Disjoint near/far probes
        // oscillated at a stationary root when a seam ended between the rays.
        const bool entry=clearAt(.43f)&&clearAt(.80f);
        return entry||(cover.corner&&clearAt(.58f)&&clearAt(.95f));
    };
    const bool open=edgeOpen(cover.side),other=edgeOpen(-cover.side);
    // L2 can expose the available edge even when the last strafe faced away.
    if(aim&&!open&&other){cover.side=-cover.side;cover.turn={};}
    cover.corner=open||(aim&&other);
}
static V cornerGoal(V root,int side){
    const V along=tangent(cover.wall.normal)*float(side);
    const float extra=side<0?.34f:.26f;
    // Find the actual end of the obstacle; left corners need extra clearance so SA's right-handed gun clears the wall.
    float edge=.06f;
    while(edge<.66f&&sameWall(root+along*edge,cover.wall))edge+=.06f;
    V goal=root+along*(edge+extra);
    ColPoint block{};void* ent=nullptr;
    if(ray(root+V{0,0,.65f},goal+V{0,0,.65f},block,ent)&&finite(block.point)){
        const float clearDist=dot(block.point-root,along)-.28f;
        if(clearDist>.04f&&clearDist<edge+extra)goal=root+along*clearDist;
    }
    float rootFloor=0.f,goalFloor=0.f;
    if(ground(root,rootFloor)&&(!ground(goal,goalFloor)||std::abs(goalFloor-rootFloor)>.28f)){
        for(float step=dot(goal-root,along)-.06f;step>=.06f;step-=.06f){
            const V candidate=root+along*step;
            if(ground(candidate,goalFloor)&&std::abs(goalFloor-rootFloor)<=.28f){goal=candidate;break;}
        }
    }
    return goal;
}
static bool findWall(void* ped,V ahead,float reach,Wall& picked,V& destination){
    const V root=position(ped);
    float height;
    if(!ground(root,height))return false;
    const V waist{root.x,root.y,height+.60f};
    const V lowWaist{root.x,root.y,height+.40f};
    const V right=tangent(ahead);
    float best=reach+1.f;
    for(float side:{0.f,-.24f,.24f,-.48f,.48f}){
        const V direction=flat(ahead+right*side);
        Wall wall;
        if(!wallRay(waist,direction,reach,wall)&&!wallRay(lowWaist,direction,reach,wall))continue;
        if(dot(wall.normal,ahead)>-.45f||dot(wall.normal,direction)>-.55f)continue;
        V stand=wall.point+wall.normal*.46f;
        stand.z=root.z;
        float groundHeight;
        if(!ground(stand,groundHeight)||std::abs(groundHeight-height)>.28f)continue;
        const V travel2d{stand.x-root.x,stand.y-root.y,0};
        const float travelLen=length(travel2d);
        if(travelLen>.28f){
            const V travelDir=travel2d*(1.f/travelLen);
            const V sideAxis=tangent(travelDir);
            float midHeight=0.f;
            const V mid{(root.x+stand.x)*.5f,(root.y+stand.y)*.5f,root.z};
            if(!ground(mid,midHeight)||std::abs(midHeight-height)>.28f||std::abs(midHeight-groundHeight)>.24f)continue;
            const float clearDist=travelLen-.14f;
            bool blocked=false;
            if(clearDist>.12f)for(float lane:{0.f,-.20f,.20f}){
                for(float zOff:{.42f,1.05f}){
                    const V p0{root.x+sideAxis.x*lane,root.y+sideAxis.y*lane,height+zOff};
                    const V p1=p0+travelDir*clearDist;
                    ColPoint block{};void* blockEntity=nullptr;
                    if(ray(p0,p1,block,blockEntity)&&
                       (!finite(block.point)||dist2d(block.point,wall.point)>.50f||std::abs(dot(block.point-wall.point,wall.normal))>.18f)){
                        blocked=true;break;
                    }
                }
                if(blocked)break;
            }
            if(blocked)continue;
        }
        Wall backing{};
        if(!supportWall(stand,wall,backing)||!matches(backing,wall))continue;
        const V along=tangent(wall.normal);
        const bool wide=(sameWall(stand+along*.14f,wall)&&sameWall(stand-along*.14f,wall))||
            sameWall(stand+along*.28f,wall)||sameWall(stand-along*.28f,wall);
        if(!wide)continue;
        const V standWaist{stand.x,stand.y,groundHeight+.65f};
        bool tight=false;
        for(const V& probe:{along*.24f,along*-.24f,wall.normal*.24f}){
            ColPoint c{};void* e=nullptr;
            if(ray(standWaist,standWaist+probe,c,e)){tight=true;break;}
        }
        if(tight)continue;
        const float cost=length(stand-root)+std::abs(side)*.95f;
        if(cost>=best)continue;
        ColPoint upper{};void* top=nullptr;
        const V upperFrom=V{stand.x,stand.y,height+1.4f};
        wall.low=!ray(upperFrom,upperFrom-wall.normal*.82f,upper,top);
        picked=wall;destination=stand;best=cost;
    }
    return best<reach+1.f;
}
static bool preparePose(PoseReader&,void*);
static ClipMeta clipMeta(PoseReader&,void*,const std::string&);
static const char* weaponPose(int weapon){
    return weapon>=25&&weapon!=28&&weapon!=32?"rifle":weapon>=22?"pistol":"unarmed";
}
static std::string keyFor(bool low,bool corner,int side,const char* name){
    return std::string("cover_")+(side>0?"r_":"l_")+(low?"low_":"high_")+
        (corner?"corner/":"centre/")+name;
}
static const char* blindClip(int weapon){
    if(weapon>=22&&weapon<=24)return "pistol_blindfire";
    if(weapon>=25&&weapon<=27)return "shotgun_blindfire";
    if(weapon==28||weapon==32)return "uzi_blindfire";
    if(weapon==30||weapon==31)return "ak47_blindfire";
    return weapon==29||weapon==33?"rifle_blindfire":nullptr;
}
static std::string animationKey(bool low,bool corner,int side,int weapon,const char* suffix){
    return keyFor(low,corner,side,weaponPose(weapon))+suffix;
}
static std::string diveKey(void* ped){
    const char* held=weaponPose(combatWeapon(ped));
    return std::string("cover_dive/")+(cover.wall.low?"low_":"high_")+
        (cover.side>0?"r_":"l_")+(held[0]=='r'?"rifle":"pistol")+(cover.shortDive?"_short":"");
}
static unsigned coverKey(){
    static const unsigned key=[](){
        const auto ini=(folder.parent_path()/"DeliriumSA.ini").wstring();
        const unsigned k=GetPrivateProfileIntW(L"Player",L"CoverKey",'B',ini.c_str());
        return k&&k<=255?k:unsigned('B');
    }();
    return key;
}
static bool r1(void* pad){
    using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    static const GetState get=[]()->GetState {
        for(const wchar_t* name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"})
            if(auto module=GetModuleHandleW(name))if(auto fn=GetProcAddress(module,"XInputGetState"))
                return reinterpret_cast<GetState>(fn);
        return nullptr;
    }();
    DWORD foreground=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId())return false;
    if(pad&&!at<short>(pad,0x10E)){
        const unsigned k=coverKey();
        const auto* newKeys=reinterpret_cast<const short*>(0xB73190+0x18);
        if(newKeys[k]!=0||(GetAsyncKeyState(int(k))&0x8000)!=0)return true;
    }
    XINPUT_STATE input{};
    if(get&&get(0,&input)==ERROR_SUCCESS)return (input.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
    return false;
}
static void tick(bool down){
    const bool pressed=down&&!wasDown;
    wasDown=down;
    auto* ped=player();
    if(!pressed&&!owns(ped))return;
    auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
    if(!pad||at<short>(pad,0x10E)||!available(ped)){leave("control_boundary");return;}
    if(pressed&&owns(ped)){leave("button");return;}
    if(pressed){
        departedAt=0;
        const float px=std::clamp(reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad)/128.f,-1.f,1.f);
        const float py=std::clamp(-reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad)/128.f,-1.f,1.f);
        auto* camera=activeCamera();
        const V camView=camera?flat(at<V>(camera,0x190)):flat(matrix(ped)->up);
        const V wish=V{camView.y,-camView.x,0}*px+camView*py;
        const V bodyAhead=flat(matrix(ped)->up);
        const int gait=at<int>(ped,0x534);
        const V rawVel=at<V>(ped,0x44);
        const float moveSpeed=std::hypot(rawVel.x,rawVel.y)*50.f;
        const bool walkingOnly=nikoActive&&nikoState[3]>.05f&&nikoState[3]<1.35f;
        const bool running=!walkingOnly&&(gait==7||(gait==6&&(moveSpeed>=2.0f||!nikoActive||nikoState[3]==0.f)));
        const float reach=running?6.0f:2.2f;
        Wall wall;V destination;
        bool found=false;
        if(length(wish)>.25f){
            found=findWall(ped,flat(wish),reach,wall,destination);
            if(!found)found=findWall(ped,bodyAhead,std::min(reach,2.2f),wall,destination);
            if(!found)found=findWall(ped,camView,std::min(reach,2.2f),wall,destination);
        }else{
            found=findWall(ped,camView,reach,wall,destination);
            if(!found)found=findWall(ped,bodyAhead,reach,wall,destination);
        }
        if(!found)return;
        cover.ref=reference(ped);
        cover.wall=wall;
        cover.destination=destination;
        cover.lastSafe=destination;
        const V view=camView;
        cover.side=dot(tangent(wall.normal),V{view.y,-view.x,0})>=0.f?1:-1;
        cover.entered=gameTime();
        cover.runningEntry=running;
        cover.walkTime=0.f;
        cover.stallPos=position(ped);
        cover.stalledSince=cover.entered;
        const float distance=dist2d(destination,position(ped));
        cover.entry=startEntry(ped,cover.entered,std::clamp(distance/(running?4.0f:1.85f),.18f,1.80f));
        cover.approaching=true;
        if(running&&distance>1.5f){
            cover.shortDive=distance<4.f;
            float meta[2]{};
            if(!preparePose(poseReader,ped)||nm_locomotion_clip_pose(combat,poseReader.reader,diveKey(ped).c_str(),0,
                poseReader.rows.data(),int(poseReader.rig->ids.size()),meta)!=1||!(meta[0]>0.f)){
                cover={};return;
            }
            cover.diveSeconds=meta[0];
            cover.entry.seconds=meta[0];
        }
        event("player_cover_entered",cover.ref,0,std::string(wall.low?"low":"high")+
            (cover.diveSeconds>0.f?" dive":cover.entry.seconds>.30f?" aimed":" standing"));
    }
    if(!owns(ped))return;
    if(cover.wall.vehicle>=0){
        auto* vehicle=vehicleFromRef(cover.wall.vehicle);
        if(!vehicle||length(at<V>(vehicle,0x44))*50.f>2.f){leave("vehicle_moved");return;}
    }
    const int weapon=combatWeapon(ped);
    const bool requested=weapon>=22&&weapon<=34&&aimInput(pad);
    if(!cover.approaching&&!cover.peeking)refreshCorner(position(ped),requested);
    const bool aim=requested&&!holdsFire(ped);
    const bool blind=!aim&&!cover.approaching&&!cover.turn.started&&(cover.wall.low||cover.corner)&&blindClip(weapon)&&
        reinterpret_cast<int(__thiscall*)(void*,void*)>(0x540180)(pad,ped)!=0;
    updateAim(aim,gameTime());
    if(blind&&!cover.blind){cover.blindStarted=gameTime();cover.blindReleased=0;cover.blindShotAt=0;}
    if(!blind&&cover.blind)cover.blindReleased=gameTime();
    cover.blind=blind;
    setDuck(ped,cover.duck,cover.wall.low&&(!aim||cover.fireCorner)&&!cover.approaching);
}
static void sample(){
    auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
    tick(r1(pad));
}
static bool move(void* ped){
    if(!owns(ped))return false;
    if(!available(ped)){leave("interrupted");return false;}
    const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    const V current=position(ped);
    if(!(dt>0.f))return true;
    const V along=tangent(cover.wall.normal);
    const char* walk=cover.wall.low?"move_crouch/walk":"move_player/walk";
    V delta=cover.destination-current;delta.z=0;
    V wallVelocity{};
    if(cover.approaching){
        Wall arrivedWall{};
        const float normalGap=dot(current-cover.wall.point,cover.wall.normal);
        const float alongGap=std::abs(dot(current-cover.destination,along));
        const bool hasSup=supportWall(current,cover.wall,arrivedWall)&&retainMatches(arrivedWall,cover.wall);
        const bool arrived=cover.diveSeconds<=0.f
            ?(hasSup&&(length(delta)<.11f||(normalGap>=.32f&&normalGap<=.58f&&alongGap<.16f)))
            :((length(delta)<.11f&&hasSup)||(entryAge(cover.entry)>=cover.diveSeconds*.85f&&normalGap>=.30f&&normalGap<=.62f&&alongGap<.22f&&hasSup));
        if(arrived){
            cover.approaching=false;
            cover.standOff=std::clamp(normalGap,.40f,.52f);
            cover.lastSafe=current;
        }else if(length(delta)<.11f&&!hasSup){
            Wall destWall{};
            if(supportWall(cover.destination,cover.wall,destWall)&&retainMatches(destWall,cover.wall)){
                cover.approaching=false;
                cover.standOff=.46f;
                cover.lastSafe=cover.destination;
            }else{
                leave("approach_blocked");return false;
            }
        }else{
            const unsigned now=gameTime();
            const unsigned limit=std::max(1400u,unsigned((cover.diveSeconds>0.f?cover.diveSeconds:cover.entry.seconds)*1000.f)+600u);
            if(dist2d(current,cover.stallPos)>=.05f){cover.stallPos=current;cover.stalledSince=now;}
            else if(cover.diveSeconds<=0.f&&length(delta)>.35f&&now-cover.stalledSince>550u){leave("approach_blocked");return false;}
            if(now-cover.entered>limit){leave("approach_blocked");return false;}
            if(cover.diveSeconds<=0.f&&length(delta)>.35f){
                ColPoint obs{};void* obsEnt=nullptr;
                const V fwd=flat(delta);
                const V feelFrom=current+V{0,0,.55f};
                if(ray(feelFrom,feelFrom+fwd*.65f,obs,obsEnt)&&finite(obs.point)&&finite(obs.normal)&&
                   obs.normal.z<.45f&&dist2d(obs.point,cover.wall.point)>.40f){
                    V t=tangent(flat(obs.normal));
                    if(dot(t,fwd)<0.f)t=V{}-t;
                    const V bypass=flat(t*.85f+flat(obs.normal)*.25f);
                    delta=bypass*length(delta);
                }
            }
        }
    }
    if(!cover.approaching){
        const V support=cover.peeking?cover.peekBase:current;
        Wall wall{};
        bool hasWall=supportWall(support,cover.wall,wall)&&retainMatches(wall,cover.wall);
        V recoverDelta{};
        if(!hasWall&&!cover.peeking&&dist2d(current,cover.lastSafe)<.85f&&
           supportWall(cover.lastSafe,cover.wall,wall)&&retainMatches(wall,cover.wall)){
            hasWall=true;
            recoverDelta=cover.lastSafe-current;recoverDelta.z=0;
        }
        if(!hasWall){leave("cover_lost");return false;}
        if(!cover.peeking&&length(recoverDelta)==0.f)cover.lastSafe=current;
        if(!cover.peeking&&!cover.aiming&&matches(wall,cover.wall)&&length(wall.point-cover.wall.point)<.45f){
            cover.wall.point=wall.point;cover.wall.normal=wall.normal;
        }
        auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
        const float x=std::clamp(reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad)/128.f,-1.f,1.f);
        const float y=std::clamp(-reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad)/128.f,-1.f,1.f);
        auto* camera=activeCamera();
        const V view=camera?flat(at<V>(camera,0x190)):flat(matrix(ped)->up);
        const int weapon=combatWeapon(ped);
        const bool requested=weapon>=22&&weapon<=34&&aimInput(pad);
        if(!cover.peeking)refreshCorner(current,requested);
        updateAim(requested&&!holdsFire(ped),gameTime());
        if(!cover.peeking&&cover.returning&&cover.returnFacing.seconds>0.f&&entryAge(cover.returnFacing)>=cover.returnFacing.seconds)
            cover.returning=0;
        setDuck(ped,cover.duck,cover.wall.low&&(!cover.aiming||cover.fireCorner));
        if(cover.aiming)applyAimFacing(ped,view,camera,false);
        const V wish=V{view.y,-view.x,0}*x+view*y;
        const float wishLen=length(wish);
        // IV main.rs:2693: step away / sprint out only when pressed to cover (not mid-aim or corner peek/return).
        if(!cover.aiming&&!cover.peeking&&!cover.returning&&!cover.blind&&!cover.turn.started&&wishLen>.25f){
            const bool sprintOut=pad&&reinterpret_cast<bool(__thiscall*)(void*)>(0x5407A0)(pad);
            if(sprintOut||dot(wish,cover.wall.normal)>.70f*wishLen){leave("step_away");return false;}
        }
        const float slide=dot(wish,along);
        if(cover.turn.started&&entryAge(cover.turn)>=cover.turn.seconds)cover.turn={};
        const bool canSlide=!cover.aiming&&!cover.blind&&!cover.peeking&&!cover.returning;
        const int next=slide>0.f?1:-1;
        const bool sameSide=next==cover.side;
        const bool hasRoom=std::abs(slide)>.18f&&sameWall(current+along*(.16f*next),cover.wall);
        if(canSlide&&!cover.turn.started&&!sameSide&&hasRoom&&std::abs(slide)>.24f&&std::abs(cover.slideSpeed)<.18f){
            const auto turn=animationKey(cover.wall.low,false,next,combatWeapon(ped),"_flip_180");
            const float duration=clipMeta(poseReader,ped,turn).duration;
            cover.turn=startEntry(ped,gameTime(),duration>0.f?std::clamp(duration,.35f,.85f):.45f);
            cover.side=next;
        }
        if(cover.peeking&&!cover.aiming){
            V backDelta=cover.peekBase-current;backDelta.z=0;
            Wall curWall{};
            const bool backSupported=length(backDelta)<.14f||
                (cover.returning&&gameTime()-cover.returning>450u&&supportWall(current,cover.wall,curWall)&&retainMatches(curWall,cover.wall));
            if(backSupported||(cover.returning&&gameTime()-cover.returning>2200u))cover.peeking=false;
            else cover.destination=cover.peekBase;
        }
        if(cover.aiming&&cover.corner){
            if(!cover.peeking){
                cover.peekBase=current;
                cover.peeking=true;
                cover.peekTarget=cornerGoal(cover.peekBase,cover.fireSide);
            }
            cover.destination=cover.peekTarget;
            wallVelocity={};
        }else if(!cover.peeking){
            const bool pushing=cover.corner&&!hasRoom&&std::abs(cover.slideSpeed)<.16f&&
                slide*cover.side>.18f&&canSlide&&!cover.turn.started;
            if(pushing&&!cover.peekStarted)cover.peekStarted=gameTime();
            if(!pushing)cover.peekStarted=0;
            const float gap=dot(current-wall.point,cover.wall.normal);
            const float error=cover.standOff-gap;
            const float drift=std::copysign(std::min(.28f,std::max(0.f,std::abs(error)-.045f)*4.5f),error);
            cover.destination=current;
            wallVelocity=length(recoverDelta)>.01f?flat(recoverDelta)*std::min(2.2f,length(recoverDelta)*10.f):cover.wall.normal*drift;
        }
        const float targetSpeed=length(recoverDelta)<=.01f&&canSlide&&!cover.turn.started&&hasRoom&&sameSide?slide*1.25f:0.f;
        cover.slideSpeed+=std::clamp(targetSpeed-cover.slideSpeed,-7.f*dt,7.f*dt);
        if(length(recoverDelta)<=.01f&&std::abs(cover.slideSpeed)>.025f&&canSlide&&!cover.turn.started&&
           sameWall(current+along*std::copysign(.16f,cover.slideSpeed),cover.wall)){
            wallVelocity=wallVelocity+along*cover.slideSpeed;
            const float authoredSpeed=clipMeta(poseReader,ped,walk).moverSpeed;
            cover.slideTime+=dt*std::abs(cover.slideSpeed)/std::max(.5f,authoredSpeed);
        }else if(length(recoverDelta)>.01f||!canSlide||cover.turn.started||std::abs(cover.slideSpeed)>.025f){
            cover.slideSpeed=0.f;
        }
        delta=cover.destination-current;delta.z=0;
    }
    const float distance=length(delta);
    float speed=cover.approaching?(cover.runningEntry?(distance>.65f?4.2f:2.8f):(distance>.45f?1.85f:1.55f)):cover.peeking?2.2f:1.45f;
    if(cover.approaching&&cover.diveSeconds>0.f){
        const float totalDistance=std::max(.5f,dist2d(cover.destination,cover.entry.from));
        speed=std::clamp(totalDistance/std::max(.45f,cover.diveSeconds),2.4f,5.2f);
    }
    const V velocity=wallVelocity+(distance>.015f?delta*(std::min(speed,distance*6.f)/distance):V{});
    if(cover.approaching&&cover.diveSeconds<=0.f){
        const char* moveClip=cover.runningEntry?"move_player/run":(cover.duck?"move_crouch/walk":"move_player/walk");
        const float authored=clipMeta(poseReader,ped,moveClip).moverSpeed;
        cover.walkTime+=dt*std::clamp(length(velocity)/std::max(.5f,authored),.5f,1.35f);
    }
    at<float>(ped,0x550)=velocity.x*.02f;
    at<float>(ped,0x554)=velocity.y*.02f;
    if(!cover.aiming||cover.turn.started){
        const V endFacing=along*float(cover.side);
        V facing=endFacing;
        if(cover.approaching){
            if(cover.diveSeconds>0.f){
                const float t=smooth((entryAge(cover.entry)/std::max(.2f,cover.diveSeconds)-.55f)/.40f);
                facing=normalized(flat(delta)*(1.f-t)+endFacing*t);
            }else if(distance>.30f){
                const float t=smooth((.60f-distance)/.32f);
                facing=normalized(flat(delta)*(1.f-t)+endFacing*t);
            }
        }
        const bool returnTurn=cover.returning&&cover.returnFacing.seconds>0.f&&entryAge(cover.returnFacing)<cover.returnFacing.seconds;
        const bool outwardFlip=cover.turn.started&&!cover.approaching;
        if(returnTurn)facing=V{-cover.wall.normal.x,-cover.wall.normal.y,0.f};
        face(ped,facing,cover.approaching?&cover.entry:outwardFlip?&cover.turn:returnTurn?&cover.returnFacing:nullptr,outwardFlip?&cover.wall.normal:nullptr);
    }
    return true;
}
static bool preparePose(PoseReader& slot,void* ped){
    const int model=at<short>(ped,0x22);
    if(slot.reader&&slot.ref==reference(ped)&&slot.model==model)return true;
    slot.reset();
    try{
        slot.rig=std::make_unique<Rig>(ped,sourceBuild);
        const std::vector<uint32_t> ids(slot.rig->ids.begin(),slot.rig->ids.end()),
            parents(slot.rig->parents.begin(),slot.rig->parents.end());
        std::vector<float> bind(ids.size()*7);
        for(size_t n=0;n<ids.size();++n){
            const auto& t=slot.rig->bind[n];auto* row=bind.data()+7*n;
            row[0]=t.p.x;row[1]=t.p.y;row[2]=t.p.z;row[3]=t.q.x;row[4]=t.q.y;row[5]=t.q.z;row[6]=t.q.w;
        }
        slot.reader=nm_locomotion_create(combat,ids.data(),parents.data(),bind.data(),int(ids.size()),at<float>(ped,0x558));
        slot.rows.resize(bind.size());slot.ref=reference(ped);slot.model=model;
    }catch(...){slot.reset();return false;}
    return slot.reader!=nullptr;
}
enum class Clip { Loop, Once, Peek, Hold };
static ClipMeta clipMeta(PoseReader& slot,void* ped,const std::string& key){
    if(!preparePose(slot,ped))return {};
    const auto [entry,added]=slot.clips.try_emplace(key);
    if(added){
        float info[2]{};
        if(nm_locomotion_clip_pose(combat,slot.reader,key.c_str(),0,slot.rows.data(),int(slot.rig->ids.size()),info)==1)
            entry->second={info[0],info[1]};
    }
    return entry->second;
}
static T rowPose(const float* row){return {{row[3],row[4],row[5],row[6]},{row[0],row[1],row[2]}};}
static bool drawClip(PoseReader& slot,void* ped,const std::string& key,float seconds,Clip playback,float weight,unsigned moverMask=0){
    if(!preparePose(slot,ped))return false;
    auto* h=hierarchy(ped);
    if(!h||!h->matrices||h->num!=int(slot.rig->ids.size()))return false;
    float meta[2]{};
    const float duration=clipMeta(slot,ped,key).duration;
    if(!(duration>0.f))return false;
    const float cycle=seconds/duration;
    if(playback==Clip::Once&&cycle>=1.f)return false;
    const float phase=playback==Clip::Peek?std::min(.5f,cycle):
        playback==Clip::Hold?std::min(.999f,cycle):std::fmod(cycle,1.f);
    if(nm_locomotion_clip_pose_masked(combat,slot.reader,key.c_str(),phase,slot.rows.data(),h->num,meta,moverMask)!=1)return false;
    const T root=transform(*matrix(ped));
    auto& bones=slot.bones;
    auto& native=slot.native;
    auto& authored=slot.authored;
    bones.resize(h->num);
    if(weight<1.f){native.resize(h->num);authored.resize(h->num);}
    for(int n=0;n<h->num;++n){
        const T pose=mul(root,rowPose(slot.rows.data()+n*7));
        if(weight>=1.f){bones[n]=pose;continue;}
        native[n]=transform(h->matrices[n]);
        authored[n]=pose;
        const int parent=slot.rig->parents[n];
        bones[n]=parent<0||parent>=n?mix(native[n],authored[n],weight):
            mul(bones[parent],mix(mul(inverse(native[parent]),native[n]),mul(inverse(authored[parent]),authored[n]),weight));
    }
    const auto palette=slot.rig->renderPalette(h->matrices,bones);
    std::copy(palette.begin(),palette.end(),h->matrices);
    return true;
}
static bool drawEntry(PoseReader& slot,void* ped,const Wall& wall,int side,int weapon,const Entry& entry){
    return drawClip(slot,ped,animationKey(wall.low,false,side,weapon,"_idle"),
        entryAge(entry),Clip::Loop,smooth(entryAge(entry)/entry.seconds));
}
static bool drawReturn(PoseReader& slot,void* ped,bool low,bool corner,int side,int weapon,float seconds){
    return drawClip(slot,ped,animationKey(low,corner,side,weapon,"_normal_fire_outro"),seconds,Clip::Once,1.f,0x600);
}
static std::string blindKey(int weapon){
    const auto* name=blindClip(weapon);
    return name?keyFor(cover.wall.low,cover.corner,cover.side,name):std::string{};
}
static float blindTime(float duration){
    if(!cover.blindStarted)return 0.f;
    const unsigned until=cover.blindReleased?cover.blindReleased:gameTime();
    const float raised=std::min(duration*.55f,float(until-cover.blindStarted)*.001f);
    return cover.blindReleased?std::min(duration,raised+float(gameTime()-cover.blindReleased)*.001f):raised;
}
static bool blindReady(void* ped){
    if(!owns(ped)||cover.aiming)return true;
    if(holdsFire(ped)||!cover.blind)return false;
    const float duration=clipMeta(poseReader,ped,blindKey(combatWeapon(ped))).duration;
    return duration>0.f&&blindTime(duration)>=duration*.30f;
}
static void confirmBlindShot(void*){cover.blindShotAt=gameTime();}
static bool drawPlayerPose(void* ped,Motion& motion){
    if(!owns(ped)){
        const unsigned age=gameTime()-departedAt;
        if(ped!=player()||!departedAt||age>=180||reference(ped)!=departed.ref||!available(ped))return false;
        return drawClip(poseReader,ped,animationKey(departed.wall.low,false,departed.side,combatWeapon(ped),"_idle"),
            0,Clip::Loop,1.f-float(age)/180.f);
    }
    const int weapon=combatWeapon(ped);
    const unsigned now=gameTime();
    const auto elapsed=[&](unsigned start){return float(now-start)*.001f;};
    const auto clip=[&](const char* suffix,bool corner,unsigned started,Clip playback){
        return drawClip(poseReader,ped,animationKey(cover.wall.low,corner,cover.side,weapon,suffix),elapsed(started),playback,1.f,0);
    };
    if(cover.approaching){
        const float age=entryAge(cover.entry);
        if(cover.diveSeconds>0&&drawClip(poseReader,ped,diveKey(ped),age,Clip::Once,smooth(age/.10f),0x600)){motion=Motion::Dive;return true;}
        auto* m=matrix(ped);
        const float dist=m?std::hypot(cover.destination.x-m->pos.x,cover.destination.y-m->pos.y):0.f;
        if(cover.entry.seconds>.26f&&dist>.32f){
            const char* moveClip=cover.runningEntry?"move_player/run":
                (cover.duck?"move_crouch/walk":"move_player/walk");
            const float w=smooth(age/.12f);
            const bool moving=drawClip(poseReader,ped,moveClip,cover.walkTime,Clip::Loop,w,0x600);
            const float idleW=smooth((.48f-dist)/.32f)*w;
            const bool idle=idleW>0.f&&drawClip(poseReader,ped,
                animationKey(cover.wall.low,false,cover.side,weapon,"_idle"),age,Clip::Loop,idleW);
            motion=idleW>=.5f?Motion::Entry:Motion::Walk;
            if(moving||idle)return true;
        }
        motion=Motion::Entry;
        return drawEntry(poseReader,ped,cover.wall,cover.side,weapon,cover.entry);
    }
    if(!cover.aiming&&(cover.blind||cover.blindShotAt)){
        const auto key=blindKey(weapon);
        const float duration=key.empty()?0.f:clipMeta(poseReader,ped,key).duration;
        const float age=blindTime(duration);
        if(duration>0.f&&age<duration){
            motion=Motion::Blind;
            const float fade=smooth((duration-age)/.15f);
            drawClip(poseReader,ped,animationKey(cover.wall.low,false,cover.side,weapon,"_idle"),elapsed(cover.entered),Clip::Loop,1.f);
            if(drawClip(poseReader,ped,key,age,Clip::Hold,fade))return true;
        }
    }
    if(cover.aiming){
        // Preserve authored mover_q turn (0x600 strips only horizontal translation);
        // aimFacing handles only any angular offset between view and -wall.normal.
        const bool intro=(cover.wall.low||cover.fireCorner)&&cover.exposed&&drawClip(poseReader,ped,
            animationKey(cover.wall.low,cover.fireCorner,cover.fireSide,weapon,"_normal_fire_intro"),elapsed(cover.exposed),Clip::Once,1.f,0x600);
        motion=intro?Motion::AimIntro:Motion::AimNative;
        return intro;
    }
    if(cover.returning&&(cover.wall.low||cover.fireCorner)&&drawReturn(poseReader,ped,
        cover.wall.low,cover.fireCorner,cover.fireSide,weapon,elapsed(cover.returning))){motion=Motion::Return;return true;}
    if(cover.turn.started){
        motion=Motion::Turn;
        const float age=entryAge(cover.turn);
        const auto key=animationKey(cover.wall.low,false,cover.side,weapon,"_flip_180");
        const auto duration=clipMeta(poseReader,ped,key).duration;
        const int idleSide=age<cover.turn.seconds*.5f?-cover.side:cover.side;
        drawClip(poseReader,ped,animationKey(cover.wall.low,false,idleSide,weapon,"_idle"),elapsed(cover.entered),Clip::Loop,1.f);
        if(duration>0.f)
            drawClip(poseReader,ped,key,duration*std::min(.999f,age/cover.turn.seconds),Clip::Once,
                smooth(age/.10f)*smooth((cover.turn.seconds-age)/.12f),0x3600);
        return true;
    }
    if(cover.peekStarted&&clip("_peek",true,cover.peekStarted,Clip::Peek)){motion=Motion::Peek;return true;}
    const bool idle=drawClip(poseReader,ped,animationKey(cover.wall.low,false,cover.side,weapon,"_idle"),
        elapsed(cover.entered),Clip::Loop,1.f);
    if(std::abs(cover.slideSpeed)<=.025f)return idle;
    motion=Motion::Walk;
    const float blend=smooth(std::abs(cover.slideSpeed)/.85f);
    return drawClip(poseReader,ped,cover.wall.low?"move_crouch/walk":"move_player/walk",cover.slideTime,Clip::Loop,blend,0x600)||idle;
}
static bool blendCoverPose(void* ped, PoseReader& poses, PoseTransition& blend, Motion motion,
                           unsigned action, float seconds){
    auto* h=hierarchy(ped);
    if(!h||!h->matrices||h->num<=0||h->num>256)return false;
    const int ref=reference(ped);
    const bool sameRig=blend.ref==ref&&int(blend.last.size())==h->num;
    if(!sameRig){blend.ref=ref;blend.motion=motion;blend.previous.clear();blend.last.clear();}
    if(blend.motion!=motion||blend.weapon!=combatWeapon(ped)||blend.action!=action){
        blend.previous=blend.last;
        blend.motion=motion;blend.weapon=combatWeapon(ped);blend.action=action;
        blend.started=gameTime();
    }
    const float weight=blend.previous.empty()?1.f:smooth(float(gameTime()-blend.started)*.001f/seconds);
    if(weight>=1.f)blend.previous.clear();
    const T root{Q{},position(ped)},inv=inverse(root);
    blend.last.resize(h->num);
    if(weight<1.f)blend.world.resize(h->num);
    for(int n=0;n<h->num;++n){
        const T current=mul(inv,transform(h->matrices[n]));
        blend.last[n]=weight<1.f?mix(blend.previous[n],current,weight):current;
        if(weight<1.f)blend.world[n]=mul(root,blend.last[n]);
    }
    if(weight<1.f){
        if(poses.rig&&int(poses.rig->ids.size())==h->num){
            const auto palette=poses.rig->renderPalette(h->matrices,blend.world);
            std::copy(palette.begin(),palette.end(),h->matrices);
        }else for(int n=0;n<h->num;++n)write(h->matrices[n],blend.world[n]);
    }
    for(int n=0;n<h->num;++n)blend.last[n]=mul(inv,transform(h->matrices[n]));
    return weight<1.f;
}
static bool draw(void* ped){
    Motion motion=Motion::Rest;
    const bool authored=drawPlayerPose(ped,motion);
    if(!owns(ped)||!matrix(ped))return authored;
    auto* h=hierarchy(ped);
    if(!h||!h->matrices||h->num<=0||h->num>256)return authored;
    // Blind-fire clips supply the raised-arm posture, not an absolute shot
    // direction. Align the arm as a chain so the visible muzzle agrees with
    // the camera-directed native ray without rotating CJ's hidden torso.
    if(!cover.aiming&&motion==Motion::Blind&&poseReader.rig){
        const int shoulder=poseReader.rig->index(weaponPose(combatWeapon(ped))[0]=='r'?saBone::chest:22),hand=poseReader.rig->index(24);
        auto* cam=activeCamera();
        const int skill=reinterpret_cast<int(__thiscall*)(void*)>(0x5E6580)(ped);
        auto* info=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(combatWeapon(ped),skill);
        if(shoulder>=0&&hand>=0&&cam&&info){
            const float duration=clipMeta(poseReader,ped,blindKey(combatWeapon(ped))).duration;
            const float age=blindTime(duration);
            const float gain=smooth(age/.20f)*smooth((duration-age)/.15f);
            const V target=at<V>(cam,0x19C)+normalized(at<V>(cam,0x190))*std::max(20.f,at<float>(info,8));
            for(int pass=0;pass<2;++pass){
                const V muzzle=point(h->matrices[hand],at<V>(info,0x24));
                const Q turn=mix(Q{},turnBetween(h->matrices[hand].right,target-muzzle),gain);
                const V pivot=h->matrices[shoulder].pos;
                for(int n=0;n<h->num;++n){
                    int parent=n;while(parent>=0&&parent!=shoulder)parent=poseReader.rig->parents[parent];
                    if(parent!=shoulder)continue;
                    auto& bone=h->matrices[n];
                    bone.right=rotate(turn,bone.right);bone.up=rotate(turn,bone.up);bone.at=rotate(turn,bone.at);
                    bone.pos=pivot+rotate(turn,bone.pos-pivot);
                }
            }
        }
    }
    const unsigned action=motion==Motion::Blind?cover.blindStarted:
        motion==Motion::Turn?cover.turn.started:motion==Motion::Peek?cover.peekStarted:
        motion==Motion::Return?cover.returning:cover.exposed;
    auto& blend=transition;
    const bool blending=blendCoverPose(ped,poseReader,blend,motion,action,motion==Motion::Blind?.18f:.14f);
    blend.blindGrip=motion==Motion::Blind;
    if(poseReader.rig){const int hand=poseReader.rig->index(24);
        if(hand>=0){blend.grip=h->matrices[hand];blend.gripAt=gameTime();}}
    return cover.aiming||authored||blending;
}
static bool blindShot(void* ped,V& muzzle,V& end,float range){
    if(!owns(ped)||cover.aiming||holdsFire(ped)||!matrix(ped))return false;
    const int weapon=combatWeapon(ped);
    const auto key=blindKey(weapon);
    const float duration=key.empty()?0.f:clipMeta(poseReader,ped,key).duration;
    if(!(duration>0.f))return false;
    const auto& ids=poseReader.rig->ids;
    const auto hand=std::find(ids.begin(),ids.end(),24);
    if(hand==ids.end())return false;
    float info[2]{};
    const float phase=std::clamp(blindTime(duration)/duration,.30f,.58f);
    if(nm_locomotion_clip_pose(combat,poseReader.reader,key.c_str(),phase,poseReader.rows.data(),int(ids.size()),info)!=1)return false;
    const T grip=mul(transform(*matrix(ped)),rowPose(poseReader.rows.data()+7*(hand-ids.begin())));
    const int skill=reinterpret_cast<int(__thiscall*)(void*)>(0x5E6580)(ped);
    auto* weaponInfo=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(weapon,skill);
    muzzle=grip.p+(weaponInfo?rotate(grip.q,at<V>(weaponInfo,0x24)):V{});
    if(weaponInfo&&transition.ref==cover.ref&&transition.blindGrip&&gameTime()-transition.gripAt<100)
        muzzle=point(transition.grip,at<V>(weaponInfo,0x24));
    auto* camera=activeCamera();
    const V target=camera?at<V>(camera,0x19C)+normalized(at<V>(camera,0x190))*range:
        muzzle-cover.wall.normal*range;
    end=muzzle+normalized(target-muzzle)*range;
    return finite(muzzle)&&finite(end)&&length(muzzle-position(ped))<2.25f&&length(end-muzzle)>2.f;
}
}
