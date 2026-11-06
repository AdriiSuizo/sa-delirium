#pragma once
static int expansionTarget=-1;
static int expansionAttacker=-1;
static int bumpProbeWalker=-1;static V bumpProbeGoal{};
static int rearTrafficBike=-1;
static int carBikeApproach=0; // 0 rear, 1 right, 2 front, 3 left (modes 261..264)
static int expansionFixture(int mode){
    if(mode>=253&&mode<=255){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -253;
        const auto roadGround=[](V& p){
            bool found=false;void* support=nullptr;
            const float z=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(p.x,p.y,p.z,&found,&support);
            if(!found||!std::isfinite(z)||z<40.f||z>62.f)return false;
            p.z=z+.8f;return true;
        };
        if(mode==253){
            V observer{2639.8f,-1210.f,65.f};
            reinterpret_cast<void(__cdecl*)(const V*)>(0x40ED80)(&observer);
            if(!roadGround(observer))return -253;
            clear("probe_rear_bike_route");
            teleport(player(),observer);at<V>(player(),0x44)={};
            return reference(player());
        }
        const V here=position(player());
        if(std::abs(here.x-2639.8f)>2.f||std::abs(here.y+1210.f)>2.f)return -254;
        if(mode==254){
            for(int model:{492,581,523,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
            reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
            auto spawn=[&](int model,V p){
                if(!roadGround(p)){
                    event("probe_rear_bike_ground_missing",-1,model,"x="+std::to_string(p.x)+" y="+std::to_string(p.y));
                    return -1;
                }
                ProbeScript create(0x00A5);create.integer(model);create.number(p.x);create.number(p.y);create.number(p.z);create.result();
                const int ref=create.run();auto*v=vehicleFromRef(ref);
                if(!v){event("probe_rear_bike_spawn_missing",ref,model);return -1;}
                reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(v,0.f);return ref;
            };
            probeCar=spawn(492,{2644.8f,-1217.f,65.f});
            probeBike=spawn(581,{2645.2f,-1208.5f,65.f});
            rearTrafficBike=spawn(523,{2645.23f,-1201.9f,65.f});
            void*bike=vehicleFromRef(probeBike);if(!vehicleFromRef(probeCar)||!bike||!vehicleFromRef(rearTrafficBike))return -254;
            auto*person=reinterpret_cast<void*(__cdecl*)(void*,bool,int,int,bool,bool)>(0x613A00)(bike,true,0,-1,true,false);
            if(!person||!ridingBike(person)){
                event("probe_rear_bike_rider_missing",probeBike,581,"native_ped_created="+std::to_string(person!=nullptr));
                return -254;
            }
            ProbeScript hold(0x00AD);hold.integer(probeBike);hold.number(0.f);hold.run();
            probeBikePed=reference(person);watchSessionPed(person);return probeBikePed;
        }
        void* rider=pedFromRef(probeBikePed);
        void* car=vehicleFromRef(probeCar),*bike=vehicleFromRef(probeBike),*traffic=vehicleFromRef(rearTrafficBike);
        if(!rider||!ridingBike(rider)||!car||!bike||!traffic)return -255;
        // Initial velocities only. All impacts, rider ejection and road loads are native.
        at<unsigned char>(car,0xB8)=at<unsigned char>(bike,0xB8)=at<unsigned char>(traffic,0xB8)=0;
        at<V>(car,0x44)=matrix(car)->up*(29.59f*.02f);
        at<V>(bike,0x44)=matrix(bike)->up*(15.19f*.02f);
        at<V>(traffic,0x44)=matrix(traffic)->up*(12.f*.02f);
        watchRoadVehicle(probeCar,probeBikePed);watchRoadVehicle(rearTrafficBike);
        event("probe_rear_bike_traffic",probeBikePed,at<short>(rider,0x22),"models=492,581,523 native_collision_only=1");
        return probeBikePed;
    }
    if(mode>=261&&mode<=264){carBikeApproach=mode-261;return 0;}
    if(mode==250){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -250;
        for(int model:{461,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        const V origin=position(player());ProbeScript create(0x00A5);create.integer(461);create.number(origin.x+8);create.number(origin.y);create.number(origin.z);create.result();probeBike=create.run();
        void*bike=vehicleFromRef(probeBike);if(!bike)return -250;
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(bike,-1.57079632679f);
        // Actual population creation, not a mission ped's harder knock-off
        // setting. 613A00 selects a streamed civilian and seats it natively.
        void*person=reinterpret_cast<void*(__cdecl*)(void*,bool,int,int,bool,bool)>(0x613A00)(bike,true,0,-1,true,false);
        if(!person||!ridingBike(person))return -250;
        const int rider=probeBikePed=reference(person);watchSessionPed(person);
        // A normal rider waiting in traffic: let SA brake/hold the bike through
        // its own autopilot, rather than freezing a physical owner or changing
        // crash eligibility. A second moving car supplies the entire impact.
        ProbeScript stop(0x00AD);stop.integer(probeBike);stop.number(0.f);stop.run();
        const int carRef=carFixture(445);void*car=vehicleFromRef(carRef);if(!car)return -250;
        // Approach side chosen by mode 261..264 (rear, right, front, left of
        // the rider). The car faces the bike; only its later coast moves it.
        const V location=position(bike),forward=matrix(bike)->up,right=matrix(bike)->right;
        const V away=carBikeApproach==1?right*9.f:carBikeApproach==2?forward*15.f:carBikeApproach==3?right*-9.f:forward*-15.f;
        teleport(car,V{location.x+away.x,location.y+away.y,position(car).z});
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car,std::atan2(away.x,-away.y));
        at<V>(car,0x44)={};at<V>(car,0x50)={};
        // CJ must not stand between the approaching car and the motorcycle.
        // Setup only; no continuous steering, damage or speed injection.
        // Front/rear hits at 45 m/s throw the rider 40-60 m along the road and
        // SA's CPopulation::ManagePed (0x611FC0) removes a civilian beyond
        // 54.5 m of CJ: stand CJ down the throw, beside the road. A head-on
        // car carries the rider on its bonnet for ~80 m (measured), a rear one throws it ~20 m;
        // the bike must stay inside the 40 m activation radius.
        // CJ within 25 m: SA's ManagePed removes an off-screen civilian past 25 m
        // as soon as it leaves the bike (the head-on rider vanished at 35 m, 06/11).
        if(carBikeApproach==0||carBikeApproach==2)teleport(player(),location-normalized(away)*20.f+right*6.f);
        else teleport(player(),position(player())+V{0,-4.f,0});
        event("probe_car_bike_setup",rider,at<short>(person,0x22),"vehicle="+std::to_string(carRef)+" bike="+std::to_string(probeBike)+" approach="+std::string(carBikeApproach==1?"right":carBikeApproach==2?"front":carBikeApproach==3?"left":"rear")+" native_collision_only=1");return rider;
    }
    if(mode==251||mode==252){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -251;
        void*car=vehicleFromRef(probeCar);void*bike=vehicleFromRef(probeBike);auto*rider=pedFromRef(probeBikePed);
        if(!car||!bike||!rider||!ridingBike(rider))return -251;
        const float speed=mode==251?25.f:45.f;at<unsigned char>(car,0xB8)=0;at<V>(car,0x44)=matrix(car)->up*(speed*.02f);
        watchRoadVehicle(probeCar,probeBikePed);
        event("probe_car_bike_coast",probeBikePed,at<short>(rider,0x22),"speed_mps="+std::to_string(speed)+" car_x="+std::to_string(position(car).x)+" car_y="+std::to_string(position(car).y)+" bike_x="+std::to_string(position(bike).x)+" bike_y="+std::to_string(position(bike).y)+" synthetic_damage=0");return probeBikePed;
    }
    if(mode==249){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!player()||inVehicle(player()))return -249;
        const int ref=carFixture(445);auto*car=vehicleFromRef(ref);if(!car)return -249;
        const V here=position(player());teleport(car,here+V{-5.f,0,0});
        reinterpret_cast<void(__thiscall*)(void*,void*,bool)>(0x6B3780)(car,nullptr,false);
        event("probe_native_car_explosion",reference(player()),0,"vehicle="+std::to_string(ref)+" native_blow_up=1");return reference(player());
    }
    if(mode==237){
        if(!combatAllowed()||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||
           !GetEnvironmentVariableA("GTASA_PHYSICS_PROFILE",nullptr,0))return -237;
        // Reproducible native crowd, no damage/velocity/physics parameters.
        // Disjoint starting capsules walk across each other on Grove's flat road.
        const V origin=position(player());int count=0;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        for(int row=0;row<2;row++)for(int col=0;col<7;col++){
            const V at=origin+V{(col-3)*.9f,3.f+row*3.f,0.f};
            void*p=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,at,true);if(!p)continue;
            const V goal=at+V{0,row?-6.f:6.f,0};
            void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x30);if(!t)return -237;
            reinterpret_cast<void*(__thiscall*)(void*,int,const V&,float,bool,bool)>(0x667CD0)(t,4,goal,.15f,false,false);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,3,true);watchSessionPed(p);count++;
        }
        event("probe_performance_crowd",-1,7,"native_walkers="+std::to_string(count)+" damage_injected=0 velocity_injected=0");return count;
    }
    if(mode>=186&&mode<=188){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||entryProbeRef>=0)return -186;
        const int car=carFixture(445);auto*v=vehicleFromRef(car);if(!v||!matrix(v))return -186;
        // Grove's straight road, not the houses north of the cul-de-sac.
        // Scene setup only: the shared fixture is on the pavement. Put this
        // route in the road before creating the native boarding task.
        ProbeScript road(0x00AB);road.integer(car);road.number(2430.f);road.number(-1661.f);road.number(position(v).z);road.run();
        ProbeScript heading(0x0175);heading.integer(car);heading.number(90.f);heading.run();
        V approach{};reinterpret_cast<V*(__cdecl*)(V*,const void*,int)>(0x64E740)(&approach,v,10);
        approach=approach-matrix(v)->right*.8f;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(7,2);reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        void*p=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,approach,true);if(!p)return -186;
        if(mode==188)at<float>(p,0x540)=12.f; // Isolated death-accounting fixture, never normal play.
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x50);if(!t)return -186;
        reinterpret_cast<void*(__thiscall*)(void*,void*)>(0x6402F0)(t,v);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,3,true);
        entryProbeRef=reference(p);entryProbeCar=car;entryProbeSpeed=mode==187?3.f:4.5f;entryProbeStarted=gameTime();entryProbeAccelerated=false;
        roadProbePed=entryProbeRef;roadProbeVehicle=car;roadProbeSide=matrix(v)->right*-1.f;
        roadProbeForward=matrix(v)->up;roadProbeCameraUntil=gameTime()+25000;
        event("probe_entry_created",entryProbeRef,7,"native_enter_task=1 vehicle_ref="+std::to_string(car)+" initial_health="+std::to_string(at<float>(p,0x540)));return entryProbeRef;
    }
    if(mode>=180&&mode<=185){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||expansionFixture(150)<0)return -180;
        combatProbeGymPassive=mode>=183;const int style=5+(mode-180)%3;
        void*p=pedFromRef(expansionTarget);teleport(player(),position(p)+V{0,-.9f,0});
        combatProbeGymOriginal=at<unsigned char>(player(),0x72D);combatProbeGymRef=reference(player());
        at<unsigned char>(player(),0x72D)=static_cast<unsigned char>(style);
        combatProbeGymStart=gameTime();combatProbeGymUntil=combatProbeGymStart+7000;combatProbeUntil=combatProbeGymUntil;
        combatTargets[expansionTarget]={reference(player()),gameTime()};
        event("probe_gym_chain",expansionTarget,7,"style="+std::to_string(style)+" repeated_kick=1 forced_clips=0 passive_target="+std::to_string(combatProbeGymPassive));return expansionTarget;
    }
    if(mode==179){
        if(!combatAllowed()||!pedFromRef(expansionTarget)||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -179;
        combatProbeButtons=0x10u;combatProbeUntil=gameTime()+350;
        if(GetEnvironmentVariableA("GTASA_TEST_ALTERNATE_COMBO",nullptr,0)){
            combatProbeButtons=0;combatProbeAlternateStart=gameTime();
            combatProbeUntil=combatProbeAlternateStart+1800;
        }
        for(unsigned delay:{160u,260u,340u,500u})meleeFrames.push_back({gameTime()+delay,gameTime(),expansionTarget});
        event("probe_combat_alternate",expansionTarget,7,"button=triangle_y iv_bit=16");return expansionTarget;
    }
    if(mode==176||mode==177){
        void*w=pedFromRef(bumpProbeWalker),*target=pedFromRef(expansionTarget);
        if(!w||!target||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -176;
        const float side=mode==176?.25f:-.25f;V start=position(w);start.x=position(target).x+side;bumpProbeGoal.x=start.x;teleport(w,start);
        event("probe_standing_bump_side",bumpProbeWalker,0,"lateral_m="+std::to_string(side));return bumpProbeWalker;
    }
    if(mode==172||mode==173){
        if(!combatAllowed()||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -172;
        combatProbeUntil=0;combatClear();
        if(expansionFixture(80)<0)return -172;void*target=pedFromRef(expansionTarget);
        const V start=position(target)+V{0,-1.2f,0};bumpProbeGoal=position(target)+V{0,1.5f,0};
        void*walker=player();
        if(mode==173){walker=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,start,true);if(!walker)return -173;}
        else teleport(walker,start);
        bumpProbeWalker=reference(walker);at<V>(walker,0x44)={};
        // The NPC must not wander a metre sideways while the render/capture
        // fixture gets ready. This idle is replaced by the native go-to task.
        void*idle=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);if(!idle)return -173;
        reinterpret_cast<void*(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(idle,30000,false,false,8.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(walker),idle,3,true);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(walker,0.f);at<float>(walker,0x558)=at<float>(walker,0x55C)=0;
        watchSessionPed(walker);watchSessionPed(target);
        event("probe_standing_bump_setup",expansionTarget,7,"walker="+std::to_string(bumpProbeWalker)+" health="+std::to_string(at<float>(target,0x540))+" mode="+std::to_string(mode));return expansionTarget;
    }
    if(mode==174||mode==175){
        void*walker=pedFromRef(bumpProbeWalker);if(!walker||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -174;
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x30);if(!t)return -174;
        // SA: WALK=4, RUN=6. Player go-to's target blend ignores WALK when
        // MoveTowardsTargetPoint is true; use its state-based blend for walk.
        reinterpret_cast<void*(__thiscall*)(void*,int,const V&,float,bool,bool)>(0x667CD0)(t,mode==174?6:4,bumpProbeGoal,.15f,mode==174,false);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(walker),t,3,true);
        event("probe_standing_bump_walk",bumpProbeWalker,at<short>(walker,0x22),"native_task_only=1 damage_injected=0 velocity_injected=0");return bumpProbeWalker;
    }
    if(mode>=165&&mode<=168){
        if(expansionFixture(150)<0)return -165;
        static bool placed=false;static V origin{};
        void*p=pedFromRef(expansionTarget);if(!placed){origin=position(p);placed=true;}
        // Each fresh pair stays on the first verified street patch. Repeated
        // relative spawning previously walked the last fixture into a house.
        teleport(p,origin+V{(mode==165||mode==168)?2.5f:0.f,mode>=167?-3.5f:0.f,0});
        const float distance=mode==165?1.35f:mode==166?.9f:mode==167?2.1f:1.5f;
        teleport(player(),position(p)+V{0,-distance,0});
        event("probe_combat_range_setup",expansionTarget,at<short>(p,0x22),"distance="+std::to_string(distance)+" mode="+std::to_string(mode));return expansionTarget;
    }
    if(mode==169){
        if(!combatAllowed()||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -169;
        combatProbeButtons=0x10u;combatProbeUntil=gameTime()+350;
        for(unsigned delay:{160u,260u,340u,500u})meleeFrames.push_back({gameTime()+delay,gameTime(),expansionTarget});return expansionTarget;
    }
    if(mode==164){
        if(!combatAllowed()||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -164;
        void*p=pedFromRef(expansionAttacker),*from=pedFromRef(expansionTarget);if(!p||!from)return -164;
        void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x40);
        reinterpret_cast<void*(__thiscall*)(void*,void*,bool,float,int,int,float)>(0x65C430)(task,from,false,30.f,15000,1000,1.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),task,1,true);
        event("probe_combat_native_flee",expansionAttacker,at<short>(p,0x22),"secondary_fight_not_cleared_by_fixture=1");return expansionAttacker;
    }
    if(mode==214||mode==215){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||expansionFixture(150)<0)return -214;
        if(mode==215){
            void*p=pedFromRef(expansionTarget);void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x40);
            if(!task)return -215;
            reinterpret_cast<void*(__thiscall*)(void*,void*,bool,float,int,int,float)>(0x65C430)(task,player(),false,40.f,1000000,1000,1.f);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),task,1,true);
        }
        event("probe_civilian_event_setup",expansionTarget,7,"prior_flee="+std::to_string(mode==215)+" no_combat_task_injected=1");return expansionTarget;
    }
    if(mode==228){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||expansionFixture(133)<0)return -228;
        void*first=pedFromRef(expansionTarget);const V base=position(first);teleport(first,base-roadProbeSide*.42f);std::string refs=std::to_string(expansionTarget);
        // GTASA_ROAD_GROUP_COUNT: group size (2..8, default 8).
        char countText[8]{};const int groupCount=GetEnvironmentVariableA("GTASA_ROAD_GROUP_COUNT",countText,8)?std::clamp(std::atoi(countText),2,8):8;
        for(int i=1;i<groupCount;i++){
            const int ref=expansionFixture(80);void*p=pedFromRef(ref);if(!p)return -228;
            V dest=base+roadProbeForward*(float(i/2)*1.0f)+roadProbeSide*(i%2?.42f:-.42f);
            teleport(p,dest);at<V>(p,0x44)={};refs+=","+std::to_string(ref);
        }
        expansionTarget=roadProbePed;event("probe_road_group_setup",roadProbePed,7,"refs="+refs+" count="+std::to_string(groupCount)+" synthetic_damage=0");return roadProbePed;
    }
    if(mode==234||mode==235){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!pedFromRef(expansionTarget))return -234;
        if(mode==234){
            reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(346,2);reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
            ProbeScript give(0x01B2);give.integer(reference(player()));give.integer(22);give.integer(100);give.run();
            ProbeScript equip(0x01B9);equip.integer(reference(player()));equip.integer(22);equip.run();
        }else{
            void*p=pedFromRef(expansionTarget);void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x40);if(!task)return -235;
            reinterpret_cast<void*(__thiscall*)(void*,void*,bool,float,int,int,float)>(0x65C430)(task,player(),false,40.f,1000000,1000,1.f);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),task,1,true);
        }
        event("probe_civilian_followup",expansionTarget,7,"mode="+std::to_string(mode)+" after_real_accepted_hit=1");return expansionTarget;
    }
    if(mode==231){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -231;
        int hits=0;for(int axis=0;axis<3;axis++)for(float side:{-1.f,1.f}){
            Compressed vertices[3]{};const short points[3][2]={{-384,-384},{384,-384},{0,384}};
            for(int i=0;i<3;i++){short*row=&vertices[i].x;row[(axis+1)%3]=points[i][0];row[(axis+2)%3]=points[i][1];}
            Tri triangle{0,1,2,0,0};alignas(4) unsigned char plane[12]{};
            reinterpret_cast<void(__thiscall*)(void*,const Compressed*,Tri&)>(0x411660)(plane,vertices,triangle);
            ColSphere sphere{};(&sphere.pos.x)[axis]=side*.1f;sphere.radius=.2f;ColPoint cp{};float distance=100.f;
            const bool hit=reinterpret_cast<bool(__cdecl*)(const ColSphere&,const Compressed*,const Tri&,const void*,ColPoint&,float&)>(0x416BA0)(sphere,vertices,triangle,plane,cp,distance);
            if(hit)hits++;
            event("probe_native_triangle_sides",-1,-1,"axis="+std::to_string(axis)+" side="+std::to_string(side)+" hit="+std::to_string(hit)+" normal="+std::to_string(cp.normal.x)+","+std::to_string(cp.normal.y)+","+std::to_string(cp.normal.z)+" depth="+std::to_string(cp.depth));
        }return hits;
    }
    if(mode==230){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!collision||!collision->world)return -230;
        collision->rebuild(position(player()));int hits=0;
        for(float height:{-.7f,0.f,.7f})for(int i=0;i<32;i++){
            const float angle=float(i)*6.28318530718f/32.f;const V from=position(player())+V{0,0,height},dir{std::cos(angle),std::sin(angle),0};
            ColPoint cp{};void*entity=nullptr;
            if(!lineOfSight(from,from+dir*25.f,cp,entity,true,false,false,true,true,false,false,false)||!entity)continue;
            const float nativeDistance=length(cp.point-from);float distance=0;V normal{};
            const int imported=nm_world_raycast(collision->world,&from.x,&dir.x,25.f,&distance,&normal.x);
            event("probe_world_ray",reference(player()),at<short>(entity,0x22),"from="+std::to_string(from.x)+","+std::to_string(from.y)+","+std::to_string(from.z)+" dir="+std::to_string(dir.x)+","+std::to_string(dir.y)+","+std::to_string(dir.z)+" native="+std::to_string(nativeDistance)+" imported_hit="+std::to_string(imported)+" imported="+std::to_string(distance)+" normal="+std::to_string(cp.normal.x)+","+std::to_string(cp.normal.y)+","+std::to_string(cp.normal.z)+" point="+std::to_string(cp.point.x)+","+std::to_string(cp.point.y)+","+std::to_string(cp.point.z));hits++;
        }
        return hits;
    }
    if(mode==229){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -229;
        teleport(player(),V{2823.29f,-1783.915f,11.4f});at<V>(player(),0x44)={};
        event("probe_industrial_scene",reference(player()),0,"reported_manual_scene=1 wait_streaming_before_measurement=1");return reference(player());
    }
    if(mode==213){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||expansionFixture(150)<0)return -213;
        combatProbeLock=false;return expansionTarget;
    }
    if(mode==212){
        if(!combatAllowed()||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!pedFromRef(expansionTarget))return -212;
        combatProbeLock=false;combatProbeButtons=0x20u;combatProbeUntil=gameTime()+350;
        event("probe_unlocked_melee",expansionTarget,7,"attack_only=1 aim=0 force_clip=0");return expansionTarget;
    }
    if(mode>=150&&mode<=163){
        if(!combatAllowed()||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -150;
        combatProbeTimedDodge=false;combatProbeCounter=false;combatProbeFatalCounter=false;
        if(mode==150){
            combatProbeAlternateStart=0;combatProbeAlternateSent=false;
            combatProbeLock=true;
            if(expansionFixture(80)<0)return -150;
            void*p=pedFromRef(expansionTarget);V dest=position(p)+V{0,-1.15f,0};teleport(player(),dest);
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(player(),0.f);at<float>(player(),0x558)=at<float>(player(),0x55C)=0;
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(p,3.14159265f);at<float>(p,0x558)=at<float>(p,0x55C)=3.14159265f;
            ProbeScript equip(0x01B9);equip.integer(reference(player()));equip.integer(0);equip.run();
            combatProbeTarget=expansionTarget;combatProbeButtons=0;combatProbeUntil=gameTime()+15000;
            meleeProbeAttacker=reference(player());meleeProbeVictim=expansionTarget;meleeCapturedHits=0;
        }else if(mode==156){
            combatProbeUntil=0;combatTargets.clear();if(expansionFixture(80)<0||expansionFixture(95)<0)return -156;
            combatTargets[expansionTarget]={expansionAttacker,gameTime()};combatTargets[expansionAttacker]={expansionTarget,gameTime()};
            meleeProbeAttacker=expansionAttacker;meleeProbeVictim=expansionTarget;meleeCapturedHits=0;
        }else if(mode==157){
            combatProbeUntil=0;if(expansionFixture(80)<0)return -157;return expansionFixture(132);
        }else if(mode>=158&&mode<=163){
            if(expansionFixture(150)<0)return -158;void*npc=pedFromRef(expansionTarget);
            void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x38);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,int,int,int,char)>(0x620E30)(task,player(),-1,0,0,0,1);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(npc),task,3,true);
            combatTargets[expansionTarget]={reference(player()),gameTime()};
            combatProbeButtons=mode==159?0x40u:0u;combatProbeUntil=gameTime()+12000;combatProbeTimedDodge=mode==160||mode>=162;combatProbeCounter=mode>=162;
            combatProbeFatalCounter=mode==163;
            if(mode==161){at<float>(player(),0x540)=60.f;event("probe_combat_low_health",reference(player()),0,"initial_sa_health=60 real_npc_attack_no_injected_hit=1");}
        }else{
            combatProbeButtons=mode==152?0x20u:mode==153?0x80u:mode==154?0x40u:0u;
            combatProbeUntil=mode==155?0:gameTime()+(mode==151?12000:350);
            if(mode==152||mode==153)for(unsigned delay:{160u,260u,340u,500u})meleeFrames.push_back({gameTime()+delay,gameTime(),expansionTarget});
        }
        event("probe_combat_controller",expansionTarget,7,"mode="+std::to_string(mode)+" input_module_only=1");return expansionTarget;
    }
    if(mode==142){
        void*car=vehicleFromRef(probeCar);void*target=pedFromRef(expansionTarget);
        if(!car||!target||!matrix(car)||agents.contains(expansionTarget))return -142;
        auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);if(!col)return -142;
        V dest=position(car)+matrix(car)->up*(col->box.max.y+.7f);dest.z=position(target).z;
        teleport(target,dest);at<V>(target,0x44)={};
        event("probe_road_near_start",expansionTarget,-1,"bumper_gap_m=0.7 setup_only=1");return expansionTarget;
    }
    if(mode==140){
        void*car=vehicleFromRef(probeCar);if(!car||!player()||inVehicle(player()))return -140;
        ProbeScript seat(0x036A);seat.integer(reference(player()));seat.integer(probeCar);seat.run();
        if(!inVehicle(player())||at<void*>(car,0x460)!=player())return -140;
        // Driver/streaming performance must use the game's player camera,
        // rather than the body/car midpoint camera of the observer fixture.
        roadProbeCameraUntil=0;
        reinterpret_cast<void(__thiscall*)(void*)>(0x50BAB0)(reinterpret_cast<void*>(0xB6F028)); // CCamera::RestoreWithJumpCut, verified plugin-sdk SA
        event("probe_road_player_driver",expansionTarget,-1,"vehicle_ref="+std::to_string(probeCar)+" native_camera_restored=1");return expansionTarget;
    }
    if(mode==141){
        void*target=pedFromRef(expansionTarget);if(!target||agents.contains(expansionTarget))return -141;
        at<float>(target,0x540)=2000.f;
        event("probe_road_survival_fixture",expansionTarget,-1,"initial_health=2000 native_damage_preserved=1");return expansionTarget;
    }
    if(mode==139)return carFixture(445);
    if(mode==133){
        const int carRef=carFixture();void*car=vehicleFromRef(carRef);
        if(!car||!matrix(car))return -133;
        auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        if(!col)return -133;
        // Test along the flat road, not across both kerbs. A kerb strike was
        // lifting the chassis during the contact window and confounded the test.
        V oldFrom=position(car)+V{0,0,3},newFrom{2406.f,-1658.f,oldFrom.z};ColPoint oldGround{},newGround{};void*supportOld=nullptr,*supportNew=nullptr;
        const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
        if(!vertical(oldFrom,oldFrom.z-8,oldGround,supportOld,true,false,false,true,true,false,nullptr)||
           !vertical(newFrom,newFrom.z-8,newGround,supportNew,true,false,false,true,true,false,nullptr))return -133;
        V roadStart{newFrom.x,newFrom.y,position(car).z+newGround.point.z-oldGround.point.z};teleport(car,roadStart);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car,-1.5707963268f);
        // Keep the observer out of the car/body trajectory.
        // Centre the observer on the measured travel corridor so a fast hit
        // does not leave the production streaming/physics radius mid-test.
        teleport(player(),V{2425.f,-1653.f,newGround.point.z+1.f});
        const int ref=expansionFixture(80);void*target=pedFromRef(ref);if(!target)return -133;
        // A real coasting car, starting clear of a standing native ped. The
        // fixture neither injects damage nor creates a physical body itself.
        V dest=position(car)+matrix(car)->up*(col->box.max.y+5.f);
        V from=dest+V{0,0,3};ColPoint ground{};void*support=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,from.z-7,ground,support,true,false,false,true,true,false,nullptr))return -133;
        dest.z=ground.point.z+1.f;teleport(target,dest);
        roadProbePed=ref;roadProbeCaptured=false;
        roadProbeVehicle=carRef;roadProbeSide=matrix(car)->right;roadProbeForward=matrix(car)->up;roadProbeCameraUntil=gameTime()+15000;
        at<V>(target,0x44)={};at<V>(car,0x44)={};at<V>(car,0x50)={};
        const V toward=position(car)-dest;const float heading=std::atan2(-toward.x,toward.y);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(target,heading);
        at<float>(target,0x558)=at<float>(target,0x55C)=heading;
        event("probe_standing_car_setup",ref,at<short>(target,0x22),"vehicle_ref="+std::to_string(carRef)+" bumper_gap_m=5 synthetic_damage=0");
        return ref;
    }
    if((mode>=134&&mode<=138)||mode==143||mode==238||mode==239){
        if((mode==238||mode==239)&&!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -238;
        void*car=vehicleFromRef(probeCar);void*target=pedFromRef(expansionTarget);
        if(!car||!target||!matrix(car))return -134;
        const float speed=mode==134?8.f:mode==135?15.f:mode==137?10.f:mode==138?25.f:mode==238?35.f:mode==239?45.f:mode==143?4.f:2.f;
        // One initial velocity only: subsequent speed, damage and contact are
        // the live game's result; the existing idle flag wakes the car.
        at<unsigned char>(car,0xB8)=0;at<V>(car,0x44)=matrix(car)->up*(speed*.02f);
        watchRoadVehicle(probeCar,expansionTarget);
        bodyContactProbe=expansionTarget;bodyContactProbeUntil=gameTime()+9000;bodyContactProbeLogged=0;
        event("probe_standing_car_coast",expansionTarget,at<short>(target,0x22),"initial_mps="+std::to_string(speed)+" synthetic_damage=0");return expansionTarget;
    }
    if(mode==132){
        if(expansionFixture(95)<0)return -132;
        void*target=pedFromRef(expansionTarget);void*attacker=pedFromRef(expansionAttacker);
        if(!target||!attacker)return -132;
        finishMeleeContactProbe();
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(346,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        V dest=position(target)+V{0,6.f,1.f};ColPoint ground{};void*support=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (dest,dest.z-6,ground,support,true,false,false,true,true,false,nullptr))return -132;
        dest.z=ground.point.z+1.f;teleport(attacker,dest);
        ProbeScript give(0x01B2);give.integer(expansionAttacker);give.integer(22);give.integer(200);give.run();
        ProbeScript equip(0x01B9);equip.integer(expansionAttacker);equip.integer(22);equip.run();
        // Bounded fixture only: allow several actual AI shots at this target.
        at<float>(target,0x540)=2000.f;
        auto*info=reinterpret_cast<void*(__cdecl*)(int,unsigned char)>(0x743C60)(22,1);
        event("probe_native_pistol_attacker",expansionTarget,at<short>(target,0x22),"attacker_ref="+std::to_string(expansionAttacker)+" weapon=22 anim_group="+std::to_string(at<int>(info,0x1C))+" synthetic_damage=0");
        return expansionTarget;
    }
    if(mode==131){
        if(!collision||!player())return -131;
        const V origin=position(player());collision->update(origin);
        int nativeHits=0,missing=0,mismatch=0,recovered=0;
        for(int y=-5;y<=5;y++)for(int x=-5;x<=5;x++){
            const V from=origin+V{8.f*x,8.f*y,3.f};ColPoint ground{};void*support=nullptr;
            const bool native=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
                (from,from.z-12,ground,support,true,false,false,true,true,false,nullptr);
            if(!native||!support)continue;nativeHits++;
            V down{0,0,-1},normal{};float distance=0;
            bool hit=nm_world_raycast(collision->world,&from.x,&down.x,12,&distance,&normal.x)>0;
            if(!hit){collision->rebuild(origin,support);collision->applyVehicleBoxes();hit=nm_world_raycast(collision->world,&from.x,&down.x,12,&distance,&normal.x)>0;if(hit)recovered++;}
            const float error=hit?std::abs(from.z-distance-ground.point.z):-1.f;
            if(!hit)missing++;else if(error>.08f)mismatch++;
            event("probe_floor_sample",-1,at<short>(support,0x22),"x="+std::to_string(from.x)+" y="+std::to_string(from.y)+" native_z="+std::to_string(ground.point.z)+" physical_hit="+std::to_string(hit)+" error_m="+std::to_string(error)+" support_simple="+std::to_string(matrix(support)==nullptr));
        }
        event("probe_floor_grid",-1,-1,"native_hits="+std::to_string(nativeHits)+" missing="+std::to_string(missing)+" mismatched="+std::to_string(mismatch)+" recovered="+std::to_string(recovered)+" simple_transforms="+std::to_string(collision->simpleTransforms));
        return nativeHits>=50&&!missing&&!mismatch?nativeHits:-131;
    }
    if(mode==124){
        const int ref=expansionFixture(80);if(ref<0)return ref;
        // Blast on the far side of the target: CJ must remain outside its
        // radius so the subsequent native walking task is available.
        void*target=pedFromRef(ref);const V point=position(target)+V{5,0,0};
        explosion(point,10.f,5.f,nullptr,nullptr,false,1.f);
        event("probe_recovery_push_setup",ref,at<short>(target,0x22),"blast_away_from_walker=1");return ref;
    }
    if(mode==125){
        auto it=agents.find(expansionTarget);void*cj=player();
        if(it==agents.end()||!it->second->recovering||physicalTask(cj))return -125;
        const V target=contactPose(*it->second)[10].p;
        V from=target+V{.75f,0,2};ColPoint floor{};void*entity=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,from.z-5,floor,entity,true,true,false,true,true,false,nullptr))return -125;
        V start{from.x,from.y,floor.point.z+1.f};teleport(cj,start);
        const V goal=start+V{-2.f,0,0};
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(cj,1.5707963f);
        at<float>(cj,0x558)=at<float>(cj,0x55C)=1.5707963f;
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x30);if(!t)return -125;
        reinterpret_cast<void*(__thiscall*)(void*,int,const V&,float,bool,bool)>(0x667CD0)(t,4,goal,.15f,true,false);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(cj),t,0,true);routeTask=t;
        bodyContactProbe=expansionTarget;bodyContactProbeUntil=gameTime()+6000;bodyContactProbeLogged=0;
        event("probe_walk_into_recovery",expansionTarget,it->second->model,"native_walk=1 no_scripted_impulse=1");return reference(cj);
    }
    if(mode==127)return carFixture();
    if(mode==128||mode==178){
        if(mode==178&&!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -178;
        auto it=agents.find(expansionTarget);void*car=vehicleFromRef(probeCar);
        if(it==agents.end()||!car||!matrix(car))return -128;
        auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        if(!col||!col->data||col->data->lines!=4||!col->data->line)return -128;
        const auto&line=static_cast<SuspensionLine*>(col->data->line)[0];
        const V target=contactPose(*it->second)[10].p;
        // Keep the already settled car's actual ride height. Positioning it
        // at line.end + .15 made a sleeping car hover, then drop onto the body
        // when coasting woke it: that is not a light-contact fixture.
        ColPoint oldFloor{};void*oldEntity=nullptr;const V oldOrigin=position(car);
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (oldOrigin,oldOrigin.z-6,oldFloor,oldEntity,true,false,false,true,true,false,nullptr))return -128;
        const float clearance=oldOrigin.z-oldFloor.point.z;
        V dest=mode==178?position(it->second->ped):target-vector(*matrix(car),line.start)-matrix(car)->up*.35f;
        V from=dest+V{0,0,3};ColPoint floor{};void*entity=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,from.z-6,floor,entity,true,false,false,true,true,false,nullptr))return -128;
        dest.z=floor.point.z+clearance;
        teleport(car,dest);at<V>(car,0x44)={};at<V>(car,0x50)={};
        at<unsigned char>(car,0xB8)=0;
        if(mode==178){
            // Test setup only: let the car settle before the normal recovery
            // gate runs. The body keeps all simulation and collision ownership.
            it->second->probeRecoveryNotBefore=gameTime()+3000;
            const bool blocked=recoveryBlockingCar(it->second->ped)!=nullptr;
            // Exercise the recovery request at this known obstruction. This
            // deliberately tests the gate before the normal stability clock;
            // it must return false and retain live physics, not force get-up.
            const bool began=blocked?beginRecovery(*it->second):false;
            event("probe_car_clearance_placed",expansionTarget,it->second->model,"vehicle="+std::to_string(probeCar)+" native_blocked="+std::to_string(blocked)+" early_recovery_request=1 began="+std::to_string(began));
        }else{
            event("probe_wheel_body_placed",expansionTarget,it->second->model,"vehicle="+std::to_string(probeCar));
            // Diagnostic: name every native COL sphere the static world query
            // (same flags as CollisionWorld::rebuild) returns near the car.
            std::array<void*,256> nearby{};short n=0;
            reinterpret_cast<void(__cdecl*)(const V&,float,bool,short*,short,void**,bool,bool,bool,bool,bool)>(0x568B80)(dest,16.f,true,&n,256,nearby.data(),true,false,false,true,true);
            for(int k=0;k<std::clamp<short>(n,0,256);k++){
                void*e=nearby[k];auto*c=e?reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(e):nullptr;if(!c||!c->data||!c->data->sphere)continue;
                const M f=NativeWorldCoverage::frame(e);
                for(int i=0;i<c->data->spheres;i++){const auto&sp=c->data->sphere[i];const V w=point(f,sp.pos);if(length(w-dest)>15)continue;
                    event("probe_wheel_body_world_sphere",expansionTarget,at<short>(e,0x22),"type="+std::to_string(at<unsigned char>(e,0x36)&7)+" sphere="+std::to_string(i)+" x="+std::to_string(w.x)+" y="+std::to_string(w.y)+" z="+std::to_string(w.z)+" r="+std::to_string(sp.radius)+" entity_x="+std::to_string(f.pos.x)+" entity_y="+std::to_string(f.pos.y)+" entity_z="+std::to_string(f.pos.z)+" has_matrix="+std::to_string(matrix(e)!=nullptr)+" flags="+std::to_string(at<unsigned>(e,0x1C)));}
            }
        }
        return expansionTarget;
    }
    if(mode==129||mode==126){
        void*car=vehicleFromRef(probeCar);if(!car)return -129;
        const float speed=mode==126?2.f:4.f;
        at<V>(car,0x44)=matrix(car)->up*(speed*.02f);
        at<unsigned char>(car,0xB8)=0;
        wheelProbeBody=expansionTarget;wheelProbeCar=probeCar;wheelProbeUntil=gameTime()+8000;wheelProbeLogged=0;
        wheelProbePeakSpeed=wheelProbePeakUp=wheelProbePeakEnergy=0;
        event("probe_wheel_body_roll",expansionTarget,-1,"vehicle="+std::to_string(probeCar)+" initial_mps="+std::to_string(speed)+" native_coasting=1");return expansionTarget;
    }
    if(mode==120){
        const int ref=expansionFixture(80);if(ref<0)return ref;
        return DeliriumSA_ProbeGameDamage(ref,1);
    }
    if(mode==121){
        auto a=agents.find(expansionTarget);auto*cj=player();
        if(a==agents.end()||a->second->current[10].p.z-a->second->current[0].p.z>.25f||physicalTask(cj))return -121;
        auto*h=hierarchy(cj);V mid{};int count=0;
        if(h)for(int n=0;n<h->num;n++)if(h->nodes[n].id==43||h->nodes[n].id==44){mid=mid+h->matrices[n].pos;count++;}
        if(count!=2)return -121;mid=mid*.5f;const V here=position(cj);
        ColPoint floor{};void*support=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (here,here.z-3.f,floor,support,true,true,false,true,true,false,nullptr))return -121;
        const float clearance=here.z-floor.point.z;const V target=a->second->current[10].p;
        V destination=target-(mid-here);destination.z=target.z+2.f;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (destination,destination.z-5.f,floor,support,true,true,false,true,true,false,nullptr))return -121;
        destination.z=floor.point.z+clearance;teleport(cj,destination);
        footProbePed=reference(cj);footProbeBody=expansionTarget;footProbeUntil=gameTime()+25000;
        footFramesTaken=0;footFrameDue=0;footProbeLoadNs=0;
        walkingSupports.erase(footProbePed);event("probe_foot_support_stand",footProbePed,0,"body_ref="+std::to_string(expansionTarget));return footProbePed;
    }
    if(mode==122){
        auto*cj=player();if(physicalTask(cj))return -122;
        const V destination=position(cj)+normalized(matrix(cj)->up)*3.f;
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x30);if(!t)return -122;
        reinterpret_cast<void*(__thiscall*)(void*,int,const V&,float,bool,bool)>(0x667CD0)(t,4,destination,.15f,true,false);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(cj),t,0,true);routeTask=t;
        event("probe_foot_support_walk",reference(cj),0);return reference(cj);
    }
    if(mode==123){streamingRoute(56);footProbeUntil=0;footProbePed=-1;return reference(player());}
    if(mode==80){
        int ref=recoveryFixture(40);auto*p=pedFromRef(ref);if(!p)return ref;
        expansionTarget=ref;
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);
        reinterpret_cast<void*(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(t,30000,false,false,8.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,3,true);
        DeliriumSA_ProbeFocus(ref);return ref;
    }
    auto*p=pedFromRef(expansionTarget);
    if(mode==98){
        if(!p||!agents.contains(expansionTarget))return -98;
        const V before=position(p);
        // Exercise the same ownership cancellation as a protected boundary,
        // without faking mission flags, answering a phone or writing a save.
        clear("probe_protected_boundary");
        V from=position(p)+V{0,0,2.f};ColPoint ground{};void*support=nullptr;
        if(!reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,from.z-4.f,ground,support,true,true,false,true,true,false,nullptr))return -98;
        const float clearance=position(p).z-ground.point.z;
        event("probe_boundary_restoration",expansionTarget,at<short>(p,0x22),"before_z="+std::to_string(before.z)+" after_z="+std::to_string(position(p).z)+" clearance="+std::to_string(clearance));
        return clearance>=.65f&&!agents.contains(expansionTarget)?expansionTarget:-98;
    }
    if(mode>=100&&mode<=114){
        if(!p)return -90;
        const int id=214+(mode-100)/3;
        void*clump=at<void*>(p,0x18);
        // Inspect the installed native clips at stable phases; no hit, damage,
        // physical impulse or claim of gameplay acceptance is generated here.
        for(int old=214;old<=218;old++)if(void*a=reinterpret_cast<void*(__cdecl*)(void*,unsigned)>(0x4D68B0)(clump,old)){
            at<float>(a,0x18)=0;at<float>(a,0x1C)=-8;at<unsigned short>(a,0x2E)|=4u;
        }
        void*anim=reinterpret_cast<void*(__cdecl*)(void*,int,int,float)>(0x4D4610)(clump,33,id,1000.f);
        if(!anim)return -98;
        const float seconds=std::min(.10f+.13f*((mode-100)%3),at<float>(at<void*>(anim,0x14),0x10)-.02f);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x4CEB70)(anim,seconds);
        at<float>(anim,0x18)=1;at<float>(anim,0x1C)=0;at<float>(anim,0x24)=0;
        event("probe_melee_clip_pose",expansionTarget,at<short>(p,0x22),"anim="+std::to_string(id)+" seconds="+std::to_string(seconds));return expansionTarget;
    }
    if(mode==97){
        auto it=agents.find(expansionTarget);if(it==agents.end()||it->second->preview||it->second->recovering)return -97;
        auto&a=*it->second;stopBehaviour(a,"bodyBalance");stopBehaviour(a,"upperBodyFlinch");
        ok(nm_agent_task_catch_fall(a.agent,"*"));
        a.probeRecoveryNotBefore=gameTime()+8000;
        // Controlled fixture transition only, not evidence that two punches
        // always knock someone down. Subsequent ground hits are native AI.
        event("probe_ground_contact_setup",expansionTarget,a.model,"explicit_ground_hit_fixture_recovery_hold_max_8s_ends_on_new_hit");return expansionTarget;
    }
    if(mode==95){
        if(!p)return -90;
        V dest=position(p)+V{0,1.1f,0};
        V from=dest+V{0,0,3};ColPoint ground{};void*support=nullptr;
        if(reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)
            (from,from.z-6,ground,support,true,true,false,true,true,false,nullptr))dest.z=ground.point.z+1.f;
        void*attacker=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,dest,true);
        if(!attacker)return -95;expansionAttacker=reference(attacker);
        finishMeleeContactProbe();meleeProbeAttacker=expansionAttacker;meleeProbeVictim=expansionTarget;
        // CPed::m_nFightingStyle (SDK 0x72D), STYLE_STANDARD=4: explicitly
        // exercise the FightA animations supplied by Better Animations.
        at<unsigned char>(attacker,0x72D)=4;
        const V toward=position(p)-dest;const float heading=std::atan2(-toward.x,toward.y);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(attacker,heading);
        at<float>(attacker,0x558)=heading;at<float>(attacker,0x55C)=heading;
        void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x38);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,int,int,int,char)>(0x620E30)(task,p,-1,0,0,0,1);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(attacker),task,3,true);
        event("probe_native_melee_attacker",expansionTarget,at<short>(p,0x22),"attacker_ref="+std::to_string(expansionAttacker)+" fighting_style=4");return expansionTarget;
    }
    if(mode==96){
        finishMeleeContactProbe();
        auto*a=pedFromRef(expansionAttacker);if(!a)return expansionTarget>=0?expansionTarget:0;
        // The Fight task is secondary. Replacing only primary locomotion left
        // another already queued punch alive during the recovery observation.
        reinterpret_cast<void(__thiscall*)(void*,void*,int)>(0x681B60)(taskManager(a),nullptr,0);
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);
        reinterpret_cast<void*(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(t,30000,false,false,8.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(a),t,3,true);
        return expansionTarget;
    }
    if(mode==81||mode==83||mode==92){
        if(!p)return -90;
        for(int i=0;i<(mode==83?8:mode==92?2:1);i++)reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(p,player(),mode==83?25:0,mode==83?2:12,4,0);
        event("probe_expansion_hit",expansionTarget,at<short>(p,0x22),mode==83?"eight_shotgun_pellets":"native_melee_damage");return expansionTarget;
    }
    if(mode==82){
        auto it=agents.find(expansionTarget);if(it==agents.end()||it->second->lastRendered.empty())return -91;
        auto&a=*it->second;int tested=0,hits=0;const bool beforeCollision=(at<unsigned char>(p,0x1C)&1)!=0;
        for(int id:{8,4,2,32,22,42,52}){
            V centre=a.lastRendered[a.rig->index(id)].p;
            for(V axis:{V{1,0,0},V{0,1,0},V{0,0,1}}){
                ColPoint cp{};void*entity=nullptr;tested++;
                if(lineOfSight(centre-axis*.6f,centre+axis*.6f,cp,entity,true,true,true,true,false,false,false,false)&&entity==p)hits++;
            }
        }
        const bool movementCollision=(at<unsigned char>(p,0x1C)&1)!=0;
        event("probe_prone_hitboxes",expansionTarget,a.model,"queries="+std::to_string(tested)+" hits="+std::to_string(hits)+" movement_collision="+std::to_string(movementCollision));
        return hits>=5&&movementCollision==beforeCollision?hits:-92;
    }
    if(mode==84||mode==85){
        if(mode==85)p=player();if(!p||inVehicle(p))return -90;
        const float health=at<float>(p,0x540);const V point=position(p)-V{5,0,0};
        explosion(point,10.f,5.f,nullptr,nullptr,false,1.f);
        event("probe_midrange_explosion",reference(p),at<short>(p,0x22),"health_before="+std::to_string(health)+" health_after="+std::to_string(at<float>(p,0x540)));return reference(p);
    }
    if(mode==86||mode==93){
        if(inVehicle(player()))return -93;
        int car=carFixture();if(car<0)return car;
        ProbeScript seat(mode==93?0x0430:0x036A);seat.integer(reference(player()));seat.integer(car);if(mode==93)seat.integer(0);seat.run();
        return inVehicle(player())?reference(player()):-94;
    }
    if(mode==87||mode==88||mode==94||mode==170||mode==171){
        p=player();void*v=at<void*>(p,0x58C);if(!inVehicle(p)||vehicleRef(v)<0)return -93;
        const float speed=mode==88?10.f:mode==170?20.f:mode==171?45.f:30.f;
        at<V>(v,0x44)=matrix(v)->up*(speed/180.f);
        void*t=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x24);
        reinterpret_cast<void*(__thiscall*)(void*,void*,unsigned,void*)>(0x64BE40)(t,v,mode==94?8:10,nullptr);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),t,3,true);
        event("probe_native_jump_out_task",reference(p),0,"initial_kmh="+std::to_string(speed));return reference(p);
    }
    if(mode==90){
        p=player();V centre=position(p);if(auto it=agents.find(reference(p));it!=agents.end())centre=it->second->current[10].p;
        V from=centre+V{-2,-3,1},up{};
        reinterpret_cast<void(__thiscall*)(void*,const V*,const V*)>(0x50BEC0)(reinterpret_cast<void*>(0xB6F028),&from,&up);
        reinterpret_cast<void(__thiscall*)(void*,const V*,short,int)>(0x50C8B0)(reinterpret_cast<void*>(0xB6F028),&centre,2,1);return reference(p);
    }
    return -2;
}
