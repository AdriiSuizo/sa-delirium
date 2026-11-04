//! `data/meleeanims.dat` (level file kind MELEEANIMS), as the game reads it (0x826605E8, for each such file from
//! 0x82660F18; Xbox 360 exe, TU8). Board row C1fc.
//!
//! Lines come through the same line reader as animgrp.dat (0x82568D20); those starting with `#` and empty ones are
//! skipped. A line starting with `MELEE_ANIM_GROUP:` (`strncmp`) starts a group: the next line gives its name and
//! dictionary (each cut to 31 bytes) and how it loads alone (`SoloResident`, `SoloStreamed`, `SoloBarred`) and in a
//! network game (`NetResident`, `NetStreamed`, `NetBarred`), then optionally `WeaponBased` (all `_stricmp`, tokens
//! by strtok on " ,\t()"). Each line after that is an anim: its name (31 bytes) and its entry, (place + 0x1C0,
//! 0x1008600, 3), whose flags `-UpperBody` (0x208001), `-HeadNeckAndArms` (0x8008) or `-RotatesRoot` (0x1009600)
//! replace, the last one found winning. A line starting with `END_ANIM_GROUP` ends the group: it is added to the anim
//! group store (0x8224ABB0, no dictionary slot); each anim named like one of the movement anims ([`MOVES`], the
//! exe's table at 0x82A97828 / 0x82A97518) takes that one's entry and marks the group as movement; and a group with
//! an anim named `Idle_Outro` adds a second group, its name "%s_%s" of the group's and `trans` less its last byte
//! (0x826605E8 copies one byte fewer than it allocates), with the one anim `Idle_Outro` (0x82AAE2CC, entry
//! 0x82AAE2D0). The groups then go into the melee lists by their flags (not kept here).
//!
//! A line short of the tokens it needs hands strtok's null on to a read through it (the game faults there): `read`
//! gives nothing then. Only the header's last token (`WeaponBased`) is tested for null.

use crate::animgrp::{tokens, Lines};

