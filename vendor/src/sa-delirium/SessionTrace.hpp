#pragma once
static std::map<int,ULONGLONG> sessionWatched,sessionSampled;
static void watchSessionPed(void*p){
    const int ref=reference(p);if(ref<0)return;
    const auto now=GetTickCount64();
    for(auto it=sessionWatched.begin();it!=sessionWatched.end();)if(now-it->second>10000){sessionSampled.erase(it->first);it=sessionWatched.erase(it);}else ++it;
    if(!sessionWatched.contains(ref)&&sessionWatched.size()>=12){
        const auto oldest=std::min_element(sessionWatched.begin(),sessionWatched.end(),[](const auto&a,const auto&b){return a.second<b.second;});
        sessionSampled.erase(oldest->first);sessionWatched.erase(oldest);
    }sessionWatched[ref]=now;
}
static void traceSessionPed(void*p){
    if(!sessionlog::enabled||sessionlog::poseLimit||!playing()||!p||!matrix(p)||!player())return;
    const int ref=reference(p);if(ref<0)return;const auto now=GetTickCount64();
    // Nearby rendered pedestrians provide pre-contact evidence without a test fixture.
    if(p==player()||length(position(p)-position(player()))<6.f)watchSessionPed(p);
    const auto w=sessionWatched.find(ref);if(w==sessionWatched.end()||now-w->second>10000)return;
    // GTASA_POSE_TRACE_EVERY_FRAME=1 (diagnostic only): one row per rendered
    // frame instead of 20 Hz, to measure the drawn pose's update cadence.
    static const bool everyFrame=GetEnvironmentVariableA("GTASA_POSE_TRACE_EVERY_FRAME",nullptr,0)>0;
    if(!everyFrame&&sessionSampled.contains(ref)&&now-sessionSampled[ref]<50)return;sessionSampled[ref]=now;
    auto*h=hierarchy(p);if(!h||!h->nodes||!h->matrices||h->num<1||h->num>128)return;
    const auto a=agents.find(ref);const auto attack=meleeAttack(p);
    auto number=[](std::ostringstream&s,float v){if(std::isfinite(v))s<<v;else s<<"null";};
    auto vector=[&](std::ostringstream&s,V v){s<<'[';number(s,v.x);s<<',';number(s,v.y);s<<',';number(s,v.z);s<<']';};
    LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);std::ostringstream s;s<<std::setprecision(everyFrame?11:7)<<"{\"qpc\":"<<qpc.QuadPart<<",\"pid\":"<<GetCurrentProcessId()<<",\"wall_ms\":"<<now<<",\"time_ms\":"<<gameTime()<<",\"frame\":"<<*reinterpret_cast<unsigned*>(0xB7CB4C)<<",\"ref\":"<<ref<<",\"model\":"<<at<short>(p,0x22)
        <<",\"health\":";number(s,at<float>(p,0x540));s<<",\"root\":";vector(s,position(p));s<<",\"velocity_mps\":";vector(s,at<V>(p,0x44)*50.f);
    // Native movement velocity is deliberately not the NM body's velocity.
    // Record the actual solver COM separately; never infer a frozen body from
    // CPed's animation-owned velocity while its physical root is moving.
    if(a!=agents.end()&&a->second->agent){V centre{},velocity{};
        if(nm_agent_get_com(a->second->agent,&centre.x,&velocity.x)>=0){s<<",\"physical_com\":";vector(s,centre);s<<",\"physical_velocity_mps\":";vector(s,velocity);}}
    s<<",\"move_state\":"<<at<int>(p,0x534);if(p==player())if(void*d=at<void*>(p,0x480)){s<<",\"move_blend\":";number(s,at<float>(d,0x14));}
    s<<",\"native_collision\":"<<((at<unsigned char>(p,0x1C)&1)?"true":"false")<<",\"native_state\":"<<at<int>(p,0x530)
        <<",\"physics\":"<<(a==agents.end()?"false":"true")<<",\"recovering\":"<<(a!=agents.end()&&a->second->recovering?"true":"false")
        <<",\"attack\":{\"anim\":"<<attack.anim<<",\"group\":"<<attack.group<<",\"move\":"<<attack.move<<",\"target\":"<<reference(attack.target)<<",\"profile\":"<<sessionlog::quote(attack.profile)<<",\"phase\":";number(s,attack.time);s<<"},\"animations\":[";
    void*clump=at<void*>(p,0x18);void*assoc=clump?reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump):nullptr;
    for(int count=0;assoc&&count<24;count++){
        if(count)s<<',';s<<"{\"group\":"<<at<unsigned short>(assoc,0xE)<<",\"anim\":"<<at<short>(assoc,0x2C)<<",\"phase\":";number(s,at<float>(assoc,0x20));
        s<<",\"weight\":";number(s,at<float>(assoc,0x18));s<<",\"blend_delta\":";number(s,at<float>(assoc,0x1C));s<<",\"rate\":";number(s,at<float>(assoc,0x24));s<<'}';
        assoc=reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(assoc);
    }s<<"],\"bones\":[";bool first=true;
    for(int n=0;n<h->num;n++){
        const int bone=h->nodes[n].id;if(bone!=1&&bone!=2&&bone!=3&&bone!=4&&bone!=5&&bone!=8&&bone!=24&&bone!=34&&bone!=44&&bone!=54)continue;
        if(!first)s<<',';first=false;s<<"{\"id\":"<<bone<<",\"p\":";vector(s,h->matrices[n].pos);s<<'}';
    }s<<"],\"vehicles\":[";first=true;
    if(collision)for(const auto&v:collision->vehicles)if(!v.component&&vehicleFromRef(v.ref)==v.entity&&length(position(v.entity)-position(p))<6.f){
        if(!first)s<<',';first=false;s<<"{\"ref\":"<<v.ref<<",\"model\":"<<at<short>(v.entity,0x22)<<",\"root\":";vector(s,position(v.entity));s<<",\"velocity_mps\":";vector(s,at<V>(v.entity,0x44)*50.f);s<<'}';
    }s<<"]}";sessionlog::pose(s.str());
}
struct SessionPedTrace {void*p;~SessionPedTrace(){traceSessionPed(p);}};
