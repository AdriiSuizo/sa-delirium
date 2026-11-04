#pragma once
#include "NativeGroundAudit.hpp"
// Explicit passive test observer. Keeps the exact native store and common
// owner commit in one per-launch stream; never changes support or placement.
struct NativeGroundOwnerCommit {
    void* ped{};void* clump{};int model=-1;unsigned frame=0,time=0;
    V before{},after{},deltaVelocity{};
};
static bool nativeGroundAuditEnabled=false;
static std::map<int,NativeGroundOwnerCommit> nativeGroundOwnerCommits;
static std::ofstream nativeGroundAuditStream;
static unsigned nativeGroundAuditRows=0;
static void recordNativeGroundOwnerCommit(void*p,V before,V after,V deltaVelocity){
    if(!nativeGroundAuditEnabled||!p)return;
    nativeGroundOwnerCommits[reference(p)]={p,at<void*>(p,0x18),at<short>(p,0x22),
        *reinterpret_cast<unsigned*>(0xB7CB4C),gameTime(),before,after,deltaVelocity};
}
static void __cdecl observeNativeGroundStore(const native_ground_audit::Sample&s){
    if(!nativeGroundAuditEnabled||!ready||!s.ped||!nativeGroundAuditStream)return;
    const int ref=reference(s.ped);if(ref<0||pedFromRef(ref)!=s.ped)return;
    const bool animated=animatedNativePairCurrent(s.ped)&&animatedExternalEligible(s.ped);
    const auto physical=agents.find(ref);const bool physicalOwner=physical!=agents.end()&&physical->second->ped==s.ped;
    if(!animated&&!physicalOwner)return;
    const auto i=nativeGroundOwnerCommits.find(ref);
    const NativeGroundOwnerCommit*c=i!=nativeGroundOwnerCommits.end()&&i->second.ped==s.ped&&
        i->second.clump==at<void*>(s.ped,0x18)&&i->second.model==at<short>(s.ped,0x22)?&i->second:nullptr;
    auto&f=nativeGroundAuditStream;
    f<<"{\"frame\":"<<*reinterpret_cast<unsigned*>(0xB7CB4C)<<",\"time_ms\":"<<gameTime()
        <<",\"ref\":"<<ref<<",\"site\":"<<s.site<<",\"other_model\":"<<(s.other?at<short>(s.other,0x22):-1)
        <<",\"animated_owner\":"<<animated<<",\"physical_owner\":"<<physicalOwner
        <<",\"velocity_store\":"<<s.velocityStore<<",\"position_z\":"<<s.positionZ
        <<",\"before\":"<<s.before<<",\"incoming\":"<<s.incoming<<",\"native_v\":["<<s.velocity[0]<<','<<s.velocity[1]<<','<<s.velocity[2]
        <<"],\"flags\":["<<s.flags46c<<','<<s.flags470<<','<<s.flags474<<"],\"ground_height\":"<<s.groundHeight
        <<",\"standing\":"<<reinterpret_cast<uintptr_t>(s.standing)<<",\"contact\":"<<reinterpret_cast<uintptr_t>(s.contact)
        <<",\"previous_commit\":";
    if(c)f<<"{\"frame\":"<<c->frame<<",\"time_ms\":"<<c->time<<",\"before_z\":"<<c->before.z<<",\"after_z\":"<<c->after.z<<",\"dv_z\":"<<c->deltaVelocity.z<<'}';
    else f<<"null";
    f<<"}\n";
    // Bound observer I/O, retaining a flushed prefix if a test terminates.
    if((++nativeGroundAuditRows&127)==0)f.flush();
}
static bool installNativeGroundAudit(){
    char passive[8]{},requested[8]{};
    if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",passive,8)||
       !GetEnvironmentVariableA("GTASA_CAPTURE_GROUND_OWNER",requested,8)||requested[0]!='1')return true;
    if(!sessionlog::enabled||sessionlog::directory.empty())return false;
    nativeGroundAuditStream.open(sessionlog::directory/"ground-owner.jsonl");
    if(!nativeGroundAuditStream)return false;
    nativeGroundAuditStream<<std::setprecision(9);
    uintptr_t failed=0;
    if(!native_ground_audit::install(&observeNativeGroundStore,failed)){
        event("native_ground_audit_failed",-1,-1,"site="+std::to_string(failed));return false;
    }
    nativeGroundAuditEnabled=true;
    event("native_ground_audit_ready",-1,-1,"read_only=1 sites=4 schema=2 stream=ground-owner.jsonl");return true;
}
