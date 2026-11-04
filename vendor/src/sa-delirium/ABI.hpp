#pragma once
// Narrow views verified against src/plugin-sdk-sa (current checked-in fork).
// No guessed executable offsets are selected at runtime: only the pinned 1.0 US image is accepted.
#include "Math.hpp"
#include <cstdint>
#include <cstddef>
template<class A>inline A& at(void* p,size_t n){return *reinterpret_cast<A*>(static_cast<unsigned char*>(p)+n);}
struct Pool {unsigned char* objects;unsigned char* flags;int size,first;};
struct Node {int id,index,flags;void* frame;};
struct Hierarchy {int flags,num;M* matrices;void* allocation;Node* nodes;void* frame;};
struct Box {V min,max;};
struct ColBox {Box box;unsigned char material,flags,lighting,light;};
struct ColSphere {V pos;float radius;unsigned char material,flags,lighting,light;};
struct Tri {unsigned short a,b,c;unsigned char material,light;};
struct Compressed {short x,y,z;};
struct ColData {unsigned short spheres,boxes,triangles;unsigned char lines,flags;ColSphere* sphere;ColBox* box;void* line;Compressed* vertices;Tri* tri;};
struct ColModel {Box box;V sphere;float radius;unsigned char slot,flags,pad[2];ColData* data;};
struct ColPoint {V point;float pad0;V normal;float pad1;unsigned char surfaces[8];float depth;};
static_assert(sizeof(ColPoint)==0x2C&&offsetof(ColPoint,normal)==0x10);
static_assert(sizeof(Node)==16&&offsetof(Hierarchy,nodes)==16);
static_assert(sizeof(ColModel)==0x30&&offsetof(ColModel,data)==0x2C);
static_assert(offsetof(ColData,vertices)==0x14&&offsetof(ColData,tri)==0x18);
static_assert(sizeof(Tri)==8&&sizeof(ColBox)==0x1C&&sizeof(ColSphere)==0x14);
// gta-reversed EventDamage.h (pinned in research/native-abi-reference).
struct DamageResponse {float health,armour;bool zero,force,calculated,check;};
struct DamageEvent {unsigned char base[0x14];void* source;unsigned time;int weapon,piece;unsigned char direction,flags,unk26,unk27;int animGroup,anim;float blend,speed;DamageResponse response;};
static_assert(sizeof(DamageEvent)==0x44&&offsetof(DamageEvent,source)==0x14&&offsetof(DamageEvent,response)==0x38);
// CEvent base is 0xC in Event.h; all members follow EventKnockOffBike.h.
struct KnockOffEventView {unsigned char base[0xC];V moveSpeed,impact;float intensity,extra;unsigned time;void* ped;unsigned char direction,flags,type,door;void* vehicle;};
static_assert(sizeof(KnockOffEventView)==0x3C&&offsetof(KnockOffEventView,impact)==0x18&&offsetof(KnockOffEventView,intensity)==0x24&&offsetof(KnockOffEventView,type)==0x36&&offsetof(KnockOffEventView,vehicle)==0x38);
inline Pool* pedPool(){return *reinterpret_cast<Pool**>(0xB74490);}
inline Pool* vehiclePool(){return *reinterpret_cast<Pool**>(0xB74494);}
inline int vehicleRef(void* p){auto*b=vehiclePool();if(!b||!p||b->size<1||b->size>10000)return -1;auto delta=static_cast<unsigned char*>(p)-b->objects;if(delta<0||delta%0xA18)return -1;int i=int(delta/0xA18);if(i>=b->size||(b->flags[i]&0x80))return -1;return(i<<8)|b->flags[i];}
inline void* vehicleFromRef(int ref){auto*b=vehiclePool();int i=ref>>8;if(ref<0||!b||i<0||i>=b->size||b->flags[i]!=(ref&255))return nullptr;return b->objects+i*0xA18;}
inline bool inVehicle(void*p){return (at<unsigned char>(p,0x46D)&1)!=0;}
inline bool ridingBike(void*p){auto*v=at<void*>(p,0x58C);if(!inVehicle(p)||vehicleRef(v)<0)return false;int type=at<int>(v,0x594);return type==9||type==10||type==2;}
inline void* player(){return *reinterpret_cast<void**>(0xB7CD98);}
inline M* matrix(void* e){return at<M*>(e,0x14);}
inline V position(void* e){auto*m=matrix(e);return m?m->pos:at<V>(e,4);}
inline int reference(void* p){auto*b=pedPool();if(!b||!p||b->size<1||b->size>10000)return -1;auto delta=static_cast<unsigned char*>(p)-b->objects;if(delta<0||delta%0x7C4)return -1;int i=int(delta/0x7C4);if(i>=b->size||(b->flags[i]&0x80))return -1;return(i<<8)|b->flags[i];}
inline void* pedFromRef(int ref){auto*b=pedPool();int i=ref>>8;if(ref<0||!b||i<0||i>=b->size||b->flags[i]!=(ref&255))return nullptr;return b->objects+i*0x7C4;}
inline Hierarchy* hierarchy(void* ped){auto*c=at<void*>(ped,0x18);return c?reinterpret_cast<Hierarchy*(__cdecl*)(void*)>(0x734A40)(c):nullptr;}
inline bool onMission(){return reinterpret_cast<bool(__cdecl*)()>(0x464D50)();}
// The ambient phone script also sets OnAMissionFlag. The mission loader's
// separate flag identifies an actual mission script, not a ringing phone.
inline bool scriptMissionActive(){return *reinterpret_cast<bool*>(0xA444B1);}
inline bool playing(){return *reinterpret_cast<int*>(0xC8D4C0)==9&&!*reinterpret_cast<bool*>(0xBA67A4);}
inline unsigned gameTime(){return *reinterpret_cast<unsigned*>(0xB7CB84);}
inline int area(){return *reinterpret_cast<int*>(0xB72914);}
inline bool cutscene(){return *reinterpret_cast<bool*>(0xB5F852);}
inline void teleport(void*p,V v){reinterpret_cast<void(__thiscall*)(void*,V,bool)>((*reinterpret_cast<void***>(p))[14])(p,v,false);}
