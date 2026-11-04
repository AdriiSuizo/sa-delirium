//! The melee action test 0x8265E798 (Xbox 360 exe, TU8; board row PD11) and the pick of a move from the table
//! (0x8265FF00, [`choose`]): the ped part ([`ped_takes`], 0x8265B648: who, the must flags, the stat ranges, the
//! weapon class), the moves from the pad (below), Type and AvailableFromIdle, the Move's anim group
//! ([`anim_in`]), target, lock-on, target type and relative position, AdditionalActionTest, and the sort and
//! weighted draw ([`pick`]), and SpecialTest (0x82659CC8, [`special_test`]). The opponent part (0x8265CBE8) is the
//! caller's.
//!
//! Melee moves from the pad: whether an action table action's impulses (its two `CheckImpulse` / `Impulse` /
//! `ImpulseState` / `ImpulseRange` / `ImpulseInterval` sets, `actiontable::Impulse`) hold on a controls object, as
//! the action test asks it.
//!
//! The action test asks this only when its caller wants impulses checked (its stack byte at r1+0x167), and fails
//! without a controls object (r1+0x16C is 0). Each impulse is tested by 0x8265B9F0, then the two are joined by the
//! action's ImpulseOp: `or`, `and`, `xor` (anything else fails).
//!
//! 0x8265B9F0: an impulse not checked holds; impulse `none` fails. The control is the melee button's (`attack`
//! +0xBDC, `attack2` +0xBE8, `attack3` +0xBF4, `attack4` +0xD14, `kick` +0xC00, `block` +0xC0C) or the move stick's
//! (`analogLeftRight` the pair 0x822B7FC0 takes, `analogUpDown` 0x822B8018's, `analogFromCenter` both). The value
//! range is 0x80 ..= 0xFF, except: the stick axes take ImpulseRange from -1..1 to 0..255 (((r + 1) * 0.5) * 255),
//! `analogFromCenter` takes |r| * 127 (then tests both sides of both axes: 0x80 + lo ..= 0x80 + hi and 0x7F - hi
//! ..= 0x7F - lo), and `attack3` takes r * 255 while the controller setting 0x82BF9F24 is 0 (RT bound as pressure).
//! Each end goes to a byte as the low byte of the truncated integer (`fctidz`). The interval is ImpulseInterval
//! seconds as whole milliseconds. Then the state asks the control's history (the last 64 frames' values and
//! times, `controls::IoValue`): see [`held`], [`held_up`], [`pressed_and_held`], [`released_and_held`],
//! [`pressed`], [`released`]; state `none` holds.

use crate::actiontable::{Action, ActionTable, Impulse, ResultDef};
use crate::controls::{index, pick as pair_pick, Controls, IoValue, AXIS_PAIRS};

/// The history entry `i` frames back from the one at `at` (the queries' index; 64 entries).
fn back(at: u8, i: u8) -> usize {
    (if i <= at { at - i } else { at.wrapping_sub(i.min(0x3F)).wrapping_add(0x40) }) as usize
}

/// The window's start: this frame's time less `ms`, or 0 when `ms` is past it.
fn since(v: &IoValue, h: &[(u8, u32)], ms: u32) -> u32 {
    h[v.history_at as usize].1.saturating_sub(ms)
}

/// A history value (invert applied) in lo ..= hi, the ends either way round.
fn within(v: &IoValue, value: u8, lo: u8, hi: u8) -> bool {
    let x = value ^ v.invert;
    if lo > hi { hi <= x && x <= lo } else { lo <= x && x <= hi }
}

fn hist(v: &IoValue) -> &[(u8, u32)] {
    v.history.as_deref().expect("melee controls keep a history")
}

/// 0x822D4158 (`heldDown`): every entry within the last `ms` (this frame's too) in the range.
pub fn held(v: &IoValue, ms: u32, lo: u8, hi: u8) -> bool {
    all_since(v, ms, |x| within(v, x, lo, hi))
}

/// 0x8265B018 (`heldUp`): every entry within the last `ms` out of the range.
pub fn held_up(v: &IoValue, ms: u32, lo: u8, hi: u8) -> bool {
    all_since(v, ms, |x| !within(v, x, lo, hi))
}

fn all_since(v: &IoValue, ms: u32, ok: impl Fn(u8) -> bool) -> bool {
    let h = hist(v);
    let t = since(v, h, ms);
    for i in 0..0x40 {
        let e = h[back(v.history_at, i)];
        if e.1 < t {
            return true;
        }
        if !ok(e.0) {
            return false;
        }
    }
    true
}

/// 0x8265B150 (`pressedAndHeld`): in the range this frame, and out of it at some entry within the last `ms` with
/// every one between in it.
pub fn pressed_and_held(v: &IoValue, ms: u32, lo: u8, hi: u8) -> bool {
    let h = hist(v);
    within(v, h[v.history_at as usize].0, lo, hi) && first_since(v, ms, |x| !within(v, x, lo, hi))
}

/// 0x8265B300 (`releasedAndHeld`): out of the range this frame, and in it at some entry within the last `ms` with
/// every one between out.
pub fn released_and_held(v: &IoValue, ms: u32, lo: u8, hi: u8) -> bool {
    let h = hist(v);
    !within(v, h[v.history_at as usize].0, lo, hi) && first_since(v, ms, |x| within(v, x, lo, hi))
}

/// Entries 1 .. 63 back while within the window: the first that `hit` takes, if one does (the rest must not).
fn first_since(v: &IoValue, ms: u32, hit: impl Fn(u8) -> bool) -> bool {
    let h = hist(v);
    let t = since(v, h, ms);
    for i in 1..0x40 {
        let e = h[back(v.history_at, i)];
        if e.1 < t {
            return false;
        }
        if hit(e.0) {
            return true;
        }
    }
    false
}

/// 0x8237B980 (`pressed`): some entry within the last `ms` out of the range with the one after it in.
pub fn pressed(v: &IoValue, ms: u32, lo: u8, hi: u8) -> bool {
    edge_since(v, ms, |before, after| !within(v, before, lo, hi) && within(v, after, lo, hi))
}

/// 0x8237BB50 (`released`): some entry within the last `ms` in the range with the one after it out.
pub fn released(v: &IoValue, ms: u32, lo: u8, hi: u8) -> bool {
    edge_since(v, ms, |before, after| within(v, before, lo, hi) && !within(v, after, lo, hi))
}

fn edge_since(v: &IoValue, ms: u32, edge: impl Fn(u8, u8) -> bool) -> bool {
    let h = hist(v);
    let t = since(v, h, ms);
    for i in 1..0x40 {
        let e = h[back(v.history_at, i)];
        if e.1 < t {
            return false;
        }
        if edge(e.0, h[back(v.history_at, i - 1)].0) {
            return true;
        }
    }
    false
}

/// A float to a byte as 0x8265B9F0 does: truncated to an integer, its low byte.
fn byte(x: f32) -> u8 {
    x as i64 as u8
}

impl Impulse {
    /// 0x8265B9F0 on controls `c`; `setting` is the controller setting 0x82BF9F24.
    pub fn test(&self, c: &Controls, setting: u32) -> bool {
        if !self.check {
            return true;
        }
        let v = |off: u32| &c.values[index(off)];
        let axis = |n: usize| v(pair_pick(AXIS_PAIRS[n], v(AXIS_PAIRS[n].0)));
        let (a, b) = match self.impulse {
            1 => (v(0xBDC), None),
            2 => (v(0xBE8), None),
            3 => (v(0xBF4), None),
            4 => (v(0xD14), None),
            5 => (v(0xC00), None),
            6 => (v(0xC0C), None),
            7 => (axis(0), None),
            8 => (axis(1), None),
            9 => (axis(0), Some(axis(1))),
            _ => return false,
        };
        let ms = (self.interval * 1000.0) as i64 as u32;
        let [r0, r1] = self.range;
        let (lo, hi) = match self.impulse {
            7 | 8 => (byte((r0 + 1.0) * 0.5 * 255.0), byte((r1 + 1.0) * 0.5 * 255.0)),
            9 => (byte(r0.abs() * 127.0), byte(r1.abs() * 127.0)),
            3 if setting == 0 => (byte(r0 * 255.0), byte(r1 * 255.0)),
            _ => (0x80, 0xFF),
        };
        let q: fn(&IoValue, u32, u8, u8) -> bool = match self.state {
            0 => return true,
            1 => held,
            2 => held_up,
            3 => pressed_and_held,
            4 => released_and_held,
            5 => pressed,
            6 => released,
            _ => return false,
        };
        match b {
            None => q(a, ms, lo, hi),
            Some(b) => {
                let (p, n) = ((lo.wrapping_add(0x80), hi.wrapping_add(0x80)), (0x7Fu8.wrapping_sub(hi), 0x7Fu8.wrapping_sub(lo)));
                q(a, ms, p.0, p.1) || q(a, ms, n.0, n.1) || q(b, ms, p.0, p.1) || q(b, ms, n.0, n.1)
            }
        }
    }
}

