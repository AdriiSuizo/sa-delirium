#pragma once
// Pure host facts for the original phInstGta owner. It is registered once per
// current physics frame in the shared scene, not copied into every NM agent.
// All shape contacts refer to this same generation-bearing identity.
struct OriginalAnimatedOwnerSample {
    int ref=-1;void*ped=nullptr;void*clump=nullptr;int model=-1;
    unsigned frame=0;V root{},velocity{};float heading=0;
};
static bool captureOriginalAnimatedOwner(void*p,float nativeClearance,OriginalAnimatedOwnerSample&out){
    if(!p||!matrix(p)||!std::isfinite(nativeClearance)||nativeClearance<=0)return false;
    const int ref=reference(p);const V root=position(p),velocity=at<V>(p,0x44)*50.f;
    const V forward=matrix(p)->up; // SA CMatrix up is its horizontal local +Y.
    if(ref<0||pedFromRef(ref)!=p||!finite(root)||!finite(velocity)||!finite(forward))return false;
    // IV BlendFromNM places its owner root one metre above the ground
    // (8261DF30 onward). SA's native origin clearance is rig-specific. This
    // coordinate conversion preserves the same ground position; it does not
    // move the native ped or infer placement from the currently lying pelvis.
    out={ref,p,at<void*>(p,0x18),at<short>(p,0x22),*reinterpret_cast<unsigned*>(0xB7CB4C),
         root+V{0,0,1.f-nativeClearance},velocity,std::atan2(-forward.x,forward.y)};
    return true;
}
static bool originalAnimatedSampleCurrent(const OriginalAnimatedOwnerSample&s){
    return s.ped&&pedFromRef(s.ref)==s.ped&&s.frame==*reinterpret_cast<unsigned*>(0xB7CB4C)&&
        at<void*>(s.ped,0x18)==s.clump&&at<short>(s.ped,0x22)==s.model;
}
// Adapter must call capture only for an actual animated owner:
//  * ordinary walking/standing actor; or
//  * BlendFromNM AFTER original fit + room validation + committed placement.
// While seeking a fit/room or sampling clips, retain the original NM owner and
// relaxation. Neither `recovering` alone nor an SA animation stage is proof.
// An accepted semantic ComplexNM response hands authority back atomically,
// preserving current displayed pose, velocities and body generation.
// Animated locomotion mobility: L=I/25kg, angular=0. The native SA mass does not
// redefine that source contract: commit the solved delta-V exactly once.
// 21 articulated parts remain damage/targeting geometry only while animated.

