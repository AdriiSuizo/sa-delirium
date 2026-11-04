//! The weapon info loader (Xex TU8; board WP10f): the code that fills one 272-byte CWeaponInfo row from one weapon's
//! element of weaponinfo.xml. Read in `docs/weapons/weaponinfoload.md`.
//!
//! The rows are at 0x82CB8AB0 + type x 272 (0x82299AD0 gives the row of a type). The loader itself is 0x8229ADE0, which
//! reads the element's own attributes and then walks its children, each by the hash of its tag name (0x8284E060), into
//! the sub-handlers below: the aiming block (0x8229A7A8), the projectile (0x8229A938), the flags (0x8229ABC8) and the
//! controller's rumble (0x8229AB30). Each name table is looked up as the exe does (see the doc comment per function).
//!
//! The boundary: the attribute lookup (0x82850CA0), the text-to-number conversions (0x82850F90 int, 0x82850FE0 float),
//! the flag's `sscanf("%s")` (0x829FFC90) and the hex text (0x82A02FE8, strtoul base 16) are taken as the parser's job,
//! and what is kept here is the text itself: [`Element`] holds its attributes as text, and [`int_text`], [`float_text`],
//! [`token`], [`hex_text`] stand for those conversions. The checks feed the exe the same text.
//!
//! Checked bit for bit against the exe by `tools/xbox360-re/checks/weaponinfoload.py` on a random row (the bytes the
//! loader does not write are kept, so the flags' `|=` is tested too).

use crate::map::hash_bytes;
use crate::weapons;

/// One CWeaponInfo row (272 bytes, big-endian words as the exe keeps them).
pub type Row = [u8; ROW];
pub const ROW: usize = 272;

/// An element of weaponinfo.xml as the loader sees it: the tag, the parser's node kind (+0x2C; a `<flag>` child counts
/// only when it is not 0), the text (+0x20; `None` when the text is empty, +0x24 0), the attributes in file order and the
/// children in file order (+0x1C, the next sibling +0x18).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Element {
    pub name: String,
    pub kind: u8,
    pub text: Option<String>,
    pub attrs: Vec<(String, String)>,
    pub children: Vec<Element>,
}

/// 0x8284E060 (the tag and name hash, seed 0), as `map::hash_bytes`.
fn hash(name: &str) -> u32 {
    hash_bytes(name.as_bytes(), 0)
}

// The tag hashes the dispatches compare with (each is the hash of its tag). The three marked "not in the image" are
// compared by hash only: their names are not among the strings in the TU8 image, so the names are not known here.
const RELOAD: u32 = 0x5da46b68; // reload
const DAMAGE: u32 = 0x431375e1; // damage
const AIMING: u32 = 0x09a5b381; // aiming
const PROJECTILE: u32 = 0x4ace140a; // projectile
const FLAGS: u32 = 0x67d26872; // flags
const PICKUP: u32 = 0xbc9403c8; // pickup
const PHYSICS: u32 = 0x22b6caa7; // physics
const CONTROLLER: u32 = 0xcebcfd68; // controller
const OFFSET: u32 = 0xa13c79ef; // offset
const CROUCHED_OFFSET: u32 = 0x04bf038a; // crouchedoffset
const AIM_PITCH: u32 = 0x9446c004; // a child of aiming with `pitch` (name not in the image)
const AIM_STANCE: u32 = 0xcb57b1ff; // a child of aiming with standing / ducked / scale (name not in the image)
const TYPE_TO_CREATE: u32 = 0x82527040; // typetocreate
const EXPLOSION: u32 = 0xdd91088f; // explosion
const PROJECTILE_VECTOR: u32 = 0x36f9e805; // `rotoffset` (its hash; the name is not in the image), a vector at +0xC0