/// The movement anims (the exe's names at 0x82A97828, entries at 0x82A97518): name and (id, flags, 0).
pub const MOVES: [(&str, [u32; 3]); 65] = [
    ("Idle", [0xF, 0x40000020, 0x0]), ("Turn_360_L", [0x10, 0x1011620, 0x0]),
    ("Turn_360_R", [0x11, 0x1011620, 0x0]), ("WStart", [0x12, 0x1004600, 0x0]),
    ("WStart_Turn_L90", [0x13, 0x1005600, 0x0]), ("WStart_Turn_R90", [0x14, 0x1005600, 0x0]),
    ("WStart_Turn_L180", [0x15, 0x1005600, 0x0]), ("WStart_Turn_R180", [0x16, 0x1005600, 0x0]),
    ("RunStart_Fwd", [0x17, 0x1004600, 0x0]), ("RunStart_L_90", [0x18, 0x1005600, 0x0]),
    ("RunStart_R_90", [0x19, 0x1005600, 0x0]), ("RunStart_L_180", [0x1A, 0x1005600, 0x0]),
    ("RunStart_R_180", [0x1B, 0x1005600, 0x0]), ("Walk", [0x1C, 0x1008720, 0x0]),
    ("Walk_B", [0x1D, 0x1008720, 0x0]), ("Walk_C", [0x1E, 0x1008720, 0x0]), ("Walk_D", [0x1F, 0x1008720, 0x0]),
    ("Walk_Turn_L", [0x20, 0x1009720, 0x0]), ("Walk_Turn_R", [0x21, 0x1009720, 0x0]),
    ("Walk_Turn_L2", [0x22, 0x1009720, 0x0]), ("Walk_Turn_R2", [0x23, 0x1009720, 0x0]),
    ("Walk_Turn_L3", [0x24, 0x1009720, 0x0]), ("Walk_Turn_R3", [0x25, 0x1009720, 0x0]),
    ("WStop_L", [0x26, 0x1008600, 0x0]), ("WStop_R", [0x27, 0x1008600, 0x0]), ("Run", [0x28, 0x1008720, 0x0]),
    ("Run_B", [0x29, 0x1008720, 0x0]), ("Run_C", [0x2A, 0x1008720, 0x0]), ("Run_D", [0x2B, 0x1008720, 0x0]),
    ("Run_Turn_L", [0x2C, 0x1009720, 0x0]), ("Run_Turn_R", [0x2D, 0x1009720, 0x0]),
    ("Run_Turn_L2", [0x2E, 0x1009720, 0x0]), ("Run_Turn_R2", [0x2F, 0x1009720, 0x0]),
    ("RStop_L", [0x30, 0x1008600, 0x0]), ("RStop_R", [0x31, 0x1008600, 0x0]), ("Sprint", [0x32, 0x1008720, 0x0]),
    ("Sprint_Turn_L", [0x33, 0x1009720, 0x0]), ("Sprint_Turn_R", [0x34, 0x1009720, 0x0]),
    ("SStop_L", [0x35, 0x1008600, 0x0]), ("SStop_R", [0x36, 0x1008600, 0x0]),
    ("Walk_Turn_180_L", [0x37, 0x1019600, 0x0]), ("Walk_Turn_180_R", [0x38, 0x1019600, 0x0]),
    ("Run_Turn_180_L", [0x39, 0x1019600, 0x0]), ("Run_Turn_180_R", [0x3A, 0x1019600, 0x0]),
    ("Sprint_Turn_180_L", [0x3B, 0x1019600, 0x0]), ("Sprint_Turn_180_R", [0x3C, 0x1019600, 0x0]),
    ("Walk_Strafe_L", [0x3D, 0x1008720, 0x0]), ("Walk_Strafe_R", [0x3E, 0x1008720, 0x0]),
    ("Walk_Strafe_B", [0x3F, 0x1008720, 0x0]), ("Run_Strafe_L", [0x40, 0x1008720, 0x0]),
    ("Run_Strafe_R", [0x41, 0x1008720, 0x0]), ("Run_Strafe_B", [0x42, 0x1008520, 0x0]),
    ("Walk_Strafe_L45", [0x43, 0x1008720, 0x0]), ("Walk_Strafe_R45", [0x44, 0x1008720, 0x0]),
    ("Walk_Strafe_BL45", [0x45, 0x1008720, 0x0]), ("Walk_Strafe_BR45", [0x46, 0x1008720, 0x0]),
    ("Run_Strafe_L45", [0x47, 0x1008720, 0x0]), ("Run_Strafe_R45", [0x48, 0x1008720, 0x0]),
    ("Run_Strafe_BL45", [0x49, 0x1008720, 0x0]), ("Run_Strafe_BR45", [0x4A, 0x1008720, 0x0]),
    ("Shuffle_Stop", [0x4B, 0x4000, 0x0]), ("Walk_Up", [0x4C, 0x1008720, 0x0]),
    ("Walk_Down", [0x4D, 0x1008720, 0x0]), ("Run_Up", [0x4E, 0x1008720, 0x0]), ("Run_Down", [0x4F, 0x1008720, 0x0]),
];

/// The entry of the `trans` group's one anim, `Idle_Outro` (0x82AAE2D0).
pub const TRANS_ENTRY: [u32; 3] = [0x52, 0xC000, 3];

/// How a group loads; `Unset` when its line names none of the three.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Load {
    #[default]
    Unset,
    Resident,
    Streamed,
    Barred,
}

/// An anim of a group: its name and its entry (id, flags, 3, or a movement anim's).
#[derive(Clone, Debug, PartialEq)]
pub struct Anim {
    pub name: Vec<u8>,
    pub entry: [u32; 3],
}

/// A group as `END_ANIM_GROUP` leaves it.
#[derive(Clone, Debug, PartialEq)]
pub struct MeleeGroup {
    pub name: Vec<u8>,
    pub dictionary: Vec<u8>,
    pub solo: Load,
    pub net: Load,
    pub weapon_based: bool,
    pub anims: Vec<Anim>,
    /// One of its anims is a movement anim.
    pub movement: bool,
    /// It has an `Idle_Outro`: the `trans` group follows it in the store.
    pub trans: bool,
}

impl MeleeGroup {
    /// The name of the `trans` group (see the module notes).
    pub fn trans_name(&self) -> Vec<u8> {
        let mut n = [&self.name[..], b"_trans"].concat();
        n.truncate(254);
        n.pop();
        n
    }
}

fn cut31(t: &[u8]) -> Vec<u8> {
    t[..t.len().min(31)].to_vec()
}

