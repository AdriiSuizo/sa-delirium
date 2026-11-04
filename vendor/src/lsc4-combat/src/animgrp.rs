//! The animation groups (CAnimAssociations, the store at 82BF9AAC: 0x58 bytes each, the count at 82BF9AA0) and
//! `data/animgrp.dat` (level file kind ANIMGRP) as the game reads it into them (Xbox 360 exe, TU8). Board row PP1g5a.
//!
//! The store starts with 101 groups (vf1 0x8224ACD8 through 0x8224AA90; 0x82248638 leaves every one's dictionary
//! slot, +0x48, at -1): 98 named in the exe, 13 of them with a dictionary slot (their dictionary's, +0x20), and three
//! left empty. animgrp.dat's reader (0x8224C1B8) adds groups after them (0x8224ABB0), each with its dictionary slot.
//!
//! A ped model's +0x7C (its vfunc 0x822E37C0) is the first group whose dictionary slot is the model's (+0x56: peds.ide's
//! animation file's slot, -1 for `null`), through 0x82248D68; -1 when none is. Two slots are the same when they are the
//! same dictionary: the anim store finds a dictionary by its name's hash (0x8235C370, then 0x821E0900 or 0x821E2D70).
//! The game works it out once for every ped model (0x822E4B28 hands 0x822E37C0 to 0x821C6C88), in the start-up
//! (0x82141F00) right after animgrp.dat is read (0x8224CBB8) and before meleeanims.dat (0x822BE638 through
//! 0x82660F18, whose reader 0x826605E8 adds more groups), so only the built-in groups and animgrp.dat's count.

use crate::map::hash_bytes;

/// The built-in groups' names (+0x00), in store order.
pub const BUILT_IN: [&str; 101] = [
    "default", "jump_std", "jump_rifle", "ragdoll_trans", "ragdoll_trans_back", "get_up", "get_up_back",
    "cellphone", "cellphone", "climb", "prone", "door", "amb_sit_chair_f", "amb_sit_stool_f", "amb_sit_couch_f",
    "amb_sit_chair_m", "amb_sit_stool_m", "amb_sit_couch_m", "amb_sit_step_m", "amb_sit_wall_m", "arrest", "medic",
    "busted", "AMB@FLEE", "car_bomb", "gun@rifle", "gun@m40a1", "melee", "molotov", "grenade", "CONFIG_SCREEN_F",
    "CONFIG_SCREEN_M", "GUN@COPS", "SWAT_SIGNAL", "cover_l_high_centre", "cover_l_high_corner",
    "cover_l_low_centre", "cover_l_low_corner", "cover_r_high_centre", "cover_r_high_corner", "cover_r_low_centre",
    "cover_r_low_corner", "cover_dive", "injured", "damage_ko", "damage_react", "damage_react_floor",
    "move_m@generic", "move_f@generic", "move_player", "move_rifle", "move_f@armed", "move_combat_strafe",
    "move_combat_strafe_c", "move_crouch", "move_crouch_rifle", "move_rpg", "move_crouch_rpg", "move_melee", "swim",
    "move_crouch_trans", "move_crouch_trans_armed", "move_melee_trans", "move_injured_lower", "move_injured_upper",
    "move_injured_generic", "reaction@male_flee", "gestures@male", "gestures@female", "gestures@car",
    "gestures@car_f", "gestures@niko", "pickup_object", "visemes@m_hi", "visemes@f_hi", "visemes@m_lo",
    "visemes@f_lo", "facials@m_hi", "facials@f_hi", "facials@m_lo", "facials@f_lo", "veh@base", "veh@std",
    "veh@LOW_CONV", "veh@van", "veh@low", "veh@truck", "veh@bus", "veh@train", "veh@HELICOPTER", "veh@boat_speed",
    "veh@busted_std", "veh@busted_low", "veh@busted_van", "veh@busted_truck", "AVOIDS", "EV_DIVES",
    "MOVE_CROUCH_RIFLE", "", "", "",
];

/// The built-in groups set up with a dictionary slot (0x8224AA90's flag at its stack +0x67): their place and
/// dictionary (+0x20). The other 88 named ones keep -1.
pub const BUILT_IN_SLOTS: [(usize, &str); 13] = [
    (47, "move_m@generic"), (48, "move_f@generic"), (49, "move_player"), (50, "move_rifle"), (51, "move_f@armed"),
    (52, "move_combat_strafe"), (53, "move_combat_strafe_c"), (54, "move_crouch"), (55, "move_crouch_rifle"),
    (56, "move_rpg"), (57, "move_crouch_rpg"), (58, "move_melee"), (59, "swimming"),
];

