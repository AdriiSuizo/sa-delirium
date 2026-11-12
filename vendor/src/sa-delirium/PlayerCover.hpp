#pragma once
#include <xinput.h>

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
    V destination{},peekBase{};
    void* duck=nullptr;
    Entry entry{},turn{};
    int side=1,fireSide=1;
    bool fireCorner=false;
    unsigned entered=0,exposed=0,returning=0,peekStarted=0,blindStarted=0,blindShotAt=0,blindReleased=0;
    float diveSeconds=0,slideSpeed=0,slideTime=0,standOff=.44f;
    bool shortDive=false;
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
    float shoulder=0.f;
    bool seeded=false;
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
static void updateAim(bool aim,unsigned now){
    if(aim&&!cover.aiming){cover.exposed=now;cover.returning=0;cover.fireSide=cover.side;cover.fireCorner=cover.corner;}
    if(!aim&&cover.aiming)cover.returning=now;
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
    return reinterpret_cast<bool(__thiscall*)(void*)>(0x540670)(pad);
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
static void face(void* ped,V direction,const Entry* entry=nullptr){
    float yaw=std::atan2(-direction.x,direction.y);
    if(entry)yaw=entry->yaw+std::remainder(yaw-entry->yaw,6.2831853f)*smooth(entryAge(*entry)/entry->seconds);
    at<float>(ped,0x558)=at<float>(ped,0x55C)=yaw;
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(ped,yaw);
}
static V flat(V v){v.z=0;return normalized(v);}
static V tangent(V normal){return {-normal.y,normal.x,0};}
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
    if(viewState.cam){
        at<V>(viewState.cam,0x190)=viewState.nativeFront;
        at<V>(viewState.cam,0x19C)=viewState.nativeSource;
        at<V>(viewState.cam,0x1B4)=viewState.nativeUp;
        viewState.cam=nullptr;
    }
    viewState.previousFront=previous;
    if(!owns(player())||!cover.aiming)viewState.seeded=false;
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
        viewState.offset=finite(seed)&&length(seed)>.9f?turnBetween(pose.up,seed):Q{};
        viewState.seeded=true;
    }
    const V front=normalized(rotate(viewState.offset,pose.up));
    const V left=normalized(cross(V{0,0,1},front)),up=cross(front,left);
    const V pivot=position(ped)+V{0,0,.55f};
    V source=pivot+rotate(viewState.offset,pose.pos-pivot);
    // SA has one native shoulder. Mirror its lateral displacement for the
    // opposite cover side while preserving aim direction and camera pitch.
    const float nativeShoulder=dot(source-pivot,left);
    if(enteringAim)viewState.shoulder=nativeShoulder;
    const float wantedShoulder=std::copysign(std::abs(nativeShoulder),float(cover.fireSide));
    const float dt=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
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
static bool traceWall(V root,V normal,Wall& hit){
    float height;
    if(!ground(root,height))return false;
    root.z=height+.60f;
    return wallRay(root+normal*.12f,normal*-1.f,.95f,hit);
}
static bool matches(const Wall& found,const Wall& wall){
    return dot(found.normal,wall.normal)>.82f&&
        (wall.vehicle<0?found.vehicle<0:found.vehicle==wall.vehicle);
}
static bool sameWall(V root,const Wall& wall){
    Wall found;
    return traceWall(root,wall.normal,found)&&matches(found,wall);
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
static V cornerGoal(V root){
    const V along=tangent(cover.wall.normal)*float(cover.side);
    // Find the actual end of the obstacle. A fixed 42 cm step can leave the
    // muzzle behind the very corner which allowed aiming in the first place.
    float edge=.06f;
    while(edge<.66f&&sameWall(root+along*edge,cover.wall))edge+=.06f;
    return root+along*(edge+.26f);
}
static bool findWall(void* ped,V ahead,float reach,Wall& picked,V& destination){
    const V root=position(ped);
    float height;
    if(!ground(root,height))return false;
    const V waist{root.x,root.y,height+.60f};
    const V right=tangent(ahead);
    float best=reach+1.f;
    for(float side:{0.f,-.38f,.38f,-.8f,.8f}){
        const V direction=flat(ahead+right*side);
        Wall wall;
        if(!wallRay(waist,direction,reach,wall))continue;
        V stand=wall.point+wall.normal*.55f;
        stand.z=root.z;
        float groundHeight;
        if(!ground(stand,groundHeight)||std::abs(groundHeight-height)>.28f)continue;
        const float cost=length(stand-root)+std::abs(side)*.18f;
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
    return std::string("cover_")+(side>0?"l_":"r_")+(low?"low_":"high_")+
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
        (cover.side>0?"l_":"r_")+(held[0]=='r'?"rifle":"pistol")+(cover.shortDive?"_short":"");
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
    XINPUT_STATE input{};
    if(get&&get(0,&input)==ERROR_SUCCESS)return (input.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
    // DirectInput mappings still expose the raw right shoulder in CPad.
    return pad&&at<short>(pad,0xC)>0;
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
        V ahead=flat(matrix(ped)->up);
        Wall wall;V destination;
        const int gait=at<int>(ped,0x534);
        if(!findWall(ped,ahead,gait==6||gait==7?6.8f:1.8f,wall,destination))return;
        cover.ref=reference(ped);
        cover.wall=wall;
        cover.destination=destination;
        auto* camera=activeCamera();
        const V view=camera?flat(at<V>(camera,0x190)):flat(matrix(ped)->up);
        cover.side=dot(tangent(wall.normal),V{view.y,-view.x,0})>=0.f?1:-1;
        cover.entered=gameTime();
        cover.entry=startEntry(ped,cover.entered,combatWeapon(ped)>=22&&aimInput(pad)?.38f:.30f);
        cover.approaching=true;
        const float distance=length(destination-position(ped));
        if(distance>1.5f){
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
    const bool requested=weapon>=22&&aimInput(pad);
    if(!cover.approaching&&!cover.peeking)refreshCorner(position(ped),requested);
    const bool aim=requested&&!holdsFire(ped);
    const bool blind=!aim&&!cover.approaching&&!cover.turn.started&&(cover.wall.low||cover.corner)&&blindClip(weapon)&&
        reinterpret_cast<int(__thiscall*)(void*,void*)>(0x540180)(pad,ped)!=0;
    updateAim(aim,gameTime());
    if(blind&&!cover.blind){cover.blindStarted=gameTime();cover.blindReleased=0;cover.blindShotAt=0;}
    if(!blind&&cover.blind)cover.blindReleased=gameTime();
    cover.blind=blind;
    setDuck(ped,cover.duck,cover.wall.low&&!aim&&!cover.approaching);
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
        if(length(delta)<.12f&&entryAge(cover.entry)>=cover.entry.seconds){
            cover.approaching=false;
            cover.standOff=std::clamp(dot(current-cover.wall.point,cover.wall.normal),.55f,.65f);
        }
        else if(gameTime()-cover.entered>std::max(950u,unsigned(cover.diveSeconds*1000.f)+400)){
            leave("approach_blocked");return false;
        }
        if(cover.approaching&&cover.diveSeconds<=0.f){
            delta=entryGoal(cover.entry,cover.destination,dt)-current;delta.z=0;
        }
    }
    if(!cover.approaching){
        const V support=cover.peeking?cover.peekBase:current;
        Wall wall;
        if(!traceWall(support,cover.wall.normal,wall)||!matches(wall,cover.wall)){leave("cover_lost");return false;}
        auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
        const float x=std::clamp(reinterpret_cast<short(__thiscall*)(void*)>(0x53FC90)(pad)/128.f,-1.f,1.f);
        const float y=std::clamp(-reinterpret_cast<short(__thiscall*)(void*)>(0x53FD30)(pad)/128.f,-1.f,1.f);
        auto* camera=activeCamera();
        const V view=camera?flat(at<V>(camera,0x190)):flat(matrix(ped)->up);
        // CPad is refreshed inside CGame::Process. The earlier sample can be
        // one frame old here, otherwise it turns CJ sideways again just before
        // SA creates the aim camera behind his heading.
        const bool requested=combatWeapon(ped)>=22&&aimInput(pad);
        if(!cover.peeking)refreshCorner(current,requested);
        updateAim(requested&&!holdsFire(ped),gameTime());
        if(requested){
            face(ped,view);
            setDuck(ped,cover.duck,cover.wall.low&&!cover.aiming);
        }
        const V wish=V{view.y,-view.x,0}*x+view*y;
        if(dot(wish,cover.wall.normal)>.68f){leave("step_away");return false;}
        const float slide=dot(wish,along);
        if(cover.turn.started&&entryAge(cover.turn)>=cover.turn.seconds)cover.turn={};
        const bool canSlide=!cover.aiming&&!cover.blind&&!cover.peeking;
        const int next=slide>0.f?1:-1;
        const bool sameSide=next==cover.side;
        const bool hasRoom=std::abs(slide)>.18f&&sameWall(current+along*(.18f*next),cover.wall);
        if(canSlide&&!cover.turn.started&&!sameSide&&hasRoom&&std::abs(slide)>.24f&&std::abs(cover.slideSpeed)<.18f){
            const auto turn=animationKey(cover.wall.low,false,next,combatWeapon(ped),"_flip_180");
            const float duration=clipMeta(poseReader,ped,turn).duration;
            cover.turn=startEntry(ped,gameTime(),duration>0.f?std::clamp(duration,.35f,.85f):.45f);
            cover.side=next;
        }
        if(cover.peeking&&!cover.aiming){
            if(length(current-cover.peekBase)<.1f)cover.peeking=false;
            else cover.destination=cover.peekBase;
        }
        if(!cover.peeking){
            refreshCorner(current,requested);
            const bool pushing=cover.corner&&!hasRoom&&std::abs(cover.slideSpeed)<.16f&&
                slide*cover.side>.18f&&canSlide&&!cover.turn.started;
            if(pushing&&!cover.peekStarted)cover.peekStarted=gameTime();
            if(!pushing)cover.peekStarted=0;
            const float gap=dot(current-wall.point,cover.wall.normal);
            const float error=cover.standOff-gap;
            const float drift=std::copysign(std::min(.28f,std::max(0.f,std::abs(error)-.045f)*4.5f),error);
            cover.destination=current;
            wallVelocity=cover.wall.normal*drift;
            if(cover.aiming&&cover.corner){
                cover.peekBase=current;cover.destination=cornerGoal(current);
                cover.peeking=true;
                wallVelocity={};
            }
        }
        const float targetSpeed=canSlide&&!cover.turn.started&&hasRoom&&sameSide?slide*1.25f:0.f;
        cover.slideSpeed+=std::clamp(targetSpeed-cover.slideSpeed,-7.f*dt,7.f*dt);
        if(std::abs(cover.slideSpeed)>.025f&&canSlide&&!cover.turn.started&&
           sameWall(current+along*std::copysign(.12f,cover.slideSpeed),cover.wall)){
            wallVelocity=wallVelocity+along*cover.slideSpeed;
            const float authoredSpeed=clipMeta(poseReader,ped,walk).moverSpeed;
            cover.slideTime+=dt*std::abs(cover.slideSpeed)/std::max(.5f,authoredSpeed);
        }else if(!canSlide||cover.turn.started||std::abs(cover.slideSpeed)>.025f){
            cover.slideSpeed=0.f;
        }
        delta=cover.destination-current;delta.z=0;
    }
    const float distance=length(delta);
    float speed=cover.approaching?3.2f:cover.peeking?2.1f:1.45f;
    if(cover.approaching&&cover.diveSeconds>0.f){
        const float remaining=cover.diveSeconds-float(gameTime()-cover.entered)*.001f;
        speed=std::clamp(distance/std::max(.15f,remaining),2.f,8.f);
    }
    const V velocity=wallVelocity+(distance>.012f?delta*(std::min(speed,distance*(cover.approaching?1.f/dt:5.f))/distance):V{});
    at<float>(ped,0x550)=velocity.x*.02f;
    at<float>(ped,0x554)=velocity.y*.02f;
    auto* currentPad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
    if((!cover.aiming&&!aimInput(currentPad))||cover.turn.started){
        const V facing=along*float(cover.side);
        face(ped,facing,cover.approaching?&cover.entry:cover.turn.started?&cover.turn:nullptr);
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
        return drawClip(poseReader,ped,animationKey(cover.wall.low,corner,cover.side,weapon,suffix),elapsed(started),playback,1.f,0x600);
    };
    if(cover.approaching){
        const float age=entryAge(cover.entry);
        if(cover.diveSeconds>0&&drawClip(poseReader,ped,diveKey(ped),age,Clip::Once,smooth(age/.10f))){motion=Motion::Dive;return true;}
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
    std::vector<M> aimPose;
    if(owns(ped)&&cover.aiming)if(auto* h=hierarchy(ped);h&&h->matrices&&h->num>0&&h->num<=256)
        aimPose.assign(h->matrices,h->matrices+h->num);
    Motion motion=Motion::Rest;
    const bool authored=drawPlayerPose(ped,motion);
    if(!owns(ped)||!matrix(ped))return authored;
    auto* h=hierarchy(ped);
    if(!h||!h->matrices||h->num<=0||h->num>256)return authored;
    // Cover clips own the rise/lean, but SA's gun task owns the aimed arms.
    // Replacing that IK with the fixed IV intro was pointing the arm sideways
    // while the native bullet correctly followed the reticle.
    if(cover.aiming&&aimPose.size()==size_t(h->num)&&poseReader.rig){
        const int chest=poseReader.rig->index(saBone::chest);
        const int hand=poseReader.rig->index(24);
        if(chest>=0){
            const V shift=h->matrices[chest].pos-aimPose[chest].pos;
            for(int n=0;n<h->num;++n){
                int parent=n;
                while(parent>=0&&parent!=chest)parent=poseReader.rig->parents[parent];
                if(parent==chest){h->matrices[n]=aimPose[n];h->matrices[n].pos=h->matrices[n].pos+shift;}
            }
            // Retain the native elbow/wrist chain and grip. Rotate that whole
            // upper-body chain towards SA's actual crosshair ray, whose screen
            // offset differs from the centre of CCam::Front.
            if(hand>=0){
                const float gain=smooth(float(gameTime()-cover.exposed)/150.f);
                const int skill=reinterpret_cast<int(__thiscall*)(void*)>(0x5E6580)(ped);
                auto* info=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(combatWeapon(ped),skill);
                if(info)for(int pass=0;pass<2;++pass){
                    const V muzzle=point(h->matrices[hand],at<V>(info,0x24));
                    V from{},end{};
                    reinterpret_cast<void(__thiscall*)(void*,float,V,V&,V&)>(0x514970)
                        (reinterpret_cast<void*>(0xB6F028),std::max(20.f,at<float>(info,8)),muzzle,from,end);
                    const Q rotateTo=mix(Q{},turnBetween(h->matrices[hand].right,end-muzzle),gain);
                    const V origin=h->matrices[chest].pos;
                    for(int n=0;n<h->num;++n){
                        int parent=n;
                        while(parent>=0&&parent!=chest)parent=poseReader.rig->parents[parent];
                        if(parent==chest){
                            auto& bone=h->matrices[n];
                            bone.right=rotate(rotateTo,bone.right);bone.up=rotate(rotateTo,bone.up);
                            bone.at=rotate(rotateTo,bone.at);bone.pos=origin+rotate(rotateTo,bone.pos-origin);
                        }
                    }
                }
            }
        }
    }
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
    const bool blending=blendCoverPose(ped,poseReader,blend,motion,action,motion==Motion::Blind?.18f:.30f);
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
