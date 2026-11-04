#pragma once
// OBSERVER ONLY. No include/registration in Adapter yet. This header never
// installs a hook, suppresses a contact, restores speed or changes damage.
// CRITICAL: 54A2C0 MUST NEVER receive an entry detour. 406B2A reads54A2C1
// as data; this project's previous crash established that dependency. Reuse
// Scope around BikeImpactCapture's existing wrappers and validated CALL sites.
// The wrappers below are ABI fixtures / call-site targets, NOT entry-hook
// instructions. No address is registered by this header.
// ABI: local gta_sa.exe SHA256 f01a00ce950fa40ca1ed59df0e789848c6edcf6405456274965885d0929343ac,
// plugin-sdk CPhysical.cpp, plus npc-audit-native-owner-authority.asm.
// Pair hard/soft: thiscall(self, other, point&, float&, float&), ret16.
// Pair friction: thiscall(self, other, float, point&), ret12.
// Single hard/soft: thiscall(self, other, point&, float&), ret12.
// ApplyCollisionAlt: thiscall(self, other, point&, float&, V&, V&), ret20.
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace NativePairAudit {
struct Vec { float x{},y{},z{}; };
struct Point { Vec point;float pad0;Vec normal;float pad1;uint8_t surfaces[8];float depth; };
static_assert(sizeof(Point)==44);
struct Identity { int type{},ref=-1; bool finiteOwner{},fresh{};uintptr_t clump{};int model=-1; };
struct Body {
    Identity id; Vec velocity,spin,frictionVelocity,frictionSpin;
    float mass{},turnMass{},health{}; uint32_t flags{};
};
enum class Path : uint32_t { HardPair=0x548680,SoftPair=0x54A2C0,FrictionPair=0x545980,
    HardSingle=0x5435C0,SoftSingle=0x543890,Alt=0x544D50 };
struct Record {
    uint64_t serial{},parent{}; unsigned depth{},frame{}; uintptr_t caller{}; Path path{};
    Body aBefore,bBefore,aAfter,bAfter; Point pointBefore,pointAfter;
    bool result{},damageValid{},hasOutputs{}; float damageA{},damageB{},friction{};
    Vec outputMove,outputTurn;
};
using Identify=Identity(*)(void*);
using Sink=void(*)(const Record&);
using Frame=unsigned(*)();
// Integration must supply generation-bearing identity validation. Null means
// entirely passive, including no raw object reads beyond the original call.
inline Identify identify=nullptr;
inline Sink sink=nullptr;
inline Frame frame=nullptr;
inline thread_local uint64_t serial=0,parent=0;
inline thread_local unsigned depth=0;
template<class T> T read(void*p,unsigned offset){T x;std::memcpy(&x,static_cast<const char*>(p)+offset,sizeof x);return x;}
inline Body capture(void*p,Identity id){
    Body b;b.id=id;b.velocity=read<Vec>(p,0x44);b.spin=read<Vec>(p,0x50);
    b.frictionVelocity=read<Vec>(p,0x5c);b.frictionSpin=read<Vec>(p,0x68);
    b.mass=read<float>(p,0x8c);b.turnMass=read<float>(p,0x90);b.flags=read<uint32_t>(p,0x40);
    if(id.type==3)b.health=read<float>(p,0x540);return b;
}
struct Scope {
    Record r;void*a;void*b;Identity ia,ib;bool active=false;uint64_t prior=0;
    Scope(Path path,void*aa,void*bb,Point*p,uintptr_t caller):a(aa),b(bb){
        if(!identify||!sink||!a||!b||!p)return;
        ia=identify(a);ib=identify(b);
        if(ia.ref<0||ib.ref<0||!((ia.type==3&&ib.type==2)||(ia.type==2&&ib.type==3)))return;
        active=true;prior=parent;r.serial=++serial;r.parent=parent;parent=r.serial;r.depth=depth++;
        r.path=path;r.frame=frame?frame():0;r.caller=caller;r.pointBefore=*p;
        r.aBefore=capture(a,ia);r.bBefore=capture(b,ib);
    }
    void finish(Point*p,bool result,float*da=nullptr,float*db=nullptr,Vec*outMove=nullptr,Vec*outTurn=nullptr){
        if(!active)return;
        r.result=result;r.pointAfter=*p;
        // Do not read a native output on a false-return path: some native
        // branches leave the caller's damage locals uninitialised.
        r.damageValid=result&&da;r.damageA=r.damageValid?*da:0;r.damageB=r.damageValid&&db?*db:0;
        r.hasOutputs=result&&outMove&&outTurn;if(r.hasOutputs){r.outputMove=*outMove;r.outputTurn=*outTurn;}
        const auto ca=identify(a),cb=identify(b);
        if(ca.ref==ia.ref&&ca.type==ia.type&&ca.clump==ia.clump&&ca.model==ia.model&&
           cb.ref==ib.ref&&cb.type==ib.type&&cb.clump==ib.clump&&cb.model==ib.model){
            r.aAfter=capture(a,ca);r.bAfter=capture(b,cb);sink(r);
        }
    }
    ~Scope(){if(active){--depth;parent=prior;}}
};
using Pair=bool(__thiscall*)(void*,void*,Point*,float*,float*);
using Single=bool(__thiscall*)(void*,void*,Point*,float*);
using Friction=bool(__thiscall*)(void*,void*,float,Point*);
using Alt=bool(__thiscall*)(void*,void*,Point*,float*,Vec*,Vec*);
inline Pair hardPairOriginal=nullptr,softPairOriginal=nullptr;
inline Single hardSingleOriginal=nullptr,softSingleOriginal=nullptr;
inline Friction frictionOriginal=nullptr;
inline Alt altOriginal=nullptr;
inline bool __fastcall hardPair(void*a,void*,void*b,Point*p,float*da,float*db){
    Scope s(Path::HardPair,a,b,p,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool r=hardPairOriginal(a,b,p,da,db);s.finish(p,r,da,db);return r;
}
inline bool __fastcall softPair(void*a,void*,void*b,Point*p,float*da,float*db){
    Scope s(Path::SoftPair,a,b,p,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool r=softPairOriginal(a,b,p,da,db);s.finish(p,r,da,db);return r;
}
inline bool __fastcall hardSingle(void*a,void*,void*b,Point*p,float*da){
    Scope s(Path::HardSingle,a,b,p,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool r=hardSingleOriginal(a,b,p,da);s.finish(p,r,da);return r;
}
inline bool __fastcall softSingle(void*a,void*,void*b,Point*p,float*da){
    Scope s(Path::SoftSingle,a,b,p,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool r=softSingleOriginal(a,b,p,da);s.finish(p,r,da);return r;
}
inline bool __fastcall friction(void*a,void*,void*b,float f,Point*p){
    Scope s(Path::FrictionPair,a,b,p,reinterpret_cast<uintptr_t>(_ReturnAddress()));s.r.friction=f;
    const bool r=frictionOriginal(a,b,f,p);s.finish(p,r);return r;
}
inline bool __fastcall alt(void*a,void*,void*b,Point*p,float*da,Vec*mv,Vec*tw){
    Scope s(Path::Alt,a,b,p,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool r=altOriginal(a,b,p,da,mv,tw);s.finish(p,r,da,nullptr,mv,tw);return r;
}
} // namespace NativePairAudit