/// The impulse part of the action test 0x8265E798 (0x8265E860 ..): false without controls, else the two impulses
/// joined by the action's ImpulseOp (0 `or`, 1 `and`, 2 `xor`; others fail).
pub fn impulses_hold(a: &Action, c: Option<&Controls>, setting: u32) -> bool {
    let Some(c) = c else { return false };
    let [i0, i1] = &a.impulses;
    match a.impulse_op {
        0 => i0.test(c, setting) || i1.test(c, setting),
        1 => i0.test(c, setting) && i1.test(c, setting),
        2 => i1.test(c, setting) != i0.test(c, setting),
        _ => false,
    }
}

/// The weapon types by number, the names whose hashes 0x82A5C690 puts in 0x82CBCA70 (from 0x82A993E8).
pub const WEAPON_TYPES: [&str; 58] = [
    "UNARMED", "BASEBALLBAT", "POOLCUE", "KNIFE", "GRENADE", "MOLOTOV", "ROCKET", "PISTOL", "UNUSED0", "DEAGLE", "SHOTGUN",
    "BARETTA", "MICRO_UZI", "MP5", "AK47", "M4", "SNIPERRIFLE", "M40A1", "RLAUNCHER", "FTHROWER", "MINIGUN", "EPISODIC_1",
    "EPISODIC_2", "EPISODIC_3", "EPISODIC_4", "EPISODIC_5", "EPISODIC_6", "EPISODIC_7", "EPISODIC_8", "EPISODIC_9",
    "EPISODIC_10", "EPISODIC_11", "EPISODIC_12", "EPISODIC_13", "EPISODIC_14", "EPISODIC_15", "EPISODIC_16", "EPISODIC_17",
    "EPISODIC_18", "EPISODIC_19", "EPISODIC_20", "EPISODIC_21", "EPISODIC_22", "EPISODIC_23", "EPISODIC_24", "CAMERA",
    "OBJECT", "WEAPONTYPE_LAST_WEAPONTYPE", "ARMOUR", "RAMMEDBYCAR", "RUNOVERBYCAR", "EXPLOSION", "UZI_DRIVEBY",
    "DROWNING", "FALL", "UNIDENTIFIED", "ANYMELEE", "ANYWEAPON",
];

/// A weapon in hand as the action test reads it: its type (the weapon's +0x14) and its weaponinfo's flags (+0x20:
/// bit 5 GUN, 9 MELEE_CLUB, 10 MELEE_BLADE, 12 2HANDED) and timebetweenshots (+0x8C, ms).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Held {
    pub kind: u32,
    pub flags: u32,
    pub between_shots: i32,
}

/// A ped as 0x8265B648 reads it: the player byte (+0x211), health (+0x1E4), its model's attack, defend and melee
/// martial level (pedpersonality's, the model info's +0xF4, +0xF8, +0xFC) and the weapon in hand (none without one).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Fighter {
    pub player: bool,
    pub health: f32,
    pub attack: f32,
    pub defend: f32,
    pub martial: f32,
    pub weapon: Option<Held>,
}

/// 0x8265AAE0: whether a who (`all` 0, `player` 1, `ai` 2; actiontable's 0x8265A780) takes in this ped.
pub fn who_takes(who: u32, player: bool) -> bool {
    match who {
        0 => true,
        1 => player,
        2 => !player,
        _ => false,
    }
}

/// 0x8265B648, the ped part of the action test (on the action's +0x0C ..): AIPlayerfilter takes the ped in; each of
/// MustBeRecievingHit, MustBeBlocking, MustHaveJustStruckSomething set needs the caller's flag; health, attack,
/// defend and martial level within their ranges (NaN passes, as `fcmpu`); and WeaponClass fits the weapon in hand:
/// `none` (or 0) never, `any` always, `unarmed` none or type 0, `object` type 46, `knife` MELEE_BLADE, `club`
/// MELEE_CLUB, `oneHandedGun` GUN without 2HANDED (0x822E5CB8), `oneHandedMG` that with timebetweenshots under 200
/// (0x826598C0), `pistol` that without, `rifle` GUN and 2HANDED (0x822E5D08); another name, the type whose name
/// hashes to it ([`WEAPON_TYPES`], 0x82299AF8: 55 when none does). All but `unarmed` and `any` need a weapon.
pub fn ped_takes(a: &Action, f: &Fighter, receiving_hit: bool, blocking: bool, just_struck: bool) -> bool {
    if !who_takes(a.ai_player_filter, f.player)
        || a.flags[1] && !receiving_hit
        || a.flags[2] && !blocking
        || a.flags[3] && !just_struck
    {
        return false;
    }
    let stats = [f.health, f.attack, f.defend, f.martial];
    if stats.iter().zip(&a.ranges).any(|(x, r)| *x < r[0] || *x > r[1]) {
        return false;
    }
    let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
    let class = a.weapon_class;
    if class == 0 || class == crate::actiontable::none() {
        return false;
    }
    if class == h("any") {
        return true;
    }
    if class == h("unarmed") {
        return f.weapon.map_or(true, |w| w.kind == 0);
    }
    let Some(w) = f.weapon else { return false };
    let bit = |n: u32| w.flags >> n & 1 != 0;
    let one_handed = bit(5) && !bit(12);
    let mg = bit(5) && w.between_shots < 200;
    match class {
        c if c == h("object") => w.kind == 0x2E,
        c if c == h("knife") => bit(10),
        c if c == h("club") => bit(9),
        c if c == h("oneHandedGun") => one_handed,
        c if c == h("oneHandedMG") => one_handed && mg,
        c if c == h("pistol") => one_handed && !mg,
        c if c == h("rifle") => bit(5) && bit(12),
        c => w.kind == WEAPON_TYPES.iter().position(|t| h(t) == c).map_or(0x37, |i| i as u32),
    }
}

/// A ped's melee target as the action test reads it: its entity type (the entity's +0x28 bits 22..25: 3 ped, 2
/// vehicle) and where its points are (0x8265DCB0: by point type, actiontable's 0x8265A850 place, given where the
/// tester stands for the closest box points), None where the game finds none.
#[derive(Clone, Copy)]
pub struct Target<'a> {
    pub entity_type: u32,
    pub point: &'a dyn Fn(u32, [f32; 3]) -> Option<[f32; 3]>,
    /// A ped target's side of the opponent part (0x8265CBE8).
    pub opponent: Option<&'a Opponent<'a>>,
    /// What the SpecialTest reads of it (its "opponent" tests only for a ped, entity type 3).
    pub special: Special,
}

