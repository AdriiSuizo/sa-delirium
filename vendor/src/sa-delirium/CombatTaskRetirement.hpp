#pragma once
#include <initializer_list>
#include "../plugin-sdk-sa/plugin_sa/game_sa/enums/eTaskType.h"
inline bool combatOwnsPrimaryRoot(int type){return type==TASK_COMPLEX_KILL_PED_ON_FOOT||type==TASK_COMPLEX_KILL_PED_ON_FOOT_MELEE;}
// End of the imported task is a semantic boundary, not an IV->SA numeric
// urgency cast. SA FightCtrl refuses leisure forever; urgent+no event performs
// its verified normal cleanup and marks the secondary Fight finished.
struct CombatAttackTask {void*task=nullptr;void*target=nullptr;bool fight=false;};
template<class Host> bool retireEndedCombatTasks(Host&h){
    auto attack=h.attack();
    // A newer gun/attack/other-target task is external to this completed fight.
    // In particular, do not let an old primary's abort delegate into it.
    if(attack.task&&(!attack.fight||attack.target!=h.target))return false;
    for(int slot:{1,2}){
        void*root=h.primary(slot);
        if(!root||!h.matchesPrimary(root))continue;
        if(!h.abort(root,1))return false;
        // Abort callbacks can replace a task; never delete the replacement.
        if(h.primary(slot)==root)h.clearPrimary(slot);
    }
    attack=h.attack();
    if(attack.task){
        if(!attack.fight||attack.target!=h.target)return false;
        if(!h.abort(attack.task,1))return false;
        if(h.attack().task==attack.task)h.clearAttack();
    }
    return true;
}
