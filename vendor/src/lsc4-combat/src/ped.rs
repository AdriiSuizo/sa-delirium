//! A ped's health and armour, and what a hit does to them (CPedDamage; Xbox 360 exe, TU8 addresses). Board PD1.
//!
//! Fields: health CPed +0x1E4, its max +0x9D4, armour +0x9D8. The ped's setup (0x822F9CB8, from CPed::ctor
//! 0x822FF300) gives every ped 200 health and 200 max (0x8200EF74) and no armour; the player's (0x823D2EC8) 200
//! and 200 again. The police factory (CPedFactoryNY, 0x821643A8 / 0x821646D8) gives cops 0 armour and M_Y_SWAT and
//! M_M_FBI 100. The player info holds the player's caps: max health +0x53E and max armour +0x540, 200 and 100 (u16s,
//! 0x82238F28). ADD_ARMOUR_TO_CHAR (0x825BC1A8) adds up to that cap for the player, up to 100 for others, never
//! below 0. IS_CHAR_INJURED is health below 100.
//!
//! A hit (0x824DC670, once per hit: result flag 0x80) is scaled (0x824DA600), soaked by armour (0x824DB3E0) and taken
//! off health (0x824DB650, with the instant kill 0x824DA8B8). At 100 health or more the ped's damage proofs
//! (`allowed`: 0x824DA160 and the physical's 0x82482238) may refuse it; below 100 it always lands, so a dying ped can
//! be finished off.
//!
//! The result (`Outcome`, the record the dispatcher 0x8226C9A8 reads): +4 flags (`KNOCKED_OUT` 4, `INSTANT` 8,
//! `INJURED` 0x10, `KILLED` 0x20), +8 health lost, +0xC armour absorbed. Either of 0x10 / 0x20 starts the death
//! (`Outcome::dies`): the dispatcher gives CTaskComplexDie (0x824C82B0, blend 8.0, the death clip from 0x824DBD00),
//! or the NM reaction 0x8226B618 with `direct` set when the ped is ragdolling. Board PD3 (the task system) takes the
//! death task and the damage events (0x82189D78) from here; see `docs/peds/damage.md` for what the death tasks do.
//!
//! ponytail: network-game paths (0x824DA348's multipliers, clones, weapon 30) and the player's bookkeeping (armour time
//! +0x4E0, the 150 s timer +0x550) are not ported; the caller works out `friend`, `same_group` and the victim's and
//! attacker's flags (`Hit`, `Victim`). In `allowed`: an attacker owned by the victim (+0x1BC) and the network's "group
//! 31-54 hits group 0" are left out. The prop a hat or helmet is (0x822F96A0) is on no viewer ped, so `hat` stays false
//! and the chooser's knock-off roll (0x8239D5D0) has nothing to knock off; a ped in a vehicle is a car's driver here,
//! with no health of its own (`Victim::in_vehicle` is for the day it has).

use crate::{melee::WEAPON_TYPES, rng::Rand, weapons::Weapon};

/// A ped's start health and max (0x8200EF74), the player's too (0x8201966C).
pub const START: f32 = 200.0;
/// IS_CHAR_INJURED (0x825BAE18, 0x82000D68): below this a ped is dying.
pub const INJURED_BELOW: f32 = 100.0;
/// ADD_ARMOUR_TO_CHAR's cap for a non-player, and the player info's max armour (+0x540) at the start.
pub const ARMOUR_CAP: f32 = 100.0;

/// Result flags (+4).
pub const KNOCKED_OUT: u8 = 0x4;
pub const INSTANT: u8 = 0x8;
pub const INJURED: u8 = 0x10;
pub const KILLED: u8 = 0x20;

/// CPed +0x118 (every physical's flag word): the damage proofs and the "only damaged by" bits that 0x82482238 reads.
/// SET_CHAR_PROOFS (`set_proofs`), SET_CHAR_INVINCIBLE, SET_CHAR_ONLY_DAMAGED_BY_PLAYER and
/// SET_CHAR_ONLY_DAMAGED_BY_RELATIONSHIP_GROUP write them.
pub mod flags {
    /// Not damaged by a ped or the driver of a car of the group in +0x11C.
    pub const NOT_BY_GROUP: u32 = 0x0002_0000;
    /// Damaged only by the group in +0x11C.
    pub const ONLY_BY_GROUP: u32 = 0x0004_0000;
    pub const EXPLOSION_PROOF: u32 = 0x0008_0000;
    /// Damaged only by the player (explosions and a few weapon types still hurt).
    pub const ONLY_BY_PLAYER: u32 = 0x0010_0000;
    pub const INVINCIBLE: u32 = 0x0020_0000;
    pub const MELEE_PROOF: u32 = 0x0040_0000;
    /// Weapons 46 (OBJECT), 49 (RAMMEDBYCAR), 50 (RUNOVERBYCAR) and 54 (FALL).
    pub const COLLISION_PROOF: u32 = 0x0080_0000;
    pub const FIRE_PROOF: u32 = 0x0100_0000;
    pub const BULLET_PROOF: u32 = 0x0200_0000;
}

/// SET_CHAR_PROOFS (native 0x825C3478, body 0x825BBC50): `bits` (the ped's +0x118) with the bullet, fire, explosion,
/// collision and melee proof bits set or cleared, in the script's argument order.
pub fn set_proofs(bits: u32, [bullet, fire, explosion, collision, melee]: [bool; 5]) -> u32 {
    use flags::*;
    [(BULLET_PROOF, bullet), (FIRE_PROOF, fire), (EXPLOSION_PROOF, explosion), (COLLISION_PROOF, collision), (MELEE_PROOF, melee)]
        .iter().fold(bits, |b, &(bit, on)| if on { b | bit } else { b & !bit })
}

/// The player's weapon-59 timers (CPlayerPed +0xE00 and +0xE04; weapon 59 has no name in the table: the vehicle parts that
/// sweep a ped use it, `docs/events/port.md` reason 6). A weapon-59 hit that lands (0x823D07E0, called at the end of
/// 0x824DC670 for the player) adds the frame step x 1.1 to `sum`; past 20 the player gets a `window` of 10 (s) with `sum`
/// back to 0, during which weapon 59 does nothing to him (0x823D07C0, in `allowed`). Both run down in the player's
/// per-frame process (0x823D1CE0): the window by the step, the sum by a tenth of it.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Blades {
    pub sum: f32,
    pub window: f32,
}

impl Blades {
    /// 0x823D07C0: weapon 59 may hurt him (the window is not open; a NaN counts as not open).
    pub fn may_hurt(&self) -> bool {
        !(self.window > 0.0)
    }

    /// 0x823D07E0, with the game step `step` (s).
    pub fn hit(&mut self, step: f32) {
        self.sum = step.mul_add(1.1, self.sum);
        if self.sum > 20.0 {
            (self.window, self.sum) = (10.0, 0.0);
        }
    }

