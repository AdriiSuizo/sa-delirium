#pragma once
// Actual native world collision fixture. No knock-off/damage event is injected;
// one initial velocity is written only after the native seated bike has settled.
struct BikeCrashProbeState {int rider=-1,bike=-1,mode=0;V direction{},wall{},wallNormal{};unsigned prepared=0;bool launched=false;void*wallEntity=nullptr;};
static BikeCrashProbeState bikeCrashProbe;
static bool bikeProbeFloor(V from,ColPoint&hit){
    void*entity=nullptr;from.z+=2;
    return reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
        (from,from.z-5,hit,entity,true,false,false,true,true,false,nullptr)&&hit.normal.z>.8f;
}
static bool bikeProbeWall(V here,int mode,float approach,V&start,V&direction,V&wall,V&normal,void*&wallEntity){
    ColPoint base{};if(!bikeProbeFloor(here,base))return false;
    const V origin{here.x,here.y,base.point.z+.65f};
    // Verify a vertical solid surface and a level, unobstructed measured
    // approach in the loaded scene. Never assume a hard-coded house is solid.
    for(int i=0;i<32;i++){
        const float angle=i*6.28318530718f/32;V ray{std::cos(angle),std::sin(angle),0};
        const V end=origin+ray*25.f;ColPoint hit{};void*entity=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,const V&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool)>(0x56BA00)
            (origin,end,hit,entity,true,false,false,false,false,false,false,false)||!entity||(at<unsigned char>(entity,0x36)&7)!=1||std::abs(hit.normal.z)>.15f||length(hit.point-origin)<8)continue;
        V n=normalized(V{hit.normal.x,hit.normal.y,0});if(dot(n,ray)>0)n=n*-1;
        V dir=n*-1;
        if(mode==204||mode==205){const float side=mode==204?1.f:-1.f;dir=dir*.8660254f+V{-dir.y,dir.x,0}*(side*.5f);}
        V launch=hit.point-dir*approach;ColPoint ground{},middle{},wallFloor{};
        if(length(V{launch.x-here.x,launch.y-here.y,0})<2.5f)continue;
        if(!bikeProbeFloor(launch,ground)||!bikeProbeFloor(hit.point-dir*(approach*.5f),middle)||!bikeProbeFloor(hit.point-dir*1.2f,wallFloor))continue;
        if(std::abs(ground.point.z-middle.point.z)>.2f||std::abs(wallFloor.point.z-middle.point.z)>.2f)continue;
        // A wall is a full panel of the same building, not a small breakable
        // mailbox or a thin curb. All approach rays include dynamic blockers:
        // accepting only the centre line let the previous fixture hit a mailbox
        // and coast through its former plane before reaching a different house.
        const V side{-dir.y,dir.x,0};bool panel=true;
        for(float dx:{-.55f,0.f,.55f})for(float height:{.35f,.8f,1.3f}){
            V from{launch.x+side.x*dx,launch.y+side.y*dx,ground.point.z+height};
            V to=from+dir*(approach+.5f);ColPoint verify{};void*target=nullptr;
            if(!reinterpret_cast<bool(__cdecl*)(const V&,const V&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool)>(0x56BA00)
                (from,to,verify,target,true,true,true,true,true,false,false,false)||target!=entity||
                dot(normalized(verify.normal),n)<.98f||std::abs(dot(verify.point-hit.point,n))>.15f)panel=false;
        }
        if(!panel)continue;
        start={launch.x,launch.y,ground.point.z+.8f};direction=dir;wall=hit.point;normal=n;wallEntity=entity;return true;
    }
    return false;
}
static int bikeCrashFixture(int mode){
    if(mode<200||mode>209||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -200;
    if(mode<=205){
        if(!player()||inVehicle(player())||agents.contains(reference(player()))||pending.contains(reference(player())))return -201;
        if(bikeCrashProbe.rider>=0)return -202; // one controlled setup per case
        float approach=7.f;
        if(!entryProbeNumber("GTASA_BIKE_APPROACH_METRES",7.f,1.35f,12.f,approach))return -209;
        V start{},direction{},wall{},normal{};void*wallEntity=nullptr;
        if(!bikeProbeWall(position(player()),mode,approach,start,direction,wall,normal,wallEntity)){event("probe_bike_crash_no_wall",-1,-1,"verified_building_panel_and_level_approach_missing");return -203;}
        const int model=(mode==202||mode==203)?481:461;
        for(int asset:{model,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(asset,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        ProbeScript car(0x00A5);car.integer(model);car.number(start.x);car.number(start.y);car.number(start.z);car.result();const int bike=car.run();
        void*v=vehicleFromRef(bike);if(!v)return -204;
        ProbeScript heading(0x0175);heading.integer(bike);heading.number(std::atan2(-direction.x,direction.y)*57.2957795f);heading.run();
        int rider=-1;
        if(mode==201||mode==203||mode==205){ProbeScript seat(0x036A);seat.integer(reference(player()));seat.integer(bike);seat.run();rider=reference(player());}
        else{ProbeScript seat(0x0129);seat.integer(bike);seat.integer(4);seat.integer(7);seat.result();rider=seat.run();}
        void*p=pedFromRef(rider);if(!p||!ridingBike(p)||at<void*>(p,0x58C)!=v)return -205;
        at<V>(v,0x44)={};at<V>(v,0x50)={};
        bikeCrashProbe={rider,bike,mode,direction,wall,normal,gameTime(),false,wallEntity};watchSessionPed(p);
        event("probe_bike_crash_prepared",rider,at<short>(p,0x22),"mode="+std::to_string(mode)+" bike="+std::to_string(bike)+" model="+std::to_string(model)+
            " wall_x="+std::to_string(wall.x)+" wall_y="+std::to_string(wall.y)+" wall_entity="+std::to_string(uintptr_t(wallEntity))+" wall_model="+std::to_string(at<short>(wallEntity,0x22))+" start_x="+std::to_string(start.x)+" start_y="+std::to_string(start.y)+" approach_metres="+std::to_string(approach)+" panel_rays=9 no_injected_event=1");
        return rider;
    }
    auto&s=bikeCrashProbe;void*p=pedFromRef(s.rider),*v=vehicleFromRef(s.bike);
    if(!p||!v||!matrix(v))return -206;
    if(mode==209){
        event("probe_bike_crash_observation",s.rider,at<short>(p,0x22),"in_vehicle="+std::to_string(inVehicle(p))+" active="+std::to_string(agents.contains(s.rider))+
            " bike_mps="+std::to_string(length(at<V>(v,0x44))*50)+" rider_mps="+std::to_string(length(at<V>(p,0x44))*50)+" health="+std::to_string(at<float>(p,0x540))+
            " wall_distance="+std::to_string(dot(position(v)-s.wall,s.wallNormal)));
        return s.rider;
    }
    if(s.launched||!ridingBike(p)||at<void*>(p,0x58C)!=v||gameTime()-s.prepared<1500)return -207;
    if(dot(normalized(matrix(v)->up),s.direction)<.98f||length(at<V>(v,0x44))*50>.5f){event("probe_bike_crash_not_settled",s.rider,-1);return -208;}
    // Shorter approaches let low-speed coasting reach the wall before native
    // rider braking stops it. Reject an already overlapping initial bound;
    // never create a crash by placing the bike through the surface.
    auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(v);if(!col)return -210;
    float wallGap=1.e10f;
    for(int corner=0;corner<8;corner++){
        const V local{corner&1?col->box.max.x:col->box.min.x,corner&2?col->box.max.y:col->box.min.y,corner&4?col->box.max.z:col->box.min.z};
        wallGap=std::min(wallGap,dot(point(*matrix(v),local)-s.wall,s.wallNormal));
    }
    if(!std::isfinite(wallGap)||wallGap<=.025f){event("probe_bike_crash_initial_overlap",s.rider,-1,"native_col_plane_gap="+std::to_string(wallGap));return -211;}
    const float speed=mode==206?4.f:mode==207?10.f:20.f;
    at<V>(v,0x44)=s.direction*(speed/50.f);at<V>(v,0x50)={};s.launched=true;
    event("probe_bike_crash_launched",s.rider,at<short>(p,0x22),"bike="+std::to_string(s.bike)+" initial_mps="+std::to_string(speed)+" native_col_plane_gap="+std::to_string(wallGap)+" coasting_only=1 knockoff_event_injected=0");
    return s.rider;
}