/// A group a file adds to the store: its name (+0x00) and its dictionary's (+0x20, whose slot it gets), as the store
/// entry holds them ([`Group::stored`]).
#[derive(Clone, Debug, PartialEq)]
pub struct Group {
    pub name: Vec<u8>,
    pub dictionary: Vec<u8>,
}

impl Group {
    /// The entry 0x8224ABB0 (and 0x8224AA90) leaves for `name` and `dictionary`: both copied whole (strcpy), the name
    /// to +0x00 and then the dictionary to +0x20, then the count to +0x38. So the dictionary keeps 24 bytes (the
    /// count's top byte, 0, ends it; this before its slot is looked up), and a name of 32 bytes or more runs on into
    /// what the dictionary then overwrites: read back, its first 32 bytes and the dictionary.
    pub fn stored(name: &[u8], dictionary: &[u8]) -> Group {
        let dictionary = dictionary[..dictionary.len().min(24)].to_vec();
        let name = if name.len() >= 32 { [&name[..32], &dictionary[..]].concat() } else { name.to_vec() };
        Group { name, dictionary }
    }
}

/// The kinds of line that always add a group named by their third token, its dictionary of the same name.
/// The kind names' pointers on the reader's stack: each kind, its place past the name buffer, its string's TU8
/// address (0x8224C1B8's prologue; VehicleDrivebyGroupV2 and Resident are held in registers).
const KIND_AT: [(&[u8], usize, u32); 11] = [
    (b"WeaponGroup", 0x20, 0x82008E2C), (b"Version", 0x2C, 0x82008E08), (b"VehicleDrivebyGroup", 0x30, 0x82008EB8),
    (b"GestureGroup", 0x34, 0x82008E50), (b"VehicleGroup", 0x3C, 0x82008E6C), (b"FacialGroup", 0x40, 0x82008E60),
    (b"MovementGroup", 0x44, 0x82008E1C), (b"%d", 0x48, 0x820BC4F8), (b"BikeGroup", 0x4C, 0x82008E7C),
    (b"WeaponProjectileGroup", 0x50, 0x82008E38), (b"VehicleToDrivebyLink", 0x54, 0x82008E88),
];

const ADDS: [&[u8]; 7] =
    [b"MovementGroup", b"WeaponGroup", b"WeaponProjectileGroup", b"GestureGroup", b"FacialGroup", b"VehicleGroup", b"BikeGroup"];

/// The line reader animgrp.dat goes through (0x82568D20): 0x821B5240 into its own 512-byte buffer (0x8307F918),
/// each byte below a space as a signed byte (so also 0x80 and up) or a comma made a space, then the line from its
/// first byte above a space.
pub(crate) struct Lines {
    pub(crate) buf: [u8; 512],
    pub(crate) at: usize,
}

impl Lines {
    pub(crate) fn next(&mut self, file: &[u8]) -> Option<Vec<u8>> {
        crate::textlines::read_line(&mut self.buf, file, &mut self.at)?;
        let b = &mut self.buf;
        let mut i = 0;
        while b[i] != 0 {
            if (b[i] as i8) < 0x20 || b[i] == b',' {
                b[i] = b' ';
            }
            i += 1;
        }
        let mut start = 0;
        while b[start] != 0 && (b[start] as i8) < 0x21 {
            start += 1;
        }
        let end = b[start..].iter().position(|&c| c == 0).map_or(b.len(), |e| start + e);
        Some(b[start..end].to_vec())
    }
}

/// strtok (0x82A01DE8) on " ,\t()" over one line: its tokens in order.
pub(crate) fn tokens(line: &[u8]) -> impl Iterator<Item = &[u8]> {
    line.split(|c| b" ,\t()".contains(c)).filter(|t| !t.is_empty())
}

