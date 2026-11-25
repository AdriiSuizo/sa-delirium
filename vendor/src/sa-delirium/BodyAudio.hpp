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

static int playNativeGroundImpact(Active& a,float volume){
    // Native landing audio refuses an actor still flagged airborne.
    // Use SA's quieter knockdown/crunch mix in that state.
    constexpr int collapse=59,knockdown=119;
    const bool airborne=(at<unsigned char>(a.ped,0x46D)&2)!=0;
    const int sound=airborne?knockdown:collapse;

    reinterpret_cast<void(__thiscall*)(void*,int,float,float,void*,unsigned char,int,unsigned)>(0x4E2BB0)
        (static_cast<unsigned char*>(a.ped)+0x138,sound,volume,1.f,a.ped,0,0,0);
    return sound;
}

#include <random>
static IvSample& pickCjGroundSample(){
    static IvSample samples[]={
        {L"body01.wav","BODY_1"},
        {L"body02.wav","BODY_2"},
        {L"body03.wav","BODY_3"},
        {L"body05.wav","BODY_5"},
        {L"body06.wav","BODY_6"},
        {L"body07.wav","BODY_7"},
        {L"body08.wav","BODY_8"},
        {L"body09.wav","BODY_9"},
        {L"body10.wav","BODY_10"},
        {L"body11.wav","BODY_11"},
        {L"body12.wav","BODY_12"},
        {L"body13.wav","BODY_13"}
    };
    // GTA IV BODYFALL_BODY selects these twelve variants with equal weights.
    static std::minstd_rand rng(gameTime()|1u);
    return samples[std::uniform_int_distribution<unsigned>(0,11)(rng)];
}

static void playBodyAudio(Active&a,V com,V velocity,unsigned now,bool stepRejected){
    if(a.mounted||a.preview||a.recovering||inVehicle(a.ped))return;
    if(bodyImpactAudio||a.kind==Reaction::HighFall){
        float hit[2]{};
        const bool contact=nm_agent_take_ground_impact(a.agent,hit)==1;
        if(contact)finishPlayerHighFall(a,com,hit);
        static unsigned lastNearbyImpact=0;
        if(bodyImpactAudio&&contact&&a.ped==player()){
            if(!stepRejected&&a.groundAudio.accepts(now,hit[0],hit[1])){
                const float strength=CjGroundAudioGate::strength(hit[0],hit[1]);
                auto& sample=pickCjGroundSample();
                const auto playback=playIvSample(L"cj-ground-audio",sample,com,.26f+.67f*strength);
                if(playback!=IvPlayback::inaudible){
                    const int native=playback==IvPlayback::unavailable?
                        playNativeGroundImpact(a,-17.f+12.f*strength):0;
                    a.impactSoundAt=now;
                    event("cj_ground_audio",a.ref,a.model,"sample="+std::string(native?"SA":sample.label)+
                        " contact_number="+std::to_string(a.groundAudio.sequence)+
                        " closing_mps="+std::to_string(hit[1])+" impulse_per_kg="+std::to_string(hit[0])+
                        " strength="+std::to_string(strength));
                }
            }
        }else if(bodyImpactAudio&&contact&&hit[0]>=.8f&&hit[1]>=2.5f&&
           (!a.impactSoundAt||now-a.impactSoundAt>=280)&&
           (!lastNearbyImpact||now-lastNearbyImpact>=45)&&length(position(player())-com)<=26.f){
            a.impactSoundAt=lastNearbyImpact=now;
            const int sound=playNativeGroundImpact(a,std::clamp(-17.f+hit[0]*2.f,-15.f,-5.f));
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
