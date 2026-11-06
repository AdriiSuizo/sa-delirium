#pragma once
// The reference host supplies an animated IDLE zero pose to NM. A skin bind
// pose is only an assembly reference, not a character's neutral motor target.
// Observe SA's already evaluated idle; never install an idle or edit tasks.
struct NativeIdlePose {void*ped=nullptr;void*clump=nullptr;int model=-1;unsigned time=0;std::vector<int>ids;std::vector<T>bones;};
static std::map<int,NativeIdlePose> nativeIdlePoses;
static void sampleNativeIdlePose(void*p){
    const int ref=reference(p);if(ref<0||agents.contains(ref)||pending.contains(ref)||!matrix(p)||!player()||inVehicle(p)||at<float>(p,0x540)<=0||length(position(p)-position(player()))>radius+5.f)return;
    if(length(at<V>(p,0x44))>.003f)return;
    auto*clump=at<void*>(p,0x18);if(!clump)return;
    // SDK ANIM_DEFAULT_IDLE_STANCE=3 and CAnimBlendAssociation::blend=0x18.
    auto*idle=reinterpret_cast<void*(__cdecl*)(void*,unsigned)>(0x4D68B0)(clump,3);
    if(!idle||at<float>(idle,0x18)<.99f)return;
    // A full-weight idle can still sit under another clip (Ryder's script
    // smoking overlay bent the head 25 degrees): that is not a neutral pose.
    {void*assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump);
     for(int count=0;assoc&&count<64;count++){
         const short id=at<short>(assoc,0x2C);
         const bool facial=at<unsigned short>(assoc,0xE)==0&&id>=154&&id<=161; // FACSURP..FACGUM: face only
         if(assoc!=idle&&!facial&&at<float>(assoc,0x18)>.05f)return;
         assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
     }}
    auto*intelligence=at<void*>(p,0x47C);
    if(intelligence&&at<void*>(static_cast<unsigned char*>(intelligence)+4,0x14))return; // secondary attack task
    auto*h=hierarchy(p);if(!h||!h->nodes||!h->matrices||h->num<20||h->num>128)return;
    NativeIdlePose sample;sample.ped=p;sample.clump=clump;sample.model=at<short>(p,0x22);sample.time=gameTime();
    for(int n=0;n<h->num;n++){
        T t=transform(h->matrices[n]);if(!finite(t.p)||length(t.p-position(p))>3.f)return;
        sample.ids.push_back(h->nodes[n].id);sample.bones.push_back(t);
    }
    nativeIdlePoses[ref]=std::move(sample);
    for(auto it=nativeIdlePoses.begin();it!=nativeIdlePoses.end();)
        if(gameTime()-it->second.time>30000||pedFromRef(it->first)!=it->second.ped)it=nativeIdlePoses.erase(it);else ++it;
}
static void applyNativeIdlePose(Active&a){
    const auto it=nativeIdlePoses.find(a.ref);
    if(it==nativeIdlePoses.end()||it->second.ped!=a.ped||it->second.clump!=at<void*>(a.ped,0x18)||it->second.model!=a.model||it->second.ids!=a.rig->ids||gameTime()-it->second.time>30000){
        event("native_idle_pose_unavailable",a.ref,a.model,"bind_fallback_not_idle_certified");return;
    }
    const auto joints=a.rig->jointFrames(it->second.bones);std::array<float,147>rows{};
    for(int n=0;n<21;n++){
        const auto t=mul(joints[n],a.rig->offsets[n]);
        const float row[]={t.p.x,t.p.y,t.p.z,t.q.x,t.q.y,t.q.z,t.q.w};std::copy(row,row+7,rows.begin()+n*7);
    }
    if(ok(nm_agent_set_zero_transforms(a.agent,rows.data(),21)))
        event("native_idle_pose_applied",a.ref,a.model,"observed_anim=3 age_ms="+std::to_string(gameTime()-it->second.time));
}