/// 0x82A993E8: the weapon types (58 names; the hashes in 0x82CBCA70 are made from them by 0x82A5C690).
pub const WEAPON_TYPES: [&str; 58] = [
    "UNARMED", "BASEBALLBAT", "POOLCUE", "KNIFE", "GRENADE", "MOLOTOV", "ROCKET", "PISTOL", "UNUSED0", "DEAGLE", "SHOTGUN",
    "BARETTA", "MICRO_UZI", "MP5", "AK47", "M4", "SNIPERRIFLE", "M40A1", "RLAUNCHER", "FTHROWER", "MINIGUN", "EPISODIC_1",
    "EPISODIC_2", "EPISODIC_3", "EPISODIC_4", "EPISODIC_5", "EPISODIC_6", "EPISODIC_7", "EPISODIC_8", "EPISODIC_9",
    "EPISODIC_10", "EPISODIC_11", "EPISODIC_12", "EPISODIC_13", "EPISODIC_14", "EPISODIC_15", "EPISODIC_16", "EPISODIC_17",
    "EPISODIC_18", "EPISODIC_19", "EPISODIC_20", "EPISODIC_21", "EPISODIC_22", "EPISODIC_23", "EPISODIC_24", "CAMERA", "OBJECT",
    "WEAPONTYPE_LAST_WEAPONTYPE", "ARMOUR", "RAMMEDBYCAR", "RUNOVERBYCAR", "EXPLOSION", "UZI_DRIVEBY", "DROWNING", "FALL",
    "UNIDENTIFIED", "ANYMELEE", "ANYWEAPON",
];
/// 0x82A99508: the fire types (`<data firetype>`).
pub const FIRE_TYPES: [&str; 8] = ["MELEE", "INSTANT_HIT", "DELAYED_HIT", "PROJECTILE", "AREA_EFFECT", "CAMERA", "USE", "UNKNOWN"];
/// 0x82A9954C: the projectile types (`<projectile type>`).
pub const PROJECTILE_TYPES: [&str; 7] = ["NONE", "FUSE", "IMPACT", "ROCKET", "STICKY", "UNKNOWN", ""];
/// 0x82A995B0: the explosion types (`<explosion type>`).
pub const EXPLOSIONS: [&str; 26] = [
    "GRENADE", "MOLOTOV", "ROCKET", "HI_OCTANE", "CAR", "PLANE", "PETROL_PUMP", "BIKE", "DIR_STEAM", "DIR_FLAME",
    "DIR_WATER_HYDRANT", "DIR_GAS_CANISTER", "BOAT", "SHIP_DESTROY", "TRUCK", "EPISODIC_1", "EPISODIC_2", "EPISODIC_3",
    "EPISODIC_4", "EPISODIC_5", "EPISODIC_6", "EPISODIC_7", "EPISODIC_8", "EPISODIC_9", "EPISODIC_10", "UNKNOWN",
];

/// The attribute's text, the first with the name (case ignored; 0x82850CA0), `None` when absent.
fn attr<'a>(el: &'a Element, name: &str) -> Option<&'a str> {
    el.attrs.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).map(|(_, v)| v.as_str())
}

/// The attribute's text, "" when absent (the loader's `r29` default string, 0x820B92FC).
fn attr_or_empty<'a>(el: &'a Element, name: &str) -> &'a str {
    attr(el, name).unwrap_or("")
}

/// Boundary: an attribute's text as an integer (0x82855F30). Decimal text only in the checks.
pub fn int_text(s: &str) -> i32 {
    s.trim().parse().unwrap_or(0)
}

/// Boundary: an attribute's text as a single float (0x82855F98). Decimal text only in the checks.
pub fn float_text(s: &str) -> f32 {
    s.trim().parse().unwrap_or(0.0)
}

/// Boundary: the first whitespace-separated word of a text (the `"%s"` scan, 0x829FFC90); "" for no text.
pub fn token(text: Option<&str>) -> &str {
    text.and_then(|t| t.split_whitespace().next()).unwrap_or("")
}

/// Boundary: a hex word (0x82A02FE8 is strtoul with base 16): the leading hex digits, 0 for none, saturated to 32 bits.
pub fn hex_text(s: &str) -> u32 {
    let digits: String = s.chars().take_while(|c| c.is_ascii_hexdigit()).collect();
    if digits.is_empty() {
        0
    } else {
        u32::from_str_radix(&digits, 16).unwrap_or(u32::MAX)
    }
}