    /// 0x823D1CE0's two lines, once a frame.
    pub fn tick(&mut self, step: f32) {
        if self.window > 0.0 {
            self.window -= step;
        }
        if self.sum > 0.0 {
            self.sum = (-step).mul_add(0.1, self.sum);
        }
    }
}

/// CPed +0x1E4, +0x9D4, +0x9D8.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Health {
    pub health: f32,
    pub max: f32,
    pub armour: f32,
}

impl Default for Health {
    /// 0x822F9CB8.
    fn default() -> Self {
        Health { health: START, max: START, armour: 0.0 }
    }
}

/// Who did it (the record's +0 entity).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Attacker {
    /// Not a ped or a car (nothing, an object).
    None,
    Ped,
    /// The player. `sure`: a head shot kills outright (0x824DA8B8): he is in a vehicle (+0x23C bit 29), his player
    /// info's +0x420 bit 28, or his task of type 4 has its byte +0x40 set (0x823C4498); else 1 in 16 like anyone.
    Player { sure: bool },
    /// A car (it ran the ped down). `population`: the population made it (its +0x28 bits 17-21 are not 0), not a
    /// script or the player.
    Vehicle { population: bool },
}

/// One hit (the record 0x824DA110 fills: +0 attacker, +4 amount, +8 component (its bone), +0xC weapon).
pub struct Hit<'a> {
    pub amount: f32,
    pub weapon: &'a Weapon,
    /// The bone tag of the part hit (0x8247E070 on the component; 0 for none).
    pub bone: u16,
    pub attacker: Attacker,
    /// The victim and the attacker ped are friends (0x823C1778: either's relationship masks respect or like the other's
    /// group, or they share a ped group, or both are cops, medics or firemen: `relationships::Side::friend` both ways).
    /// Only read when the victim is the player: a friend's hit on him is cut as friendly fire (`scale`).
    pub friend: bool,
    /// The 105 floor (0x824DB650): FALL (weapon 54) with the record's +0x10 set on a ped above 100, or ped +0x270
    /// bit 29.
    pub floor: bool,
    /// The attacker ped's relationship group (+0xAD8), or its car's driver's (none: no driver).
    pub group: Option<u32>,
    /// The attacker ped is in the victim's ped group (0x8244F198: both have one and it is the same).
    pub same_group: bool,
    /// The weapon type's number when the name table (`melee::WEAPON_TYPES`) has no name for it (58, 59); the row is then
    /// the blank one (`weapon_row`).
    pub number: Option<u32>,
}

impl<'a> Hit<'a> {
    /// A hit with the rest left off: not friendly fire, no floor, no group.
    pub fn new(amount: f32, weapon: &'a Weapon, bone: u16, attacker: Attacker) -> Self {
        Hit { amount, weapon, bone, attacker, friend: false, floor: false, group: None, same_group: false, number: None }
    }
}

/// The amount 0x824DA600 takes as "do not cut this one for friendly fire" (0x82AA67C8).
pub const NO_CUT: f32 = 5555.55;

/// The ped hit.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Victim {
    /// The player (+0x211).
    pub player: bool,
    /// Its model's damage factor (model info +0xF8: pedpersonality.dat's DefendWeakness, 1.3 without a row);
    /// not the player's.
    pub factor: f32,
    /// In a vehicle (+0x23C bit 29): head shots always kill and going below 100 kills.
    pub in_vehicle: bool,
    /// A population ped (+0x9B0 == 1): x1.5 in a vehicle.
    pub population: bool,
    /// Wearing a hat or helmet (a prop at anchor 0, 0x822F96A0): a melee hit on the head x0.75.
    pub hat: bool,
    /// Its own melee action result (task 433) is a block (+0x5C 0x20, the result's IsABlock from the ctor 0x822C40C0 via
    /// 0x82659BC8; read by 0x823C0E98 / 0x822BF340): melee x0.5.
    pub guard: bool,
    /// The model is M_Y_SWAT (0x82A9A5C4): 3 head shots in 4 kill outright.
    pub swat: bool,
    /// +0x118: the proofs (`flags`).
    pub flags: u32,
    /// +0x11C: the relationship group the group bits of `flags` name.
    pub group: u16,
    /// SET_CHAR_BULLETPROOF_VEST (+0x230 bit 25): a GUN weapon's hit on the torso does nothing.
    pub vest: bool,
    /// On NM's ragdoll (+0x1E2 low nibble 2 or more): falls don't hurt it (the ragdoll's own impacts do).
    pub ragdolling: bool,
    /// SET_CHAR_CAN_BE_SHOT_IN_VEHICLE (+0x230 bit 30; set from the ped's setup, 0x58000401): bullets hurt it in a car.
    pub shootable_in_vehicle: bool,
    /// The player's info +0x420 bit 27 (set when made, 0x82238DD8; SET_PLAYER_CONTROL off clears it, 0x82239558): he can
    /// be hurt. Players only.
    pub hurtable: bool,
    /// MAKE_PLAYER_FIRE_PROOF (info +0x53C): fire does nothing to him. Players only.
    pub fire_proof: bool,
    /// A mission ped (+0x9B0 == 2: one a script made): friendly fire from its own ped group is cut.
    pub mission: bool,
    /// +0x230 bit 4 (SET_PED_DIES_WHEN_INJURED): going below 100 kills it (flag 0x20, health 0), like the player.
    pub dies_when_injured: bool,
    /// In deep water (+0x26C bit 29): going below 100 kills it too.
    pub deep_water: bool,
    /// +0x230 bit 29 (SET_CHAR_SUFFERS_CRITICAL_HITS with 0 sets it): a head shot is not an instant kill.
    pub no_critical: bool,
    /// +0x230 bit 28 (set at the ped's setup): DROWNING (53) hurts it.
    pub drowning_ok: bool,
    /// +0x230 bit 27 (set at the ped's setup): weapon 58 hurts it.
    pub weapon58_ok: bool,
    /// The player's weapon-59 window is shut (`Blades::may_hurt`). Players only.
    pub blades_ok: bool,
}

impl Default for Victim {
    fn default() -> Self {
        Victim { player: false, factor: 1.0, in_vehicle: false, population: true, hat: false, guard: false, swat: false, flags: 0, group: 0,
                 vest: false, ragdolling: false, shootable_in_vehicle: true, hurtable: true, fire_proof: false, mission: false,
                 dies_when_injured: false, deep_water: false, no_critical: false, drowning_ok: true, weapon58_ok: true, blades_ok: true }
    }
}

/// The result record (0x824DC670's r5).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Outcome {
    pub flags: u8,
    pub lost: f32,
    pub absorbed: f32,
}

