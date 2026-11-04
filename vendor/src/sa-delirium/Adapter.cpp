#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <intrin.h>
#include "MinHook.h"
#include "World.hpp"
#include "Rig.hpp"
#include "RecoveryMath.hpp"
#include "NativeMotion.hpp"
#include "MeleePolicy.hpp"
#include "OriginalMeleeForce.hpp"
#include "DamageOwnership.hpp"
#include "ObstacleTimeline.hpp"
#include "PhysicsFramePhase.hpp"
#include "NativeFrameSteps.hpp"
#include "HostPredictorTimeline.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <memory>
#include <string>

namespace fs=std::filesystem;
#include "SessionLog.hpp"
#include "PlayerPolicy.hpp"
static fs::path folder;
static NmBuild* sourceBuild=nullptr;
static std::unique_ptr<CollisionWorld> collision;
static NmGroup* bodyGroup=nullptr;static unsigned lastGroupReport=0;
static void tickPhysicalPeerProbe();
static void tickSolidBodyProbe();
static void tickStaticMeshProbe();
static void tickWheelSmokeProbe();
static void traceSolidBodyContact(int,int,const V&,const V&,const V&,int,float,float,float);
static void recordNativeGroundOwnerCommit(void*,V,V,V);
static std::map<int,std::unique_ptr<Rig>> rigs;
static std::string status="starting";
static bool ready=false,applyBones=false,detectImpacts=true,protectMissions=true,allowScripted=true;
static int maxActive=4;static float radius=40,activeSeconds=3,blendSeconds=.6f;
static unsigned lastTime=0,lastReport=0;static int lastArea=-1;
static PhysicsFramePhase physicsFrame;
static unsigned physicsFrameMisses=0,physicsFrameCompletions=0;
static std::map<int,void*> evaluatedPeds;
static unsigned evaluatedPedFrame=~0u;
static void finishPhysicsFrame();
static bool physicsFrameNeedsCompletion(){return ready&&physicsFrame.pending&&physicsFrame.frame==*reinterpret_cast<unsigned*>(0xB7CB4C);}
static unsigned activations=0,releases=0,steps=0,boneWrites=0,damageEvents=0,unsupported=0,errors=0;
static unsigned contactSteps=0;static float biggestBoneDelta=0,peakTickMs=0;static double totalTickMs=0;static unsigned ticks=0;
static unsigned recoveryStarts=0,recoveryFinishes=0;static float maxSegmentError=0;
static unsigned nativeGetupCompleted=0,uprightRecoveries=0,vehicleHits=0,bikeEjections=0,braceStarts=0,grabStarts=0,grabFeedback=0,mountedReactions=0;
static unsigned recoveryInterrupted=0,recoveryAligned=0;
static float recoveryStableSeconds=.35f,recoveryMaxSpeed=.65f,recoveryMaxSpin=2.f;
static bool hitRagdollMotorcycles=true,hitRagdollBicycles=true;
static bool playerVehicleImpacts=true,playerBikeCrashEjections=true;
static bool playerExplosions=true,physicalVehicleExits=true,aimWheelZoom=true;
static float exitThresholdKmh=15.f;
#include "NativePaletteEpoch.hpp"
enum class SeedPhase {Unknown,NativeContact,NativeTask,EvaluatedEnd,AuthoredEnd};
struct SeedEpoch {
    unsigned frame=0;uint64_t serial=0;SeedPhase phase=SeedPhase::Unknown;
    float nativeStepSeconds=0,frameStepSeconds=0;
};
struct PoseSeed {
    std::array<T,21> parts{};std::vector<T> bones;V velocity{};int vehicle=-1;
    V angular{},centre{};bool hasPartVelocities=false;std::array<V,21> linear{},spin{};bool deriveParts=false;
    SeedEpoch poseEpoch{},motionEpoch{};T sampledRoot{};
};
struct BikeHitFall {int vehicle;V velocity;unsigned time;int weapon;std::shared_ptr<PoseSeed> pose;};
static std::map<int,BikeHitFall> bikeHitFalls;

static void event(const std::string& type,int ref=-1,int model=-1,const std::string& detail=""){
    sessionlog::event(type,ref,model,detail);
    static sessionlog::LegacyEventLog legacy(folder/"events.jsonl",
        GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&
        GetEnvironmentVariableA("GTASA_EVENT_REOPEN_BASELINE",nullptr,0));
    legacy.write(GetCurrentProcessId(),gameTime(),type,ref,model,detail);
}
static bool ok(int value){if(value>=0)return true;char e[512]={};nm_last_error(e,512);status=e;for(auto&c:status)if(c=='"'||c=='\\'||c<' ')c=' ';errors++;event("engine_error",-1,-1,status);return false;}
#include "BikeRenderPhase.hpp"
static bool validPed(void*p,bool allowDead=false,bool allowMounted=false,bool allowPlayer=false){
    if(!p||reference(p)<0||((p==player()||at<void*>(p,0x480))&&!(allowPlayer&&p==player()))||!matrix(p)||!at<void*>(p,0x18)||(!allowDead&&at<float>(p,0x540)<=0))return false;
    if(at<void*>(p,0xFC)||at<unsigned char>(p,0x2F)!=area())return false;
    if((inVehicle(p)||at<int>(p,0x530)==50)&&!(allowMounted&&ridingBike(p)))return false;
    if(!allowScripted&&p!=player()&&at<unsigned char>(p,0x484)!=1)return false;
    return player()&&length(position(p)-position(player()))<=radius;
}
enum class Reaction {Shot,Melee,Impact,Brace,BikeFall,Mounted,Explosion,VehicleExit,Bump,DoorEntry};
struct Request {V impulse{},direction{0,1,0};unsigned time=0;int part=10;bool test=false;Reaction kind=Reaction::Shot;int vehicle=-1;std::shared_ptr<PoseSeed> pose;bool playerVehicle=false;bool velocityOverride=false;V velocity{},deltaVelocity{};int weapon=-1;int attackerRef=-1;MeleeResponse melee=MeleeResponse::Flinch;bool contactKnown=false;V contact{};V strikeVelocity{},contactNormal{};float strikingMass=0,reactionDraw=0;bool meleeContactReady=false;bool originalMeleeBlow=false;bool physicalAnimation=false;int nativeMeleePart=10;RoadDamageReceipt roadDamage{};CombatDamageReceipt combatDamage{};float bumpImpulse=0,bumpDt=0;int entryComponent=0;T entryFrame{};};
struct MeleePush {int part=0;V partLocal{},force{};float left=0;unsigned hitTime=0;V delivered{};float duration=.15f;};
static bool permitsPlayer(const Request&r){return r.playerVehicle&&(r.kind==Reaction::Bump||(r.kind==Reaction::Melee&&r.meleeContactReady)||(r.kind==Reaction::Impact&&playerVehicleImpacts)||(r.kind==Reaction::BikeFall&&playerBikeCrashEjections)||(r.kind==Reaction::Explosion&&playerExplosions)||(r.kind==Reaction::VehicleExit&&physicalVehicleExits));}
static std::map<int,Request> pending;
struct Active {
    int ref=-1,model=-1;void* ped=nullptr;Rig* rig=nullptr;NmAgent* agent=nullptr;
    bool playerVehicle=false;std::unique_ptr<Rig> playerRig;
    unsigned start=0;float accumulator=0;bool usedCollision=false;V rootOffset{};
    unsigned admittedFrame=0;bool seedBeforeCurrentFrame=false;
    std::map<int,ObstacleHistory> vehicleHistory;
    bool recovering=false;unsigned recoverStart=0;std::vector<T> frozenBones;
    bool recoveryAnimatedOwner=false,recoveryPlacementReady=false;
    uint64_t recoveryClipEpoch=0,recoverySampleSerial=0;unsigned recoveryNextProbe=0;
    bool recoveryCarBlocked=false,recoveryClearanceLost=false;
    void* ownedTask=nullptr;void* ownedAnimation=nullptr;
    bool taskAborted=false,nativeAnimationFinished=false,recoveryDone=false,uprightRecovery=false;
    RoadDamageReceipt roadDamage{};bool resumeDeliveredRoadDamage=false;
    CombatDamageReceipt combatDamage{};bool resumeDeliveredCombatDamage=false;
    ShotDamageReceipt groundShot{};bool groundShotDelivered=false;
    int recoveryStage=0,sampleFrames=0,recoveryAnim=112;float stableFor=0;
    RecoveryFit recoveryFits[2];unsigned animationStarted=0;float animationDuration=0;
    std::vector<T> lastRendered;float maxRecoveryStep=0;unsigned lastRenderTime=0;
    Reaction kind=Reaction::Shot;int vehicle=-1;bool brace=false,grabbing=false,grabConfirmed=false;unsigned gripStart=0;V handleLocal{};
    bool mounted=false,preview=false,roadFalling=false;NmWorld* mountedWorld=nullptr;T lastSeat{};
    int roadReaction=0; // selected from a solved contact, never vehicle speed alone
    bool roadRelaxLogged=false;
    int exitPhase=-1;bool exitReady=false,bumpReady=false,meleeTaskOriginal=false;
    int entryPhase=-1,entryHolding=-1,entryComponent=0,entryIndex=-1;bool entryReady=false;
    unsigned entryFatalAt=0;bool entryDeathDelivered=false;
    float initialComZ=0,peakComRise=0,peakUpSpeed=0;bool seededBikeFall=false,poseSeeded=false;
    V acceptedBikeVelocity{};
    int departingBike=-1;
    bool ownedFallenFlag=false;
    float maxHeadTorsoDegrees=0,maxNeckTorsoDegrees=0;unsigned anatomyFrames=0;
    float maxHelperLocalError=0;
    unsigned peerCapsules=0;bool peerContactsLogged=false;
    unsigned meleeReceived=0;bool meleeRendered=false;
    unsigned meleeMotionSample=0;V meleeHeadLocal{},meleePelvis{};Q meleeHeadRotation{};
    std::vector<MeleePush> meleePushes;
    int meleeAttacker=-1,meleeWeapon=-1;bool meleeHandContactLogged=false;
    unsigned probeRecoveryNotBefore=0; // Set only by the explicit ground-hit fixture.
    unsigned lastVehicleContactReport=0;
    MeleeResponse melee=MeleeResponse::Flinch;
    bool physicalAnimation=false;int animationDriveStatus=-1;unsigned entryDamageReport=0;
    float nativeGroundClearance=0;V recoveryVelocity{};
    std::array<T,21> renderedParts{};std::array<V,21> renderVelocity{},renderSpin{};
    std::array<T,21> incoming{},previous{},current{};std::vector<T> bones;
    ~Active(){if(agent)nm_agent_destroy(agent);if(mountedWorld)nm_world_destroy(mountedWorld);}
};
struct HostExternalOwner {
    uint64_t id=0;int ref=-1;void*entity=nullptr;void*clump=nullptr;int model=-1;bool animated=false;
    std::array<float,27> rows{};
    HostPredictorTimeline*predictor=nullptr;
    HostPredictorSnapshot acceptedBase{},staged{};
    float fraction=1.f;
};
static std::map<int,std::unique_ptr<Active>> agents;
static void observeSolvedAnimatedContact(Active&,int,V,V,float);
static void observeSolvedExternalPairs(NmGroup*,float);
static void recordFinalPedPose(void*);
static void clearFinalPedPoses();
static bool combatOwns(void*p);
static bool combatFightTask(void*task,void*ped);
static bool combatRender(void*p);
static bool combatQueryPose(void*p,std::vector<T>&pose);
static void combatPrepare();
static void combatTick(float dt);
static void combatFinish();
static void combatClear();
static bool combatDelivering=false;
static void startPlayerShove(void*other);
static void renderPlayerShove(void*p);
static void tickPlayerShove(float dt);
static void nikoUpdatePosition(void*p);
static void nikoRender(void*p);
static void nikoClear();
static void captureRoadPoseSeeds();
static void clearPhysicalDeaths();
static void forgetPhysicalDeath(int);
static bool replacePhysicalDeathAnimation(DamageEvent*,void*);
#include "NativeIdlePose.hpp"
#include "VehicleReactions.hpp"
static void releaseRecoveryTask(Active& a);

