#pragma once
struct MeleeAttack {
    int anim=-1,move=-1,group=-1;float time=0,contactTime=0;
    void*target=nullptr;std::string profile="native";unsigned allowed=0;
};
static MeleeAttack meleeAttack(void*ped){
    MeleeAttack out;if(!ped)return out;
    if(void*m=taskManager(ped))if(void*t=at<void*>(m,0x14);t&&at<unsigned>(t,0)==0x86D684){
        out.move=at<signed char>(t,0x25);out.target=at<void*>(t,0x18);
        if(void*a=at<void*>(t,0x1C)){out.anim=at<short>(a,0x2C);out.group=at<unsigned short>(a,0xE);out.time=at<float>(a,0x20);}
    }
    const std::string key=std::to_string(out.group)+"."+std::to_string(out.anim);
    static std::map<std::string,std::pair<std::string,float>> profiles;
    auto profile=profiles.find(key);
    if(profile==profiles.end()){
        char name[40]{},time[40]{};const auto ini=(folder.parent_path()/"DeliriumSA.ini").string();
        GetPrivateProfileStringA("MeleeContactProfiles",key.c_str(),"native",name,sizeof(name),ini.c_str());
        GetPrivateProfileStringA("MeleeContactProfiles",(key+".ContactTime").c_str(),"0",time,sizeof(time),ini.c_str());
        profile=profiles.emplace(key,std::make_pair(std::string(name),float(std::atof(time)))).first;
    }
    out.profile=profile->second.first;out.contactTime=profile->second.second;out.allowed=strikerMask(out.profile);return out;
}