/// `sprintf` (0x82A00108) with `token` as the format and no arguments: `%%` and a `%` closing a spec early are a `%`,
/// flags, width, precision and sizes are skipped, any other byte breaking off a spec is printed as itself, an
/// unfinished spec at the end is dropped. None for a conversion or `*` that reads an argument: the call gives none,
/// so it reads whatever the caller left in r5 on (a stack address), nothing the file sets. All checked on the exe
/// (tools/xbox360-re/checks/animgrp.py).
fn format(token: &[u8]) -> Option<Vec<u8>> {
    #[derive(PartialEq, Clone, Copy)]
    enum St { Normal, Percent, Flag, Width, Dot, Precis, Size }
    let (mut out, mut st, mut i) = (vec![], St::Normal, 0);
    while i < token.len() {
        let c = token[i];
        i += 1;
        st = match (st, c) {
            (St::Normal, b'%') => St::Percent,
            (St::Percent | St::Flag, b' ' | b'+' | b'-' | b'#' | b'0') => St::Flag,
            (St::Percent | St::Flag | St::Width, b'1'..=b'9') | (St::Width, b'0') => St::Width,
            (St::Dot | St::Precis, b'0'..=b'9') => St::Precis,
            (St::Percent | St::Flag | St::Width, b'.') => St::Dot,
            (St::Percent | St::Flag | St::Dot, b'*') => return None,
            (St::Normal, _) => {
                out.push(c);
                St::Normal
            }
            (_, b'h' | b'l' | b'L' | b'w' | b'F' | b'N') => St::Size,
            (_, b'I') if token[i..].starts_with(b"64") || token[i..].starts_with(b"32") => {
                i += 2;
                St::Size
            }
            (_, b'I') if token.get(i).is_some_and(|n| b"diouxX".contains(n)) => St::Size,
            (_, b'c' | b'C' | b'd' | b'i' | b'o' | b'u' | b'x' | b'X' | b'e' | b'E' | b'f' | b'g' | b'G' | b'a' | b'A' | b'n'
               | b'p' | b's' | b'S' | b'Z') => return None,
            _ => {
                out.push(c);
                St::Normal
            }
        };
    }
    Some(out)
}

/// `sscanf` (0x829FFC90) with `%d` (0x820BC4F8): the number the token starts with (after blanks, a sign), or `old`
/// when it starts with none.
fn scan_int(t: &[u8], old: i32) -> i32 {
    let t = t.trim_ascii_start();
    let (neg, t) = match t.first() {
        Some(b'-') => (true, &t[1..]),
        Some(b'+') => (false, &t[1..]),
        _ => (false, t),
    };
    let digits = t.iter().take_while(|c| c.is_ascii_digit()).count();
    if digits == 0 {
        return old;
    }
    let n = t[..digits].iter().fold(0i32, |n, d| n.wrapping_mul(10).wrapping_add((d - b'0') as i32));
    if neg { n.wrapping_neg() } else { n }
}