impl Outcome {
    /// Dying (0x8226C9A8 tests 0x10 | 0x20): CTaskComplexDie starts.
    pub fn dies(&self) -> bool {
        self.flags & (INJURED | KILLED) != 0
    }
}

/// weaponinfo +0xC (names from -1 at 0x82A99524): 0 NONE, 1 MELEE, 2 BULLET, 3 EXPLOSIVE, 4 FIRE, 5 COLLISION, 6 FALL,
/// 7 DROWN.
fn damage_type(w: &Weapon) -> u8 {
    ["NONE", "MELEE", "BULLET", "EXPLOSIVE", "FIRE", "COLLISION", "FALL", "DROWN"].iter().position(|&n| n == w.damage_type).unwrap_or(0) as u8
}

/// 0x824DA600's share by bone: upper arms, forearms, thighs, calves 0.75; hands and feet 0.5.
fn bone_share(bone: u16) -> f32 {
    match bone {
        0x4C1 | 0x4C2 | 0x4C8 | 0x4C9 | 0x1A2 | 0x1A3 | 0x1A7 | 0x1A8 => 0.75,
        0x4C3 | 0x4D0 | 0x1A4 | 0x1A9 => 0.5,
        _ => 1.0,
    }
}

const HEAD: u16 = 0x4B5;
const NECK: u16 = 0x4B4;
const SPINE: [u16; 4] = [0x4B2, 0x4B3, 0x36A0, 0x36A1];

/// 0x824DA600 (the network multipliers left out): the amount after the victim's scale (the player: explosions, fire
/// and collisions x0.66, melee x1, the rest x0.33; a ped: its model's factor), the hit part's share, the 1.5 of a
/// population ped in a vehicle and the friendly-fire cut. Friendly fire is a ped attacker's hit (the player is one) with a
/// damage type that is not 3 to 5, other than the 5555.55 amount ([`NO_CUT`]) from the player: the player hitting
/// someone in his ped group, a friend hitting the player, or a ped hitting a mission ped of its group are cut to a third,
/// at most 12.5. ponytail: an attacker that is the victim itself (the exe skips it) is not told apart, none hurts itself.
pub fn scale(hit: &Hit, victim: &Victim) -> f32 {
    let kind = damage_type(hit.weapon);
    let not_area = !(3..=5).contains(&kind);
    let mut a = hit.amount;
    if victim.player {
        if !not_area {
            a *= 0.66;
        } else if kind != 1 {
            a *= 0.33;
        }
    } else {
        a *= victim.factor;
    }
    let share = bone_share(hit.bone);
    if share != 1.0 {
        a *= share;
    }
    if victim.in_vehicle && victim.population {
        a *= 1.5;
    }
    let cut = not_area && match hit.attacker {
        Attacker::Player { .. } => hit.same_group && hit.amount != NO_CUT,
        Attacker::Ped => (victim.player && hit.friend) || (victim.mission && hit.same_group),
        Attacker::None | Attacker::Vehicle { .. } => false,
    };
    if cut {
        // fsel: 12.5 when the third is over it (or a NaN).
        let third = a * 0.33;
        a = if 12.5 - third >= 0.0 { third } else { 12.5 };
    }
    a
}

/// 0x824DC670: `hit` on a ped with `h` (changed) as `victim`; `rand` is the game's C runtime one.
pub fn damage(h: &mut Health, hit: &Hit, victim: &Victim, rand: &mut Rand) -> Outcome {
    let mut out = Outcome::default();
    // 0x824DC670: at 100 health or more the proofs may refuse it; below, it always lands.
    if h.health >= INJURED_BELOW && !allowed(hit, victim) {
        return out;
    }
    let kind = damage_type(hit.weapon);
    let mut a = scale(hit, victim);
    // 0x824DB3E0.
    if h.armour > 0.0 && !hit.weapon.has("ARMOUR_PENETRATING") {
        if a > h.armour {
            (out.absorbed, h.armour) = (h.armour, 0.0);
        } else {
            out.absorbed = a;
            h.armour -= a;
        }
    }
    if kind == 1 {
        let mut guard = true;
        if hit.bone == HEAD {
            if victim.hat {
                a *= 0.75;
            }
            if hit.weapon.has("MELEE_CLUB") {
                a *= 4.0;
            }
            if !victim.player && a > 6.0 && rand.float_in_range(0.0, 1.0) < 0.2 {
                if h.health - a > INJURED_BELOW {
                    a = h.health - INJURED_BELOW + 1.0;
                }
                out.flags |= KNOCKED_OUT;
                guard = false;
            }
        }
        if guard && victim.guard {
            a *= 0.5;
        }
    }
    // 0x824DB650.
    if instant(hit, victim, rand) {
        out.flags |= INSTANT | KILLED;
        (out.lost, out.absorbed) = (h.health, h.armour);
        (h.health, h.armour) = (0.0, 0.0);
        return out;
    }
    let d = (a - out.absorbed).max(0.0);
    if hit.floor {
        let left = (h.health - d).max(105.0);
        out.lost = h.health - left;
        h.health = left;
    } else if h.health - d >= INJURED_BELOW {
        out.lost = d;
        h.health += -d;
    } else if h.health - d >= 1.0 && !(victim.in_vehicle || victim.player || victim.dies_when_injured || victim.deep_water) {
        // 0x824DB650: the player, a ped in a vehicle, SET_PED_DIES_WHEN_INJURED's and one in deep water die at once below
        // 100 (the branch after this one); the rest are only injured, dying.
        out.lost = d;
        out.flags |= INJURED;
        h.health += -d;
    } else {
        out.flags |= KILLED;
        out.lost = h.health;
        h.health = 0.0;
    }
    out
}

/// A weapon's number (`melee::WEAPON_TYPES`), which the proof tests go by; `u32::MAX` for a name not in the table.
fn weapon_id(w: &Weapon) -> u32 {
    WEAPON_TYPES.iter().position(|n| n.eq_ignore_ascii_case(&w.name)).map_or(u32::MAX, |i| i as u32)
}

/// Weapon `name`'s row in `rows` (weaponinfo.xml). The types with none there (RAMMEDBYCAR, RUNOVERBYCAR, EXPLOSION,
/// FALL ...) keep the row the start-up zeroes (0x82A5C530): damage type NONE, no flags. An empty name is EXPLOSION,
/// what a blast with no explosionfx row counts as (0x82230F90).
pub fn weapon_row(name: &str, rows: &[Weapon]) -> Weapon {
    let name = if name.is_empty() { "EXPLOSION" } else { name };
    rows.iter().find(|w| w.name.eq_ignore_ascii_case(name)).cloned().unwrap_or_else(|| Weapon { name: name.to_string(), ..Default::default() })
}

