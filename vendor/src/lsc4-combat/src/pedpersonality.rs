//! `data/pedpersonality.dat` (level file kind PEDPERSONALITY), as the game reads it into the ped model infos
//! (0x822E3888; Xbox 360 exe, TU8 addresses). Board row PP1g4.
//!
//! Each line not starting with `#` (through the line reader 0x821B52F0, commas made spaces) is one sscanf of
//! "%s %s %d %d %d %f %f %d %f %s %s %s %s %s %s %s %s %s %s %s": the model, sex, age, sexiness, bravery, attack,
//! defend, agility, melee martial level percent, first and second language, seven melee and animation group names,
//! the move group, and the tickboxes (`+word+word...`). A model is found by its name's hash (0x821CA340: 0x8235C370
//! then 0x821CA138) and must be a ped model (its type, vfunc +0x0C, is 6); other lines are skipped.
//!
//! The scan's buffers live on the reader's stack for the whole file: a short line leaves the fields it doesn't
//! reach as the line before left them, except the two languages and the tickboxes, whose first byte is cleared
//! before each line. ponytail: before the first line ours are 0 and empty (the game's are what its stack held).

use std::collections::HashMap;

/// The flag bits (CPedModelInfo +0x120) the tickboxes set, each word matched exactly (case counts).
pub const TICKBOXES: [(&str, u64); 44] = [
    ("bowling", 0x4), ("pool", 0x8), ("basketball", 0x10), ("drivespoorcar", 0x20), ("drivesaveragecar", 0x40),
    ("drivesrichcar", 0x80), ("drivesbigcar", 0x100), ("drivesmotorcycle", 0x200), ("drivesboat", 0x400),
    ("onfoot", 0x800), ("argue", 0x1000), ("smoker", 0x2000), ("phoneuser", 0x4000), ("buydrugs", 0x8000),
    ("givebummoney", 0x10000), ("insultbum", 0x20000), ("replytoho", 0x40000), ("taketaxi", 0x80000),
    ("watchfight", 0x100000), ("late", 0x200000), ("softjack", 0x2_0000_0000), ("murdershakefist", 0x400000),
    ("murdercallpolice", 0x800000), ("murderrunaway", 0x1000000), ("murderattack", 0x2000000),
    ("taxidriver", 0x20_0000_0000), ("policeofficer", 0x4000000), ("swat", 0x8000000), ("fbi", 0x10000000),
    ("dea", 0x20000000), ("csi", 0x40000000), ("paramedic", 0x80000000), ("fireman", 0x1_0000_0000),
    ("prostitute", 0x4_0000_0000), ("pimp", 0x8_0000_0000), ("bum", 0x10_0000_0000),
    ("cancarryweapons", 0x40_0000_0000), ("likesart", 0x80_0000_0000), ("dislikesart", 0x100_0000_0000),
    ("tourist", 0x200_0000_0000), ("office", 0x400_0000_0000), ("gardening", 0x800_0000_0000),
    ("jewish", 0x1000_0000_0000), ("criminal", 0x2000_0000_0000),
];
/// Set for every model with a line.
pub const HAS_PERSONALITY: u64 = 0x1;
/// Set when the sex starts with `m` or `M`, cleared otherwise (the model info starts with it set).
pub const MALE: u64 = 0x2;
/// Set when the model is in one of the ped groups 30 to 41 (0x822A39B0: pedgrp.dat's lists at 0x82CF09B8).
pub const IN_GROUPS_30_41: u64 = 0x8000_0000_0000;
/// Set on the player's model once the file is read (0x822E5100, the model at 82A9A57C).
pub const PLAYER: u64 = 0x4000_0000_0000;

/// What a ped model info holds from the file (+0xEC to +0x120). The defaults are the model info's setup's
/// (CPedModelInfo::vf1 0x822E3698), which leaves the eight names alone.
#[derive(Clone, Debug, PartialEq)]
pub struct Personality {
    /// +0x120.
    pub flags: u64,
    /// +0xEC, +0xED, +0xEE, +0xEF: the low bytes of age, sexiness, bravery and agility.
    pub age: u8,
    pub sexiness: u8,
    pub bravery: u8,
    pub agility: u8,
    /// +0xF0, +0xF1: each language's first letter, `C` 3, `R` 2, `S` 1, anything else 0.
    pub languages: [u8; 2],
    /// +0xF4, +0xF8, +0xFC.
    pub attack: f32,
    pub defend: f32,
    pub melee_percent: f32,
    /// +0x100 to +0x118: the seven names as the name store 82BF9AA4 knows them (0x82248CF0), and +0x11C the move
    /// group's (handed to 0x82295888 unless it is 0x3A). Ours keeps the names.
    pub groups: [Vec<u8>; 7],
    pub move_group: Vec<u8>,
}

