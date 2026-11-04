#pragma once
// Passive, per-launch evidence. No input, game flags, animation or physics writes.
#include <wincrypt.h>
#include <mutex>
#include <iomanip>
namespace sessionlog {
static fs::path directory;
static std::ofstream events,poses;
static std::mutex lock;
static unsigned long long sequence=0,eventBytes=0,poseBytes=0;
static bool enabled=false,poseLimit=false;
static std::string id;
static std::string quote(const std::string&s){
    static constexpr char hex[]="0123456789abcdef";
    std::string out;out.reserve(s.size()+2);out.push_back('"');
    for(unsigned char c:s){
        if(c=='"'||c=='\\'){out.push_back('\\');out.push_back(char(c));}
        else if(c<32){out+="\\u00";out.push_back(hex[c>>4]);out.push_back(hex[c&15]);}
        else out.push_back(char(c));
    }
    out.push_back('"');return out;
}
class LegacyEventLog {
    fs::path path;
    std::mutex mutex;
    std::ofstream output;
    bool reopen;
public:
    LegacyEventLog(fs::path file,bool reopenEachEvent):path(std::move(file)),reopen(reopenEachEvent){}
    void write(DWORD pid,unsigned time,const std::string&type,int ref,int model,const std::string&detail){
        const auto row=[&](std::ostream& f){
            f<<"{\"pid\":"<<pid<<",\"time_ms\":"<<time<<",\"event\":"<<quote(type)
                <<",\"ref\":"<<ref<<",\"model\":"<<model<<",\"detail\":"<<quote(detail)<<"}\n";
        };
        std::lock_guard<std::mutex> guard(mutex);
        if(reopen){std::ofstream f(path,std::ios::app);row(f);return;}
        if(!output.is_open())output.open(path,std::ios::app);
        row(output);output.flush();
        if(!output){
            if(output.is_open())output.close();
            output.clear();
        }
    }
};
static std::string sha(const fs::path&p){
    std::ifstream f(p,std::ios::binary);if(!f)return {};
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return {};
    bool valid=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=0;
    char data[65536];while(valid&&f){f.read(data,sizeof(data));if(f.gcount())valid=CryptHashData(hash,reinterpret_cast<BYTE*>(data),DWORD(f.gcount()),0)!=0;}
    BYTE out[32]{};DWORD n=32;valid=valid&&f.eof()&&CryptGetHashParam(hash,HP_HASHVAL,out,&n,0)!=0;
    if(hash)CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    if(!valid)return {};std::ostringstream s;for(auto b:out)s<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);return s.str();
}
static void init(const fs::path&game){
    const auto ini=game/"DeliriumSA.ini";
    if(!GetPrivateProfileIntW(L"Diagnostics",L"SessionLogging",1,ini.c_str()))return;
    SYSTEMTIME t;GetSystemTime(&t);char stamp[64];
    snprintf(stamp,sizeof(stamp),"%04u%02u%02u-%02u%02u%02u-%03u-p%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId());id=stamp;
    directory=game.parent_path()/"logs"/"manual-sessions"/id;
    std::error_code ec;fs::create_directories(directory,ec);if(ec)return;
    events.open(directory/"events.jsonl");poses.open(directory/"poses.jsonl");enabled=events.good()&&poses.good();if(!enabled)return;
    char passive[4]{};const bool test=GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",passive,4)>0;
    std::ofstream manifest(directory/"session.json");
    manifest<<"{\"schema\":2,\"session\":"<<quote(id)<<",\"pid\":"<<GetCurrentProcessId()
        <<",\"clock\":\"UTC directory; game_ms may reset on load; wall_ms is monotonic\",\"mode\":"<<quote(test?"owned_test":"manual")
        <<",\"sample_interval_ms\":50,\"pose_limit_bytes\":134217728,\"pose_retention\":\"rolling current and previous 64 MiB chunks; older poses expire, events retained separately\",\"event_limit_bytes\":16777216,\"files\":[";
    bool first=true;for(const char*name:{"gta_sa.exe","scripts/DeliriumSA.asi","delirium_physics.dll","DeliriumSA/combat/action_table.csv","DeliriumSA/combat/sa-contact-ranges.json","DeliriumSA/combat/decision/M_NO_NO.cmb","DeliriumSA/combat/decision/R_Norm.ped","DeliriumSA/combat/decision/m_empty.ped","DeliriumSA.ini","modloader/Better Animations/anim/ped.ifp","modloader/Better Animations/data/melee.dat","modloader/fxsfuncs/fxsfuncs.asi","modloader/fxsfuncs/FxsFuncs.ini"}){
        if(!first)manifest<<',';first=false;manifest<<"{\"path\":"<<quote(name)<<",\"sha256\":"<<quote(sha(game/name))<<'}';
    }manifest<<"]}\n";
    for(const char*name:{"DeliriumSA/combat/sa-contact-ranges.json","DeliriumSA.ini","modloader/Better Animations/data/melee.dat","modloader/fxsfuncs/FxsFuncs.ini"})fs::copy_file(game/name,directory/fs::path(name).filename(),fs::copy_options::none,ec);
    std::ofstream latest(game.parent_path()/"logs"/"manual-sessions"/"latest.txt");latest<<directory.string();
}
static void event(const std::string&type,int ref,int model,const std::string&detail){
    if(!enabled)return;std::lock_guard<std::mutex> guard(lock);
    if(eventBytes>=16777216)return;
    std::ostringstream s;s<<"{\"session\":"<<quote(id)<<",\"seq\":"<<++sequence<<",\"pid\":"<<GetCurrentProcessId()<<",\"wall_ms\":"<<GetTickCount64()<<",\"time_ms\":"<<gameTime()
        <<",\"event\":"<<quote(type)<<",\"ref\":"<<ref<<",\"model\":"<<model<<",\"detail\":"<<quote(detail)<<"}\n";
    const auto row=s.str();events<<row;events.flush();eventBytes+=row.size();
    if(eventBytes>=16777216){events<<"{\"event\":\"event_budget_exhausted\"}\n";events.flush();}
}
static void pose(const std::string&row){
    if(!enabled||poseLimit)return;std::lock_guard<std::mutex> guard(lock);
    if(poseBytes+row.size()+1>67108864){
        poses.close();std::error_code ec;
        fs::remove(directory/"poses.previous.jsonl",ec);
        if(!ec)fs::rename(directory/"poses.jsonl",directory/"poses.previous.jsonl",ec);
        if(!ec)poses.open(directory/"poses.jsonl",std::ios::trunc);
        if(ec||!poses){poseLimit=true;events<<"{\"event\":\"pose_rotation_failed\"}\n";events.flush();return;}
        poseBytes=0;
        events<<"{\"event\":\"pose_window_rotated\",\"wall_ms\":"<<GetTickCount64()<<"}\n";events.flush();
    }
    poses<<row<<'\n';poses.flush();poseBytes+=row.size()+1;
}
}