/// Whether an explosion hurts this entity now (0x82230F90 at 0x822310AC): each explosion keeps the entities it has hurt,
/// 32 slots at its +0x88 (0x825495A8 asks, 0x82549530 adds to the first empty one and registers the reference, so a
/// deleted entity frees its slot). One not listed is added and hurt; one listed, or not listed with the list full, is
/// hurt only when it is a ped (entity +0x28 type 3) in ragdoll state 5 (+0x708).
pub fn blast_hurts(hurt: &mut Vec<u64>, id: u64, ped_ragdoll_state: Option<i32>) -> bool {
    if !hurt.contains(&id) && hurt.len() < 32 {
        hurt.push(id);
        return true;
    }
    ped_ragdoll_state == Some(5)
}

/// 0x824DA160: may this hit hurt this ped at all (true: it may). The ped's own refusals, then the physical's
/// (`may_damage`), and no fall damage on a ragdoll.
pub fn allowed(hit: &Hit, victim: &Victim) -> bool {
    let (weapon, kind) = (hit.number.unwrap_or_else(|| weapon_id(hit.weapon)), damage_type(hit.weapon));
    // DROWNING (53) and weapon 58 need their ped bits (the setup sets both).
    if (weapon == 53 && !victim.drowning_ok) || (weapon == 58 && !victim.weapon58_ok) {
        return false;
    }
    // A bullet-proof vest stops a GUN weapon on the torso.
    if victim.vest && hit.weapon.has("GUN") && SPINE.contains(&hit.bone) {
        return false;
    }
    // The player: hurtable, not fire-proof against fire, and weapon 59 only while its window is shut.
    if victim.player && (!victim.hurtable || (victim.fire_proof && kind == 4) || (weapon == 59 && !victim.blades_ok)) {
        return false;
    }
    // A ped never hurts one in its own group, except by EXPLOSION (51).
    if matches!(hit.attacker, Attacker::Ped) && hit.same_group && weapon != 51 {
        return false;
    }
    if kind == 2 && victim.in_vehicle && !victim.shootable_in_vehicle {
        return false;
    }
    may_damage(victim.flags, victim.group, weapon, kind, hit.attacker, hit.group) && !(weapon == 54 && victim.ragdolling)
}

/// 0x82482238 (CPhysical: peds, cars and objects share it): may an entity with flags `bits` (+0x118) and group
/// (+0x11C) be hurt by weapon `weapon` of damage type `kind` from `att` (`att_group`: the ped's relationship group, or
/// the car's driver's)? Invincible: never. "Only damaged by the player" and the group bits ignore explosive damage and
/// weapons 51 (EXPLOSION), 53 and 58; a car passes the player-only test unless the population made it. Then the proof
/// of the damage type (1 melee, 2 bullet, 3 explosion, 4 fire) and the collision proof of weapons 46, 49, 50 and 54.
fn may_damage(bits: u32, group: u16, weapon: u32, kind: u8, att: Attacker, att_group: Option<u32>) -> bool {
    use flags::*;
    if bits & INVINCIBLE != 0 {
        return false;
    }
    let exempt = kind == 3 || matches!(weapon, 51 | 53 | 58);
    // The group of a ped attacker (0 when not given), or of a car's driver (none without).
    let who = match att {
        Attacker::Ped | Attacker::Player { .. } => Some(att_group.unwrap_or(0)),
        Attacker::Vehicle { .. } => att_group,
        Attacker::None => None,
    };
    if bits & ONLY_BY_PLAYER != 0 && !exempt && !matches!(att, Attacker::Player { .. } | Attacker::Vehicle { population: false }) {
        return false;
    }
    // 0x8247F1D8: the low 16 bits of the attacker's group are the ped's named one. ponytail: the network's "group 31-54
    // hits group 0" (0x82B3950C == 1) is left out.
    if bits & ONLY_BY_GROUP != 0 && !exempt && !who.is_some_and(|g| g & 0xFFFF == group as u32) {
        return false;
    }
    if bits & NOT_BY_GROUP != 0 && !exempt && who == Some(group as u32) {
        return false;
    }
    match kind {
        1 if bits & MELEE_PROOF != 0 => return false,
        2 if bits & BULLET_PROOF != 0 => return false,
        3 if bits & EXPLOSION_PROOF != 0 => return false,
        4 if bits & FIRE_PROOF != 0 => return false,
        _ => {}
    }
    !(matches!(weapon, 46 | 49 | 50 | 54) && bits & COLLISION_PROOF != 0)
}

/// 0x822FCCF8: a car at `speed` m/s (the scanner caps it at 1000) meets a ped that is not the player, falling at `vz`
/// m/s (ped +0x188): what hurts it, as the weapon and the amount, or none when it only nudges it. Up to 12 m/s a knock
/// needs over 6 m/s, or over 3 when `vz` is under -0.8: 30 of RAMMEDBYCAR (type 0x31). Faster, the type is the throw's
/// (the viewer's `runover::throw`): RUNOVERBYCAR (0x32) when `along`, thrown along the car's way (a train 0x822FCF8C, past
/// the side within the front 0x822FD098, or the roll's 0x822FD380), else RAMMEDBYCAR (over the bonnet, or not thrown:
/// reversing, a front corner). Both are collision damage of 30.
pub fn car_hit(speed: f32, vz: f32, along: bool) -> Option<(&'static str, f32)> {
    let speed = speed.min(1000.0);
    if speed > 12.0 {
        Some((if along { "RUNOVERBYCAR" } else { "RAMMEDBYCAR" }, 30.0))
    } else if speed > 6.0 || (vz < -0.8 && speed > 3.0) {
        Some(("RAMMEDBYCAR", 30.0))
    } else {
        None
    }
}

/// 0x822FCCF8 for the player in a single-player game (the throw over 12 m/s is for the others and the network's: he always
/// takes the light branch, 0x822FD7F8): over 10 m/s (0x82019670), or over 3 when falling faster than 0.8 m/s, a car knocks him down
/// with 30 of RAMMEDBYCAR (type 0x31), a train (the vehicle's kind, +0x12E4, 3) with 150 (0x82002518).
pub fn car_hit_player(speed: f32, vz: f32, train: bool) -> Option<(&'static str, f32)> {
    let speed = speed.min(1000.0);
    (speed > 10.0 || (vz < -0.8 && speed > 3.0)).then_some(("RAMMEDBYCAR", if train { 150.0 } else { 30.0 }))
}

/// 0x824DD470, the jump task's damage for a long fall (CTaskComplexJump's next step 0x824E0DD0, after the in-air task
/// ended with a fall faster than 9.5 m/s): `fell` is the height the jump started at (task +0x34) less the ped's height
/// now. Up to 10 m (15 m for the player, vf +0x114) it is 4 a metre; past that 100 x the ped's health, a certain death.
/// Its weapon is FALL (54), no attacker, through CPedDamage like the rest (a fall on a dying or dead ped is refused,
/// [`allowed`]). The exe does not clamp: a `fell` under 0 (landing above the launch) gives a negative amount.
pub fn fall_damage(fell: f32, player: bool, health: f32) -> f32 {
    let limit = if player { 15.0 } else { 10.0 };
    if fell > limit { health * 100.0 } else { fell * 4.0 }
}

