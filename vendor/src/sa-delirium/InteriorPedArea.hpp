#pragma once
#include <map>

namespace interior_peds {
struct Evidence {
    int reference;
    short model;
    const void* clump;
    int room;
};
class Membership {
    std::map<int, Evidence> bySlot;
public:
    void clear() { bySlot.clear(); }

    void observe(int tag, const Evidence& ped) {
        if (tag == 0 && ped.room > 0 && ped.reference >= 0 && ped.clump)
            bySlot.insert_or_assign(ped.reference >> 8, ped);
    }

    bool contains(int tag, const Evidence& ped) const {
        // CEntity::IsInCurrentArea also admits the shared area (13).
        if (tag == ped.room || tag == 13) return true;
        if (tag != 0 || ped.room <= 0 || ped.reference < 0 || !ped.clump) return false;
        const auto found = bySlot.find(ped.reference >> 8);
        if (found == bySlot.end()) return false;
        const auto& seen = found->second;
        return seen.reference == ped.reference && seen.model == ped.model &&
               seen.clump == ped.clump && seen.room == ped.room;
    }
};
}
