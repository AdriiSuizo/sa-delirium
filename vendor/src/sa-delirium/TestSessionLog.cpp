#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <algorithm>
#include <iostream>
namespace fs=std::filesystem;
static unsigned gameTime(){return 1234;}
#include "SessionLog.hpp"
int main(int argc,char**argv){
    if(argc!=2)return 2;
    // Independently compare the previous escaping algorithm on every byte.
    std::string input;
    for(int i=0;i<256;i++)input.push_back(char(i));
    std::ostringstream expected;
    expected<<'"';
    for(unsigned char c:input){
        if(c=='"'||c=='\\')expected<<'\\'<<char(c);
        else if(c<32)expected<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c)<<std::dec;
        else expected<<char(c);
    }
    expected<<'"';
    if(sessionlog::quote(input)!=expected.str())return 6;
    const auto game=fs::absolute(argv[1])/"game";fs::create_directories(game);
    {std::ofstream f(game/"gta_sa.exe",std::ios::binary);f<<"hash fixture";}
    SetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW","1");
    sessionlog::init(game);if(!sessionlog::enabled)return 3;
    sessionlog::event("quoted\"event",7,8,"line1\nline2\t\\path\x01");
    sessionlog::pose("{\"pid\":1,\"ref\":7,\"wall_ms\":100}");
    // Cross the byte boundary without writing 64 MiB of disposable padding.
    sessionlog::poseBytes=67108864;
    sessionlog::pose("{\"pid\":1,\"ref\":7,\"wall_ms\":200}");
    if(sessionlog::poseLimit)return 4;
    sessionlog::poses.flush();sessionlog::events.flush();
    const auto oldPath=game/"events-reopen.jsonl",newPath=game/"events-retained.jsonl";
    {std::ofstream old(oldPath),now(newPath);}
    sessionlog::LegacyEventLog old(oldPath,true),retained(newPath,false);
    const auto batch=[](sessionlog::LegacyEventLog& log,int time){
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<51;i++)log.write(123,time,"quoted\"event",i,8,"line1\nline2\t\\path\x01");
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    for(int i=0;i<8;i++){
        double a,b;
        if(i%2){b=batch(retained,i);a=batch(old,i);}
        else{a=batch(old,i);b=batch(retained,i);}
        std::cout<<"event_batch "<<i<<" reopen_ms="<<a<<" retained_ms="<<b<<'\n';
    }
    const auto bytes=[](const fs::path&path){
        std::ifstream in(path,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),{});
    };
    const auto a=bytes(oldPath),b=bytes(newPath);
    if(a.empty()||a!=b||std::count(a.begin(),a.end(),'\n')!=8*51)return 5;
    std::cout<<"legacy_output_byte_equal rows=408\n";
    return 0;
}