/// What the SpecialTest (0x82659CC8) and its moving-away test (0x822BFEB0) read of an entity.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Special {
    /// +0x14C bit 0x01: on stairs.
    pub on_stairs: bool,
    /// Its velocity (vf 0xE8; a ped's, vehicle's or object's, else 0 for the moving-away test).
    pub velocity: [f32; 3],
    /// +0x240: bit 27 SET_CHAR_READY_TO_BE_STUNNED, 26 SET_CHAR_READY_TO_BE_EXECUTED, 25 / 24 SET_CHAR_MELEE_ACTION_FLAG0
    /// / 1 (24 read as the low bit of the byte +0x240).
    pub flags_240: u32,
    /// +0x21C set, its CTaskComplexMelee (431, 0x823C0E88) found, and that task's +0xF0 bit 0x20 (0x822BE8A0): the move
    /// it plays started with no result playing (vf20 at 0x822C7048, from 0x822C0330).
    pub move_from_idle: bool,
    /// Where the moving-away test aims at it: a ped's root bone (0x4B2, 0x822FC390), else its matrix's position (+0x30;
    /// the entity's +0x10 without one); its forward (matrix +0x10; 0x822BFA98 from the heading without one).
    pub point: [f32; 3],
    pub forward: [f32; 3],
}

/// The SpecialTest names (0x82A663F0..: their hashes into 0x830B705C..0x830B70C0, in this order).
pub const SPECIAL_TESTS: [&str; 26] = [
    "onStairs", "notOnStairs", "isMoving", "notIsMoving", "isMovingFast", "notIsMovingFast", "isTargetMovingAway",
    "NotIsTargetMovingAway", "isTargetMovingAndFacingAway", "NotIsTargetMovingAndFacingAway", "isReadyToBeStunned",
    "opponentIsReadyToBeStunned", "isReadyToBeExecuted", "opponentIsReadyToBeExecuted", "checkMeleeActionFlag0",
    "checkNotMeleeActionFlag0", "opponentCheckMeleeActionFlag0", "opponentCheckNotMeleeActionFlag0", "checkMeleeActionFlag1",
    "checkNotMeleeActionFlag1", "opponentCheckMeleeActionFlag1", "opponentCheckNotMeleeActionFlag1",
    "isCurrentlyDoingAMoveFromIdle", "isNotCurrentlyDoingAMoveFromIdle", "OpponentIsCurrentlyDoingAMoveFromIdle",
    "OpponentIsNotCurrentlyDoingAMoveFromIdle",
];

/// isMoving's and isMovingFast's squared speeds (0x820BECD8, 0x82000D74).
const MOVING: f32 = 0.01;
const MOVING_FAST: f32 = 0.5;
/// The moving-away test's limit along the way and least speed (0x82A99D24, 0x82A99D28).
const AWAY: f32 = 0.707;
const AWAY_SPEED: f32 = 0.6;

/// A unit vector the vector unit's way (0x822BFF78, 0x822C12F4): the squares summed in double (vmsum3fp), the estimate
/// and one Newton step ([`crate::aiahead::vrsq`]), each lane times it. Zero stays zero.
pub(crate) fn vunit(d: [f32; 3]) -> [f32; 3] {
    let n = d.iter().fold(0.0f64, |s, &v| s + v as f64 * v as f64) as f32;
    let y = crate::aiahead::vrsq(n);
    d.map(|v| v * y)
}

/// 0x822BFEB0 (as the SpecialTest asks it: `AWAY`, `AWAY_SPEED`): `t` moving away from a ped at `from` (its matrix +0x30).
/// The way from there to its point, normalized; with `facing` its forward along that way past `AWAY` first; then its
/// velocity at least `AWAY_SPEED` and along the way past `AWAY` (past: not at or under it, so a NaN is past).
pub fn moving_away(from: [f32; 3], t: &Special, facing: bool) -> bool {
    let d = [t.point[0] - from[0], t.point[1] - from[1], t.point[2] - from[2]];
    let w = vunit(d);
    if facing {
        let f = t.forward;
        if f[1].mul_add(w[1], f[2].mul_add(w[2], f[0] * w[0])) <= AWAY {
            return false;
        }
    }
    let v = t.velocity;
    if v[0].mul_add(v[0], v[2].mul_add(v[2], v[1] * v[1])) < AWAY_SPEED * AWAY_SPEED {
        return false;
    }
    !(v[0].mul_add(w[0], v[2].mul_add(w[2], v[1] * w[1])) <= AWAY)
}

/// 0x82659CC8: the action's SpecialTest (`hash`, 0 for none: it holds) on the ped `me` at `from` and its target (entity
/// type and what it reads of it): one of [`SPECIAL_TESTS`] on the ped or, the "opponent" ones, on a ped target (none:
/// fails); any other hash fails. Its squared speed (x² + (y² + z²) fused) at least `MOVING` / `MOVING_FAST` for isMoving
/// / isMovingFast; the target moving away ([`moving_away`]; no target fails).
pub fn special_test(hash: u32, me: &Special, from: [f32; 3], target: Option<(u32, &Special)>) -> bool {
    if hash == 0 {
        return true;
    }
    let Some(i) = SPECIAL_TESTS.iter().position(|n| crate::map::hash_bytes(n.as_bytes(), 0) == hash) else { return false };
    let opp = target.filter(|t| t.0 == 3).map(|t| t.1);
    let v = me.velocity;
    let speed2 = v[0].mul_add(v[0], v[1].mul_add(v[1], v[2] * v[2]));
    let bit = |s: &Special, b: u32| s.flags_240 & b != 0;
    let on = |b: u32| opp.is_some_and(|o| bit(o, b));
    let off = |b: u32| opp.is_some_and(|o| !bit(o, b));
    let away = |facing: bool| target.is_some_and(|t| moving_away(from, t.1, facing));
    match i {
        0 => me.on_stairs,
        1 => !me.on_stairs,
        2 => speed2 >= MOVING,
        3 => !(MOVING <= speed2),
        4 => speed2 >= MOVING_FAST,
        5 => !(MOVING_FAST <= speed2),
        6 => away(false),
        7 => target.is_some() && !away(false),
        8 => away(true),
        9 => target.is_some() && !away(true),
        10 => bit(me, 0x800_0000),
        11 => on(0x800_0000),
        12 => bit(me, 0x400_0000),
        13 => on(0x400_0000),
        14 => bit(me, 0x200_0000),
        15 => !bit(me, 0x200_0000),
        16 => on(0x200_0000),
        17 => off(0x200_0000),
        18 => bit(me, 0x100_0000),
        19 => !bit(me, 0x100_0000),
        20 => on(0x100_0000),
        21 => off(0x100_0000),
        22 => me.move_from_idle,
        23 => !me.move_from_idle,
        24 => opp.is_some_and(|o| o.move_from_idle),
        _ => !opp.is_some_and(|o| o.move_from_idle),
    }
}

/// The SpecialResult names (0x82A5D080..: their hashes into 0x82D2BA6C..0x82D2BAD4, in this order).
pub const SPECIAL_RESULTS: [&str; 27] = [
    "DoExecutionStuff", "SetActionflagtrueReadytobeexecutedfalse", "SetOpponentActionflagtrueReadytobeexecutedfalse",
    "SetIsReadyToBeStunned", "UnsetIsReadyToBeStunned", "SetOpponentIsReadyToBeStunned", "UnsetOpponentIsReadyToBeStunned",
    "SetIsReadyToBeExecuted", "UnsetIsReadyToBeExecuted", "SetOpponentIsReadyToBeExecuted", "UnsetOpponentIsReadyToBeExecuted",
    "SetMeleeActionFlag0", "UnsetMeleeActionFlag0", "SetOpponentMeleeActionFlag0", "UnsetOpponentMeleeActionFlag0",
    "SetMeleeActionFlag1", "UnsetMeleeActionFlag1", "SetOpponentMeleeActionFlag1", "UnsetOpponentMeleeActionFlag1",
    "SetMeleeActionFlag2", "UnsetMeleeActionFlag2", "SetOpponentMeleeActionFlag2", "UnsetOpponentMeleeActionFlag2",
    "SetSlowMoGameTime", "UnsetSlowMoGameTime", "SetStopMovesHoming", "SetOpponentStopMovesHoming",
];

/// The time scale SetSlowMoGameTime puts in 0x82A98B2C (0x8201A6BC); UnsetSlowMoGameTime puts back 1.
pub const SLOW_MO: f32 = 0.2;