impl Default for Personality {
    fn default() -> Self {
        Personality {
            flags: MALE,
            age: 30,
            sexiness: 2,
            bravery: 2,
            agility: 3,
            languages: [0; 2],
            attack: 0.8,
            defend: 1.3,
            melee_percent: 50.0,
            groups: Default::default(),
            move_group: vec![],
        }
    }
}

/// The reader's stack buffers, kept from line to line.
#[derive(Default)]
struct Fields {
    name: Vec<u8>,
    sex: Vec<u8>,
    ints: [i32; 4],
    floats: [f32; 3],
    lang: [Vec<u8>; 2],
    groups: [Vec<u8>; 7],
    move_group: Vec<u8>,
    ticks: Vec<u8>,
}

fn language(s: &[u8]) -> u8 {
    match s.first() {
        Some(b'C') => 3,
        Some(b'R') => 2,
        Some(b'S') => 1,
        _ => 0,
    }
}

/// 0x822E3888 over `file`: each line's model found in `models` by its name's hash (`map::hash_bytes` from 0; the
/// ped models only) and filled in; `in_group(hash, group)` says whether that model is in a ped group (0x822A39B0).
pub fn read(file: &[u8], models: &mut HashMap<u32, Personality>, in_group: impl Fn(u32, u32) -> bool) {
    let mut f = Fields::default();
    crate::textlines::each_line(file, true, |memory| {
        if memory.first() == Some(&b'#') {
            return;
        }
        f.lang[0].clear();
        f.lang[1].clear();
        f.ticks.clear();
        let s = crate::crt::sscanf(memory, b"%s %s %d %d %d %f %f %d %f %s %s %s %s %s %s %s %s %s %s %s");
        let text = |k: usize, into: &mut Vec<u8>| {
            if let Some(b) = s.bytes(k) {
                *into = b[..b.iter().position(|&c| c == 0).unwrap_or(b.len())].to_vec();
            }
        };
        text(0, &mut f.name);
        text(1, &mut f.sex);
        for (slot, k) in [(0, 2), (1, 3), (2, 4), (3, 7)] {
            if let Some(v) = s.int(k) {
                f.ints[slot] = v;
            }
        }
        for (slot, k) in [(0, 5), (1, 6), (2, 8)] {
            if let Some(v) = s.float(k) {
                f.floats[slot] = v;
            }
        }
        text(9, &mut f.lang[0]);
        text(10, &mut f.lang[1]);
        for g in 0..7 {
            text(11 + g, &mut f.groups[g]);
        }
        text(18, &mut f.move_group);
        text(19, &mut f.ticks);
        let hash = crate::map::hash_bytes(&f.name, 0);
        let Some(p) = models.get_mut(&hash) else { return };
        p.flags |= HAS_PERSONALITY;
        if matches!(f.sex.first(), Some(b'm' | b'M')) {
            p.flags |= MALE;
        } else {
            p.flags &= !MALE;
        }
        [p.age, p.sexiness, p.bravery, p.agility] = f.ints.map(|v| v as u8);
        [p.attack, p.defend, p.melee_percent] = f.floats;
        p.groups = f.groups.clone();
        p.move_group = f.move_group.clone();
        p.languages = [language(&f.lang[0]), language(&f.lang[1])];
        // `+` separated words, each matched against every tickbox.
        for word in f.ticks.split(|&c| c == b'+').filter(|w| !w.is_empty()) {
            for (name, bit) in TICKBOXES {
                if word == name.as_bytes() {
                    p.flags |= bit;
                }
            }
        }
        for group in 30..42 {
            if in_group(hash, group) {
                p.flags |= IN_GROUPS_30_41;
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_line() {
        let mut models = HashMap::new();
        models.insert(crate::map::hash_bytes(b"m_y_cool", 0), Personality::default());
        let file = b"# header\nM_Y_Cool, F, 25, 3, 4, 1.5, 0.4, 5, 50, R, C, a, b, c, d, e, f, g, move, +smoker+bum+fbi\n";
        read(file, &mut models, |_, g| g == 33);
        let p = &models[&crate::map::hash_bytes(b"m_y_cool", 0)];
        assert_eq!(p.flags, 1 | 0x2000 | 0x10_0000_0000 | 0x10000000 | IN_GROUPS_30_41);
        assert_eq!((p.age, p.sexiness, p.bravery, p.agility, p.languages), (25, 3, 4, 5, [2, 3]));
        assert_eq!((p.attack, p.defend), (1.5, 0.4));
        assert_eq!(p.move_group, b"move");
    }
}
