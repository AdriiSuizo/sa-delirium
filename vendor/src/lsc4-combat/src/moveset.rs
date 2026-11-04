//! The move clip set's flags (+0x378, board PD5): which kinds of clip a set has, set by its set-up 0x826666F8 (at
//! 0x82666D90-0x8266755C). Each flag asks 0x821E2E20 whether the set's dictionary (+0) has clips, by their ids (the move
//! clip names at 0x82A97824, id = index + 0xE), and when it does not, the set's partner (+4, from the list at
//! 0x82CB1B18; -1 for none). The starts, the stops, the strafe, turning, shuffle and slope steps read them. Read in
//! `docs/peds/movement.md` (The set's flags).

/// The partner list (0x82CB1B18) as 0x82295C20 reads it from common:/data/moveblend.dat: each `MOVEMENT_GROUP a
/// FALLBACK_GROUP b` line (sscanf 0x8200A60C) gives set `a` the partner `b`, in the file's order; the set-up takes the
/// first for its set. Lines starting `#` are skipped; the game drops a pair whose dictionary is not found.
pub fn read_fallbacks(text: &str) -> Vec<(String, String)> {
    text.lines().filter_map(|l| match l.split_whitespace().collect::<Vec<_>>()[..] {
        ["MOVEMENT_GROUP", a, "FALLBACK_GROUP", b, ..] => Some((a.to_string(), b.to_string())),
        _ => None,
    }).collect()
}

/// WStart (0x12), asked of the first dictionary alone: +0, or the partner when the set's +0xFC has bit 2.
pub const STARTS: u32 = 0x8000_0000;
/// The walk's turning starts, WStart_Turn_L90..R180 (0x13-0x16).
pub const TURN_STARTS: u32 = 0x4000_0000;
/// The run starts, RunStart_Fwd..R_180 (0x17-0x1B).
pub const RUN_STARTS: u32 = 0x2000_0000;
/// WStop_L and WStop_R (0x26, 0x27).
pub const WALK_STOPS: u32 = 0x1000_0000;
/// RStop_L and RStop_R (0x30, 0x31).
pub const RUN_STOPS: u32 = 0x0800_0000;
/// Walk and its three strafes (0x8204374C: 0x1C, 0x3E, 0x3F, 0x3D).
pub const WALK_STRAFE: u32 = 0x0400_0000;
/// Both strafes, walk and run, and their four 45s each (0x8204373C: 0x44, 0x46, 0x45, 0x43; 0x8204372C: 0x48, 0x4A,
/// 0x49, 0x47): the eight-way blend.
pub const EIGHT_WAY: u32 = 0x0200_0000;
/// Run and its three strafes (0x82018A08: 0x28, 0x41, 0x42, 0x40).
pub const RUN_STRAFE: u32 = 0x0100_0000;
/// Turn_360_L and _R (0x10, 0x11): turning on the spot.
pub const TURN_ON_SPOT: u32 = 0x0080_0000;
/// The six 180 turns (0x37-0x3C).
pub const TURN_180: u32 = 0x0040_0000;
/// Shuffle_Stop (0x4B).
pub const SHUFFLE: u32 = 0x0020_0000;
/// Walk_Up, Walk_Down, Run_Up, Run_Down (0x4C-0x4F).
pub const SLOPES: u32 = 0x0010_0000;

const WALK_STRAFE_IDS: [u32; 4] = [0x1C, 0x3E, 0x3F, 0x3D];
const RUN_STRAFE_IDS: [u32; 4] = [0x28, 0x41, 0x42, 0x40];
const WALK_45_IDS: [u32; 4] = [0x44, 0x46, 0x45, 0x43];
const RUN_45_IDS: [u32; 4] = [0x48, 0x4A, 0x49, 0x47];