/// What a SpecialResult does (0x822BF5C0): +0x240 bits set and cleared on the owner and on its opponent ped (a ped
/// target only), the game's time scale, and whose running result's window (+0x5E 0x80, its homing) shuts.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SpecialEffect {
    pub me: (u32, u32),
    pub opponent: (u32, u32),
    pub slow_mo: Option<f32>,
    pub stop_homing: bool,
    pub stop_opponent_homing: bool,
}

impl SpecialEffect {
    /// A word of +0x240 bits after `(set, clear)`: cleared, then set, as the game's `& !clear | set`.
    pub fn apply((set, clear): (u32, u32), w: u32) -> u32 {
        w & !clear | set
    }
}

/// 0x822BF5C0: the result's SpecialResult `hash` (0, none: nothing) as [`SpecialEffect`]. DoExecutionStuff sets the
/// owner's flag 0 and clears the opponent's ready-to-be-executed; the two "Actionflagtrue..." clear flag 0 and
/// ready-to-be-executed, then set flag 0 (`& 0xF9FFFFFF | 0x2000000`), on the owner or the opponent; the rest set or
/// clear one bit (27 stunned, 26 executed, 25 / 24 / 23 flags 0 / 1 / 2) on the owner or the opponent. The caller runs
/// it once per result (+0x5D 0x80, set after; 0x822C4768).
pub fn special_result(hash: u32) -> SpecialEffect {
    let mut e = SpecialEffect::default();
    if hash == 0 {
        return e;
    }
    let Some(i) = SPECIAL_RESULTS.iter().position(|n| crate::map::hash_bytes(n.as_bytes(), 0) == hash) else { return e };
    const BITS: [u32; 5] = [0x800_0000, 0x400_0000, 0x200_0000, 0x100_0000, 0x80_0000];
    match i {
        0 => (e.me, e.opponent) = ((0x200_0000, 0), (0, 0x400_0000)),
        1 => e.me = (0x200_0000, 0x600_0000),
        2 => e.opponent = (0x200_0000, 0x600_0000),
        3..=22 => {
            let (bit, k) = (BITS[(i - 3) / 4], (i - 3) % 4);
            let (set, clear) = if k % 2 == 0 { (bit, 0) } else { (0, bit) };
            if k < 2 { e.me = (set, clear) } else { e.opponent = (set, clear) }
        }
        23 => e.slow_mo = Some(SLOW_MO),
        24 => e.slow_mo = Some(1.0),
        25 => e.stop_homing = true,
        _ => e.stop_opponent_homing = true,
    }
    e
}

/// A target ped as the opponent part 0x8265CBE8 reads it.
#[derive(Clone, Copy)]
pub struct Opponent<'a> {
    /// Its melee action result task (433, 0x82159078), none without one: that task's result's hash (0x823DF1D0; 0
    /// for none) and its clip on anim layer 3 (0x82445560): the time and this frame's step (0x82454250, 0x82454280).
    pub task: Option<(u32, Option<(f32, f32)>)>,
    /// How it lies (0x822FCB78, `fall::lies`: 4 upright, 0 face, 1 right side, 2 back, 3 left side), crouched
    /// (0x822F03C0).
    pub lies: u8,
    pub crouched: bool,
    /// The test of OpponentsActionToTest from its side: itself as the fighter, impulses for a player; a player's
    /// melee task gives the target, lock-on and the three flags, an AI's none of those (its target is the tester) but
    /// blocking when its result IsABlock (when its +0x68's +0x0E is set).
    pub ask: &'a Ask<'a>,
}

/// What the action test 0x8265E798 is asked with, besides the action.
#[derive(Clone, Copy)]
pub struct Ask<'a> {
    pub fighter: Fighter,
    /// The ped's model's (the player's: 0x830B7028's) seven melee anim groups (pedpersonality +0x100), as anim
    /// group store places.
    pub groups: [i32; 7],
    /// Whether an anim group's dictionary is in (0x82248DB8 its slot, 0x821D0310 that slot's state 1), by store place.
    pub loaded: &'a dyn Fn(i32) -> bool,
    /// r10, r1+0x147, r1+0x14F: the ped part's three caller flags (0x8265B648).
    pub receiving_hit: bool,
    pub blocking: bool,
    pub just_struck: bool,
    /// r1+0x154 the target, r1+0x15F locked on.
    pub target: Option<Target<'a>>,
    pub lock_on: bool,
    /// r1+0x167 test the impulses; r1+0x16C the player's controls (0x823CF688; none for others), and the
    /// controller setting 0x82BF9F24.
    pub impulses: bool,
    pub controls: Option<&'a Controls>,
    pub setting: u32,
    /// r4/r5: Type must be this hash; r6/r7: AvailableFromIdle must be this.
    pub kind: Option<u32>,
    pub idle: Option<bool>,
    /// The ped's position (matrix +0x30) and heading (0x82147E90).
    pub pos: [f32; 3],
    pub heading: f32,
    /// r9: the opponent's result, when the caller knows it (a hit reaction's pick: the blow's result), else 0.
    pub opponent_res: u32,
    /// What the SpecialTest (0x82659CC8, +0xC0; 8 rows name one) reads of the ped ([`special_test`]).
    pub special: Special,
}

/// 0x8265C3F8: the action's Move result's anim group must be in (`loaded`); for an action for `none` or `unarmed`
/// whose Type isn't `HitReaction` it must also be one of the ped's melee groups, and each of those before it in
/// (0x8265C4E8 asks the store for each, the action's group's sign guarding all). No Move, or a Move with no group,
/// is in. The Move is the last result of that name (0x826603B8).
pub fn anim_in(a: &Action, results: &[ResultDef], groups: &[i32; 7], loaded: &dyn Fn(i32) -> bool) -> bool {
    let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
    if a.move_ == 0 {
        return true;
    }
    let Some(r) = results.iter().rev().find(|r| r.name == a.move_) else { return true };
    if r.anim_group == -1 {
        return true;
    }
    if a.weapon_class != crate::actiontable::none() && a.weapon_class != h("unarmed") || a.kind == h("HitReaction") {
        return loaded(r.anim_group);
    }
    let Some(i) = groups.iter().position(|&g| g == r.anim_group) else { return false };
    loaded(r.anim_group) && groups[..i].iter().all(|&g| loaded(g))
}

/// 0x8265AB48: whether a target is of RequiredTargetType: entity points (0, 1) any, ped points (2 ..= 7) a ped,
/// vehicle points (8 ..= 15) a vehicle; no target, or another type, never.
pub fn target_fits(kind: u32, t: Option<&Target>) -> bool {
    let Some(t) = t else { return false };
    match kind {
        0 | 1 => true,
        2..=7 => t.entity_type == 3,
        8..=15 => t.entity_type == 2,
        _ => false,
    }
}

/// 0x82659910: `x` (wrapped) in an angle range; one with lo above hi goes the other way round.
pub fn angle_in(x: f32, r: [f32; 2]) -> bool {
    let x = crate::actiontable::wrap(x);
    if r[0] > r[1] {
        x >= r[0] || x <= r[1]
    } else {
        !(x < r[0] || x > r[1])
    }
}

