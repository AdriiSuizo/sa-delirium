#pragma once
// Normal-owner contacts now share the same finite island as articulated NPCs.
// This file observes solved receipts for behaviour. It never solves another
// ped pair, moves a root by ray, or adds the impulse a second time.
static std::map<std::pair<int,int>,unsigned> standingContactLog;
static void captureStandingContacts(){
    animatedNativePairOwners.clear();if(!recoveryAllowed())return;
    auto*pool=pedPool();const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(int i=0;pool&&i<pool->size;i++)if(!(pool->flags[i]&128)){
        void*p=pool->objects+i*0x7C4;const int ref=reference(p);
        if(ref<0||!validPed(p,false,false,p==player())||inVehicle(p))continue;
        if(const auto a=agents.find(ref);a!=agents.end()){
            if(!a->second->recovering||!a->second->recoveryAnimatedOwner)continue;
        }
        auto*cache=bodyCollider(p);if(!cache||!previousFinalPedPose(p,cache->rig->ids))continue;
        animatedNativePairOwners[ref]={p,at<void*>(p,0x18),at<short>(p,0x22),frame};
    }
}
static int(__thiscall* originalStandingEntityCollision)(void*,void*,ColPoint*)=nullptr;
static int __fastcall standingEntityCollision(void*p,void*,void*other,ColPoint*points){
    if(suppressOriginalGroundCollision(p,other,reinterpret_cast<uintptr_t>(_ReturnAddress())))return 0;
    if(recoveryAllowed()&&animatedNativePairCurrent(p)&&animatedNativePairCurrent(other)&&
       animatedExternalEligible(p)&&animatedExternalEligible(other))return 0;
    return originalStandingEntityCollision(p,other,points);
}
static void processStandingContacts(float dt){(void)dt;} // replaced in shared island
// IV (peds.rs touched): the victim's fragment capsules as last drawn, moved on
// by this frame's walk (its velocity * dt), against the player's FRAGMENT parts.
static std::vector<float> standingFragmentCapsules(void*victim,V shift){
    std::vector<PeerCapsule> rows;
    if(const auto a=agents.find(reference(victim));a!=agents.end())rows=animatedPedCapsules(victim,a->second.get());
    else rows=animatedPedCapsules(victim);
    std::vector<float> capsules;
    for(const auto&r:rows){capsules.insert(capsules.end(),{r[0]+shift.x,r[1]+shift.y,r[2]+shift.z,r[3]+shift.x,r[4]+shift.y,r[5]+shift.z,r[6]});}
    return capsules;
}
static bool standingReceiptMeetsFragment(const std::vector<float>&capsules,V peerRoot){
    return !capsules.empty()&&capsules.size()<=32*7&&nm_ped_meets_fragment(&peerRoot.x,capsules.data(),int(capsules.size()/7))==1;
}
static bool standingReceiptMeetsFragment(void*victim,V peerRoot){return standingReceiptMeetsFragment(standingFragmentCapsules(victim,{}),peerRoot);}
static void observeSolvedExternalPairs(NmGroup*group,float dt){
    const int needed=nm_group_external_contacts(group,nullptr,nullptr,0);if(needed<=0){if(needed<0)ok(needed);return;}
    std::vector<uint64_t> ids(size_t(needed)*2);std::vector<float> rows(size_t(needed)*17);
    const int count=nm_group_external_contacts(group,ids.data(),rows.data(),needed);if(count<0){ok(count);return;}
    for(int n=0;n<count;n++)auditSolvedExternalPair(ids[n*2],ids[n*2+1],rows.data()+n*17,dt);
    for(int n=0;n<count;n++)for(int side=0;side<2;side++){
        const auto subject=ids[n*2+side],peer=ids[n*2+1-side];if(subject>>32!=2||peer>>32!=2)continue;
        void*p=pedFromRef(int(uint32_t(subject))),*other=pedFromRef(int(uint32_t(peer)));if(!p||!other||p==other)continue;
        const float*r=rows.data()+n*17;const V j{r[0],r[1],r[2]},normal=V{r[6],r[7],r[8]}*(side?-1.f:1.f);
        const V ownVelocity{r[side?12:9],r[side?13:10],r[side?14:11]};
        const V otherVelocity{r[side?9:12],r[side?10:13],r[side?11:14]};
        const float impulse=length(j),closing=dot(otherVelocity-ownVelocity,normal);if(impulse<=0)continue;
        const auto key=std::make_pair(reference(p),reference(other));
        if(gameTime()-standingContactLog[key]>=200){standingContactLog[key]=gameTime();event("standing_owner_contact",key.first,at<short>(p,0x22),
            "other="+std::to_string(key.second)+" impulse_ns="+std::to_string(impulse)+" closing_mps="+std::to_string(closing)+" owner_mass_kg=25 shared_solve=1");}
        // Original reason10 requires the moving PLAYER and actual fragment
        // overlap after the normal collider receipt. NPC/NPC's collision event
        // has no response handler; its physical rows still transmit momentum.
        if(other!=player()||p==player())continue;
        auto*peerRig=bodyCollider(other);
        // IV evaluates reason 10 at the player's post-walk place BEFORE the
        // body pair is pushed apart. The staged owner row of this substep is
        // that place; position(other) is already the separated result.
        // Then the player's walk of this step is added (staged velocity * dt),
        // and the victim's capsules are moved on by its own velocity * dt.
        // IV's walk is the whole frame's (the bodies' substeps run after it):
        // the native frame step (CTimer::ms_fTimeStep, 1/50 s units).
        // The reference is the 30 Hz console frame (1/30 s, as in the IV
        // sources); SA runs far faster here (~0.01 s), which would make the
        // same walk never reach the limbs. Use at least one IV frame.
        const float frameDt=std::clamp(*reinterpret_cast<float*>(0xB7CB5C)/50.f,1.f/30.f,.1f);
        V peerRoot=position(other)+V{0,0,1.f-(peerRig?peerRig->rig->rootClearance:1.f)};
        {const uint64_t id=animatedExternalId(reference(other));
         for(const auto&o:externalHostOwners)if(o.id==id&&o.animated){peerRoot=V{o.rows[0],o.rows[1],o.rows[2]}+V{o.rows[3],o.rows[4],0}*frameDt;break;}}
        // The victim's own walk (its staged animated velocity), never the
        // separation velocity this solve just gave it.
        V victimWalk{};
        {const uint64_t id=animatedExternalId(reference(p));
         // A still ped (CPed move state 0/1) has no walk: its owner velocity
         // is then only the push the bodies just gave it (measured 2.5 m/s).
         const int move=at<int>(p,0x534);
         if(move>1)for(const auto&o:externalHostOwners)if(o.id==id&&o.animated){victimWalk={o.rows[3],o.rows[4],0};break;}}
        const auto fragment=standingFragmentCapsules(p,victimWalk*frameDt);
        const bool fast=dot(otherVelocity,otherVelocity)>.25f,meets=peerRig&&standingReceiptMeetsFragment(fragment,peerRoot);
        // IV posts the player's CEventPlayerCollisionWithPed from the body
        // contact record alone (peds.rs 3745..3790), apart from the ped's NM
        // wake below: a slow or body-only touch still shoves.
        if(closing>0&&!agents.contains(reference(p))&&!combatOwns(p)&&!combatOwns(other))startPlayerShove(p);
        if(!fast||closing<=0||!meets)continue;
        if(auto a=agents.find(reference(p));a!=agents.end()){
            if(a->second->recoveryAnimatedOwner)acceptRecoveryPlayerContact(*a->second,other,impulse,dt,otherVelocity);
            continue;
        }
        if(combatOwns(p)||combatOwns(other)||pending.contains(reference(p)))continue;
        Request request;request.kind=Reaction::Bump;request.time=gameTime();request.attackerRef=reference(other);
        request.direction=normal;request.bumpImpulse=impulse;request.bumpDt=dt;request.velocityOverride=true;request.velocity=at<V>(p,0x44);
        pending[reference(p)]=request;event("standing_bump_requested",reference(p),at<short>(p,0x22),"shared_impulse_already_solved=1 health_unchanged=1");
    }
    for(auto i=standingContactLog.begin();i!=standingContactLog.end();)if(gameTime()-i->second>5000)i=standingContactLog.erase(i);else ++i;
}