static void restoreNative(Active& a,bool settle,bool enableCollision=true){
    if(auto*p=pedFromRef(a.ref);p&&p==a.ped&&at<short>(p,0x22)==a.model){
        if(a.ownedFallenFlag){at<unsigned char>(p,0x476)&=~16u;a.ownedFallenFlag=false;}
        if(applyBones&&!a.mounted&&!a.preview){
            // Interrupted ownership (phone/mission, superseded task) can
            // return a standing native animation at the low ragdoll root.
            // Correct only ground penetration at this handoff, never clamp
            // the simulated body or pull an airborne actor down to the floor.
            if(enableCollision&&!settle&&!a.recoveryDone&&!a.uprightRecovery&&
                !inVehicle(p)&&area()==at<unsigned char>(p,0x2F)&&at<float>(p,0x540)>0&&
                length(position(p)-a.current[0].p)<2.f){
                const V before=position(p);V from=before+V{0,0,2.f};ColPoint ground{};void*support=nullptr;
                if(reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
                    (from,before.z-2.f,ground,support,true,true,false,true,true,false,nullptr)&&finite(ground.point)&&ground.normal.z>.45f){
                    const float standing=ground.point.z+(a.nativeGroundClearance>0?a.nativeGroundClearance:a.rig->rootClearance);
                    if(before.z<standing-.03f){
                        V fixed=before;fixed.z=standing;movePhysicalRoot(p,fixed,a.playerVehicle);
                        event("native_boundary_ground_clearance",a.ref,a.model,"before_z="+std::to_string(before.z)+" ground_z="+std::to_string(ground.point.z)+" after_z="+std::to_string(fixed.z));
                    }
                }
            }
            if(enableCollision){auto&flags=at<unsigned char>(p,0x1C);flags=(flags&~1u)|(a.usedCollision?1u:0u);}
            if(settle&&area()==at<unsigned char>(p,0x2F)&&at<float>(p,0x540)>0&&collision&&collision->world){
                // The SDK's nm_world_raycast queries only the static mesh. Use
                // the game's verified vertical collision query for recovery so
                // a body resting on a vehicle is not placed through its roof.
                V from=position(p);from.z=a.current[0].p.z+.5f;ColPoint hit{};void* support=nullptr;
                const bool supported=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
                    (from,from.z-10,hit,support,true,true,false,true,true,false,nullptr);
                if(supported&&finite(hit.point)&&hit.normal.z>.45f){
                    V root=position(p);root.z=hit.point.z+(a.nativeGroundClearance>0?a.nativeGroundClearance:a.rig->rootClearance);movePhysicalRoot(p,root,a.playerVehicle);
                    event("recovery_support",a.ref,a.model,"z="+std::to_string(hit.point.z)+" support_model="+std::to_string(support?at<short>(support,0x22):-1));
                }
                at<V>(p,0x44)=V{};
            }
            if(enableCollision&&a.recovering&&a.uprightRecovery){
                // A supported standing handoff keeps its residual horizontal
                // motion; it never adds native upward velocity to the blend.
                at<V>(p,0x44)=V{a.recoveryVelocity.x,a.recoveryVelocity.y,0}*.02f;
                event("upright_collision_handoff",a.ref,a.model,"root_z="+std::to_string(position(p).z)+" vertical_velocity=0");
            }
        }
    }
}
static void restore(Active& a,const char* why,bool settle){
    forgetPhysicalDeath(a.ref);
    releaseRecoveryTask(a);restoreNative(a,settle);releases++;event("release",a.ref,a.model,why);
    event("anatomy_motion",a.ref,a.model,"head_torso_degrees="+std::to_string(a.maxHeadTorsoDegrees)+" neck_torso_degrees="+std::to_string(a.maxNeckTorsoDegrees)+" rendered_frames="+std::to_string(a.anatomyFrames)+" helper_local_error="+std::to_string(a.maxHelperLocalError));
    if(a.seededBikeFall)event("bike_fall_motion",a.ref,a.model,"peak_com_rise="+std::to_string(a.peakComRise)+" peak_up_speed="+std::to_string(a.peakUpSpeed));
}
#include "Recovery.hpp"
#include "RoadTelemetry.hpp"
#include "VehicleContacts.hpp"
#include "MeleeProfiles.hpp"
#include "SessionTrace.hpp"
#include "PedContacts.hpp"
#include "MeleeContactProbe.hpp"
#include "MeleeAim.hpp"
#include "BodyGeometry.hpp"
static bool originalGroundActive(void*);
#include "FootSupport.hpp"
#include "BodyContacts.hpp"
#include "RecoveryContacts.hpp"
static V stagedExternalDrive(uint64_t);
static bool originalGroundSupported(void*);
static V originalGroundDrive(uint64_t);
static bool stageOriginalGround(NmGroup*,uint64_t);
static bool preflightOriginalGround(NmGroup*);
static bool preflightOriginalGroundMotion(const HostExternalOwner&,V,V);
static void commitOriginalGround(const HostExternalOwner&);
#include "OriginalAnimatedOwner.hpp"
static bool originalPedMotorImpulse(const HostExternalOwner&,float,V&);
#include "ExternalMobility.hpp"
#include "NativeVehicleEnvironment.hpp"
#include "OriginalPedMotorHost.hpp"
#include "OriginalPedMotorInstall.hpp"
#include "OriginalGroundHost.hpp"
#include "SolverGapAudit.hpp"
#include "SkinGroundAudit.hpp"
#include "NativeGroundAuditHost.hpp"
#include "ExpandedSkinCaptureHost.hpp"
#include "BikeOverlapAudit.hpp"
static void clearExpandedSkinAdapterTest();
static void clear(const char* why){clearExpandedSkinAdapterTest();clearOriginalGroundHost();clearPhysicalDeaths();clearOriginalPedMotors();clearExternalScene();clearFinalPedPoses();if(bodyGroup)nm_group_reset(bodyGroup);clearRecoveryContacts();for(auto&[ref,a]:agents)restore(*a,why,false);agents.clear();pending.clear();bikeHitFalls.clear();animatedContacts.clear();clearAnimatedBodyColliders();walkingSupports.clear();wheelBodyLoads.clear();walkingContactLog.clear();wheelContactLog.clear();footFrameDue=0;}
static bool readPose(Active&a,std::array<T,21>& pose){
    V center{},velocity{};nm_agent_get_com(a.agent,&center.x,&velocity.x);
    if(!finite(center)||!finite(velocity)||length(velocity)>80){errors++;event("invalid_physics_motion",a.ref,a.model);return false;}
    for(int i=0;i<21;i++){
        if(!ok(nm_agent_get_part_transform(a.agent,i,&pose[i].p.x,&pose[i].q.x)))return false;
        const auto q=pose[i].q;
        const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        if(!finite(pose[i].p)||length(pose[i].p-center)>4||!std::isfinite(norm)||norm<.9f||norm>1.1f){
            errors++;event("invalid_physics_pose",a.ref,a.model,"part="+std::to_string(i));return false;
        }
    }return true;
}
static bool activate(void*p,const Request&r){
    if(!validPed(p,!r.test,r.kind==Reaction::Mounted,permitsPlayer(r)))return false;
    FrameCostProfile cost("activate",int(agents.size()),int(pending.size()),reference(p),at<short>(p,0x22),.5);
    // Optional reproduction artifacts are not part of admission or simulation.
    // Opening/truncating three text files per victim stalled native hit frames.
    static const bool snapshotIncoming=GetEnvironmentVariableA("GTASA_INCOMING_POSE_AUDIT",nullptr,0)!=0;
    // MaxActive is the speculative brace/preview budget. A real accepted
    // impact must not compete with four nearby previews or living bodies.
    // Query the transport's real capacity, reserving CJ's physical slot.
    const int capacity=nm_group_capacity();if(capacity<=1){event("capacity_invalid");return false;}
    const bool speculative=r.kind==Reaction::Brace||r.kind==Reaction::Mounted;
    int npcActive=0;bool playerActive=false;for(const auto&[ref,a]:agents){if(a->ped==player())playerActive=true;else npcActive++;}
    const int limit=p==player()?capacity:capacity-(playerActive?0:1);
    if((speculative&&npcActive>=maxActive)||int(agents.size())>=limit){
        auto preview=agents.end();
        if(!speculative)for(auto it=agents.begin();it!=agents.end();++it)if(it->second->preview||it->second->mounted){preview=it;break;}
        if(preview==agents.end()){
            event("capacity_rejected",reference(p),at<short>(p,0x22),"active="+std::to_string(agents.size())+" limit="+std::to_string(speculative?maxActive:limit)+" speculative="+std::to_string(speculative));return false;
        }
        restore(*preview->second,"lower_priority_capacity_reuse",false);agents.erase(preview);
    }
    int ref=reference(p),model=at<short>(p,0x22);
    try {
        // The barrier already evaluated the authored/native palette. A forced
        // GetBonePosition here can rebuild HAnim and erase its adapted pose.
        auto it=rigs.find(model);if(p!=player()&&it==rigs.end()){
            auto*h=hierarchy(p);std::string nodes;
            if(h&&h->nodes&&h->num>0&&h->num<=128)for(int i=0;i<h->num;i++)nodes+=std::to_string(h->nodes[i].id)+",";
            event("skeleton_nodes",ref,model,nodes);
            it=rigs.emplace(model,std::make_unique<Rig>(p,sourceBuild)).first;
            {const auto&g=*it->second;std::string d;
             for(int k:{12,15,16,19,20}){float box[3]{};const bool boxed=nm_build_part_box(g.build,k,box)==1;
                d+=" part"+std::to_string(k)+"="+std::to_string(g.skinShape[k])+":"+std::to_string(g.skinVertices[k])+":"+
                   (boxed?std::to_string(box[0])+","+std::to_string(box[1])+","+std::to_string(box[2]):std::to_string(g.partRadius[k])+","+std::to_string(g.partHalf[k]));}
             event("skin_shapes",ref,model,d.substr(1));}
        }
        auto a=std::make_unique<Active>();a->ref=ref;a->model=model;a->ped=p;a->playerVehicle=p==player()&&permitsPlayer(r);
        // CJ's clothes/body can change without changing model ID 0.
        if(a->playerVehicle){a->playerRig=std::make_unique<Rig>(p,sourceBuild);a->rig=a->playerRig.get();}else a->rig=it->second.get();
        cost.lap(0);
        a->start=gameTime();a->admittedFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);
        if(r.pose){
            const auto&poseEpoch=r.pose->poseEpoch;const auto&motionEpoch=r.pose->motionEpoch;
            a->seedBeforeCurrentFrame=poseEpoch.phase!=SeedPhase::Unknown&&motionEpoch.phase!=SeedPhase::Unknown&&
                poseEpoch.frame==motionEpoch.frame&&poseEpoch.serial!=0&&poseEpoch.serial==motionEpoch.serial&&
                a->admittedFrame-poseEpoch.frame==1;
            event("physical_seed_epoch",ref,model,"pose_frame="+std::to_string(poseEpoch.frame)+
                " motion_frame="+std::to_string(motionEpoch.frame)+" admitted_frame="+std::to_string(a->admittedFrame)+
                " pose_phase="+std::to_string(int(poseEpoch.phase))+" motion_phase="+std::to_string(int(motionEpoch.phase))+
                " pose_serial="+std::to_string(poseEpoch.serial)+" motion_serial="+std::to_string(motionEpoch.serial)+
                " current_frame_eligible="+std::to_string(a->seedBeforeCurrentFrame)+" intraframe_remainder_verified=0");
        }
        a->departingBike=r.kind==Reaction::BikeFall?r.vehicle:-1;a->seededBikeFall=r.kind==Reaction::BikeFall;
        if(r.pose){
            // The saved pose was evaluated before detaching/interruption.
            // Do not replace or reject it because SA changed its animation since.
            if(r.pose->bones.size()!=a->rig->ids.size())return false;
            a->incoming=r.pose->parts;a->bones=r.pose->bones;
            if(r.pose->deriveParts){auto joints=a->rig->jointFrames(a->bones);for(int n=0;n<21;n++)a->incoming[n]=mul(joints[n],a->rig->offsets[n]);}
            a->poseSeeded=true;a->seededBikeFall=r.kind==Reaction::BikeFall;a->departingBike=(r.kind==Reaction::VehicleExit||r.kind==Reaction::DoorEntry)?-1:r.pose->vehicle;
            if(r.kind==Reaction::VehicleExit)if(auto*v=vehicleFromRef(r.pose->vehicle)){int type=at<int>(v,0x594);if(type==2||type==9||type==10)a->departingBike=r.pose->vehicle;}
        }else{
            if(!evaluatedPeds.contains(ref)||evaluatedPedFrame!=a->admittedFrame||
               !a->rig->incoming(p,a->incoming,a->bones)){unsupported++;event("invalid_incoming_pose",ref,model);return false;}
        }
        a->mounted=r.kind==Reaction::Mounted;a->lastSeat=a->incoming[0];
        if(a->mounted){a->mountedWorld=nm_world_create();if(!a->mountedWorld)throw std::runtime_error("Mounted world allocation failed");}
        if(snapshotIncoming){std::ofstream f(folder/("model-"+std::to_string(model)+"-bones.txt"));f.precision(9);for(size_t i=0;i<a->bones.size();i++){const auto&t=a->bones[i];f<<a->rig->ids[i]<<' '<<t.p.x<<' '<<t.p.y<<' '<<t.p.z<<' '<<t.q.x<<' '<<t.q.y<<' '<<t.q.z<<' '<<t.q.w<<'\n';}}
        if(snapshotIncoming){std::ofstream f(folder/("model-"+std::to_string(model)+"-fit.txt"));f.precision(9);for(int i=0;i<21;i++)f<<a->rig->fitPoints[i*3]<<' '<<a->rig->fitPoints[i*3+1]<<' '<<a->rig->fitPoints[i*3+2]<<'\n';}
        cost.lap(1);
        collision->update(position(player()));
        V floorFrom=position(p)+V{0,0,2},down{0,0,-1},normal{};float distance=0;
        if(collision->triangles<=0||nm_world_raycast(collision->world,&floorFrom.x,&down.x,12,&distance,&normal.x)<=0){
            ColPoint native{};void*support=nullptr;
            const bool found=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
                (floorFrom,floorFrom.z-12,native,support,true,false,false,true,true,false,nullptr);
            // Retry using the actual static support, which a bounded world
            // enumeration can omit. Never substitute a synthetic floor plane.
            if(found&&support){collision->rebuild(position(player()),support);collision->applyVehicleBoxes();}
            const bool recovered=found&&support&&nm_world_raycast(collision->world,&floorFrom.x,&down.x,12,&distance,&normal.x)>0;
            event(recovered?"collision_floor_recovered":"no_real_collision_floor",ref,model,"native_found="+std::to_string(found)+" support_model="+std::to_string(support?at<short>(support,0x22):-1)+" simple_transforms="+std::to_string(collision->simpleTransforms)+" x="+std::to_string(floorFrom.x)+" y="+std::to_string(floorFrom.y)+" z="+std::to_string(floorFrom.z));
            if(!recovered)return false;
        }
        event("collision_floor",ref,model,"z="+std::to_string(floorFrom.z-distance)+" normal_z="+std::to_string(normal.z)+" foot_z="+std::to_string(std::min(a->incoming[3].p.z,a->incoming[6].p.z)));
        if(snapshotIncoming){std::ofstream f(folder/("model-"+std::to_string(model)+"-incoming.txt"));f.precision(9);f<<floorFrom.z-distance<<'\n';for(auto&t:a->incoming)f<<t.p.x<<' '<<t.p.y<<' '<<t.p.z<<' '<<t.q.x<<' '<<t.q.y<<' '<<t.q.z<<' '<<t.q.w<<'\n';}
        if(snapshotIncoming)event("incoming_pose_snapshot",ref,model,"opt_in=GTASA_INCOMING_POSE_AUDIT files=bones,fit,incoming");
        V feet=position(p);feet.z=floorFrom.z-distance;
        const float clearance=position(p).z-feet.z;
        if(clearance>.65f&&clearance<1.5f&&(a->incoming[10].p-a->incoming[0].p).z>.3f)a->nativeGroundClearance=clearance;
        // Distinct native RNG samples, not the same behaviour seed for every body.
        const float behaviourSeed=reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(0.f,1.f);
        cost.lap(2);
        a->agent=nm_agent_create(a->rig->build,&feet.x,0,behaviourSeed);if(!a->agent){ok(-1);return false;}
        ok(nm_agent_set_mode(a->agent,NM_MODE_FOLLOW));
        V zero{},velocity=(r.pose?r.pose->velocity:(r.velocityOverride?r.velocity:at<V>(p,0x44)))*50.f;
        if(r.kind==Reaction::Impact)event("road_impact_motion_seed",ref,model,"pre_native_velocity="+std::to_string(r.velocityOverride)+" vx="+std::to_string(velocity.x)+" vy="+std::to_string(velocity.y)+" vz="+std::to_string(velocity.z)+" extra_impulse="+std::to_string(length(r.impulse))+" seeded_pose="+std::to_string(bool(r.pose)));
        if(a->playerVehicle)event("player_native_motion_seed",ref,model,"vx="+std::to_string(velocity.x)+" vy="+std::to_string(velocity.y)+" vz="+std::to_string(velocity.z)+" extra_impulse="+std::to_string(length(r.impulse)));
        for(int i=0;i<21;i++){
            const V angular=r.pose&&r.pose->hasPartVelocities?r.pose->spin[i]:(r.pose?r.pose->angular*50.f:zero);
            const V linear=r.pose&&r.pose->hasPartVelocities?r.pose->linear[i]:(r.pose?inheritedVelocity(velocity,angular,a->incoming[i].p,r.pose->centre):velocity);
            if(!ok(nm_agent_set_part_state(a->agent,i,&a->incoming[i].p.x,&a->incoming[i].q.x,&linear.x,&angular.x)))return false;
        }
        ok(nm_agent_set_mode(a->agent,NM_MODE_LEAD));
        cost.lap(3);
        if(r.kind==Reaction::BikeFall||r.kind==Reaction::VehicleExit)auditBikeInitialContacts(*a);
        if(r.kind==Reaction::Melee||r.kind==Reaction::VehicleExit||r.kind==Reaction::DoorEntry||r.kind==Reaction::Brace||r.kind==Reaction::Bump)applyNativeIdlePose(*a);
        // A rollout translates the skeleton sideways relative to its entity.
        // Keeping that animation offset would put the invisible native root
        // back inside the car while the rendered body is already outside.
        a->rootOffset=r.pose?a->rig->rootToPelvis:rotate(conjugate(rotation(*matrix(p))),a->incoming[0].p-position(p));
        if(r.pose)event("physical_entity_offset",ref,model,"x="+std::to_string(a->rootOffset.x)+" y="+std::to_string(a->rootOffset.y)+" z="+std::to_string(a->rootOffset.z));
        a->usedCollision=(at<unsigned char>(p,0x1C)&1)!=0;
        if(!readPose(*a,a->current))return false;a->previous=a->current;
        {V com{},vel{};nm_agent_get_com(a->agent,&com.x,&vel.x);a->initialComZ=com.z;
         if(a->seededBikeFall)a->acceptedBikeVelocity=vel;
         if(r.kind==Reaction::Melee)event("melee_initial_motion",ref,model,"com_z="+std::to_string(com.z)+" com_vz="+std::to_string(vel.z)+" per_part_motion="+std::to_string(r.pose&&r.pose->hasPartVelocities));}
        startReaction(*a,r);updateVehicleReaction(*a);
        for(const auto&v:collision->vehicles){
            auto&history=a->vehicleHistory[v.identity()];history={v.frame,v.shape?v.shape->id:-1};
            if(a->seedBeforeCurrentFrame&&v.velocityAtFrame){
                // Fork/lean/wheel articulation is not parent motion. Only a
                // consecutive sample of this exact geometry supplies history.
                previousBikeComponentFrame(v.entity,v.component,history.frame);
            }else if(a->seedBeforeCurrentFrame&&(r.kind==Reaction::Impact||r.kind==Reaction::DoorEntry)&&r.pose)
                if(auto old=beforeVehicleFrame.find(v.ref);old!=beforeVehicleFrame.end())
                    history.frame=mul(old->second,mul(inverse(transform(*matrix(v.entity))),v.frame));
            if(r.kind==Reaction::DoorEntry&&v.ref==r.vehicle&&v.component==r.entryComponent)history.frame=r.entryFrame;
        }
        if(a->mounted){mountedReactions++;event("mounted_bike_reaction",ref,model,"native_seat_legs_and_hands_anchored");}
        if(applyBones&&!a->mounted&&!a->preview)at<unsigned char>(p,0x1C)&=~1u;
        event(r.test?"test_activation":"damage_activation",ref,model,"real_asset_fitted_to_skin_bind_pose");
        if(a->playerVehicle)event("player_vehicle_physics_started",ref,model,r.kind==Reaction::BikeFall?"native_bike_exit_handoff":r.kind==Reaction::Explosion?"explosion":r.kind==Reaction::VehicleExit?"moving_vehicle_exit":"confirmed_vehicle_impact");
        cost.lap(4);
        agents.emplace(ref,std::move(a));activations++;return true;
    }catch(const std::exception&e){unsupported++;status=e.what();event("unsupported_rig",ref,model,status);return false;}
}

