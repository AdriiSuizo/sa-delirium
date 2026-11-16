#pragma once

namespace ragdoll_fire {
struct State {
    unsigned agentStart=0;
    unsigned lastShotAt=0;
    unsigned lastManualShot=0;
    unsigned blindUntil=0;
    unsigned blindNext=0;
    unsigned pointStartAt=0;
    unsigned pointNextAt=0;
    int pointTargetRef=-1;
    int pointBone=3;
    float pointTwist=0.f;
    float pointStraightness=.85f;
    bool prevFire=false;
    bool pointing=false;
};
static std::map<int,State> states;

static float randRange(float lo,float hi){
    return reinterpret_cast<float(__cdecl*)(float,float)>(0x41BD90)(lo,hi);
}

static void stopPointArm(NmAgent* agent,State& st){
    st.pointTargetRef=-1;
    if(!agent||!st.pointing)return;
    ok(nm_msg_begin(agent,"pointArm"));
    ok(nm_msg_set_bool(agent,"start",false));
    sendMessage(agent);
    st.pointing=false;
}

static void resetState(NmAgent* agent,State& st){
    stopPointArm(agent,st);
    st.blindUntil=0;
    st.blindNext=0;
    st.pointStartAt=0;
    st.pointNextAt=0;
    st.prevFire=false;
}

static bool rightHandPose(Active& a,V fireOffset,V& muzzle,V& dir){
    auto* ped=a.ped;
    auto* h=hierarchy(ped);
    auto* rig=a.playerRig?a.playerRig.get():a.rig;
    if(!h||!h->matrices||!rig)return false;
    const int hand=rig->index(24);
    if(hand<0||hand>=h->num)return false;
    const auto& m=h->matrices[hand];
    if(!finite(m.pos)||!finite(m.right)||length(m.right)<.1f)return false;
    if(length(m.pos-a.current[0].p)>2.5f)return false;
    muzzle=point(m,fireOffset);
    dir=normalized(m.right);
    if(!finite(muzzle)||!finite(dir)||length(dir)<.5f)return false;
    if(length(muzzle-m.pos)>1.5f)return false;
    return true;
}

static void* findNpcThreat(Active& a){
    if(a.kind==Reaction::Shot){
        void* attacker=a.shotAttacker>=0?pedFromRef(a.shotAttacker):at<void*>(a.ped,0x764);
        if(attacker&&reference(attacker)>=0&&attacker!=a.ped&&matrix(attacker)&&at<float>(attacker,0x540)>0.f)
            return attacker;
    }else if(a.kind==Reaction::Brace){
        if(void* veh=vehicleFromRef(a.vehicle))
            if(void* driver=at<void*>(veh,0x460);driver&&reference(driver)>=0&&driver!=a.ped&&matrix(driver)&&at<float>(driver,0x540)>0.f)
                return driver;
    }
    return nullptr;
}

static V targetBonePosition(void* ped,int boneId){
    if(auto* h=hierarchy(ped);h&&h->nodes&&h->matrices&&h->num>0&&h->num<=256){
        for(int n=0;n<h->num;++n)
            if(h->nodes[n].id==boneId&&finite(h->matrices[n].pos))return h->matrices[n].pos;
    }
    return position(ped)+V{0,0,.35f};
}

static void applyRecoil(Active& a,int weaponId,V dir){
    if(!a.agent||!a.ped)return;
    const V root=position(a.ped);
    if(!finite(a.current[20].p)||!finite(a.current[19].p)||!finite(a.current[9].p))return;
    if(length(a.current[20].p-root)>2.5f)return;
    float back=9.f,up=2.2f;
    if(weaponId>=25&&weaponId<=27){back=26.f;up=7.f;}
    else if(weaponId==24||weaponId==33||weaponId==34){back=17.f;up=4.5f;}
    else if(weaponId==30||weaponId==31){back=12.f;up=3.f;}
    const V total=dir*(-back)+V{0,0,up};
    const V handImp=total*.44f,foreImp=total*.28f,chestImp=total*.28f;
    ok(nm_agent_apply_impulse(a.agent,20,&a.current[20].p.x,&handImp.x));
    ok(nm_agent_apply_impulse(a.agent,19,&a.current[19].p.x,&foreImp.x));
    ok(nm_agent_apply_impulse(a.agent,9,&a.current[9].p.x,&chestImp.x));
}

static bool shootWeapon(Active& a,void* weaponPtr,int weaponId,V muzzle,V dir,float range,bool isPlayer){
    auto* ped=a.ped;
    V end=muzzle+dir*std::clamp(range,8.f,150.f);
    at<int>(weaponPtr,4)=0;
    at<unsigned>(weaponPtr,0x10)=0;
    firearm::ragdollFiring=true;
    const bool fired=firearm::fire(weaponPtr,nullptr,ped,&muzzle,&muzzle,nullptr,&end,nullptr);
    firearm::ragdollFiring=false;
    if(fired){
        applyRecoil(a,weaponId,dir);
        event("ragdoll_weapon_fired",a.ref,weaponId,std::string("player=")+(isPlayer?"1":"0"));
    }
    return fired;
}

static void tick(unsigned now){
    for(auto it=states.begin();it!=states.end();){
        auto found=agents.find(it->first);
        if(found==agents.end()||!found->second->ped||pedFromRef(it->first)!=found->second->ped)it=states.erase(it);
        else ++it;
    }
    if(!ready||!playing()||cutscene()||(protectMissions&&scriptMissionActive()))return;
    for(auto& [ref,a]:agents){
        auto* ped=a->ped;
        if(!ped||!a->agent||a->corpseResting||!matrix(ped)||inVehicle(ped))continue;
        auto& st=states[ref];
        if(st.agentStart!=a->start){
            resetState(a->agent,st);
            st.agentStart=a->start;
            st.lastShotAt=0;
        }
        const bool isPlayer=(ped==player());
        if(at<float>(ped,0x540)<=0.f||a->recovering||pedWasDisarmed(ped)){
            resetState(a->agent,st);
            continue;
        }
        if(!isPlayer&&a->kind!=Reaction::Shot&&a->kind!=Reaction::Brace){
            resetState(a->agent,st);
            continue;
        }
        if(!isPlayer&&a->preview&&a->kind!=Reaction::Brace){
            resetState(a->agent,st);
            continue;
        }
        const unsigned slot=at<unsigned char>(ped,0x718);
        if(slot>=13){resetState(a->agent,st);continue;}
        void* weaponPtr=reinterpret_cast<char*>(ped)+0x5A0+slot*0x1C;
        const int id=at<int>(weaponPtr,0);
        if(id<22||id>34){resetState(a->agent,st);continue;}
        if(at<unsigned>(weaponPtr,8)==0){resetState(a->agent,st);continue;}
        const int skill=reinterpret_cast<int(__thiscall*)(void*)>(0x5E6580)(ped);
        auto* info=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(id,skill);
        if(!info)continue;
        const bool continuous=(at<unsigned>(info,0x18)&0x400u)!=0;
        const auto& spec=firearm::profile(id);
        const unsigned cooldown=spec.interval>0u?spec.interval:(continuous?110u:(id>=25&&id<=27?700u:240u));
        V muzzle{},dir{};
        if(!rightHandPose(*a,at<V>(info,0x24),muzzle,dir))continue;
        if(isPlayer){
            auto* pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
            const bool fireDown=pad&&reinterpret_cast<int(__thiscall*)(void*,void*)>(0x540180)(pad,nullptr)!=0;
            const bool trigger=continuous?fireDown:(fireDown&&!st.prevFire);
            st.prevFire=fireDown;
            if(trigger&&(st.lastManualShot==0||now-st.lastManualShot>=cooldown)){
                if(shootWeapon(*a,weaponPtr,id,muzzle,dir,at<float>(info,8),true))
                    st.lastManualShot=now;
            }
            continue;
        }
        // IV PointGun::ready (0x8260F350) and CTaskSimpleNMShot::Control (0x826197E8):
        // require healthy/alive threshold (>= 100 in IV, = 50 HP in SA via ivHealth).
        if(ivHealth(ped)<combat_health::injured){
            resetState(a->agent,st);
            continue;
        }
        // IV CTaskSimpleNMShot Start/Control (0x82618A18 / 0x826197E8):
        // On a new gun-hit task/feed, roll shotBlindFireProbability (0.1) via (rand & 100) <= 10
        // and fire along the weapon barrel for rand(shotMaxBlindFireTimeL..H = 400..1000) ms.
        if(a->kind==Reaction::Shot&&a->lastShotAt>0&&st.lastShotAt!=a->lastShotAt){
            st.lastShotAt=a->lastShotAt;
            st.blindUntil=0;st.blindNext=0;
            const unsigned rollBits=unsigned(std::clamp(randRange(0.f,32767.f),0.f,32767.f));
            if((rollBits&100u)<=10u){
                st.blindUntil=now+unsigned(std::clamp(randRange(400.f,1000.f),400.f,1000.f));
                st.blindNext=now;
            }
        }
        if(a->kind==Reaction::Shot&&st.blindUntil>0){
            if(now>=st.blindUntil){
                st.blindUntil=0;st.blindNext=0;
            }else if(now>=st.blindNext){
                if(shootWeapon(*a,weaponPtr,id,muzzle,dir,at<float>(info,8),false))
                    st.blindNext=now+cooldown;
            }
        }
        // IV CTaskSimpleNMShot (0x826188F8) / CTaskSimpleNMBrace (0x826104C4):
        // PointGun only while balancing (not fallen on the floor) against the shot attacker or car driver.
        const bool fallen=(nm_agent_has_fallen(a->agent)==1)||(a->current[10].p.z-a->current[0].p.z<.25f);
        void* threat=fallen?nullptr:findNpcThreat(*a);
        if(!threat){
            stopPointArm(a->agent,st);
            continue;
        }
        const int threatRef=reference(threat);
        const V checkPos=position(threat)+V{0,0,.35f};
        const float dist=length(checkPos-muzzle);
        if(dist>40.f||dist<.5f){
            stopPointArm(a->agent,st);
            continue;
        }
        if(st.pointTargetRef!=threatRef){
            st.pointTargetRef=threatRef;
            st.pointing=false;
            static constexpr int pointBones[4]={saBone::pelvis,2,saBone::chest,saBone::head};
            st.pointBone=pointBones[std::clamp(int(randRange(0.f,3.99f)),0,3)];
            const float startMax=a->kind==Reaction::Brace?800.f:500.f;
            const float nextMin=a->kind==Reaction::Brace?200.f:100.f;
            const float nextMax=a->kind==Reaction::Brace?700.f:500.f;
            st.pointStartAt=now+unsigned(std::clamp(randRange(0.f,startMax),0.f,startMax));
            st.pointNextAt=now+unsigned(std::clamp(randRange(nextMin,nextMax),nextMin,nextMax));
            const int pedType=at<int>(ped,0x598);
            const bool gang=(pedType>=7&&pedType<=16);
            st.pointTwist=a->kind==Reaction::Brace?randRange(0.f,.25f)
                :(gang?randRange(-.4f,.2f):randRange(-1.37f,1.67f));
            st.pointStraightness=randRange(.75f,.95f);
        }
        const V targetPos=targetBonePosition(threat,st.pointBone);
        if(now<st.pointStartAt)continue;
        ok(nm_msg_begin(a->agent,"pointArm"));
        ok(nm_msg_set_bool(a->agent,"start",true));
        ok(nm_msg_set_bool(a->agent,"useLeftArm",false));
        ok(nm_msg_set_vec3(a->agent,"target",&targetPos.x));
        ok(nm_msg_set_float(a->agent,"twist",st.pointTwist));
        ok(nm_msg_set_float(a->agent,"pointSwingLimit",1.f));
        ok(nm_msg_set_float(a->agent,"armStiffness",9.5f));
        ok(nm_msg_set_float(a->agent,"armDamping",.85f));
        ok(nm_msg_set_float(a->agent,"armStraightness",st.pointStraightness));
        ok(nm_msg_set_bool(a->agent,"useZeroPoseWhenNotPointing",true));
        sendMessage(a->agent);
        st.pointing=true;
        const V toTarget=normalized(targetPos-muzzle);
        // IV 0x820BED00: braceAimAngleDPThreshold / shot aim angle dot > 0.8.
        if(now>=st.pointNextAt&&dot(dir,toTarget)>.80f){
            if(shootWeapon(*a,weaponPtr,id,muzzle,dir,at<float>(info,8),false)){
                const unsigned extra=a->kind==Reaction::Brace
                    ?unsigned(std::clamp(randRange(200.f,700.f),200.f,700.f))
                    :cooldown+unsigned(std::clamp(randRange(0.f,1000.f),0.f,1000.f));
                st.pointNextAt=now+std::max(cooldown,extra);
            }
        }
    }
}
}
