#include "geometry.h"
#include <plugin.h>
#include <common.h>
#include <CPed.h>
#include <CVehicle.h>
#include <CPlayerPed.h>
#include <CWeaponInfo.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
using namespace pose;
struct ArmSample { V pole, reach; float extension; };
struct Sample { V aim, shoulder, neck, palm; ArmSample arm[2]; };
static_assert(sizeof(Sample)==26*sizeof(float));
using Bank = std::vector<Sample>;
struct SeatClips {
    Bank aim, entry, exit, throwing;
    unsigned aimHand=0, throwHand=0;
    float entryTime=0, exitTime=0, duration=0, release=0;
    V launch{};
};
std::array<std::array<SeatClips,4>,9> groups;
bool readBanks(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary);
    auto read=[&](auto& field){return bool(file.read(reinterpret_cast<char*>(&field),sizeof(field)));};
    char magic[4];
    if(!read(magic)||std::memcmp(magic,"DBV4",4))return false;
    decltype(groups) banks;
    for(size_t group=0;group<banks.size();++group)for(unsigned seat=0;seat<4;++seat){
        auto& c=banks[group][seat];
        unsigned counts[4]{};
        if(!read(counts[0]))return false;
        const bool exists=seat<2 || group==0 || group==2 || group==3;
        if(!exists){if(counts[0])return false;continue;}
        if(!read(counts[1])||!read(counts[2])||!read(counts[3])||!read(c.aimHand)||!read(c.throwHand)||
           !read(c.entryTime)||!read(c.exitTime)||!read(c.duration)||!read(c.release)||!read(c.launch))return false;
        if(c.aimHand>1||c.throwHand>1||!(c.release>0&&c.release<1&&std::abs(length(c.launch)-1)<.005f))return false;
        for(float t:{c.entryTime,c.exitTime,c.duration})if(!(t>0&&t<5))return false;
        Bank* clips[]={&c.aim,&c.entry,&c.exit,&c.throwing};
        for(int i=0;i<4;++i){
            if(counts[i]<2||counts[i]>512)return false;
            auto& bank=*clips[i];bank.resize(counts[i]);
            if(!file.read(reinterpret_cast<char*>(bank.data()),bank.size()*sizeof(Sample)))return false;
            for(const auto& s:bank){
                for(V v:{s.aim,s.shoulder,s.neck,s.palm,s.arm[0].pole,s.arm[1].pole,s.arm[0].reach,s.arm[1].reach})
                    if(!(std::abs(length(v)-1.f)<.005f))return false;
                for(const auto& a:s.arm)if(!(a.extension>0&&a.extension<1.001f))return false;
            }
        }
    }
    if(file.peek()!=EOF)return false;
    groups.swap(banks);
    return true;
}
int groupFor(unsigned type,int model){
    if(type==VEHICLE_BIKE||type==VEHICLE_QUAD){
        switch(model){
            case 468:case 471:return 5;
            case 463:case 586:return 7;
            case 462:case 448:return 8;
            case 521:case 522:case 461:return 4;
            default:return 6;
        }
    }
    if(type==VEHICLE_MTRUCK)return 3;
    if(type!=VEHICLE_AUTOMOBILE || model==432)return -1;
    switch(model){
        case 411:case 415:case 429:case 451:case 541:return 1;
        case 413:case 414:case 440:case 482:case 498:return 2;
        case 403:case 514:case 515:return 3;
        default:return 0;
    }
}
const SeatClips* seatedBank(int group,int seat){
    if(group<0||group>=int(groups.size())||seat<0||seat>3)return nullptr;
    // Two-door cars sometimes have a rear seat in a replacement model.
    // The standard rear-left/rear-right clips retain the correct side.
    const auto& selected=groups[group][seat];
    if(!selected.aim.empty())return &selected;
    if(group==1&&!groups[0][seat].aim.empty())return &groups[0][seat];
    return nullptr;
}
Turn align(V a,V b,float weight,float limit){
    a=unit(a);b=unit(b);
    if(length(a)<.999f||length(b)<.999f)return {{},0.f,1.f};
    if(dot(a,b)<-.99999f){
        const V axis=unit(cross(a,std::abs(a.z)<.8f?V{0,0,1}:V{0,1,0}));
        const float angle=std::min(limit,3.14159265f)*weight;
        return {axis,std::sin(angle),std::cos(angle)};
    }
    return toward(a,b,weight,limit);
}
template<class T> V vec(const T& p){return {p.x,p.y,p.z};}
void set(RwV3d& out,V p){out={p.x,p.y,p.z};}
void turnMatrix(RwMatrix& mat,const Turn& turn,V pivot){
    set(mat.right,turn.apply(vec(mat.right)));set(mat.up,turn.apply(vec(mat.up)));set(mat.at,turn.apply(vec(mat.at)));
    set(mat.pos,pivot+turn.apply(vec(mat.pos)-pivot));
}
V basisVector(const RwMatrix& m,V v){return vec(m.right)*v.x+vec(m.up)*v.y+vec(m.at)*v.z;}
bool relativeShoulderBind(const RwMatrix& upperInv,const RwMatrix& helperInv,RwMatrix& relative){
    const V x=cross(vec(helperInv.up),vec(helperInv.at)),y=cross(vec(helperInv.at),vec(helperInv.right)),z=cross(vec(helperInv.right),vec(helperInv.up));
    const float det=dot(vec(helperInv.right),x);
    if(!(std::abs(det)>1.e-6f))return false;
    auto helperToSkin=[&](V v){return V{dot(x,v)/det,dot(y,v)/det,dot(z,v)/det};};
    auto local=[&](V v){return basisVector(upperInv,helperToSkin(v));};
    set(relative.right,local({1,0,0}));set(relative.up,local({0,1,0}));set(relative.at,local({0,0,1}));
    set(relative.pos,vec(upperInv.pos)+local(vec(helperInv.pos)*-1.f));
    return length(vec(relative.pos))<.12f&&std::abs(dot(vec(relative.right),cross(vec(relative.up),vec(relative.at)))-1.f)<.025f;
}
// Interpolate a proper rotation and translation. Blending matrix components
// independently would shrink a shoulder during a large entry rotation.
void blendShoulder(RwMatrix& current,const RwMatrix& goal,float weight){
    if(weight<=0)return;
    if(weight>=1){current=goal;return;}
    const V a[]={unit(vec(current.right)),unit(vec(current.up)),unit(vec(current.at))};
    const V b[]={unit(vec(goal.right)),unit(vec(goal.up)),unit(vec(goal.at))};
    const V skew=cross(a[0],b[0])+cross(a[1],b[1])+cross(a[2],b[2]);
    const float sine=length(skew)*.5f,cosine=std::clamp((dot(a[0],b[0])+dot(a[1],b[1])+dot(a[2],b[2])-1.f)*.5f,-1.f,1.f);
    V axis=unit(skew);
    if(sine<1.e-5f&&cosine<0.f){
        // Near 180 degrees the antisymmetric part vanishes. The longest
        // matching-axis sum still points along the rotation axis.
        V along=a[0]+b[0];
        for(int i=1;i<3;++i)if(length(a[i]+b[i])>length(along))along=a[i]+b[i];
        axis=unit(along);
    }
    const V pivot=vec(current.pos);
    if(length(axis)>.99f){
        const float angle=std::atan2(sine,cosine)*weight;
        turnMatrix(current,Turn{axis,std::sin(angle),std::cos(angle)},pivot);
    }
    set(current.pos,pivot+(vec(goal.pos)-pivot)*weight);
}
struct Skin {
    RpHAnimHierarchy* h;int n;std::array<int,128> parent{};
    std::array<RwMatrix,128> original;
    struct Helper{int bone=-1,upper=-1;RwMatrix local{};};
    std::array<Helper,2> shoulders{};
    const RwMatrix* inverseBind=nullptr;
    bool init(){
        int stack[128],depth=0,p=-1;
        for(int i=0;i<n;++i){
            parent[i]=p;int flags=h->pNodeInfo[i].flags;
            if(flags&rpHANIMPUSHPARENTMATRIX)stack[depth++]=p;
            p=i;if(flags&rpHANIMPOPPARENTMATRIX){if(!depth&&i!=n-1)return false;p=depth?stack[--depth]:-1;}
        }
        std::copy_n(h->pMatrixArray,n,original.begin());
        attachShoulderHelpers();return true;
    }
    void attachShoulderHelpers(){
        for(int helper=0;helper<n;++helper){
            const int tag=h->pNodeInfo[helper].nodeID;
            if(tag!=301&&tag!=302)continue;
            for(int arm=0;arm<helper;++arm)if(h->pNodeInfo[arm].nodeID==(tag==301?22:32)){
                auto& fit=shoulders[tag-301];
                if(inverseBind&&relativeShoulderBind(inverseBind[arm],inverseBind[helper],fit.local)){
                    // Eligibility comes from the mesh bind, which remains stable
                    // even when a native lean moves the original helper far away.
                    fit.bone=helper;fit.upper=arm;parent[helper]=arm;
                }else if(length(vec(original[arm].pos)-vec(original[helper].pos))<.12f)parent[helper]=arm;
                break;
            }
        }
    }
    void restore(){std::copy_n(original.begin(),n,h->pMatrixArray);}
    bool under(int i,int ancestor){while(i>=0&&i!=ancestor)i=parent[i];return i==ancestor;}
    void branch(int root,Turn turn){V pivot=vec(h->pMatrixArray[root].pos);
        for(int i=root;i<n;++i)if(under(i,root))turnMatrix(h->pMatrixArray[i],turn,pivot);
    }
    V pos(int i){return vec(h->pMatrixArray[i].pos);}
    void fitShoulders(float weight){
        for(const auto& fit:shoulders)if(fit.bone>=0){
            const auto& upper=h->pMatrixArray[fit.upper];auto& helper=h->pMatrixArray[fit.bone];
            RwMatrix goal=helper;
            set(goal.right,basisVector(upper,vec(fit.local.right)));
            set(goal.up,basisVector(upper,vec(fit.local.up)));
            set(goal.at,basisVector(upper,vec(fit.local.at)));
            set(goal.pos,vec(upper.pos)+basisVector(upper,vec(fit.local.pos)));
            blendShoulder(helper,goal,weight);
        }
    }
};
struct PoseRequest { bool driver; float weight; V right,front,up,view; bool throwable; float phase; bool exiting=false; };
Sample mixPose(const Sample& a,const Sample& b,float t){
    auto dir=[&](V u,V v){V w=unit(u*(1-t)+v*t);return length(w)>.99f?w:(t<.5f?u:v);};
    Sample s{dir(a.aim,b.aim),dir(a.shoulder,b.shoulder),dir(a.neck,b.neck),dir(a.palm,b.palm)};
    for(int arm=0;arm<2;++arm){
        const auto& x=a.arm[arm];const auto& y=b.arm[arm];
        s.arm[arm]={dir(x.pole,y.pole),dir(x.reach,y.reach),x.extension+(y.extension-x.extension)*t};
    }
    return s;
}
Sample phasePose(const Bank& bank,float t){
    const float f=std::clamp(t,0.f,1.f)*float(bank.size()-1);
    const size_t i=std::min(size_t(f),bank.size()-2);
    return mixPose(bank[i],bank[i+1],f-float(i));
}
Sample aimedPose(const Bank& bank,V look){
    float best=1e9f, t=0;size_t frame=0;
    for(size_t i=1;i<bank.size();++i){
        V d=bank[i].aim-bank[i-1].aim;
        float u=std::clamp(dot(look-bank[i-1].aim,d)/std::max(.000001f,dot(d,d)),0.f,1.f);
        const V error=look-(bank[i-1].aim+d*u);
        if(dot(error,error)<best){best=dot(error,error);frame=i-1;t=u;}
    }
    return mixPose(bank[frame],bank[frame+1],t);
}
// Select the nearest swivel around the shoulder-to-wrist line that clears
// the chest. This preserves the hand target and both original bone lengths.
V clearForearm(V shoulder,V wrist,V joint,V chestLow,V chestHigh,float radius){
    const V spine=chestHigh-chestLow;
    const float sq=std::max(dot(spine,spine),1.e-6f);
    auto margin=[&](V el){
        float result=100.f;
        for(float f:{0.f,.25f,.50f,.75f,1.f}){
            const V delta=el+(wrist-el)*f-chestLow;
            const V relative=delta-spine*std::clamp(dot(delta,spine)/sq,0.f,1.f);
            result=std::min(result,dot(relative,relative));
        }
        return result;
    };
    float best=margin(joint);
    if(best>=radius*radius)return joint;
    V axis=unit(wrist-shoulder),out=joint;
    for(int step=1;step<=12;++step){
        const float rad=step*.26179939f;
        for(float side:{-1.f,1.f}){
            Turn t{axis,side*std::sin(rad),std::cos(rad)};
            const V alt=shoulder+t.apply(joint-shoulder);const float score=margin(alt);
            if(score>best){out=alt;best=score;}
        }
        if(best>=radius*radius)break;
    }
    return out;
}
bool poseCJ(Skin& skin,const SeatClips& clips,const PoseRequest& req,int& handIndex){
    auto id=[&](int tag){for(int i=0;i<skin.n;++i)if(skin.h->pNodeInfo[i].nodeID==tag)return i;return -1;};
    const int spine=id(3),head=id(5);
    const int upper[]={id(22),id(32)},lower[]={id(23),id(33)},hands[]={id(24),id(34)};
    for(int i:{spine,head,upper[0],upper[1],lower[0],lower[1],hands[0],hands[1]})if(i<0)return false;
    const bool throwing=req.throwable&&req.phase>=0.f;
    const int gun=throwing?clips.throwHand:clips.aimHand;
    handIndex=hands[gun];
    const auto world=[&](V v){return req.right*v.x+req.front*v.y+req.up*v.z;};
    const V localLook=unit({dot(req.view,req.right),dot(req.view,req.front),dot(req.view,req.up)});
    const float phase=std::clamp(req.weight,0.f,1.f);
    Sample s=throwing?phasePose(clips.throwing,req.phase):aimedPose(clips.aim,localLook);
    if(!throwing&&phase<1.f){
        const Sample transition=req.exiting?phasePose(clips.exit,1-phase):phasePose(clips.entry,phase);
        float t=std::clamp((phase-.55f)/.45f,0.f,1.f);
        s=mixPose(transition,s,t*t*(3-2*t));
    }
    const float w=std::clamp(phase/.45f,0.f,1.f);
    const float weight=w*w*(3.f-2.f*w);
    // Keep the arm within the authored sector when the camera points beyond
    // the window. The weapon attachment carries the residual aiming angle.
    const auto aimCorrection=align(throwing?world(clips.launch):world(s.aim),req.view,throwing?1.f:phase,.32f);
    // If leaning overshoots the fixed steering grip, reduce only the torso
    // contribution. The root, seat and leg animation remain native.
    for(float torso:{1.f,.5f,0.f}){
        skin.restore();
        skin.branch(spine,align(skin.pos(head)-skin.pos(spine),world(s.neck),weight*torso,.42f));
        skin.branch(spine,align(skin.pos(upper[0])-skin.pos(upper[1]),world(s.shoulder),weight*torso,.55f));
        bool valid=true;
        for(int arm=0;arm<2;++arm){
            const V start=skin.pos(upper[arm]),el=skin.pos(lower[arm]),hand=skin.pos(hands[arm]);
            const float ulen=length(el-start),llen=length(hand-el);
            const auto& a=s.arm[arm];
            const bool steer=req.driver&&arm!=gun;
            V pole=world(a.pole),reach=world(a.reach);
            if(arm==gun){reach=aimCorrection.apply(reach);pole=aimCorrection.apply(pole);}
            const float ext=std::clamp(a.extension,.05f,.997f);
            const V rightShoulder=skin.pos(upper[0]),leftShoulder=skin.pos(upper[1]);
            const V neck=(rightShoulder+leftShoulder)*.5f;
            const float width=length(rightShoulder-leftShoulder);
            if(arm==gun&&!throwing&&width>.01f){
                // With one hand fixed to the handlebar, some backwards IV
                // targets land across CJ's chest. Route the hand around the
                // front of his ribcage before solving the elbow.
                const V rightBody=unit(rightShoulder-leftShoulder);
                const V frontBody=unit(cross(neck-skin.pos(spine),rightBody));
                const float range=(ulen+llen)*ext;
                const float lateral=dot(start+reach*range-neck,rightBody)*(arm==0?1.f:-1.f);
                const float crossing=std::clamp((.25f*width-lateral)/(.50f*width),0.f,1.f);
                const float along=dot(reach,frontBody);
                const float wanted=std::clamp((width*.54f-dot(start-neck,frontBody))/range,-.9f,.9f);
                if(crossing>0&&along<wanted){
                    V side=unit(reach-frontBody*along);
                    if(length(side)<.99f)side=rightBody*(arm==0?1.f:-1.f);
                    const float a=along+(wanted-along)*crossing*weight;
                    reach=side*std::sqrt(std::max(0.f,1.f-a*a))+frontBody*a;
                }
            }
            const V rest=vec(skin.original[hands[arm]].pos);
            const V target=steer?rest:rest*(1.f-weight)+(start+reach*((ulen+llen)*ext))*weight;
            const V axis=unit(target-start);
            V nativePole=unit((el-start)-axis*dot(el-start,axis));
            if(length(nativePole)<.01f)nativePole=pole;
            if(steer){
                const V seatedElbow=vec(skin.original[lower[arm]].pos)-start;
                const V seatedPole=unit(seatedElbow-axis*dot(seatedElbow,axis));
                pole=length(seatedPole)>.99f?seatedPole:nativePole;
            }else pole=align(nativePole,pole,weight,3.14159265f).apply(nativePole);
            V goal{};
            if(!elbow(start,target,ulen,llen,pole,goal)){valid=false;break;}
            if(arm==gun&&!throwing){
                const float radius=width*.52f*weight;
                goal=clearForearm(start,target,goal,skin.pos(spine),neck,radius);
            }
            skin.branch(upper[arm],align(el-start,goal-start,1.f,3.14159265f));
            skin.branch(lower[arm],align(skin.pos(hands[arm])-skin.pos(lower[arm]),target-skin.pos(lower[arm]),1.f,3.14159265f));
            if(length(skin.pos(hands[arm])-target)>.001f){valid=false;break;}
            if(steer){
                for(int i=hands[arm];i<skin.n;++i)if(skin.under(i,hands[arm]))skin.h->pMatrixArray[i]=skin.original[i];
            }else if(arm==gun){
                // CJ has a single main finger chain. Match the donor's central
                // knuckle direction without rotating the elbow or shoulder.
                const int finger=id(arm==0?25:35);
                if(finger>=0){
                    const V from=skin.pos(finger)-skin.pos(hands[arm]);
                    const V palm=aimCorrection.apply(world(s.palm));
                    skin.branch(hands[arm],align(from,palm,weight,1.05f));
                }
            }
        }
        if(valid){skin.fitShoulders(weight);return true;}
    }
    skin.restore();
    return false;
}
RwMatrix weaponFrame(const RwMatrix& hand,V aim,V up,V front){
    // The native weapon model points down +X on either side of the car.
    // Keep the palm and finger palette in its anatomical orientation.
    RwMatrix grip{};grip.pos=hand.pos;
    V side=unit(cross(up,aim));
    if(length(side)<.99f)side=unit(cross(front,aim));
    set(grip.right,aim);set(grip.up,side);set(grip.at,cross(aim,side));
    return grip;
}
#ifndef DRIVER_POSE_TEST
struct LastPose { RwMatrix weaponFrame{}; CPed* ped=nullptr; CVehicle* car=nullptr; int seat=-1,weapon=-1; unsigned frame=0; V relative{}; };
LastPose last;
struct Transition { CPed* ped=nullptr; CVehicle* car=nullptr; unsigned frame=0; float weight=0; bool exiting=false; int seat=-1,weapon=-1; } transition;
bool loaded=false;
std::filesystem::path logPath;
bool logged[9][4]{};
unsigned currentFrame(){return *reinterpret_cast<const unsigned*>(0xB7CB4C);}
int seatFor(CPed* p,CVehicle* car){
    if(!p||!car||p!=FindPlayerPed()||!p->m_nPedFlags.bInVehicle||p->m_pVehicle!=car||!car->m_matrix||p->m_fHealth<=0.f||p->m_nActiveWeaponSlot>=13)return -1;
    if(car->m_pDriver==p)return 0;
    for(int i=0;i<3;i++)if(car->m_apPassengers[i]==p)return i+1;
    return -1;
}
bool firearm(int w){return w==22||w==23||w==24||w==28||w==29||w==32;}
bool throwable(int w){return w==16||w==18;}
RpAtomic* findShoulderSkin(RpAtomic* atomic,void* data){
    auto& target=*static_cast<Skin*>(data);
    if(!atomic->geometry||RpSkinAtomicGetHAnimHierarchy(atomic)!=target.h)return atomic;
    auto* binding=RpSkinGeometryGetSkin(atomic->geometry);
    if(binding&&RpSkinGetNumBones(binding)==unsigned(target.n))
        target.inverseBind=RpSkinGetSkinToBoneMatrices(binding);
    return target.inverseBind?nullptr:atomic;
}
int weaponFor(CPed* p){return p->m_aWeapons[p->m_nActiveWeaponSlot].m_eWeaponType;}
V worldPoint(const RwMatrix& m,V v){return vec(m.pos)+vec(m.right)*v.x+vec(m.up)*v.y+vec(m.at)*v.z;}
struct Plugin{Plugin(){
    const auto game=plugin::GetGameVersion();
    if(game!=GAME_10US_COMPACT&&game!=GAME_10US_HOODLUM)return;
    plugin::Events::initRwEvent.after+=[]{
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&readBanks),&module))return;
        wchar_t path[MAX_PATH];if(!GetModuleFileNameW(module,path,MAX_PATH))return;
        const auto dir=std::filesystem::path(path).parent_path();
        loaded=readBanks(dir/L"DriverPoses.bin")
            ||readBanks(dir.parent_path()/L"DeliriumSA-local-v3"/L"DriverPoses.bin");
        logPath=dir/L"IVDriverPose.log";
        std::ofstream log(logPath,std::ios::app);
        log<<(loaded?"Vehicle aim/throw poses ready, 24 seats each":"Vehicle pose bank unavailable")<<'\n';
    };
}} pluginInstance;
#endif
}
#ifndef DRIVER_POSE_TEST
extern "C" __declspec(dllexport) int __cdecl IVApplyVehicleAimPose(CPed* p,CVehicle* car,const float* direction,float weight,float throwPhase){
    last={};
    if(!loaded||!direction||!std::isfinite(weight)||weight<=.001f||!std::isfinite(throwPhase))return 0;
    const int seat=seatFor(p,car);
    if(seat<0||!p->m_pRwClump)return 0;
    const int weapon=weaponFor(p),group=groupFor(car->m_nVehicleSubClass,car->m_nModelIndex);
    if(!firearm(weapon)&&!throwable(weapon))return 0;
    const auto* clips=seatedBank(group,seat);
    if(!clips)return 0;
    auto* h=GetAnimHierarchyFromSkinClump(p->m_pRwClump);
    if(!h||!h->pMatrixArray||!h->pNodeInfo||h->numNodes<8||h->numNodes>128)return 0;
    const V aim=unit({direction[0],direction[1],direction[2]});
    if(!(length(aim)>.999f))return 0;
    auto& carFrame=*car->m_matrix;
    const V right=unit(vec(carFrame.right)),front=unit(vec(carFrame.up)),up=unit(vec(carFrame.at));
    Skin skin{h,h->numNodes};
    RpClumpForAllAtomics(p->m_pRwClump,&findShoulderSkin,&skin);
    int hand=-1;
    const unsigned frame=currentFrame();
    bool exiting=transition.exiting;
    if(p!=transition.ped||car!=transition.car||seat!=transition.seat||weapon!=transition.weapon||frame-transition.frame>2)exiting=false;
    else if(std::abs(weight-transition.weight)>.00001f)exiting=weight<transition.weight;
    transition={p,car,frame,weight,exiting,seat,weapon};
    if(!skin.init()||!poseCJ(skin,*clips,{seat==0,weight,right,front,up,aim,throwable(weapon),throwPhase,exiting},hand))return 0;
    const int skill=reinterpret_cast<int(__thiscall*)(void*)>(0x5E6580)(p);
    const auto* wi=reinterpret_cast<CWeaponInfo*(__cdecl*)(int,int)>(0x743C60)(weapon,skill);
    RwMatrix grip=weaponFrame(h->pMatrixArray[hand],aim,up,front);
    const V muzzle=wi&&!throwable(weapon)?worldPoint(grip,vec(wi->m_vecFireOffset)):vec(grip.pos);
    const V delta=muzzle-vec(carFrame.pos);
    last={grip,p,car,seat,weapon,frame,{dot(delta,right),dot(delta,front),dot(delta,up)}};
    if(!logged[group][seat]){
        logged[group][seat]=true;
        std::ofstream log(logPath,std::ios::app);log<<"Vehicle pose applied group "<<group<<" seat "<<seat<<'\n';
    }
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl IVVehicleShotMuzzle(CPed* p,CVehicle* car,int weapon,float* out){
    if(!out||!last.ped||last.ped!=p||last.car!=car||last.weapon!=weapon||currentFrame()-last.frame>2)return 0;
    if(seatFor(p,car)!=last.seat||weaponFor(p)!=weapon)return 0;
    const auto& m=*car->m_matrix;const auto v=last.relative;
    const V point=vec(m.pos)+unit(vec(m.right))*v.x+unit(vec(m.up))*v.y+unit(vec(m.at))*v.z;
    if(!std::isfinite(length(point)))return 0;
    out[0]=point.x;out[1]=point.y;out[2]=point.z;return 1;
}
extern "C" __declspec(dllexport) int __cdecl IVVehicleWeaponFrame(CPed* p,CVehicle* car,int weapon,RwMatrix* out){
    if(!out||!last.ped||last.ped!=p||last.car!=car||last.weapon!=weapon||currentFrame()!=last.frame)return 0;
    if(seatFor(p,car)!=last.seat||weaponFor(p)!=weapon)return 0;
    *out=last.weaponFrame;return 1;
}
extern "C" __declspec(dllexport) int __cdecl IVVehicleAimSetup(CPed* p,CVehicle* car,float* entry,float* exit,int* handTag){
    if(!loaded||!entry||!exit||!handTag)return 0;
    const int seat=seatFor(p,car);if(seat<0)return 0;
    const auto* clips=seatedBank(groupFor(car->m_nVehicleSubClass,car->m_nModelIndex),seat);
    if(!clips)return 0;
    *entry=clips->entryTime;*exit=clips->exitTime;*handTag=clips->aimHand?34:24;return 1;
}
extern "C" __declspec(dllexport) int __cdecl IVVehicleThrowTiming(CPed* p,CVehicle* car,float* seconds,float* release){
    if(!loaded||!seconds||!release)return 0;
    const int seat=seatFor(p,car);
    if(seat<0)return 0;
    const auto* clips=seatedBank(groupFor(car->m_nVehicleSubClass,car->m_nModelIndex),seat);
    if(!clips)return 0;
    *seconds=clips->duration;*release=clips->release;return 1;
}
#endif
