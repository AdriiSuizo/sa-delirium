#pragma once

namespace mounted_weapon {
// SA draws equipment in a separate pass. A rendered driver can reach that
// pass with neither an entry in the weapon list nor a hand model.
template<class Host,class Render>
void run(Host& h,Render render){
    void* ped=h.visiblePed();
    if(!ped){render();return;}
    const int model=h.modelFor(ped);
    if(model<0){render();return;}
    void* current=h.currentClump(ped);
    if(current&&h.matches(current,model)){
        if(!h.contains(ped))h.enqueue(ped);
        render();
        return;
    }

    void* visual=h.visualFor(model);
    // A previously selected model can still be attached while its replacement
    // streams. Let other characters render and hide that stale model for this pass.
    if(!visual&&!current){render();return;}
    struct Restore {
        void *&weapon,*&flash,*previousWeapon,*previousFlash;
        ~Restore(){weapon=previousWeapon;flash=previousFlash;}
    } restore{h.weapon(ped),h.flash(ped),current,h.flash(ped)};
    h.weapon(ped)=visual;
    h.flash(ped)=visual?h.flashFor(visual):nullptr;
    if(visual&&!h.contains(ped))h.enqueue(ped);
    render();
}
}
