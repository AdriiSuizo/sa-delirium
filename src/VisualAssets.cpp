// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <array>

namespace fs=std::filesystem;
bool installSteamModelStores(const fs::path&root,std::ostream&log);
bool installSteamVisualPools(const fs::path&root,std::ostream&log);
bool prepareVisualAssets(const fs::path& root,std::ostream& log){
    if(!fs::is_regular_file(root/L"SAStandalone-assets"/L"manifest.json"))return true;
    const auto module=LoadLibraryExW((root/L"scripts"/L"sa_delirium_bootstrap.dll").c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Prepare=int(__cdecl*)(const wchar_t*,std::size_t,char*,std::size_t);
    using Add=bool(WINAPI*)(const wchar_t*,const wchar_t*,int);
    using Remove=void(WINAPI*)(const wchar_t*);
    const auto loader=GetModuleHandleW(L"dinput8.dll");
    const auto prepare=module?reinterpret_cast<Prepare>(GetProcAddress(module,"sa_delirium_prepare_visuals")):nullptr;
    const auto add=loader?reinterpret_cast<Add>(GetProcAddress(loader,"AddVirtualPathForOverloadW")):nullptr;
    const auto remove=loader?reinterpret_cast<Remove>(GetProcAddress(loader,"RemoveVirtualPathFromOverloadW")):nullptr;
    if(!prepare||!add||!remove){log<<"visual_pack_api_unavailable\n";return false;}
    std::array<char,2048> status{};
    const int result=prepare(root.c_str(),root.native().size(),status.data(),status.size());
    log<<status.data()<<'\n'<<std::flush;
    if(result!=0)return false;
    std::ifstream input(root/L"SAStandalone-assets-active.tsv");
    std::vector<std::pair<fs::path,fs::path>> mappings;
    std::string line;
    const auto safe=[](const fs::path& p){
        if(p.empty()||p.is_absolute()||p.has_root_name())return false;
        for(const auto& part:p)if(part==L".."||part==L".")return false;
        return true;
    };
    while(std::getline(input,line)){
        const auto tab=line.find('\t');
        if(tab==std::string::npos||line.find('\t',tab+1)!=std::string::npos)return false;
        const auto from=fs::u8path(line.substr(0,tab));const auto to=fs::u8path(line.substr(tab+1));
        if(!safe(from)||!safe(to)||!fs::is_regular_file(root/to))return false;
        mappings.emplace_back(root/from,root/to);
    }
    if(!input.eof()||mappings.empty())return false;
    bool hasDefinitions=false;
    for(const auto&[from,to]:mappings)if(_wcsicmp(from.extension().c_str(),L".ide")==0)hasDefinitions=true;
    if(hasDefinitions&&!installSteamModelStores(root,log)){log<<"required_model_store_backend_unavailable\n";return false;}
    bool hasWorldMaps=false;
    for(const auto&[from,to]:mappings)if(_wcsicmp(from.filename().c_str(),L"gta.dat")==0)hasWorldMaps=true;
    if(hasWorldMaps&&!installSteamVisualPools(root,log)){log<<"required_collision_pool_backend_unavailable\n";return false;}
    std::size_t installed=0;
    for(const auto& [from,to]:mappings){
        if(!add(from.c_str(),to.c_str(),10)){
            for(std::size_t i=0;i<installed;++i)remove(mappings[i].first.c_str());
            log<<"visual_path_registration_rolled_back\n";return false;
        }
        ++installed;
        log<<"visual_path_registered "<<from.lexically_relative(root).generic_string()
           <<" -> "<<to.lexically_relative(root).generic_string()<<'\n';
    }
    log<<"visual_pack_registered paths="<<installed<<'\n'<<std::flush;return true;
}