/// 0x8265CBE8, the opponent part: each of RequiredOpponentRes, CheckOpponentAnimTime, RequiredOpponentPose (not
/// `any`) and OpponentsActionToTest set needs a ped target. RequiredOpponentRes against the caller's opponent result
/// when there is one, else the target's melee task's (none: only a `!` one holds), `!` turning it round.
/// OpponentAnimTime: the task and its clip, the time in the range, or before it with its start inside this frame's
/// step. Poses: `none` fails, `standing` upright, `crouched`, `onGround` any of the four lyings, `onFront` 0, `onRHS`
/// 1, `onBack` 2, `onLHS` 3. OpponentsActionToTest: that action tested from the target's side (0x82660170).
pub fn opponent_holds(a: &Action, t: &ActionTable, ask: &Ask) -> bool {
    let o = ask.target.and_then(|t| t.opponent).filter(|_| ask.target.is_some_and(|t| t.entity_type == 3));
    if a.opponent_res != 0 {
        let Some(o) = o else { return false };
        let cur = if ask.opponent_res != 0 {
            ask.opponent_res
        } else {
            match o.task {
                Some((r, _)) => r,
                None if a.opponent_res_not => 0,
                None => return false,
            }
        };
        if ask.opponent_res != 0 || o.task.is_some() {
            if (cur == a.opponent_res) == a.opponent_res_not {
                return false;
            }
        }
    }
    if a.check_opponent_anim_time {
        let Some((t, step)) = o.and_then(|o| o.task).and_then(|t| t.1) else { return false };
        let [lo, hi] = a.opponent_anim_time;
        let d = if t >= lo { t - hi } else { lo - (step + t) };
        if d >= 0.0 {
            return false;
        }
    }
    if a.opponent_pose != 1 {
        let Some(o) = o else { return false };
        let ok = match a.opponent_pose {
            2 => o.lies == 4,
            3 => o.crouched,
            4 => o.lies <= 3,
            5..=8 => o.lies as u32 == a.opponent_pose - 5,
            _ => false,
        };
        if !ok {
            return false;
        }
    }
    if a.opponents_action_to_test == 0 {
        return true;
    }
    let Some(o) = o else { return false };
    let Some(b) = t.actions.iter().find(|b| b.name == a.opponents_action_to_test) else { return false };
    action_test(b, t, &Ask { kind: None, idle: None, opponent_res: 0, ..*o.ask })
}

/// 0x8265C9D0: the point of a box nearest `p`, and the way out of the box there (a unit vector, or zero). The box is
/// `min` / `max` in the frame of `rows` (right, forward, up, position: a matrix's four rows); `p` goes into that frame
/// (less the position, dotted with each axis: vmsum3fp, the three products summed in double onto +0 and rounded once), then
/// [`box_point`], then back (each world lane the four products of the point (w 1, the 0x831AB6F0 / 0x831AB6D0 lanes)
/// and the rows' column summed in double, vmsum4fp; the way by the three axes, vmsum3fp).
/// For the point type ClosestBoxPoint (0x8265DCB0 types 1 and 3) on a ped's box (0x822F95C0: its fragment's bound's
/// box, +0x704 -> +4 -> +0xC, +0x10 / +0x20; else its vf 0x68 box).
pub fn closest_box_point(rows: [[f32; 3]; 4], min: [f32; 3], max: [f32; 3], p: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    let d = [p[0] - rows[3][0], p[1] - rows[3][1], p[2] - rows[3][2]];
    let dot3 = |a: [f32; 3], b: [f32; 3]| (((0.0 + a[0] as f64 * b[0] as f64) + a[1] as f64 * b[1] as f64) + a[2] as f64 * b[2] as f64) as f32;
    let local = [dot3(rows[0], d), dot3(rows[1], d), dot3(rows[2], d)];
    let (at, way) = box_point(min, max, local);
    let col = |c: usize| [rows[0][c], rows[1][c], rows[2][c]];
    let world = |c: usize| ((((0.0 + at[0] as f64 * rows[0][c] as f64) + at[1] as f64 * rows[1][c] as f64) + at[2] as f64 * rows[2][c] as f64)
                            + rows[3][c] as f64) as f32;
    ([world(0), world(1), world(2)], [dot3(way, col(0)), dot3(way, col(1)), dot3(way, col(2))])
}

/// 0x8265AC18: the point of the box `min` / `max` nearest `p` (all in the box's frame) and the way out of it there.
/// Outside (on any axis below min or above max) each axis past a side is held to it and that side's way (+-1) added
/// up, the sum made a unit vector ([`crate::aiahead::vrsq`]: vrsqrtefp, an infinite estimate taken as 0, one Newton
/// step). Inside, on each axis the side the point is nearer (at or past the centre, (min + max) x (vrefp(2) and one
/// Newton step), the max side) and how far it is; each axis whose distance is no more than both others' goes onto its
/// side, the way that side's (the last such axis's).
fn box_point(min: [f32; 3], max: [f32; 3], p: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    let mut out = p;
    let mut way = [0.0f32; 3];
    if (0..3).any(|i| min[i] > p[i] || p[i] > max[i]) {
        for i in 0..3 {
            if p[i] > max[i] {
                out[i] = max[i];
                way[i] += 1.0;
            }
            if p[i] < min[i] {
                out[i] = min[i];
                way[i] += -1.0;
            }
        }
        let n = (((0.0 + way[0] as f64 * way[0] as f64) + way[1] as f64 * way[1] as f64) + way[2] as f64 * way[2] as f64) as f32;
        let y = crate::aiahead::vrsq(n);
        return (out, way.map(|w| w * y));
    }
    let est = crate::xenon::vrefp(2.0);
    let e = (1.0 - est as f64 * 2.0) as f32;
    let half = (est as f64 * e as f64 + est as f64) as f32;
    let hi: [bool; 3] = std::array::from_fn(|i| !(p[i] < (min[i] + max[i]) * half));
    let d: [f32; 3] = std::array::from_fn(|i| if hi[i] { max[i] - p[i] } else { p[i] - min[i] });
    for i in 0..3 {
        let (a, b) = ((i + 1) % 3, (i + 2) % 3);
        if !(d[i] > d[a]) && !(d[i] > d[b]) {
            out[i] = if hi[i] { max[i] } else { min[i] };
            way = [0.0; 3];
            way[i] = if hi[i] { 1.0 } else { -1.0 };
        }
    }
    (out, way)
}

/// 0x8265E728 / 0x8265E1D0: CheckRelPos off holds; else the target's point (+0xA4) less the ped's position must be
/// in HeightRange (z), DistanceRange (flat length) and AngleRange (wrap(wrap(atan2(-x, y)) - wrap(heading))).
/// Comparisons as `fcmpu`: NaN passes the height and distance ranges.
pub fn rel_pos_holds(a: &Action, ask: &Ask) -> bool {
    if !a.check_rel_pos {
        return true;
    }
    let Some(t) = &ask.target else { return false };
    let Some(p) = (t.point)(a.rel_point, ask.pos) else { return false };
    let d = [p[0] - ask.pos[0], p[1] - ask.pos[1], p[2] - ask.pos[2]];
    let out = |x: f32, r: [f32; 2]| x < r[0] || x > r[1];
    if out(d[2], a.height_range) || out(d[1].mul_add(d[1], d[0] * d[0]).sqrt(), a.distance_range) {
        return false;
    }
    let w = crate::actiontable::wrap;
    let dir = w((-d[0] as f64).atan2(d[1] as f64) as f32);
    angle_in(dir - w(ask.heading), a.angle_range)
}

/// The action test 0x8265E798 on `a` from the table `t`: Type and AvailableFromIdle when asked, the Move's anim group
/// ([`anim_in`]), the ped part ([`ped_takes`]), the impulses when asked ([`impulses_hold`]), RequireTarget and
/// RequireLockOn (each by [`who_takes`]), then with a target (or one required) its type ([`target_fits`]), the
/// opponent and the relative position ([`rel_pos_holds`]); the special test; and AdditionalActionTest: that action
/// (none found fails) tested the same way but without Type and AvailableFromIdle, impulses only for a player
/// (0x82660170).
pub fn action_test(a: &Action, t: &ActionTable, ask: &Ask) -> bool {
    if ask.kind.is_some_and(|k| a.kind != k) || ask.idle.is_some_and(|i| a.available_from_idle != i) {
        return false;
    }
    if !anim_in(a, &t.results, &ask.groups, ask.loaded)
        || !ped_takes(a, &ask.fighter, ask.receiving_hit, ask.blocking, ask.just_struck)
        || ask.impulses && !impulses_hold(a, ask.controls, ask.setting)
    {
        return false;
    }
    let player = ask.fighter.player;
    let needs = who_takes(a.require_target, player);
    if needs && ask.target.is_none() || who_takes(a.require_lock_on, player) && !ask.lock_on {
        return false;
    }
    if (needs || ask.target.is_some())
        && !(target_fits(a.required_target_type, ask.target.as_ref()) && opponent_holds(a, t, ask) && rel_pos_holds(a, ask))
    {
        return false;
    }
    if !special_test(a.special_test, &ask.special, ask.pos, ask.target.as_ref().map(|t| (t.entity_type, &t.special))) {
        return false;
    }
    if a.additional_action_test == 0 {
        return true;
    }
    let Some(b) = t.actions.iter().find(|b| b.name == a.additional_action_test) else { return false };
    action_test(b, t, &Ask { kind: None, idle: None, impulses: ask.controls.is_some(), ..*ask })
}

