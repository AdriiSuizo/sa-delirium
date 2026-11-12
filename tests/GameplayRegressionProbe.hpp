#pragma once

static int gameplayRegressionFixture(int mode) {
    if (!ready||!playing()||!player()||area()!=0||scriptMissionActive()||
        !GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)) return -390;
    auto* cj=player();
    static int headshotTarget=-1;
    static int firearmTarget=-1, firearmShooter=-1, cameraTargets[2]={-1,-1};
    static int coverCop=-1;
    static bool farCover=false;
    if(mode>=1912&&mode<=1915){
        const int ref=expansionFixture(133);
        auto*ped=pedFromRef(ref);
        auto*car=vehicleFromRef(probeCar);
        if(!ped||!car||!matrix(car))return -1912;
        auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        if(!col)return -1912;
        if(mode==1915&&expansionFixture(142)<0)return -1915;
        // The pass-by must clear the animated arms as well as the ped root.
        const float lateral=mode==1912||mode==1915?0.f:col->box.max.x+(mode==1913?.12f:.85f);
        const V spot=position(ped)+matrix(car)->right*lateral;
        teleport(ped,spot);
        const V toward=position(car)-spot;
        const float heading=std::atan2(-toward.x,toward.y);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(ped,heading);
        at<float>(ped,0x558)=at<float>(ped,0x55C)=heading;
        at<float>(ped,0x540)=1200.f;
        if(GetEnvironmentVariableA("GTASA_FRAME_PROFILE",nullptr,0))roadProbeCaptured=true;
        event("probe_road_contact_case",ref,at<short>(car,0x22),"mode="+std::to_string(mode)+
            " lateral_m="+std::to_string(lateral)+" hull_max_x="+std::to_string(col->box.max.x));
        return expansionFixture(mode==1915?143:134);
    }
    if(mode==1910){
        const int ref=expansionFixture(133);
        auto* victim=pedFromRef(ref);
        auto* car=vehicleFromRef(probeCar);
        if(!victim||!car)return -1910;
        ProbeScript driver(0x0129);driver.integer(probeCar);driver.integer(4);driver.integer(7);driver.result();
        const int driverRef=driver.run();
        if(!pedFromRef(driverRef)||at<void*>(car,0x460)!=pedFromRef(driverRef))return -1910;
        at<float>(victim,0x540)=1200.f; // Preserve a real living recovery after the collision.
        event("probe_npc_runover",ref,at<short>(car,0x22),"driver_ref="+std::to_string(driverRef));
        return expansionFixture(134);
    }
    if(mode==1909){
        const int ref=expansionFixture(150);
        auto* target=pedFromRef(ref);
        if(!target)return -1909;
        combatProbeFatalKick=ref;
        return ref;
    }
    if(mode==1908){
        if(!gravity_control::engineGravity||inVehicle(cj))return -1908;
        auto* npc=pedFromRef(expansionFixture(133));
        auto* car=vehicleFromRef(probeCar);
        if(!npc||!car||at<void*>(car,0x460))return -1908;
        const auto measure=[](void* physical){
            auto& flags=at<unsigned>(physical,0x40);
            auto& collisionFlags=at<unsigned char>(physical,0x1C);
            const unsigned saved=flags;
            const unsigned char savedCollision=collisionFlags;
            const float old=at<float>(physical,0x4C);
            flags=(flags|2u)&~0x60u;collisionFlags|=1u;
            reinterpret_cast<void(__thiscall*)(void*)>(gravity_control::gravityAddress)(physical);
            const float delta=old-at<float>(physical,0x4C);
            at<float>(physical,0x4C)=old;flags=saved;collisionFlags=savedCollision;
            return delta;
        };
        const bool wasNormal=gravity_control::enabled;
        gravity_control::setEnabled(true,false);
        const float npcNormal=measure(npc),cjNormal=measure(cj),carNormal=measure(car);
        gravity_control::setEnabled(false,false);
        const float npcReduced=measure(npc),cjReduced=measure(cj),parked=measure(car);
        const V observer=position(cj);
        ProbeScript seat(0x036A);seat.integer(reference(cj));seat.integer(probeCar);seat.run();
        const float driven=at<void*>(car,0x460)==cj?measure(car):0.f;
        if(inVehicle(cj)){
            ProbeScript leave(0x0362);leave.integer(reference(cj));
            leave.number(observer.x);leave.number(observer.y);leave.number(observer.z);leave.run();
        }
        const float afterExit=at<void*>(car,0x460)?0.f:measure(car);
        gravity_control::setEnabled(wasNormal,false);
        if(npcNormal<=0.f||cjNormal<=0.f||carNormal<=0.f)return -1908;
        event("probe_reduced_gravity",reference(cj),-1,"npc="+std::to_string(npcReduced/npcNormal)+
            " cj="+std::to_string(cjReduced/cjNormal)+" parked="+std::to_string(parked/carNormal)+
            " driven="+std::to_string(driven/carNormal)+" after_exit="+std::to_string(afterExit/carNormal));
        return std::abs(npcReduced/npcNormal-.01f)<.002f&&std::abs(cjReduced/cjNormal-.5f)<.002f&&
            std::abs(parked/carNormal-.01f)<.002f&&std::abs(driven/carNormal-.5f)<.002f&&
            std::abs(afterExit/carNormal-.01f)<.002f;
    }
    if(mode==1907){
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(355,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(cj,30,120,false);
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(cj,30);
        return combatWeapon(cj);
    }
    if(mode==1900){
        if(!player_cover::owns(cj))return -1900;
        const auto& c=player_cover::cover;
        return 1|(c.aiming?2:0)|(c.corner?4:0)|(c.peeking?8:0)|
            (player_cover::holdsFire(cj)?16:0);
    }
    if(mode==1901||mode==1902){
        if(!player_cover::owns(cj))return -1901;
        auto& c=player_cover::cover;
        const V along=player_cover::tangent(c.wall.normal)*float(mode==1901?1:-1);
        V root=position(cj);
        for(int n=0;n<25&&player_cover::sameWall(root+along*.15f,c.wall);++n)root=root+along*.06f;
        teleport(cj,root);c.destination=root;c.side=mode==1901?1:-1;c.slideSpeed=0;
        c.peeking=false;c.turn={};
        return 1;
    }
    if(mode==1903||mode==1906){
        auto* npc=pedFromRef(firearmTarget);if(!npc)return -1903;
        for(int model:{346,355})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        for(int weapon:{22,30})reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(npc,weapon,40,false);
        if(mode==1906){
            at<float>(npc,0x540)=1.f;
            unsigned pistol[7]={22,0,8,40,0,0,0};
            ColPoint hit{};hit.point=position(npc)+V{0,0,.3f};hit.surfaces[4]=3;
            const V from=position(cj)+V{0,0,.3f};hit.normal=normalized(from-hit.point);
            reinterpret_cast<void(__thiscall*)(void*,void*,void*,const V&,const V&,const ColPoint&,int)>(0x73B550)
                (pistol,cj,npc,from,hit.point,hit,0);
            return reference(npc);
        }
        const int count=int(dropped_weapons::drops.size());
        reinterpret_cast<void(__thiscall*)(void*)>(0x4591D0)(npc);
        return int(dropped_weapons::drops.size())-count;
    }
    if(mode==1904){
        int valid=0;
        for(const auto& drop:dropped_weapons::drops){
            auto* p=dropped_weapons::pickup(drop.handle);
            auto* obj=p?at<void*>(p,4):nullptr;
            if(obj&&matrix(obj)&&drop.fall>=1.f&&length(position(obj)-drop.landing)<.02f)++valid;
        }
        return valid;
    }
    if(mode==425||mode==426||mode==429||mode==430||mode==443||mode==1905||mode==1911){
        player_cover::leave("fixture_reset");
        farCover=mode==429;
        static std::vector<int> boxes;
        for(int box:boxes){ProbeScript remove(0x0108);remove.integer(box);remove.run();}
        boxes.clear();
        const V origin=position(cj);
        float floor;
        if(!player_cover::ground(origin,floor))return -425;
        const float rootHeight=origin.z-floor;
        if(mode==426||mode==430||mode==443||mode==1905||mode==1911){
            const int model=1224;
            reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
            reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
            const V boxSpot{2420.f,-1664.f,origin.z+3.f};
            float base;
            if(!player_cover::ground({boxSpot.x,boxSpot.y,origin.z+.5f},base))return -426;
            const int span=mode==443?2:0;
            for(int n=-span;n<=span;++n){
                ProbeScript create(0x029B);create.integer(model);create.number(boxSpot.x+float(n)*.84f);
                create.number(boxSpot.y);create.number(base+.48f);create.result();
                const int box=create.run();
                if(box<0)return -426;
                boxes.push_back(box);
                ProbeScript immobile(0x0392);immobile.integer(box);immobile.integer(0);immobile.run();
                if(auto* object=reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(box))
                    at<unsigned>(object,0x40)=(at<unsigned>(object,0x40)|0x30u)&~2u;
            }
            if(mode==1905||mode==1911){
                ProbeScript create(0x029B);create.integer(model);create.number(boxSpot.x);
                create.number(boxSpot.y);create.number(base+1.4f);create.result();
                const int box=create.run();if(box<0)return -1905;
                boxes.push_back(box);
                ProbeScript immobile(0x0392);immobile.integer(box);immobile.integer(0);immobile.run();
                if(auto* object=reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(box))
                    at<unsigned>(object,0x40)=(at<unsigned>(object,0x40)|0x30u)&~2u;
            }
            const V root{boxSpot.x,boxSpot.y-1.35f,base+rootHeight};
            teleport(cj,root);
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(cj,0.f);
            at<float>(cj,0x558)=at<float>(cj,0x55C)=0.f;
            if(mode==430||mode==1911){
                for(int asset:{280,346})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(asset,2);
                reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
                void* officer=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(6,280,root,true);
                if(!officer)return -430;
                coverCop=reference(officer);
                at<unsigned char>(officer,0x484)=1;
                reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(officer,22,100,false);
                reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(officer,22);
                const V enemy{boxSpot.x,boxSpot.y+11.f,base+rootHeight};
                teleport(cj,enemy);
                auto* task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x38);
                reinterpret_cast<void(__thiscall*)(void*,void*,int,int,int,int,int)>(0x620E30)(task,cj,-1,0,0,0,1);
                reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(officer),task,3,true);
                return coverCop;
            }
            return 1;
        }
        for(float heading:{0.f,1.5707963f,3.14159265f,-1.5707963f,.78539816f,-.78539816f,2.3561945f,-2.3561945f}){
            const V direction{-std::sin(heading),std::cos(heading),0};
            player_cover::Wall wall;
            if(!player_cover::wallRay({origin.x,origin.y,floor+.6f},direction,26.f,wall)||wall.vehicle>=0)continue;
            const V out=wall.point+wall.normal*.9f;
            ColPoint top{};void* obstacle=nullptr;
            if(!player_cover::ray(out+V{0,0,.8f},out+V{0,0,.8f}-wall.normal*1.2f,top,obstacle))continue;
            float base;
            if(!player_cover::ground({out.x,out.y,origin.z+.5f},base))continue;
            V start=out+wall.normal*(farCover?2.f:0.f);
            if(!player_cover::ground({start.x,start.y,origin.z+.5f},base))continue;
            teleport(cj,{start.x,start.y,base+rootHeight});
            const V facing=wall.normal*-1.f;
            const float yaw=std::atan2(-facing.x,facing.y);
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(cj,yaw);
            at<float>(cj,0x558)=at<float>(cj,0x55C)=yaw;
            return 1;
        }
        return -425;
    }
    if(mode==427){
        player_cover::tick(false);
        const int gait=at<int>(cj,0x534);
        if(farCover&&!player_cover::owns(cj))at<int>(cj,0x534)=6;
        player_cover::tick(true);
        at<int>(cj,0x534)=gait;
        return player_cover::owns(cj)?1:0;
    }
    if(mode==428){
        if(!player_cover::owns(cj))return 0;
        const auto& cover=player_cover::cover;
        return 1|(cover.wall.low?2:0)|(cover.approaching?0:4)|
            (player_cover::draw(cj)?8:0)|(player_cover::duckTask(cj)?16:0);
    }
    if(mode==431){
        unsigned pistol[7]={22,0,8,40,0,0,0};
        V from=position(cj)+V{0,0,.6f},muzzle=from,end=from+V{3.f,0,2.f};
        reinterpret_cast<bool(__thiscall*)(void*,void*,V*,V*,void*,V*,V*)>(0x742300)
            (pistol,cj,&from,&muzzle,nullptr,&end,nullptr);
        police_cover::tick();
        return police_cover::gunfire.contains(reference(cj))?1:0;
    }
    if(mode==432){
        const auto found=police_cover::officers.find(coverCop);
        return found==police_cover::officers.end()?0:1+int(found->second.phase);
    }
    if(mode==433){
        if(!player_cover::owns(cj))return 0;
        const auto saved=player_cover::cover;
        if(combatWeapon(cj)<22)return -433;
        player_cover::cover.blind=true;
        player_cover::cover.blindStarted=gameTime()-unsigned(400.f*player_cover::clipMeta(player_cover::poseReader,cj,player_cover::blindKey(combatWeapon(cj))).duration);
        player_cover::cover.blindReleased=0;
        const bool pose=player_cover::draw(cj);
        V muzzle{},endPoint{};
        const bool origin=player_cover::blindShot(cj,muzzle,endPoint,35.f)&&std::abs(length(endPoint-muzzle)-35.f)<.02f;
        const unsigned slot=at<unsigned char>(cj,0x718);
        auto* gun=static_cast<unsigned char*>(cj)+0x5A0+slot*0x1C;
        bool fired=false;
        if(origin&&slot<13){
            V start=position(cj)+V{0,0,.6f},end=start+V{3.f,0,2.f};
            const unsigned before=at<unsigned>(gun,0xC);
            fired=reinterpret_cast<bool(__thiscall*)(void*,void*,V*,V*,void*,V*,V*)>(0x742300)
                (gun,cj,&start,&start,nullptr,&end,nullptr)&&at<unsigned>(gun,0xC)<before;
        }
        player_cover::cover=saved;
        return (pose?1:0)|(origin?2:0)|(fired?4:0);
    }
    if(mode==434){
        auto* cop=pedFromRef(coverCop);
        if(!cop)return 0;
        player_cover::Wall wall;
        V hide,fire;int side=0;
        return 1|(police_cover::available(cop)?2:0)|(police_cover::target(cop)==cj?4:0)|
            (police_cover::find(cop,cj,wall,hide,fire,side)?8:0);
    }
    if(mode==435){
        auto* cop=pedFromRef(coverCop);
        const unsigned slot=cop?at<unsigned char>(cop,0x718):255;
        return slot<13?int(at<unsigned>(cop,0x5A0+slot*0x1C+0xC)):-435;
    }
    if(mode==436){
        if(!player_cover::owns(cj))return 0;
        const auto saved=player_cover::cover;
        const auto now=gameTime();
        player_cover::updateAim(true,now);
        const bool exposed=player_cover::cover.exposed==now&&player_cover::draw(cj);
        player_cover::updateAim(false,now);
        const bool returned=player_cover::cover.returning==now&&player_cover::draw(cj);
        player_cover::cover=saved;
        return (exposed?1:0)|(returned?2:0);
    }
    if(mode==437){
        if(!player_cover::owns(cj))return 0;
        const auto& cover=player_cover::cover;
        return (cover.approaching?1:0)|(player_cover::draw(cj)?2:0)|
            (cover.entry.seconds>.30f?4:0);
    }
    if(mode==438){
        auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
        return player_cover::aimInput(pad)?1:0;
    }
    if((mode>=439&&mode<=442)||mode==444){
        if(!player_cover::owns(cj))return -439;
        const auto& cover=player_cover::cover;
        const V along=player_cover::tangent(cover.wall.normal);
        if(mode==439)return int(std::round(dot(position(cj)-cover.wall.point,along)*100.f));
        if(mode==444)return int(std::round(dot(position(cj)-cover.wall.point,cover.wall.normal)*1000.f));
        if(mode==440){
            const V view=player_cover::flat(at<V>(activeCamera(),0x190));
            const int side=player_cover::sameWall(position(cj)+along*(.8f*cover.side),cover.wall)?cover.side:-cover.side;
            const V wanted=along*float(side);
            const float sx=dot(wanted,V{view.y,-view.x,0}),sy=-dot(wanted,view);
            return std::abs(sx)>std::abs(sy)?(sx>0?1:-1):(sy>0?2:-2);
        }
        if(mode==441)return (std::abs(cover.slideSpeed)>.15f?1:0)|(cover.turn.started?2:0)|
            (player_cover::draw(cj)?4:0)|(cover.blind?8:0)|(cover.blindShotAt?16:0);
        const unsigned slot=at<unsigned char>(cj,0x718);
        return slot<13?int(at<unsigned>(cj,0x5A0+slot*0x1C+0xC)):-442;
    }
    if(mode>=400&&mode<=403){
        const int types[]={4,6,8,5}, models[]={7,280,105,9};
        if(auto*old=pedFromRef(firearmTarget)){
            ProbeScript remove(0x009B);remove.integer(firearmTarget);remove.run();
        }
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(models[mode-400],2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        const V spot=position(cj)+V{0,3.f,0};
        auto* target=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(
            types[mode-400],models[mode-400],spot,true);
        if(!target)return -400;
        firearmTarget=reference(target);
        event("probe_firearm_npc",firearmTarget,at<short>(target,0x22),"type="+std::to_string(at<int>(target,0x598)));
        void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x20);
        reinterpret_cast<void(__thiscall*)(void*,int,bool,bool,float)>(0x62F310)(task,30000,false,false,8.f);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(target),task,3,true);
        watchSessionPed(target);
        return firearmTarget;
    }
    if((mode>=404&&mode<=407)||mode==415){
        auto* target=pedFromRef(firearmTarget);if(!target)return -404;
        const int ids[]={22,30,25,28};
        const int type=mode==415?25:ids[mode-404];
        void* shooter=mode==415?pedFromRef(firearmShooter):cj;
        if(!shooter)return -415;
        unsigned weapon[7]={unsigned(type),0,8,40,0,0,0};
        V end=position(target)+V{0,0,.25f};
        V from=end+V{0,-1.5f,.04f}, muzzle=from+normalized(end-from)*.12f;
        if(type==25){
            at<float>(target,0x540)=420.f;
            auto*& ignore=*reinterpret_cast<void**>(0xB7CD68);
            void* prior=ignore;ignore=shooter;
            bool aligned=false;
            auto*h=hierarchy(target);
            for(int bone:{saBone::chest,saBone::pelvis,saBone::head}){
                if(!h||!h->nodes||!h->matrices)break;
                V centre{};bool found=false;
                for(int i=0;i<h->num;++i)if(h->nodes[i].id==bone){centre=h->matrices[i].pos;found=true;break;}
                if(!found)continue;
                for(V side:{V{0,-1.5f,0},V{1.5f,0,0},V{-1.5f,0,0},V{0,1.5f,0}}){
                    firearm::CollisionRay ray{centre-side*.3f,{},nullptr};
                    firearm::tracePellets(centre+side,&ray,1);
                    if(ray.victim!=target)continue;
                    end=centre;from=centre+side;muzzle=from+normalized(end-from)*.12f;aligned=true;break;
                }
                if(aligned)break;
            }
            ignore=prior;
            if(!aligned)return -417;
        }
        const auto fire=reinterpret_cast<bool(__thiscall*)(void*,void*,V*,V*,void*,V*,V*)>(0x742300);
        const unsigned now=gameTime();
        const bool fired=fire(weapon,shooter,&from,&muzzle,target,&end,nullptr);
        const int interval=int(weapon[4]-now);
        const bool second=fire(weapon,shooter,&from,&muzzle,target,&end,nullptr);
        event("probe_native_weapon_cadence",firearmTarget,at<short>(target,0x22),
            "weapon="+std::to_string(type)+" interval="+std::to_string(interval)+" first="+std::to_string(fired)+" second="+std::to_string(second));
        return fired&&!second&&interval==int(firearm::profile(type).interval)?interval:-404;
    }
    if(mode==408){
        auto* target=pedFromRef(firearmTarget);if(!target)return -408;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(346,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        reinterpret_cast<void(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(target,22,17,false);
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(target,22);
        return combatWeapon(target);
    }
    if(mode==409||mode==411||mode==412){
        auto* target=pedFromRef(firearmTarget);if(!target)return -409;
        const auto hand=dropped_weapons::rightHand(target);if(!hand)return -410;
        unsigned weapon[7]={22,0,8,40,0,0,0};
        ColPoint contact{};
        contact.point=mode==409?*hand:position(target)+V{0,0,.25f};
        const V from=position(cj)+V{0,0,.4f}, end=contact.point;
        contact.normal=normalized(from-end);contact.surfaces[4]=mode==409?6:3;
        const auto hit=reinterpret_cast<void(__thiscall*)(void*,void*,void*,const V&,const V&,const ColPoint&,int)>(0x73B550);
        if(mode==412)at<float>(target,0x540)=1.f;
        const auto createdBy=at<unsigned char>(target,0x484);
        if(mode==409)at<unsigned char>(target,0x484)=1;
        hit(weapon,cj,target,from,end,contact,0);
        if(mode==409)at<unsigned char>(target,0x484)=createdBy;
        return firearmTarget;
    }
    if(mode==410){
        if(dropped_weapons::drops.empty())return 0;
        const auto& drop=dropped_weapons::drops.back();
        auto* pickup=dropped_weapons::pickup(drop.handle);
        return (pickup?1:0)|(drop.fall>=1.f?2:0)|(pickup&&at<void*>(pickup,4)?4:0);
    }
    if(mode==413){
        const V origin=position(cj);
        for(int n=0;n<2;++n){
            const V point=origin+V{n?1.55f:-1.55f,3.f,0};
            auto* p=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,point,true);
            if(!p)return -413;
            cameraTargets[n]=reference(p);
        }
        return 1;
    }
    if(mode==414){
        const auto left=pedFromRef(cameraTargets[0]),right=pedFromRef(cameraTargets[1]);
        if(!left||!right)return -414;
        const V origin=position(cj),leftView=combatFlat(position(left)-origin),rightView=combatFlat(position(right)-origin);
        const V savedView=combatLockView;const unsigned savedTime=combatLockChanged;
        combatLockChanged=gameTime()-250;
        combatLockView=leftView;
        const bool stable=combatCameraTarget(cj,left,leftView)==left;
        const auto rightByView=combatCameraTarget(cj,left,rightView);
        const bool turnRight=rightByView!=left&&cross(leftView,combatFlat(position(rightByView)-origin)).z<0.f;
        combatLockView=rightView;
        const auto leftByView=combatCameraTarget(cj,right,leftView);
        const bool turnLeft=leftByView!=right&&cross(rightView,combatFlat(position(leftByView)-origin)).z>0.f;
        const auto nextLeft=combatCameraTarget(cj,right,rightView,1);
        const auto nextRight=combatCameraTarget(cj,left,leftView,-1);
        const bool stickLeft=nextLeft!=right&&cross(rightView,combatFlat(position(nextLeft)-origin)).z>0.f;
        const bool stickRight=nextRight!=left&&cross(leftView,combatFlat(position(nextRight)-origin)).z<0.f;
        combatLockView=savedView;combatLockChanged=savedTime;
        return (stable?1:0)|(turnRight?2:0)|(turnLeft?4:0)|(stickLeft?8:0)|(stickRight?16:0);
    }
    if(mode==416){
        auto* target=pedFromRef(firearmTarget);if(!target)return -416;
        const V spot=position(target)+V{0,-1.6f,0};
        auto* shooter=reinterpret_cast<void*(__cdecl*)(int,unsigned,const V&,bool)>(0x612710)(4,7,spot,true);
        if(!shooter)return -416;
        firearmShooter=reference(shooter);
        return firearmShooter;
    }
    if(mode>=417&&mode<=424){
        auto* npc=pedFromRef(firearmTarget);if(!npc)return -417;
        const bool cjVictim=mode==419||mode==420||mode==422||mode==423||mode==424;
        const bool melee=mode==421||mode==422;
        void* victim=cjVictim?cj:npc;
        void* source=cjVictim?npc:cj;
        auto& hp=at<float>(victim,0x540);
        auto& armour=at<float>(victim,0x548);
        const float savedHealth=hp,savedArmour=armour;
        const bool oldGod=*reinterpret_cast<bool*>(0x96916D);
        *reinterpret_cast<bool*>(0x96916D)=mode==423;
        const float before=mode==418?55.f:mode==424?8.f:100.f;
        hp=before;armour=mode==417||mode==420||melee?20.f:0.f;
        if(melee){
            authoredMeleeDamage={victim,source,0,3,10.f};combatDelivering=true;
            reinterpret_cast<bool(__cdecl*)(void*,void*,int,int,int,unsigned char)>(0x73A530)(victim,source,0,10,3,0);
            combatDelivering=false;authoredMeleeDamage={};
        }else{
            unsigned weapon[7]={22,0,8,40,0,0,0};
            ColPoint point{};point.point=position(victim)+V{0,0,.3f};point.surfaces[4]=3;
            const V from=position(source)+V{0,0,.3f};
            point.normal=normalized(from-point.point);
            reinterpret_cast<void(__thiscall*)(void*,void*,void*,const V&,const V&,const ColPoint&,int)>(0x73B550)
                (weapon,source,victim,from,point.point,point,0);
        }
        const float expectedHealth[]={97.5f,42.5f,91.75f,100.f,95.f,90.f,100.f,0.f};
        const float expectedArmour[]={0.f,0.f,0.f,11.75f,20.f,20.f,0.f,0.f};
        const int n=mode-417;
        const bool correct=std::abs(hp-expectedHealth[n])<.02f&&std::abs(armour-expectedArmour[n])<.02f;
        const bool threshold=mode!=418||ivHealth(victim)<combat_health::injured;
        event("probe_iv_combat_health",reference(victim),at<short>(victim,0x22),"mode="+std::to_string(mode)+
            " health_sa="+std::to_string(hp)+" health_iv="+std::to_string(ivHealth(victim))+" armour="+std::to_string(armour));
        *reinterpret_cast<bool*>(0x96916D)=oldGod;
        if(mode!=424){hp=savedHealth;armour=savedArmour;}
        return correct&&threshold?1:0;
    }
    if(mode==390){
        if(!gravity_control::engineGravity)return -391;
        gravity_control::setEnabled(true,false);
        gravity_control::sample(false,999);
        gravity_control::sample(true,1000);
        gravity_control::sample(true,1599);
        const bool early=gravity_control::enabled;
        gravity_control::sample(true,1600);
        gravity_control::sample(true,2900);
        const bool heldReduced=!gravity_control::enabled;
        const bool nativeConstantPreserved=*reinterpret_cast<const float*>(gravity_control::gravityConstant)==
            gravity_control::normalGravity;
        gravity_control::sample(false,2901);
        gravity_control::sample(true,3000);
        gravity_control::sample(true,3600);
        gravity_control::sample(false,3601);
        event("probe_gravity_toggle",reference(cj),0,"held_once="+std::to_string(heldReduced)+
            " native_constant_preserved="+std::to_string(nativeConstantPreserved));
        return early&&heldReduced&&nativeConstantPreserved&&gravity_control::enabled;
    }
    if(mode==391){
        *reinterpret_cast<bool*>(0x96916D)=true;
        // Grinch Trainer 1.0 writes the native cheat byte and these proofs.
        at<unsigned>(cj,0x40)|=0x00FC0000u;
        at<float>(cj,0x540)=77.f;at<float>(cj,0x548)=33.f;
        return expansionFixture(85);
    }
    if(mode==392){
        const auto it=agents.find(reference(cj));
        const bool preserved=at<float>(cj,0x540)==77.f&&at<float>(cj,0x548)==33.f;
        return preserved&&it!=agents.end()&&it->second->kind==Reaction::Explosion;
    }
    if(mode==393){
        headshotTarget=expansionFixture(80);
        return headshotTarget;
    }
    if(mode==398){
        headshotTarget=expansionFixture(80);
        auto* target=pedFromRef(headshotTarget);
        if(!target)return -398;
        const V look=position(cj)+V{0,2.2f,0};
        teleport(target,look);
        const V camera=position(cj)+V{0,-3.f,1.3f},up{};
        auto* nativeCamera=reinterpret_cast<void*>(0xB6F028);
        reinterpret_cast<void(__thiscall*)(void*,const V*,const V*)>(0x50BEC0)(nativeCamera,&camera,&up);
        reinterpret_cast<void(__thiscall*)(void*,const V*,short,int)>(0x50C8B0)(nativeCamera,&look,2,1);
        return headshotTarget;
    }
    if(mode==394){
        auto* target=pedFromRef(headshotTarget);auto* data=at<void*>(cj,0x480);
        if(!target||!data)return -394;
        const auto flags=at<unsigned>(data,0x34);
        at<unsigned>(data,0x34)=flags|4u; // Native free aim grants a critical AK headshot.
        reinterpret_cast<void(__cdecl*)(void*,void*,int,int,int,int)>(0x73A530)(target,cj,30,30,9,0);
        at<unsigned>(data,0x34)=flags;
        return headshotTarget;
    }
    if(mode==395){
        auto* target=pedFromRef(headshotTarget);
        if(!target)return -395;
        const auto it=agents.find(headshotTarget);
        return (at<float>(target,0x540)==0.f?1:0)|
            ((at<unsigned>(target,0x46C)&0x8000u)?2:0)|
            ((it!=agents.end()&&it->second->kind==Reaction::Shot)?4:0);
    }
    if(mode==396){
        return int(combatMarkerDrawn);
    }
    if(mode==397){
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(346,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        ProbeScript give(0x01B2);give.integer(reference(cj));give.integer(22);give.integer(90);give.run();
        ProbeScript equip(0x01B9);equip.integer(reference(cj));equip.integer(22);equip.run();
        return combatWeapon(cj);
    }
    return -399;
}
