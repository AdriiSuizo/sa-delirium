#pragma once
#include "NativePairAudit.hpp"
// Include after OriginalAnimatedOwner + ExternalMobility. This is a journal,
// not an admission/suppression rule. A native-phase candidate may leave the
// cohort before the later finite solve; only the paired receipts prove overlap.
struct NativePairAuditSummary {unsigned calls=0,effects=0;V pedDv{},carDv{},pedDw{},carDw{},pedFriction{},carFriction{};};
static unsigned nativePairAuditFrame=~0u;
static std::map<std::pair<int,int>,NativePairAuditSummary> nativePairAuditRows;
static unsigned nativePairAuditCurrentFrame(){return *reinterpret_cast<unsigned*>(0xB7CB4C);}
static NativePairAudit::Identity nativePairAuditIdentity(void*p){
    if(!ready||!p)return {};
    const int type=at<unsigned char>(p,0x36)&7;
    const int ref=type==3?reference(p):type==2?vehicleRef(p):-1;if(ref<0||!matrix(p))return {};
    const uint64_t id=(uint64_t(type==3?2:1)<<32)|uint32_t(ref);
    const auto start=hostNativeStarts.find(id);
    const bool fresh=start!=hostNativeStarts.end()&&hostNativeStateCurrent(start->second);
    const bool candidate=type==3?animatedExternalEligible(p):fresh;
    return {type,ref,candidate,fresh,reinterpret_cast<uintptr_t>(at<void*>(p,0x18)),at<short>(p,0x22)};
}
static V nativePairAuditVector(NativePairAudit::Vec v){return{v.x,v.y,v.z};}
static std::string nativePairAuditText(V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);}
static void nativePairAuditBegin(unsigned frame){if(nativePairAuditFrame!=frame){nativePairAuditRows.clear();nativePairAuditFrame=frame;}}
static void nativePairAuditSink(const NativePairAudit::Record&r){
    if(!r.result)return;
    nativePairAuditBegin(r.frame);
    const bool pedA=r.aBefore.id.type==3;const auto&pb=pedA?r.aBefore:r.bBefore;const auto&pa=pedA?r.aAfter:r.bAfter;
    const auto&vb=pedA?r.bBefore:r.aBefore;const auto&va=pedA?r.bAfter:r.aAfter;
    const V pdv=(nativePairAuditVector(pa.velocity)-nativePairAuditVector(pb.velocity))*50.f;
    const V vdv=(nativePairAuditVector(va.velocity)-nativePairAuditVector(vb.velocity))*50.f;
    const V pdw=(nativePairAuditVector(pa.spin)-nativePairAuditVector(pb.spin))*50.f;
    const V vdw=(nativePairAuditVector(va.spin)-nativePairAuditVector(vb.spin))*50.f;
    const V pf=(nativePairAuditVector(pa.frictionVelocity)-nativePairAuditVector(pb.frictionVelocity))*50.f;
    const V vf=(nativePairAuditVector(va.frictionVelocity)-nativePairAuditVector(vb.frictionVelocity))*50.f;
    auto&sum=nativePairAuditRows[{pb.id.ref,vb.id.ref}];
    // Nested native fallbacks are journaled but not double-counted as two
    // independent physical changes. The outer record already contains them.
    if(r.depth==0){sum.calls++;sum.effects+=(length(pdv)+length(vdv)+length(pdw)+length(vdw)+length(pf)+length(vf)>0);
        sum.pedDv=sum.pedDv+pdv;sum.carDv=sum.carDv+vdv;sum.pedDw=sum.pedDw+pdw;sum.carDw=sum.carDw+vdw;
        sum.pedFriction=sum.pedFriction+pf;sum.carFriction=sum.carFriction+vf;}
    event("native_pair_receipt",pb.id.ref,pb.id.model,
        "vehicle_ref="+std::to_string(vb.id.ref)+" frame="+std::to_string(r.frame)+" path="+std::to_string(uint32_t(r.path))+
        " serial="+std::to_string(r.serial)+" parent="+std::to_string(r.parent)+" depth="+std::to_string(r.depth)+" caller="+std::to_string(r.caller)+
        " ped_candidate="+std::to_string(pb.id.finiteOwner)+" vehicle_candidate="+std::to_string(vb.id.finiteOwner)+
        " ped_fresh="+std::to_string(pb.id.fresh)+" vehicle_fresh="+std::to_string(vb.id.fresh)+
        " ped_dv_mps="+nativePairAuditText(pdv)+" vehicle_dv_mps="+nativePairAuditText(vdv)+
        " ped_dw_rps="+nativePairAuditText(pdw)+" vehicle_dw_rps="+nativePairAuditText(vdw)+
        " ped_friction_dv="+nativePairAuditText(pf)+" vehicle_friction_dv="+nativePairAuditText(vf)+
        " native_ped_mass="+std::to_string(pb.mass)+" native_vehicle_mass="+std::to_string(vb.mass)+
        " damage_valid="+std::to_string(r.damageValid)+" damage_ped="+std::to_string(pedA?r.damageA:r.damageB)+
        " damage_vehicle="+std::to_string(pedA?r.damageB:r.damageA)+
        " point="+nativePairAuditText(nativePairAuditVector(r.pointAfter.point))+" normal_A="+nativePairAuditText(nativePairAuditVector(r.pointAfter.normal))+
        " penetration="+std::to_string(r.pointAfter.depth)+" observer_only=1");
}
static void initializeNativePairAudit(){
    NativePairAudit::identify=&nativePairAuditIdentity;NativePairAudit::sink=&nativePairAuditSink;NativePairAudit::frame=&nativePairAuditCurrentFrame;
}
static void auditSolvedExternalPair(uint64_t a,uint64_t b,const float*r,float dt){
    const bool pedA=(a>>32)==2&&(b>>32)==1,pedB=(a>>32)==1&&(b>>32)==2;if(!pedA&&!pedB)return;
    const int ped=int(uint32_t(pedA?a:b)),vehicle=int(uint32_t(pedA?b:a));
    const unsigned frame=nativePairAuditCurrentFrame();nativePairAuditBegin(frame);
    const auto found=nativePairAuditRows.find({ped,vehicle});const NativePairAuditSummary s=found!=nativePairAuditRows.end()?found->second:NativePairAuditSummary{};
    const V j=V{r[0],r[1],r[2]}*(pedA?1.f:-1.f);if(length(j)<=0)return;
    event("external_pair_authority_receipt",ped,pedFromRef(ped)?at<short>(pedFromRef(ped),0x22):-1,
        "vehicle_ref="+std::to_string(vehicle)+" frame="+std::to_string(frame)+" dt="+std::to_string(dt)+
        " impulse_on_ped_ns="+nativePairAuditText(j)+" point="+nativePairAuditText({r[3],r[4],r[5]})+
        " native_calls_same_frame="+std::to_string(s.calls)+" native_effects_same_frame="+std::to_string(s.effects)+
        " native_ped_dv_mps="+nativePairAuditText(s.pedDv)+" native_vehicle_dv_mps="+nativePairAuditText(s.carDv)+
        " native_ped_dw_rps="+nativePairAuditText(s.pedDw)+" native_vehicle_dw_rps="+nativePairAuditText(s.carDw)+
        " double_authority_evidence="+std::to_string(s.effects>0)+" observer_only=1");
}