/// The C runtime's `qsort` (0x82A01A20): below 9 elements the max to the end, else median of three and a partition
/// that skips runs equal to the pivot, the smaller side first. Its order among equal keys is the game's.
pub fn crt_qsort<T>(v: &mut [T], cmp: impl Fn(&T, &T) -> i32) {
    if v.len() < 2 {
        return;
    }
    let c = |v: &[T], a: isize, b: isize| cmp(&v[a as usize], &v[b as usize]);
    let mut stack = vec![];
    let (mut lo, mut hi) = (0isize, v.len() as isize - 1);
    loop {
        let size = hi - lo + 1;
        if size <= 8 {
            let mut h = hi;
            while h > lo {
                let mut max = lo;
                for p in lo + 1..=h {
                    if c(v, p, max) > 0 {
                        max = p;
                    }
                }
                v.swap(max as usize, h as usize);
                h -= 1;
            }
        } else {
            let mut mid = lo + size / 2;
            for (a, b) in [(lo, mid), (lo, hi), (mid, hi)] {
                if c(v, a, b) > 0 {
                    v.swap(a as usize, b as usize);
                }
            }
            let (mut lg, mut hg) = (lo, hi);
            loop {
                if mid > lg {
                    loop {
                        lg += 1;
                        if !(lg < mid && c(v, lg, mid) <= 0) {
                            break;
                        }
                    }
                }
                if mid <= lg {
                    loop {
                        lg += 1;
                        if !(lg <= hi && c(v, lg, mid) <= 0) {
                            break;
                        }
                    }
                }
                loop {
                    hg -= 1;
                    if !(hg > mid && c(v, hg, mid) > 0) {
                        break;
                    }
                }
                if hg < lg {
                    break;
                }
                v.swap(lg as usize, hg as usize);
                if mid == hg {
                    mid = lg;
                }
            }
            hg += 1;
            if mid < hg {
                loop {
                    hg -= 1;
                    if !(hg > mid && c(v, hg, mid) == 0) {
                        break;
                    }
                }
            }
            if mid >= hg {
                loop {
                    hg -= 1;
                    if !(hg > lo && c(v, hg, mid) == 0) {
                        break;
                    }
                }
            }
            if hg - lo >= hi - lg {
                if lo < hg {
                    stack.push((lo, hg));
                }
                if lg < hi {
                    lo = lg;
                    continue;
                }
            } else {
                if lg < hi {
                    stack.push((lg, hi));
                }
                if lo < hg {
                    hi = hg;
                    continue;
                }
            }
        }
        match stack.pop() {
            Some(p) => (lo, hi) = p,
            None => return,
        }
    }
}

/// 0x8265C7E0: of the actions that passed, none, the one, or: sorted by Priority, highest first ([`crt_qsort`] by
/// b - a), those below the first's (unsigned) dropped, then the first whose running sum of Bias reaches
/// `rand` x 1/32767 (the float 0x38000100) x the sum (index 0 when that is not above 0). `rand` (0x829FFB80) is drawn only then.
pub fn pick<'t>(mut hits: Vec<&'t Action>, rand: impl FnOnce() -> u32) -> Option<&'t Action> {
    if hits.len() < 2 {
        return hits.first().copied();
    }
    crt_qsort(&mut hits, |a, b| b.priority.wrapping_sub(a.priority));
    let top = hits[0].priority as u32;
    hits.retain(|a| a.priority as u32 >= top);
    if hits.len() == 1 {
        return Some(hits[0]);
    }
    let sum = hits.iter().fold(0.0f32, |s, a| a.bias + s);
    let r = rand() as f32 * f32::from_bits(0x3800_0100) * sum;
    let mut i = 0;
    if r > 0.0 {
        let mut acc = 0.0f32;
        // The game has no end test, none needed: the last running sum is `sum` bit for bit (the same adds, same order),
        // and r = x * sum with x at most 32767 x 0x38000100, which rounds to 1.0, so r <= sum.
        loop {
            acc = hits[i].bias + acc;
            if !(acc < r) || i == hits.len() - 1 {
                break;
            }
            i += 1;
        }
    }
    Some(hits[i])
}

/// 0x8265FF00: every action of the table tested in order ([`action_test`]); none passing, all again without the
/// target and lock-on; then the [`pick`].
pub fn choose<'t>(t: &'t ActionTable, ask: &Ask, rand: impl FnOnce() -> u32) -> Option<&'t Action> {
    let mut hits: Vec<&Action> = t.actions.iter().filter(|a| action_test(a, t, ask)).collect();
    if hits.is_empty() {
        let bare = Ask { target: None, lock_on: false, ..*ask };
        hits = t.actions.iter().filter(|a| action_test(a, t, &bare)).collect();
    }
    pick(hits, rand)
}

/// A landed blow's reaction (the damage dispatcher 0x8226C9A8's melee part, 0x8226CDC8..0x8226CF1C): the victim's
/// HitReaction action and its Move result, and whether NaturalMotion takes the hit instead.
#[derive(Clone, Copy, Debug, Default)]
pub struct Reaction<'t> {
    pub action: Option<&'t Action>,
    pub result: Option<&'t ResultDef>,
    pub ragdoll: bool,
}

