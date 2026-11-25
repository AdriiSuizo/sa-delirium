#pragma once
static void playIvMeleeImpact(int weapon,int part,V point,int ref){
    static IvSample samples[]={
        {L"bat.wav","bat",0},{L"fist2.wav","fist2",0},{L"fist3.wav","fist3",0},
        {L"fist5.wav","fist5",0},{L"face1.wav","face1",0}
    };
    const auto variety=(gameTime()^unsigned(ref))%3u;
    auto&sample=samples[weapon==5?0:part==12?4:1+variety];
    if(playIvSample(L"melee-audio",sample,point,.62f)==IvPlayback::played)event("iv_melee_audio",ref,-1,sample.label);
}
