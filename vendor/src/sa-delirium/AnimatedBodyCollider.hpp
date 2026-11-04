#pragma once
// Fitted geometry cache only. These colliders have no NM behaviour or simulated
// velocities; native animation remains the owner of the walking pedestrian.
struct AnimatedBodyCollider {
    void*ped=nullptr;void*clump=nullptr;int model=-1;unsigned used=0;
    std::unique_ptr<Rig>rig;NmPoseCollider*shape=nullptr;
    ~AnimatedBodyCollider(){if(shape)nm_pose_collider_destroy(shape);}
};
static std::map<int,std::unique_ptr<AnimatedBodyCollider>> animatedBodyColliders;
static void clearAnimatedBodyColliders(){animatedBodyColliders.clear();}
static AnimatedBodyCollider* bodyCollider(void*p){
    const int ref=reference(p),model=at<short>(p,0x22);void*clump=at<void*>(p,0x18);
    if(ref<0||!clump||!sourceBuild)return nullptr;
    auto it=animatedBodyColliders.find(ref);
    if(it!=animatedBodyColliders.end()&&(it->second->ped!=p||it->second->model!=model||it->second->clump!=clump)){
        animatedBodyColliders.erase(it);it=animatedBodyColliders.end();
    }
    if(it==animatedBodyColliders.end())try{
        auto c=std::make_unique<AnimatedBodyCollider>();c->ped=p;c->clump=clump;c->model=model;
        c->rig=std::make_unique<Rig>(p,sourceBuild);c->shape=nm_pose_collider_create(c->rig->build);
        if(!c->shape)return nullptr;it=animatedBodyColliders.emplace(ref,std::move(c)).first;
    }catch(const std::exception&e){event("animated_collider_rejected",ref,model,e.what());return nullptr;}
    it->second->used=gameTime();return it->second.get();
}
static bool setAnimatedBodyPose(AnimatedBodyCollider&body,const AnimatedContactPose&cache){
    if(cache.bones.size()!=body.rig->ids.size())return false;
    const V shift=position(body.ped)-cache.root;if(length(shift)>.5f)return false;
    const auto joints=body.rig->jointFrames(cache.bones);std::array<float,21*13> states{};
    const V velocity=at<V>(body.ped,0x44)*50.f;
    for(int k=0;k<21;k++){
        T part=mul(joints[k],body.rig->offsets[k]);part.p=part.p+shift;
        const float row[]={part.p.x,part.p.y,part.p.z,part.q.x,part.q.y,part.q.z,part.q.w,
                           velocity.x,velocity.y,velocity.z,0.f,0.f,0.f};
        std::copy(std::begin(row),std::end(row),states.begin()+13*k);
    }
    return ok(nm_pose_collider_set_pose(body.shape,states.data(),21));
}
// Contact correction happens after native PreRender. Publish the same rigid
// translation to this frame's skin and query cache, preserving authored local
// rotations, procedural IK and all older motion-history samples.
static void translateAnimatedBodyPose(void*p,AnimatedContactPose&cache,V delta){
    const unsigned now=gameTime(),frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    const int ref=reference(p);
    if(p!=cache.ped||cache.time!=now||evaluatedPedFrame!=frame||
       !evaluatedPeds.contains(ref)||evaluatedPeds.at(ref)!=p||!finite(delta))return;
    if(auto*h=hierarchy(p);h&&h->matrices&&h->num==int(cache.bones.size()))
        for(int k=0;k<h->num;k++)h->matrices[k].pos=h->matrices[k].pos+delta;
    cache.root=cache.root+delta;
    for(auto&t:cache.bones)t.p=t.p+delta;
    auto shiftRows=[&](std::vector<PeerCapsule>&rows){for(auto&r:rows){
        r[0]+=delta.x;r[1]+=delta.y;r[2]+=delta.z;
        r[3]+=delta.x;r[4]+=delta.y;r[5]+=delta.z;
    }};
    shiftRows(cache.rows);
    for(auto&sample:cache.history)if(sample.time==now)shiftRows(sample.rows);
}
static bool physicalContactStates(Active&a,std::array<float,21*13>&states){
    const auto&pose=contactPose(a);
    for(int k=0;k<21;k++){
        V v{},w{};if(a.recovering){v=a.renderVelocity[k];w=a.renderSpin[k];}
        else if(!ok(nm_agent_get_part_velocity(a.agent,k,&v.x,&w.x)))return false;
        const auto&t=pose[k];const float row[]={t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w,v.x,v.y,v.z,w.x,w.y,w.z};
        std::copy(std::begin(row),std::end(row),states.begin()+13*k);
    }return true;
}