/// The set's +0xFC bit 2, its `alt` below (0x82666884-0x826668A0 in the set-up's slot loop): the slot at +0xF8 (group 1's
/// fourth, WStart 0x12 from the constructor, 0x8266644C) found in the partner's dictionary and not the set's own.
/// `partner` is whether the set has one; `has(partner, id)` as in [`flags`]. Taken as streamed (0x82248B50).
pub fn start_from_partner(partner: bool, has: impl Fn(bool, u32) -> bool) -> bool {
    partner && !has(false, 0x12) && has(true, 0x12)
}

/// The set's flags as 0x826666F8 leaves them: `old` keeps the bits it does not set. `has(partner, id)` is 0x821E2E20 on
/// the set's dictionary (false) or its partner's (true); `partner` is whether the set has one (+4 is not -1), `alt` its
/// +0xFC bit 2. A missing dictionary (-1) has nothing.
pub fn flags(old: u32, partner: bool, alt: bool, has: impl Fn(bool, u32) -> bool) -> u32 {
    let either = |id| has(false, id) || partner && has(true, id);
    let all = |ids: &[u32]| ids.iter().all(|&id| either(id));
    // The starts' dictionary is +0, or +4 with the bit; the turning and run starts then fall back to +4.
    let first = |id| if alt { partner && has(true, id) } else { has(false, id) };
    let starts = |ids: std::ops::RangeInclusive<u32>| ids.into_iter().all(|id| first(id) || partner && has(true, id));
    let (walk, run) = (all(&WALK_STRAFE_IDS), all(&RUN_STRAFE_IDS));
    let eight = walk && run && all(&WALK_45_IDS) && all(&RUN_45_IDS);
    let bits = [
        (STARTS, first(0x12)),
        (TURN_STARTS, starts(0x13..=0x16)),
        (RUN_STARTS, starts(0x17..=0x1B)),
        (WALK_STOPS, all(&[0x26, 0x27])),
        (RUN_STOPS, all(&[0x30, 0x31])),
        (WALK_STRAFE, walk),
        (EIGHT_WAY, eight),
        // 0x82666DD8: the eight-way's run 45s, else the run strafe; the eight-way needs the run strafe.
        (RUN_STRAFE, run),
        (TURN_ON_SPOT, all(&[0x10, 0x11])),
        (TURN_180, all(&[0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C])),
        (SHUFFLE, either(0x4B)),
        (SLOPES, all(&[0x4C, 0x4D, 0x4E, 0x4F])),
    ];
    bits.iter().fold(old, |f, &(bit, on)| if on { f | bit } else { f & !bit })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_set_with_starts_and_stops_but_no_turning_starts() {
        let own = [0x12, 0x26, 0x27, 0x30, 0x31, 0x10, 0x11, 0x1C];
        let f = flags(0x0004_0001, false, false, |p, id| !p && own.contains(&id));
        assert_eq!(f, STARTS | WALK_STOPS | RUN_STOPS | TURN_ON_SPOT | 0x0004_0001);
    }

    #[test]
    fn wstart_comes_from_the_partner_only_when_the_set_lacks_it() {
        assert!(start_from_partner(true, |p, id| p && id == 0x12));
        assert!(!start_from_partner(true, |_, id| id == 0x12));
        assert!(!start_from_partner(true, |_, _| false));
        assert!(!start_from_partner(false, |_, _| true));
    }

    #[test]
    fn fallbacks_in_order() {
        let f = read_fallbacks("# x\nMOVEMENT_GROUP\tmove_crouch\t\tFALLBACK_GROUP\tmove_combat_strafe_c\r\nSIMILAR_ANIMS\n");
        assert_eq!(f, [("move_crouch".to_string(), "move_combat_strafe_c".to_string())]);
    }

    #[test]
    fn the_partner_fills_in_but_not_the_first_start() {
        let f = flags(STARTS, true, false, |p, id| p && (0x12..=0x16).contains(&id));
        assert_eq!(f, TURN_STARTS);
        let f = flags(0, true, true, |p, id| p && id == 0x12);
        assert_eq!(f, STARTS);
        assert_eq!(flags(0, false, true, |_, _| true) & (STARTS | TURN_STARTS | RUN_STARTS), 0);
    }
}