// Same validated cohort owns native-pair replacement and the later island.
// New/changed ordinary peds keep native collision until a completed palette
// proves their identity/rig; a newly placed get-up already owns no SA capsule.
struct AnimatedNativePairOwner {void*ped=nullptr;void*clump=nullptr;int model=-1;unsigned frame=0;};
static std::map<int,AnimatedNativePairOwner> animatedNativePairOwners;
static bool animatedNativePairCurrent(void*p){
    if(!p)return false;const auto i=animatedNativePairOwners.find(reference(p));if(i==animatedNativePairOwners.end())return false;
    const auto&s=i->second;return s.ped==p&&pedFromRef(i->first)==p&&s.clump==at<void*>(p,0x18)&&s.model==at<short>(p,0x22)&&s.frame==*reinterpret_cast<unsigned*>(0xB7CB4C);
}
static uint64_t animatedExternalId(int ref){return (2ull<<32)|uint32_t(ref);}
static bool animatedExternalEligible(void*p){
    if(!p||!matrix(p)||inVehicle(p)||!validPed(p,false,false,p==player()))return false;
    const auto active=agents.find(reference(p));
    return active==agents.end()?animatedNativePairCurrent(p):(active->second->ped==p&&active->second->recovering&&active->second->recoveryAnimatedOwner);
}
static void appendAnimatedExternalOwners(std::vector<HostExternalOwner>&owners){
    if(evaluatedPedFrame!=*reinterpret_cast<unsigned*>(0xB7CB4C))return;
    for(const auto&[ref,p]:evaluatedPeds){
        if(pedFromRef(ref)!=p||!animatedExternalEligible(p))continue;
        auto*cache=bodyCollider(p);if(!cache)continue;
        float clearance=cache->rig->rootClearance;
        if(const auto a=agents.find(ref);a!=agents.end()&&a->second->nativeGroundClearance>0)clearance=a->second->nativeGroundClearance;
        OriginalAnimatedOwnerSample s;if(!captureOriginalAnimatedOwner(p,clearance,s))continue;
        HostExternalOwner o{animatedExternalId(ref),ref,p,s.clump,s.model,true,{}};
        const auto put=[&](int i,V v){o.rows[i]=v.x;o.rows[i+1]=v.y;o.rows[i+2]=v.z;};
        put(0,s.root);put(3,s.velocity);o.rows[9]=o.rows[13]=o.rows[17]=1.f/25.f;
        // A=0 is the verified constrained-collider rotation lock. The one
        // root velocity is shared across all four shapes and all NM agents.
        owners.push_back(o);
    }
}
static bool appendAnimatedExternalObstacles(NmWorld*world,const std::vector<HostExternalOwner>&owners){
    for(const auto&o:owners)if(o.animated){
        if(pedFromRef(o.ref)!=o.entity||!animatedExternalEligible(o.entity)||
           at<void*>(o.entity,0x18)!=o.clump||at<short>(o.entity,0x22)!=o.model)return false;
        const V forward=rotate(o.staged.centre.q,{0,1,0});const float heading=std::atan2(-forward.x,forward.y);
        if(!ok(nm_world_append_animated_owner(world,o.id,o.rows.data(),heading,o.rows.data()+3)))return false;
    }return true;
}
static void commitAnimatedExternalOwner(const HostExternalOwner&o,const float*r,const HostPredictorCorrection&correction,V moved,int blocked,bool fallback,float dt){
    void*p=o.entity;
    const bool sourceGround=originalGroundActive(p);
    const V impulse{r[0],r[1],r[2]},drive=stagedExternalDrive(o.id)+originalGroundDrive(o.id),contactImpulse=impulse-drive,dv=correction.deltaLinear;
    const V before=position(p),wish=correction.translation;
    // All result identities/poses and the accepted placement were preflighted
    // before ANY owner was changed. Core placement is total from acceptedBase;
    // the legacy fallback sweep, when explicitly used, has the same contract.
    at<V>(p,0x44)=at<V>(p,0x44)+dv*.02f;
    matrix(p)->pos=before+moved;reinterpret_cast<void(__thiscall*)(void*)>(0x542560)(p);
    recordNativeGroundOwnerCommit(p,before,before+moved,dv);
    commitOriginalGround(o);
    auto active=agents.find(o.ref);
    if(active!=agents.end()&&active->second->recoveryAnimatedOwner){auto&a=*active->second;
        a.recoveryVelocity=a.recoveryVelocity+dv;
        for(auto&t:a.renderedParts)t.p=t.p+moved;
        for(auto&t:a.lastRendered)t.p=t.p+moved;
        for(auto&t:a.frozenBones)t.p=t.p+moved;
        for(auto&t:a.bones)t.p=t.p+moved;
        for(auto&v:a.renderVelocity)v=v+dv;
        if(auto*h=hierarchy(p);h&&h->matrices)for(int i=0;i<h->num;i++)h->matrices[i].pos=h->matrices[i].pos+moved;
    }else if(auto cache=animatedContacts.find(o.ref);cache!=animatedContacts.end())translateAnimatedBodyPose(p,cache->second,moved);
    else if(auto*h=hierarchy(p);h&&h->matrices)for(int i=0;i<h->num;i++)h->matrices[i].pos=h->matrices[i].pos+moved;
    if(length(impulse)>0||length(wish)>1.e-6f){
        // The motor is an external drive, not evidence of touching another
        // body. Preserve total force in the commit, separate it in telemetry.
        if(length(contactImpulse)>1.e-6f)traceSolidBodyContact(o.ref,-1,before,wish,moved,blocked,length(contactImpulse),0,0);
        static std::map<uint64_t,unsigned> reports;
        if(gameTime()-reports[o.id]>=200){reports[o.id]=gameTime();
            event("animated_owner_committed",o.ref,o.model,"mass_kg=25 impulse_ns="+std::to_string(length(impulse))+" drive_impulse_ns="+std::to_string(length(drive))+" contact_impulse_ns="+std::to_string(length(contactImpulse))+" requested_m="+std::to_string(length(wish))+" moved_m="+std::to_string(length(moved))+" solid_contacts="+std::to_string(blocked)+
                " fraction="+std::to_string(o.fraction)+" accumulated_velocity_integrated=1 fallback_world_sweep="+std::to_string(fallback)+" native_ground_support="+std::to_string(!sourceGround)+" source_world_verified="+std::to_string(sourceGround)+" articulated_animation_energy=0");}
    }
}
// Called with a solved per-contact receipt, before vehicle-only filtering.
// A receipt selects behaviour; its impulse has ALREADY entered the shared
// island and must never be sent through ApplyMoveForce again.
static void observeSolvedAnimatedContact(Active&a,int obstacle,V impulse,V point,float dt){
    const auto id=nm_world_obstacle_owner(collision->world,obstacle);if(id>>32!=2)return;
    void*peer=pedFromRef(int(uint32_t(id)));if(!peer||peer==a.ped||!finite(impulse)||dt<=0)return;
    const float amount=length(impulse);if(amount<=0)return;
    if(peer==player()){
        if(a.recovering)acceptRecoveryPlayerContact(a,peer,amount,dt,at<V>(peer,0x44)*50.f);
        if(!a.recovering)ok(nm_agent_ped_bump_touch(a.agent));
        if(a.current[10].p.z-a.current[0].p.z>.30f)startPlayerShove(a.ped);
    }
    // Recovery selection consumes a semantic BlendFromNM context in the
    // response bridge. Geometry alone cannot manufacture CatchFall or cancel
    // a native get-up. Ordinary contact still transmits physical momentum.
    static std::map<std::pair<int,int>,unsigned> reports;const auto key=std::make_pair(a.ref,reference(peer));
    if(gameTime()-reports[key]>=150){reports[key]=gameTime();event("animated_owner_contact_receipt",a.ref,a.model,
        "peer="+std::to_string(reference(peer))+" impulse_ns="+std::to_string(amount)+" physics_applied_once=1 recovery="+std::to_string(a.recovering)+" point_z="+std::to_string(point.z));}
}
