#include "delirium_physics.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <iostream>
// A real 32-bit client of the DLL. No GTA executable, hooks or desktop input.
int main(int argc,char**argv){
    assert(argc==3);
    assert(nm_combat_load(nullptr)==nullptr);
    auto*data=nm_combat_load(argv[1]);assert(data);
    std::ifstream in(argv[2]);assert(in);
    const std::string json((std::istreambuf_iterator<char>(in)),{});
    const int size=nm_combat_evaluate_hit(data,json.c_str(),nullptr,0);assert(size>1&&size<65536);
    std::vector<char> first(size),second(size);
    assert(nm_combat_evaluate_hit(data,json.c_str(),first.data(),size)==size);
    assert(nm_combat_evaluate_hit(data,json.c_str(),second.data(),size)==size);
    assert(first==second&&first.back()==0);
    assert(std::string(first.data()).find("\"route\":\"natural_motion\"")!=std::string::npos);
    char shortBuffer[4]={'x','x','x','x'};
    assert(nm_combat_evaluate_hit(data,json.c_str(),shortBuffer,4)==size&&shortBuffer[3]==0);
    assert(nm_combat_evaluate_hit(data,"{}",nullptr,0)==NM_E_ARG);
    assert(nm_combat_evaluate_hit(nullptr,json.c_str(),nullptr,0)==NM_E_ARG);
    assert(nm_combat_evaluate_hit(data,nullptr,nullptr,0)==NM_E_ARG);
    assert(nm_combat_evaluate_hit(data,json.c_str(),second.data(),size)==size&&first==second);
    nm_combat_destroy(data);nm_combat_destroy(nullptr);
    std::cout<<"PASS: x86 combat ABI; deterministic two-pass result; buffer and invalid input contracts\n";
}
