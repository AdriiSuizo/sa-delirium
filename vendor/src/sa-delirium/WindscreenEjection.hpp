#pragma once
// GTA IV windscreen ejection: an occupant of a car that crashes head-on hard enough flies out through the glass as a
// ragdoll. Included by Adapter.cpp AFTER VehicleExits.hpp / BikeTransitions.hpp / BikeHitFalls.hpp.
//
// IV mechanism (Xbox 360 TU8 exe, re/image.bin disassembled; the LSC4 viewer has NO port of it, only the script flag):
//  * Every occupant runs CTaskComplexCarDriveBasic (task 709); its vf20 0x823EEEA0 calls the driver's crash check
//    0x823E8048 each frame (call at 0x823EF5A4; task state +0x40 := the kind it returns; leaf by state 0x823EC1D8).
//    Kind 3 = shunt clip (dv > 2.0, 0x82AA274C), kind 6 = THROUGH WINDSCREEN (this file).
//  * 0x823E8048: dv = vehicle +0x170 (the frame's impact impulse) x archetype vf+0x28 (inverse mass) = the car's speed
//    change this frame (m/s); direction = vehicle +0x180 (direction of the impulse ON the car). docs: audio/speech.md:409.
//  * Who may fly (+0x234 bit 0x800000, SET_CHAR_WILL_FLY_THROUGH_WINDSCREEN 0x825C8650, native_meanings.csv:164,
//    scripts.rs:3024). Default is set; becoming a mission ped (+0x9B0 = 2, 0x822F3778 at 0x822F3838) clears it unless
//    the ped is the player. Player or +0x9B0 = 2 (vf+0x114 = player): flag only, threshold 18.0 (0x82AA2760); a network
//    player also needs a draw > 0.9 (0x82AA2754). Ambient ped (+0x9B0 = 1): flag AND rand(0,1) > 0.75 (0x82AA2758,
//    drawn every frame the check runs, 0x823E8230..0x823E826C), threshold 12.0 (0x82AA275C).
//  * Kind 6 needs also: vehicle +0x12E4 == 0 (a car, not bike/boat/heli), model not "apc" / "n_apc" (globals 0x82A9A77C /
//    0x82A9A7F4, names at 0x8200D80C / 0x8200D8A0), dv > threshold, seat 0 (driver) or 2 (front passenger) by
//    0x8237ACF0, and dot(vehicle forward row +0x10, impact dir) < -0.707 (0x8200A81C), i.e. a frontal hit
//    (0x823E8390..0x823E843C). Then out kind := 6 and task +0x50 := 5.0 x dv (0x82AA2750); task +0x60 := -dv x impact dir
//    (0x823E8454..0x823E8468). The real player (+0x211 set, +0x210 clear) with health > 101 also queues a CEventDamage of
//    weapon 0x31, damage 0 (0x823E84A0..0x823E8514, the same record as the bike throw, bikethrow.rs:18).
//  * State 6 leaf = CTaskComplexNewExitVehicle(vehicle, flags 0x2440, wait 0) with +0x50 := that vector
//    (0x823EC358..0x823EC390); exit-vehicle.md:67: flag 0x2000 -> CTaskSimpleCarJumpOut(vehicle, door 0x1C = windscreen,
//    vector) (ctor 0x824B80E0 keeps it at +0x30). Its clip is THROUGH_WINDSCREEN_DS/PS 0x15A/0x15B (vehclip.rs:330,
//    player.rs:2762).
//  * CarJumpOut vf17 0x824BD3F0 at the clip event (+0x74 bit 0x80000): SetPedOut (0x824B82E0/0x824BA668), ped velocity :=
//    vehicle velocity (0x8247F840), no-collision with the vehicle (0x8247EEA8, CPhysical +0x1F4), NM gate reason 15
//    (0x826174F0) -> NMJumpRollFromRoadVehicle 0x82613AA8 with 3000..6000 ms (0x824BD96C..0x824BD974), as the bail in
//    player.rs:2759-2790; and for door 0x1C an extra impulse mass x (0.25 x vector + (0,0,3)) through 0x82480480
//    (0x824BD9D0..0x824BDA50; constants 0x82AA6064 = 0.25, 0x82AA6068 = 3.0, 0x82B071E0 = up).
// SA mapping: the car's per-frame velocity change (beforeVehicleVelocity -> after the world process) stands for +0x170/mass and
// its direction for +0x180; SA's CTaskSimpleCarSetPedOut (the task IV runs inside CarJumpOut) removes the ped; the ragdoll goes
// through the existing Reaction::VehicleExit pipeline (jump roll 3..6 s, no-collision with the car, player rules) with IV's launch.
static bool windscreenEjection=true;
// Adaptation asked by the user: SA stops the car a frame on a lamp post before it gives way, and on a bike, which read as a
// head-on crash. +0xDC CPhysical damage entity; object info +0x160: uproot limit +0x14, col damage effect +0x1C.
static bool windscreenYields(void*v){
    void*hit=at<void*>(v,0xDC);if(!hit)return false;
    const int type=at<unsigned char>(hit,0x36)&7;
    if(type==4){const auto*info=at<const char*>(hit,0x160);return info&&(*reinterpret_cast<const float*>(info+0x14)>0.f||info[0x1C]!=0);}
    return type==2&&at<float>(hit,0x90)<700.f;
}
constexpr float windscreenPlayerDv=18.f,windscreenPedDv=12.f,windscreenChance=.75f,windscreenForwardDot=-.707f,windscreenAlong=.25f,windscreenUp=3.f;
struct WindscreenRun {V last{};unsigned lastFrame=0,decided[2]{0,0};};
static std::map<int,WindscreenRun> windscreenRuns; // per vehicle ref
struct WindscreenMark {V launch{};unsigned time=0;};
static std::map<int,WindscreenMark> windscreenMarks; // per ejected ped ref, until its NM task is verified
// Adaptation asked by the user (IV keeps the glass; the clip goes through it): the windscreen breaks the moment the body
// leaves, and the whole body ignores the car until it is above the car's surface.
struct WindscreenHold {
    int vehicle=-1;V point{},normal{},seat{},exit{},launch{};unsigned until=0,carryStart=0;float carrySeconds=.15f,pitch=0,swing=0;bool started=false,carried=false;
};
static std::map<int,WindscreenHold> windscreenHolds; // per ejected ped ref
// The front glass of the car's COL in model space: the triangles CGlass::CarWindscreenShatters (0x71C2B0) picks by IsGlass,
// facing the front. Without any, a vertical plane a third of the way to the front of the bounds.
static bool windscreenPlane(void*v,V&point,V&normal){
    const auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v);auto*d=col?col->data:nullptr;if(!d)return false;
    V sum{},n{};int count=0;
    if(d->tri&&d->vertices)for(int i=0;i<d->triangles;i++){
        const auto t=d->tri[i];if(!glassSurface(t.material))continue;
        auto vertex=[&](int k){V p;reinterpret_cast<void(__thiscall*)(void*,V&,int)>(0x40F5E0)(d,p,k);return p;};
        const V a=vertex(t.a),b=vertex(t.b),c=vertex(t.c),centre=(a+b+c)*(1.f/3.f);
        V f=normalized(cross(b-a,c-a));if(f.y<0)f=V{}-f;
        if(centre.y<=0||f.y<.3f)continue; // side and rear windows
        sum=sum+centre;n=n+f;count++;
    }
    if(count){point=sum*(1.f/float(count));normal=normalized(n);return true;}
    point={0,col->box.max.y*.33f,0};normal={0,1,0};return true;
}
// NM parts 1..6: Thigh_Left, Shin_Left, Foot_Left, Thigh_Right, Shin_Right, Foot_Right (delirium-data nm_asset::PARTS).
constexpr uint32_t windscreenLegMask=0x7Eu;
// The car's upper surface in model space on a 10 cm grid, from its COL. Any car, van or truck: no per-model data.
struct WindscreenTop {
    float x0=0,y0=0;int nx=0,ny=0;std::vector<float> z;
    float height(float x,float y)const{
        const int i=int(std::floor((x-x0)*10.f)),j=int(std::floor((y-y0)*10.f));
        return i<0||j<0||i>=nx||j>=ny?-1000.f:z[size_t(j)*nx+i];
    }
};
static const WindscreenTop* windscreenTop(void*v){
    static std::map<int,WindscreenTop> tops;const int model=at<short>(v,0x22);
    if(const auto i=tops.find(model);i!=tops.end())return &i->second;
    const auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v);if(!col)return nullptr;
    WindscreenTop t;t.x0=col->box.min.x-.2f;t.y0=col->box.min.y-.2f;
    t.nx=int((col->box.max.x-col->box.min.x+.4f)*10.f)+1;t.ny=int((col->box.max.y-col->box.min.y+.4f)*10.f)+1;
    if(t.nx<=0||t.ny<=0||t.nx>200||t.ny>400)return nullptr;
    std::vector<float> raw(size_t(t.nx)*t.ny,-1000.f);
    auto mark=[&](float x,float y,float h){
        const int i=int(std::floor((x-t.x0)*10.f)),j=int(std::floor((y-t.y0)*10.f));
        if(i>=0&&j>=0&&i<t.nx&&j<t.ny)raw[size_t(j)*t.nx+i]=std::max(raw[size_t(j)*t.nx+i],h);
    };
    bool any=false;
    if(auto*d=col->data){
        if(d->tri&&d->vertices)for(int n=0;n<d->triangles;n++){
            const auto f=d->tri[n];
            auto vertex=[&](int k){V p;reinterpret_cast<void(__thiscall*)(void*,V&,int)>(0x40F5E0)(d,p,k);return p;};
            const V a=vertex(f.a),b=vertex(f.b),c=vertex(f.c);
            // Stepping along the two edges covers the triangle at grid resolution, however thin.
            const int steps=std::clamp(int(std::max(length(b-a),length(c-a))*20.f)+1,1,120);
            for(int s=0;s<=steps;s++)for(int r=0;r<=steps-s;r++){
                const V p=a+(b-a)*(float(s)/steps)+(c-a)*(float(r)/steps);mark(p.x,p.y,p.z);
            }
            any=true;
        }
        if(d->sphere)for(int n=0;n<d->spheres;n++){
            const auto&s=d->sphere[n];
            for(float x=-s.radius;x<=s.radius;x+=.05f)for(float y=-s.radius;y<=s.radius;y+=.05f){
                const float q=s.radius*s.radius-x*x-y*y;if(q>=0)mark(s.pos.x+x,s.pos.y+y,s.pos.z+std::sqrt(q));
            }
            any=true;
        }
        if(d->box)for(int n=0;n<d->boxes;n++){
            const auto&b=d->box[n].box;
            for(float x=b.min.x;x<=b.max.x;x+=.05f)for(float y=b.min.y;y<=b.max.y;y+=.05f)mark(x,y,b.max.z);
            any=true;
        }
    }
    if(!any)for(float x=col->box.min.x;x<=col->box.max.x;x+=.05f)for(float y=col->box.min.y;y<=col->box.max.y;y+=.05f)mark(x,y,col->box.max.z);
    // One cell of margin all round: a part's radius is added on top of this.
    t.z=raw;
    for(int j=0;j<t.ny;j++)for(int i=0;i<t.nx;i++)for(int dj=-1;dj<=1;dj++)for(int di=-1;di<=1;di++){
        const int a=i+di,b=j+dj;if(a<0||b<0||a>=t.nx||b>=t.ny)continue;
        t.z[size_t(j)*t.nx+i]=std::max(t.z[size_t(j)*t.nx+i],raw[size_t(b)*t.nx+a]);
    }
    return &(tops[model]=std::move(t));
}
static bool windscreenLineFree(void*v,V from,V to){
    ColPoint hit{};void*entity=nullptr;auto*&ignore=*reinterpret_cast<void**>(0xB7CD68);void*previous=ignore;ignore=v; // CWorld::pIgnoreEntity
    const bool blocked=reinterpret_cast<bool(__cdecl*)(const V&,const V&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool)>(0x56BA00)
        (from,to,hit,entity,true,true,false,true,false,false,false,false);
    ignore=previous;return !blocked;
}
// The body lying in the air, head forward: its extent around the centre of mass (forward, back, under).
constexpr float windscreenBodyAhead=.75f,windscreenBodyBehind=1.1f,windscreenBodyUnder=.3f,windscreenBodyHalfWidth=.35f,windscreenPartRadius=.12f;
// Where the body comes out, in car space: the lowest point clear of the whole shell along the body's length, as near the glass
// as that allows, with a free line from the seat. With no such point (a van's flat nose on a wall) there is no way out, and
// the occupant stays in the car: an ejection is never begun that could end inside the car.
static bool windscreenPlanExit(void*v,WindscreenHold&h,V seat){
    const auto*top=windscreenTop(v);const auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v);const M*m=matrix(v);
    if(!top||!col||!m)return false;
    struct Option {float y,z,cost;};std::vector<Option> options;
    const float side=seat.x*.6f; // driver and passenger each keep their side
    for(float y=h.point.y+.3f;y<=col->box.max.y+1.2f;y+=.1f){
        float surface=-1000.f;
        for(float x=-windscreenBodyHalfWidth;x<=windscreenBodyHalfWidth;x+=.1f)for(float s=y-windscreenBodyBehind;s<=y+windscreenBodyAhead;s+=.1f)
            surface=std::max(surface,top->height(side+x,s));
        const float z=std::max(surface,seat.z)+windscreenPartRadius+windscreenBodyUnder;
        options.push_back({y,z,z+(y-h.point.y)*.25f});
    }
    std::sort(options.begin(),options.end(),[](const Option&a,const Option&b){return a.cost<b.cost;});
    const V seatWorld=point(*m,seat+V{0,0,.3f});int tried=0;
    for(const auto&o:options){
        if(++tried>12)break;
        const V to=point(*m,{side,o.y,o.z}),ahead=point(*m,{side,o.y+windscreenBodyAhead+windscreenPartRadius,o.z});
        if(!windscreenLineFree(v,seatWorld,to)||!windscreenLineFree(v,to,ahead))continue;
        h.exit={side,o.y,o.z};return true;
    }
    return false;
}
static Q windscreenTurn(V axis,float radians){const float s=std::sin(radians*.5f);return unit({axis.x*s,axis.y*s,axis.z*s,std::cos(radians*.5f)});}
// Parts still under the car's surface (or within a part's radius of it) keep ignoring the car.
static uint32_t windscreenPartsInside(NmAgent*agent,void*v){
    const M*m=matrix(v);const auto*top=windscreenTop(v);if(!m||!top)return 0;const Q inv=conjugate(rotation(*m));uint32_t inside=0;
    for(int i=0;i<21;i++){
        V p{};float q[4]{};if(!ok(nm_agent_get_part_transform(agent,i,&p.x,q)))continue;
        const V l=rotate(inv,p-m->pos);if(l.z-windscreenPartRadius<top->height(l.x,l.y))inside|=1u<<i;
    }
    return inside;
}
// IV's clip THROUGH_WINDSCREEN carries the ped out of the car before the ragdoll starts. Here the ragdoll itself is carried: its
// centre of mass follows a curve from the seat to the exit point in the car's frame while it pitches head first and the legs
// swing back behind it, ignoring the car. It is free above the shell, so nothing of it starts inside the car, and the seated
// pose never meets what stopped the car (07/11: feet first, the body stopped at the bonnet and sank into the shell).
constexpr float windscreenPitch=1.3f,windscreenLegSwing=1.45f,windscreenSpin=1.2f;
static void windscreenCarry(Active&a,WindscreenHold&h,void*v,unsigned now){
    const M*m=matrix(v);if(!m)return;
    float rows[21*13]{},pose[21*7]{};
    if(!ok(nm_agent_get_part_transforms(a.agent,pose,21)))return;
    V com{},comVelocity{};if(!ok(nm_agent_get_com(a.agent,&com.x,&comVelocity.x)))return;
    const float u=std::min(1.f,float(now-h.carryStart)/(h.carrySeconds*1000.f));
    const float smooth=u*u*(3.f-2.f*u);
    const V right=normalized(m->right);
    const float pitch=-windscreenPitch*smooth,swing=-windscreenLegSwing*smooth;
    const Q whole=windscreenTurn(right,pitch-h.pitch),legs=windscreenTurn(right,swing-h.swing);h.pitch=pitch;h.swing=swing;
    // Quadratic curve: straight up the seat first, then forward over the bonnet.
    const V control{h.exit.x,h.seat.y+(h.exit.y-h.seat.y)*.35f,h.exit.z};
    const V local=h.seat*((1-u)*(1-u))+control*(2*u*(1-u))+h.exit*(u*u);
    const V slope=(control-h.seat)*(2*(1-u))+(h.exit-control)*(2*u);
    const V target=point(*m,local);
    const V carVelocity=at<V>(v,0x44)*50.f,carried=u<1.f?carVelocity+vector(*m,slope)*(1.f/h.carrySeconds):h.launch;
    const V spin=u<1.f?V{}:right*-windscreenSpin;
    const V pelvis{pose[0],pose[1],pose[2]};
    for(int k=0;k<21;k++){
        const float*p=pose+k*7;V offset=V{p[0],p[1],p[2]}-com;Q q{p[3],p[4],p[5],p[6]};
        if(windscreenLegMask>>k&1){const V pivot=pelvis-com;offset=pivot+rotate(legs,offset-pivot);q=mul(legs,q);}
        offset=rotate(whole,offset);q=mul(whole,q);
        const V placed=target+offset;float*row=rows+k*13;
        row[0]=placed.x;row[1]=placed.y;row[2]=placed.z;row[3]=q.x;row[4]=q.y;row[5]=q.z;row[6]=q.w;
        row[7]=carried.x;row[8]=carried.y;row[9]=carried.z;row[10]=spin.x;row[11]=spin.y;row[12]=spin.z;
    }
    ok(nm_agent_set_part_states(a.agent,rows,21));
    h.carried=u>=1.f;
}
static void windscreenHoldStarted(Active&a,WindscreenHold&h,void*v){
    const M*m=matrix(v);V com{},velocity{};
    if(!m||!ok(nm_agent_get_com(a.agent,&com.x,&velocity.x)))return;
    h.seat=rotate(conjugate(rotation(*m)),com-m->pos);h.started=true;h.carryStart=gameTime();
    const float way=length(h.exit-h.seat),relative=length(h.launch-at<V>(v,0x44)*50.f);
    h.carrySeconds=std::clamp(way/std::max(relative,1.f),.1f,.22f);
    ok(nm_agent_hold_no_collision(a.agent,vehicleExternalId(h.vehicle),0x1FFFFFu));
    event("windscreen_carry",a.ref,a.model,"vehicle_ref="+std::to_string(h.vehicle)+" exit_y="+std::to_string(h.exit.y)+" exit_z="+std::to_string(h.exit.z)+
        " seat_y="+std::to_string(h.seat.y)+" seat_z="+std::to_string(h.seat.z)+" seconds="+std::to_string(h.carrySeconds));
}
static void windscreenBodyStarted(Active&a){
    const auto h=windscreenHolds.find(a.ref);if(h==windscreenHolds.end()||h->second.started)return;
    if(void*v=vehicleFromRef(h->second.vehicle)){windscreenHoldStarted(a,h->second,v);h->second.until=gameTime()+1500;}
}
// Last resort, for a body something pinned under the car's surface all the same: it goes up onto the surface.
static void windscreenLift(Active&a,void*v){
    const M*m=matrix(v);const auto*top=windscreenTop(v);if(!m||!top)return;
    const Q inv=conjugate(rotation(*m));float pose[21*7]{},rows[21*13]{},lift=0;
    if(!ok(nm_agent_get_part_transforms(a.agent,pose,21)))return;
    for(int k=0;k<21;k++){const V l=rotate(inv,V{pose[k*7],pose[k*7+1],pose[k*7+2]}-m->pos);lift=std::max(lift,top->height(l.x,l.y)+windscreenPartRadius-l.z);}
    if(lift<=0)return;
    for(int k=0;k<21;k++){
        const float*p=pose+k*7;float*row=rows+k*13;
        const V placed=V{p[0],p[1],p[2]}+normalized(m->at)*(lift+.03f);
        row[0]=placed.x;row[1]=placed.y;row[2]=placed.z;row[3]=p[3];row[4]=p[4];row[5]=p[5];row[6]=p[6];
    }
    ok(nm_agent_set_part_states(a.agent,rows,21));
    event("windscreen_lifted",a.ref,a.model,"lift_m="+std::to_string(lift));
}
static void tickWindscreenHolds(){
    const unsigned now=gameTime();
    for(auto it=windscreenHolds.begin();it!=windscreenHolds.end();){
        const auto a=agents.find(it->first);void*v=vehicleFromRef(it->second.vehicle);auto&h=it->second;
        if(a==agents.end()||!a->second->agent){if(h.started||now>h.until)it=windscreenHolds.erase(it);else ++it;continue;}
        auto&body=*a->second;
        if(!v){ok(nm_agent_hold_no_collision(body.agent,vehicleExternalId(h.vehicle),0));it=windscreenHolds.erase(it);continue;}
        if(!h.started){windscreenHoldStarted(body,h,v);if(h.started)h.until=now+1500;}
        if(!h.started){++it;continue;}
        if(!h.carried){windscreenCarry(body,h,v,now);++it;continue;}
        const uint32_t mask=windscreenPartsInside(body.agent,v);
        if(mask&&now>h.until)windscreenLift(body,v);
        ok(nm_agent_hold_no_collision(body.agent,vehicleExternalId(h.vehicle),now>h.until?0:mask));
        if(!mask||now>h.until){event("windscreen_clear",it->first,body.model,"timeout="+std::to_string(now>h.until)+" parts_inside="+std::to_string(__popcnt(mask)));it=windscreenHolds.erase(it);}else ++it;
    }
}
// CDamageManager::SetPanelStatus 0x6C2150 (WINDSCREEN_PANEL 4, DAMSTATE_OPENED_DAMAGED 3) then CAutomobile::SetPanelDamage
// 0x6B1480 with glass: CGlass::CarWindscreenShatters throws the shards and the component is hidden, SA's own breaking; the
// shatter sound is AE_WINDSCREEN_SHATTER 91 through CAEVehicleAudioEntity::AddAudioEvent 0x4F6420 (vehicle +0x138).
static void windscreenShatter(void*v){
    if(at<int>(v,0x594)!=0||!at<void*>(v,0x648+18*4))return; // automobiles with a windscreen node (CAR_WINDSCREEN 18)
    void*dm=static_cast<unsigned char*>(v)+0x5A0;
    if(reinterpret_cast<unsigned char(__thiscall*)(void*,int)>(0x6C2180)(dm,4)==3)return;
    reinterpret_cast<void(__thiscall*)(void*,int,int)>(0x6C2150)(dm,4,3);
    reinterpret_cast<void(__thiscall*)(void*,int,bool)>(0x6B1480)(v,4,true);
    reinterpret_cast<void(__thiscall*)(void*,int,float)>(0x4F6420)(static_cast<unsigned char*>(v)+0x138,91,0.f);
    event("windscreen_shattered",-1,at<short>(v,0x22),"vehicle_ref="+std::to_string(vehicleRef(v)));
}
static bool windscreenAudit(){static const bool on=GetEnvironmentVariableA("GTASA_WINDSCREEN_AUDIT",nullptr,0)!=0;return on;}
// CTaskSimpleCarSetPedOut (ctor 0x6478B0, 0x18 bytes, +0x12 m_bFallingOutOfCar like the jump/knock-off users), run through its
// vtable ProcessPed (slot 7) and destroyed without freeing: IV's 0x824B82E0 / 0x824BA668 / 0x824B8360 sequence (0x824BD62C).
static bool windscreenSetPedOut(void*ped,void*v){
    alignas(4) unsigned char task[0x18]{};
    int door=reinterpret_cast<int(__cdecl*)(const void*,const void*)>(0x64F110)(v,ped); // CCarEnterExit::ComputeTargetDoorToExit
    if(door<8||door>11)door=at<void*>(v,0x460)==ped?10:8; // front left / front right
    reinterpret_cast<void*(__thiscall*)(void*,void*,int,bool)>(0x6478B0)(task,v,door,false);
    at<bool>(task,0x12)=true;
    reinterpret_cast<bool(__thiscall*)(void*,void*)>((*reinterpret_cast<void***>(task))[7])(task,ped);
    reinterpret_cast<void(__thiscall*)(void*,unsigned)>((*reinterpret_cast<void***>(task))[0])(task,0);
    return !inVehicle(ped);
}
static bool windscreenOccupant(void*ped,void*v){
    const int ref=reference(ped);
    if(ref<0||!matrix(ped)||!inVehicle(ped)||at<void*>(ped,0x58C)!=v||at<float>(ped,0x540)<=0||at<unsigned char>(ped,0x2F)!=area())return false;
    // IV +0x9B0 == 1 (ambient) keeps +0x234 bit 23; mission peds lose it (0x822F3838). SA 0x484 == 1 is the ambient ped.
    if(ped==player()){if(!physicalVehicleExits)return false;}
    else if(at<void*>(ped,0x480)||at<unsigned char>(ped,0x484)!=1||int(agents.size()+pending.size())>=maxActive)return false;
    if(length(position(ped)-position(player()))>radius)return false;
    return !agents.contains(ref)&&!pending.contains(ref)&&!exitTransits.contains(ref);
}
// vehicle_exit_start picks NMBalance at or below 7.5 m/s; IV's CarJumpOut always runs the jump roll (0x824BD96C). Replace a freshly
// started balance by the SDK's always-jump-roll start (its finish window is 2..30 s there, 3..6 s in IV: engine follow-up noted).
static void windscreenNmFix(){
    const unsigned now=gameTime();
    for(auto it=windscreenMarks.begin();it!=windscreenMarks.end();){
        const auto a=agents.find(it->first);
        if(now-it->second.time>4000||(a==agents.end()&&now-it->second.time>1500)){it=windscreenMarks.erase(it);continue;}
        if(a!=agents.end()&&a->second->agent&&a->second->kind==Reaction::VehicleExit&&!a->second->mounted){
            float s[4]{};const int phase=nm_agent_vehicle_exit_status(a->second->agent,s);
            if(phase>=1){
                // The roll stays IV's; NMHFall's flail, arms up and catchFall fill the flight, as asked (IV's jump roll has only catchFall).
                const V launch=it->second.launch;
                if(ok(nm_agent_bike_throw_start(a->second->agent,collision->world,&launch.x,at<float>(a->second->ped,0x540)*2.f,(a->second->ped==player()?1u:0u)|2u)))
                    event("windscreen_high_fall",a->first,a->second->model,"task=jump_roll speed_mps="+std::to_string(length(launch))+" original_times=3_6 sdk_times=2_30");
            }
            if(phase>=1){it=windscreenMarks.erase(it);continue;}
        }
        ++it;
    }
}
static void tickWindscreenEjection(){
    if(!windscreenEjection||!ready||!recoveryAllowed()||!player()){windscreenRuns.clear();windscreenMarks.clear();windscreenHolds.clear();return;}
    windscreenNmFix();tickWindscreenHolds();
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    for(auto it=windscreenRuns.begin();it!=windscreenRuns.end();)if(!vehicleFromRef(it->first))it=windscreenRuns.erase(it);else ++it;
    for(const auto&[ref,before]:beforeVehicleVelocity){
        void*v=vehicleFromRef(ref);if(!v||!matrix(v))continue;
        const int type=at<int>(v,0x594),status=at<unsigned char>(v,0x36)>>3;
        if((type!=0&&type!=1)||at<short>(v,0x22)==432||status==1||status==5)continue; // car/monster only; no Rhino (IV apc), recorded or wrecked
        void*seat[2]={at<void*>(v,0x460),at<void*>(v,0x464)}; // IV seats 0 and 2: driver and front passenger
        if(!seat[0]&&!seat[1]){windscreenRuns.erase(ref);continue;}
        const V now=at<V>(v,0x44)*50.f,d=now-before;
        auto&run=windscreenRuns[ref];
        // IV's impact is one frame's impulse; our contact can spread over two frames, so that pair counts once.
        V best=d;if(run.lastFrame&&frame-run.lastFrame==1&&length(run.last+d)>length(d))best=run.last+d;
        run.last=d;run.lastFrame=frame;
        // Tests only: GTASA_WINDSCREEN_TEST_DV=x replaces both thresholds (exercise the detach on a small frontal hit).
        static const float testDv=[]{char b[16]{};return GetEnvironmentVariableA("GTASA_WINDSCREEN_TEST_DV",b,16)?float(std::atof(b)):0.f;}();
        const float pedDv=testDv>0?testDv:windscreenPedDv,playerDv=testDv>0?testDv:windscreenPlayerDv;
        const float dv=length(best);
        if(!(dv>pedDv)||dv>200.f||!finite(best))continue;
        const V dir=best*(1.f/dv);const float along=dot(normalized(matrix(v)->up),dir);
        if(windscreenAudit())event("windscreen_eval",-1,at<short>(v,0x22),"vehicle_ref="+std::to_string(ref)+" dv_mps="+std::to_string(dv)+" forward_dot="+std::to_string(along)+
            " frontal="+std::to_string(along<windscreenForwardDot)+" driver="+std::to_string(reference(seat[0]))+" passenger="+std::to_string(reference(seat[1])));
        if(!(along<windscreenForwardDot))continue;
        if(windscreenYields(v)){event("windscreen_yielded",ref,at<short>(v,0x22),"dv_mps="+std::to_string(dv));continue;}
        for(int s=0;s<2;s++){
            void*ped=seat[s];if(!ped||!windscreenOccupant(ped,v))continue;
            const bool isPlayer=ped==player();
            if(dv<=(isPlayer?playerDv:pedDv))continue;
            if(!isPlayer){
                // 0x823E8238..0x823E8264: a draw over 0.75 per check frame; one draw per crash here (consecutive frames are one crash).
                const bool again=run.decided[s]&&frame-run.decided[s]<=1;run.decided[s]=frame;if(again)continue;
                // Tests only: GTASA_WINDSCREEN_TEST_DRAW=1 makes every draw pass.
                static const bool forceDraw=GetEnvironmentVariableA("GTASA_WINDSCREEN_TEST_DRAW",nullptr,0)!=0;
                const float draw=forceDraw?1.f:reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(0.f,1.f);
                if(windscreenAudit())event("windscreen_draw",reference(ped),at<short>(ped,0x22),"draw="+std::to_string(draw)+" need_over="+std::to_string(windscreenChance));
                if(!(draw>windscreenChance))continue;
            }
            const auto pose=captureVehiclePose(ped,v);
            if(!pose){event("windscreen_pose_unavailable",reference(ped),at<short>(ped,0x22),"seated_pose_rejected no_ejection=1");continue;}
            WindscreenHold exit{ref};
            if(!windscreenPlane(v,exit.point,exit.normal)||!windscreenPlanExit(v,exit,rotate(conjugate(rotation(*matrix(v))),pose->sampledRoot.p-matrix(v)->pos))){
                event("windscreen_no_way_out",reference(ped),at<short>(ped,0x22),"vehicle_ref="+std::to_string(ref)+" stays_in_vehicle=1");continue;
            }
            // Jump vector -dv x impact dir (0x823E8454..0x823E8468); ped velocity = vehicle velocity (0x8247F840) plus the door-0x1C
            // impulse mass x (0.25 x vector + (0,0,3)) (0x824BD9D0..0x824BDA50).
            const V jump=V{}-dir*dv;const V launch=now+jump*windscreenAlong+V{0,0,windscreenUp};
            const int pedRef=reference(ped);const short model=at<short>(ped,0x22);
            if(!windscreenSetPedOut(ped,v)){event("windscreen_detach_failed",pedRef,model,"still_in_vehicle=1 no_ejection=1");continue;}
            // SA's SetPedOut places the ped at the door (1.3-1.7 m): the body leaves the cabin from its seat, as IV's clip does.
            const bool moved=movePhysicalRoot(ped,pose->sampledRoot.p,isPlayer);
            clearBikeNativeIgnore(ped,v);
            pose->velocity=launch*(1.f/50.f);pose->angular=V{};
            queuePhysicalExit(ped,pose,moved?position(ped):pose->sampledRoot.p,Reaction::VehicleExit);
            windscreenMarks[pedRef]={launch,gameTime()};
            windscreenShatter(v);
            exit.launch=launch;exit.until=gameTime()+1500;windscreenHolds[pedRef]=exit;
            // Tests only: GTASA_WINDSCREEN_TEST_STOP=1 stops the car dead, as a wall would.
            static const bool stopCar=GetEnvironmentVariableA("GTASA_WINDSCREEN_TEST_STOP",nullptr,0)!=0;if(stopCar)at<V>(v,0x44)={};
            event("windscreen_ejection",pedRef,model,"vehicle_ref="+std::to_string(ref)+" vehicle_model="+std::to_string(at<short>(v,0x22))+" seat="+std::to_string(s?2:0)+
                " dv_mps="+std::to_string(dv)+" forward_dot="+std::to_string(along)+" threshold="+std::to_string(isPlayer?playerDv:pedDv)+
                " vehicle_mps="+std::to_string(length(now))+" launch_mps="+std::to_string(length(launch))+" launch_vz="+std::to_string(launch.z)+" root_kept="+std::to_string(moved)+
                " iv_kind=6 door=0x1C added_launch=iv_0.25_vector_plus_3_up");
        }
    }
}
