#pragma once

namespace vehicle_weapon_selection {
// SA stores one carried weapon per category. GTA IV's vehicle shortcuts are
// 3 (handgun), 5 (SMG), 9 (throwable); Q/Z cycle only the eligible inventory.
constexpr std::array<int,3> slots{2,4,8};
constexpr int slotForType(int type){
    switch(type){
        case 22: case 23: case 24: return 2;
        case 28: case 29: case 32: return 4;
        case 16: case 18: return 8;
        default: return -1;
    }
}
struct Inventory { int type; unsigned ammo; };
static int choose(const std::array<Inventory,3>& inventory,int currentSlot,int savedType,
                  int shortcut,int cycle,bool useTrigger,bool firstIntent){
    const auto usable=[&](int i){return inventory[i].ammo && slotForType(inventory[i].type)==slots[i];};
    if(shortcut>=0){
        for(int i=0;i<3;i++)if(shortcut==slots[i]&&usable(i))return slots[i];
        return -1;
    }
    if(cycle){
        int start=cycle>0?2:0;
        for(int i=0;i<3;i++)if(currentSlot==slots[i])start=i;
        for(int n=1;n<=3;n++){
            const int i=(start+cycle*n+3)%3;
            if(usable(i))return slots[i];
        }
        return -1;
    }
    if(!useTrigger)return -1;
    // SA may replace a selected pistol with an SMG on entry. Prefer the gun
    // carried into the car on the first drawn frame. Throws require an
    // explicit choice; running out of bullets must never throw a grenade.
    if(firstIntent)for(int i=0;i<2;i++)
        if(usable(i)&&inventory[i].type==savedType)return slots[i];
    for(int i=0;i<3;i++)if(slots[i]==currentSlot&&usable(i))return currentSlot;
    for(int i=0;i<2;i++)if(usable(i))return slots[i];
    return -1;
}

#ifndef VEHICLE_WEAPON_SELECTION_TEST
static void tick(){
    using namespace drive_by_controls;
    static unsigned previousKeys=0;
    static int previousVehicle=-1,previousPed=-1;
    static bool firstIntent=true;
    auto* pad=reinterpret_cast<void*>(primaryPad);
    if(!installed||!driveByEnabled)return;
    sample();
    const auto* keys=reinterpret_cast<const short*>(0xB73190+0x18);
    const bool next=(!testInput&&keys['Q'])||(connected&&(input.wButtons&XINPUT_GAMEPAD_X));
    const unsigned down=(next?1u:0u)|(!testInput&&keys['Z']?2u:0u)|(!testInput&&keys['3']?4u:0u)|
        (!testInput&&keys['5']?8u:0u)|(!testInput&&keys['9']?16u:0u);
    const unsigned pressed=down&~previousKeys;
    previousKeys=down;
    auto* car=ivVehicleOccupant(pad);
    auto* cj=player();
    const int carRef=car?vehicleRef(car):-1, pedRef=cj?reference(cj):-1;
    if(previousVehicle!=carRef||previousPed!=pedRef){
        firstIntent=true;previousVehicle=carRef;previousPed=pedRef;
    }
    if(!car||!cj||at<unsigned char>(pad,0x11D))return;
    void* data=at<void*>(cj,0x480);
    if(!data||at<unsigned char>(data,0x85))return;
    const bool trigger=aimHeld();
    if((!pressed&&!trigger)||vehicleThrowPhase()>=0.f)return;
    const int currentSlot=at<unsigned char>(cj,0x718);
    if(currentSlot>=13)return;
    std::array<Inventory,3> inventory{};
    for(int i=0;i<3;++i){
        const int offset=0x5A0+slots[i]*0x1C;
        inventory[i]={at<int>(cj,offset),at<unsigned>(cj,offset+0xC)};
    }
    const int shortcut=pressed&4?2:pressed&8?4:pressed&16?8:-1;
    const int cycle=int(bool(pressed&1))-int(bool(pressed&2));
    const int saved=at<int>(cj,0x70C);
    const int chosen=choose(inventory,currentSlot,saved,shortcut,cycle,trigger,firstIntent);
    if(chosen<0)return;
    const bool modelMissing=at<void*>(cj,0x4F4)==nullptr;
    const bool attachModel=modelMissing&&(firstIntent||pressed);
    firstIntent=false;
    if(chosen==currentSlot&&!attachModel)return;
    // Mirror SA's vehicle entry/exit handoff. SetCurrentWeapon(slot) attaches
    // the model and syncs the HUD without awarding ammo or refilling the clip.
    if(chosen!=currentSlot&&saved==55)at<int>(cj,0x70C)=at<int>(cj,0x5A0+currentSlot*0x1C);
    reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E61F0)(cj,chosen);
}
#endif
}