/// 0x826605E8 over `file`: the groups in the order `END_ANIM_GROUP` adds them; None where the game reads through a
/// null token.
pub fn read(file: &[u8]) -> Option<Vec<MeleeGroup>> {
    let mut lines = Lines { buf: [0; 512], at: 0 };
    let mut out = vec![];
    // The reader's locals, kept from line to line.
    let (mut header, mut in_anims) = (false, false);
    let (mut name, mut dictionary) = (vec![], vec![]);
    let (mut solo, mut net, mut weapon_based) = (Load::Unset, Load::Unset, false);
    let mut anims: Vec<Anim> = vec![];
    let is = |t: &[u8], s: &str| t.eq_ignore_ascii_case(s.as_bytes());
    while let Some(line) = lines.next(file) {
        if matches!(line.first(), None | Some(b'#')) {
            continue;
        }
        if line.starts_with(b"MELEE_ANIM_GROUP:") {
            header = true;
            in_anims = false;
            solo = Load::Unset;
            net = Load::Unset;
            weapon_based = false;
            anims.clear();
        } else if line.starts_with(b"END_ANIM_GROUP") {
            header = false;
            in_anims = false;
            let mut g = MeleeGroup {
                name: name.clone(),
                dictionary: dictionary.clone(),
                solo,
                net,
                weapon_based,
                anims: std::mem::take(&mut anims),
                movement: false,
                trans: false,
            };
            for a in &mut g.anims {
                for (m, entry) in MOVES {
                    if is(&a.name, m) {
                        g.movement = true;
                        a.entry = entry;
                    }
                }
                if is(&a.name, "Idle_Outro") {
                    g.trans = true;
                }
            }
            out.push(g);
        } else if header {
            let mut t = tokens(&line);
            name = cut31(t.next()?);
            dictionary = cut31(t.next()?);
            let s = t.next()?;
            if is(s, "SoloResident") {
                solo = Load::Resident;
            } else if is(s, "SoloStreamed") {
                solo = Load::Streamed;
            } else if is(s, "SoloBarred") {
                solo = Load::Barred;
            }
            in_anims = true;
            let n = t.next()?;
            if is(n, "NetResident") {
                net = Load::Resident;
            } else if is(n, "NetStreamed") {
                net = Load::Streamed;
            } else if is(n, "NetBarred") {
                net = Load::Barred;
            }
            if t.next().is_some_and(|w| is(w, "WeaponBased")) {
                weapon_based = true;
            }
            header = false;
        } else if in_anims {
            let mut t = tokens(&line);
            let name = cut31(t.next()?);
            let mut a = Anim { name, entry: [anims.len() as u32 + 0x1C0, 0x1008600, 3] };
            for w in t {
                if is(w, "-UpperBody") {
                    a.entry[1] = 0x208001;
                }
                if is(w, "-HeadNeckAndArms") {
                    a.entry[1] = 0x8008;
                }
                if is(w, "-RotatesRoot") {
                    a.entry[1] = 0x1009600;
                }
            }
            anims.push(a);
        }
    }
    Some(out)
}

/// The anim groups `groups` adds to the store, in order: each group, then its `trans` group when it has one (as the
/// store entries hold them, `Group::stored`; none has a dictionary slot).
pub fn store_groups(groups: &[MeleeGroup]) -> Vec<crate::animgrp::Group> {
    let mut out = vec![];
    for g in groups {
        out.push(crate::animgrp::Group::stored(&g.name, &g.dictionary));
        if g.trans {
            out.push(crate::animgrp::Group::stored(&g.trans_name(), &g.dictionary));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_group() {
        let file = b"# c\nMELEE_ANIM_GROUP:\nmove_full_set move_melee SoloResident NetResident\n\tIdle\n\tIdle_Outro\n\
                     \tpunch -UpperBody\nEND_ANIM_GROUP\n";
        let g = read(file).unwrap();
        assert_eq!(g.len(), 1);
        assert_eq!((g[0].solo, g[0].net, g[0].movement, g[0].trans), (Load::Resident, Load::Resident, true, true));
        assert_eq!(g[0].anims[0].entry, MOVES[0].1);
        assert_eq!(g[0].anims[2].entry, [0x1C2, 0x208001, 3]);
        assert_eq!(g[0].trans_name(), b"move_full_set_tran");
        assert_eq!(store_groups(&g).len(), 2);
    }

    /// A header short of its load tokens, or an anim line of brackets only (commas and spaces are blanked, so a line
    /// of those is empty and skipped): the game reads through strtok's null.
    #[test]
    fn short_line_faults() {
        assert!(read(b"MELEE_ANIM_GROUP:\nmove_full_set move_melee SoloResident\nEND_ANIM_GROUP\n").is_none());
        assert!(read(b"MELEE_ANIM_GROUP:\nmove_full_set move_melee SoloResident NetResident\n () \nEND_ANIM_GROUP\n").is_none());
    }
}