static void report(){
    std::ostringstream s;s<<"{\"pid\":"<<GetCurrentProcessId()<<",\"ready\":"<<(ready?"true":"false")<<",\"apply_bones\":"<<(applyBones?"true":"false")
        <<",\"status\":\""<<status<<"\",\"sdk_api_version\":"<<nm_version()<<",\"area\":"<<area()<<",\"active\":"<<agents.size()<<",\"rigs\":"<<rigs.size()
        <<",\"activations\":"<<activations<<",\"releases\":"<<releases<<",\"physics_steps\":"<<steps<<",\"bone_writes\":"<<boneWrites
        <<",\"damage_events\":"<<damageEvents<<",\"unsupported\":"<<unsupported<<",\"errors\":"<<errors<<",\"contact_steps\":"<<contactSteps
        <<",\"physics_frame_misses\":"<<physicsFrameMisses<<",\"physics_frame_completions\":"<<physicsFrameCompletions
        <<",\"geometry_frame\":"<<lastBikeRenderPhase.frame<<",\"renderer_barrier\":"<<(lastBikeRenderPhase.rendererBarrier?"true":"false")
        <<",\"bike_components_rendered\":"<<lastBikeRenderPhase.rendered<<",\"bike_components_offscreen\":"<<lastBikeRenderPhase.offscreen
        <<",\"bike_geometry_rejected\":"<<lastBikeRenderPhase.rejected<<",\"bike_geometry_motion_unverified\":"<<lastBikeRenderPhase.unverifiedMotion
        <<",\"bike_geometry_local_error\":"<<lastBikeRenderPhase.maxNativeLocalError<<",\"bike_geometry_world_error\":"<<lastBikeRenderPhase.maxNativeWorldError
        <<",\"native_getups\":"<<recoveryStarts<<",\"recovery_blends_finished\":"<<recoveryFinishes<<",\"max_segment_length_error\":"<<maxSegmentError
        <<",\"native_getups_completed\":"<<nativeGetupCompleted<<",\"upright_recoveries\":"<<uprightRecoveries<<",\"vehicle_hits\":"<<vehicleHits<<",\"bike_ejections\":"<<bikeEjections
        <<",\"vehicle_braces\":"<<braceStarts<<",\"door_grab_requests\":"<<grabStarts<<",\"door_grab_feedback\":"<<grabFeedback
        <<",\"mounted_bike_reactions\":"<<mountedReactions
        <<",\"recovery_aligned\":"<<recoveryAligned<<",\"recovery_interrupted\":"<<recoveryInterrupted
        <<",\"native_vehicle_contacts\":"<<nativeVehicleContacts
        <<",\"body_relocations\":"<<bodyRelocations<<",\"relocation_task_changes\":"<<relocationTaskChanges
        <<",\"largest_bone_delta\":"<<biggestBoneDelta<<",\"world_triangles\":"<<(collision?collision->triangles:0)<<",\"moving_vehicle_boxes\":"<<(collision?collision->boxes:0)
        <<",\"mean_tick_ms\":"<<(ticks?totalTickMs/ticks:0)<<",\"peak_tick_ms\":"<<peakTickMs<<",\"protect_missions\":"<<(protectMissions?"true":"false")
        <<",\"playing\":"<<(playing()?"true":"false")<<",\"cutscene_processing\":"<<(cutscene()?"true":"false")<<",\"on_mission\":"<<(*reinterpret_cast<int*>(0xC8D4C0)==9&&onMission()?"true":"false")<<",\"script_mission_active\":"<<(scriptMissionActive()?"true":"false")<<",\"agents\":[";
    bool comma=false;for(auto&[ref,a]:agents){
        if(comma)s<<',';comma=true;V com{},vel{};nm_agent_get_com(a->agent,&com.x,&vel.x);int contact=0;for(int i=0;i<21;i++)contact|=nm_agent_get_part_contact(a->agent,i);
        s<<"{\"ref\":"<<ref<<",\"model\":"<<a->model<<",\"recovering\":"<<(a->recovering?"true":"false")<<",\"recovery_stage\":"<<a->recoveryStage<<",\"recovery_anim\":"<<a->recoveryAnim<<",\"stable_seconds\":"<<a->stableFor<<",\"max_recovery_step\":"<<a->maxRecoveryStep<<",\"com\":["<<com.x<<','<<com.y<<','<<com.z<<"],\"speed\":"<<length(vel)<<",\"pelvis_z\":"<<a->current[0].p.z<<",\"foot_z\":"<<std::min(a->current[3].p.z,a->current[6].p.z)<<",\"contact_mask\":"<<contact<<",\"awake\":"<<nm_agent_is_awake(a->agent)<<",\"energy\":"<<nm_agent_get_kinetic_energy(a->agent)<<",\"initial_com_z\":"<<a->initialComZ<<",\"peak_com_rise\":"<<a->peakComRise<<",\"peak_up_speed\":"<<a->peakUpSpeed<<'}';
    }s<<"]}";
    auto temp=folder/"state.tmp";{std::ofstream f(temp);f<<s.str();}MoveFileExW(temp.c_str(),(folder/"state.json").c_str(),MOVEFILE_REPLACE_EXISTING);
}
static void(__cdecl* originalProcess)()=nullptr;
static void(__thiscall* originalPreRender)(void*)=nullptr;
static void(__thiscall* originalDamage)(DamageEvent*,void*)=nullptr;
static void(__thiscall* originalBikeOut)(void*,void*)=nullptr;
#include "VehicleExits.hpp"
#include "BikeTransitions.hpp"
#include "VehicleEntries.hpp"
#include "ExpandedSkinAdapterTest.hpp"
static void __fastcall bikeOut(void* ev,void*,void* ped){
    processNativeBikeOut(ev,ped);
}
#include "BikeHitFalls.hpp"

