#pragma once
#include "CodePatch.hpp"
// CPhysical applies contact impulses in the preceding frame; CBike::VehicleDamage
// consumes its saved damage record on its next ProcessControl. Sampling merely
// before the event handler therefore captures the stopped bike, not its rider's
// incoming momentum. This ledger follows that exact native contact/event chain.
#include <deque>
#include "NativePairAuditHost.hpp"
struct BikeContactTwist {
    int vehicle=-1;V linear{},angular{},centre{};unsigned frame=0;SeedEpoch epoch{};
    T vehicleFrame{};std::array<std::shared_ptr<BikeContactPose>,2> riders{};std::array<BikePoseDiagnostic,2> riderDiagnostics{};
};
struct BikeContactRecord {BikeContactTwist twist;void*other=nullptr;V point{},normal{};float intensity=0;};
static std::deque<BikeContactRecord> bikeContactRecords;
struct BikeImpactAdmission {std::shared_ptr<PoseSeed> pose;V normal{},eventVelocity{};float intensity=0;unsigned frame=0;};
static std::map<int,BikeImpactAdmission> bikeImpactAdmissions;
static std::optional<BikeContactTwist> beforeBikeContact(void*entity){
    if(!ready||!entity||(at<unsigned char>(entity,0x36)&7)!=2||!matrix(entity))return {};
    const int type=at<int>(entity,0x594),ref=vehicleRef(entity);
    if(ref<0||(type!=9&&type!=10&&type!=2)||(!at<void*>(entity,0x460)&&!at<void*>(entity,0x464)))return {};
    const auto motion=bikeRigidMotion(*matrix(entity),at<V>(entity,0xA4),at<V>(entity,0x44),at<V>(entity,0x50));
    if(!finite(motion.linear)||!finite(motion.angular)||!finite(motion.centre))return {};
    BikeContactTwist sample{ref,motion.linear,motion.angular,motion.centre,*reinterpret_cast<unsigned*>(0xB7CB4C)};
    sample.epoch=bikePoseEpoch(SeedPhase::NativeContact);sample.vehicleFrame=transform(*matrix(entity));
    for(size_t n=0;n<sample.riders.size();n++)sample.riders[n]=captureBikeContactPose(at<void*>(entity,0x460+n*4),entity,sample.epoch,true,&sample.riderDiagnostics[n]);
    return sample;
}
static void afterBikeContact(const std::optional<BikeContactTwist>&before,void*other,const ColPoint&point,float intensity,float sign,unsigned path,const V*outputLinear=nullptr,const V*outputAngular=nullptr){
    if(!before||!other||!std::isfinite(intensity))return;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    while(!bikeContactRecords.empty()&&(frame-bikeContactRecords.front().twist.frame>3||bikeContactRecords.size()>=4096))bikeContactRecords.pop_front();
    if(intensity>0)bikeContactRecords.push_back({*before,other,point.point,point.normal*sign,intensity});
    // Observe the actual impulse carrier before choosing an IV ejection
    // policy. Native damage intensity is not presumed to be an impulse in Ns.
    // This diagnostic never changes the admission, force or rider state.
    if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&GetEnvironmentVariableA("GTASA_CAPTURE_BIKE_IMPULSE",nullptr,0)){
        void*bike=vehicleFromRef(before->vehicle);if(!bike||!matrix(bike))return;
        const V after=at<V>(bike,0x44)*50.f,prior=before->linear*50.f;
        const V change=after-prior;const float mass=at<float>(bike,0x8C);
        const V local=rotate(conjugate(before->vehicleFrame.q),normalized(change));
        const auto vec=[](V v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
        event("bike_native_impulse_observed",before->vehicle,at<short>(bike,0x22),
            "frame="+std::to_string(frame)+" native_path="+std::to_string(path)+" other_entity="+std::to_string(reinterpret_cast<uintptr_t>(other))+" other_model="+std::to_string(at<short>(other,0x22))+
            " before_mps="+vec(prior)+" after_mps="+vec(after)+" delta_mps="+vec(change)+
            " mass_kg="+std::to_string(mass)+" velocity_impulse_ns="+std::to_string(length(change)*mass)+
            " native_intensity="+std::to_string(intensity)+" local_delta_direction="+vec(local)+
            " up_z="+std::to_string(rotate(before->vehicleFrame.q,{0,0,1}).z)+
            " contact_normal="+vec(point.normal*sign)+" contact_point="+vec(point.point)+" native_flags="+std::to_string(at<unsigned>(bike,0x40))+
            " deferred_output="+std::to_string(outputLinear&&outputAngular)+
            " deferred_linear_delta_mps="+vec(outputLinear?*outputLinear*50.f:V{})+
            " deferred_angular_delta_rps="+vec(outputAngular?*outputAngular*50.f:V{})+
            " deferred_output_is_contact_contribution_not_final_body_velocity=1"+
            " interpretation=measured_native_velocity_change source_impulse_mapping_unverified=1");
    }
}
static const BikeContactRecord* matchedBikeContact(void*bike,float intensity,void*other,V point,V normal){
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);const int ref=vehicleRef(bike);
    for(auto i=bikeContactRecords.rbegin();i!=bikeContactRecords.rend();++i){
        if(frame-i->twist.frame>3)break;
        // Numerical tolerances identify copied native floats. They never alter
        // contact, damage, eligibility, velocity, or any simulated force.
        if(i->twist.vehicle==ref&&i->other==other&&std::abs(i->intensity-intensity)<=std::max(.0001f,std::abs(intensity)*.00001f)
            &&length(i->point-point)<.002f&&length(i->normal-normal)<.002f)return &*i;
    }return nullptr;
}
static bool(__thiscall* originalBikeCollisionOne)(void*,void*,const ColPoint&,float&)=nullptr;
static bool(__thiscall* originalBikeSoftCollisionOne)(void*,void*,const ColPoint&,float&)=nullptr;
static bool(__thiscall* originalBikeCollisionTwo)(void*,void*,ColPoint&,float&,float&)=nullptr;
static bool(__thiscall* originalBikeSoftCollisionTwo)(void*,void*,ColPoint&,float&,float&)=nullptr;
static bool(__thiscall* originalBikeCollisionAlt)(void*,void*,ColPoint&,float&,V&,V&)=nullptr;
static bool __fastcall bikeCollisionOne(void*self,void*,void*other,const ColPoint&point,float&damage){
    NativePairAudit::Scope audit(NativePairAudit::Path::HardSingle,self,other,reinterpret_cast<NativePairAudit::Point*>(const_cast<ColPoint*>(&point)),reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const auto before=beforeBikeContact(self);const bool result=originalBikeCollisionOne(self,other,point,damage);
    audit.finish(reinterpret_cast<NativePairAudit::Point*>(const_cast<ColPoint*>(&point)),result,&damage);
    if(result)afterBikeContact(before,other,point,damage,1,0x5435C0);return result;
}
static bool __fastcall bikeSoftCollisionOne(void*self,void*,void*other,const ColPoint&point,float&damage){
    NativePairAudit::Scope audit(NativePairAudit::Path::SoftSingle,self,other,reinterpret_cast<NativePairAudit::Point*>(const_cast<ColPoint*>(&point)),reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const auto before=beforeBikeContact(self);const bool result=originalBikeSoftCollisionOne(self,other,point,damage);
    audit.finish(reinterpret_cast<NativePairAudit::Point*>(const_cast<ColPoint*>(&point)),result,&damage);
    if(result)afterBikeContact(before,other,point,damage,1,0x543890);return result;
}
static bool __fastcall bikeCollisionTwo(void*self,void*,void*other,ColPoint&point,float&own,float&theirs){
    NativePairAudit::Scope audit(NativePairAudit::Path::HardPair,self,other,reinterpret_cast<NativePairAudit::Point*>(&point),reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const auto a=beforeBikeContact(self),b=beforeBikeContact(other);const bool result=originalBikeCollisionTwo(self,other,point,own,theirs);
    audit.finish(reinterpret_cast<NativePairAudit::Point*>(&point),result,&own,&theirs);
    if(result){afterBikeContact(a,other,point,own,1,0x548680);afterBikeContact(b,self,point,theirs,-1,0x548680);}return result;
}
static bool __fastcall bikeSoftCollisionTwo(void*self,void*,void*other,ColPoint&point,float&own,float&theirs){
    NativePairAudit::Scope audit(NativePairAudit::Path::SoftPair,self,other,reinterpret_cast<NativePairAudit::Point*>(&point),reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const auto a=beforeBikeContact(self),b=beforeBikeContact(other);const bool result=originalBikeSoftCollisionTwo(self,other,point,own,theirs);
    audit.finish(reinterpret_cast<NativePairAudit::Point*>(&point),result,&own,&theirs);
    if(result){afterBikeContact(a,other,point,own,1,0x54A2C0);afterBikeContact(b,self,point,theirs,-1,0x54A2C0);}return result;
}
static bool installBikeContactCallSites(){
    // This executable reads the operand at 0x54A2C1 as DATA in its native
    // 0x406B2A dispatcher. A function-entry detour corrupts that value even
    // with a correct trampoline. Preserve every native function byte and
    // replace only its four verified direct calls in ProcessCollisionSectorList.
    // Setup calls this before gameplay starts; no live frame may execute these
    // instructions while the transaction is being installed.
    struct Site {uintptr_t address;std::array<unsigned char,5> expected;};
    static constexpr Site sites[]={
        {0x54BE1C,{0xE8,0x9F,0xE4,0xFF,0xFF}},
        {0x54C098,{0xE8,0x23,0xE2,0xFF,0xFF}},
        {0x54C35A,{0xE8,0x61,0xDF,0xFF,0xFF}},
        {0x54C54D,{0xE8,0x6E,0xDD,0xFF,0xFF}}
    };
    const auto valid=[](){for(const auto&s:sites)if(std::memcmp(reinterpret_cast<const void*>(s.address),s.expected.data(),5)!=0)return false;return true;};
    if(!valid()){event("bike_contact_calls_refused",-1,-1,"native_call_bytes_changed=1 no_mutation=1");return false;}
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const uintptr_t first=sites[0].address/info.dwPageSize*info.dwPageSize;
    const uintptr_t end=(sites[3].address+5+info.dwPageSize-1)/info.dwPageSize*info.dwPageSize;
    // One protection transaction is safe only if every covered page has the
    // same original protection. Do not accidentally change another mod's page.
    DWORD protection=0;
    for(uintptr_t page=first;page<end;page+=info.dwPageSize){
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<void*>(page),&region,sizeof(region))||region.State!=MEM_COMMIT||
            (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||(protection&&protection!=region.Protect)){
            event("bike_contact_calls_refused",-1,-1,"incompatible_code_pages=1 no_mutation=1");return false;
        }
        protection=region.Protect;
    }
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(first),end-first,PAGE_EXECUTE_READWRITE,&old)){
        event("bike_contact_calls_refused",-1,-1,"protect_failed=1 no_mutation=1");return false;
    }
    DWORD unused=0;
    if(!valid()){
        const bool restored=VirtualProtect(reinterpret_cast<void*>(first),end-first,protection,&unused)!=0;
        event("bike_contact_calls_refused",-1,-1,restored?"concurrent_patch=1 no_mutation=1":"concurrent_patch=1 protection_restore_failed=1");return false;
    }
    originalBikeSoftCollisionTwo=reinterpret_cast<decltype(originalBikeSoftCollisionTwo)>(0x54A2C0);
    // setup() runs concurrently with the main thread: write with it suspended.
    const std::array<CodeRange,4> ranges{{{sites[0].address,sites[0].address+5},{sites[1].address,sites[1].address+5},{sites[2].address,sites[2].address+5},{sites[3].address,sites[3].address+5}}};
    bool frozen=false,flushed=false;
    {
        FrozenThreads freeze(ranges);frozen=freeze.ok();
        if(frozen){
            for(const auto&s:sites){
                auto bytes=s.expected;const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&bikeSoftCollisionTwo)-(s.address+5));
                std::memcpy(bytes.data()+1,&relative,sizeof(relative));std::memcpy(reinterpret_cast<void*>(s.address),bytes.data(),bytes.size());
            }
            flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(first),end-first)!=0;
        }
    }
    if(!frozen){
        const bool restored=VirtualProtect(reinterpret_cast<void*>(first),end-first,protection,&unused)!=0;
        event("bike_contact_calls_refused",-1,-1,std::string("thread_freeze_failed=1 no_mutation=1 protection_restored=")+(restored?"1":"0"));return false;
    }
    if(flushed&&VirtualProtect(reinterpret_cast<void*>(first),end-first,protection,&unused)){
        event("bike_contact_calls_installed",-1,-1,"verified_calls=4 native_entry_preserved=1");return true;
    }
    // VirtualProtect failure leaves the committed region unchanged. The pages
    // are therefore still writable here; restore all original calls before
    // returning a failed setup result, then retry the original protection.
    {FrozenThreads freeze(ranges);for(const auto&s:sites)std::memcpy(reinterpret_cast<void*>(s.address),s.expected.data(),s.expected.size());}
    const bool rollbackFlushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(first),end-first)!=0;
    const bool rollbackProtected=VirtualProtect(reinterpret_cast<void*>(first),end-first,protection,&unused)!=0;
    event("bike_contact_calls_rolled_back",-1,-1,"cache_flushed="+std::to_string(rollbackFlushed)+" protection_restored="+std::to_string(rollbackProtected));
    return false;
}
static bool __fastcall bikeCollisionAlt(void*self,void*,void*other,ColPoint&point,float&damage,V&linear,V&angular){
    NativePairAudit::Scope audit(NativePairAudit::Path::Alt,self,other,reinterpret_cast<NativePairAudit::Point*>(&point),reinterpret_cast<uintptr_t>(_ReturnAddress()));
    // Native ProcessCollisionSectorList initializes these accumulators, adds
    // each accepted contact, then divides by the accepted contact count. The
    // callback's output is not a velocity already committed to the bike.
    const V linearBefore=linear,angularBefore=angular;
    const auto before=beforeBikeContact(self);const bool result=originalBikeCollisionAlt(self,other,point,damage,linear,angular);
    audit.finish(reinterpret_cast<NativePairAudit::Point*>(&point),result,&damage,nullptr,reinterpret_cast<NativePairAudit::Vec*>(&linear),reinterpret_cast<NativePairAudit::Vec*>(&angular));
    if(result){const V linearDelta=linear-linearBefore,angularDelta=angular-angularBefore;
        afterBikeContact(before,other,point,damage,1,0x544D50,&linearDelta,&angularDelta);}return result;
}
static bool(__cdecl* originalBikeDamageKnockOff)(void*,float,unsigned short,void*,const V&,const V&)=nullptr;
static bool __cdecl bikeDamageKnockOff(void*bike,float intensity,unsigned short piece,void*other,const V&point,const V&normal){
    const auto*matched=matchedBikeContact(bike,intensity,other,point,normal);
    const std::optional<BikeContactRecord> sample=matched?std::optional<BikeContactRecord>(*matched):std::nullopt;
    // Original event copies the now-postcollision moveSpeed. Keep it only as
    // event identity; it is specifically NOT the initial physical velocity.
    const V eventVelocity=at<V>(bike,0x44);
    const bool accepted=originalBikeDamageKnockOff(bike,intensity,piece,other,point,normal);
    if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&GetEnvironmentVariableA("GTASA_CAPTURE_BIKE_IMPULSE",nullptr,0)){
        void*driver=at<void*>(bike,0x460);const unsigned char flags=driver?at<unsigned char>(driver,0x477):0;
        event("bike_native_knockoff_decision",driver?reference(driver):-1,driver?at<short>(driver,0x22):-1,
            "bike="+std::to_string(vehicleRef(bike))+" accepted="+std::to_string(accepted)+" intensity="+std::to_string(intensity)+
            " piece="+std::to_string(piece)+" status="+std::to_string(at<unsigned char>(bike,0x36)>>3)+
            " ped_state="+std::to_string(driver?at<unsigned>(driver,0x530):0)+" knockoff_policy="+std::to_string((flags>>3)&3)+
            " rendered="+std::to_string((flags>>5)&1)+" contact_snapshot="+std::to_string(bool(sample))+
            " post_contact_mps="+std::to_string(length(eventVelocity)*50.f));
    }
    if(!accepted)return false;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(size_t offset:{size_t(0x460),size_t(0x464)})if(void*ped=at<void*>(bike,offset);ped&&ridingBike(ped)&&at<void*>(ped,0x58C)==bike){
        const int ref=reference(ped);bikeImpactAdmissions.erase(ref);
        if(!sample){event("bike_preimpact_contact_missing",ref,at<short>(ped,0x22),"native_event_accepted=1 no_velocity_guess=1");continue;}
        std::shared_ptr<BikeContactPose> rider;
        for(const auto&candidate:sample->twist.riders)if(candidate&&candidate->ped==ref){rider=candidate;break;}
        std::string fitReason="no_rider_snapshot";auto pose=rider?fitBikeContactPose(ped,*rider,&fitReason):nullptr;
        if(!pose){
            BikePoseDiagnostic d;for(const auto&candidate:sample->twist.riderDiagnostics)if(candidate.ped==ref){d=candidate;break;}
            event("bike_preimpact_pose_missing",ref,at<short>(ped,0x22),"no_same_contact_pose=1 source_frame="+std::to_string(sample->twist.frame)+
                " reason="+d.reason+" fit_reason="+fitReason+" contact_registry_frame="+std::to_string(d.registryFrame)+
                " authored_frame="+std::to_string(d.authoredFrame)+" seat_frame="+std::to_string(d.seatFrame)+
                " native_frame="+std::to_string(d.nativeFrame)+" native_serial="+std::to_string(d.nativeSerial)+
                " root_source="+bikeRootSourceName(d.rootSource)+" root_order="+std::to_string(d.rootOrder)+
                " registered="+std::to_string(d.registered)+" requested="+std::to_string(d.requested)+
                " ped_area="+std::to_string(d.pedArea)+" world_area="+std::to_string(d.worldArea)+
                " authored_calls="+std::to_string(d.progress.authoredCalls)+" seat_calls="+std::to_string(d.progress.seatCalls)+
                " authored_result="+d.progress.authoredResult+" authored_order="+std::to_string(d.progress.authoredOrder)+" seat_order="+std::to_string(d.progress.seatOrder));continue;
        }
        // Both pose and rigid twist precede the SAME qualifying native impulse.
        // Keep the candidate contact matrix and COM; the next-frame handler's
        // stopped/restored matrix cannot replace the physical initial state.
        pose->velocity=sample->twist.linear;pose->angular=sample->twist.angular;
        pose->centre=sample->twist.centre;pose->motionEpoch=sample->twist.epoch;
        bikeImpactAdmissions[ref]={pose,normal,eventVelocity,intensity,frame};
        event("bike_preimpact_contact_matched",ref,at<short>(ped,0x22),"source_frame="+std::to_string(sample->twist.frame)+" event_frame="+std::to_string(frame)+
            " vx="+std::to_string(pose->velocity.x*50)+" vy="+std::to_string(pose->velocity.y*50)+" vz="+std::to_string(pose->velocity.z*50)+
            " spin="+std::to_string(length(pose->angular)*50)+" event_speed="+std::to_string(length(eventVelocity)*50)+
            " pose_frame="+std::to_string(pose->poseEpoch.frame)+" pose_serial="+std::to_string(pose->poseEpoch.serial)+" motion_serial="+std::to_string(pose->motionEpoch.serial)+
            " root_source="+bikeRootSourceName(rider->rootSource)+" root_frame="+std::to_string(rider->rootFrame)+
            " root_order="+std::to_string(rider->rootOrder)+" authored_native_serial="+std::to_string(rider->nativeSerial)+
            " native_step_seconds="+std::to_string(sample->twist.epoch.nativeStepSeconds)+" frame_step_seconds="+std::to_string(sample->twist.epoch.frameStepSeconds)+
            " intensity="+std::to_string(intensity)+" source_entity="+std::to_string(uintptr_t(other))+" source_model="+std::to_string(other?at<short>(other,0x22):-1)+
            " px="+std::to_string(point.x)+" py="+std::to_string(point.y)+" pz="+std::to_string(point.z)+
            " nx="+std::to_string(normal.x)+" ny="+std::to_string(normal.y)+" nz="+std::to_string(normal.z));
    }
    return true;
}
static std::shared_ptr<PoseSeed> nativeBikeImpactPose(void*ev,void*ped){
    auto i=bikeImpactAdmissions.find(reference(ped));if(i==bikeImpactAdmissions.end())return {};
    const auto&entry=i->second;const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(frame-entry.frame>2||vehicleFromRef(entry.pose->vehicle)!=at<void*>(ev,0x38)||
        length(entry.normal-at<V>(ev,0x18))>.002f||std::abs(entry.intensity-at<float>(ev,0x24))>std::max(.0001f,std::abs(entry.intensity)*.00001f)||
        length(entry.eventVelocity-at<V>(ev,0xC))>.0001f)return {};
    return entry.pose;
}
