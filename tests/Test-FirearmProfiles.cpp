#include "../vendor/src/sa-delirium/WeaponProfiles.hpp"
#include "../vendor/src/sa-delirium/CombatHealth.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc,char**argv){
    static_assert(combat_health::healthy(100,true)==200&&combat_health::healthy(100,false)==200);
    static_assert(combat_health::healthy(1,true)==101&&combat_health::healthy(0,true)==0);
    static_assert(combat_health::visible(combat_health::healthy(50,false),200)==0);
    static_assert(combat_health::nativeDamage(combat_health::bulletDamage(25,1,true,false,false),true)==8.25f);
    static_assert(combat_health::nativeDamage(combat_health::bulletDamage(25,1,false,false,false),false)==12.5f);
    static_assert(combat_health::meleeDamage(10,1,false,0x4B5,true,true)==20);
    static_assert(combat_health::meleeDamage(10,1,true,0x4C3,false,true)==2.5f);
    const auto synthetic=firearm::parseProfiles(R"xml(
        <weapons><weapon type='AK47'><data slot='RIFLE' timebetweenshots='133' weaponrange='65'>
        <physics force='75'/></data></weapon>
        <weapon type='SHOTGUN'><data slot='SHOTGUN' timebetweenshots='1233'>
        <physics force='15'/><aiming accuracy='0.08' pellets='16'/></data></weapon>
        <weapon type='M4'><data timebetweenshots='abc'><physics force='75'/></data></weapon></weapons>
    )xml");
    assert(synthetic[30].force==75&&synthetic[30].interval==133&&synthetic[30].reach==65);
    assert(synthetic[25].shotgun&&synthetic[25].accuracy==.08f&&synthetic[25].pellets==16);
    assert(synthetic[31].interval==0&&synthetic[31].force==0);
    const V forward{0,1,0};
    assert(length(firearm::pelletDirection(forward,.08f,0,0)-forward)<.00001f);
    const auto edge=firearm::pelletDirection(forward,.08f,1,-1);
    assert(std::abs(length(edge)-1.f)<.00001f&&std::abs(std::abs(edge.x/edge.y)-.032f)<.00001f&&edge.z<0);
    assert(firearm::shotDirection({},{0,3,4}).y==1.f);
    if(argc<2)return 0;
    std::ifstream file(argv[1],std::ios::binary);
    assert(file);
    const std::string xml((std::istreambuf_iterator<char>(file)),{});
    const auto owned=firearm::parseProfiles(xml);
    const unsigned expected[]={333,333,333,1233,1233,433,66,66,133,133,66,333,400};
    for(int id=22;id<=34;++id)assert(owned[id].interval==expected[id-22]&&owned[id].force>0);
    assert(owned[38].interval==66&&owned[38].force==500);
    assert(owned[22].damage==25&&!owned[22].penetratesArmour);
    assert(owned[25].force==15&&owned[25].damage==20&&owned[25].accuracy==.08f&&owned[25].pellets==0);
    assert(owned[27].force==12&&owned[27].damage==10&&owned[27].accuracy==.05f&&owned[27].pellets==0);
    assert(owned[34].damage==20&&owned[34].scopedDamage==75);
}
