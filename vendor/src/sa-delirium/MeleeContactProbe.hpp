#pragma once
// Passive diagnostics for the explicit two-ped melee fixture only. It records
// evaluated bones and native animation time; it cannot start hits or alter poses.
static int meleeProbeAttacker=-1,meleeProbeVictim=-1;
static std::vector<std::string> meleeProbeRows;
extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeFrame();
struct MeleeFrameDue {unsigned due,hit;int ref;const char* eventName="probe_melee_hit_frame";};
static std::vector<MeleeFrameDue> meleeFrames;
static unsigned meleeCapturedHits=0;
static void queueMeleeFrames(int ref){
    // Explicit owned fixture only, maximum four hits. No production recording.
    if(ref!=meleeProbeVictim||meleeProbeAttacker<0||meleeCapturedHits>=8)return;
    ++meleeCapturedHits;const auto now=gameTime();
    for(unsigned delay:{17u,75u,150u,300u,500u,1000u,2000u})meleeFrames.push_back({now+delay,now,ref});
}
static void captureMeleeFrames(){
    if(!recoveryAllowed()){meleeFrames.clear();return;}
    const auto now=gameTime();
    for(auto it=meleeFrames.begin();it!=meleeFrames.end();){
        if(now<it->due){++it;continue;}
        // Reads the game's previous completed backbuffer, without pausing,
        // changing time scale, or touching desktop input/focus.
        const int result=DeliriumSA_ProbeFrame();
        event(it->eventName,it->ref,-1,"hit_ms="+std::to_string(it->hit)+" capture_ms="+std::to_string(now)+" result="+std::to_string(result));
        if(void*p=pedFromRef(it->ref))event("probe_melee_lifecycle",it->ref,at<short>(p,0x22),
            "health="+std::to_string(at<float>(p,0x540))+" ped_state="+std::to_string(at<int>(p,0x530))+
            " death_time="+std::to_string(at<unsigned>(p,0x750))+" physical="+std::to_string(agents.contains(it->ref))+
            " root_z="+std::to_string(position(p).z));
        it=meleeFrames.erase(it);break; // at most one readback per game tick
    }
}
static bool probeBone(void*p,int id,V&out){
    auto*h=hierarchy(p);if(!h||!h->nodes||!h->matrices||h->num<20||h->num>128)return false;
    for(int n=0;n<h->num;n++)if(h->nodes[n].id==id){out=h->matrices[n].pos;return finite(out)&&length(out-position(p))<3.f;}
    return false;
}
static float probePointSegment(V p,V a,V b){
    const V d=b-a;const float t=std::clamp(dot(p-a,d)/std::max(dot(d,d),1.e-8f),0.f,1.f);
    return length(p-(a+d*t));
}
static void sampleMeleeContact(void*ped){
    if(reference(ped)!=meleeProbeAttacker||meleeProbeRows.size()>=1200)return;
    auto*target=pedFromRef(meleeProbeVictim);if(!target||!matrix(target))return;
    V pelvis,neck,head;if(!probeBone(target,saBone::pelvis,pelvis)||!probeBone(target,saBone::neck,neck)||!probeBone(target,saBone::head,head))return;
    float gap=99.f,speed=0,handGap[2]={99.f,99.f},handSpeed[2]={0,0};const float scale=std::clamp(length(neck-pelvis)/.48f,.7f,1.3f);
    for(int side=0;side<2;side++){const int hand=side==0?24:34;V wrist,finger;if(!probeBone(ped,hand,wrist)||!probeBone(ped,hand+1,finger))continue;
        // Approximate skin-contact diagnostic, not a mesh intersection proof.
        for(V p:{wrist,(wrist+finger)*.5f,finger})handGap[side]=std::min(handGap[side],std::min(probePointSegment(p,pelvis,neck)-.17f*scale,length(p-head)-.12f*scale)-.07f);
        gap=std::min(gap,handGap[side]);
    }
    if(auto it=animatedContacts.find(meleeProbeAttacker);it!=animatedContacts.end())
        for(size_t n=7;n<std::min(size_t(9),it->second.rows.size());n++){const auto&r=it->second.rows[n];handSpeed[n==8?0:1]=length(V{r[7],r[8],r[9]});speed=std::max(speed,handSpeed[n==8?0:1]);}
    float headGap=99.f,headSpeed=0;V attackerHead;
    if(probeBone(ped,saBone::head,attackerHead))headGap=std::min(probePointSegment(attackerHead,pelvis,neck)-.17f*scale,length(attackerHead-head)-.12f*scale)-.12f;
    if(auto it=animatedContacts.find(meleeProbeAttacker);it!=animatedContacts.end()&&it->second.rows.size()>2){const auto&r=it->second.rows[2];headSpeed=length(V{r[7],r[8],r[9]});}
    int move=-1,anim=-1,group=-1;float time=-1;
    if(auto*m=taskManager(ped))if(auto*t=at<void*>(m,0x14);t&&at<unsigned>(t,0)==0x86D684){
        move=at<signed char>(t,0x25);if(auto*a=at<void*>(t,0x1C)){anim=at<short>(a,0x2C);group=at<unsigned short>(a,0xE);time=at<float>(a,0x20);}
    }
    auto active=agents.find(meleeProbeVictim);const bool physical=active!=agents.end()&&!active->second->recovering&&!active->second->preview;
    const int renderAge=active!=agents.end()&&active->second->lastRenderTime?int(gameTime()-active->second->lastRenderTime):-1;
    std::ostringstream row;row<<gameTime()<<','<<anim<<','<<group<<','<<time<<','<<move<<','<<physical<<','<<renderAge<<','<<gap<<','<<speed;
    row<<','<<handGap[0]<<','<<handGap[1]<<','<<handSpeed[0]<<','<<handSpeed[1]<<','<<headGap<<','<<headSpeed;
    meleeProbeRows.push_back(row.str());
}
static void finishMeleeContactProbe(){
    if(meleeProbeRows.empty())return;
    const auto name="melee-contact-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(meleeProbeVictim)+".csv";
    std::ofstream f(folder/name);f<<"game_ms,anim_id,anim_group,anim_time,move,physical,render_age_ms,hand_body_gap_m,hand_speed_mps,right_gap_m,left_gap_m,right_speed_mps,left_speed_mps,head_gap_m,head_speed_mps\n";
    for(const auto&row:meleeProbeRows)f<<row<<'\n';
    event("probe_melee_contact_trace",meleeProbeVictim,-1,"file="+name+" samples="+std::to_string(meleeProbeRows.size()));
    meleeProbeRows.clear();meleeProbeAttacker=meleeProbeVictim=-1;
}
