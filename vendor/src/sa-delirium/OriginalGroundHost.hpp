#pragma once
#include "NativeGroundAuthority.hpp"
#include <cstdlib>
#include "../plugin-sdk-sa/plugin_sa/game_sa/enums/eTaskType.h"
// Opt-in authority bridge, not a post-physics height repair. A complete native
// interval is reserved before ProcessControl and consumed by the shared island.
// Unknown geometry or a state change after a suppressed branch aborts ONLY the
// explicitly requested diagnostic process: no partial rollback is advertised.
struct OriginalGroundLease {
    native_ground_authority::Lease lease;
    native_ground_authority::Snapshot before;
    std::map<void*,native_ground_authority::Identity> peers;
    unsigned lastCommit=~0u;bool complete=false;int uncoveredModel=-1,uncoveredType=-1;
};
struct OriginalGroundReceipt {uint64_t epoch=0,support=0,member=~uint64_t(0);std::array<float,19> row{};};
static bool originalGroundEnabled=false;
static int originalGroundOnlyRef=-1;
static int originalGroundFixtureRef=-1;
static bool originalGroundFixtureStop=false;
static std::map<uint64_t,OriginalGroundLease> originalGroundLeases;
static std::map<uint64_t,OriginalGroundReceipt> originalGroundReceipts;
static std::map<uint64_t,unsigned> originalGroundLastClaim;
static std::map<uint64_t,float> originalGroundPreviousPlane;
static std::map<void*,native_ground_authority::Identity> originalGroundGroupPeers;
static uint64_t originalGroundGroupEpoch=~uint64_t(0);
static unsigned originalGroundFrame=~0u;
static native_ground_authority::Identity originalGroundIdentity(void*p){
    return {reinterpret_cast<uintptr_t>(p),reinterpret_cast<uintptr_t>(p?at<void*>(p,0x18):nullptr),
        reinterpret_cast<uintptr_t>(p?matrix(p):nullptr),p?reference(p):-1,p?at<short>(p,0x22):-1,*reinterpret_cast<unsigned*>(0xB7CB4C)};
}
static bool originalGroundOrdinaryTask(void*p){
    auto*m=taskManager(p);if(!m)return false;
    auto*t=reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m);
    for(int depth=0;t&&depth<16;depth++){
        auto*v=at<void**>(t,0);const int id=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t);
        auto*sub=reinterpret_cast<void*(__thiscall*)(void*)>(v[2])(t);
        if(!sub)return id==TASK_SIMPLE_PLAYER_ON_FOOT||id==TASK_SIMPLE_GO_TO_POINT||
            id==TASK_SIMPLE_GO_TO_POINT_FINE||id==TASK_SIMPLE_STAND_STILL;
        t=sub;
    }return false;
}
static native_ground_authority::Facts originalGroundFacts(void*p){
    native_ground_authority::Facts f{};if(!p||reference(p)<0||pedFromRef(reference(p))!=p)return f;
    f.identity=originalGroundIdentity(p);f.testGate=originalGroundEnabled&&recoveryAllowed()&&reference(p)==originalGroundFixtureRef;
    f.ordinaryOnFoot=originalGroundOrdinaryTask(p);f.grounded=(at<unsigned>(p,0x46C)&1)!=0;
    f.alive=at<float>(p,0x540)>0;f.nativePairReady=animatedNativePairCurrent(p);
    f.sourceReady=sourceBuild&&originalPedMotorEnabled&&collision&&collision->world;
    f.inVehicle=inVehicle(p);f.attached=at<void*>(p,0xFC)!=nullptr;f.submerged=(at<unsigned>(p,0x40)&256)!=0;
    const auto a=agents.find(reference(p));f.naturalMotion=a!=agents.end();f.recovering=a!=agents.end()&&a->second->recovering;
    // Positive task admission is deliberate; jumping/climbing/swimming and
    // get-up retain their SA task branches without guessed enum substitutions.
    // bIsInTheAir alone is not a jump task (curbs also set it). The positive
    // task chain above excludes authored jump/fall/climb/swim transitions.
    return f;
}
[[noreturn]] static void abortOriginalGround(const char*reason,int ref=-1){
    status="original_ground_interval_aborted";errors++;
    event(status,ref,-1,std::string("reason=")+reason+" diagnostic_only=1 no_half_native_fallback=1 exit_code=3758100481");
    // The opt-in is required both for installation and reservation. If this
    // invariant is broken, still do not resume a half-simulated interval.
    TerminateProcess(GetCurrentProcess(),0xE0001001u);std::abort();
}
static bool originalGroundActive(void*p){
    if(!originalGroundEnabled||!p)return false;
    const auto i=originalGroundLeases.find(animatedExternalId(reference(p)));
    return i!=originalGroundLeases.end()&&i->second.lease.reserved&&!i->second.complete&&
        native_ground_authority::same(i->second.lease.identity,originalGroundIdentity(p));
}
static bool originalGroundSupported(void*p){
    if(!originalGroundActive(p))return false;
    const auto&r=originalGroundLeases.at(animatedExternalId(reference(p)));return r.lease.initialGrounded;
}
static bool originalGroundPeer(void*p,native_ground_authority::Identity&identity){
    if(!p||reference(p)<0||pedFromRef(reference(p))!=p||!matrix(p))return false;
    if(animatedNativePairCurrent(p)&&animatedExternalEligible(p)){identity=originalGroundIdentity(p);return true;}
    const auto a=agents.find(reference(p));if(a==agents.end()||a->second->ped!=p||a->second->mounted||a->second->preview||a->second->taskAborted)return false;
    identity=originalGroundIdentity(p);return true;
}
static float originalGroundBodyRadius(void*p){
    auto*cache=bodyCollider(p);auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(p);auto*m=matrix(p);
    if(!cache||!cache->rig||!col||!m||!finite(col->sphere)||!std::isfinite(col->radius)||col->radius<=0)return INFINITY;
    const float rootShift=1.f-cache->rig->rootClearance;
    const float rows=std::max({std::abs(m->right.x)+std::abs(m->up.x)+std::abs(m->at.x),std::abs(m->right.y)+std::abs(m->up.y)+std::abs(m->at.y),std::abs(m->right.z)+std::abs(m->up.z)+std::abs(m->at.z)});
    const float cols=std::max({std::abs(m->right.x)+std::abs(m->right.y)+std::abs(m->right.z),std::abs(m->up.x)+std::abs(m->up.y)+std::abs(m->up.z),std::abs(m->at.x)+std::abs(m->at.y)+std::abs(m->at.z)});
    // Enclose both the native COL (which decides callback eligibility) and
    // the original normal4 owner. No fixed two-metre substitute for anatomy.
    const float native=length(vector(*m,col->sphere))+col->radius*std::sqrt(rows*cols);
    return std::nextafter(std::max(native,native_coverage_geometry::originalNormalRadius(rootShift)),INFINITY);
}
static bool originalGroundCoveragePath(void*p,OriginalGroundLease&lease,V start,V end,float extra=0){
    if(!collision||!collision->world||!finite(start)||!finite(end)||!std::isfinite(extra)||extra<0)return false;
    const float radius=originalGroundBodyRadius(p)+extra;
    if(!std::isfinite(radius)||!collision->nativeCoverage.contains(start,radius)||!collision->nativeCoverage.contains(end,radius))return false;
    const V midpoint=(start+end)*.5f;const float reach=radius+.5f*length(end-start);
    std::array<void*,257> found{};short count=0;
    // 568B80 -> 565000 uses the transformed native COL bound centre/radius.
    // 564A20 only tests entity origins and missed building17656's nearby
    // collision volume, admitting an interval with seven uncovered spheres.
    reinterpret_cast<void(__cdecl*)(const V&,float,bool,short*,short,void**,bool,bool,bool,bool,bool)>(0x568B80)
        (midpoint,reach,false,&count,256,found.data(),true,true,true,true,true);
    if(count<0||count>=256)return false;
    for(int n=0;n<count;n++){
        void*e=found[n];if(!e||e==p)continue;
        if(at<unsigned char>(e,0x2F)!=area()&&at<unsigned char>(e,0x2F)!=13)continue;
        if(!(at<unsigned char>(e,0x1C)&1))continue; // bUsesCollision off: not native support (World.hpp rebuild)
        const int type=at<unsigned char>(e,0x36)&7;
        if(type==3){native_ground_authority::Identity id{};if(!originalGroundPeer(e,id)){lease.uncoveredModel=at<short>(e,0x22);lease.uncoveredType=type;return false;}lease.peers[e]=id;continue;}
        if(!collision->nativeCoverage.currentLocal(e,start,end,radius)){lease.uncoveredModel=at<short>(e,0x22);lease.uncoveredType=type;return false;}
    }return true;
}
static bool originalGroundCoverage(void*p,OriginalGroundLease&lease){
    const float dt=hostNativeSeconds;if(!std::isfinite(dt)||dt<=0||dt>.25f)return false;
    const V start=position(p),velocity=at<V>(p,0x44)*50.f;
    // Source-authored acceleration envelope: XY motor30, support spring at
    // most10g plus damping50, and gravity9.8. This is only early admission;
    // peer impulses are NOT assumed bounded by it. Their actual TOTAL proposed
    // and admitted paths are checked before any host commit below.
    const float extra=(30.f+98.f+50.f+9.8f)*dt*dt;
    return finite(velocity)&&originalGroundCoveragePath(p,lease,start,start+velocity*dt,extra);
}
static void clearOriginalGroundHost(){
    for(const auto&[id,s]:originalGroundLeases)if(!s.complete&&(s.lease.gravityCalls||s.lease.collisionCalls))abortOriginalGround("clear_before_complete",s.lease.identity.reference);
    originalGroundLeases.clear();originalGroundReceipts.clear();originalGroundLastClaim.clear();originalGroundPreviousPlane.clear();originalGroundFrame=~0u;
    originalGroundFixtureRef=-1;originalGroundFixtureStop=false;originalGroundGroupPeers.clear();originalGroundGroupEpoch=~uint64_t(0);
}
static void armOriginalGroundFixture(int ref){
    if(!originalGroundEnabled)return;
    if(ref<0||!pedFromRef(ref)||(originalGroundOnlyRef>=0&&originalGroundOnlyRef!=ref))return;
    for(const auto&[id,s]:originalGroundLeases)if(!s.complete&&(s.lease.gravityCalls||s.lease.collisionCalls))abortOriginalGround("fixture_rearm_inside_owned_interval",s.lease.identity.reference);
    originalGroundFixtureRef=ref;originalGroundFixtureStop=false;
    event("original_ground_fixture_armed",ref,at<short>(pedFromRef(ref),0x22),"scene_prepared=1 activation_next_native_interval=1 no_house_or_transport_claims=1");
}
static void disarmOriginalGroundFixture(){originalGroundFixtureStop=true;}
static void reserveOriginalGroundFrame(){
    if(!originalGroundEnabled)return;
    for(const auto&[id,s]:originalGroundLeases)if(!s.complete&&(s.lease.gravityCalls||s.lease.collisionCalls))abortOriginalGround("renderer_barrier_missing",s.lease.identity.reference);
    originalGroundLeases.clear();originalGroundReceipts.clear();originalGroundFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(originalGroundFixtureStop){originalGroundFixtureRef=-1;originalGroundFixtureStop=false;}
    if(originalGroundFixtureRef<0)return;
    if(!recoveryAllowed()||!collision||!collision->world)return;
    unsigned candidates=0,geometryRejected=0;int uncoveredModel=-1,uncoveredType=-1;
    for(const auto&[ref,row]:animatedNativePairOwners){
        if(ref!=originalGroundFixtureRef)continue;
        if(originalGroundOnlyRef>=0&&ref!=originalGroundOnlyRef)continue;
        void*p=row.ped;auto f=originalGroundFacts(p);OriginalGroundLease s;
        const unsigned physical=at<unsigned>(p,0x40);
        // ApplyGravity would be live on this exact native branch. Do not
        // enable source gravity for a native no-gravity/locked/attached actor.
        if(!native_ground_authority::nativeGravityEligible(physical,at<unsigned char>(p,0x1C),at<unsigned char>(p,0x36),at<void*>(p,0xFC)!=nullptr)||!native_ground_authority::ordinary(f)||!f.grounded||
           !hostNativeStarts.contains(animatedExternalId(ref)))continue;
        if(!native_ground_authority::capture(f.identity,s.before))continue;
        candidates++;
        f.worldCoverageComplete=originalGroundCoverage(p,s);f.finiteCohortComplete=f.worldCoverageComplete;
        if(!f.worldCoverageComplete){geometryRejected++;uncoveredModel=s.uncoveredModel;uncoveredType=s.uncoveredType;}
        f.metadataComplete=true;f.stageCapacityReady=hostNativeStarts.size()<=256;
        if(!s.lease.reserve(f))continue;
        originalGroundLeases.emplace(animatedExternalId(ref),std::move(s));
        static std::map<int,unsigned> reports;if(gameTime()-reports[ref]>=500){reports[ref]=gameTime();
            event("original_ground_reserved",ref,row.model,"source_frame="+std::to_string(originalGroundFrame)+" static_exact_coverage=1 finite_ped_cohort=1 gravity_and_collision_reserved=1");}
    }
    static unsigned report=0;if(gameTime()-report>=500){report=gameTime();event("original_ground_coverage",-1,-1,
        "candidates="+std::to_string(candidates)+" reserved="+std::to_string(originalGroundLeases.size())+" geometry_rejected="+std::to_string(geometryRejected)+" uncovered_model="+std::to_string(uncoveredModel)+" uncovered_type="+std::to_string(uncoveredType)+" mesh_complete="+std::to_string(collision->nativeCoverage.complete));}
    for(auto i=originalGroundLastClaim.begin();i!=originalGroundLastClaim.end();){
        if(!pedFromRef(int(uint32_t(i->first)))){if(bodyGroup)nm_group_forget_external_ground(bodyGroup,i->first);originalGroundPreviousPlane.erase(i->first);i=originalGroundLastClaim.erase(i);}else ++i;
    }
}
static bool __cdecl suppressOriginalGroundGravity(void*p) noexcept {
    try{
        if(!originalGroundEnabled||!p)return false;const auto i=originalGroundLeases.find(animatedExternalId(reference(p)));if(i==originalGroundLeases.end())return false;
        const auto decision=i->second.lease.gravity(originalGroundFacts(p));
        if(decision==native_ground_authority::Decision::Rejected)abortOriginalGround("gravity_state_changed",reference(p));
        return decision==native_ground_authority::Decision::Shared;
    }catch(...){abortOriginalGround("gravity_callback_exception",reference(p));}
}
static std::string originalGroundFailureFacts(const OriginalGroundLease&s,const native_ground_authority::Facts&f){
    const auto&i=s.lease.identity;const auto&j=f.identity;
    return " identity_same="+std::to_string(native_ground_authority::same(i,j))+" source_frame="+std::to_string(i.frame)+" current_frame="+std::to_string(j.frame)+
        " ped_same="+std::to_string(i.ped==j.ped)+" clump_same="+std::to_string(i.clump==j.clump)+" matrix_same="+std::to_string(i.matrix==j.matrix)+" ref_same="+std::to_string(i.reference==j.reference)+" model_same="+std::to_string(i.model==j.model)+
        " ordinary="+std::to_string(f.ordinaryOnFoot)+" alive="+std::to_string(f.alive)+" test_gate="+std::to_string(f.testGate)+" native_pair="+std::to_string(f.nativePairReady)+" source_ready="+std::to_string(f.sourceReady)+
        " nm="+std::to_string(f.naturalMotion)+" recovery="+std::to_string(f.recovering)+" in_vehicle="+std::to_string(f.inVehicle)+" attached="+std::to_string(f.attached)+" submerged="+std::to_string(f.submerged)+
        " jump="+std::to_string(f.jumping)+" climb="+std::to_string(f.climbing)+" swim="+std::to_string(f.swimming)+" fall="+std::to_string(f.fallTask)+" gravity_suppressed="+std::to_string(s.lease.gravityCalls)+" collision_suppressed="+std::to_string(s.lease.collisionCalls);
}
static bool suppressOriginalGroundCollision(void*p,void*other,uintptr_t caller){
    if(!originalGroundEnabled||!p)return false;const auto i=originalGroundLeases.find(animatedExternalId(reference(p)));if(i==originalGroundLeases.end())return false;
    auto&s=i->second;auto coverage=native_ground_authority::Coverage::Unknown;
    const V initial{s.before.position[0],s.before.position[1],s.before.position[2]};
    if(other&&collision&&collision->nativeCoverage.currentLocal(other,initial,position(p),originalGroundBodyRadius(p)))coverage=native_ground_authority::Coverage::VerifiedStaticMesh;
    if(const auto peer=s.peers.find(other);peer!=s.peers.end()){
        native_ground_authority::Identity current{};if(originalGroundPeer(other,current)&&native_ground_authority::same(peer->second,current))coverage=native_ground_authority::Coverage::StagedFiniteOwner;
    }
    const auto facts=originalGroundFacts(p);const auto decision=s.lease.collision(facts,coverage);
    if(decision==native_ground_authority::Decision::Rejected){
        auto*col=other?reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(other):nullptr;
        const auto row=collision->nativeCoverage.rows.find(other);const bool known=row!=collision->nativeCoverage.rows.end();
        const bool exact=NativeWorldCoverage::comparable(col);
        const bool samePose=other&&known&&NativeWorldCoverage::sameFrame(NativeWorldCoverage::frame(other),row->second.frame);
        const V where=position(p),old{s.before.position[0],s.before.position[1],s.before.position[2]};
        event("original_ground_collision_rejected_detail",reference(p),at<short>(p,0x22),
            "caller="+std::to_string(caller)+" coverage="+std::to_string(int(coverage))+" known_static="+std::to_string(known)+" static_pose_same="+std::to_string(samePose)+" comparable_col="+std::to_string(exact)+" static_geometry_same="+std::to_string(known&&collision->nativeCoverage.sameGeometry(col,row->second))+
            " other_ref="+std::to_string(reference(other))+" other_model="+std::to_string(other?at<short>(other,0x22):-1)+" other_type="+std::to_string(other?(at<unsigned char>(other,0x36)&7):-1)+" other_flags="+std::to_string(other?at<unsigned>(other,0x1C):0)+
            " col_spheres="+std::to_string(col&&col->data?col->data->spheres:0)+" col_lines="+std::to_string(col&&col->data?col->data->lines:0)+" col_triangles="+std::to_string(col&&col->data?col->data->triangles:0)+
            " peer_reserved="+std::to_string(s.peers.contains(other))+" native_root_travel="+std::to_string(length(where-old))+" native_flags46c="+std::to_string(at<unsigned>(p,0x46C))+originalGroundFailureFacts(s,facts));
        abortOriginalGround("collision_state_or_coverage_changed",reference(p));
    }
    return decision==native_ground_authority::Decision::Shared;
}
static void verifyOriginalGroundNativeEnd(){
    if(!originalGroundEnabled)return;
    for(auto&[id,s]:originalGroundLeases){
        void*p=pedFromRef(s.lease.identity.reference);auto f=originalGroundFacts(p);
        const auto decision=s.lease.current(f);
        if(decision==native_ground_authority::Decision::Native){s.complete=true;continue;}
        if(decision==native_ground_authority::Decision::Rejected||!s.lease.gravityCalls)abortOriginalGround("native_interval_not_fully_owned",s.lease.identity.reference);
        if(!originalPedMotorSamples.contains(id))abortOriginalGround("native_motor_branch_not_claimed",s.lease.identity.reference);
    }
}
static bool stageOriginalGround(NmGroup*group,uint64_t epoch){
    if(!originalGroundEnabled)return true;
    std::vector<uint64_t> ids,claims;std::vector<uint32_t> flags,claimFlags;std::vector<float> masses,xy;
    for(const auto&o:externalHostOwners){
        ids.push_back(o.id);float mass=o.animated?25.f:at<float>(o.entity,0x8C);
        if(!std::isfinite(mass)||mass<=0)return false;masses.push_back(mass);
        const bool fixed=!o.animated&&std::all_of(o.rows.begin()+9,o.rows.end(),[](float x){return x==0;});
        // Namespace2 is the original ordinary phInstGta owner, namespace1 is
        // a vehicle. Neither is the source's excluded instance-vf4 type15.
        const bool boat=!o.animated&&at<int>(o.entity,0x594)==5;flags.push_back((boat?1u:0u)|(fixed?4u:0u));
    }
    for(auto&[id,s]:originalGroundLeases){
        if(s.complete||!s.lease.reserved)continue;
        void*p=pedFromRef(s.lease.identity.reference);auto f=originalGroundFacts(p);
        const auto owner=std::find_if(externalHostOwners.begin(),externalHostOwners.end(),[&](const auto&o){return o.id==id;});
        if(owner==externalHostOwners.end())abortOriginalGround("reserved_owner_missing_from_island",s.lease.identity.reference);
        OriginalGroundLease currentCoverage;f.worldCoverageComplete=originalGroundCoverage(p,currentCoverage);f.finiteCohortComplete=f.worldCoverageComplete;
        f.metadataComplete=ids.size()==externalHostOwners.size();f.stageCapacityReady=ids.size()<=256;
        if(!s.lease.claim(f))abortOriginalGround("post_native_claim_rejected",s.lease.identity.reference);
        for(const auto&[ped,identity]:currentCoverage.peers)s.peers[ped]=identity;
        const auto motor=originalPedMotorSamples.find(id);if(motor==originalPedMotorSamples.end())abortOriginalGround("authored_motor_missing",s.lease.identity.reference);
        claims.push_back(id);uint32_t cf=(s.lease.initialGrounded?1u:0u)|2u;
        if((epoch&255)==0&&(!originalGroundLastClaim.contains(id)||unsigned(originalGroundFrame-originalGroundLastClaim[id])!=1))cf|=4;
        claimFlags.push_back(cf);xy.push_back(motor->second.facts.authored.x);xy.push_back(motor->second.facts.authored.y);
    }
    if(claims.empty())return true;
    if(!ok(nm_group_set_external_ground(group,epoch,ids.data(),flags.data(),masses.data(),int(ids.size()),claims.data(),claimFlags.data(),xy.data(),int(claims.size()))))abortOriginalGround("sdk_ground_staging_rejected");
    return true;
}
static bool preflightOriginalGround(NmGroup*group){
    originalGroundReceipts.clear();if(!originalGroundEnabled)return true;
    std::vector<uint64_t> expected;for(const auto&[id,s]:originalGroundLeases)if(s.lease.claimed&&!s.complete)expected.push_back(id);
    const int count=nm_group_external_ground_results(group,nullptr,nullptr,0);
    if(count<0||count!=int(expected.size()))return false;
    std::vector<uint64_t> keys(size_t(count)*4);std::vector<float> rows(size_t(count)*19);
    if(nm_group_external_ground_results(group,keys.data(),rows.data(),count)!=count)return false;
    for(int n=0;n<count;n++){
        const uint64_t id=keys[n*4];if(keys[n*4+1]!=externalHostEpoch||std::count(expected.begin(),expected.end(),id)!=1||originalGroundReceipts.contains(id))return false;
        const auto&r=originalGroundLeases.at(id);void*p=pedFromRef(r.lease.identity.reference);const float*q=rows.data()+n*19;
        if(!std::all_of(q,q+19,[](float v){return std::isfinite(v);})||!native_ground_authority::same(r.lease.identity,originalGroundIdentity(p))||
           (q[0]!=0&&q[0]!=1)||(q[1]!=0&&q[1]!=1))return false;
        OriginalGroundReceipt s;s.epoch=keys[n*4+1];s.support=keys[n*4+2];s.member=keys[n*4+3];std::copy(q,q+19,s.row.begin());originalGroundReceipts[id]=s;
    }return true;
}
static bool preflightOriginalGroundMotion(const HostExternalOwner&o,V requested,V admitted){
    if(!originalGroundActive(o.entity))return true;
    if(originalGroundGroupEpoch!=externalHostEpoch)return false;
    const auto&s=originalGroundLeases.at(o.id);auto*cache=bodyCollider(o.entity);if(!cache||!cache->rig)return false;
    const V rootShift{0,0,1.f-cache->rig->rootClearance};
    const V initial{s.before.position[0],s.before.position[1],s.before.position[2]},now=position(o.entity),base=o.acceptedBase.centre.p-rootShift,staged=o.staged.centre.p-rootShift;
    // Verify every actual segment, including the uncut request. Checking only
    // the clipped endpoint could silently approve a result influenced by an
    // approximate sphere encountered on the proposed path.
    const std::pair<V,V> paths[]={{initial,now},{base,staged},{base,base+requested},{base,base+admitted},{now,now+admitted}};
    for(const auto&[start,end]:paths){
        OriginalGroundLease proof;if(!originalGroundCoveragePath(o.entity,proof,start,end)){
            event("original_ground_motion_coverage_rejected",o.ref,o.model,"uncovered_model="+std::to_string(proof.uncoveredModel)+" uncovered_type="+std::to_string(proof.uncoveredType)+" requested_m="+std::to_string(length(requested))+" admitted_m="+std::to_string(length(admitted))+" no_host_mutated=1");return false;
        }
        for(const auto&[ped,identity]:proof.peers){const auto i=originalGroundGroupPeers.find(ped);if(i==originalGroundGroupPeers.end()||!native_ground_authority::same(i->second,identity))return false;}
    }return true;
}
static V originalGroundDrive(uint64_t id){
    const auto i=originalGroundReceipts.find(id);if(i==originalGroundReceipts.end())return {};
    const auto&r=i->second.row;const auto previous=originalGroundPreviousPlane.find(id);
    const float old=previous==originalGroundPreviousPlane.end()?0.f:previous->second;
    return V{r[12]+r[15],r[13]+r[16],r[14]+r[17]+25.f*(r[18]-old)};
}
static void commitOriginalGround(const HostExternalOwner&o){
    const auto i=originalGroundReceipts.find(o.id);if(i==originalGroundReceipts.end())return;
    auto&s=originalGroundLeases.at(o.id);const auto&r=i->second.row;void*p=o.entity;
    if(!s.lease.canCommit(originalGroundFacts(p),true))abortOriginalGround("ground_commit_identity_changed",o.ref);
    at<unsigned>(p,0x46C)=native_ground_authority::standingFlags(at<unsigned>(p,0x46C),r[1]==1,r[0]==1);
    originalGroundPreviousPlane[o.id]=r[18];
    if(r[0]==1)at<float>(p,0x588)=r[5];
    // Source support owns these references too. Static map support is not a
    // dynamic attachment; no native platform motor may apply a second twist.
    for(size_t offset:{size_t(0x568),size_t(0x584)})if(void*old=at<void*>(p,offset)){
        reinterpret_cast<void(__thiscall*)(void*,void**)>(0x571A00)(old,&at<void*>(p,offset));at<void*>(p,offset)=nullptr;
    }
    s.lastCommit=originalGroundFrame;if(o.fraction==1.f){s.complete=true;originalGroundLastClaim[o.id]=originalGroundFrame;}
    static std::map<int,unsigned> logs;if(gameTime()-logs[o.ref]>=100){logs[o.ref]=gameTime();event("original_ground_committed",o.ref,o.model,
        "epoch="+std::to_string(i->second.epoch)+" fraction="+std::to_string(o.fraction)+" grounded="+std::to_string(r[0])+" previous="+std::to_string(r[1])+" support_owner="+std::to_string(i->second.support)+" support_member="+std::to_string(i->second.member)+
        " point_z="+std::to_string(r[5])+" support_jz="+std::to_string(r[14])+" gravity_jz="+std::to_string(r[17])+" slope_v="+std::to_string(r[18])+" final_z="+std::to_string(position(p).z)+" final_vz="+std::to_string(at<float>(p,0x4C)*50.f)+
        " native_gravity_calls_suppressed="+std::to_string(s.lease.gravityCalls)+" native_collision_calls_suppressed="+std::to_string(s.lease.collisionCalls)+" forces_applied_once=1");}
}
static void failOriginalGroundStep(){
    for(const auto&[id,s]:originalGroundLeases)if(!s.complete&&(s.lease.gravityCalls||s.lease.collisionCalls))abortOriginalGround("shared_step_or_preflight_failed",s.lease.identity.reference);
}
static void verifyOriginalGroundMembers(const std::vector<Active*>&live,const std::vector<int>&invalid){
    originalGroundGroupPeers.clear();originalGroundGroupEpoch=externalHostEpoch;
    for(const auto&o:externalHostOwners)if(o.animated)originalGroundGroupPeers[o.entity]=originalGroundIdentity(o.entity);
    for(const auto*a:live)if(!a->preview&&!a->mounted&&std::find(invalid.begin(),invalid.end(),a->ref)==invalid.end())originalGroundGroupPeers[a->ped]=originalGroundIdentity(a->ped);
    for(const auto&[id,s]:originalGroundLeases)if(s.lease.claimed&&!s.complete)for(const auto&[ped,identity]:s.peers){
        if(!native_ground_authority::same(identity,originalGroundIdentity(ped)))abortOriginalGround("finite_peer_identity_changed",s.lease.identity.reference);
        const auto owner=std::find_if(externalHostOwners.begin(),externalHostOwners.end(),[&](const auto&o){return o.animated&&o.entity==ped&&o.ref==identity.reference;});
        if(owner!=externalHostOwners.end())continue;
        const auto member=std::find_if(live.begin(),live.end(),[&](const auto*a){return a->ped==ped&&a->ref==identity.reference&&!a->preview&&!a->mounted;});
        if(member==live.end()||std::find(invalid.begin(),invalid.end(),identity.reference)!=invalid.end())abortOriginalGround("finite_peer_missing_from_group",s.lease.identity.reference);
    }
}
static bool installOriginalGroundHost(){
    char gate[8]{},only[32]{};
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||GetEnvironmentVariableA("GTASA_SOURCE_GROUND_OWNER",gate,sizeof gate)!=1||gate[0]!='1')return true;
    if(GetEnvironmentVariableA("GTASA_SOURCE_GROUND_REF",only,sizeof only))originalGroundOnlyRef=std::atoi(only);
    using namespace native_ground_authority;auto*location=reinterpret_cast<unsigned char*>(gravityCallSite);
    if(std::memcmp(location,gravityCallBytes,5)!=0){event("original_ground_install_rejected",-1,-1,"gravity_call_bytes_changed=1");return false;}
    DWORD old=0;if(!VirtualProtect(location,5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::array<unsigned char,5> replacement{0xE8,0,0,0,0};const int32_t offset=int32_t(reinterpret_cast<uintptr_t>(&gravityBranch)-(gravityCallSite+5));std::memcpy(replacement.data()+1,&offset,4);
    suppressGravity=&suppressOriginalGroundGravity;std::memcpy(location,replacement.data(),5);DWORD ignored=0;
    if(!FlushInstructionCache(GetCurrentProcess(),location,5)||!VirtualProtect(location,5,old,&ignored)){
        std::memcpy(location,gravityCallBytes,5);FlushInstructionCache(GetCurrentProcess(),location,5);VirtualProtect(location,5,old,&ignored);suppressGravity=nullptr;return false;
    }
    originalGroundEnabled=true;event("original_ground_host_ready",-1,-1,"diagnostic_opt_in=1 requires_prepared_fixture_arm=1 native_gravity_and_collision_atomic=1 ordinary_static_and_verified_peds_only=1");return true;
}
