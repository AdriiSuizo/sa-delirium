#pragma once
// The engine already resolves capsule contacts, but its SDK previously always
// supplied an empty peer list. Keep these contacts distinct from native hit
// queries and car collision, and never include the stepping body's own parts.
using PeerCapsule=std::array<float,11>;
struct MeleeContactFrame {unsigned time=0;int anim=-1,group=-1,target=-1;float phase=0;std::vector<PeerCapsule> rows;};
struct AnimatedContactPose {void* ped=nullptr;unsigned time=0;std::vector<PeerCapsule> rows;std::vector<T> bones;V root{};std::vector<MeleeContactFrame> history;};
static std::map<int,AnimatedContactPose> animatedContacts;
static PeerCapsule peerCapsule(V from,V to,float radius,V velocity,bool ragdoll){
    return {from.x,from.y,from.z,to.x,to.y,to.z,radius,velocity.x,velocity.y,velocity.z,ragdoll?1.f:0.f};
}
static std::vector<PeerCapsule> animatedPedCapsules(void* ped,const Active*rendered=nullptr){
    auto*h=hierarchy(ped);
    if(!h||!h->matrices||!h->nodes||h->num<20||h->num>128)return {};
    // The IV controller replaces the native animation palette at render time.
    // Colliding against SA's hidden idle pose gives different arms/legs from
    // those the player sees. Query the same authored palette before override.
    std::vector<T> combatPose;const bool authored=!rendered&&combatQueryPose(ped,combatPose)&&combatPose.size()==size_t(h->num);
    auto point=[&](int id,V& out){
        if(rendered&&rendered->lastRendered.size()==rendered->rig->ids.size()){
            for(size_t n=0;n<rendered->rig->ids.size();n++)if(rendered->rig->ids[n]==id){out=rendered->lastRendered[n].p;return finite(out)&&length(out-position(ped))<3.f;}
            return false;
        }
        for(int n=0;n<h->num;n++)if(h->nodes[n].id==id){
            out=authored?combatPose[n].p:h->matrices[n].pos;return finite(out)&&length(out-position(ped))<3.f;
        }return false;
    };
    V pelvis,neck;
    if(!point(saBone::pelvis,pelvis)||!point(saBone::neck,neck))return {};
    const float torso=length(neck-pelvis);if(torso<.2f||torso>1.1f)return {};
    const float scale=std::clamp(torso/.48f,.7f,1.3f);
    struct Segment {int a,b;float radius;};
    static constexpr Segment segments[]={
        {1,3,.17f},{41,51,.13f},{4,5,.12f},
        {32,33,.085f},{33,34,.075f},{22,23,.085f},{23,24,.075f},
        {34,35,.07f},{24,25,.07f}, // hands beyond the wrist, including closed fists
        {41,42,.115f},{42,43,.095f},{51,52,.115f},{52,53,.095f},
        {43,44,.10f},{53,54,.10f},{3,4,.15f} // close the upper-chest/neck gap
    };
    const V velocity=at<V>(ped,0x44)*50.f;
    if(!finite(velocity)||length(velocity)>80.f)return {};
    std::vector<PeerCapsule> rows;
    for(const auto&s:segments){V a,b;
        if(!point(s.a,a))return {};
        if(!point(s.b,b)){if(s.b==35||s.b==25||s.b==44||s.b==54)b=a;else return {};}
        if(length(b-a)>=1.2f)return {};
        rows.push_back(peerCapsule(a,b,s.radius*scale,velocity,false));
    }
    return rows;
}
static void sampleAnimatedContacts(void*ped){
    const int ref=reference(ped);if(ref<0||!player()||!matrix(ped)||length(position(ped)-position(player()))>radius+5.f)return;
    // Capture the evaluated native hierarchy once, before our skin override.
    // Physics uses the latest completed sample, never a second sample at dt=0.
    auto rows=animatedPedCapsules(ped);if(rows.empty()){animatedContacts.erase(ref);return;}
    auto&last=animatedContacts[ref];const unsigned now=gameTime();
    const unsigned elapsed=now-last.time;
    if(last.ped==ped&&elapsed>=2&&elapsed<=100&&last.rows.size()==rows.size()){
        for(size_t i=0;i<rows.size();i++){
            const auto&old=last.rows[i];auto&r=rows[i];
            const V from{r[0]-old[0],r[1]-old[1],r[2]-old[2]},to{r[3]-old[3],r[4]-old[4],r[5]-old[5]};
            const V v=(from+to)*(500.f/float(elapsed));
            // Teleports, animation cuts and stale palettes are not punches.
            if(finite(v)&&length(from)<.6f&&length(to)<.6f&&length(v)<=20.f){r[7]=v.x;r[8]=v.y;r[9]=v.z;}
        }
    }
    if(last.time!=now){
        std::vector<T> bones;auto*h=hierarchy(ped);
        if(!combatQueryPose(ped,bones)&&h&&h->matrices)for(int n=0;n<h->num;n++)bones.push_back(transform(h->matrices[n]));
        auto history=last.ped==ped?std::move(last.history):std::vector<MeleeContactFrame>{};
        const auto attack=meleeAttack(ped);
        auto active=agents.find(ref);const bool physical=active!=agents.end()&&!active->second->lastRendered.empty();
        const unsigned frameTime=physical?active->second->lastRenderTime:now;
        if(!history.empty()&&(frameTime<history.back().time||
            (attack.anim==history.back().anim&&attack.time+.001f<history.back().phase)))history.clear();
        if(history.empty()||frameTime>history.back().time){
            history.push_back({frameTime,attack.anim,attack.group,reference(attack.target),attack.time,
                physical?animatedPedCapsules(ped,active->second.get()):rows});
        }
        while(!history.empty()&&(now-history.front().time>140||history.size()>20))history.erase(history.begin());
        last={ped,now,std::move(rows),std::move(bones),position(ped),std::move(history)};
    }
    for(auto it=animatedContacts.begin();it!=animatedContacts.end();)
        if(now-it->second.time>500||pedFromRef(it->first)!=it->second.ped)it=animatedContacts.erase(it);else ++it;
}
// Physical peers are stepped together by stepPhysicsActors. Animated peers
// exchange finite impulses in BodyContacts; no infinite-mass duplicate here.