/// `int` attribute with the 0x82850F90 default: the default when the attribute is absent.
fn int_attr(el: &Element, name: &str, default: i32) -> i32 {
    attr(el, name).map_or(default, int_text)
}

/// `float` attribute with the 0x82850FE0 default.
fn float_attr(el: &Element, name: &str, default: f32) -> f32 {
    attr(el, name).map_or(default, float_text)
}

fn put32(row: &mut Row, at: usize, v: u32) {
    row[at..at + 4].copy_from_slice(&v.to_be_bytes());
}

fn put16(row: &mut Row, at: usize, v: u16) {
    row[at..at + 2].copy_from_slice(&v.to_be_bytes());
}

fn putf(row: &mut Row, at: usize, v: f32) {
    put32(row, at, v.to_bits());
}

fn get32(row: &Row, at: usize) -> u32 {
    u32::from_be_bytes([row[at], row[at + 1], row[at + 2], row[at + 3]])
}

/// 0x82299A48: a vector's three floats, `x`, `y`, `z` (0 each when absent), into the row at `at`.
fn vector(el: &Element, row: &mut Row, at: usize) {
    for (i, k) in ["x", "y", "z"].iter().enumerate() {
        putf(row, at + 4 * i, float_attr(el, k, 0.0));
    }
}

/// 0x82299BD0: the fire type's index. Empty or unknown is 1 (INSTANT_HIT).
pub fn fire_type_index(name: &str) -> i32 {
    if name.is_empty() {
        return 1;
    }
    FIRE_TYPES.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(1, |i| i as i32)
}

/// 0x82299E10: the projectile type's index. Empty or unknown is 5 (UNKNOWN).
pub fn projectile_type_index(name: &str) -> i32 {
    if name.is_empty() {
        return 5;
    }
    PROJECTILE_TYPES.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(5, |i| i as i32)
}

/// 0x82299EA0: the explosion's index. Empty or unknown is -1.
pub fn explosion_index(name: &str) -> i32 {
    if name.is_empty() {
        return -1;
    }
    EXPLOSIONS.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(-1, |i| i as i32)
}

/// 0x8229A6F8: a weapon type's index by the hash of its name (0x82CBCA70 holds the hashes of [`WEAPON_TYPES`]); `default`
/// when the name is empty or not one of them.
pub fn weapon_type_index(name: &str, default: i32) -> i32 {
    if name.is_empty() {
        return default;
    }
    let h = hash(name);
    WEAPON_TYPES.iter().position(|t| hash(t) == h).map_or(default, |i| i as i32)
}

/// 0x8229A7A8: the `<aiming>` block. `accuracy` and `accuracyfps` (+0x34, +0x38) default to 1, `accuracyTime` (+0xF8) and
/// `pellets` (+0xFC) to 0; the children: `offset` (+0x40), `crouchedoffset` (+0x50), `pitch` (+0x60), and the stance
/// child (+0x64 standing, +0x68 ducked, +0x6C scale).
pub fn aiming(el: &Element, row: &mut Row) {
    putf(row, 0x34, float_attr(el, "accuracy", 1.0));
    putf(row, 0x38, float_attr(el, "accuracyfps", 1.0));
    putf(row, 0xF8, float_attr(el, "accuracyTime", 0.0));
    put32(row, 0xFC, int_attr(el, "pellets", 0) as u32);
    for c in &el.children {
        match hash(&c.name) {
            OFFSET => vector(c, row, 0x40),
            CROUCHED_OFFSET => vector(c, row, 0x50),
            AIM_PITCH => putf(row, 0x60, float_attr(c, "pitch", 0.0)),
            AIM_STANCE => {
                putf(row, 0x64, float_attr(c, "standing", 0.0));
                putf(row, 0x68, float_attr(c, "ducked", 0.0));
                putf(row, 0x6C, float_attr(c, "scale", 0.0));
            }
            _ => {}
        }
    }
}

