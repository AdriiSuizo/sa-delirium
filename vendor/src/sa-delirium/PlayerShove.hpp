#pragma once
// A secondary tracked-channel clip. It never owns locomotion or creates a hit.
static int shoveLastPeer=-1,shovePlayer=-1,shoveClip=0;
static std::unique_ptr<Rig> shoveRig;
static std::vector<unsigned> shoveFrames;
static int shoveMove(void*p){const int s=at<int>(p,0x534);return s==7?4:s==5||s==6?3:s==4?2:1;}
static void startPlayerShove(void*other){
    auto*p=player();if(!combatAllowed()||!p||!matrix(other)||!matrix(p)||!validPed(other)||
        agents.contains(reference(p))||combatOwns(p)||combatOwns(other)||inVehicle(p)||shoveClip)return;
    const int type=at<int>(other,0x598);const unsigned bit=type>=0&&type<32?1u<<type:0;
    if((at<unsigned>(p,0x4EC)|at<unsigned>(p,0x4F0))&bit)return; // dislike/hate: native combat
    const V forward=matrix(p)->up,otherForward=matrix(other)->up,d=position(other)-position(p),right=matrix(p)->right;
    const float r[]={forward.x,forward.y,forward.z,otherForward.x,otherForward.y,otherForward.z,d.x,d.y,d.z,right.x,right.y,right.z,
        float(shoveMove(p)),float(shoveMove(other)),at<float>(other,0x540)*2.f,1.f,1.f};
    // SA has no IV DefendWeakness. Neutral values cannot affect a Shove answer;
    // the weaker/fall branches are left native by the SDK boundary.
    unsigned flags=((at<unsigned>(p,0x4E0)|at<unsigned>(p,0x4E4))&bit?1u:0u)|(shoveLastPeer==reference(other)?2u:0u)|(combatWeapon(p)>1?8u:0u);
    if(!combatRegister(p))return;
    const int result=nm_combat_shove_begin(combat,reference(p),r,flags);
    if(result<=0){ok(result);return;}
    try{shoveRig=std::make_unique<Rig>(p,sourceBuild);}catch(const std::exception&e){nm_combat_shove_step(combat,-1);event("player_shove_rejected",reference(p),0,e.what());return;}
    shoveClip=result;shovePlayer=reference(p);shoveLastPeer=reference(other);
    event("player_shove_started",shovePlayer,0,"other="+std::to_string(shoveLastPeer)+" clip="+std::to_string(result)+" mask=8 tracked_channels_only=1");
    if(GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&shoveFrames.empty())
        for(unsigned delay:{17u,50u,100u,150u,250u,400u})shoveFrames.push_back(gameTime()+delay);
}
// IV: the shove event itself (CEventPlayerCollisionWithPed, peds.rs ~4883, collision scanner 0x82462A08) only follows a body contact, but the
// player's own scanner (0x82461A78, events.rs `walks_into`) already looks half a second ahead: the walk of 0.5 s must be over 0.1 squared flat, the
// ped more than 0.25 along his forward row, his line meeting the ped's sphere. Same test here with the answer 0x82471158 (shove_begin) as the gate,
// so the arm goes up on the approach. ADAPTED: the sphere is 1 m in IV (its radius is for the ped's evasive step); a shove needs the bodies to
// meet, so it is 0.25 + 0.25 (pedbodies PARTS torso/capsule radius) + 0.02 (pedbodies BREAK, the contact distance) = 0.52 m.
static void scanPlayerShoveAhead(){
    static const bool off=GetEnvironmentVariableA("GTASA_NO_SHOVE_LEADIN",nullptr,0)>0;
    auto*p=player();auto*pool=pedPool();
    if(off||!p||!pool||shoveClip||!playing()||!combatAllowed()||!matrix(p)||inVehicle(p)||agents.contains(reference(p))||combatOwns(p))return;
    V velocity=at<V>(p,0x44);
    {const uint64_t id=animatedExternalId(reference(p));for(const auto&o:externalHostOwners)if(o.id==id&&o.animated){velocity={o.rows[3],o.rows[4],0};break;}}
    velocity.z=0;const V pos=position(p),walk=velocity*.5f,forward=matrix(p)->up;
    if(!finite(velocity)||walk.x*walk.x+walk.y*walk.y<=.1f)return;
    void*best=nullptr;float bestAlong=1e9f,miss=0;
    for(int i=0;i<pool->size;i++)if(!(pool->flags[i]&128)){
        void*o=pool->objects+i*0x7C4;if(o==p||!validPed(o)||inVehicle(o)||agents.contains(reference(o)))continue;
        const V off2=position(o)-pos;const float along=dot(forward,off2),den=dot(walk,walk);if(along<=.25f||along>=bestAlong)continue;
        const float t=std::clamp(dot(off2,walk)/den,0.f,1.f);const V d=off2-walk*t;const float d2=dot(d,d);
        if(d2<=.52f*.52f){best=o;bestAlong=along;miss=std::sqrt(d2);}
    }
    if(!best)return;
    startPlayerShove(best);
    if(shoveClip)event("player_shove_leadin",shovePlayer,0,"other="+std::to_string(reference(best))+" along_m="+std::to_string(bestAlong)+" miss_m="+std::to_string(miss)+" speed_mps="+std::to_string(length(velocity)));
}
static void tickPlayerShove(float dt){
    if(!combat)return;
    if(!recoveryAllowed())shoveFrames.clear();
    if(!shoveFrames.empty()&&gameTime()>=shoveFrames.front()){
        const int capture=DeliriumSA_ProbeFrame();shoveFrames.erase(shoveFrames.begin());
        event("probe_shove_frame",shovePlayer,0,"clip="+std::to_string(shoveClip)+" result="+std::to_string(capture));
    }
    const bool cancel=!combatAllowed()||!validPed(player(),false,false,true)||agents.contains(reference(player()))||combatOwns(player())||reference(player())!=shovePlayer;
    const int previous=shoveClip;shoveClip=nm_combat_shove_step(combat,cancel?-1.f:dt);
    if(shoveClip<0){ok(shoveClip);shoveClip=0;}
    if(previous&&!shoveClip){event("player_shove_finished",shovePlayer,0);shoveRig.reset();}
    if(!playing()||!pedFromRef(shoveLastPeer))shoveLastPeer=-1;
    if(!shoveClip)scanPlayerShoveAhead();
}
static void renderPlayerShove(void*p){
    if(p!=player()||reference(p)!=shovePlayer||!shoveRig||!shoveClip||combatOwns(p)||agents.contains(reference(p)))return;
    std::array<T,21> parts{};std::vector<T> bones;if(!shoveRig->incoming(p,parts,bones))return;
    const T root=transform(*matrix(p)),inv=inverse(root);std::vector<float> input(bones.size()*7),out(input.size());
    for(size_t i=0;i<bones.size();i++){const T t=mul(inv,bones[i]);float*r=input.data()+i*7;r[0]=t.p.x;r[1]=t.p.y;r[2]=t.p.z;r[3]=t.q.x;r[4]=t.q.y;r[5]=t.q.z;r[6]=t.q.w;}
    if(nm_combat_shove_pose(combat,input.data(),int(bones.size()),out.data())<=0)return;
    for(size_t i=0;i<bones.size();i++){const float*r=out.data()+7*i;bones[i]=mul(root,T{{r[3],r[4],r[5],r[6]},{r[0],r[1],r[2]}});}
    auto*h=hierarchy(p);const auto palette=shoveRig->renderPalette(h->matrices,bones);std::copy(palette.begin(),palette.end(),h->matrices);
}