static void syncPedRenderRoot(void*p){
    if(!p||!matrix(p)||!at<void*>(p,0x18))return;
    // Post-PreRender contact corrections must also publish the entity's new
    // root to RenderWare; CWorld::RemoveAndAdd does not update its frame.
    reinterpret_cast<void(__thiscall*)(void*)>(0x446F90)(p);
    reinterpret_cast<void(__thiscall*)(void*)>(0x532B00)(p);
}
static bool refreshNativePalette(void*p){
    return publishCurrentNativePalette(p);
}
static void applyPhysicalPalette(void*p);
static void evaluatePedPalette(void*p){
    if(!ready||!p)return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(evaluatedPedFrame!=frame){evaluatedPeds.clear();evaluatedPedFrame=frame;}
    const int identity=reference(p);
    if(identity<0||evaluatedPeds.contains(identity))return;
    evaluatedPeds[identity]=p;
    sampleNativeIdlePose(p);
    const bool authoredCombat=combatRender(p);
    if(!authoredCombat)nikoRender(p);
    if(!authoredCombat)renderPlayerShove(p);
    if(!authoredCombat)aimMeleeStrike(p);
    applyWalkingSupport(p);
    sampleAnimatedContacts(p);
    sampleMeleeContact(p);
    captureExpandedSkinAuthored(p);
    int ref=reference(p);
    // Admission waits until the global barrier: a ped may precede its bike in
    // the render lists. Creating it here seeds vehicle history with coarse COL,
    // then interpolates that centre into a different mesh-component centre.
    // The current native palette remains available to simulatePhysicsFrame.
    auto it=agents.find(ref);if(it==agents.end())return;auto&a=*it->second;
    if(p!=a.ped||!a.rig->incoming(p,a.incoming,a.bones))return;
    std::array<float,147> pose{};for(int i=0;i<21;i++){auto t=a.incoming[i];float*o=pose.data()+i*7;o[0]=t.p.x;o[1]=t.p.y;o[2]=t.p.z;o[3]=t.q.x;o[4]=t.q.y;o[5]=t.q.z;o[6]=t.q.w;}
    ok(nm_agent_set_incoming_transforms(a.agent,pose.data(),21));
    if(a.recovering){
        const M before=*matrix(p);
        sampleRecoveryPose(a);
        // Pose fitting can align the native root/heading. Re-evaluate at the
        // SAME animation time before publishing its collision pose.
        if(length(before.pos-matrix(p)->pos)>1.e-6f||length(before.up-matrix(p)->up)>1.e-6f){
            if(refreshNativePalette(p)&&a.rig->incoming(p,a.incoming,a.bones)){
                for(int i=0;i<21;i++){const auto&t=a.incoming[i];const float row[]={t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w};std::copy(std::begin(row),std::end(row),pose.begin()+7*i);}
                ok(nm_agent_set_incoming_transforms(a.agent,pose.data(),21));
            }
        }
        // Recovery is animation-owned. Publish its current blend before
        // contact resolution, then reapply the corrected pose after the solve.
        captureExpandedSkinAuthored(p);
        applyPhysicalPalette(p);
    }
}
static void __fastcall preRender(void*p,void*){
    originalPreRender(p);
    evaluatePedPalette(p);
}
static void applyPhysicalPalette(void*p){
    if(!ready)return;
    syncPedRenderRoot(p);
    SessionPedTrace trace{p}; // observe the final palette after the island solve
    auto it=agents.find(reference(p));if(it==agents.end())return;auto&a=*it->second;
    if(a.ped!=p||a.bones.empty())return;
    if(!applyBones)return;
    float age=(gameTime()-a.start)*.001f;
    const bool animatedRecovery=a.recovering&&a.recoveryAnimatedOwner;
    float weight=animatedRecovery?recoveryWeight(a):std::clamp(age/.15f,0.f,1.f);
    // Melee begins from this evaluated pose. A 150 ms crossfade let the
    // advancing native hit animation draw the victim inside the attacker's fist.
    if((a.kind==Reaction::Melee||a.kind==Reaction::Impact||a.kind==Reaction::Bump)&&!animatedRecovery)weight=1.f;
    if(a.poseSeeded&&!animatedRecovery)weight=1.f;
    if(a.recovering&&!animatedRecovery)weight=1.f;
    if(a.mounted)weight*=.40f*std::clamp((1.8f-age)/.6f,0.f,1.f);
    if(a.preview)weight*=std::clamp((activeSeconds-age)/blendSeconds,0.f,1.f);
    auto*h=hierarchy(p);if(!h)return;
    // The solver now consumes the complete host interval before this palette.
    // Publish its actual pose at the same time as the vehicle, with no residual
    // accumulator interpolation or extrapolation.
    const auto&interpolated=a.current;
    const auto physical=animatedRecovery?a.frozenBones:a.rig->physicalBones(interpolated);
    auto output=a.rig->blendBones(a.bones,physical,weight,a.preview||a.mounted);
    auto palette=a.rig->renderPalette(h->matrices,output);
    const auto*expandedFrame=stageExpandedSkinAdapterTest(a,weight,animatedRecovery);
    if(expandedFrame){
        bool valid=expandedFrame->nativeWorld.size()==a.bones.size();
        for(size_t i=0;valid&&i<a.bones.size();i++)valid=expandedSkin::affineFinite(expandedFrame->nativeWorld[i])&&length(expandedFrame->nativeWorld[i].pos-a.bones[i].p)<=8.f;
        if(valid)palette=expandedFrame->nativeWorld;
        else{expandedTestManager->invalidateFrame();expandedFrame=nullptr;event("expanded_skin_test_refused",a.ref,a.model,"palette_publication_preflight_failed=1");}
    }
    for(size_t i=0;i<output.size();i++)output[i]=transform(palette[i]);
    for(size_t i=0;i<output.size();i++)if(articulatedHelper(a.rig->ids[i])&&a.rig->parents[i]>=0){
        int parent=a.rig->parents[i];T n=mul(inverse(a.bones[parent]),a.bones[i]),o=mul(inverse(output[parent]),output[i]);
        a.maxHelperLocalError=std::max(a.maxHelperLocalError,length(n.p-o.p));
    }
    a.maxHeadTorsoDegrees=std::max(a.maxHeadTorsoDegrees,a.rig->relativeTurnDegrees(output,saBone::chest,saBone::head));
    a.maxNeckTorsoDegrees=std::max(a.maxNeckTorsoDegrees,a.rig->relativeTurnDegrees(output,saBone::chest,saBone::neck));
    a.anatomyFrames++;
    if(animatedRecovery&&a.lastRendered.size()==output.size()&&gameTime()-a.lastRenderTime<50)
        for(size_t i=0;i<output.size();i++){
            const float step=length(output[i].p-a.lastRendered[i].p);a.maxRecoveryStep=std::max(a.maxRecoveryStep,step);
            if(step>.12f)event("recovery_pose_step",a.ref,a.model,"stage="+std::to_string(a.recoveryStage)+" bone="+std::to_string(a.rig->ids[i])+" step="+std::to_string(step)+" weight="+std::to_string(weight));
        }
    auto renderedJoints=a.rig->jointFrames(output);
    if(expandedFrame){
        renderedJoints[8]=transform(expandedFrame->pose.world[a.rig->ids.size()]);
        renderedJoints[10]=transform(expandedFrame->pose.world[a.rig->ids.size()+1]);
    }
    const float renderDt=(gameTime()-a.lastRenderTime)*.001f;
    for(int n=0;n<21;n++){
        const T body=mul(renderedJoints[n],a.rig->offsets[n]);
        if(a.lastRenderTime!=gameTime()){
            a.renderVelocity[n]=renderDt>=.002f&&renderDt<=.05f?(body.p-a.renderedParts[n].p)*(1.f/renderDt):V{};
            a.renderSpin[n]=poseAngularVelocity(a.renderedParts[n].q,body.q,renderDt);
            if(!finite(a.renderVelocity[n])||length(a.renderVelocity[n])>20.f)a.renderVelocity[n]={};
        }
        a.renderedParts[n]=body;
    }
    a.lastRendered=output;a.lastRenderTime=gameTime();
    observeVehicleEntryRender(a,output);
    if(a.kind==Reaction::Melee&&!a.recovering&&!a.meleeRendered){
        event("melee_render_handoff",a.ref,a.model,"delay_ms="+std::to_string(gameTime()-a.meleeReceived)+" physical_weight="+std::to_string(weight));
        a.meleeRendered=true;
    }
    for(size_t n=0;n<a.bones.size();n++){
        T target=output[n];
        float delta=length(target.p-a.bones[n].p);
        if(!finite(target.p)||delta>8){errors++;continue;}
        // Expanded torso has two actual extra joints. Its verified expanded
        // topology/origin residual is logged separately; the old native-parent
        // segment length is not a valid invariant across those new joints.
        if(!expandedFrame&&weight>.999f&&a.rig->parents[n]>=0){const int parent=a.rig->parents[n];maxSegmentError=std::max(maxSegmentError,std::abs(length(target.p-output[parent].p)-length(a.rig->bindLocal[n].p)));}
        biggestBoneDelta=std::max(biggestBoneDelta,delta);h->matrices[n]=palette[n];boneWrites++;
    }
    noteSkinGroundAudit(p,a,h,gameTime());
    if(expandedFrame){
        const auto*evaluation=currentNativePaletteEvaluation(p);
        if(!evaluation||!expandedTestManager->commit(evaluation->frame,evaluation->serial))event("expanded_skin_test_refused",a.ref,a.model,"published_palette_epoch_commit_failed=1");
    }
}
static void acceptedDamage(DamageEvent* hitEvent,void*p);
static void __fastcall damage(DamageEvent* hitEvent,void*,void*p){originalDamage(hitEvent,p);}
#include "HitDetection.hpp"
#include "Explosions.hpp"
#include "Aiming.hpp"
#include "MeleeResponse.hpp"
#include "CombatRuntime.hpp"
#include "PhysicalDeathNative.hpp"
#include "NikoMovement.hpp"
#include "PlayerShove.hpp"
#include "RoadPoseSeeds.hpp"
#include "StandingContacts.hpp"
static void acceptedDamage(DamageEvent* hitEvent,void*p){
    if(combatDelivering)return; // authored sweep delivers native damage once
    const float amount=hitEvent->response.health+hitEvent->response.armour;
    watchSessionPed(p);watchSessionPed(hitEvent->source);
    event("native_damage_decision",reference(p),at<short>(p,0x22),"stage=damage_calculation attacker="+std::to_string(reference(hitEvent->source))+" vehicle="+std::to_string(vehicleRef(hitEvent->source))+" weapon="+std::to_string(hitEvent->weapon)+" piece="+std::to_string(hitEvent->piece)+" native_anim="+std::to_string(hitEvent->anim)+" native_group="+std::to_string(hitEvent->animGroup)+" damage="+std::to_string(amount)+" protected="+std::to_string(cutscene()||(protectMissions&&scriptMissionActive())));
    const char* skip=!ready?"engine_not_ready":!detectImpacts?"impact_detection_disabled":!playing()?"not_playing":cutscene()?"cutscene":(protectMissions&&scriptMissionActive())?"mission_or_phone":!std::isfinite(amount)?"invalid_damage":amount<=0?"no_damage":nullptr;
    if(skip){event("damage_kept_native",reference(p),at<short>(p,0x22),skip);return;}
    const bool isPlayer=p==player();const int weapon=hitEvent->weapon;
    const bool playerImpact=isPlayer&&playerVehicleImpacts&&playerRoadDamage(weapon,vehicleRef(hitEvent->source)>=0)&&playerGroundVehicle(at<int>(hitEvent->source,0x594));
    const bool blast=weapon==51;
    const bool playerBlast=isPlayer&&playerExplosions&&blast;
    if(isPlayer&&((!playerImpact&&!playerBlast)||inVehicle(p))){event("player_damage_kept_native",reference(p),at<short>(p,0x22),"weapon="+std::to_string(weapon));return;}
    if(!validPed(p,true,true,playerImpact||playerBlast)){event("damage_kept_native",reference(p),at<short>(p,0x22),"ped_not_eligible");return;}
    damageEvents++;V direction=matrix(p)->up*-1.f;
    if(auto* attacker=hitEvent->source;attacker&&length(position(p)-position(attacker))>.01f)direction=normalized(position(p)-position(attacker));
    int part=10;
    if(queueBikeHitFall(p,weapon))return;
    if(ridingBike(p)){
        if(at<float>(p,0x540)>0)pending[reference(p)]={direction*std::clamp(amount*.15f,2.f,12.f),direction,gameTime(),10,false,Reaction::Mounted,vehicleRef(at<void*>(p,0x58C))};
        event("accepted_mounted_damage",reference(p),at<short>(p,0x22),"native_health="+std::to_string(at<float>(p,0x540)));return;
    }
    switch(hitEvent->piece){case 3:part=8;break;case 4:part=10;break;case 5:part=14;break;case 6:part=18;break;case 7:part=1;break;case 8:part=4;break;case 9:part=12;break;default:break;}
    const bool bullet=firearmDamage(weapon);
    if(!bullet&&!blast&&!((weapon>=0&&weapon<=15)||weapon==49||weapon==50)){event("damage_kept_native",reference(p),at<short>(p,0x22),"unsupported_weapon="+std::to_string(weapon));return;}
    // Shotgun pellets retain all native damage but share one physical reaction
    // per discharge. This also prevents eight pellets restarting eight tasks.
    static std::map<int,unsigned> lastShotgun;
    if(weapon>=25&&weapon<=27){int ref=reference(p);if(lastShotgun.count(ref)&&gameTime()-lastShotgun[ref]<120)return;lastShotgun[ref]=gameTime();}
    float momentum=bullet?std::clamp(amount*.25f,3.f,35.f):std::clamp(amount*.6f,5.f,60.f);
    const bool vehicleImpact=weapon==49||weapon==50;
    const int car=vehicleImpact?vehicleRef(hitEvent->source):-1;
    if(vehicleImpact){
        vehicleHits++;part=weapon==50?0:1;
        if(auto* v=vehicleFromRef(car)){
            const V relative=at<V>(v,0x44)*50.f-at<V>(p,0x44)*50.f;
            if(length(relative)>.1f)direction=normalized(relative);
            // The moving COL surface transfers momentum in the solver. An
            // extra pelvis kick double-counted it and launched slow victims.
            momentum=0.f;
        }
    }
    Request request{directedHit(direction,momentum),direction,gameTime(),part,false,blast?Reaction::Explosion:(vehicleImpact?Reaction::Impact:(bullet?Reaction::Shot:Reaction::Melee)),car};
    if(bullet)if(auto*body=groundShotOwner(p)){
        body->groundShot=shotReceipt(*hitEvent);body->groundShotDelivered=false;
    }
    request.playerVehicle=playerImpact||playerBlast;request.weapon=weapon;request.attackerRef=reference(hitEvent->source);
    if(vehicleImpact&&car>=0)request.roadDamage={hitEvent->time,car,weapon,hitEvent->piece};
    if(beforePedVelocity.count(reference(p))){request.velocityOverride=true;request.velocity=beforePedVelocity[reference(p)];}
    if(vehicleImpact)if(auto seed=beforePedPose.find(reference(p));seed!=beforePedPose.end())request.pose=seed->second;
    if(blast)configureBlastRequest(request,p);
    if(request.kind==Reaction::Melee){configureMeleeRequest(request,p,amount);queueMeleeFrames(reference(p));}
    pending[reference(p)]=request;
    event("accepted_game_damage",reference(p),at<short>(p,0x22),"weapon="+std::to_string(weapon)+" damage="+std::to_string(amount)+" health="+std::to_string(at<float>(p,0x540)));
}
static std::vector<int> stepPhysicsActors(const std::vector<Active*>& live,float dt,unsigned now){
    const double intervalStarted=bikeGeometryClockMs();
    double bindingMs=0,solverMs=0,commitMs=0;
    std::vector<int> invalid;
    auto bad=[&](int ref){return std::find(invalid.begin(),invalid.end(),ref)!=invalid.end();};
    if(!bodyGroup)bodyGroup=nm_group_create();
    const NativeFrameSteps frameSteps(dt);
    FrameCostProfile intervalProfile("physics_interval",int(live.size()),0);
    intervalProfile.inputDt=dt;intervalProfile.substeps=frameSteps.count;
    prepareExternalIntervalScene();
    const double sceneMs=bikeGeometryClockMs()-intervalStarted;
    for(auto*entry:live){auto&a=*entry;
            updateMeleeThreat(a);
            if(a.kind!=Reaction::Impact)updateVehicleReaction(a);
            // The released body participates against the actual compound COL
            // immediately. A broad-box departure filter hid the entire bike,
            // including valid wheel/frame contacts throughout the crash.
    }
    for(int substep=0;substep<frameSteps.count;substep++){
        const double bindingStarted=bikeGeometryClockMs();
        const float stepSeconds=frameSteps.step(substep);
        nm_group_begin(bodyGroup);
        const uint64_t externalEpoch=(uint64_t(*reinterpret_cast<unsigned*>(0xB7CB4C))<<8)|uint64_t(substep);
        if(!stageExternalOwners(bodyGroup,externalEpoch,float(substep+1)/frameSteps.count,stepSeconds)){
            failOriginalGroundStep();
            for(auto*a:live)if(!bad(a->ref))invalid.push_back(a->ref);break;
        }
        beginSceneVehicleStep(float(substep+1)/frameSteps.count);
        // These snapshots are immutable during staging. A preview needs the
        // kinematic view, while the group needs the finite-owner view.
        enum class SceneBinding { Empty, Kinematic, Finite };
        SceneBinding scene=SceneBinding::Empty;
        auto bindScene=[&](bool finiteOwners){
            if(scene==SceneBinding::Empty||(scene==SceneBinding::Finite&&!finiteOwners)){
                scene=SceneBinding::Empty;
                if(!bindSceneVehicleStep())return false;
                scene=SceneBinding::Kinematic;
            }
            if(finiteOwners&&scene!=SceneBinding::Finite){
                if(!bindExternalObstacles()){scene=SceneBinding::Empty;return false;}
                scene=SceneBinding::Finite;
            }
            return true;
        };
        for(auto*entry:live){auto&a=*entry;if(bad(a.ref))continue;
            a.accumulator=frameSteps.remaining(substep);
            // Physical peers participate below as finite articulated bodies;
            // never submit a second, infinite-mass capsule copy.
            if(!ok(nm_world_set_capsules(a.mounted?a.mountedWorld:collision->world,nullptr,0))){invalid.push_back(a.ref);continue;}
                if(a.mounted||a.preview)anchorAnimation(a,a.mounted);
                applyFootLoads(a,stepSeconds);
                for(auto&push:a.meleePushes){
                    const float dt=std::min(push.left,stepSeconds);const V point=mul(a.current[push.part],T{{},push.partLocal}).p,impulse=push.force*dt;
                    ok(nm_agent_apply_impulse(a.agent,push.part,&point.x,&impulse.x));push.left-=dt;push.delivered=push.delivered+impulse;
                    if(push.left<=0)event("melee_impulse_delivered",a.ref,a.model,"hit_ms="+std::to_string(push.hitTime)+" part="+std::to_string(push.part)+" total_ns="+std::to_string(length(push.delivered))+" integration_seconds="+std::to_string(push.duration));
                }
                std::erase_if(a.meleePushes,[](const auto&push){return push.left<=0;});
                if(!a.mounted){
                    if(!bindScene(!a.preview)){invalid.push_back(a.ref);continue;}
                    if((a.kind==Reaction::Impact&&a.roadReaction==1)||(a.kind==Reaction::Brace&&a.brace))updateVehicleReaction(a);
                }
                combatDrivePhysics(a);
            a.previous=a.current;
            const int result=(a.mounted||a.preview)?nm_agent_step(a.agent,a.mounted?a.mountedWorld:collision->world,stepSeconds):nm_group_add(bodyGroup,a.agent,uint32_t(a.ref),collision->world);
            if(!ok(result))invalid.push_back(a.ref);
        }
        // External-only islands also need this exact geometry; a final preview
        // actor must not replace the shared scene by its kinematic snapshot.
        if(!bindScene(true)||!bindNativeVehicleEnvironment()){
            failOriginalGroundStep();
            for(auto*a:live)if(!bad(a->ref))invalid.push_back(a->ref);break;
        }
        bindingMs+=bikeGeometryClockMs()-bindingStarted;
        verifyOriginalGroundMembers(live,invalid);
        const double solveStarted=bikeGeometryClockMs();
        const bool solved=ok(nm_group_step(bodyGroup,collision->world,stepSeconds));
        solverMs+=bikeGeometryClockMs()-solveStarted;
        const double commitStarted=bikeGeometryClockMs();
        const bool committed=solved&&commitExternalOwners(bodyGroup,stepSeconds);
        commitMs+=bikeGeometryClockMs()-commitStarted;
        if(!committed){
            failOriginalGroundStep();
            for(auto*a:live)if(!bad(a->ref))invalid.push_back(a->ref);break;
        }
        observeSolvedExternalPairs(bodyGroup,stepSeconds);
        finishSceneVehicleStep(live);
        for(auto*entry:live){auto&a=*entry;if(bad(a.ref))continue;
                if(a.physicalAnimation){const int status=nm_agent_animation_pose_status(a.agent);if(status!=a.animationDriveStatus){event("combat_physical_animation",a.ref,a.model,"motor_status="+std::to_string(status)+" body_pose_not_overwritten=1");a.animationDriveStatus=status;}}
                for(auto h=a.vehicleHistory.begin();h!=a.vehicleHistory.end();)if(!collision->findIdentity(h->first))h=a.vehicleHistory.erase(h);else ++h;
                if(!a.mounted){selectSolvedRoadReaction(a,stepSeconds);applyVehicleBodyFeedback(a,stepSeconds);}
                if(!readPose(a,a.current)){invalid.push_back(a.ref);continue;}steps++;noteSolverGapAudit(a,now);
                if(a.kind==Reaction::DoorEntry&&!a.mounted&&!a.preview)observeVehicleEntrySolvedStep(a,stepSeconds);
                if(a.kind==Reaction::Melee&&a.meleeMotionSample<4){
                    static constexpr unsigned times[]={100,250,500,1000};
                    if(now-a.meleeReceived>=times[a.meleeMotionSample]){
                        const T head=mul(inverse(a.current[10]),a.current[12]);
                        const Q q=mul(head.q,conjugate(a.meleeHeadRotation));
                        V linear{},angular{};nm_agent_get_part_velocity(a.agent,12,&linear.x,&angular.x);
                        V centre{},motion{};nm_agent_get_com(a.agent,&centre.x,&motion.x);
                        event("melee_motion_response",a.ref,a.model,"age_ms="+std::to_string(now-a.meleeReceived)+
                            " head_relative_m="+std::to_string(length(head.p-a.meleeHeadLocal))+
                            " head_relative_degrees="+std::to_string(2.f*std::acos(std::clamp(std::abs(q.w),0.f,1.f))*57.29578f)+
                            " pelvis_shift_m="+std::to_string(length(a.current[0].p-a.meleePelvis))+
                            " chest_height_m="+std::to_string(a.current[10].p.z-a.current[0].p.z)+
                            " head_speed_mps="+std::to_string(length(linear))+" head_spin="+std::to_string(length(angular))+
                            " com_rise="+std::to_string(centre.z-a.initialComZ)+" com_vz="+std::to_string(motion.z)+
                            " engine_fallen="+std::to_string(nm_agent_has_fallen(a.agent)));a.meleeMotionSample++;
                    }
                }
                {V com{},vel{};nm_agent_get_com(a.agent,&com.x,&vel.x);a.peakComRise=std::max(a.peakComRise,com.z-a.initialComZ);a.peakUpSpeed=std::max(a.peakUpSpeed,vel.z);
                 if(a.seededBikeFall&&finite(vel))a.acceptedBikeVelocity=vel;}
                readVehicleFeedback(a);
                // The engine reports opaque contact bits; the current character's bits differ
                // from the old header comment. Count a solver contact record, not a guessed mask.
                for(int part=0;part<21;part++)if(nm_agent_get_part_contact(a.agent,part)>0){contactSteps++;break;}
        }
        float contacts[4]{};
        if(nm_group_contacts(bodyGroup,contacts)==0&&contacts[0]>0&&now-lastGroupReport>=100){
            lastGroupReport=now;event("physical_peer_island",-1,-1,"members="+std::to_string(live.size())+" pairs="+std::to_string(int(contacts[0]))+" points="+std::to_string(int(contacts[1]))+" pre_solve_depth="+std::to_string(contacts[2])+" normal_impulse_ns="+std::to_string(contacts[3])+" reciprocal=1");
        }
    }
    for(auto*a:live)a->accumulator=0;
    collision->applyVehicleBoxes();
    static unsigned costReport=0;
    if(now-costReport>=500){costReport=now;event("shared_physics_cost",-1,-1,
        "dt="+std::to_string(dt)+" substeps="+std::to_string(frameSteps.count)+
        " agents="+std::to_string(live.size())+" owners="+std::to_string(externalHostOwners.size())+
        " vehicle_shapes="+std::to_string(collision->vehicles.size())+
        " scene_ms="+std::to_string(sceneMs)+" binding_ms="+std::to_string(bindingMs)+
        " solver_ms="+std::to_string(solverMs)+" commit_ms="+std::to_string(commitMs)+
        " total_ms="+std::to_string(bikeGeometryClockMs()-intervalStarted));}
    return invalid;
}