/// 0x8224C1B8 over `file`: the groups it adds to the store, in order (they come after the 101 built-in ones). Lines
/// starting with `#` or empty are skipped. The first token is the kind (`Version` reads a number and adds nothing),
/// the second whether resident, the third the group's name. The seven kinds in `ADDS` always add it; a
/// `VehicleDrivebyGroup` or `VehicleDrivebyGroupV2` adds it only when no group has the name yet (0x82248CF0:
/// _stricmp over the whole store), its dictionary the name (copied once more, [`format`]) or, for V2, the fourth token. The kind and the name are
/// copied with `sprintf` (0x82A00108) with the token as the format ([`format`]). The tokens each kind reads after the
/// name: a BikeGroup six numbers (`%f`), a VehicleToDrivebyLink a second group and, when both groups are in the store,
/// a third token, a drive-by group (V2 its dictionary first) six numbers, two before them when the last Version line
/// gave more than 1. None when the game stops: a token it reads is missing (strtok's null goes to `sprintf`,
/// `sscanf` or the Resident compare: the C runtime's invalid parameter handler 0x82A05020, or a fault), or a copied
/// one reads an argument ([`format`]), or a kind pointer a long name broke points outside `exe` (TU8's image: its
/// base address and bytes).
pub fn read(file: &[u8], exe: (u32, &[u8])) -> Option<Vec<Group>> {
    let mut lines = Lines { buf: [0; 512], at: 0 };
    let mut out: Vec<Group> = vec![];
    // The name is strcpy'd into a 32-byte buffer on the reader's stack (r1+0xB0 up), and after it the pointers to
    // the kind names, set once before the first line (and the file's stream at +0x38): a name of 32 bytes or more
    // writes over those within its length (and its zero), and that kind is then compared (an inline strcmp, case and
    // all) with the string the broken pointer points at. In the image that is the image's bytes; outside it (the
    // stack, the heap or nothing mapped) what is there is not in the exe, and the reader stops here. Over the stream,
    // the file ends (the game reads through the broken pointer). At +0x60 is the kind's own buffer: a name of 96
    // bytes or more leaves its tail there as the kind compared.
    let mut stack = [0u8; 0x60];
    for (_, s, at) in KIND_AT {
        stack[s..s + 4].copy_from_slice(&at.to_be_bytes());
    }
    let mut written = 0; // how far past the buffer's start the longest name so far reached (its zero included)
    let mut version = 0; // +0x1A4: the last Version line's number
    let is = |stack: &[u8; 0x60], kind: &[u8], k: &[u8]| -> Option<bool> {
        let Some(&(_, s, at)) = KIND_AT.iter().find(|e| e.0 == k) else { return Some(kind == k) };
        let p = u32::from_be_bytes(stack[s..s + 4].try_into().unwrap());
        if p == at {
            return Some(kind == k);
        }
        let from = exe.1.get(p.checked_sub(exe.0)? as usize..)?;
        let n = from.iter().position(|&c| c == 0)?;
        Some(kind == &from[..n])
    };
    while let Some(line) = lines.next(file) {
        if written > 0x38 {
            break;
        }
        if matches!(line.first(), None | Some(b'#')) {
            continue;
        }
        let mut t = tokens(&line);
        let kind = format(t.next()?)?;
        if is(&stack, &kind, b"Version")? {
            version = scan_int(t.next()?, version);
            continue;
        }
        t.next()?;
        let name = format(t.next()?)?;
        written = written.max(name.len() + 1);
        for (d, s) in stack.iter_mut().zip(name.iter().chain([&0])) {
            *d = *s;
        }
        let kind = if name.len() >= 0x60 { name[0x60..].to_vec() } else { kind };
        let known = |out: &[Group], n: &[u8]| {
            BUILT_IN.iter().map(|n| n.as_bytes()).chain(out.iter().map(|g| &g.name[..])).any(|g| g.eq_ignore_ascii_case(n))
        };
        // The compares go in this order and stop at the first that matches.
        let mut adds = false;
        for k in ADDS {
            if is(&stack, &kind, k)? {
                adds = true;
                break;
            }
        }
        if adds {
            out.push(Group::stored(&name, &name));
            if kind == b"BikeGroup" {
                for _ in 0..6 {
                    t.next()?;
                }
            }
        } else if is(&stack, &kind, b"VehicleToDrivebyLink")? {
            let other = format(t.next()?)?;
            if known(&out, &name) && known(&out, &other) {
                format(t.next()?)?;
            }
        } else if kind == b"VehicleDrivebyGroupV2" || is(&stack, &kind, b"VehicleDrivebyGroup")? {
            // The name copied again with itself as the format (sprintf at 0x8224C9E8), then V2's own token over it.
            let dictionary = if kind == b"VehicleDrivebyGroupV2" { format(&name).and(format(t.next()?))? } else { format(&name)? };
            if !known(&out, &name) {
                out.push(Group::stored(&name, &dictionary));
            }
            for _ in 0..if version > 1 { 8 } else { 6 } {
                t.next()?;
            }
        }
    }
    Some(out)
}

/// A ped model's +0x7C (0x822E37C0 through 0x82248D68): the first group (built-in ones first, then `groups` from
/// 101 on) whose dictionary slot is that of the model's animation file `anims` (`null`, without case, is slot -1,
/// which every built-in group without a slot has); -1 when none is.
pub fn group_of(groups: &[Group], anims: &[u8]) -> i32 {
    let slot = (!anims.eq_ignore_ascii_case(b"null")).then(|| hash_bytes(anims, 0));
    for k in 0..BUILT_IN.len() {
        let own = BUILT_IN_SLOTS.iter().find(|(i, _)| *i == k).map(|(_, d)| hash_bytes(d.as_bytes(), 0));
        if own == slot {
            return k as i32;
        }
    }
    groups.iter().position(|g| Some(hash_bytes(&g.dictionary, 0)) == slot).map_or(-1, |i| (BUILT_IN.len() + i) as i32)
}

