#pragma once
// Local bridge fixture. A native vehicle hits a native pedestrian alongside
// a native breakable object. No damage, velocity or state is injected into it.
static int expansionFixture(int mode);
static struct {
    int ref=-1, model=-1;
    void* address=nullptr;
    V initial{};
    unsigned start=0, changedAt=0, staleFrames=0, samples=0;
    bool active=false;
} roadProp;

static void observeRoadProp(){
    if(!roadProp.active||!collision)return;
    auto*prop=reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(roadProp.ref);
    const bool changed=prop!=roadProp.address||(at<unsigned char>(prop,0x1C)&5)!=5||
        length(position(prop)-roadProp.initial)>.01f;
    if(!changed&&!roadProp.changedAt)return;
    const auto now=gameTime();
    if(!roadProp.changedAt){
        roadProp.changedAt=now;
        event("probe_road_prop_motion_started",roadProp.ref,roadProp.model);
    }
    ++roadProp.samples;
    if(collision->entityChunks.contains(roadProp.address))++roadProp.staleFrames;
    if(now-roadProp.changedAt<400)return;
    roadProp.active=false;
    event("probe_road_prop_after_impact",roadProp.ref,roadProp.model,
          "samples="+std::to_string(roadProp.samples)+" stale_frames="+std::to_string(roadProp.staleFrames)+
          " first_change_ms="+std::to_string(roadProp.changedAt-roadProp.start));
}

static int roadPropFixture(int mode){
    if(mode==293){
        if(!roadProp.changedAt)return -294;
        if(roadProp.active)return -295;
        return int(roadProp.staleFrames);
    }
    if(roadProp.active)return -296;
    if(roadProp.ref>=0){
        if(reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(roadProp.ref)){
            ProbeScript remove(0x0108);remove.integer(roadProp.ref);remove.run();
        }
        if(pedFromRef(expansionTarget)){
            ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
        }
        if(vehicleFromRef(probeCar)){
            ProbeScript remove(0x00A6);remove.integer(probeCar);remove.run();
        }
    }
    const int model=mode==291?1211:1224; // fire_hydrant, woodenbox
    // Object creation can stream/populate its neighborhood. Place it at the
    // end of the road fixture before spawning the live road actors.
    reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
    const V above{2414.5f,-1658.f,position(player()).z+5.f};
    const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
    ColPoint ground{};void*support=nullptr;
    if(!vertical(above,above.z-12,ground,support,true,false,false,true,true,false,nullptr))return -297;
    ProbeScript create(0x029B);
    create.integer(model);create.number(above.x);create.number(above.y);
    create.number(ground.point.z+.48f);create.result();
    const int ref=create.run();
    if(ref<0)return -292;
    if(carFixture(445)<0)return -291;
    const int pedRef=expansionFixture(133);
    if(pedRef<0)return -291;
    expansionTarget=pedRef;
    auto*car=vehicleFromRef(probeCar);auto*ped=pedFromRef(expansionTarget);
    if(!car||!ped||!matrix(car)||!matrix(ped))return -291;
    // Same lane and in this order along travel: front bumper, pedestrian, prop.
    const V forward=matrix(car)->up,right=matrix(car)->right;
    auto*prop=reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(ref);
    if(!prop||!collision)return -292;
    collision->rebuild(position(player()));
    if(!collision->entityChunks.contains(prop))return -293;
    if(pedFromRef(pedRef)!=ped||vehicleFromRef(probeCar)!=car)return -298;
    const V gap=position(prop)-position(ped);
    if(dot(position(ped)-position(car),forward)<=0||dot(gap,forward)<=0||std::abs(dot(gap,right))>.10f)return -299;
    roadProp={ref,model,prop,position(prop),gameTime(),0,0,0,true};
    event("probe_road_prop_cached",ref,model,"ped_ref="+std::to_string(pedRef)+
          " car_to_ped_m="+std::to_string(dot(position(ped)-position(car),forward))+
          " ped_to_prop_m="+std::to_string(dot(position(prop)-position(ped),forward))+
          " prop_lateral_m="+std::to_string(dot(position(prop)-position(ped),right)));
    return expansionFixture(mode==295?238:mode==296?239:135); // 35, 45, or 15 m/s
}