static void simulatePhysicsFrame(){
    FrameCostProfile cost("simulate",int(agents.size()),int(pending.size()));
    LARGE_INTEGER before,after,freq;QueryPerformanceCounter(&before);QueryPerformanceFrequency(&freq);
    unsigned now=gameTime();
    if(!playing()||!player()||cutscene()||(protectMissions&&scriptMissionActive())||lastArea!=area()||(lastTime&&now<lastTime)){
        const char* reason=!playing()?"game_not_playing":!player()?"player_unavailable":cutscene()?"cutscene_boundary":
            (protectMissions&&scriptMissionActive())?"mission_boundary":lastArea!=area()?"area_boundary":"game_time_reset";
        clear(reason);combatClear();exitTransits.clear();lastArea=area();lastTime=now;
    }else{
        // Native movement and NM must consume the same simulated interval.
        // CTimer milliseconds can advance farther than the capped movement
        // timestep during a slow frame; using them gave physics extra time.
        float dt=lastTime?combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C)):0;lastTime=now;
        processBikeHitFalls();
        // Native entities have completed movement. All body contact queries
        // below must see those same current solid/vehicle transforms.
        // Normal animated owners participate even when there are no NM
        // agents. Build/refresh their world BEFORE any contact query or group
        // step; recovery's terminal scene runs later and cannot initialize it
        // retroactively for this interval.
        collision->update(position(player()));cost.lap(0);
        processWalkingContacts(combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C)));
        applyWheelBodyLoads();
        scanApproachingCars();cost.lap(1);
        for(auto it=pending.begin();it!=pending.end();){
            auto request=it->second;int ref=it->first;void*p=pedFromRef(ref);
            if(now-request.time>2500||!validPed(p,!request.test,request.kind==Reaction::Mounted,permitsPlayer(request))){it=pending.erase(it);continue;}
            if(auto a=agents.find(ref);a!=agents.end()){
                auto&active=*a->second;
                if((active.recovering||active.taskAborted)&&!active.mounted){
                    // Reacquire only the task. Destroying/recreating the body
                    // enabled native collision for a frame between punches.
                    resumePhysicalContact(active,request.kind==Reaction::Shot?"shot_rehit_continuous_body":"melee_rehit_continuous_body");
                }
                // Damage may abort the native ownership task in this very
                // frame. Keep its physical body even while the accepted melee
                // waits for the completed render palettes; otherwise cleanup
                // raises a prone body to standing and recreates it next frame.
                if(request.kind==Reaction::Melee&&!request.meleeContactReady){++it;continue;}
                if(active.mounted&&request.kind!=Reaction::Mounted){
                    restore(active,"new_hit_or_ejection",false);agents.erase(a);continue;
                }
                releaseVehicle(active,"new_impact");startReaction(active,request);active.start=now;
                it=pending.erase(it);
            }else if((request.pose&&(request.kind==Reaction::BikeFall||request.kind==Reaction::VehicleExit||request.kind==Reaction::DoorEntry))||
                     (evaluatedPeds.contains(ref)&&(request.kind!=Reaction::Melee||request.meleeContactReady))){
                // These transitions already carry the fully evaluated source
                // skeleton. Waiting for another visible PreRender leaves SA's
                // standing capsule in charge inside the vehicle for a frame.
                // Admit the saved body in this simulation tick; unsampled hits
                // still wait for their real animation palette.
                if(activate(p,request)){
                    event("seeded_transition_admitted",ref,at<short>(p,0x22),"pending_ms="+std::to_string(now-request.time)+" source_pose_preserved=1");
                    it=pending.erase(it);
                }else ++it;
            }else ++it; // Unsampled hits need the normal PreRender bone update.
        }
        cost.lap(2);
        std::vector<Active*> live;
        for(auto it=agents.begin();it!=agents.end();){
            auto&a=*it->second;void*p=pedFromRef(a.ref);
            const bool vehicleBoundary=!p||(a.mounted?(!ridingBike(p)||vehicleFromRef(a.vehicle)!=at<void*>(p,0x58C)):(inVehicle(p)||at<int>(p,0x530)==50));
            const bool invalidPed=p!=a.ped||!p||at<short>(p,0x22)!=a.model||at<unsigned char>(p,0x2F)!=area()||at<void*>(p,0xFC)||vehicleBoundary;
            if(invalidPed||length(position(p)-position(player()))>radius+10){
                if(p&&GetEnvironmentVariableA("GTASA_TRANSITION_BOUNDARY_AUDIT",nullptr,0)){
                    event("physical_lifecycle_boundary",a.ref,a.model,
                        "same_ped="+std::to_string(p==a.ped)+" same_model="+std::to_string(at<short>(p,0x22)==a.model)+
                        " same_area="+std::to_string(at<unsigned char>(p,0x2F)==area())+" attached="+std::to_string(at<void*>(p,0xFC)!=nullptr)+
                        " ped_state="+std::to_string(at<int>(p,0x530))+" in_vehicle="+std::to_string(inVehicle(p))+
                        " vehicle_ref="+std::to_string(vehicleRef(at<void*>(p,0x58C)))+" mounted="+std::to_string(a.mounted)+
                        " owned_task_current="+std::to_string(a.ownedTask&&physicalTask(p)==a.ownedTask)+
                        " player_distance="+std::to_string(length(position(p)-position(player()))));
                }
                if(invalidPed||!resumeNativeBikeFall(a,"collision_coverage_exit"))
                    restore(a,"ped_lifecycle_or_vehicle",false);
                it=agents.erase(it);continue;
            }
            if(a.mounted&&(now-a.start>1800||at<float>(p,0x540)<=0)){restore(a,"mounted_reaction_finished",false);it=agents.erase(it);continue;}
            if(a.preview&&(now-a.start)*.001f>=activeSeconds){restore(a,"anticipatory_brace_finished",false);it=agents.erase(it);continue;}
            // A late native delivery of the SAME already simulated road hit
            // may retire our ownership task after pending was consumed. Let SA
            // finish handling its damage, then retain this exact physical body.
            // Unrelated tasks, death and protected boundaries still win.
            if(a.taskAborted&&a.resumeDeliveredRoadDamage&&at<float>(p,0x540)>0&&
                sameDeliveredRoadDamage(a.roadDamage,a.roadDamage,now))
                resumePhysicalContact(a,"road_damage_continuous_body");
            if(a.taskAborted&&a.resumeDeliveredCombatDamage&&at<float>(p,0x540)>0&&
                sameDeliveredCombatDamage(a.combatDamage,a.combatDamage,now))
                resumePhysicalContact(a,"combat_damage_continuous_body");
            if(a.taskAborted&&a.groundShotDelivered&&at<float>(p,0x540)>0&&sameDeliveredShot(a.groundShot,a.groundShot,now))
                resumePhysicalContact(a,"shot_damage_continuous_body");
            const bool combatDeath=a.kind==Reaction::Melee&&sameDeliveredCombatDamage(a.combatDamage,a.combatDamage,now);
            const bool entryDeath=a.kind==Reaction::DoorEntry&&a.entryDeathDelivered&&now-a.entryFatalAt<=150;
            const bool impactDeath=a.taskAborted&&at<float>(p,0x540)<=0&&consumePhysicalImpactDeath(a);
            if(a.taskAborted&&at<float>(p,0x540)<=0&&(combatDeath||entryDeath||impactDeath)){
                // SA has replaced our dormant live-ped task with its death
                // task. Keep native death accounting and the same corpse;
                // relinquishing the body here resumed the old death clip.
                retireRecoveryAnimation(a);a.ownedTask=nullptr;a.taskAborted=false;
                a.recovering=false;a.physicalAnimation=false;
                nm_agent_cancel_animation_pose(a.agent);
                event(impactDeath?"impact_death_continuous_body":entryDeath?"entry_death_continuous_body":"combat_death_continuous_body",a.ref,a.model,"same_agent=1 native_death_task_preserved=1");
            }
            if(a.taskAborted){restore(a,"native_task_superseded",false);it=agents.erase(it);continue;}
            if(a.recovering){
                if(at<float>(p,0x540)<=0){restore(a,"died_during_recovery",false);it=agents.erase(it);continue;}
                if(a.recoveryClearanceLost&&!a.recoveryAnimatedOwner){
                    // A pending fit never surrendered its NM owner. Keep
                    // simulating it while looking for real room to get up.
                    a.recoveryClearanceLost=false;
                }
                if(a.recoveryClearanceLost){
                    a.recoveryClearanceLost=false;
                    resumePhysicalContact(a,"recovery_car_clearance_lost");
                    ok(nm_agent_task_catch_fall(a.agent,"*"));ensureRecoveryTask(a);
                    live.push_back(&a);++it;continue;
                }
                if(a.recoveryDone||(a.recoveryAnimatedOwner&&a.uprightRecovery&&(now-a.recoverStart)*.001f>=blendSeconds)){
                    recoveryFinishes++;event("recovery_motion",a.ref,a.model,"max_bone_step="+std::to_string(a.maxRecoveryStep));
                    restore(a,"native_getup_pose_handoff_complete",false);it=agents.erase(it);continue;
                }
                if(!a.uprightRecovery&&a.ownedTask&&physicalTask(p)!=a.ownedTask){
                    recoveryInterrupted++;restore(a,"recovery_native_task_interrupted_or_timeout",false);it=agents.erase(it);continue;
                }
                if(!a.recoveryAnimatedOwner)live.push_back(&a);
                ++it;continue;
            }
            live.push_back(&a);++it;
        }
        const unsigned currentFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);
        std::erase_if(live,[&](const Active*a){return !physicalAdmissionCanStep(a->admittedFrame,currentFrame,a->seedBeforeCurrentFrame);});
        promoteRecoveryContacts(live,dt,now);
        cost.lap(3);
        const auto invalid=stepPhysicsActors(live,dt,now);cost.lap(4);
        for(auto it=agents.begin();it!=agents.end();){
            auto&a=*it->second;void*p=pedFromRef(a.ref);
            if(std::find(live.begin(),live.end(),&a)==live.end()){++it;continue;}
            if(std::find(invalid.begin(),invalid.end(),a.ref)!=invalid.end()){
                if(!resumeNativeBikeFall(a,"invalid_physics_pose_rejected"))
                    restore(a,"invalid_physics_pose_rejected",true);
                it=agents.erase(it);continue;
            }
            vehicleEntryDragDamage(a,dt);
            const bool exitReady=updateVehicleExitTask(a)&&updateVehicleEntryTask(a);
            const bool bumpReady=updatePedBumpTask(a);
            const float responseSeconds=a.kind==Reaction::VehicleExit||a.kind==Reaction::DoorEntry||a.kind==Reaction::Bump||a.meleeTaskOriginal?0.f:activeSeconds;
            if(!a.recovering&&!a.mounted&&!a.preview&&exitReady&&bumpReady&&!pending.contains(a.ref)&&!combatReactionPlaying(a)&&nm_agent_road_recovery_ready(a.agent)==1&&now>=a.probeRecoveryNotBefore&&at<float>(p,0x540)>0&&(now-a.start)*.001f>=responseSeconds){
                V com{},velocity{};nm_agent_get_com(a.agent,&com.x,&velocity.x);
                bool contact=false;for(int i=0;i<21;i++)contact|=nm_agent_get_part_contact(a.agent,i)>0;
                V linear{},spin{};float maxSpin=0;for(int part:{0,10}){nm_agent_get_part_velocity(a.agent,part,&linear.x,&spin.x);maxSpin=std::max(maxSpin,length(spin));}
                a.stableFor=contact&&length(velocity)<recoveryMaxSpeed&&maxSpin<recoveryMaxSpin?a.stableFor+dt:0;
                const bool originalUpright=(a.meleeTaskOriginal||a.kind==Reaction::Bump)&&bumpReady&&contact&&nm_agent_is_balancing(a.agent)==1&&nm_agent_has_fallen(a.agent)==0;
                // A blocked get-up still advances physics and its native root.
                // Only the actual handoff owns the pose/root from this point.
                if((originalUpright||a.stableFor>=recoveryStableSeconds)&&beginRecovery(a)){++it;continue;}
            }
            if(applyBones&&!a.mounted&&!a.preview){
                V root=a.current[0].p-rotate(rotation(*matrix(p)),a.rootOffset);
                if(finite(root)&&length(root-position(p))<8){movePhysicalRoot(p,root,a.playerVehicle);at<unsigned char>(p,0x1C)&=~1u;}
            }
            ++it;
        }
    }
    if(recoveryAllowed()){tickPhysicalPeerProbe();tickSolidBodyProbe();tickStaticMeshProbe();}
    cost.lap(5);
    traceRoadVehicles();cost.lap(6);
    QueryPerformanceCounter(&after);float ms=float((after.QuadPart-before.QuadPart)*1000.0/freq.QuadPart);totalTickMs+=ms;ticks++;peakTickMs=std::max(peakTickMs,ms);
    if(now-lastReport>500){lastReport=now;report();}
}
// PreRender may omit offscreen peds. Evaluate their CURRENT animation time
// explicitly; validity of yesterday's palette is not evidence of freshness.
// This does not call CPed::PreRender or advance animation/gameplay a second time.
static void evaluateOffscreenPalettes(){
    auto*pool=pedPool();if(!pool||!player())return;
    for(int n=0;n<pool->size;n++){
        if(pool->flags[n]&0x80)continue;void*p=pool->objects+n*0x7C4;
        const int ref=reference(p);if(evaluatedPeds.contains(ref)||!matrix(p)||
            at<unsigned char>(p,0x2F)!=area()||length(position(p)-position(player()))>radius+5.f)continue;
        if(!refreshNativePalette(p))continue;
        evaluatePedPalette(p);
    }
}
static void finishPhysicsFrame(){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(!ready||!physicsFrame.consume(frame))return;
    physicsFrameCompletions++;
    FrameCostProfile cost("finish",int(agents.size()),int(pending.size()));
    if(evaluatedPedFrame!=frame){evaluatedPeds.clear();evaluatedPedFrame=frame;}
    if(playing()&&player()&&!cutscene()&&!(protectMissions&&scriptMissionActive())){
        evaluateOffscreenPalettes();
        finishMeleeContacts();
        processVehicleEntries();
    }
    cost.lap(0);
    captureHostPredictorEnds();cost.lap(1);
    simulatePhysicsFrame();cost.lap(2);
    if(recoveryAllowed()){
        prepareRecoveryContactScene();
        commitPendingRecoveryPlacements();
    }
    cost.lap(3);
    for(const auto&[ref,p]:evaluatedPeds)if(pedFromRef(ref)==p){applyPhysicalPalette(p);recordFinalPedPose(p);}
    cost.lap(4);
    captureExpandedSkinRuntime(player(),"player-final");
    if(entryProbeRef>=0){const auto entry=agents.find(entryProbeRef);
        if(void*ped=pedFromRef(entryProbeRef))captureExpandedSkinRuntime(ped,"entry-native-final");
        if(entry!=agents.end())captureExpandedSkinRuntime(entry->second->ped,"entry-simulated-final");
        if(entry!=agents.end()&&entry->second->entryHolding>0)captureExpandedSkinRuntime(entry->second->ped,"entry-held-final");
    }
    physicsFrame.finish();
}
#include "VehicleParticles.hpp"
static void __cdecl process(){
    vehicleparticles::maintain(ready&&playing(),folder.parent_path()/"DeliriumSA.ini");
    FrameCostProfile cost("process",int(agents.size()),int(pending.size()));
    maintainIdleCaller();
    maintainExpandedSkinAdapterTest();
    if(ready&&physicsFrame.pending){
        // No matching renderer barrier was reached. Do not silently feed an old
        // dt into current geometry; report and retire the unconsumed ticket.
        event("physics_frame_barrier_missed",-1,-1,"source_frame="+std::to_string(physicsFrame.frame));
        physicsFrameMisses++;
        physicsFrame.reset();
    }
    if(ready)combatPrepare();
    if(ready&&lastArea==area()&&(!lastTime||gameTime()>=lastTime))combatTick(0);
    if(ready)tickPlayerShove(combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C)));
    captureStandingContacts();captureVehicleVelocities();captureBikeMotionSeeds();
    if(ready)tickVehicleEntryProbe();
    captureHostPredictorStarts();
    reserveOriginalGroundFrame();
    cost.lap(0);
    originalProcess();cost.lap(1);if(!ready)return;
    if(!bikeFallAudits.empty())observeNativeBikeFalls();
    tickWheelSmokeProbe();
    verifyOriginalGroundNativeEnd();
    captureMeleeFrames();captureFootSupportFrame();captureRoadFrames();cost.lap(2);
    if(!playing()||!player()||cutscene()||(protectMissions&&scriptMissionActive())||lastArea!=area()||(lastTime&&gameTime()<lastTime)){
        physicsFrame.reset();simulatePhysicsFrame();return;
    }
    processStandingContacts(combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C)));
    combatFinish();maintainRecoverySupport();cost.lap(3);
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    evaluatedPeds.clear();evaluatedPedFrame=frame;
    physicsFrame.begin(frame,gameTime());
}

extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeHit(int targetRef){
    if(!ready)return -11;if(!playing())return -12;if(cutscene())return -13;if(protectMissions&&scriptMissionActive())return -14;
    void* target=pedFromRef(targetRef);if(!validPed(target))return -15;
    pending[targetRef]={{0,28,2},{0,1,0},gameTime(),10,true};return 0;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeNearest(int scripted){
    if(!ready||!player())return -1;auto*pool=pedPool();if(!pool)return -2;
    float best=radius+1;int found=-1;
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        void*p=pool->objects+i*0x7C4;if(!validPed(p))continue;
        bool fromScript=at<unsigned char>(p,0x484)!=1;if(scripted>=0&&fromScript!=(scripted!=0))continue;
        float d=length(position(p)-position(player()));if(d<best){best=d;found=(i<<8)|pool->flags[i];}
    }
    if(found<0)return -3;int result=DeliriumSA_ProbeHit(found);return result==0?found:result;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeGameDamage(int targetRef,int lethal){
    if(!ready||!playing()||cutscene()||scriptMissionActive())return -1;
    void* p=pedFromRef(targetRef);if(!validPed(p)||at<unsigned char>(p,0x484)!=1)return -2;
    const float health=at<float>(p,0x540);
    // CWeapon::GenerateDamageEvent is the game's normal bullet/event path. A null
    // creator is also used by stock FireOneInstantHitRound; the checkpoint isn't saved.
    reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,nullptr,22,lethal?500:12,3,0);
    event("probe_native_damage",targetRef,at<short>(p,0x22),"health_before="+std::to_string(health)+" lethal="+std::to_string(lethal!=0));
    return targetRef;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeSelectRandom(){
    if(!ready||!playing()||!player()||cutscene()||scriptMissionActive())return -1;
    auto*pool=pedPool();if(!pool)return -2;float best=radius+1;int found=-3;
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80)){
        void*p=pool->objects+i*0x7C4;if(!validPed(p)||at<unsigned char>(p,0x484)!=1)continue;
        const float d=length(position(p)-position(player()));if(d>3&&d<best&&!agents.count(reference(p))){best=d;found=(i<<8)|pool->flags[i];}
    }return found;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeGroveStreet(int returnHome){
    if(!ready||!playing()||!player()||area()!=0||cutscene()||scriptMissionActive())return -1;
    const V here=position(player());if(here.x<2300||here.x>2600||here.y<-1800||here.y>-1550)return -2;
    V destination=returnHome?V{2495.33f,-1685.f,20.f}:V{2430.f,-1658.f,20.f};
    // Verified CStreaming::LoadSceneCollision and CWorld::FindGroundZFor3DCoord.
    reinterpret_cast<void(__cdecl*)(const V*)>(0x40ED80)(&destination);
    bool found=false;void* entity=nullptr;
    const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(destination.x,destination.y,destination.z,&found,&entity);
    if(!found||!std::isfinite(ground)||ground<0||ground>25)return -3;
    clear("probe_scene_transition");destination.z=ground+1.0f;teleport(player(),destination);at<V>(player(),0x44)=V{};
    event("probe_scene_position",-1,-1,returnHome?"grove_home":"grove_street");return 0;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeRyderMarker(){
    if(!ready||!playing()||!player()||area()!=0||cutscene()||scriptMissionActive())return -1;
    const V here=position(player());if(here.x<2300||here.x>2600||here.y<-1800||here.y>-1550)return -2;
    // CRadar.cpp ms_RadarTrace = 0xBA86F0, CRadar.h tRadarTrace size 0x28,
    // m_vecPos +8, sprite +0x24, inUse bit 1 of byte +0x25. Ryder sprite = 34.
    for(int i=0;i<175;i++){
        auto* blip=reinterpret_cast<unsigned char*>(0xBA86F0+i*0x28);
        if(blip[0x24]!=34||!(blip[0x25]&2)||((blip[0x26]>>2)&15)!=5)continue;
        V target=at<V>(blip,8);
        if(!finite(target)||target.x<2400||target.x>2500||target.y<-1750||target.y>-1600||target.z<10||target.z>20)return -3;
        // Enter the existing script marker. Do not change SCM globals, force a
        // mission, skip a cutscene or save. Let the original story script run.
        target.z+=1.f;teleport(player(),target);at<V>(player(),0x44)=V{};
        event("probe_story_marker",-1,-1,"existing_Ryder_contact_blip");return i;
    }return -4;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeVehicleDrop(){
    const int ref=DeliriumSA_ProbeSelectRandom();if(ref<0)return ref;
    void*ped=pedFromRef(ref);std::array<void*,128> found{};short count=0;
    const V center=position(player());
    reinterpret_cast<void(__cdecl*)(const V&,float,bool,short*,short,void**,bool,bool,bool,bool,bool)>(0x564A20)(center,35.f,true,&count,128,found.data(),false,true,false,false,false);
    void* car=nullptr;float best=36;V target{};float roof=0;
    for(int i=0;i<count;i++){
        void* e=found[i];auto*m=matrix(e);if(!m||at<void*>(e,0x460)||length(at<V>(e,0x44))>.02f||at<unsigned char>(e,0x2F)!=0)continue;
        auto* col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(e);if(!col)continue;
        const V half=(col->box.max-col->box.min)*.5f;
        if(half.x<.5f||half.x>2||half.y<1||half.y>4||half.z<.4f||half.z>2)continue;
        const float d=length(position(e)-center);if(d<5||d>=best)continue;
        best=d;car=e;target=point(*m,{(col->box.min.x+col->box.max.x)*.5f,(col->box.min.y+col->box.max.y)*.5f,col->box.max.z});roof=target.z;
    }
    if(!car)return -20;
    target.z+=1.4f;teleport(ped,target);at<V>(ped,0x44)=V{};
    pending[ref]={{0,0,0},{0,1,0},gameTime(),10,true};
    std::ofstream f(folder/"vehicle-probe.json");f<<"{\"pid\":"<<GetCurrentProcessId()<<",\"ped_ref\":"<<ref<<",\"vehicle_model\":"<<at<short>(car,0x22)<<",\"roof_z\":"<<roof<<",\"position\":["<<target.x<<','<<target.y<<','<<target.z<<"]}";
    event("probe_vehicle_drop",ref,at<short>(ped,0x22),"vehicle_model="+std::to_string(at<short>(car,0x22))+" roof_z="+std::to_string(roof));return ref;
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeFocus(int ref){
    auto*p=pedFromRef(ref);if(!validPed(p,true,true)||scriptMissionActive()||cutscene())return -1;
    V look=position(p)+V{0,0,.2f},source=look+V{-3,-4,1.4f},up{};
    if(auto a=agents.find(ref);a!=agents.end()){look=a->second->current[10].p;source=look+V{-3,-4,1.4f};}
    if(ref==roadProbePed&&roadProbeCamera(look,source)){
        // Explicit test camera keeps the moving car and body visible from the side.
    }else if(auto a=agents.find(ref);a!=agents.end()&&a->second->vehicle>=0){
        if(const auto* v=collision->findVehicle(a->second->vehicle))source=look+normalized(look-v->frame.p)*4.f+V{-1,0,1.2f};
    }
    reinterpret_cast<void(__thiscall*)(void*,const V*,const V*)>(0x50BEC0)(reinterpret_cast<void*>(0xB6F028),&source,&up);
    reinterpret_cast<void(__thiscall*)(void*,const V*,short,int)>(0x50C8B0)(reinterpret_cast<void*>(0xB6F028),&look,2,1);
    event("probe_camera_focus",ref,at<short>(p,0x22));return 0;
}
#include "VehicleProbe.hpp"
#include "PlayerProbe.hpp"
#include "StreamingProbe.hpp"
#include "PeerBodyProbe.hpp"
#include "SolidBodyProbe.hpp"
#include "BikeCrashProbe.hpp"
#include "StaticMeshProbe.hpp"
#include "ExpansionProbe.hpp"
#include "WheelSmokeProbe.hpp"
#include "RiderJackProbe.hpp"
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeVehicleReaction(int mode){
    if(mode==45&&ready&&playing())return recoveryAuditFixture(mode);
    if(mode>=50&&mode<=61)return streamingRoute(mode);
    if(!ready||!playing()||!player()||area()!=0||cutscene()||scriptMissionActive())return -1;
    if(mode>=256&&mode<=260)return armedCombatFixture(mode);
    if(mode>=240&&mode<=244)return wheelSmokeFixture(mode);
    if(mode>=245&&mode<=248)return riderJackFixture(mode);
    if(mode>=70&&mode<=77)return playerFixture(mode);
    if((mode>=190&&mode<=199)||mode==210)return physicalPeerFixture(mode);
    if(mode>=220&&mode<=227)return solidBodyFixture(mode);
    if(mode==232||mode==233)return staticMeshFixture(mode);
    if(mode>=200&&mode<=209)return bikeCrashFixture(mode);
    if((mode>=249&&mode<=255)||(mode>=212&&mode<=215)||mode==228||mode==229||mode==230||mode==231||mode==234||mode==235||mode==237||mode==238||mode==239||(mode>=80&&mode<=98)||(mode>=100&&mode<=114)||(mode>=120&&mode<=129)||(mode>=131&&mode<=143)||(mode>=150&&mode<=188))return expansionFixture(mode);
    if(mode==20||mode==21||mode==30||mode==31||mode==32)return bikeHitFixture(mode);
    if(mode>=40&&mode<=44)return recoveryFixture(mode);
    if(mode==46)return recoveryAuditFixture(mode);
    if(mode<0||mode>8)return -2;
    if(mode>=4&&mode<=6)return bikeFixture(mode);
    static unsigned fixtures=0;if(fixtures>=8)return -36;
    const int carRef=carFixture();collision->update(position(player()));
    const CollisionWorld::Vehicle* car=collision->findVehicle(carRef);
    if(!car)return -20;
    // Door fixture: keep the torso outside the body box, within normal arm
    // reach. The prior 0.65 m offset could only prove a reaching gesture.
    const V local=mode==1?V{-car->half.x-.45f,0,0}:V{0,car->half.y+.65f,0};
    V dest=mul(car->frame,T{{},local}).p;dest.z+=4;
    bool found=false;void* support=nullptr;
    float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(dest.x,dest.y,dest.z,&found,&support);
    if(!found)return -21;dest.z=ground+1;
    // Create a fresh ordinary civilian through CPopulation, rather than moving
    // an ambient NPC whose existing Urbanize task may keep sitting or fleeing.
    // SDK and gta-reversed Population.cpp verify this signature and world add.
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    void* ped=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,dest,false);
    if(!ped)return -37;fixtures++;const int ref=reference(ped);
    at<V>(ped,0x44)=V{};
    V toward=normalized(car->frame.p-dest);toward.z=0;
    const float heading=std::atan2(-toward.x,toward.y);
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(ped,heading);
    at<float>(ped,0x558)=heading;at<float>(ped,0x55C)=heading;
    if(mode==7||mode==8){
        // Actual low/high speed native vehicle motion. No injected ped damage
        // or reaction request: the production scan/collision must detect it.
        DeliriumSA_ProbeFocus(ref);
        at<V>(car->entity,0x44)=matrix(car->entity)->up*(mode==7?.06f:.20f);
    }else if(mode==2){
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(ped,car->entity,49,12,7,0);
    }else{
        pending[ref]={mode==3?V{0,160,70}:V{},toward,gameTime(),10,true,mode==3?Reaction::BikeFall:Reaction::Brace,car->ref};
    }
    event("probe_vehicle_reaction",ref,at<short>(ped,0x22),"mode="+std::to_string(mode)+" car="+std::to_string(car->ref));return ref;
}
extern "C" __declspec(dllexport) void __cdecl DeliriumSA_ProbeRestoreCamera(){
    reinterpret_cast<void(__thiscall*)(void*)>(0x50BAB0)(reinterpret_cast<void*>(0xB6F028));
}
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeFrame(){
    static const bool suppressReadback=[](){
        char owned[2]{},skip[2]{};
        return GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",owned,sizeof(owned))==1&&owned[0]=='1'&&
            GetEnvironmentVariableA("GTASA_PERFORMANCE_NO_READBACK",skip,sizeof(skip))==1&&skip[0]=='1';
    }();
    if(suppressReadback){
        static bool reported=false;if(!reported){reported=true;event("performance_readback_suppressed",-1,-1,"owned_test_only=1 no_D3D_readback=1");}
        return -53; // Explicit unavailable diagnostic; never a captured image.
    }
    auto*dev=reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x7F9D50)();if(!dev)return -1;
    IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*surface=nullptr;
    if(FAILED(dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&target))||!target)return -2;
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);HRESULT hr=S_OK;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
        hr=dev->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr);
        if(SUCCEEDED(hr))hr=dev->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE);
    }
    if(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8)hr=D3DERR_INVALIDCALL;
    if(SUCCEEDED(hr))hr=dev->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&surface,nullptr);
    if(SUCCEEDED(hr))hr=dev->GetRenderTargetData(resolved?resolved:target,surface);
    int result=-3;D3DLOCKED_RECT lock{};
    if(SUCCEEDED(hr)&&SUCCEEDED(surface->LockRect(&lock,nullptr,D3DLOCK_READONLY))){
        auto path=folder/("frame-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(gameTime())+".bmp");
        BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
        file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+desc.Width*desc.Height*4;
        info.biSize=sizeof(info);info.biWidth=desc.Width;info.biHeight=-int(desc.Height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(&file),sizeof(file));f.write(reinterpret_cast<char*>(&info),sizeof(info));
        for(unsigned y=0;y<desc.Height;y++)f.write(static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*4);f.close();surface->UnlockRect();
        event("probe_frame",-1,-1,path.filename().string());result=0;
    }
    if(surface)surface->Release();if(resolved)resolved->Release();target->Release();return result;
}
static DWORD WINAPI setup(void*){
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);folder=fs::path(exe).parent_path()/L"DeliriumSA";fs::create_directories(folder);
    sessionlog::init(folder.parent_path());
    auto*base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));auto*dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto*nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->OptionalHeader.SizeOfImage!=0x1177000){status="unsupported_executable";event(status);return 0;}
    auto ini=fs::path(exe).parent_path()/L"DeliriumSA.ini";
    if(!GetPrivateProfileIntW(L"DeliriumSA",L"Enabled",1,ini.c_str()))return 0;
    applyBones=GetPrivateProfileIntW(L"DeliriumSA",L"ApplyToBones",0,ini.c_str())!=0;
    detectImpacts=GetPrivateProfileIntW(L"DeliriumSA",L"DetectImpacts",1,ini.c_str())!=0;
    protectMissions=GetPrivateProfileIntW(L"DeliriumSA",L"ProtectMissions",1,ini.c_str())!=0;
    allowScripted=GetPrivateProfileIntW(L"DeliriumSA",L"AllowScriptedAmbientPeds",1,ini.c_str())!=0;
    hitRagdollMotorcycles=GetPrivateProfileIntW(L"VehicleReactions",L"HitRagdollMotorcycles",1,ini.c_str())!=0;
    hitRagdollBicycles=GetPrivateProfileIntW(L"VehicleReactions",L"HitRagdollBicycles",1,ini.c_str())!=0;
    playerVehicleImpacts=GetPrivateProfileIntW(L"Player",L"VehicleImpacts",1,ini.c_str())!=0;
    playerBikeCrashEjections=GetPrivateProfileIntW(L"Player",L"BikeCrashEjections",1,ini.c_str())!=0;
    playerExplosions=GetPrivateProfileIntW(L"Player",L"Explosions",1,ini.c_str())!=0;
    physicalVehicleExits=GetPrivateProfileIntW(L"VehicleReactions",L"MovingExits",1,ini.c_str())!=0;
    aimWheelZoom=GetPrivateProfileIntW(L"Camera",L"AimWheelZoom",1,ini.c_str())!=0;
    footSupportEnabled=GetPrivateProfileIntW(L"FootSupport",L"Enabled",1,ini.c_str())!=0;
    exitThresholdKmh=float(std::clamp(int(GetPrivateProfileIntW(L"VehicleReactions",L"ExitThresholdKmh",15,ini.c_str())),5,40));
    maxActive=std::clamp(int(GetPrivateProfileIntW(L"DeliriumSA",L"MaxActive",4,ini.c_str())),1,8);
    radius=float(std::clamp(int(GetPrivateProfileIntW(L"DeliriumSA",L"Radius",40,ini.c_str())),10,40));
    char value[80];GetPrivateProfileStringA("DeliriumSA","ActiveSeconds","3",value,80,ini.string().c_str());activeSeconds=std::clamp(float(std::atof(value)),1.f,10.f);
    GetPrivateProfileStringA("FootSupport","MaxLift","0.4",value,80,ini.string().c_str());footSupportMaxLift=std::clamp(float(std::atof(value)),.1f,.4f);
    GetPrivateProfileStringA("DeliriumSA","BlendSeconds","0.6",value,80,ini.string().c_str());blendSeconds=std::clamp(float(std::atof(value)),.2f,2.f);
    GetPrivateProfileStringA("Recovery","StableSeconds","0.35",value,80,ini.string().c_str());recoveryStableSeconds=std::clamp(float(std::atof(value)),.15f,1.5f);
    GetPrivateProfileStringA("Recovery","MaxSpeed","0.65",value,80,ini.string().c_str());recoveryMaxSpeed=std::clamp(float(std::atof(value)),.1f,1.5f);
    GetPrivateProfileStringA("Recovery","MaxSpin","2.0",value,80,ini.string().c_str());recoveryMaxSpin=std::clamp(float(std::atof(value)),.5f,5.f);
    if(nm_version()!=100||!ok(nm_init((folder/"fred.xrt").string().c_str()))||!ok(nm_load_task_params((folder/"taskparams.txt").string().c_str()))){status="real_asset_initialization_failed";event(status);return 0;}
    sourceBuild=nm_build_load((folder/"fred-build.txt").string().c_str());if(!sourceBuild||nm_build_part_count(sourceBuild)!=21){status="invalid_real_character_build";event(status);return 0;}
    collision=std::make_unique<CollisionWorld>();
    if(GetEnvironmentVariableA("GTASA_WORLD_SNAPSHOT_AUDIT",nullptr,0))
        collision->debugMesh=folder/"collision-mesh.bin";
    initializeCombat();
    auto hook=[](void* target,void* replacement,void** original){return MH_CreateHook(target,replacement,original)==MH_OK;};
    auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){status="hook_init_failed";event(status);return 0;}
    // CGame.cpp addrof(CGame::Process) = 0x53BEE0; 0x53BFA0 is inside its body, not an entry point.
    if(!hook(reinterpret_cast<void*>(0x53BEE0),&process,reinterpret_cast<void**>(&originalProcess))||!hook(reinterpret_cast<void*>(0x5E8A20),&preRender,reinterpret_cast<void**>(&originalPreRender))||!hook(reinterpret_cast<void*>(0x4B3A20),&damage,reinterpret_cast<void**>(&originalDamage))){status="hook_creation_failed";event(status);return 0;}
    if(!hook(reinterpret_cast<void*>(0x4AFF60),&bikeOut,reinterpret_cast<void**>(&originalBikeOut))||!hook(reinterpret_cast<void*>(0x4B4AC0),&bikeSafePosition,reinterpret_cast<void**>(&originalBikeSafePosition))||!hook(reinterpret_cast<void*>(0x4B0020),&bikeForcesAndAnims,reinterpret_cast<void**>(&originalBikeForcesAndAnims))||!hook(reinterpret_cast<void*>(0x67FA80),&getupProcess,reinterpret_cast<void**>(&originalGetupProcess))){status="vehicle_recovery_hook_failed";event(status);return 0;}
    MH_QueueEnableHook(reinterpret_cast<void*>(0x4AFF60));MH_QueueEnableHook(reinterpret_cast<void*>(0x4B4AC0));MH_QueueEnableHook(reinterpret_cast<void*>(0x4B0020));MH_QueueEnableHook(reinterpret_cast<void*>(0x67FA80));
    if(!hook(reinterpret_cast<void*>(0x681C10),&manageTasks,reinterpret_cast<void**>(&originalManageTasks))){status="recovery_ownership_hook_failed";event(status);return 0;}
    MH_QueueEnableHook(reinterpret_cast<void*>(0x681C10));
    if(!hook(reinterpret_cast<void*>(0x677FE0),&getupAbort,reinterpret_cast<void**>(&originalGetupAbort))){status="recovery_abort_hook_failed";event(status);return 0;}
    MH_QueueEnableHook(reinterpret_cast<void*>(0x677FE0));
    if(!hook(reinterpret_cast<void*>(0x543490),&collisionRecord,reinterpret_cast<void**>(&originalCollisionRecord))){status="vehicle_contact_hook_failed";event(status);return 0;}
    MH_QueueEnableHook(reinterpret_cast<void*>(0x543490));
    struct NewHook {unsigned address;void*replacement;void**original;};
    const NewHook newHooks[]={
        {0x5435C0,reinterpret_cast<void*>(&bikeCollisionOne),reinterpret_cast<void**>(&originalBikeCollisionOne)},
        {0x543890,reinterpret_cast<void*>(&bikeSoftCollisionOne),reinterpret_cast<void**>(&originalBikeSoftCollisionOne)},
        {0x548680,reinterpret_cast<void*>(&bikeCollisionTwo),reinterpret_cast<void**>(&originalBikeCollisionTwo)},
        {0x544D50,reinterpret_cast<void*>(&bikeCollisionAlt),reinterpret_cast<void**>(&originalBikeCollisionAlt)},
        {0x6B5A10,reinterpret_cast<void*>(&bikeDamageKnockOff),reinterpret_cast<void**>(&originalBikeDamageKnockOff)},
        {0x542B50,reinterpret_cast<void*>(&roadApplyForce),reinterpret_cast<void**>(&originalRoadApplyForce)},
        {0x6AFB10,reinterpret_cast<void*>(&processSuspension),reinterpret_cast<void**>(&originalSuspension)},
        {0x543D60,reinterpret_cast<void*>(&springAlt),reinterpret_cast<void**>(&originalSpringAlt)},
        {0x543E90,reinterpret_cast<void*>(&springDamping),reinterpret_cast<void**>(&originalSpringDamping)},
        // Pair-hook comparison: temporarily withheld until startup compatibility
        // with the installed physics/trainer hooks has been isolated.
        {0x5F0360,reinterpret_cast<void*>(&carPedResponse),reinterpret_cast<void**>(&originalCarPedResponse)},
        {0x4B5AC0,reinterpret_cast<void*>(&calculateDamage),reinterpret_cast<void**>(&originalCalculateDamage)},
        {0x56BA00,reinterpret_cast<void*>(&lineOfSight),reinterpret_cast<void**>(&originalLineOfSight)},
        {0x629920,reinterpret_cast<void*>(&fightProcess),reinterpret_cast<void**>(&originalFightProcess)},
        {0x5E2530,reinterpret_cast<void*>(&standingEntityCollision),reinterpret_cast<void**>(&originalStandingEntityCollision)},
        {0x73A530,reinterpret_cast<void*>(&combatGenerateDamage),reinterpret_cast<void**>(&originalCombatGenerateDamage)},
        {0x4B3FC0,reinterpret_cast<void*>(&combatDamageAnim),reinterpret_cast<void**>(&originalCombatDamageAnim)},
        {0x4B3A60,reinterpret_cast<void*>(&combatDeathAnim),reinterpret_cast<void**>(&originalCombatDeathAnim)},
        {0x4C0170,reinterpret_cast<void*>(&physicalDamageResponse),reinterpret_cast<void**>(&originalPhysicalDamageResponse)},
        {0x6302D0,reinterpret_cast<void*>(&physicalDieFirst),reinterpret_cast<void**>(&originalPhysicalDieFirst)},
        {0x540770,reinterpret_cast<void*>(&combatJumpJustDown),reinterpret_cast<void**>(&originalCombatJumpJustDown)},
        {0x5400D0,reinterpret_cast<void*>(&combatExitVehicle),reinterpret_cast<void**>(&originalCombatExitVehicle)},
        {0x540120,reinterpret_cast<void*>(&combatExitVehicleJustDown),reinterpret_cast<void**>(&originalCombatExitVehicleJustDown)},
        {0x5E1B10,reinterpret_cast<void*>(&combatUpdatePosition),reinterpret_cast<void**>(&originalCombatUpdatePosition)},
        {0x56B790,reinterpret_cast<void*>(&explosion),reinterpret_cast<void**>(&originalExplosion)},
        {0x64DD60,reinterpret_cast<void*>(&carJumpOut),reinterpret_cast<void**>(&originalCarJumpOut)},
        {0x53F3C0,reinterpret_cast<void*>(&mouseUpdate),reinterpret_cast<void**>(&originalMouseUpdate)},
        {0x5E4280,reinterpret_cast<void*>(&bonePosition),reinterpret_cast<void**>(&originalBonePosition)},
        {0x52B730,reinterpret_cast<void*>(&cameraProcess),reinterpret_cast<void**>(&originalCameraProcess)}};
    for(const auto&h:newHooks){if(!hook(reinterpret_cast<void*>(h.address),h.replacement,h.original)){status="reaction_extension_hook_failed";event(status,-1,-1,std::to_string(h.address));return 0;}MH_QueueEnableHook(reinterpret_cast<void*>(h.address));}
    MH_QueueEnableHook(reinterpret_cast<void*>(0x53BEE0));MH_QueueEnableHook(reinterpret_cast<void*>(0x5E8A20));MH_QueueEnableHook(reinterpret_cast<void*>(0x4B3A20));
    // This exe reads54A2C1 as data. Preserve that function's bytes and redirect
    // only its four verified CALL sites, before normal world processing starts.
    if(!installBikeContactCallSites()){status="bike_contact_callsite_install_failed";event(status);return 0;}
    if(!installBikeRenderPhase(&finishPhysicsFrame,&physicsFrameNeedsCompletion)){status="physics_frame_barrier_install_failed";event(status);report();return 0;}
    if(!installBikePoseSampling()){status="bike_pose_sampling_install_failed";event(status);report();return 0;}
    initializeNativePairAudit();
    if(!installOriginalPedMotor()){status="original_ped_motor_install_failed";event(status);report();return 0;}
    if(!installNativeGroundAudit()){status="native_ground_audit_install_failed";event(status);report();return 0;}
    if(!installOriginalGroundHost()){status="original_ground_host_install_failed";event(status);report();return 0;}
    if(!installExpandedSkinAdapterTest()){status="expanded_skin_test_install_failed";event(status);report();return 0;}
    if(MH_ApplyQueued()!=MH_OK){status="hook_enable_failed";event(status);return 0;}
    status="real_engine_ready";ready=true;event("initialized",-1,-1,applyBones?"bone_application_enabled":"shadow_validation_only");report();return 0;
}
extern "C" __declspec(dllexport) void InitializeASI(){static bool started=false;if(started)return;started=true;HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&InitializeASI),&pinned);if(auto t=CreateThread(nullptr,0,&setup,nullptr,0,nullptr))CloseHandle(t);}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD why,LPVOID){if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;}