/// The player info's +0x4B0: why his control is off, any reason enough (IS_PLAYER_CONTROL_ON, 0x825B56B0, answers
/// +0x4B0 == 0 outside a network game). 0x82239558 sets or clears [`SCRIPT`](control::SCRIPT) (SET_PLAYER_CONTROL and
/// _ADVANCED) or [`OTHER`](control::OTHER) by its second argument.
pub mod control {
    pub const SCRIPT: u32 = 0x20;
    pub const OTHER: u32 = 0x800;
}

/// The armour the police factory (0x821646D8) gives a cop of model `model`: 100 for M_Y_Swat and M_M_FBI, none
/// (0x821643A8 too) for the rest. It also gives them weapons: a pistol, half the time an MP5, and an M4 (4 in 5) or a
/// shotgun.
pub fn police_armour(model: &str) -> f32 {
    if ["M_Y_SWAT", "M_M_FBI"].iter().any(|m| m.eq_ignore_ascii_case(model)) { ARMOUR_CAP } else { 0.0 }
}

/// 0x824DA8B8: the hit kills outright. Bullets only, never the player nor a ped that suffers no critical hits: to the
/// head or neck (not a SHOTGUN-slot weapon's) in a vehicle, a SWAT's 3 in 4, a SNIPER-slot weapon's, the player's when
/// `sure`, else 1 in 16; and a SNIPER-slot weapon's to the spine.
fn instant(hit: &Hit, victim: &Victim, rand: &mut Rand) -> bool {
    if victim.player || victim.no_critical || damage_type(hit.weapon) != 2 {
        return false;
    }
    let sniper = hit.weapon.slot == "SNIPER";
    if (hit.bone == HEAD || hit.bone == NECK) && hit.weapon.slot != "SHOTGUN" {
        let kill = victim.in_vehicle
            || (victim.swat && rand.next() & 3 != 0)
            || sniper
            || matches!(hit.attacker, Attacker::Player { sure: true })
            || rand.next() & 15 == 0;
        if kill {
            return true;
        }
    }
    sniper && SPINE.contains(&hit.bone)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn gun(slot: &str, kind: &str, flags: &[&str]) -> Weapon {
        let mut w = crate::weapons::read(r#"<weapon type="X"><data slot="HANDGUN" firetype="INSTANT_HIT" damagetype="BULLET"><damage base="40"/></data></weapon>"#).remove(0);
        (w.slot, w.damage_type, w.flags) = (slot.into(), kind.into(), flags.iter().map(|f| f.to_string()).collect());
        w
    }

    fn hit(w: &Weapon, amount: f32, bone: u16, attacker: Attacker) -> Hit<'_> {
        Hit::new(amount, w, bone, attacker)
    }

    #[test]
    fn damage_by_part_armour_and_death() {
        let (pistol, sniper, shotgun) = (gun("HANDGUN", "BULLET", &[]), gun("SNIPER", "BULLET", &[]), gun("SHOTGUN", "BULLET", &[]));
        let ped = Victim { factor: 1.3, ..Default::default() };
        // Each hit from a fresh rand (seed 1: its first draw is 41).
        fn run(h: &mut Health, hit: &Hit, v: &Victim) -> Outcome {
            damage(h, hit, v, &mut Rand::new(1))
        }
        // Torso x the model factor; an upper arm x0.75; a hand x0.5.
        let mut h = Health::default();
        assert_eq!(run(&mut h, &hit(&pistol, 20.0, 0x1A1, Attacker::Ped), &ped), Outcome { flags: 0, lost: 26.0, absorbed: 0.0 });
        assert_eq!(h.health, 174.0);
        assert_eq!(run(&mut h, &hit(&pistol, 40.0, 0x4C1, Attacker::Ped), &Victim::default()).lost, 30.0);
        assert_eq!(run(&mut h, &hit(&pistol, 40.0, 0x4D0, Attacker::Ped), &Victim::default()).lost, 20.0);
        // Armour first.
        let mut h = Health { armour: 50.0, ..Default::default() };
        assert_eq!(run(&mut h, &hit(&pistol, 80.0, 0, Attacker::Ped), &Victim::default()), Outcome { flags: 0, lost: 30.0, absorbed: 50.0 });
        assert_eq!((h.health, h.armour), (170.0, 0.0));
        let mut h = Health { armour: 50.0, ..Default::default() };
        run(&mut h, &hit(&gun("HANDGUN", "BULLET", &["ARMOUR_PENETRATING"]), 80.0, 0, Attacker::Ped), &Victim::default());
        assert_eq!((h.health, h.armour), (120.0, 50.0));
        // The player's head shot kills; a shotgun's doesn't; a ped's (41 & 15 = 9, not 0) doesn't.
        let mut h = Health::default();
        assert_eq!(run(&mut h, &hit(&pistol, 20.0, HEAD, Attacker::Player { sure: true }), &ped).flags, INSTANT | KILLED);
        assert_eq!(h.health, 0.0);
        let mut h = Health::default();
        assert_eq!(run(&mut h, &hit(&shotgun, 20.0, HEAD, Attacker::Player { sure: true }), &Victim::default()).flags, 0);
        assert_eq!(run(&mut Health::default(), &hit(&pistol, 20.0, NECK, Attacker::Ped), &Victim::default()).flags, 0);
        // Sniper to the spine.
        assert!(run(&mut Health::default(), &hit(&sniper, 20.0, 0x36A1, Attacker::Ped), &Victim::default()).dies());
        // Below 100 dying (injured), at 0 killed; in a vehicle, below 100 is killed.
        let mut h = Health { health: 120.0, ..Default::default() };
        assert_eq!(run(&mut h, &hit(&pistol, 30.0, 0, Attacker::Ped), &Victim::default()), Outcome { flags: INJURED, lost: 30.0, absorbed: 0.0 });
        assert_eq!(run(&mut h, &hit(&pistol, 90.0, 0, Attacker::Ped), &Victim::default()), Outcome { flags: KILLED, lost: 90.0, absorbed: 0.0 });
        let mut h = Health { health: 120.0, ..Default::default() };
        let car = Victim { in_vehicle: true, population: false, ..Default::default() };
        assert_eq!(run(&mut h, &hit(&pistol, 30.0, 0, Attacker::Ped), &car).flags, KILLED);
        // The player: bullets x0.33, blasts x0.66, melee x1.
        let me = Victim { player: true, ..Default::default() };
        assert_eq!(run(&mut Health::default(), &hit(&pistol, 100.0, 0, Attacker::Ped), &me).lost, 100.0 * 0.33);
        assert_eq!(run(&mut Health::default(), &hit(&gun("THROWN", "EXPLOSIVE", &[]), 100.0, 0, Attacker::None), &me).lost, 100.0 * 0.66);
        assert_eq!(run(&mut Health::default(), &hit(&gun("MELEE", "MELEE", &[]), 10.0, HEAD, Attacker::Ped), &me).lost, 10.0);
        // The player below 100 dies at once (health 0), a ped is only injured (0x824DB650).
        let mut h = Health { health: 120.0, ..Default::default() };
        assert_eq!((run(&mut h, &hit(&gun("MELEE", "MELEE", &[]), 40.0, 0, Attacker::Ped), &me).flags, h.health), (KILLED, 0.0));
        // The 105 floor.
        let mut h = Health { health: 150.0, ..Default::default() };
        assert_eq!(run(&mut h, &Hit { floor: true, ..hit(&pistol, 100.0, 0, Attacker::Ped) }, &Victim::default()).lost, 45.0);
    }

    /// A row for a type the file has: its damage type and name.
    fn typed(name: &str, kind: &str) -> Weapon {
        Weapon { name: name.into(), damage_type: kind.into(), ..Default::default() }
    }

    /// Run `hit` on a fresh ped with these `flags`: health lost.
    fn lost(flags: u32, hit: &Hit) -> f32 {
        damage(&mut Health::default(), hit, &Victim { flags, ..Default::default() }, &mut Rand::new(1)).lost
    }

    #[test]
    fn proofs() {
        use flags::*;
        let (pistol, knife, grenade, molotov) = (gun("HANDGUN", "BULLET", &[]), typed("KNIFE", "MELEE"), typed("GRENADE", "EXPLOSIVE"), typed("MOLOTOV", "FIRE"));
        let (rammed, explosion, fall) = (weapon_row("RAMMEDBYCAR", &[]), weapon_row("", &[]), weapon_row("FALL", &[]));
        let shot = Hit::new(20.0, &pistol, 0, Attacker::Ped);
        // SET_CHAR_PROOFS: bullet, fire, explosion, collision, melee, each its own bit (0x825BBC50).
        assert_eq!(set_proofs(0, [true, false, false, false, false]), BULLET_PROOF);
        assert_eq!(set_proofs(0, [false, true, true, true, true]), FIRE_PROOF | EXPLOSION_PROOF | COLLISION_PROOF | MELEE_PROOF);
        assert_eq!(set_proofs(0x0800_0000 | BULLET_PROOF | MELEE_PROOF, [false, false, false, false, false]), 0x0800_0000);
        // Each proof stops its own damage type and nothing else; invincible stops all.
        let all = [BULLET_PROOF, MELEE_PROOF, EXPLOSION_PROOF, FIRE_PROOF];
        for (k, (w, bit)) in [(&pistol, BULLET_PROOF), (&knife, MELEE_PROOF), (&grenade, EXPLOSION_PROOF), (&molotov, FIRE_PROOF)].into_iter().enumerate() {
            let hit = Hit::new(20.0, w, 0, Attacker::Ped);
            assert_eq!(lost(bit, &hit), 0.0, "proof {k}");
            assert!(lost(all.iter().filter(|&&b| b != bit).fold(0, |a, b| a | b), &hit) > 0.0, "other proofs {k}");
            assert_eq!(lost(INVINCIBLE, &hit), 0.0);
        }
        // Collision proof: weapons 49, 50, 54 (and 46), not the damage type; a blast with no row is EXPLOSION (51),
        // type NONE: only invincibility stops that.
        assert_eq!((lost(COLLISION_PROOF, &Hit::new(30.0, &rammed, 0, Attacker::Vehicle { population: true })), lost(COLLISION_PROOF, &Hit::new(30.0, &fall, 0, Attacker::None))), (0.0, 0.0));
        assert_eq!((lost(COLLISION_PROOF, &Hit::new(30.0, &explosion, 0, Attacker::None)), lost(EXPLOSION_PROOF, &Hit::new(30.0, &explosion, 0, Attacker::None))), (30.0, 30.0));
        // A dying ped (under 100) takes it anyway, so it can be finished off.
        let mut h = Health { health: 90.0, ..Default::default() };
        assert_eq!(damage(&mut h, &shot, &Victim { flags: INVINCIBLE, ..Default::default() }, &mut Rand::new(1)).flags, INJURED);
        assert_eq!(h.health, 70.0);
        // Only damaged by the player: not a ped, not a population car; the player, a script's car and explosions hurt.
        let only = |a| lost(ONLY_BY_PLAYER, &Hit::new(20.0, &pistol, 0, a));
        assert_eq!((only(Attacker::Ped), only(Attacker::None), only(Attacker::Vehicle { population: true })), (0.0, 0.0, 0.0));
        assert_eq!((only(Attacker::Player { sure: true }), only(Attacker::Vehicle { population: false })), (20.0, 20.0));
        assert_eq!(lost(ONLY_BY_PLAYER, &Hit::new(20.0, &grenade, 0, Attacker::Ped)), 20.0);
        // Only / not damaged by a relationship group: the low 16 bits of the attacker's (or its driver's) group.
        let grp = |bits, g: u16, a, from: Option<u32>| damage(&mut Health::default(), &Hit { group: from, ..Hit::new(20.0, &pistol, 0, a) }, &Victim { flags: bits, group: g, ..Default::default() }, &mut Rand::new(1)).lost;
        assert_eq!((grp(ONLY_BY_GROUP, 7, Attacker::Ped, Some(7)), grp(ONLY_BY_GROUP, 7, Attacker::Ped, Some(0x10007)), grp(ONLY_BY_GROUP, 7, Attacker::Ped, Some(8))), (20.0, 20.0, 0.0));
        assert_eq!((grp(ONLY_BY_GROUP, 7, Attacker::Vehicle { population: false }, Some(7)), grp(ONLY_BY_GROUP, 7, Attacker::Vehicle { population: false }, None)), (20.0, 0.0));
        assert_eq!((grp(NOT_BY_GROUP, 7, Attacker::Ped, Some(7)), grp(NOT_BY_GROUP, 7, Attacker::Ped, Some(8)), grp(NOT_BY_GROUP, 7, Attacker::None, None)), (0.0, 20.0, 20.0));
        assert_eq!(grp(NOT_BY_GROUP, 7, Attacker::Vehicle { population: true }, None), 20.0);
    }

    #[test]
    fn ped_refusals() {
        let (pistol, grenade, molotov) = (gun("HANDGUN", "BULLET", &["GUN"]), typed("GRENADE", "EXPLOSIVE"), typed("MOLOTOV", "FIRE"));
        let (fall, explosion) = (weapon_row("FALL", &[]), weapon_row("", &[]));
        let take = |v: Victim, hit: Hit| damage(&mut Health::default(), &hit, &v, &mut Rand::new(1)).lost;
        // A vest stops a GUN weapon's torso hit (the spine tags), not an arm's, and not a grenade's.
        let vest = || Victim { vest: true, ..Default::default() };
        assert_eq!(take(vest(), Hit::new(20.0, &pistol, 0x4B2, Attacker::Ped)), 0.0);
        assert_eq!(take(vest(), Hit::new(20.0, &pistol, 0x36A1, Attacker::Ped)), 0.0);
        assert_eq!(take(vest(), Hit::new(20.0, &pistol, 0x4C1, Attacker::Ped)), 15.0);
        assert_eq!(take(vest(), Hit::new(20.0, &grenade, 0x4B2, Attacker::Ped)), 20.0);
        // A ped in the victim's group never hurts it, except by EXPLOSION; the player does, for a third (friendly fire).
        let mate = |w: &Weapon, a| take(Victim::default(), Hit { same_group: true, ..Hit::new(20.0, w, 0, a) });
        assert_eq!((mate(&pistol, Attacker::Ped), mate(&explosion, Attacker::Ped), mate(&pistol, Attacker::Player { sure: true })), (0.0, 20.0, 20.0 * 0.33));
        // A bullet in a car needs the can-be-shot-in-vehicle bit (set from the setup).
        let car = |shootable| Victim { in_vehicle: true, population: false, shootable_in_vehicle: shootable, ..Default::default() };
        assert_eq!((take(car(false), Hit::new(20.0, &pistol, 0, Attacker::Ped)), take(car(true), Hit::new(20.0, &pistol, 0, Attacker::Ped))), (0.0, 20.0));
        // The player: not while his info says he can't be hurt; fire only without MAKE_PLAYER_FIRE_PROOF.
        let me = |hurtable, fire_proof| Victim { player: true, hurtable, fire_proof, ..Default::default() };
        assert_eq!((take(me(false, false), Hit::new(20.0, &grenade, 0, Attacker::None)), take(me(true, true), Hit::new(20.0, &molotov, 0, Attacker::None))), (0.0, 0.0));
        assert_eq!(take(me(true, false), Hit::new(20.0, &molotov, 0, Attacker::None)), 20.0 * 0.66);
        // Falls: not on a ragdoll.
        assert_eq!(take(Victim { ragdolling: true, ..Default::default() }, Hit::new(20.0, &fall, 0, Attacker::None)), 0.0);
        assert_eq!(take(Victim::default(), Hit::new(20.0, &fall, 0, Attacker::None)), 20.0);
        // A melee hit on a ped whose own result is a block: half (0x824DB3E0).
        let knife = typed("KNIFE", "MELEE");
        let blow = |guard| take(Victim { guard, ..Default::default() }, Hit::new(20.0, &knife, 0x4B2, Attacker::Ped));
        assert_eq!(blow(true), blow(false) * 0.5);
    }

    #[test]
    fn falls() {
        // 0x824DD470: 4 a metre up to 10 m (15 for the player), over that 100 x the health; none is not clamped.
        assert_eq!((fall_damage(3.0, false, 200.0), fall_damage(10.0, false, 200.0), fall_damage(10.5, false, 200.0)), (12.0, 40.0, 20000.0));
        assert_eq!((fall_damage(14.0, true, 200.0), fall_damage(15.0, true, 200.0), fall_damage(15.5, true, 150.0)), (56.0, 60.0, 15000.0));
        assert_eq!(fall_damage(-2.0, false, 200.0), -8.0);
    }

    #[test]
    fn cars_blasts_and_cops() {
        // 0x822FCCF8: nudged under 6 m/s (3 while falling), 30 of RAMMEDBYCAR to 12, 30 of RUNOVERBYCAR above.
        assert_eq!((car_hit(5.9, 0.0, true), car_hit(3.1, -0.9, true), car_hit(3.0, -0.9, true), car_hit(3.1, -0.8, true)), (None, Some(("RAMMEDBYCAR", 30.0)), None, None));
        assert_eq!(car_hit(12.1, 0.0, false), Some(("RAMMEDBYCAR", 30.0)));
        assert_eq!((car_hit(6.1, 0.0, true), car_hit(12.0, 0.0, true), car_hit(12.1, 0.0, true), car_hit(5000.0, 0.0, true)), (Some(("RAMMEDBYCAR", 30.0)), Some(("RAMMEDBYCAR", 30.0)), Some(("RUNOVERBYCAR", 30.0)), Some(("RUNOVERBYCAR", 30.0))));
        // The player in single player: over 10 m/s (3 falling), at any speed, 30 of RAMMEDBYCAR; a train 150.
        assert_eq!((car_hit_player(10.0, 0.0, false), car_hit_player(10.1, 0.0, false), car_hit_player(50.0, 0.0, true)),
                   (None, Some(("RAMMEDBYCAR", 30.0)), Some(("RAMMEDBYCAR", 150.0))));
        assert_eq!((car_hit_player(3.1, -0.9, false), car_hit_player(3.0, -0.9, false)), (Some(("RAMMEDBYCAR", 30.0)), None));
        // The weapon of a hit with no row of its own is the blank one; a named one is the file's.
        let rows = [typed("GRENADE", "EXPLOSIVE")];
        assert_eq!((weapon_row("grenade", &rows).damage_type, weapon_row("RUNOVERBYCAR", &rows).damage_type, weapon_row("", &rows).name), ("EXPLOSIVE".to_string(), String::new(), "EXPLOSION".to_string()));
        // The police factory (0x821646D8): SWAT and FBI get 100 armour, soaked first.
        assert_eq!((police_armour("M_Y_Swat"), police_armour("m_m_fbi"), police_armour("M_Y_COP")), (100.0, 100.0, 0.0));
        let mut h = Health { armour: police_armour("M_Y_SWAT"), ..Default::default() };
        let pistol = gun("HANDGUN", "BULLET", &[]);
        assert_eq!(damage(&mut h, &Hit::new(60.0, &pistol, 0, Attacker::Ped), &Victim::default(), &mut Rand::new(1)).absorbed, 60.0);
        assert_eq!((h.armour, h.health), (40.0, 200.0));
    }

    /// 0x824DA600's friendly-fire cut: a third, at most 12.5, for a ped attacker's hit of a damage type not 3 to 5.
    #[test]
    fn friendly_fire() {
        let (pistol, grenade, explosion) = (gun("HANDGUN", "BULLET", &[]), typed("GRENADE", "EXPLOSIVE"), weapon_row("", &[]));
        let take = |v: Victim, hit: Hit| damage(&mut Health::default(), &hit, &v, &mut Rand::new(1)).lost;
        fn niko(w: &Weapon, amount: f32, same: bool) -> Hit<'_> {
            Hit { same_group: same, ..Hit::new(amount, w, 0, Attacker::Player { sure: true }) }
        }
        // Niko shooting someone in his ped group; out of it; an explosive; the 5555.55 amount he is never cut on.
        assert_eq!(take(Victim::default(), niko(&pistol, 30.0, true)), 30.0 * 0.33);
        assert_eq!(take(Victim::default(), niko(&pistol, 100.0, true)), 12.5);
        assert_eq!(take(Victim::default(), niko(&pistol, 100.0, false)), 100.0);
        assert_eq!(take(Victim::default(), niko(&grenade, 100.0, true)), 100.0);
        assert_eq!(take(Victim::default(), niko(&pistol, NO_CUT, true)), START);
        // A blast with no row (EXPLOSION, type NONE) is cut like a bullet; a ped model's factor comes first.
        assert_eq!(take(Victim { factor: 1.3, ..Default::default() }, niko(&explosion, 20.0, true)), 20.0 * 1.3 * 0.33);
        // A friend's hit on the player: his x0.33 first, then the cut; a stranger's is only the x0.33.
        let me = || Victim { player: true, ..Default::default() };
        let ped = |friend| Hit { friend, ..Hit::new(100.0, &pistol, 0, Attacker::Ped) };
        assert_eq!((take(me(), ped(true)), take(me(), ped(false))), (100.0 * 0.33 * 0.33, 100.0 * 0.33));
        assert_eq!(take(me(), Hit { friend: true, ..Hit::new(100.0, &grenade, 0, Attacker::Ped) }), 100.0 * 0.66);
        // A mission ped hit by a ped of its group (a blast: a bullet from its group is refused outright, `allowed`);
        // a population ped is not cut.
        let blast = |same| Hit { same_group: same, ..Hit::new(20.0, &explosion, 0, Attacker::Ped) };
        let mission = Victim { mission: true, ..Default::default() };
        assert_eq!((take(mission, blast(true)), take(Victim::default(), blast(true)), take(mission, blast(false))), (20.0 * 0.33, 20.0, 20.0));
        assert_eq!(take(mission, Hit { same_group: true, ..Hit::new(100.0, &pistol, 0, Attacker::Ped) }), 0.0);
        // The car and no attacker are never friends.
        assert_eq!(take(me(), Hit { friend: true, ..Hit::new(100.0, &pistol, 0, Attacker::None) }), 100.0 * 0.33);
    }

    /// DROWNING (53) and weapon 58 need the ped's bits, weapon 59 the player's shut window (0x824DA160, 0x823D07C0).
    #[test]
    fn weapons_53_58_59() {
        let none = weapon_row("", &[]);
        let take = |v: Victim, n| damage(&mut Health::default(), &Hit { number: Some(n), ..Hit::new(20.0, &none, 0, Attacker::None) }, &v, &mut Rand::new(1)).lost;
        // The setup sets both bits (0x58000401), so by default they hurt.
        assert_eq!((take(Victim::default(), 53), take(Victim::default(), 58), take(Victim::default(), 59)), (20.0, 20.0, 20.0));
        let no53 = Victim { drowning_ok: false, ..Default::default() };
        let no58 = Victim { weapon58_ok: false, ..Default::default() };
        assert_eq!((take(no53, 53), take(no53, 58), take(no58, 58), take(no58, 53)), (0.0, 20.0, 0.0, 20.0));
        // ONLY_BY_PLAYER lets 53 and 58 through (but not 59).
        let only = Victim { flags: flags::ONLY_BY_PLAYER, ..Default::default() };
        assert_eq!((take(only, 53), take(only, 58), take(only, 59)), (20.0, 20.0, 0.0));
        // Weapon 59 on the player waits on his window; on a ped it is plain.
        let me = |blades_ok| Victim { player: true, blades_ok, ..Default::default() };
        assert_eq!((take(me(true), 59), take(me(false), 59), take(Victim { blades_ok: false, ..Default::default() }, 59)), (20.0 * 0.33, 0.0, 20.0));
        assert_eq!(take(me(false), 58), 20.0 * 0.33);
    }

    /// The weapon-59 timers: 1.1 x the step a hit; past 20 the window opens for 10 s (0x823D07E0); both run down (0x823D1CE0).
    #[test]
    fn blades() {
        let mut b = Blades::default();
        assert!(b.may_hurt());
        b.hit(10.0);
        assert_eq!((b.sum, b.window), (11.0, 0.0));
        b.hit(10.0);
        assert_eq!((b.sum, b.window, b.may_hurt()), (0.0, 10.0, false));
        b.tick(4.0);
        assert_eq!((b.window, b.may_hurt()), (6.0, false));
        b.tick(6.0);
        assert_eq!((b.window, b.may_hurt()), (0.0, true));
        let mut b = Blades { sum: 5.0, window: 0.0 };
        b.tick(10.0);
        assert_eq!(b.sum, 4.0);
        assert!(Blades { sum: 0.0, window: f32::NAN }.may_hurt());
    }

    /// 0x824DB650's flags: SET_PED_DIES_WHEN_INJURED and deep water kill below 100 like the player; no critical hits
    /// takes the head shot's instant kill away (0x824DA8B8).
    #[test]
    fn script_flags_of_the_ped() {
        let pistol = gun("HANDGUN", "BULLET", &[]);
        let take = |v: Victim, hit: &Hit| { let mut h = Health { health: 120.0, ..Default::default() }; let o = damage(&mut h, hit, &v, &mut Rand::new(1)); (o.flags, h.health) };
        let shot = Hit::new(30.0, &pistol, 0, Attacker::Ped);
        assert_eq!(take(Victim::default(), &shot), (INJURED, 90.0));
        assert_eq!(take(Victim { dies_when_injured: true, ..Default::default() }, &shot), (KILLED, 0.0));
        assert_eq!(take(Victim { deep_water: true, ..Default::default() }, &shot), (KILLED, 0.0));
        let head = Hit::new(20.0, &pistol, HEAD, Attacker::Player { sure: true });
        assert_eq!(take(Victim::default(), &head).0, INSTANT | KILLED);
        assert_eq!(take(Victim { no_critical: true, ..Default::default() }, &head), (0, 100.0));
    }

    #[test]
    fn a_blast_hurts_each_entity_once() {
        let mut hurt = vec![];
        assert!(blast_hurts(&mut hurt, 7, Some(2)) && !blast_hurts(&mut hurt, 7, Some(2)));
        assert!(blast_hurts(&mut hurt, 7, Some(5)) && !blast_hurts(&mut hurt, 7, None));
        (8..39).for_each(|id| assert!(blast_hurts(&mut hurt, id, None)));
        assert!(!blast_hurts(&mut hurt, 99, None) && blast_hurts(&mut hurt, 99, Some(5)) && hurt.len() == 32);
    }
}
