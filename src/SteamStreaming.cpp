// SPDX-License-Identifier: GPL-3.0-only
// Use the original game's verified stream.ini reader, not a foreign limit ASI.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

bool installSteamStreaming(const std::filesystem::path& root,std::ostream& log){
    using Add=bool(WINAPI*)(const wchar_t*,const unsigned char*,std::size_t,int);
    const auto loader=GetModuleHandleW(L"dinput8.dll");
    const auto add=loader?reinterpret_cast<Add>(GetProcAddress(loader,"AddVirtualFileForOverloadW")):nullptr;
    if(!add)return false;
    const auto ini=root/L"scripts"/L"SAStandalone.ini";
    const int memory=GetPrivateProfileIntW(L"Streaming",L"MemoryMiB",512,ini.c_str());
    const bool ps2=GetPrivateProfileIntW(L"Visuals",L"Ps2PostEffects",1,ini.c_str())==1;
    // The unmodified Steam image has a 2-GiB process address space. Keep room
    // for D3D9/physics/heaps rather than copying a 1-GiB LAA-only lab setting.
    if(memory!=0&&(memory<50||memory>512)){log<<"streaming_budget_outside_supported_range\n";return false;}
    if(!memory&&!ps2)return true;
    std::map<std::string,std::string> values;
    if(memory){values["memory"]=std::to_string(memory*1024);values["devkit_memory"]=values["memory"];}
    if(ps2){
        values["pe_lightchangerate"]="0.0008";values["pe_lightingbasecap"]="0.35";
        values["pe_lightingbasemult"]="0.4";
        for(const char*key:{"pe_leftx","pe_topy","pe_rightx","pe_bottomy"})values[key]="8";
        values["pe_bRadiosity"]="1";
    }
    std::ifstream input(root/L"stream.ini");if(!input)return false;
    std::ostringstream output;std::string line;bool sawMemory=false,sawDevkit=false;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::istringstream tokens(line);std::string key;tokens>>key;
        if(key=="memory")sawMemory=true;if(key=="devkit_memory")sawDevkit=true;
        const auto value=values.find(key);
        if(value==values.end())output<<line<<'\n';
        else{output<<key<<'\t'<<value->second<<'\n';values.erase(value);}
    }
    if(!input.eof()||!sawMemory||!sawDevkit)return false;
    // Radiosity is understood by the native reader but absent in stock data.
    for(const auto&[key,value]:values)output<<key<<'\t'<<value<<'\n';
    static std::string overlay;overlay=output.str();
    const bool registered=add((root/L"stream.ini").c_str(),reinterpret_cast<const unsigned char*>(overlay.data()),overlay.size(),5);
    log<<"streaming_ini_overlay registered="<<registered<<" memory_mib="<<memory
       <<" ps2_posteffects="<<ps2<<" original_unchanged=1\n";
    return registered;
}
