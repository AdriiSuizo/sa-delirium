#pragma once
#include "../plugin-sdk-sa/plugin_sa/game_sa/enums/eTaskType.h"
struct CombatNativeIntent {int type=-1;void*source=nullptr;};
// Read the ACTIVE primary path. A known same-opponent flee proposal may be
// considered by original IV Combat; unknown panic and weapons are boundaries.
static CombatNativeIntent combatReadNativeIntent(void*ped){
    auto*m=taskManager(ped);if(!m)return {};
    void*t=reinterpret_cast<void*(__thiscall*)(void*)>(0x681720)(m);
    for(int depth=0;t&&depth<16;depth++){
        auto*v=at<void**>(t,0);
        const int type=reinterpret_cast<int(__thiscall*)(void*)>(v[4])(t);
        switch(type){
        case TASK_COMPLEX_FLEE_POINT:case TASK_COMPLEX_FLEE_ENTITY:
        case TASK_COMPLEX_SMART_FLEE_POINT:case TASK_COMPLEX_SMART_FLEE_ENTITY:
        case TASK_COMPLEX_FLEE_ANY_MEANS:case TASK_COMPLEX_FLEE_SHOOTING:
        case TASK_SMART_FLEE_ENTITY_WALKING:case TASK_COMPLEX_LEAVE_CAR_AND_FLEE:
        case TASK_SIMPLE_USE_GUN:case TASK_SIMPLE_GUN_CTRL:
        case TASK_COMPLEX_ARREST_PED:case TASK_COMPLEX_POLICE_PURSUIT:
        case TASK_COMPLEX_COP_ARREST_PED:case TASK_COMPLEX_COP_HASSLE_PED:
            // Arrest and dispatch remain native decision owners across NM.
            // Their nested melee task is not a civilian combat invitation.
            // CTaskComplex is 0xC; SmartFleeEntity's first member is m_fleeFrom.
            // All other task layouts remain unknown here, never guessed.
            return {type,type==TASK_COMPLEX_SMART_FLEE_ENTITY?at<void*>(t,0xC):nullptr};
        }
        t=reinterpret_cast<void*(__thiscall*)(void*)>(v[2])(t);
    }
    return {};
}
static int combatNativeExitIntent(void*ped){return combatReadNativeIntent(ped).type;}
