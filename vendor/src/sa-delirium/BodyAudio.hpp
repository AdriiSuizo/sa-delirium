#pragma once
static bool bodyImpactAudio=true,cjFlightVoice=true;

static void tryCjFlightVoice(Active&a,V velocity,unsigned now){
    if(!cjFlightVoice||a.flightScream||a.ped!=player()||at<float>(a.ped,0x540)<=0||
       (a.flightVoiceAttemptAt&&now-a.flightVoiceAttemptAt<180))return;
    a.flightVoiceAttemptAt=now;
    constexpr unsigned short highFall=358;
    const auto speech=reinterpret_cast<short(__thiscall*)(void*,unsigned short,unsigned,float,bool,bool,bool)>(0x5EFFE0)
        (a.ped,highFall,0,1.f,false,true,false);
    a.flightScream=speech>=0;
    event("cj_flight_voice",a.ref,a.model,"context=358 speech="+std::to_string(speech)+
          " speed_mps="+std::to_string(length(velocity))+" vertical_mps="+std::to_string(velocity.z));
}

static void playBodyAudio(Active&a,V com,V velocity,unsigned now){
    if(a.mounted||a.preview||a.recovering||inVehicle(a.ped))return;
    if(bodyImpactAudio||a.kind==Reaction::HighFall){
        float hit[2]{};
        const bool contact=nm_agent_take_ground_impact(a.agent,hit)==1;
        if(contact)finishPlayerHighFall(a,com,hit);
        static unsigned lastNearbyImpact=0;
        if(bodyImpactAudio&&contact&&hit[0]>=.8f&&hit[1]>=2.5f&&
           (!a.impactSoundAt||now-a.impactSoundAt>=280)&&
           (!lastNearbyImpact||now-lastNearbyImpact>=45)&&length(position(player())-com)<=26.f){
            a.impactSoundAt=lastNearbyImpact=now;
            // Native landing audio refuses an actor still flagged airborne.
            // Use SA's quieter knockdown/crunch mix in that state.
            constexpr int collapse=59,knockdown=119;
            const bool airborne=(at<unsigned char>(a.ped,0x46D)&2)!=0;
            const int sound=airborne?knockdown:collapse;
            const float volume=std::clamp(-17.f+hit[0]*2.f,-15.f,-5.f);
            reinterpret_cast<void(__thiscall*)(void*,int,float,float,void*,unsigned char,int,unsigned)>(0x4E2BB0)
                (static_cast<unsigned char*>(a.ped)+0x138,sound,volume,1.f,a.ped,0,0,0);
            event("body_ground_audio",a.ref,a.model,"event="+std::to_string(sound)+
                  " closing_mps="+std::to_string(hit[1])+" impulse_per_kg="+std::to_string(hit[0]));
        }
    }
    if(!cjFlightVoice||a.flightScream||a.ped!=player()||now-a.start<60||
       (a.impactSoundAt&&now-a.impactSoundAt<200))return;
    const bool ejected=a.kind==Reaction::VehicleExit||a.kind==Reaction::BikeFall||
        a.kind==Reaction::Explosion||a.kind==Reaction::Impact;
    const bool launched=ejected&&com.z-a.initialComZ>.35f&&
        (velocity.z>3.f||length(velocity)>13.f);
    // Native impact speech can occupy CJ's channel during the first attempt.
    // Retry during the same flight while the body still has room to fall.
    const bool stillAirborne=a.flightVoiceAttemptAt&&now-a.start<2200&&
        (com.z-a.initialComZ>.15f||velocity.z<-4.f);
    if(launched||(a.kind==Reaction::HighFall&&!a.highFallLanded&&velocity.z<-6.f)||velocity.z<=-12.f||stillAirborne)tryCjFlightVoice(a,velocity,now);
}
