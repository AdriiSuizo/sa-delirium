#pragma once

namespace mounted_model {

static void* registeredWeapon(int model){
    // SA 1.0 US has 20,000 DFF model-info slots. Read the actual model
    // definition so that replacements and custom weapon IDs use their own mesh.
    if(model<0||model>=20000)return nullptr;
    auto* info=reinterpret_cast<void**>(0xA9B0C8)[model];
    if(!info)return nullptr;
    auto** methods=*reinterpret_cast<void***>(info);
    using ModelType=unsigned char(__thiscall*)(void*);
    return reinterpret_cast<ModelType>(methods[4])(info)==4?info:nullptr;
}

static int idFor(int weapon){
    if(!drive_by_controls::ivVehicleWeapon(weapon))return -1;
    // SetCurrentWeapon and RemoveWeaponWhenEnteringVehicle also consult the
    // standard-skill definition to select the displayed equipment model.
    auto* definition=reinterpret_cast<void*(__cdecl*)(int,int)>(0x743C60)(weapon,1);
    if(!definition)return -1;
    const int model=at<int>(definition,0x0C);
    return registeredWeapon(model)?model:-1;
}

static bool isClump(void* object){
    return object&&at<unsigned char>(object,0)==2&&at<void*>(object,4);
}

static bool matches(void* clump,int model){
    if(!isClump(clump))return false;
    auto* info=registeredWeapon(model);
    return info&&reinterpret_cast<void*(__cdecl*)(void*)>(0x732AC0)(clump)==info;
}

static void* flashFor(void* clump){
    return isClump(clump)?
        reinterpret_cast<void*(__cdecl*)(void*,const char*)>(0x4C5400)(clump,"gunflash"):nullptr;
}

struct Borrowed {
    Borrowed()=default;
    Borrowed(const Borrowed&)=delete;
    Borrowed& operator=(const Borrowed&)=delete;

    void* get(int model){
        if(instance)return model==modelId?instance:nullptr;
        auto* info=registeredWeapon(model);
        if(!info)return nullptr;
        auto* source=at<void*>(info,0x1C);
        if(!source){
            // RequestModel queues a normal, evictable streaming request. Let
            // the next rendered pass retry after SA has loaded its TXD/DFF.
            static int lastModel=-1;
            static unsigned requestedAt=0;
            const unsigned now=gameTime();
            if(lastModel!=model||now-requestedAt>=200){
                reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,0);
                lastModel=model;requestedAt=now;
            }
            return nullptr;
        }
        if(!isClump(source))return nullptr;
        auto** methods=*reinterpret_cast<void***>(info);
        // CWeaponModelInfo::CreateInstance uses the registered clump's own
        // creation path, including its materials, atomics and extensions.
        instance=reinterpret_cast<void*(__thiscall*)(void*)>(methods[11])(info);
        if(!instance)return nullptr;
        reinterpret_cast<void(__thiscall*)(void*)>(0x4C4BA0)(info);
        owner=info;modelId=model;
        return instance;
    }

    ~Borrowed(){
        if(!instance)return;
        // Match CPed::RemoveWeaponModel's RenderWare ownership sequence for
        // a private model, including animations held by skinned atomics.
        reinterpret_cast<void(__thiscall*)(void*)>(0x4C4BB0)(owner);
        auto* first=reinterpret_cast<void*(__cdecl*)(void*)>(0x734820)(instance);
        auto* geometry=first?at<void*>(first,0x18):nullptr;
        if(geometry&&reinterpret_cast<void*(__cdecl*)(void*)>(0x7C7550)(geometry)){
            using AtomicCallback=void*(__cdecl*)(void*,void*);
            using Enumerate=void*(__cdecl*)(void*,AtomicCallback,void*);
            reinterpret_cast<Enumerate>(0x749B70)(instance,reinterpret_cast<AtomicCallback>(0x734B90),nullptr);
        }
        reinterpret_cast<int(__cdecl*)(void*)>(0x74A310)(instance);
    }
private:
    void* instance=nullptr;
    void* owner=nullptr;
    int modelId=-1;
};
} // namespace mounted_model