/// 0x8229A938: the `<projectile>` block, only for a PROJECTILE fire type (row +8 is 3; else nothing is written). `type`
/// (+0xA0, [`projectile_type_index`]), `fusetime` (+0xA4). Children: `typetocreate` (+0xA8, [`weapon_type_index`] with
/// default 0), `offset` (+0xB0), `explosion` (+0xAC), `physics` (+0x18 force, +0xD0 explodeimpactthreshold, +0xD4
/// explodeimpactwithvehiclethreshold, +0xD8 vehiclevelocity; the force overwrites the weapon's range at +0x18) and the
/// vector child (+0xC0).
pub fn projectile(el: &Element, row: &mut Row) {
    if get32(row, 8) != 3 {
        return;
    }
    put32(row, 0xA0, projectile_type_index(attr_or_empty(el, "type")) as u32);
    put32(row, 0xA4, int_attr(el, "fusetime", 0) as u32);
    for c in &el.children {
        match hash(&c.name) {
            TYPE_TO_CREATE => put32(row, 0xA8, weapon_type_index(attr_or_empty(c, "type"), 0) as u32),
            OFFSET => vector(c, row, 0xB0),
            EXPLOSION => put32(row, 0xAC, explosion_index(attr_or_empty(c, "type")) as u32),
            PHYSICS => {
                putf(row, 0x18, float_attr(c, "force", 0.0));
                putf(row, 0xD0, float_attr(c, "explodeimpactthreshold", 0.0));
                putf(row, 0xD4, float_attr(c, "explodeimpactwithvehiclethreshold", 0.0));
                putf(row, 0xD8, float_attr(c, "vehiclevelocity", 0.0));
            }
            PROJECTILE_VECTOR => vector(c, row, 0xC0),
            _ => {}
        }
    }
}

/// 0x8229ABC8: the `<flags>` block. A child `flag` (with a kind other than 0) ORs its bit into the flags word (+0x20;
/// `flag_bit` gives -1 for an unknown name, which adds nothing); a child `flags` (hex text) replaces the whole word.
pub fn flags(el: &Element, row: &mut Row) {
    for c in &el.children {
        if c.kind == 0 {
            continue;
        }
        if c.name.eq_ignore_ascii_case("flag") {
            let bit = weapons::flag_bit(token(c.text.as_deref()));
            let word = get32(row, 0x20);
            let add = if (0..32).contains(&bit) { 1u32 << bit } else { 0 };
            put32(row, 0x20, word | add);
        } else if c.name.eq_ignore_ascii_case("flags") {
            put32(row, 0x20, hex_text(token(c.text.as_deref())));
        }
    }
}

/// 0x8229AB30: the `<controller>` block: each `rumble` child sets the duration (int, +0x70) and the intensity (float,
/// +0x74); the last one wins.
pub fn controller(el: &Element, row: &mut Row) {
    for c in &el.children {
        if c.name.eq_ignore_ascii_case("rumble") {
            put32(row, 0x70, int_attr(c, "duration", 0) as u32);
            putf(row, 0x74, float_attr(c, "intensity", 0.0));
        }
    }
}

