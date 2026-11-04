#pragma once
#include <set>
#include "CombatMovement.hpp"
// One native rigid owner per generation, shared by every shape and NM body.
// The native predictor has already advanced the vehicle. Commit only the
// additional solved motion, never integrate gravity/engine/suspension twice.
static std::vector<HostExternalOwner> externalHostOwners;
static std::map<uint64_t,V> externalHostDrives;
static V stagedExternalDrive(uint64_t id){const auto i=externalHostDrives.find(id);return i==externalHostDrives.end()?V{}:i->second;}
static uint64_t externalHostEpoch=0,externalCommittedEpoch=~uint64_t(0);
static unsigned externalPendingPlacementReports=0;
static uint64_t vehicleExternalId(int ref){return (1ull<<32)|uint32_t(ref);}
struct HostNativePredictorState {
    uint64_t id=0;int ref=-1,model=-1;void*entity=nullptr;void*clump=nullptr;
    bool animated=false,infinite=false;unsigned frame=0;
    T root{};V linear{},angular{},localCom{};
};
static std::map<uint64_t,HostNativePredictorState> hostNativeStarts,hostNativeEnds;
static std::map<uint64_t,HostPredictorTimeline> hostPredictors;
static unsigned hostNativeFrame=0;static float hostNativeSeconds=0;
static std::map<uint64_t,unsigned> hostDeferredReports;
static bool hostNativeStateCurrent(const HostNativePredictorState&s){
    void*p=s.animated?pedFromRef(s.ref):vehicleFromRef(s.ref);
    return p&&p==s.entity&&matrix(p)&&at<void*>(p,0x18)==s.clump&&at<short>(p,0x22)==s.model&&s.frame==*reinterpret_cast<unsigned*>(0xB7CB4C);
}
static void captureHostNativeStates(std::map<uint64_t,HostNativePredictorState>&out){
    out.clear();if(!ready||!playing()||!player())return;
    auto capture=[&](void*p,int ref,bool animated){
        if(!p||ref<0||!matrix(p)||!at<void*>(p,0x18))return;
        HostNativePredictorState s;s.id=animated?animatedExternalId(ref):vehicleExternalId(ref);s.ref=ref;s.entity=p;s.clump=at<void*>(p,0x18);
        s.model=at<short>(p,0x22);s.animated=animated;s.frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
        s.root=transform(*matrix(p));s.linear=at<V>(p,0x44)*50.f;s.angular=at<V>(p,0x50)*50.f;
        s.infinite=(at<unsigned>(p,0x40)&64)!=0;s.localCom=at<V>(p,0xA4);
        if(finite(s.root.p)&&hostPredictorRotationValid(s.root.q)&&finite(s.linear)&&finite(s.angular)&&finite(s.localCom))out[s.id]=s;
    };
    if(auto*pool=vehiclePool();pool&&pool->size>0&&pool->size<=10000)
        for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80))capture(pool->objects+i*0xA18,(i<<8)|pool->flags[i],false);
    if(auto*pool=pedPool();pool&&pool->size>0&&pool->size<=10000)
        for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&0x80))capture(pool->objects+i*0x7C4,(i<<8)|pool->flags[i],true);
}
static void captureHostPredictorStarts(){
    hostNativeFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);hostNativeSeconds=combatSimulationSeconds(*reinterpret_cast<float*>(0xB7CB5C));
    hostPredictors.clear();hostNativeEnds.clear();captureHostNativeStates(hostNativeStarts);
}
static void captureHostPredictorEnds(){
    captureHostNativeStates(hostNativeEnds);
}
static bool hostHasPredictorInterval(uint64_t id){
    const auto a=hostNativeStarts.find(id),b=hostNativeEnds.find(id);
    return hostNativeFrame==*reinterpret_cast<unsigned*>(0xB7CB4C)&&a!=hostNativeStarts.end()&&b!=hostNativeEnds.end()&&
        hostNativeStateCurrent(a->second)&&hostNativeStateCurrent(b->second)&&a->second.entity==b->second.entity&&
        a->second.clump==b->second.clump&&a->second.model==b->second.model;
}
static void reportHostIntervalDeferred(uint64_t id,int ref,int model){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(!hostDeferredReports.contains(id)||hostDeferredReports[id]!=frame){hostDeferredReports[id]=frame;
        event("external_owner_interval_deferred",ref,model,"owner="+std::to_string(id)+" source_frame="+std::to_string(hostNativeFrame)+" frame="+std::to_string(frame)+" verified_start_and_end=0 interval_geometry_omitted=1 terminal_geometry_retained=1");}
}
static bool prepareHostExternalPredictor(HostExternalOwner&o,float fraction){
    if(!hostHasPredictorInterval(o.id)){reportHostIntervalDeferred(o.id,o.ref,o.model);return false;}
    auto found=hostPredictors.find(o.id);
    if(found==hostPredictors.end()){
        const auto&start=hostNativeStarts.at(o.id);const auto&end=hostNativeEnds.at(o.id);
        const V animatedOffset=V{o.rows[0],o.rows[1],o.rows[2]}-position(o.entity);
        auto convert=[&](const HostNativePredictorState&s){
            HostPredictorSnapshot result;result.centre=s.root;result.linear=s.linear;result.angular=s.angular;
            if(o.animated){
                const V forward=rotate(s.root.q,{0,1,0});const float heading=std::atan2(-forward.x,forward.y);
                result.centre={hostPredictorWorldRotation({0,0,heading}),s.root.p+animatedOffset};result.angular={};
            }else result.centre.p=s.infinite?s.root.p:mul(s.root,T{{},s.localCom}).p;
            return result;
        };
        HostPredictorTimeline timeline;if(!timeline.begin(o.id,hostNativeFrame,hostNativeSeconds,convert(start),convert(end)))return false;
        found=hostPredictors.emplace(o.id,std::move(timeline)).first;
    }
    o.predictor=&found->second;o.fraction=fraction;
    if(!o.predictor->sampleAcceptedBase(fraction,o.acceptedBase)||!o.predictor->sample(fraction,o.staged))return false;
    auto put=[&](int i,V v){o.rows[i]=v.x;o.rows[i+1]=v.y;o.rows[i+2]=v.z;};
    put(0,o.staged.centre.p);put(3,o.staged.linear);put(6,o.staged.angular);return true;
}
struct SceneVehicleStep {T previous{},current{};int shape=-1;};
struct SceneVehiclePath {T start{},end{};int shape=-1;};
static std::map<int,ObstacleHistory> sceneVehicleHistory;
static std::map<int,SceneVehicleStep> sceneVehicleStep;
static std::map<int,SceneVehiclePath> sceneVehiclePaths;
static unsigned sceneVehicleHistoryFrame=0;static bool sceneVehicleHistoryValid=false;
static void clearExternalScene(){
    sceneVehicleHistory.clear();sceneVehicleStep.clear();sceneVehiclePaths.clear();sceneVehicleHistoryValid=false;
    externalHostOwners.clear();externalHostDrives.clear();externalCommittedEpoch=~uint64_t(0);hostNativeStarts.clear();hostNativeEnds.clear();hostPredictors.clear();hostDeferredReports.clear();
}
static void prepareExternalIntervalScene(){
    sceneVehiclePaths.clear();const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    // A vehicle born after this interval began has no measured path. Defer its
    // entire compound in this interval, preserving it in the terminal scene.
    // Reindex BEFORE brace/grab updates, so every binding uses the same list.
    std::erase_if(collision->vehicles,[&](const auto&v){
        if(hostHasPredictorInterval(vehicleExternalId(v.ref)))return false;
        reportHostIntervalDeferred(vehicleExternalId(v.ref),v.ref,at<short>(v.entity,0x22));return true;
    });
    int index=0;for(auto&v:collision->vehicles)v.index=index++;
    collision->boxes=int(collision->vehicles.size());
    const bool consecutive=sceneVehicleHistoryValid&&unsigned(frame-sceneVehicleHistoryFrame)==1;
    for(const auto&v:collision->vehicles){
        const int shape=v.shape?v.shape->id:-1,id=v.identity();T previous=v.frame;
        const auto old=sceneVehicleHistory.find(id);
        if(consecutive&&old!=sceneVehicleHistory.end()&&old->second.shape==shape)previous=old->second.frame;
        else{
            bool verified=false;
            if(v.velocityAtFrame)verified=previousBikeComponentFrame(v.entity,v.component,previous);
            else if(v.component==0)if(auto parent=beforeVehicleFrame.find(v.ref);parent!=beforeVehicleFrame.end()){
                previous=mul(parent->second,mul(inverse(transform(*matrix(v.entity))),v.frame));verified=true;
            }
            if(old!=sceneVehicleHistory.end())event("vehicle_history_shape_changed",v.ref,-1,
                "component="+std::to_string(v.component)+" history_frame="+std::to_string(sceneVehicleHistoryFrame)+" current_frame="+std::to_string(frame)+" cross_representation_ccd=0 previous_verified="+std::to_string(verified));
        }
        sceneVehiclePaths[id]={previous,v.frame,shape};
    }
    sceneVehicleStep.clear();
}
static T hostNativeFractionFrame(const HostExternalOwner&o){
    T frame=o.acceptedBase.centre;
    if(o.predictor){frame.p=frame.p-o.predictor->accumulatedTranslation();frame.q=mul(conjugate(o.predictor->accumulatedRotation()),frame.q);}
    return frame;
}
static void beginSceneVehicleStep(float fraction){
    const auto previousSteps=std::move(sceneVehicleStep);sceneVehicleStep.clear();
    for(const auto&v:collision->vehicles){
        const int id=v.identity();const auto path=sceneVehiclePaths.find(id);if(path==sceneVehiclePaths.end())continue;
        T previous=path->second.start;if(auto old=previousSteps.find(id);old!=previousSteps.end())previous=old->second.current;
        T current=advanceObstacle(path->second.start,path->second.end,1.f,fraction);
        if(const auto owner=std::find_if(externalHostOwners.begin(),externalHostOwners.end(),[&](const auto&o){return o.id==vehicleExternalId(v.ref);});owner!=externalHostOwners.end())
            current=mul(mul(owner->staged.centre,inverse(hostNativeFractionFrame(*owner))),current);
        sceneVehicleStep[id]={previous,current,path->second.shape};
    }
}
static bool bindSceneVehicleStep(){
    std::vector<float> rows;std::vector<V> spins;rows.reserve(collision->vehicles.size()*13);spins.reserve(collision->vehicles.size());
    for(const auto&v:collision->vehicles){
        const auto step=sceneVehicleStep.find(v.identity());if(step==sceneVehicleStep.end())return false;
        const auto owner=std::find_if(externalHostOwners.begin(),externalHostOwners.end(),[&](const auto&o){return o.id==vehicleExternalId(v.ref);});
        if(owner==externalHostOwners.end()||!owner->predictor)return false;
        const auto&source=hostNativeEnds.at(owner->id);const auto&o=*owner;
        const V nativeCom=source.infinite?source.root.p:mul(source.root,T{{},source.localCom}).p;
        const V endCom=nativeCom+o.predictor->accumulatedTranslation();
        const V parentLinear=source.linear+o.predictor->accumulatedLinear(),parentSpin=source.angular+o.predictor->accumulatedAngular();
        const V relative=(v.velocityAtFrame||v.component)?v.velocity-parentLinear-cross(parentSpin,v.frame.p-endCom):V{};
        const V spin=o.staged.angular+(v.spin-parentSpin);const auto&f=step->second.current;
        const V velocity=o.staged.linear+cross(o.staged.angular,f.p-o.staged.centre.p)+relative;
        rows.insert(rows.end(),{f.p.x,f.p.y,f.p.z,f.q.x,f.q.y,f.q.z,f.q.w,v.half.x,v.half.y,v.half.z,velocity.x,velocity.y,velocity.z});spins.push_back(spin);
    }
    if(!ok(nm_world_set_boxes(collision->world,rows.empty()?nullptr:rows.data(),int(collision->vehicles.size()))))return false;
    for(const auto&v:collision->vehicles){
        const auto&s=sceneVehicleStep.at(v.identity());
        if(v.shape&&v.shape->id>=0&&!ok(nm_world_set_vehicle_shape(collision->world,v.index,v.shape->id,&spins[v.index].x)))return false;
        if(!ok(nm_world_set_vehicle_previous(collision->world,v.index,&s.previous.p.x,&s.previous.q.x)))return false;
    }
    return true;
}
static void finishSceneVehicleStep(const std::vector<Active*>&actors){
    sceneVehicleHistory.clear();sceneVehicleHistoryFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);sceneVehicleHistoryValid=true;
    for(const auto&[id,s]:sceneVehicleStep)sceneVehicleHistory[id]={s.current,s.shape};
    // Existing observers read this mirror; it no longer advances a private car.
    for(auto*a:actors)a->vehicleHistory=sceneVehicleHistory;
}
static bool captureVehicleExternalOwner(void*v,HostExternalOwner&out,float fraction){
    const int ref=vehicleRef(v);if(ref<0||!matrix(v))return false;
    const float mass=at<float>(v,0x8C),turn=at<float>(v,0x90);const unsigned flags=at<unsigned>(v,0x40);
    if(!std::isfinite(mass)||mass<=0||!std::isfinite(turn)||turn<=0)return false;
    const bool noMove=(flags&32)!=0,infinite=(flags&64)!=0,noTurn=(flags&16)!=0,noZ=(flags&128)!=0;
    const bool attached=at<void*>(v,0xFC)!=nullptr;
    T ownerFrame=transform(*matrix(v));
    if(const auto previous=beforeVehicleFrame.find(ref);previous!=beforeVehicleFrame.end())
        ownerFrame=advanceObstacle(previous->second,ownerFrame,1.f,fraction);
    const V com=infinite?ownerFrame.p:mul(ownerFrame,T{{},at<V>(v,0xA4)}).p;
    const V linear=at<V>(v,0x44)*50.f,spin=at<V>(v,0x50)*50.f;
    if(!finite(com)||!finite(linear)||!finite(spin))return false;
    out={vehicleExternalId(ref),ref,v,at<void*>(v,0x18),at<short>(v,0x22),false,{}};
    auto put=[&](int at,V value){out.rows[at]=value.x;out.rows[at+1]=value.y;out.rows[at+2]=value.z;};
    put(0,com);put(3,linear);put(6,spin);
    const float invMass=(!noMove&&!infinite&&!attached)?1.f/mass:0.f;
    out.rows[9]=out.rows[13]=invMass;out.rows[17]=noZ?0.f:invMass;
    const float adjustedTurn=turn+(infinite?.5f*mass*at<V>(v,0xA4).z*at<V>(v,0xA4).z:0.f);
    const float invTurn=(!noTurn&&!attached)?1.f/adjustedTurn:0.f;
    out.rows[18]=out.rows[22]=noMove?0.f:invTurn;out.rows[26]=invTurn;
    return true;
}
static bool hostExternalCurrent(const HostExternalOwner&o){
    void*p=o.animated?pedFromRef(o.ref):vehicleFromRef(o.ref);
    return p==o.entity&&p&&matrix(p)&&at<void*>(p,0x18)==o.clump&&at<short>(p,0x22)==o.model;
}
static void collectHostExternalOwners(float fraction,bool interval=true){
    externalHostOwners.clear();
    std::map<int,void*> vehicles;
    for(const auto&v:collision->vehicles)vehicles[v.ref]=v.entity;
    for(const auto&[ref,p]:vehicles){HostExternalOwner o;if(captureVehicleExternalOwner(p,o,fraction))externalHostOwners.push_back(o);}
    appendAnimatedExternalOwners(externalHostOwners);
    if(interval)std::erase_if(externalHostOwners,[&](auto&o){return !prepareHostExternalPredictor(o,fraction);});
    else for(auto&o:externalHostOwners){
        o.fraction=1.f;o.staged.centre.p={o.rows[0],o.rows[1],o.rows[2]};
        const V forward=matrix(o.entity)->up;o.staged.centre.q=o.animated?hostPredictorWorldRotation({0,0,std::atan2(-forward.x,forward.y)}):rotation(*matrix(o.entity));
        o.staged.linear={o.rows[3],o.rows[4],o.rows[5]};o.staged.angular={o.rows[6],o.rows[7],o.rows[8]};o.acceptedBase=o.staged;
    }
}
static void prepareRecoveryContactScene(){
    if(!collision||!player())return;
    collision->update(position(player()));collision->applyVehicleBoxes();
    collectHostExternalOwners(1.f,false);
    appendAnimatedExternalObstacles(collision->world,externalHostOwners);
}
static bool stageExternalOwners(NmGroup*group,uint64_t epoch,float fraction,float dt){
    externalHostDrives.clear();
    collectHostExternalOwners(fraction);externalHostEpoch=epoch;
    std::vector<uint64_t> ids;std::vector<float> rows,predictors;
    for(const auto&o:externalHostOwners){
        ids.push_back(o.id);rows.insert(rows.end(),o.rows.begin(),o.rows.end());
        const V p=o.staged.centre.p-o.acceptedBase.centre.p;const Q q=mul(o.staged.centre.q,conjugate(o.acceptedBase.centre.q));
        predictors.insert(predictors.end(),{p.x,p.y,p.z,q.x,q.y,q.z,q.w});
    }
    if(!ok(nm_group_set_external(group,epoch,ids.data(),rows.data(),int(ids.size())))||
       !ok(nm_group_set_external_predictors(group,epoch,ids.data(),predictors.data(),int(ids.size()))))return false;
    std::vector<uint64_t> driven;std::vector<float> impulses;
    for(const auto&o:externalHostOwners){V j{};
        if(!originalPedMotorImpulse(o,dt,j)){
            errors++;event("original_ped_motor_rejected",o.ref,o.model,"claimed_owner_inconsistent=1 no_group_step=1");return false;
        }
        if(dot(j,j)==0)continue;
        externalHostDrives[o.id]=j;
        driven.push_back(o.id);impulses.insert(impulses.end(),{j.x,j.y,j.z,0,0,0});
        static std::map<uint64_t,unsigned> logged;
        if(gameTime()-logged[o.id]>=200){logged[o.id]=gameTime();event("original_ped_motor_drive",o.ref,o.model,
            "impulse_x="+std::to_string(j.x)+" impulse_y="+std::to_string(j.y)+" impulse_z="+std::to_string(j.z)+
            " before_vx="+std::to_string(o.staged.linear.x)+" before_vy="+std::to_string(o.staged.linear.y)+
            " dt="+std::to_string(dt)+" shared_substep=1 committed_by_common_result=1");}
    }
    return ok(nm_group_set_external_drives(group,epoch,driven.data(),impulses.data(),int(driven.size())))&&stageOriginalGround(group,epoch);
}
static bool bindExternalObstacles(){
    for(const auto&v:collision->vehicles){
        const uint64_t id=vehicleExternalId(v.ref);
        const auto found=std::find_if(externalHostOwners.begin(),externalHostOwners.end(),[&](const auto&o){return o.id==id&&hostExternalCurrent(o);});
        if(found==externalHostOwners.end()){
            event("external_vehicle_snapshot_rejected",v.ref,-1,"finite_owner_missing=1 island_not_advanced=1");return false;
        }
        if(!ok(nm_world_set_obstacle_owner(collision->world,v.index,id)))return false;
    }
    return appendAnimatedExternalObstacles(collision->world,externalHostOwners);
}
static bool commitExternalOwners(NmGroup*group,float dt){
    if(externalCommittedEpoch==externalHostEpoch){errors++;event("external_double_commit_rejected");return false;}
    std::array<uint64_t,256> ids{},placementIds{};
    std::array<float,256*18> rows{};std::array<float,256*12> placementRows{};
    const int count=nm_group_external_results(group,ids.data(),rows.data(),256);
    const int placements=nm_group_external_placements(group,placementIds.data(),placementRows.data(),256);
    if(!ok(count)||!ok(placements))return false;
    if(!preflightOriginalGround(group)){errors++;event("original_ground_receipt_rejected",-1,-1,"no_host_mutated=1");return false;}
    if(count!=int(externalHostOwners.size())||placements!=count){errors++;event("external_commit_preflight_rejected",-1,-1,"owner_result_count_mismatch=1 no_host_mutated=1");return false;}
    struct Commit {const HostExternalOwner*owner;const float*result;HostPredictorCorrection correction;V move;Q rotation;int contacts=0;bool fallback=false;};
    std::vector<Commit> commits;std::vector<uint64_t> seen;
    auto reject=[&](const char*reason,int ref=-1,int model=-1){
        errors++;event("external_commit_preflight_rejected",ref,model,std::string(reason)+" no_host_mutated=1");
        // Capture the measured failed admission BEFORE any owner is mutated or
        // another staging call replaces its original geometry/predictor. This
        // is an offline placement replay, not a claimed full-island replay.
        // Bound diagnostics per launch; repeated failure must not fill disk or
        // change the physical decision through repeated expensive collection.
        static std::set<std::string> captured;
        const std::string key=std::to_string(ref)+":"+std::to_string(model)+":"+reason;
        char diagnosticGate[4]{};const bool replayRequested=GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&
            GetEnvironmentVariableA("GTASA_CAPTURE_EXTERNAL_REPLAY",diagnosticGate,sizeof diagnosticGate)==1&&diagnosticGate[0]=='1';
        if(replayRequested&&sessionlog::enabled&&collision&&captured.size()<4&&captured.insert(key).second){
            const auto path=sessionlog::directory/("external-placement-"+std::to_string(externalHostEpoch)+"-"+std::to_string(ref)+".json");
            const auto utf8=path.u8string();const std::string bytes(utf8.begin(),utf8.end());
            const int saved=nm_group_dump_external_replay(group,collision->world,bytes.c_str());
            {std::ofstream f(sessionlog::directory/("external-placement-"+std::to_string(externalHostEpoch)+"-"+std::to_string(ref)+"-sources.txt"));f.precision(9);
             f<<"first count model type x y z triangles boxes spheres simple\n";
             for(const auto&m:collision->meshSources)f<<m.first<<' '<<m.count<<' '<<m.model<<' '<<m.type<<' '<<m.pos.x<<' '<<m.pos.y<<' '<<m.pos.z<<' '<<m.triangles<<' '<<m.boxes<<' '<<m.spheres<<' '<<m.simple<<'\n';}
            event("external_placement_replay",ref,model,"owners="+std::to_string(saved)+" file="+path.filename().string()+" scope=final_vehicle_admission no_host_mutated=1");
        }
        return false;
    };
    for(int i=0;i<count;i++){
        if(std::find(seen.begin(),seen.end(),ids[i])!=seen.end())return reject("duplicate_owner_result=1");seen.push_back(ids[i]);
        const auto owner=std::find_if(externalHostOwners.begin(),externalHostOwners.end(),[&](const auto&o){return o.id==ids[i];});
        if(owner==externalHostOwners.end()||!hostExternalCurrent(*owner)||!owner->predictor||!owner->predictor->matches(owner->id,hostNativeFrame))return reject("stale_owner_or_predictor=1");
        const auto&o=*owner;const float*r=rows.data()+18*i;
        if(!std::all_of(r,r+18,[](float x){return std::isfinite(x);}))return reject("nonfinite_impulse_result=1",o.ref,o.model);
        const auto placement=std::find(placementIds.begin(),placementIds.begin()+placements,o.id);
        if(placement==placementIds.begin()+placements||std::count(placementIds.begin(),placementIds.begin()+placements,o.id)!=1)return reject("placement_identity_mismatch=1",o.ref,o.model);
        const float*p=placementRows.data()+12*std::distance(placementIds.begin(),placement);
        if(!std::all_of(p,p+12,[](float x){return std::isfinite(x);}))return reject("nonfinite_placement=1",o.ref,o.model);
        Commit c{&o,r};
        if(!o.predictor->propose(o.fraction,dt,{r[6],r[7],r[8]},{r[9],r[10],r[11]},{r[12],r[13],r[14]},{r[15],r[16],r[17]},c.correction))return reject("predictor_interval_mismatch=1",o.ref,o.model);
        if(p[7]==1.f){
            c.move={p[0],p[1],p[2]};c.rotation={p[3],p[4],p[5],p[6]};
            if(!hostPredictorRotationValid(c.rotation)||p[8]<0||p[8]>1000000||std::floor(p[8])!=p[8])return reject("invalid_placement_rotation_or_contacts=1",o.ref,o.model);
            c.contacts=int(p[8]);
            // Vehicle world position rows have already changed split18. Its
            // admitted total must match that final proposal; metadata9..11 is
            // intentionally the pre-world request. Animated owners instead
            // retain native support and the subsequent sweep clips the FINAL
            // coupled solution. Their metadata9..11 is that sweep's actual
            // input, not the earlier pre-world position preview.
            const V contract=o.animated?V{p[9],p[10],p[11]}:c.move;
            if(length(contract-c.correction.translation)>.001f){
                if(GetEnvironmentVariableA("GTASA_CAPTURE_EXTERNAL_REPLAY",nullptr,0)){
                    std::ostringstream audit;audit.precision(9);
                    auto vector=[&](const char*name,V v){audit<<name<<'='<<v.x<<','<<v.y<<','<<v.z<<' ';};
                    vector("admitted",contract);vector("host_total",c.correction.translation);
                    vector("new_dv",{r[6],r[7],r[8]});vector("split",{r[12],r[13],r[14]});
                    vector("prior_dv",o.predictor->accumulatedLinear());vector("pending",o.staged.centre.p-o.acceptedBase.centre.p);
                    audit<<"dt="<<dt<<" fraction="<<o.fraction;
                    event("external_contract_audit",o.ref,o.model,audit.str());
                }
                return reject("placement_total_displacement_contract_mismatch=1",o.ref,o.model);
            }
        }else if(p[7]==0.f&&o.animated&&!originalGroundActive(o.entity)){
            // Transitional source limitation: original animated support has
            // not yet granted world solver rows. Preserve the existing pure
            // translational sweep, from acceptedBase, with the TOTAL pending
            // correction. Invalid vehicle placement never gets this fallback.
            const V forward=rotate(o.acceptedBase.centre.q,{0,1,0});const float heading=std::atan2(-forward.x,forward.y);
            c.contacts=nm_world_sweep_animated_owner(collision->world,o.id,&o.acceptedBase.centre.p.x,heading,&c.correction.translation.x,&c.move.x);
            if(c.contacts<0||!finite(c.move))return reject("animated_fallback_sweep_rejected=1",o.ref,o.model);
            c.rotation={};c.fallback=true;
        }else return reject("placement_unverified=1",o.ref,o.model);
        // Original animated locomotion locks angular DOFs. Do not silently
        // discard a solver rotation, or rotate only its root without its skin.
        if(o.animated&&(length(c.correction.deltaAngular)>1.e-5f||std::abs(c.rotation.w)<1.f-1.e-6f))return reject("animated_rotation_lock_broken=1",o.ref,o.model);
        if(!finite(o.predictor->accumulatedTranslation()+c.move)||!finite(o.predictor->accumulatedLinear()+c.correction.deltaLinear)||!finite(o.predictor->accumulatedAngular()+c.correction.deltaAngular))return reject("accumulated_result_overflow=1",o.ref,o.model);
        if(!preflightOriginalGroundMotion(o,c.correction.translation,c.move))return reject("source_ground_total_path_not_covered=1",o.ref,o.model);
        commits.push_back(c);
    }
    // Complete preflight above is immutable. Distinct owner IDs have distinct
    // timelines; no later failed receipt can leave only half the host moved.
    externalCommittedEpoch=externalHostEpoch;
    for(const auto&c:commits){
        const auto&o=*c.owner;const float*r=c.result;
        if(!o.predictor->commit(c.correction,c.move,c.rotation)){errors++;event("external_commit_invariant_failure",o.ref,o.model);return false;}
        if(o.animated){commitAnimatedExternalOwner(o,r,c.correction,c.move,c.contacts,c.fallback,dt);continue;}
        void*v=o.entity;const V impulse{r[0],r[1],r[2]},torque{r[3],r[4],r[5]},dv=c.correction.deltaLinear,dw=c.correction.deltaAngular;
        const auto&native=hostNativeEnds.at(o.id);
        const V oldCom=native.infinite?position(v):point(*matrix(v),at<V>(v,0xA4));
        const T oldRoot=transform(*matrix(v));
        const T placedRoot{mul(c.rotation,oldRoot.q),oldCom+c.move+rotate(c.rotation,oldRoot.p-oldCom)};
        if(length(c.move)>0||std::abs(c.rotation.w)<1.f-1.e-7f){
            write(*matrix(v),placedRoot);
            reinterpret_cast<void(__thiscall*)(void*)>(0x542560)(v); // CPhysical::RemoveAndAdd; no task teleport.
            reinterpret_cast<void(__thiscall*)(void*)>(0x446F90)(v);
            reinterpret_cast<void(__thiscall*)(void*)>(0x532B00)(v);
        }
        at<V>(v,0x44)=at<V>(v,0x44)+dv*.02f;at<V>(v,0x50)=at<V>(v,0x50)+dw*.02f;
        if(dot(impulse,impulse)+dot(torque,torque)>1.e-10f)at<unsigned char>(v,0xB8)=0;
        recordRoadHull(o.ref,impulse,torque);
        // Bring both current-substep geometry and native-end targets to their
        // actual accepted pose. No raw split and no stale kinematic copy.
        const T accepted{mul(c.rotation,o.acceptedBase.centre.q),o.acceptedBase.centre.p+c.move};
        const T stagedToAccepted=mul(accepted,inverse(o.staged.centre));
        const T hostCorrection=mul(placedRoot,inverse(oldRoot));
        for(auto&part:collision->vehicles)if(part.ref==o.ref){
            const V oldPoint=part.frame.p;part.frame=mul(hostCorrection,part.frame);
            if(part.velocityAtFrame||part.component){
                const V relative=part.velocity-(at<V>(v,0x44)*50.f-dv)-cross(at<V>(v,0x50)*50.f-dw,oldPoint-oldCom);
                part.velocity=at<V>(v,0x44)*50.f+cross(at<V>(v,0x50)*50.f,part.frame.p-(oldCom+c.move))+rotate(c.rotation,relative);
                part.spin=at<V>(v,0x50)*50.f+rotate(c.rotation,part.spin-(at<V>(v,0x50)*50.f-dw));
            }else{part.velocity=part.velocity+dv;part.spin=part.spin+dw;}
            if(auto step=sceneVehicleStep.find(part.identity());step!=sceneVehicleStep.end())step->second.current=mul(stagedToAccepted,step->second.current);
        }
        if(length(c.move)>1.e-5f||length(impulse)>1.e-3f)event("external_vehicle_committed",o.ref,o.model,
            "fraction="+std::to_string(o.fraction)+" requested_m="+std::to_string(length(c.correction.translation))+" moved_m="+std::to_string(length(c.move))+" world_contacts="+std::to_string(c.contacts)+" total_from_accepted_base=1 accumulated_velocity_integrated=1");
    }
    return true;
}