/// 0x8226CDC8: the HitReaction pick for the victim (0x8265FF00 with Type "HitReaction" 0x82009558, AvailableFromIdle
/// 1, ReceivingHit, not JustStruck; `ask` holds the victim, its block, the attacker as the target, the victim's lock-on
/// on him and the attacker's result as `opponent_res`). Then, when the NM gate (0x826174F0, reason 1, scalar 0) said
/// yes (`gate`), NM takes it (0x8226CF1C) when the action prefers the ragdoll (+0xD0); else, with `low` its health
/// under 125 (0x8200943C) and `special` the result's SpecialResult not none (0x820BDD00): a victim running task 1900
/// without 1904 (the task tree test 0x82159078 at its +0x2E0) goes by `low` alone, any other by `!special || low`.
/// No action picked skips the PreferRagdoll and SpecialResult reads (0x8226CE70 branches to the task tests at
/// 0x8226CEC0 with `special` false), so it is NM's on the same terms; not NM's, no action means no reaction task
/// (0x8226D664 skips the CTaskComplexMelee).
pub fn hit_reaction<'t>(t: &'t ActionTable, ask: &Ask, gate: bool, tasks: [bool; 2], rand: impl FnOnce() -> u32) -> Reaction<'t> {
    let ask = Ask { kind: Some(crate::map::hash_bytes(b"HitReaction", 0)), idle: Some(true), receiving_hit: true, just_struck: false, ..*ask };
    let action = choose(t, &ask, rand);
    // The action's resolved result (+0xCC): its Move's last row of that name (0x826603B8).
    let result = action.filter(|a| a.move_ != 0).and_then(|a| t.results.iter().rfind(|r| r.name == a.move_));
    let special = result.is_some_and(|r| r.special_result != crate::actiontable::none());
    let low = ask.fighter.health < 125.0;
    let ragdoll = gate
        && (action.is_some_and(|a| a.prefer_ragdoll) || if tasks[0] && !tasks[1] { low } else { !special || low });
    Reaction { action, result, ragdoll }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x8265C3F8: an unarmed action's group must be the ped's and in, with each of the ped's groups before it in; a
    /// weapon's (or a HitReaction) only its own group in.
    #[test]
    fn anim_group_in() {
        let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
        let results = [ResultDef { name: h("jab"), anim_group: 5, ..Default::default() }];
        let unarmed = Action { move_: h("jab"), weapon_class: h("unarmed"), ..Default::default() };
        let armed = Action { weapon_class: h("bat"), ..unarmed.clone() };
        let groups = [3, 5, -1, -1, -1, -1, -1];
        let all = |_: i32| true;
        let not3 = |g: i32| g != 3;
        let not5 = |g: i32| g != 5;
        assert!(anim_in(&unarmed, &results, &groups, &all));
        assert!(!anim_in(&unarmed, &results, &groups, &not3), "a group before it out");
        assert!(!anim_in(&unarmed, &results, &groups, &not5));
        assert!(!anim_in(&unarmed, &results, &[3, -1, -1, -1, -1, -1, -1], &all), "not the ped's");
        assert!(anim_in(&armed, &results, &[-1; 7], &not3) && !anim_in(&armed, &results, &[-1; 7], &not5));
    }

    /// A control whose last frames, oldest first, held `values`, 33 ms apart, now at 1000 ms.
    fn ctl(values: &[u8]) -> IoValue {
        let mut h = vec![(0u8, 0u32); 64];
        let n = values.len();
        for (k, &x) in values.iter().enumerate() {
            h[(k + 64 - n + 5) % 64] = (x, 1000 - 33 * (n - 1 - k) as u32);
        }
        IoValue { history_at: 4, history: Some(h), ..Default::default() }
    }

    #[test]
    fn the_box_point() {
        let rows = [[0.0, 1.0, 0.0], [-1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [10.0, 0.0, 1.0]];
        let (min, max) = ([-0.3, -1.0, -0.9], [0.3, 0.8, -0.6]);
        // Beside a body lying along its frame's y: held to its side and its top, the way out between them.
        let (at, way) = closest_box_point(rows, min, max, [10.0, 1.0, 2.0]);
        assert!(at.iter().zip([10.0, 0.3, 0.4]).all(|(a, b)| (a - b).abs() < 1e-6), "{at:?}");
        assert!(way[0].abs() < 1e-6 && (way[1] - way[2]).abs() < 1e-3 && (way[1] * way[1] + way[2] * way[2] - 1.0).abs() < 1e-3);
        // Inside: onto the nearest side (here its top, 0.05 off).
        let (at, way) = box_point(min, max, [0.0, 0.0, -0.65]);
        assert_eq!((at, way), ([0.0, 0.0, -0.6], [0.0, 0.0, 1.0]));
        // A tie goes onto both sides; the way is the last axis's.
        let (at, way) = box_point([-1.0; 3], [1.0; 3], [0.5, 0.5, 0.0]);
        assert_eq!((at, way), ([1.0, 1.0, 0.0], [0.0, 1.0, 0.0]));
    }

    #[test]
    fn a_pick() {
        let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
        let any = h("any");
        let act = |name: &str, priority: i32, bias: f32| Action {
            name: h(name), priority, bias, weapon_class: any, ranges: [[0.0, 1000.0]; 4], available_from_idle: true,
            opponent_pose: 1, check_rel_pos: true, required_target_type: 2, rel_point: 2, height_range: [-2.0, 2.0],
            distance_range: [0.9, 1.4], angle_range: [-1.0, 1.0], require_target: 3, require_lock_on: 3, ..Default::default()
        };
        let mut t = ActionTable { actions: vec![act("a", 5, 1.0), act("b", 5, 3.0), act("c", 2, 9.0)], ..Default::default() };
        t.actions[2].check_rel_pos = false;
        let ahead = |_: u32, p: [f32; 3]| Some([p[0], p[1] + 1.2, p[2]]);
        let behind = |_: u32, p: [f32; 3]| Some([p[0], p[1] - 1.2, p[2]]);
        let special = Special::default();
        let o = Ask { fighter: Fighter { health: 200.0, attack: 1.0, defend: 1.0, martial: 1.0, ..Default::default() },
                      groups: [-1; 7], loaded: &|_| true, receiving_hit: false, blocking: false, just_struck: false, target: None, lock_on: false,
                      impulses: false, controls: None, setting: 0, kind: None, idle: Some(true), pos: [0.0; 3], heading: 0.0,
                      opponent_res: 0, special };
        let opp = Opponent { task: None, lies: 4, crouched: false, ask: &o };
        let ask = Ask { target: Some(Target { entity_type: 3, point: &ahead, opponent: Some(&opp), special }), ..o };
        // a and b tie on priority 5 (c is dropped); the sort leaves them b, a (the short sort moves the first max to
        // the end), and the draw over bias 3 + 1 picks a past three quarters.
        assert_eq!(choose(&t, &ask, || 0).unwrap().name, h("b"));
        assert_eq!(choose(&t, &ask, || 0x7000).unwrap().name, h("a"));
        // Behind him a and b fail the angle, so c alone.
        let back = Ask { target: Some(Target { point: &behind, ..ask.target.unwrap() }), ..ask };
        assert_eq!(choose(&t, &back, || unreachable!()).unwrap().name, h("c"));
        // A standing pose needs him upright: with him down all three fail, so the second pass goes without the
        // target, where none is asked about and all three pass again; with a target required, nothing.
        t.actions[2].opponent_pose = 2;
        let down = Opponent { lies: 2, ..opp };
        let floor = Ask { target: Some(Target { opponent: Some(&down), ..back.target.unwrap() }), ..back };
        assert!(!action_test(&t.actions[2], &t, &floor));
        assert_eq!(choose(&t, &floor, || 0).unwrap().name, h("b"));
        t.actions.iter_mut().for_each(|a| a.require_target = 0);
        assert!(choose(&t, &floor, || 0).is_none());
        assert!(angle_in(3.0, [2.5, -2.5]) && !angle_in(0.0, [2.5, -2.5]));
    }

    #[test]
    fn hit_reactions() {
        let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
        let any = h("any");
        let act = |name: &str, res: &str, opp: &str| Action {
            name: h(name), priority: 5, bias: 1.0, kind: h("HitReaction"), weapon_class: any, ranges: [[0.0, 1000.0]; 4],
            available_from_idle: true, flags: [false, true, false, false], opponent_res: h(opp),
            opponent_pose: 1, move_: h(res), require_lock_on: 3, ..Default::default()
        };
        let res = |name: &str, special: &str| ResultDef { name: h(name), special_result: h(special), anim_group: -1, anim: -1, ..Default::default() };
        let t = ActionTable {
            actions: vec![act("hit_jab", "jab_res", "jab"), act("hit_hook", "hook_res", "hook")],
            results: vec![res("jab_res", "none"), res("hook_res", "slowmo")],
            ..Default::default()
        };
        let special = Special::default();
        let ask = Ask { fighter: Fighter { health: 200.0, attack: 1.0, defend: 1.0, martial: 1.0, ..Default::default() },
                        groups: [-1; 7], loaded: &|_| true, receiving_hit: false, blocking: false, just_struck: false, target: None, lock_on: false,
                        impulses: false, controls: None, setting: 0, kind: None, idle: None, pos: [0.0; 3], heading: 0.0,
                        opponent_res: h("jab"), special };
        // The attacker, a ped (entity type 3), as the target.
        let at = |_: u32, p: [f32; 3]| Some(p);
        let opp = Opponent { task: None, lies: 4, crouched: false, ask: &ask };
        let ask = Ask { target: Some(Target { entity_type: 3, point: &at, opponent: Some(&opp), special }), ..ask };
        // The jab's reaction: no special result, so NM's on a yes, but not when the gate says no.
        let r = hit_reaction(&t, &ask, true, [false, false], || 0);
        assert_eq!((r.action.unwrap().name, r.result.unwrap().name, r.ragdoll), (h("hit_jab"), h("jab_res"), true));
        assert!(!hit_reaction(&t, &ask, false, [false, false], || 0).ragdoll);
        // In combat (1900 without 1904) only under 125 health.
        assert!(!hit_reaction(&t, &ask, true, [true, false], || 0).ragdoll);
        let weak = Ask { fighter: Fighter { health: 120.0, ..ask.fighter }, ..ask };
        assert!(hit_reaction(&t, &weak, true, [true, false], || 0).ragdoll);
        // The hook's has a special result: on its feet, unless it prefers the ragdoll.
        let hook = Ask { opponent_res: h("hook"), ..ask };
        assert!(!hit_reaction(&t, &hook, true, [false, false], || 0).ragdoll);
        let mut t2 = t.clone();
        t2.actions[1].prefer_ragdoll = true;
        assert!(hit_reaction(&t2, &hook, true, [true, false], || 0).ragdoll);
        // No action for this blow (each needs the target, so the pass without it finds none): no result, so NM's.
        let r = hit_reaction(&t, &Ask { opponent_res: h("kick"), ..ask }, true, [false, false], || 0);
        assert!(r.action.is_none() && r.ragdoll);
    }

    #[test]
    fn queries() {
        let tap = ctl(&[0, 0, 0xFF, 0xFF]);
        assert!(pressed(&tap, 100, 0x80, 0xFF) && pressed_and_held(&tap, 100, 0x80, 0xFF));
        assert!(!pressed(&tap, 20, 0x80, 0xFF) && !held(&tap, 100, 0x80, 0xFF) && held(&tap, 40, 0x80, 0xFF));
        assert!(!released(&tap, 1000, 0x80, 0xFF) && held_up(&ctl(&[0, 0]), 1000, 0x80, 0xFF));
        let up = ctl(&[0xFF, 0]);
        assert!(released(&up, 50, 0xFF, 0x80) && released_and_held(&up, 50, 0x80, 0xFF));

        let mut c = Controls::default();
        c.values[index(0xBDC)] = tap;
        let attack = Impulse { check: true, impulse: 1, state: 5, range: [0.0, 1.0], interval: 0.1 };
        let block = Impulse { impulse: 6, ..attack };
        c.values[index(0xC0C)] = ctl(&[0, 0]);
        let mut a = Action { impulses: [attack, block], impulse_op: 0, ..Default::default() };
        assert!(impulses_hold(&a, Some(&c), 0) && !impulses_hold(&a, None, 0));
        a.impulse_op = 1;
        assert!(!impulses_hold(&a, Some(&c), 0));
        a.impulse_op = 2;
        assert!(impulses_hold(&a, Some(&c), 0));
        a.impulses[1].check = false;
        assert!(!impulses_hold(&a, Some(&c), 0));
    }

    #[test]
    fn ped() {
        let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
        let mut a = Action { ai_player_filter: 1, weapon_class: h("unarmed"), ..Default::default() };
        a.ranges = [[0.0, 500.0], [0.0, 10.0], [0.0, 10.0], [0.0, 100.0]];
        let mut f = Fighter { player: true, health: 200.0, attack: 1.0, defend: 1.0, martial: 50.0, weapon: None };
        assert!(ped_takes(&a, &f, false, false, false));
        f.player = false;
        assert!(!ped_takes(&a, &f, false, false, false));
        a.ai_player_filter = 0;
        a.flags[2] = true;
        assert!(!ped_takes(&a, &f, false, false, false) && ped_takes(&a, &f, false, true, false));
        let glock = Held { kind: 7, flags: 0x20, between_shots: 250 };
        f.weapon = Some(glock);
        assert!(!ped_takes(&a, &f, false, true, false));
        for (class, ok) in [("pistol", true), ("oneHandedMG", false), ("rifle", false), ("PISTOL", true), ("none", false)] {
            a.weapon_class = h(class);
            assert_eq!(ped_takes(&a, &f, false, true, false), ok, "{class}");
        }
    }

    #[test]
    fn the_special_tests() {
        let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
        let t = |name: &str, me: &Special, target: Option<(u32, &Special)>| special_test(h(name), me, [0.0; 3], target);
        let still = Special::default();
        // None holds; an unknown name fails.
        assert!(special_test(0, &still, [0.0; 3], None) && !t("onTheMoon", &still, None));
        // Moving: past 0.1 m/s; fast: past 0.707 m/s (squared 0.01, 0.5).
        let walk = Special { velocity: [0.0, 0.5, 0.0], ..still };
        assert!(t("isMoving", &walk, None) && !t("notIsMoving", &walk, None) && !t("isMovingFast", &walk, None));
        assert!(t("notIsMoving", &still, None) && t("notIsMovingFast", &walk, None));
        // The script's bits on itself or on a ped target; an opponent test with no ped target fails, its "not" too.
        let stunned = Special { flags_240: 0x800_0000 | 0x100_0000, ..still };
        assert!(t("isReadyToBeStunned", &stunned, None) && !t("isReadyToBeExecuted", &stunned, None));
        assert!(t("opponentIsReadyToBeStunned", &still, Some((3, &stunned))) && !t("opponentIsReadyToBeStunned", &still, Some((4, &stunned))));
        assert!(t("checkMeleeActionFlag1", &stunned, None) && t("checkNotMeleeActionFlag0", &stunned, None));
        assert!(!t("opponentCheckNotMeleeActionFlag1", &still, None) && t("opponentCheckNotMeleeActionFlag0", &still, Some((3, &stunned))));
        // A move from idle; the "not" of the opponent's holds without one.
        let idle = Special { move_from_idle: true, ..still };
        assert!(t("isCurrentlyDoingAMoveFromIdle", &idle, None) && t("OpponentIsNotCurrentlyDoingAMoveFromIdle", &still, None));
        assert!(t("OpponentIsCurrentlyDoingAMoveFromIdle", &still, Some((3, &idle))));
        // The target 2 m ahead walking off at 1 m/s: moving away, and facing away when it faces off too.
        let off = Special { point: [0.0, 2.0, 0.0], velocity: [0.0, 1.0, 0.0], forward: [0.0, 1.0, 0.0], ..still };
        assert!(t("isTargetMovingAway", &still, Some((3, &off))) && t("isTargetMovingAndFacingAway", &still, Some((3, &off))));
        let back = Special { forward: [0.0, -1.0, 0.0], ..off };
        assert!(t("isTargetMovingAway", &still, Some((3, &back))) && t("NotIsTargetMovingAndFacingAway", &still, Some((3, &back))));
        // Too slow (0.6 m/s at least), or across the way rather than along it (0.707), is not moving away.
        let slow = Special { velocity: [0.0, 0.55, 0.0], ..off };
        let across = Special { velocity: [0.9, 0.6, 0.0], ..off };
        assert!(!moving_away([0.0; 3], &slow, false) && !moving_away([0.0; 3], &across, false));
        // No target: the moving-away tests and their "not"s fail.
        assert!(!t("isTargetMovingAway", &still, None) && !t("NotIsTargetMovingAway", &still, None));
        assert_eq!(SPECIAL_TESTS.len(), 26);
    }

    #[test]
    fn the_special_results() {
        let h = |s: &str| crate::map::hash_bytes(s.as_bytes(), 0);
        let fx = |name: &str| special_result(h(name));
        // A stun's hit sets flag 1 on the hitter; an execution's flag 0, and the victim no longer ready to be executed.
        assert_eq!(fx("SetMeleeActionFlag1").me, (0x100_0000, 0));
        assert_eq!((fx("DoExecutionStuff").me, fx("DoExecutionStuff").opponent), ((0x200_0000, 0), (0, 0x400_0000)));
        let e = fx("SetOpponentActionflagtrueReadytobeexecutedfalse").opponent;
        assert_eq!(SpecialEffect::apply(e, 0x600_0000 | 1), 0x200_0000 | 1);
        assert_eq!(fx("UnsetOpponentIsReadyToBeStunned").opponent, (0, 0x800_0000));
        assert_eq!(fx("UnsetMeleeActionFlag2").me, (0, 0x80_0000));
        assert_eq!((fx("SetSlowMoGameTime").slow_mo, fx("UnsetSlowMoGameTime").slow_mo), (Some(SLOW_MO), Some(1.0)));
        assert!(fx("SetStopMovesHoming").stop_homing && fx("SetOpponentStopMovesHoming").stop_opponent_homing);
        assert_eq!((special_result(0), fx("slowmo")), (SpecialEffect::default(), SpecialEffect::default()));
    }
}