/// The groups Ambient.dat's loader (0x822ECA70, after animgrp.dat's in the start-up and before meleeanims.dat's) adds
/// to the store after `before` (the store's names so far, as stored), in order, all with no dictionary slot
/// (0x8224ABB0 with its stack +0x5F 0): an AMBIENT_GROUPS line's (mode 0) group when no entry yet has its name (0x82248CF0,
/// without case; 0x822ED628: its dictionary the name too), and every ANIM_GROUP_DEFINITIONS line's (mode 4,
/// 0x822ECDB0), its dictionary the line's second token. A group is a line's first token (strtok on " ,	()"; as
/// [`Group::stored`] keeps it). Lines as 0x82568D20 gives them; `#` and empty ones are passed over, a header sets the
/// mode (strncmp on its first bytes, in the loader's order: `PROP_SETS:` 2, `AMBIENT_GROUP_SETS:` 3, `AMBIENT_GROUPS:`
/// 0, `MOVEMENT_GROUPS:` 1, `ANIM_GROUP_DEFIN` 4; `SetTiming_` in any mode adds nothing). The mode starts at 0. None
/// when the game stops: a mode 4 line short of its two tokens (strcpy from strtok's null).
pub fn ambient_groups(file: &[u8], before: &[Vec<u8>]) -> Option<Vec<Group>> {
    const HEADERS: [(&[u8], Option<u8>); 6] = [(b"PROP_SETS:", Some(2)), (b"AMBIENT_GROUP_SETS:", Some(3)), (b"AMBIENT_GROUPS:", Some(0)),
                                               (b"MOVEMENT_GROUPS:", Some(1)), (b"ANIM_GROUP_DEFIN", Some(4)), (b"SetTiming_", None)];
    let mut lines = Lines { buf: [0; 512], at: 0 };
    let (mut mode, mut out) = (0, Vec::<Group>::new());
    while let Some(line) = lines.next(file) {
        if line.first().is_none_or(|&c| c == b'#') {
            continue;
        }
        if let Some(&(_, m)) = HEADERS.iter().find(|h| line.starts_with(h.0)) {
            mode = m.unwrap_or(mode);
            continue;
        }
        let mut t = tokens(&line);
        match mode {
            0 => {
                let name = t.next()?;
                if !before.iter().chain(out.iter().map(|g| &g.name)).any(|n| n.eq_ignore_ascii_case(name)) {
                    out.push(Group::stored(name, name));
                }
            }
            4 => out.push(Group::stored(t.next()?, t.next()?)),
            _ => {}
        }
    }
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The tests' image: only MovementGroup's string, at 0x41414141 ("AAAA", a pointer a name can hold).
    fn exe() -> (u32, Vec<u8>) {
        let mut b = vec![0u8; 0x100];
        b[0x41..0x41 + 13].copy_from_slice(b"MovementGroup");
        (0x4141_4100, b)
    }

    fn read(file: &[u8]) -> Option<Vec<Group>> {
        let (base, b) = exe();
        super::read(file, (base, &b))
    }

    /// A name of 32 bytes or more breaks the kind pointers past it: that kind compares with what the broken pointer
    /// points at, in the image its bytes, outside it the reader stops.
    #[test]
    fn long_names() {
        let long = |tail: &[u8]| { let mut n = vec![b'a'; 0x20]; n.extend(tail); n };
        // WeaponGroup's pointer made 0x41414141 (a MovementGroup string): a WeaponGroup line no longer adds, a
        // MovementGroup line still does.
        let mut f = b"MovementGroup(Resident, ".to_vec();
        f.extend(long(b"AAAA"));
        f.extend(b")\nWeaponGroup(Resident, w1)\nMovementGroup(Resident, m1)\n");
        let g = read(&f).unwrap();
        let names: Vec<&[u8]> = g.iter().map(|g| &g.name[..]).collect();
        assert_eq!((names.len(), names[1]), (2, &b"m1"[..]));
        // Its pointer outside the image: the game reads what is mapped there; the reader stops.
        let mut f = b"MovementGroup(Resident, ".to_vec();
        f.extend(long(b"zzzz"));
        f.extend(b")\nWeaponGroup(Resident, w1)\n");
        assert!(read(&f).is_none());
    }

    #[test]
    fn groups() {
        let file = b"Version(3)\n# x\nMovementGroup(Streaming, move_cop)\nVehicleDrivebyGroup(Streaming, veh@std, 1,2,3,4,5,6,7,8)\n\
                     VehicleDrivebyGroupV2(Streaming, veh@new, veh@new_dict, 1,2,3,4,5,6,7,8)\nWeaponGroup(Resident, gun@ak47)\n";
        let g = read(file).unwrap();
        let names: Vec<&[u8]> = g.iter().map(|g| &g.name[..]).collect();
        assert_eq!(names, [&b"move_cop"[..], b"veh@new", b"gun@ak47"]);
        assert_eq!(g[1].dictionary, b"veh@new_dict");
        assert_eq!(group_of(&g, b"null"), 0);
        assert_eq!(group_of(&g, b"MOVE_M@GENERIC"), 47);
        assert_eq!(group_of(&g, b"move_cop"), 101);
        assert_eq!(group_of(&g, b"nothing"), -1);
    }

    #[test]
    fn ambient_groups_are_its_definitions_and_new_ambient_groups() {
        // Mode 0 first: a group not in the store yet is added (its dictionary its name), one in it (without case) or
        // added before is not; SetTiming_ in mode 4 adds nothing; a mode 2 line nothing; commas and leading blanks as
        // the line reader leaves them.
        let file = b"AMB@NEW IsRaining\nmove_cop Wander\nAMB@new Flee\nANIM_GROUP_DEFINITIONS:\n# c\n\n  AMB@CAR_STD_F_DF_DS, AMB@DEFAULT, Driver_Flee_STD\n\
                     SetTiming_Wander(1.0, 0.2, 0.0)\nAMB@LEAN_DEFAULT, AMB@DEFAULT, Lean_Default, Lean_Default_B\nPROP_SETS:\nAMB@X AMB@Y\n\
                     AMBIENT_GROUPS:\nAMB@LEAN_DEFAULT Wander\nAMB@LATER Wander\n";
        let g = ambient_groups(file, &[b"MOVE_COP".to_vec()]).unwrap();
        let names: Vec<&[u8]> = g.iter().map(|g| &g.name[..]).collect();
        assert_eq!(names, [&b"AMB@NEW"[..], b"AMB@CAR_STD_F_DF_DS", b"AMB@LEAN_DEFAULT", b"AMB@LATER"]);
        assert_eq!((&g[0].dictionary[..], &g[1].dictionary[..]), (&b"AMB@NEW"[..], &b"AMB@DEFAULT"[..]));
        // A definition without its dictionary stops the game.
        assert_eq!(ambient_groups(b"ANIM_GROUP_DEFINITIONS:\nAMB@ALONE\n", &[]), None);
    }

    /// A line short of a token the game reads stops it; so does a copied token that reads an argument.
    #[test]
    fn stops() {
        assert!(read(b"MovementGroup(Resident)\n").is_none());
        assert!(read(b"Version(2)\nVehicleDrivebyGroup(Streaming, veh@std, 1,2,3,4,5,6)\n").is_none());
        assert!(read(b"Version(1)\nVehicleDrivebyGroup(Streaming, veh@std, 1,2,3,4,5,6)\n").is_some());
        assert!(read(b"BikeGroup(Resident, veh@bike, 1,2,3,4,5)\n").is_none());
        assert!(read(b"VehicleToDrivebyLink(Resident, default, swim)\n").is_none());
        assert!(read(b"VehicleToDrivebyLink(Resident, default, nothing)\n").is_some());
        assert!(read(b"MovementGroup(Resident, k%d)\n").is_none());
        let g = read(b"VehicleDrivebyGroup(Streaming, k%h%, 1,2,3,4,5,6)\n").unwrap();
        assert_eq!((&g[0].name[..], &g[0].dictionary[..]), (&b"k%"[..], &b"k"[..]));
    }

    /// `sprintf` with the token as its format, as the exe's (animgrp.py's probes).
    #[test]
    fn formats() {
        for (t, want) in [("a%%b", "a%b"), ("c%", "c"), ("d%y", "dy"), ("e%5", "e"), ("%-05.3y", "y"), ("%3.2.y", ".y"),
                          ("%5-y", "-y"), ("%Iy", "Iy"), ("%I64y", "y"), ("%I6.y", "I6.y"), ("%h%", "%"), ("%5*y", "*y"),
                          ("%Ty", "Ty"), ("%Fy", "y"), ("%.h5y", "5y"), ("%05#y", "#y")] {
            assert_eq!(format(t.as_bytes()).as_deref(), Some(want.as_bytes()), "{t}");
        }
        for t in ["%d", "%s", "%*d", "%.*y", "%Z", "%Id"] {
            assert_eq!(format(t.as_bytes()), None, "{t}");
        }
    }
}