/// 0x8229ADE0: fills `row` from the weapon's element. The element's own attributes first (`slot` +4, `firetype` +8,
/// `damagetype` +0xC, `group` +0x10, `targetrange` +0x14, `weaponrange` +0x18, `stattype` +0x1C, `clipsize` +0x86 (u16),
/// `ammomax` +0x88 (i16, the low 16 bits sign-extended, then 0..=25000; 25000 when absent), `timebetweenshots` +0x8C
/// (int)), then its children by tag: `reload` (+0x94 time, +0x98 fasttime, +0x9C crouchtime; ints), `damage` (+0x82 base
/// u16, +0x84 fps u16, +0xF0 networkplayermod, +0xF4 networkpedmod), `aiming`, `projectile`, `flags`, `pickup` (+0x78
/// regentime int, +0x80 ammoonstreet u16), `physics` (+0x90 force), `controller`. Other tags are skipped.
pub fn load(el: &Element, row: &mut Row) {
    put32(row, 4, weapons::slot_index(attr_or_empty(el, "slot")) as u32);
    put32(row, 8, fire_type_index(attr_or_empty(el, "firetype")) as u32);
    put32(row, 0xC, weapons::damage_type_index(attr_or_empty(el, "damagetype")) as u32);
    put32(row, 0x10, weapons::group_index(attr_or_empty(el, "group")) as u32);
    putf(row, 0x14, float_attr(el, "targetrange", 0.0));
    putf(row, 0x18, float_attr(el, "weaponrange", 0.0));
    put32(row, 0x1C, weapon_type_index(attr_or_empty(el, "stattype"), -1) as u32);
    put16(row, 0x86, int_attr(el, "clipsize", 0) as u16);
    // `extsh` first: the low 16 bits, sign-extended, then clamped.
    let ammo = ((int_attr(el, "ammomax", 25000) as i16) as i32).clamp(0, 25000);
    put16(row, 0x88, ammo as u16);
    put32(row, 0x8C, int_attr(el, "timebetweenshots", 0) as u32);
    for c in &el.children {
        match hash(&c.name) {
            RELOAD => {
                put32(row, 0x94, int_attr(c, "time", 0) as u32);
                put32(row, 0x98, int_attr(c, "fasttime", 0) as u32);
                put32(row, 0x9C, int_attr(c, "crouchtime", 0) as u32);
            }
            DAMAGE => {
                put16(row, 0x82, int_attr(c, "base", 0) as u16);
                put16(row, 0x84, int_attr(c, "fps", 0) as u16);
                putf(row, 0xF0, float_attr(c, "networkplayermod", 0.0));
                putf(row, 0xF4, float_attr(c, "networkpedmod", 0.0));
            }
            AIMING => aiming(c, row),
            PROJECTILE => projectile(c, row),
            FLAGS => flags(c, row),
            PICKUP => {
                put32(row, 0x78, int_attr(c, "regentime", 0) as u32);
                put16(row, 0x80, int_attr(c, "ammoonstreet", 0) as u16);
            }
            PHYSICS => putf(row, 0x90, float_attr(c, "force", 0.0)),
            CONTROLLER => controller(c, row),
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tag_hashes_are_the_named_ones() {
        for (name, h) in [("reload", RELOAD), ("damage", DAMAGE), ("aiming", AIMING), ("projectile", PROJECTILE),
                          ("flags", FLAGS), ("pickup", PICKUP), ("physics", PHYSICS), ("controller", CONTROLLER),
                          ("offset", OFFSET), ("crouchedoffset", CROUCHED_OFFSET), ("typetocreate", TYPE_TO_CREATE),
                          ("explosion", EXPLOSION)] {
            assert_eq!(hash(name), h, "{name}");
        }
    }

    #[test]
    fn ammo_is_clamped_after_sign_extension() {
        let mut row = [0u8; ROW];
        let el = Element { attrs: vec![("ammomax".into(), "40000".into())], ..Default::default() };
        load(&el, &mut row);
        assert_eq!(&row[0x88..0x8A], &0u16.to_be_bytes(), "40000 is -25536 in 16 bits: 0");
        let el = Element { attrs: vec![("ammomax".into(), "30000".into())], ..Default::default() };
        load(&el, &mut row);
        assert_eq!(&row[0x88..0x8A], &25000u16.to_be_bytes());
    }

    #[test]
    fn flag_or_keeps_the_word_and_unknown_names_add_nothing() {
        let mut row = [0u8; ROW];
        row[0x20..0x24].copy_from_slice(&0x100u32.to_be_bytes());
        let flag = |t: &str| Element { name: "flag".into(), kind: 1, text: Some(t.into()), ..Default::default() };
        let el = Element { name: "flags".into(), children: vec![flag("GUN"), flag("NOPE")], ..Default::default() };
        flags(&el, &mut row);
        assert_eq!(get32(&row, 0x20), 0x100 | (1 << 5));
    }
}
