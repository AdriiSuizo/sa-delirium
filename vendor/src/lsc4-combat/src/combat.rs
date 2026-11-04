//! CTaskComplexCombat (1900, vtable 0x8201D33C; board PD10c), for a population ped that is not a cop: made against a
//! target (0x82483498), the first pick (0x824A64D8), the next-subtask pick (0x824A5C00) with its adjustments (0x82491CA0)
//! and maker (0x824A1260), the fight-or-flee check (0x8248B728), the every-frame control (vf20 0x824A65E0), the next
//! subtask after one finished (vf18 0x824A63D0) and the Fire (1901), Retreat (1904) and Charge (1905) subtasks. Read in
//! `docs/tasks/combat.md`.
//!
//! Not ported (ponytail, each at the code that skips it):
//! - The police, group and arrest rules (control rules 1, 4, 8, 9; flag bit 19 is only cleared, as 0x824847C0 does for a
//!   non-cop), so every ped here is "P" (population, not a cop). Add them with cops, `docs/tasks/combat.md` "Every frame".
//! - The codes the pick can give that have no subtask here are made as Fire ([`stand_in`]): advance 1902 (board PD8b),
//!   seek cover 1903 and the cover rules (PD9), investigate 1906, the vehicle and cop codes 1907-1910 (PD17, PD10g),
//!   execution 1911 and the weapon swap 319 with its check (0x82484FD0; control rule 10's line says what it waits on). The timer +0x74
//!   is set only by the advance's books, so by none here.
//! - The target upkeep's switch (0x82492440) to the targeting's next candidate (0x823C32A8 -> 0x825EC100): a lost target stops
//!   the combat. The candidate is the best slot of CPedTargetting's 8 (a ped at +0x34, its weight, the scanner's virtual
//!   calls and the line-of-sight test, updated by 0x825EB650); none of that list exists here ([`Targeting`]'s line), only
//!   the relationship scan's one pick (peds.rs). The upkeep switches to it when it differs from the target, the subtask is not 714 / 281 / 742 /
//!   319, a cop does not leave a player with no wanted level, and the target is gone or 2 s have passed since the last switch.
//! - The fight-or-flee check's cases 2, 6, 7 and 9, and the rest of cases 3 and 5, and the security guard and the
//!   ped's own level 4 (a mission ped, a cop or the model 0x82A9A630). The task's level byte +0x64 is -1 but for event
//!   17's combat ([`Combat::level`]). Case 2 reads a wanted-record bit (+0x5A & 0x10) nothing here sets (no setter
//!   found); 6 needs a mission running (byte 0x82B9D787, SET_MISSION_FLAG) and the player in a vehicle (the mission flag
//!   is not read); 7 needs the target to run task 1600 (unidentified) against a ped with no damaging weapon (every armed
//!   ped here has a gun); 9 reads byte 0x82FF5594, which nothing writes (never set); case 5's task 743 and its target
//!   flag (+0x28 bit 9) are unidentified. Single player is taken (the game mode word 0x82B39504 = 1).
//! - The Fire's gun task (CTaskComplexGun 1040) is made here ([`make_gun`], slot 25 0x824A1FD8) and run by the caller
//!   (the viewer's `complexgunsim`, whose kind 5 wait reads the ped's shoot rate byte +0x310: 40 for every ped from the
//!   weapon holder's ctor 0x823D5528 at 0x823D5600, set otherwise for cops, by the wanted response and by
//!   SET_CHAR_SHOOT_RATE). Not ported: the cover case (1054) and the thrown weapons' CTaskComplexThrowProjectile.
//! - Retreat is only a flag here: the caller runs the ped away (the viewer's `SmartFlee`, 60 m from
//!   `flee::DISTANCE`; the game's FleeAnyMeans 927 is not ported).
use crate::rng::Rand;

/// Flags +0x60 at the start (0x82483498): bits 13, 18 (just spotted) and 19 (arrest mode).
const MADE: u32 = 0xC2000;
/// Bit 19: arrest mode; 0x824847C0 clears it every frame for anything but a cop.
const ARREST: u32 = 1 << 19;
/// Bit 20: a coin flip at the start; the ped flees when its health is low (rule 4 of the check).
const COIN: u32 = 1 << 20;
/// Bits 24-25: the target's armed state at the last courage roll (0 not armed, 1 melee weapon, 2 damaging weapon).
const MEMORY: u32 = 3 << 24;
/// Bit 26: the courage roll has been made.
const ROLLED: u32 = 1 << 26;
/// Bit 17: the fight a hit response's 1900 gives (0x8218BC28's 0x20000): rule 12's level 4 for a gang member.
const FIGHT: u32 = 1 << 17;
/// Bit 28: committed to retreat; set by the check and by the maker of a Retreat; never cleared.
const FLEE: u32 = 1 << 28;
/// Bit 21: the forced re-fire (control rule 12, not ported), cleared by the re-think.
const REFIRE: u32 = 1 << 21;
/// Bit 22: cleared by the maker's preamble (0x824A1260).
const MAKER_CLEARS: u32 = 1 << 22;
/// The chance in single player below 1 is multiplied by this (0x820BEEFC).
const BRAVE_SCALE: f32 = 0.66;

/// The subtask codes the pick deals in: 200 none, Fire, Retreat, Charge.
pub const NONE: i32 = 200;
pub const FIRE: i32 = 1901;
pub const RETREAT: i32 = 1904;
pub const CHARGE: i32 = 1905;
/// 0x824943D0's answer (situation 100) that gives the Fire a 1.5 s re-think: move to better cover (cops' files).
const REPOSITION: i32 = 1931;

/// The subtask running: Fire (1901), Retreat (1904) or Charge (1905: run in and fight hand to hand).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Sub {
    Fire,
    Retreat,
    Charge,
}

impl Sub {
    /// Its task type (vf3).
    pub fn code(self) -> i32 {
        match self {
            Sub::Fire => FIRE,
            Sub::Retreat => RETREAT,
            Sub::Charge => CHARGE,
        }
    }
}

/// The combat decision maker as the pick asks it (0x82573808): its answer for a situation, a code or 200 for none, drawn
/// from the `Rand` it is given as the game's pick draws. The situations: 107 the first pick and 108 both on foot, 109 the
/// target in a vehicle (0x82491A98, with its situation mask), 100 (0x824943D0, no mask).
pub type Ask<'a> = dyn FnMut(i32, &mut Rand) -> i32 + 'a;

/// The codes the pick deals in by name: advance, seek cover, investigate, execute.
const ADVANCE: i32 = 1902;
const SEEK_COVER: i32 = 1903;
const INVESTIGATE: i32 = 1906;
const EXECUTE: i32 = 1911;
/// The running subtasks the next pick (0x824A5C00) does not read: LeaveAnyCar 714, DoNothing 281, BackOff 742 and the
/// weapon swap 319.
const SKIP: [i32; 4] = [714, 281, 742, 319];
/// The advance's hold (0x824A5C00): within 30 m ([0x82002388]) of the target.
const HOLD_RANGE: f32 = 30.0;
/// Control rule 11: the alertness ([0x82014B85], 70) from which the time firing +0x6C is set back to 0.
const ALERT: i8 = 70;

/// The codes the maker gets that have no subtask here, made as Fire (1901) (see the module's "Not ported"); the pick
/// reads the code itself (the re-think's same-code test, the advance's hold).
/// ponytail: advance 1902 (PD8b), seek cover 1903 (PD9), investigate 1906 (PD10e, on PD8's routes), pull from car 1907,
/// chase in car 1908 and 1909 (PD17, peds in vehicles), bust 1910 (PD10g), execution 1911 (PD10f) and the swap 319 (control
/// rule 10's weapon check line) are not made.
fn stand_in(code: i32) -> i32 {
    match code {
        NONE | FIRE | RETREAT | CHARGE => code,
        _ => FIRE,
    }
}

/// What the control reads of the ped, its target and the world each frame.
#[derive(Clone, Copy, Debug, Default)]
pub struct Facts {
    /// The frame time.
    pub dt: f32,
    /// The target ped is there and its health is 100 or more (the upkeep 0x82492440 clears it otherwise).
    pub target: bool,
    /// The ped's health (+0x1E4) and its model's bravery (pedpersonality.dat's, byte +0xEE).
    pub health: f32,
    pub bravery: u8,
    /// The target's armed state: 0 not armed, 1 holds a melee weapon (damage type 1), 2 a damaging one (2-4).
    pub target_armed: u8,
    /// The target is a cop; the player has a wanted level and a cop (health 100 or more) is within 20 m of the ped.
    pub target_cop: bool,
    pub wanted_cop_near: bool,
    /// The ped is a gang member ("G": peds.ide type 3 to 14, 0x822F2660) and hates the target (0x823C1458, dislike counted).
    pub gang: bool,
    pub hates_target: bool,
    /// The ped has a cover point (+0xC9C); the target is in a vehicle (+0xA80 and +0x23C bit 29: situation 109): the
    /// re-think timer's 5.0 and 1.0 s.
    pub cover: bool,
    pub target_in_vehicle: bool,
    /// The ped holds a damaging weapon (0x823D6800(ped +0x280, 1)): the adjustments' cover chance for Fire.
    pub armed: bool,
    /// The target is the player (+0x210 clear, +0x211 set); a ped in the player's group (the leader of the target's group is
    /// the player: case 3); a mission ped (+0x9B0 is 2); the player being arrested (+0x9C0 is 1: case 5).
    pub target_player: bool,
    pub target_friend: bool,
    pub target_mission: bool,
    pub target_arrested: bool,
    /// How many of the ped's 16 nearest peds are gang members of health 100 or more already running a combat task
    /// (0x823E1F70: rule 11); the ped's relationship group is 6 (+0xAD8: the 20 s cap).
    pub fights: u8,
    pub group6: bool,
    /// The target's health (+0x1E4) is under 100 ([0x82000D68]), and 0x822FF050 says the ped goes for the injured (a
    /// gang member, ped type 3-14, holding a gun, its combat decision maker's TARGET_INJURED_REACTION not 0): the target
    /// upkeep (0x82492440) keeps such a target, and the next pick (0x824A5C00) asks the decision maker at it.
    pub target_hurt: bool,
    pub for_injured: bool,
    /// The ped is the population's (+0x9B0 is 1; a script's is 2), holds a gun (0x823D68C8: info flag 0x20) and its combat
    /// decision maker's TARGET_INJURED_REACTION (+0x8F4 bits 13-14), CAUTION (bits 15-16) and NAVIGATION_STYLE (23-24).
    pub population: bool,
    pub gun: bool,
    pub injured_reaction: u8,
    pub caution: u8,
    pub navigation_style: u8,
    /// The player's ped is in an interior (its +0x24 bit 4, the entity's in-an-interior bit that the ambient condition
    /// InInterior 0x31 reads): with `population` that is 0x824850E8.
    pub player_inside: bool,
    /// 0x824851E8 says no (the ped cannot use cover): its +0x23C bit 0x40 (WILL_USE_COVER, set by the ped's setup
    /// 0x822F9CB8, cleared by SET_CHAR_WILL_USE_COVER 0x825C7360) clear, or it ragdolls (+0x1E2 low nibble 2 or more).
    pub no_cover: bool,
    /// Control rule 11: +0x230 bit 8 (SET_CHAR_ALLOWED_TO_DUCK off), the decision maker's STANDING_STYLE (+0x8F4 bits
    /// 25-26) and the intelligence's alertness byte (+0x2C8).
    pub no_duck: bool,
    pub standing_style: u8,
    pub alertness: i8,
    /// The distance from the ped to its target (their +0x20 matrices' places) and the targeting record's state for it
    /// (0x825E9CE8: 0 no record, 1 a clear shot, 2 blocked, 3 clear and dangerous; [`Targeting::state`]).
    pub target_dist: f32,
    pub line_state: u8,
}

/// Control rule 6 (0x82484D00), every frame the ped has a target: the firing factor (+0x24C, 1.0 to start) is tripled while the
/// target's root is under 4 m away (it compounds, and is never reset), else 0.2 if the target is hidden in cover (task
/// 1054) and has been for over 3 s, else it stays.
pub fn firing_factor(factor: f32, dist2: f32, hidden: bool) -> f32 {
    if dist2 < 16.0 { factor * 3.0 } else if hidden { 0.2 } else { factor }
}

/// A CTaskComplexGun (1040, ctor 0x8245C1A0) as the Fire makes it: its kind (+0x73), aim time (the ctor's f1, +0x30),
/// flags (+0x74) and whether it aims at the Fire's place (+0x50) beside the target ped (the ctor's r6). The ctor's other
/// arguments are the Fire's: f2 1.0, r8 0, r9 1 and r10 1.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GunMake {
    pub kind: u8,
    pub aim: f32,
    pub flags: u32,
    pub at_place: bool,
}

/// The Fire's byte +0x60 (the ctor 0x824843C0: its r6 into bit 7, its r7 into bit 6, bits 4 and 5 cleared): bit 7 (the cover case), bit 5 (made in arrest mode: the gun is remade when the combat leaves
/// it) and bit 4 (S 3 against a non-player, or a cover case: aim only).
pub const FIRE_COVER: u8 = 0x80;
/// Bit 6: the ctor's r7, which the maker (0x824A1260) passes 1: the cover fit vf18 (0x82492FF0) reads.
pub const FIRE_FITS: u8 = 0x40;
pub const FIRE_ARREST: u8 = 0x20;
pub const FIRE_AIM: u8 = 0x10;

/// The aim task's "fire" flag in a gun task's +0x74.
pub const GUN_FIRES: u32 = 4;

/// The gun the Fire makes for code 1020 (slot 25, 0x824A1FD8), a weapon that is not thrown (its info +0x20 bit 6 clear):
/// `state` is the targeting record's state for the target (S; 1 with no record) and `seen` its +0x2C bit 7 (F). First a
/// target that is not the player at S 3 sets the Fire's bit 4 (`bits`, its +0x60). Then:
/// - in arrest mode (`arrest`, the combat's +0x60 bit 19): kind 2 at the target ped and place, aim 2.0 s ([0x82000DC8];
///   -1.0, [0x820BECF4], when the Fire's parent is the combat 1900, as it always is here), flags 0x2000001, bit 5 set;
/// - S 0 or 2, or bit 7 or bit 4 set: kind 2 at the ped and place, aim 2.0 s, flags 0x2000001, and 0x2000005 at S 0 or 2
///   with F, or S 3;
/// - else (S 1): the picker 0x824942A0's kind and aim time (`gun_kind`) at the ped alone, flags 0x2000008.
///
/// Last, flag 8 is added when the Fire's byte +0x70 is set or the ped's physical response (its intelligence's +0x44,
/// the task tree's first slot) is a CTaskComplexReact (1312, `react`). The byte is the ctor's 0 (0x824843C0 at
/// 0x82484454; its clone 0x82490A98 makes a new one), and no code of the combat or its subtasks
/// (0x82480000-0x824B0000) writes it again, so it is never set.
pub fn make_gun(arrest: bool, state: u8, seen: bool, target_player: bool, react: bool, bits: &mut u8, fire_rate: u8, rng: &mut Rand) -> GunMake {
    if *bits & FIRE_COVER == 0 && state == 3 && !target_player {
        *bits |= FIRE_AIM;
    }
    let unseen = state == 0 || state == 2;
    let mut g = if arrest {
        *bits |= FIRE_ARREST;
        GunMake { kind: 2, aim: -1.0, flags: 0x200_0001, at_place: true }
    } else if unseen || *bits & (FIRE_COVER | FIRE_AIM) != 0 {
        let fires = unseen && seen || state == 3;
        GunMake { kind: 2, aim: 2.0, flags: if fires { 0x200_0005 } else { 0x200_0001 }, at_place: true }
    } else {
        let (kind, aim) = gun_kind(fire_rate, rng);
        GunMake { kind, aim, flags: 0x200_0008, at_place: false }
    };
    if react {
        g.flags |= 8;
    }
    g
}

/// The Fire control's step 4 (0x82493318): its gun child (1040) at the Fire's target is remade (aborted, then the first
/// child made again) when its fire flag (+0x74 bit 2) is not what the record says: should fire at S 3, or at S 0 or 2 with
/// F. A gun that fires is kept when it should not only while the Fire's bits 7 and 4 are clear.
pub fn gun_stale(flags: u32, state: u8, seen: bool, bits: u8) -> bool {
    let should = (state == 2 || state == 0) && seen || state == 3;
    let fires = flags & GUN_FIRES != 0;
    if should { !fires && bits & (FIRE_COVER | FIRE_AIM) == 0 } else { fires }
}

/// The Fire control's retarget (step 5, 0x82493318): every rand(2, 5) s ([0x82000DC8], [0x82053E68]) of its +0x6C (2.0
/// at the start, 0x824843C0) while its child is a gun task.
pub const RETARGET_FIRST: f32 = 2.0;
pub fn retarget_wait(rng: &mut Rand) -> f32 {
    rng.float_in_range(2.0, 5.0)
}

/// The retarget's new gun (0x82493318 step 5) at the other target: kind 7, or 2 when the Fire's bit 7 or bit 5 is set;
/// aim rand(0.5, 0.75) ([0x8201D078], [0x8201993C]); no place, flags 0 (the ctor's r8).
pub fn retarget_gun(bits: u8, rng: &mut Rand) -> GunMake {
    let kind = if bits & (FIRE_COVER | FIRE_ARREST) != 0 { 2 } else { 7 };
    GunMake { kind, aim: rng.float_in_range(0.5, 0.75), flags: 0, at_place: false }
}

/// The retarget's cone: 45 degrees either side ([0x82018A04]).
pub const RETARGET_CONE: f32 = std::f32::consts::FRAC_PI_4;

/// One of CPedTargetting's 8 slots (0x825EB910; 0x40 bytes from +0x20): the ped (+0x34), its place as last read, and the
/// record's state (the top 3 bits of +0x48).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Slot<T> {
    pub ped: T,
    pub at: [f32; 3],
    pub state: u8,
}

/// The ped's CPedTargetting (intelligence +0x25C, made on first use by 0x823C3238). The slots hold what the combat adds
/// (0x823C3328: its target), in the first free one; the caller keeps each slot's state as its line (1 clear, 2 blocked),
/// and the record's F (+0x2C bit 7) is clear; the upkeep's next candidate (0x823C32A8 -> 0x825EC100) is the relationship
/// scan's pick.
/// ponytail: (board PD10b) its update 0x825EB650 (the scan through vf13 filling the slots, the weights, the smoothing), the add's slot choice (vf1 0x825EBF90, VMX) and the state-0 work-out vf3 0x825EA7A0 (the line-of-sight probe 0x8220DEA0, the cone and distance tests, F) are not ported.
#[derive(Clone, Debug)]
pub struct Targeting<T> {
    pub slots: [Option<Slot<T>>; 8],
}

impl<T> Default for Targeting<T> {
    fn default() -> Self {
        Targeting { slots: std::array::from_fn(|_| None) }
    }
}

impl<T: Copy + PartialEq> Targeting<T> {
    /// 0x823C3328 (not for the ped itself, which the caller leaves out): the ped into its slot, or the first free one.
    pub fn add(&mut self, ped: T, at: [f32; 3], state: u8) {
        if let Some(s) = self.slots.iter_mut().flatten().find(|s| s.ped == ped) {
            s.at = at;
            return;
        }
        if let Some(free) = self.slots.iter_mut().find(|s| s.is_none()) {
            *free = Some(Slot { ped, at, state });
        }
    }

    /// 0x825E98A8: the ped out of its slot.
    pub fn remove(&mut self, ped: T) {
        for s in &mut self.slots {
            if s.is_some_and(|s| s.ped == ped) {
                *s = None;
            }
        }
    }

    /// 0x825E9CE8: the record's state for `ped` (0 when it has no slot; a state of 0 is worked out there by vf3
    /// 0x825EA7A0: [`Targeting`]'s line).
    pub fn state(&self, ped: T) -> u8 {
        self.slots.iter().flatten().find(|s| s.ped == ped).map_or(0, |s| s.state)
    }

    /// 0x825EB428: of the slots' peds other than `exclude` (and, with `skip_blocked`, not at state 2) whose heading from
    /// `me` (0x8245BEF8 of the flat offset) is within `cone` of `dir`, the one turned least from the heading to `exclude`
    /// (at its place), or with none from the ped's own (`forward`'s, its matrix's +0x10 row). The score of a turn `d` (wrapped, 0x8245BC88) is `-d` when it
    /// is 0 or more, `|d| - 1000` ([0x820BED28]) when less (`left` the other way round); the best above -999999
    /// ([0x8200F1C8]) wins.
    pub fn best_in_cone(&self, me: [f32; 3], forward: [f32; 2], dir: f32, cone: f32, exclude: Option<(T, [f32; 3])>, skip_blocked: bool, left: bool) -> Option<T> {
        use crate::actiontable::wrap;
        use crate::aisteer::heading;
        let from = match exclude {
            Some((_, at)) => heading(at[0] - me[0], at[1] - me[1]),
            None => heading(forward[0], forward[1]),
        };
        let mut best = (f32::from_bits(0xC974_23F0), None);
        for s in self.slots.iter().flatten() {
            if exclude.is_some_and(|x| x.0 == s.ped) || skip_blocked && s.state == 2 {
                continue;
            }
            let h = heading(s.at[0] - me[0], s.at[1] - me[1]);
            if wrap(h - dir).abs() > cone {
                continue;
            }
            let d = wrap(h - from);
            let score = if !left {
                if d < 0.0 { d.abs() - 1000.0 } else { -d }
            } else if d > 0.0 {
                d.abs() - 1000.0
            } else {
                -d.abs()
            };
            if best.0 < score {
                best = (score, Some(s.ped));
            }
        }
        best.1
    }
}

#[derive(Clone, Debug)]
pub struct Combat {
    /// +0x60.
    flags: u32,
    /// +0x5C: the lifetime (0 = none) and +0x94: the cap the "G" fight rule sets (20 s); +0x58: the re-think timer.
    life: f32,
    cap: f32,
    rethink: f32,
    /// +0x6C: the time firing (control rule 11); +0x70 and +0x74: the timers rule 2 counts down (+0x74 set by the
    /// advance's books, the cover time).
    firing: f32,
    t70: f32,
    t74: f32,
    pub sub: Option<Sub>,
    /// The running subtask's +0x14 (what follows it when the pick reads it: 3 the same code again, 4 its +0x20, 5 the
    /// decision maker asked as at the start; else nothing) and +0x20 (base 0x824842C0: 0 and 200).
    then: (u8, i32),
    /// The running advance's (1902) +0x1D bit 7 (it ended in cover) and +0x40 bit 3, which the next pick's books read:
    /// none, as 1902 is made as Fire here (`stand_in`).
    advance: (bool, bool),
    /// Ped +0x9BD: the advances that ended in cover in a row (the pick's books), kept with the ped's one combat.
    pub covers: u8,
    /// The Fire's time +0x40 when it is timed (+0x44: the retreat that fires, 5.0 s), else None.
    fire_time: Option<f32>,
    /// The maker gave a new subtask (the old one is to be replaced); taken by the caller.
    pub remade: bool,
    /// The gun task the Fire made this frame (`make_gun`), for a caller that runs the task itself (`complexgunsim`);
    /// taken by the caller.
    pub made: Option<GunMake>,
    /// +0x64: the roll's level in place of the bravery's when not -1 (the constructor's -1; event 17's handler 0x82471E90
    /// sets 3).
    pub level: i8,
    /// The speech step's wait (+0x54, s; `speech_step`).
    talk: f32,
    /// The abort the last step asked of the running subtask (its urgency) and what a refusal does.
    abort: Option<(u8, Refusal)>,
    /// The roll sent the ped running from a target armed with nothing or a melee weapon: it says FIGHT_RUN (0x8248C140,
    /// on its +0x510, force 0, repeat 0); taken by the caller.
    pub fight_run: bool,
}

/// The target's armed state from its weapon's damage type (weaponinfo `damagetype`); fists and nothing in hand are 0.
pub fn armed_state(damage_type: &str) -> u8 {
    match damage_type {
        "MELEE" => 1,
        "BULLET" | "EXPLOSIVE" | "FIRE" => 2,
        _ => 0,
    }
}

/// The courage roll's chance (the table at 0x82AA5160, 5 levels by 3 states): 0 for level 0; level 1 is 0.5 against states
/// 0 and 1; levels 2 and 3 are 1.0 against them; all of those 0 against state 2; level 4 is 1.0 whatever the state.
fn chance(level: i32, armed: u8) -> f32 {
    match (level, armed) {
        (4, _) => 1.0,
        (0, _) | (_, 2) => 0.0,
        (1, _) => 0.5,
        _ => 1.0,
    }
}

/// The picker 0x824942A0: the gun's kind and aim time by the decision maker's FIRE_RATE (its +0x8F4 bits 30-31, the
/// decision maker at intelligence +0xD8): kind 5 at 2.0 s ([0x82000DC8]) x (rand() x 1/32767 [0x820BEDDC] x 0.2
/// [0x8201DFE8] + 0.9 [0x8201843C], fused), the CRT's rand drawn whatever the rate; then 0 kind 4 (0.5 s, [0x8201D078]),
/// 1 kind 5 (2.0), 2 kind 7 (1.0, [0x82000D48]), 3 kind 5 with the drawn time.
fn gun_kind(fire_rate: u8, rng: &mut Rand) -> (u8, f32) {
    let r = rng.next() as i32 as f32 * f32::from_bits(0x3800_0100);
    let drawn = r.mul_add(f32::from_bits(0x3E4C_CCD0), f32::from_bits(0x3F66_6666)) * 2.0;
    match fire_rate {
        0 => (4, 0.5),
        1 => (5, 2.0),
        2 => (7, 1.0),
        _ => (5, drawn),
    }
}

fn tick(t: &mut f32, dt: f32) -> bool {
    *t > 0.0 && {
        *t -= dt;
        *t <= 0.0
    }
}

impl Combat {
    /// 0x82483498 with the target ped and a lifetime in seconds (0 = none): flags 0xC2000, and bit 20 on a coin flip.
    pub fn new(life: f32, rng: &mut Rand) -> Combat {
        let coin = rng.float_in_range(0.0, 1.0) < 0.5;
        Combat { flags: MADE | if coin { COIN } else { 0 }, ..Combat::blank(life) }
    }

    fn blank(life: f32) -> Combat {
        Combat { flags: MADE, life, cap: 0.0, rethink: 0.0, firing: 0.0, t70: 0.0, t74: 0.0, sub: None, then: (0, NONE), advance: (false, false),
                 covers: 0, fire_time: None, remade: false, made: None, abort: None, fight_run: false, level: -1, talk: 0.0 }
    }

    /// As [`Combat::new`] with +0x60 bit 28 set: the combat CEventPedToFlee's handler (0x82472200) gives a ped at the cop
    /// that picked it (`docs/peds/ambient.md`): its pick is Retreat.
    pub fn fleeing(life: f32, rng: &mut Rand) -> Combat {
        let mut c = Combat::new(life, rng);
        c.flags |= FLEE;
        c
    }

    /// The fight-or-flee check (0x8248B728) as another task asks it of a Combat known by its +0x60 word alone
    /// (CTaskComplexReact's first subtask, 0x8243F3C8): true when the ped flees; the word as the check leaves it.
    pub fn courage_of(flags: &mut u32, f: &Facts, rng: &mut Rand) -> bool {
        let mut c = Combat { flags: *flags, ..Combat::blank(0.0) };
        let r = c.courage(f, rng);
        *flags = c.flags;
        r
    }

    /// Flag bit 17 set (the hit response's 1900, [`FIGHT`]).
    pub fn fighting(&mut self) {
        self.flags |= FIGHT;
    }

    /// +0x60 bit 13: the maker (0x824A1260) puts it in a Retreat's +0x69, which then screams (PANIC).
    pub fn screams(&self) -> bool {
        self.flags & 1 << 13 != 0
    }

    pub fn retreating(&self) -> bool {
        self.sub == Some(Sub::Retreat)
    }

    /// 0x82491A98: the decision maker's answer for the situation, 108 both on foot or 109 the target in a vehicle (kept
    /// at +0x90); `first` asks 107 in its place.
    /// ponytail: 110 and 111 (the ped in a vehicle) are not asked: no ped rides in a combat here (board PD17).
    fn situation(f: &Facts, first: bool, ask: &mut Ask, rng: &mut Rand) -> i32 {
        let s = if f.target_in_vehicle { 109 } else { 108 };
        ask(if first { 107 } else { s }, rng)
    }

    /// 0x824850E8: a population ped (+0x9B0 1) while the player is in an interior (the player's ped +0x24 bit 4).
    fn indoors(f: &Facts) -> bool {
        f.population && f.player_inside
    }

    /// The adjustments to a code (0x82491CA0), as far as they are ported:
    /// - flag bit 28: Retreat (1904), but Fire when a Retreat is running whose next is Fire (+0x14 4, +0x20 1901);
    /// - a dying target (+0x1E4 under 100) and a gang member made by the population, or TARGET_INJURED_REACTION 2,
    ///   holding a gun (0x823D68C8): execute it (1911);
    /// - Fire: a ped holding a damaging weapon (0x823D6800), not in arrest mode, not seeking cover (1903), that can use
    ///   cover (0x824851E8) and has no cover point draws rand(0, 1) against its cover chance (1.0; 0.5 by 0x824850E8) and
    ///   looks for cover within 3 m (0x82AA519C) of the target (0x82486928), going to cover (1903) when found;
    /// - seek cover (1903): none (200) when the ped cannot use cover or its NAVIGATION_STYLE is 1.
    ///
    /// The weapon check (0x82484FD0, called first) and its swap (319): control rule 10's line.
    /// ponytail: the group order's seek cover (intelligence
    /// +0x38 bit 31 and its area test 0x823C2A38, which no ped here has); the lost target's investigate (1906:
    /// 0x825E9E48, the targeting record's weight, is taken as kept); the Fire's cover search finds nothing (no cover points,
    /// PD9); the vehicle and cop codes 1907 / 1908 / 1910 pass as they are.
    fn adjust(&self, f: &Facts, code: i32, rng: &mut Rand) -> i32 {
        if self.flags & FLEE != 0 {
            return if code == FIRE && self.sub == Some(Sub::Retreat) && self.then == (4, FIRE) { FIRE } else { RETREAT };
        }
        if f.target_hurt && (f.gang && f.population || f.injured_reaction == 2) && f.gun {
            return EXECUTE;
        }
        if code == FIRE && self.flags & ARREST == 0 && f.armed && self.sub.map(Sub::code) != Some(SEEK_COVER) && !f.no_cover && !f.cover {
            let chance = if Combat::indoors(f) { 0.5 } else { 1.0 };
            let _cover = rng.float_in_range(0.0, 1.0) < chance;
        }
        if code == SEEK_COVER && (f.no_cover || f.navigation_style == 1) {
            return NONE;
        }
        code
    }

    /// The advance's books in the next pick (0x824A5C00) for a running advance (1902): one that ended in cover (`cover`,
    /// its +0x1D bit 7) counts up ped +0x9BD, sets flag bit 15 when its +0x40 has bit 3 (`bit3`), and gives +0x74
    /// rand(0.8, 1.2) ([0x820BED00], [0x820BEF98]) x [0x82AA5B3C] (4.0); one that did not sets +0x9BD to 0.
    fn books(&mut self, cover: bool, bit3: bool, rng: &mut Rand) {
        if !cover {
            self.covers = 0;
            return;
        }
        self.covers = self.covers.wrapping_add(1);
        if bit3 {
            self.flags |= 1 << 15;
        }
        self.t74 = rng.float_in_range(0.8, 1.2) * 4.0;
    }

    /// 0x824914C8, the advance's hold: 0 in arrest mode; else by the decision maker's CAUTION: 1 50 s ([0x82000A2C]), 2
    /// 25 s ([0x820BECF8]), else 999 s ([0x820BEEF8]).
    fn hold(&self, f: &Facts) -> f32 {
        if self.flags & ARREST != 0 {
            return 0.0;
        }
        match f.caution {
            1 => 50.0,
            2 => 25.0,
            _ => 999.0,
        }
    }

    /// The next-subtask pick (0x824A5C00): `finished` its subtask finished (vf18: r5 1), `rethink` the control's re-think
    /// (rules 13 / 14: r6 1). A finished investigate (1906) makes nothing. The running subtask is read unless it is one
    /// of the skip set (714, 281, 742, 319): an advance's books, then its +0x14 says what follows (3 its own code again,
    /// 4 its +0x20, 5 the first situation asked). With a target that is not dying (or that the ped goes for, 0x822FF050)
    /// and nothing picked, the decision maker is asked. A re-think that comes to the running subtask's code makes nothing,
    /// and so does a re-think to advance (1902) while firing at a target whose record is not blocked (0x825E9CE8 not 2)
    /// within 30 m when the time firing (+0x6C) is under the hold (0x824914C8). Else the maker.
    /// ponytail: firing from cover too long (step 6: Fire with 1054 and a cover point +0xC9C, PD9; it also counts the
    /// group 0x8244EDB8 and the cops for a flank 1913) is not here: no ped has a cover point.
    fn pick(&mut self, f: &Facts, finished: bool, rethink: bool, ask: &mut Ask, rng: &mut Rand) -> Option<Sub> {
        let running = self.sub.map(Sub::code);
        if finished && running == Some(INVESTIGATE) {
            return None;
        }
        let mut code = NONE;
        if let Some(r) = running.filter(|r| !SKIP.contains(r)) {
            if r == ADVANCE {
                let (cover, bit3) = self.advance;
                self.books(cover, bit3, rng);
            }
            code = match self.then {
                (3, _) => self.adjust(f, r, rng),
                (4, next) => self.adjust(f, next, rng),
                (5, _) => {
                    let s = Combat::situation(f, true, ask, rng);
                    self.adjust(f, s, rng)
                }
                _ => NONE,
            };
        }
        if f.target && (!f.target_hurt || f.for_injured) && code == NONE {
            let s = Combat::situation(f, false, ask, rng);
            code = self.adjust(f, s, rng);
        }
        if rethink && Some(code) == running {
            return None;
        }
        if rethink && code == ADVANCE && running == Some(FIRE) && f.line_state != 2 && f.target_dist < HOLD_RANGE && self.firing < self.hold(f) {
            return None;
        }
        self.make(code, f, ask, rng)
    }

    /// The subtask maker (0x824A1260). Its preamble: the re-think timer +0x58 to 0, flag bit 22 cleared; 200 makes
    /// nothing. Fire from a Retreat is the retreat that fires: timed 5.0 s (0x82053E68), next Retreat (+0x14 4, +0x20
    /// 1904); any other Fire sets the re-think timer ([`Combat::fire_rethink`]) and is untimed. Retreat sets bit 28;
    /// Charge sets the re-think timer to 5.0 x rand(0.9, 1.1). The codes with no subtask here are made as Fire
    /// ([`stand_in`]).
    /// ponytail: a ped in a vehicle is not given LeaveAnyCar (714) first (PD17); the cover point's upkeep (kept only
    /// within 2 m by 0x825545C0, released for 200, Retreat and Charge) is not here (PD9); the aim calls (0x822F0FF8,
    /// 0x822F2258) are the viewer's.
    fn make(&mut self, code: i32, f: &Facts, ask: &mut Ask, rng: &mut Rand) -> Option<Sub> {
        let code = stand_in(code);
        self.rethink = 0.0;
        self.flags &= !MAKER_CLEARS;
        let (sub, time) = match code {
            FIRE if self.sub == Some(Sub::Retreat) => (Sub::Fire, Some(5.0)),
            FIRE => {
                self.rethink = self.fire_rethink(f, ask, rng);
                (Sub::Fire, None)
            }
            RETREAT => {
                self.flags |= FLEE;
                (Sub::Retreat, None)
            }
            CHARGE => {
                self.rethink = 5.0 * rng.float_in_range(0.9, 1.1);
                (Sub::Charge, None)
            }
            _ => return None,
        };
        self.then = if time.is_some() { (4, RETREAT) } else { (0, NONE) };
        (self.sub, self.fire_time, self.made, self.remade) = (Some(sub), time, None, true);
        Some(sub)
    }

    /// The Fire's re-think time (the maker's case 1901, and the control's rule 13 when nothing new came): 1.5 s when
    /// 0x824943D0 (situation 100) answers 1931, 5.0 with a cover point, 1.0 against a target in a vehicle, else 2.5;
    /// then x rand(0.9, 1.1), drawn after the decision maker's.
    fn fire_rethink(&self, f: &Facts, ask: &mut Ask, rng: &mut Rand) -> f32 {
        let base = if ask(100, rng) == REPOSITION { 1.5 } else if f.cover { 5.0 } else if f.target_in_vehicle { 1.0 } else { 2.5 };
        base * rng.float_in_range(0.9, 1.1)
    }

    /// The first pick (0x824A64D8, the task's first subtask): the courage roll, then situation 107, then 108 / 109 when
    /// it has no answer, else Fire; the adjustments, the maker.
    /// ponytail: the target places (+0x80, +0x20), ped +0x26C bit 1 and the targeting's add (0x823C3328; the caller makes
    /// the target its own) are not here; nor is the weapon check (0x82484FD0: control rule 10's line).
    fn first(&mut self, f: &Facts, ask: &mut Ask, rng: &mut Rand) -> Option<Sub> {
        // Its target pointer is all it asks (no health test, no lifetime: those are the control's).
        self.courage(f, rng);
        let mut code = Combat::situation(f, true, ask, rng);
        if code == NONE {
            code = Combat::situation(f, false, ask, rng);
        }
        if code == NONE {
            code = FIRE;
        }
        let code = self.adjust(f, code, rng);
        let made = self.make(code, f, ask, rng);
        // Then ped +0x9BD to 0 (0x824A65D4).
        self.covers = 0;
        made
    }

    /// The next subtask once the running one finished (vf18 0x824A63D0): none without a target, else the pick with r5 1;
    /// false when it makes nothing, which ends the combat. The caller sets ped +0x240 bit 31 (`reacted`), as it does.
    /// ponytail: a non-player target in a vehicle it cannot reach (+0x9C0 1, 0x8237ACF0) is not read (PD17).
    pub fn finished(&mut self, f: &Facts, ask: &mut Ask, rng: &mut Rand) -> bool {
        f.target && self.pick(f, true, false, ask, rng).is_some()
    }

    /// The timed Fire's clock (+0x40), counted down by its control (0x82493318 step 6) while above 0.
    pub fn fire_tick(&mut self, dt: f32) {
        if let (Some(Sub::Fire), Some(t)) = (self.sub, self.fire_time.as_mut()) {
            if *t > 0.0 {
                *t -= dt;
            }
        }
    }

    /// The Fire's next (vf18 0x82492FF0) on a timed Fire (+0x44) whose time (+0x40) is at or below 0: it ends (its next
    /// code as made); otherwise it makes its first child again, so an untimed Fire never ends by itself.
    /// ponytail: the first case (the decision maker's +0x8F8 bits 6-7 and the Fire's +0x60 bit 6, a ped in cover 1054
    /// whose point no longer fits: next code 1903) needs cover (PD9).
    pub fn fire_out(&self) -> bool {
        matches!((self.sub, self.fire_time), (Some(Sub::Fire), Some(t)) if t <= 0.0)
    }

    /// The fight-or-flee check (0x8248B728): true when the ped flees (bit 28 now set).
    fn courage(&mut self, f: &Facts, rng: &mut Rand) -> bool {
        if self.flags & FLEE != 0 {
            return true;
        }
        let run = |c: &mut Combat, bits| {
            c.flags |= bits;
            true
        };
        // Case 3: a mission ped (not the player's friend) is the target of a non-player target.
        // Case 5: the player being arrested is the target.
        if !f.target_player && f.target_mission && !f.target_friend || f.target_arrested {
            return run(self, FLEE);
        }
        // Rule 4: bit 20 and the ped's health below 120 + 20 x max(0, 4 - bravery).
        if self.flags & COIN != 0 && f.health < 120.0 + 20.0 * (4 - i32::from(f.bravery)).max(0) as f32 {
            return run(self, FLEE);
        }
        // The roll waits for the target's armed state to rise above the last roll's.
        let first = self.flags & ROLLED == 0;
        if !first && (self.flags & MEMORY) >> 24 >= u32::from(f.target_armed) {
            return false;
        }
        self.flags = self.flags & !MEMORY | u32::from(f.target_armed) << 24;
        // Rules 8 and 10, on the first roll: a cop near with a wanted level; the target a cop.
        if first && (f.wanted_cop_near || f.target_cop) {
            return run(self, FLEE | ROLLED);
        }
        // Rules 11 and 13: level = bravery - 1 clamped to 0..3; the chance is x 0.66 in single player below 1.
        let mut level = (i32::from(f.bravery) - 1).clamp(0, 3);
        // 0x8248BEB4: the task's +0x64 in its place when set.
        if self.level != -1 {
            level = i32::from(self.level);
        }
        // Rule 11: a gang member against a target that is not armed (state 0 or 1) runs when 2 of its 16 nearest are fighting.
        if f.gang && f.target_armed <= 1 && f.fights >= 2 {
            return run(self, FLEE | ROLLED);
        }
        // Rule 12: a gang member's level is 4 with flag bit 17 (the hit response's fight), after a first roll or against a
        // target it hates, else 3.
        if f.gang {
            level = if self.flags & FIGHT != 0 || !first || f.hates_target { 4 } else { 3 };
        }
        let mut chance = chance(level, f.target_armed);
        if chance < 1.0 {
            chance *= BRAVE_SCALE;
        }
        self.flags |= ROLLED;
        self.cap = 0.0;
        // A FIGHT_RUN line is said when the armed memory is 0 or 1.
        if rng.float_in_range(0.0, 1.0) > chance {
            self.fight_run = f.target_armed <= 1;
            return run(self, FLEE);
        }
        // Fighting on: a group 6 gang member against the player and a target not armed is given 20 s (rule 7 ends it).
        if f.gang && f.target_armed <= 1 && f.group6 && f.target_player {
            self.cap = 20.0;
        }
        false
    }

    /// The control (vf20 0x824A65E0), once a frame: false when the combat ends. The first call is the first pick
    /// (0x824A64D8), which ends it when it makes nothing. `ask` is the combat decision maker ([`Ask`]). Where the control
    /// asks the running subtask to abort (0x82164FB8 with r5 the urgency, no event) before it switches or stops, the
    /// step records the ask ([`Combat::abort`]): the caller asks the subtask, and on a refusal [`Combat::refused`] puts
    /// the combat back as the control's refused branch leaves it.
    pub fn step(&mut self, f: &Facts, ask: &mut Ask, rng: &mut Rand) -> bool {
        self.abort = None;
        self.flags &= !ARREST;
        // Rule 2: the timers +0x70 and +0x74 count down while above 0.
        for t in [&mut self.t70, &mut self.t74] {
            if *t > 0.0 {
                *t -= f.dt;
            }
        }
        let Some(sub) = self.sub else {
            if !f.target {
                return false;
            }
            return self.first(f, ask, rng).is_some();
        };
        // Rule 6: no target (deleted), no combat: an abort at urgency 0; refused, the subtask runs on.
        if !f.target {
            return self.stop(0, Refusal::Keep);
        }
        // Rule 7: the lifetimes +0x5C and +0x94 count down while above 0, and at 0 the combat stops (urgency 1); refused,
        // the one that ran out is set to 0.0001 to try again next frame.
        let (life, cap) = (tick(&mut self.life, f.dt), tick(&mut self.cap, f.dt));
        if life | cap {
            return self.stop(1, Refusal::Retry { life, cap });
        }
        self.after_lifetimes(f, sub, ask, rng)
    }

    /// The control from rule 10 on.
    fn after_lifetimes(&mut self, f: &Facts, sub: Sub, ask: &mut Ask, rng: &mut Rand) -> bool {
        // Rule 10: the target upkeep (0x82492440) drops one under 100 health unless the ped goes for the injured
        // (0x822FF050); then no target stops it at urgency 0.
        if f.target_hurt && !f.for_injured {
            return self.stop(0, Refusal::Keep);
        }
        // Rule 10: unless committed, the check; a flee, or a Retreat without the bit; or a Charge while the ped holds a
        // damaging weapon (0x823D6800(ped +0x280, 1)): the subtask is asked to abort first (urgency 1), and only when it
        // agrees the re-pick (0x824A5C00, r5 0 and r6 0), the combat ending when that makes nothing; refused, the rules
        // below go on.
        // ponytail: the weapon check (0x82484FD0 / 0x82483FC8: its re-pick here, the first pick's and the adjustments'
        // calls, their swap 319 (0x82491CA0) and the maker's CTaskSimpleSwapWeapon 0x10600 through swapweapon.rs) waits on
        // the AI weapon holder port (ped +0x280's slots and ammo, its own follow-up PR; the loadout is PD13's).
        if self.flags & FLEE == 0 && (self.courage(f, rng) || sub == Sub::Retreat) || sub == Sub::Charge && f.armed {
            let held = self.clone();
            let made = self.pick(f, false, false, ask, rng).is_some();
            self.abort = Some((1, Refusal::Repick(Box::new(held))));
            return made;
        }
        self.rest(f, sub, ask, rng);
        true
    }

    /// A stop at `urgency`, with what a refusal does.
    fn stop(&mut self, urgency: u8, refusal: Refusal) -> bool {
        self.abort = Some((urgency, refusal));
        false
    }

    /// Rules 11 and 13 of the control, on the running subtask `sub`.
    fn rest(&mut self, f: &Facts, sub: Sub, ask: &mut Ask, rng: &mut Rand) {
        // Rule 11: the time firing +0x6C counts up while the subtask is Fire, else (arrest mode clear) is 0. Unless the
        // armed move set's first case holds (Fire, +0x230 bit 8 clear, no cover point, STANDING_STYLE 0), an alertness of
        // 70 or more sets it to 0 too. The move set itself is [`Combat::move_set`].
        if sub == Sub::Fire {
            self.firing += f.dt;
        } else if self.flags & ARREST == 0 {
            self.firing = 0.0;
        }
        let stands = !f.no_duck && sub == Sub::Fire && !f.cover && f.standing_style == 0;
        if !stands && f.alertness >= ALERT {
            self.firing = 0.0;
        }
        // Rule 13: the re-think timer +0x58 counts down for Fire (with +0x74 out) and Charge while above 0; out, bit 21 is
        // cleared and the pick runs as a re-think; a new subtask is kept when the running one agrees to abort (urgency 1),
        // else deleted; with none kept the timer is set again: 5.0 for Charge, else as the Fire's
        // ([`Combat::fire_rethink`]), x rand(0.9, 1.1), and + rand(5, 8) ([0x82053E68], [0x82004E0C]) by 0x824850E8.
        if (sub == Sub::Fire && self.t74 <= 0.0 || sub == Sub::Charge) && self.rethink > 0.0 {
            self.rethink -= f.dt;
            if self.rethink <= 0.0 {
                self.flags &= !REFIRE;
                let held = self.clone();
                if self.pick(f, false, true, ask, rng).is_some() {
                    self.abort = Some((1, Refusal::Rethink(Box::new(held))));
                } else {
                    self.rethink_again(f, ask, rng);
                }
            }
        }
    }

    /// Rule 13's timer when no new subtask is kept.
    fn rethink_again(&mut self, f: &Facts, ask: &mut Ask, rng: &mut Rand) {
        self.rethink = if self.sub == Some(Sub::Charge) { 5.0 * rng.float_in_range(0.9, 1.1) } else { self.fire_rethink(f, ask, rng) };
        if Combat::indoors(f) {
            self.rethink += rng.float_in_range(5.0, 8.0);
        }
    }

    /// The abort the last step asked of the running subtask (0x82164FB8's urgency), if any.
    pub fn abort(&self) -> Option<u8> {
        self.abort.as_ref().map(|a| a.0)
    }

    /// The running subtask refused the step's abort: as the control's refused branch leaves the combat. A stop: the
    /// subtask runs on (a lifetime that ran out set to 0.0001, and the rules after it run on; an ask they make waits for
    /// the next frame's control). The re-pick of rule 10: not made, and rules 11 and
    /// 13 run on the running subtask. A re-think's new subtask: deleted (the maker's own changes to the combat stay), the
    /// timer set again. True when the combat goes on.
    pub fn refused(&mut self, f: &Facts, ask: &mut Ask, rng: &mut Rand) -> bool {
        match self.abort.take().map(|a| a.1) {
            Some(Refusal::Keep) => true,
            Some(Refusal::Retry { life, cap }) => {
                if life {
                    self.life = 0.0001;
                }
                if cap {
                    self.cap = 0.0001;
                }
                self.sub.is_none_or(|sub| self.after_lifetimes(f, sub, ask, rng))
            }
            Some(Refusal::Repick(held)) => {
                *self = *held;
                if let Some(sub) = self.sub {
                    self.rest(f, sub, ask, rng);
                }
                true
            }
            Some(Refusal::Rethink(held)) => {
                (self.sub, self.then, self.fire_time, self.advance, self.made, self.remade) =
                    (held.sub, held.then, held.fire_time, held.advance, held.made, false);
                self.rethink_again(f, ask, rng);
                true
            }
            None => false,
        }
    }

    /// Control rule 11's armed move set (0x822FA728(ped, 1, 1000), `complexguncreatelast::clear`: the ped's motion object
    /// at +0x9D0 takes clip set 0x35 / 0x37 with a gun held, 0x35 / 0x36 without, for 1000 ms; the time firing is not
    /// touched): asked while firing with +0x230 bit 8 clear, no cover point and STANDING_STYLE 0, or under the alertness of
    /// 70 with bit 8 and the cover point clear and flag bit 21 or 22.
    /// ponytail: no caller sets it: a viewer ped has no motion object (+0x9D0) with clip sets to switch (board PD5, the
    /// move sets).
    pub fn move_set(&self, f: &Facts) -> bool {
        let fire = self.sub == Some(Sub::Fire);
        (!f.no_duck && fire && !f.cover && f.standing_style == 0)
            || f.alertness < ALERT && !f.no_duck && !f.cover && self.flags & (REFIRE | MAKER_CLEARS) != 0
    }

    /// Control rule 10's arrest bit as the Fire's gun maker reads it (+0x60 bit 19; 0x824847C0 clears it for a non-cop).
    pub fn arrest(&self) -> bool {
        self.flags & ARREST != 0
    }
}

/// What a refused abort does (the branch of the control past its 0x82164FB8).
#[derive(Clone, Debug)]
enum Refusal {
    /// The subtask runs on (rules 6 and 10's stops).
    Keep,
    /// Rule 7: the lifetime(s) that ran out set to 0.0001.
    Retry { life: bool, cap: bool },
    /// Rule 10: the combat as it was before the re-pick.
    Repick(Box<Combat>),
    /// Rule 13: the combat as it was before the re-think's pick (its subtask's fields).
    Rethink(Box<Combat>),
}

// Bit 18 of +0x60: just spotted (set when made, `MADE`); the Fire's control says SPOT_SUSPECT and clears it.
const SPOTTED: u32 = 1 << 18;

/// What the Fire subtask's control (CTaskComplexCombatFireSubtask vf20, 0x82493318) reads for its lines.
#[derive(Clone, Copy, Debug, Default)]
pub struct FireIn {
    /// The speaker's ped type is 2.
    pub cop: bool,
    /// The speaker is in a vehicle (+0x23C bit 0x20000000).
    pub in_car: bool,
    /// 0x82485138: the combat's task 1054 descendant is in state 10, 3 or 5; with none, no fire task (1042) under it.
    pub firing: bool,
    /// The target is in a car (+0x23C, its +0xA80 a car by 0x8262BEF0).
    pub target_in_car: bool,
    /// The target is the player (vf +0x114; for GET_OUT_OF_CAR: +0x210 0 and +0x211 set).
    pub target_player: bool,
    /// The player's wanted level is 1 (player info +0x220 +0x60 +0x68).
    pub one_star: bool,
    /// 0x82484510: the friends (0x823C1328) of health 100 or more ([0x82000D68]) among the speaker's 16 nearest.
    pub friends: u32,
    /// The target's weapon (its +0x280 holder, 0x823D4D60): its info's damage type (+0xC) and weapon type (+0x14).
    pub target_weapon: Option<(i32, i32)>,
    /// The living cops (ped type 2, health 100 or more) among the speaker's 16 nearest within 30 m (900 squared), and
    /// those of them of the model [0x82A9A5C4] (SWAT).
    pub cops_near: u32,
    pub swat_near: u32,
    /// The target player's wanted level (+0x220 +0x60 +0x68), 0 for any other target.
    pub wanted: i32,
}

/// A line the control asks for: the say function's (0x8246B008, force 0, `repeat`) when `chance` is None, else the
/// maybe-say's (0x822F2868) at that chance.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LineAsk {
    pub context: &'static str,
    pub chance: Option<f32>,
    pub repeat: bool,
}

/// The Fire control's lines for a frame. `first` is the combat's just-spotted bit, cleared here; `roll` the control's
/// 0x82158E78(0, 1) draw, made only on the second path.
/// - Just spotted: a cop says SPOT_SUSPECT (repeat 1), and that is all for the frame.
/// - Else, on foot and firing, on a draw under 0.1: at a target in a car a cop says GET_OUT_OF_CAR at one star, else
///   GET_OUT_OF_CAR_SHOUT; with two or more friends near and the player its target, MOVE_IN and SURROUNDED at 0.05 each,
///   and a cop GET_DOWN and EVACUATE_AREA at 0.05; and a cop at an armed target SPOT_GUN (a damage type of 2, repeat 1)
///   or SPOT_WEAPON (not 1, and a weapon type other than 0x2E) at 0.05.
pub fn fire_lines(first: &mut bool, f: &FireIn, mut roll: impl FnMut() -> f32) -> Vec<LineAsk> {
    let say = |context, repeat| LineAsk { context, chance: None, repeat };
    let maybe = |context, repeat| LineAsk { context, chance: Some(0.05), repeat };
    if std::mem::take(first) {
        return if f.cop { vec![say("SPOT_SUSPECT", true)] } else { vec![] };
    }
    let mut out = vec![];
    if f.in_car || !f.firing || roll() >= 0.1 {
        backup(&mut out, f, &mut roll);
        return out;
    }
    if f.target_in_car && f.cop {
        out.push(say(if f.target_player && f.one_star { "GET_OUT_OF_CAR" } else { "GET_OUT_OF_CAR_SHOUT" }, false));
    }
    if f.friends >= 2 && f.target_player {
        out.extend([maybe("MOVE_IN", false), maybe("SURROUNDED", false)]);
        if f.cop {
            out.extend([maybe("GET_DOWN", false), maybe("EVACUATE_AREA", false)]);
        }
    }
    if let (true, Some((damage, kind))) = (f.cop, f.target_weapon) {
        if damage == 2 {
            out.push(maybe("SPOT_GUN", true));
        } else if damage != 1 && kind != 0x2E {
            out.push(maybe("SPOT_WEAPON", false));
        }
    }
    backup(&mut out, f, &mut roll);
    out
}

/// The control's backup lines (0x8249391C), for a cop on its own draw under 0.1: REQUEST_BACKUP (0.01) with one cop or
/// none near, or two with the target above one star; REQUEST_SWAT (0.01) with no SWAT near at four stars or more.
fn backup(out: &mut Vec<LineAsk>, f: &FireIn, roll: &mut impl FnMut() -> f32) {
    if !f.cop || roll() >= 0.1 {
        return;
    }
    let maybe = |context| LineAsk { context, chance: Some(0.01), repeat: false };
    if f.cops_near <= 1 || (f.cops_near == 2 && f.wanted > 1) {
        out.push(maybe("REQUEST_BACKUP"));
    }
    if f.swat_near == 0 && f.wanted >= 4 {
        out.push(maybe("REQUEST_SWAT"));
    }
}

impl Combat {
    /// [`fire_lines`] with this combat's just-spotted bit.
    pub fn fire_lines(&mut self, f: &FireIn, roll: impl FnMut() -> f32) -> Vec<LineAsk> {
        let mut first = self.flags & SPOTTED != 0;
        let out = fire_lines(&mut first, f, roll);
        if !first {
            self.flags &= !SPOTTED;
        }
        out
    }
}

/// What the combat's speech step (0x82492700, the control's step 13) reads.
#[derive(Clone, Copy, Debug, Default)]
pub struct SpeechIn {
    /// The decision maker's TEAMWORK (+0x8F4 bits 17-18): the step speaks only from 2.
    pub teamwork: u8,
    pub dt: f32,
    /// The target's held weapon: a melee one (0x823D6870: damage type 1) or a gun (0x823D68C8: info flag 0x20).
    pub target_melee: bool,
    pub target_gun: bool,
    /// The target is the player (+0x210 0, +0x211 set).
    pub target_player: bool,
    /// The intelligence has a target (0x823C3238 with 1) that it doesn't like (0x825E9CE8 not 2).
    pub target_ok: bool,
    /// 0x82485138: the combat's task 1054 descendant in state 10, 3 or 5; with none, no fire task (1042) under it.
    pub firing: bool,
    /// The speaker's own held weapon: melee, a gun.
    pub own_melee: bool,
    pub own_gun: bool,
    /// The speaker is in a vehicle (+0x23C bit 0x20000000), or its task 0x2E8 runs (vf +0x3C).
    pub in_car: bool,
    pub busy_2e8: bool,
    /// For GET_OUT_OF_CAR: the target in a car, the speaker a cop, the player at one star.
    pub target_in_car: bool,
    pub cop: bool,
    pub one_star: bool,
    /// The speaker is a gang member (0x822F2660) and the target a cop.
    pub gang: bool,
    pub target_cop: bool,
}

impl Combat {
    /// The combat's speech step (0x82492700). Its wait (+0x54) counts down by `dt`; while over 0 nothing is said.
    /// - Retreating (bit 28): at a melee-armed target FIGHT_RUN, else at a gun-armed player GUN_RUN; the wait becomes
    ///   1.5 + `roll`(-0.1, 0.1).
    /// - Else, with a target it doesn't like: the wait becomes 3 + `roll`(-0.1, 0.1), and when firing (0x82485138): with
    ///   a melee weapon FIGHT; with a gun, not on task 0x2E8 and on foot, TARGET, then at a target in a car a cop's
    ///   GET_OUT_OF_CAR (the player at one star) or GET_OUT_OF_CAR_SHOUT. A gang member fighting a cop also maybe-says
    ///   (0.05) GANG_YOU_DROP_WEAPON.
    ///
    /// All say-function lines are force 0, repeat 0. ponytail: the ambient memory step (0x8230D428 / 0x8230B3F0 with the
    /// +0x94 timer) isn't here; +0x50 (counted down beside it) isn't read.
    pub fn speech_step(&mut self, f: &SpeechIn, mut roll: impl FnMut(f32, f32) -> f32) -> Vec<LineAsk> {
        let say = |context| LineAsk { context, chance: None, repeat: false };
        let mut out = vec![];
        if f.teamwork < 2 {
            return out;
        }
        if self.talk > 0.0 {
            self.talk -= f.dt;
        }
        if self.talk > 0.0 {
            return out;
        }
        if self.flags & FLEE != 0 {
            if f.target_melee {
                out.push(say("FIGHT_RUN"));
            } else if f.target_player && f.target_gun {
                out.push(say("GUN_RUN"));
            }
            self.talk = roll(-0.1, 0.1) + 1.5;
            return out;
        }
        if !f.target_ok {
            return out;
        }
        self.talk = roll(-0.1, 0.1) + 3.0;
        if !f.firing {
            return out;
        }
        if f.own_melee {
            out.push(say("FIGHT"));
            return out;
        }
        if !f.own_gun || f.busy_2e8 || f.in_car {
            return out;
        }
        out.push(say("TARGET"));
        if f.target_in_car && f.cop {
            out.push(say(if f.target_player && f.one_star { "GET_OUT_OF_CAR" } else { "GET_OUT_OF_CAR_SHOUT" }));
        }
        if f.gang && f.target_cop {
            out.push(LineAsk { context: "GANG_YOU_DROP_WEAPON", chance: Some(0.05), repeat: false });
        }
        out
    }
}

/// The damage type number of a weapon info's `damagetype` (+0xC): its place in the reader's name table 0x82A99528
/// (`weapons::DAMAGE_TYPES`: NONE 0, MELEE 1, BULLET 2, EXPLOSIVE 3, FIRE 4, COLLISION 5, FALL 6, DROWN 7, UNKNOWN 8),
/// 0 for a name not there.
pub fn damage_type_number(name: &str) -> i32 {
    crate::weapons::DAMAGE_TYPES.iter().position(|&d| d == name).map_or(0, |k| k as i32)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn facts(bravery: u8, target_armed: u8) -> Facts {
        Facts { dt: 0.1, target: true, health: 200.0, bravery, target_armed, ..Default::default() }
    }

    /// Made with the coin on tails (bit 20 clear): the courage check alone.
    fn calm() -> Combat {
        Combat::blank(0.0)
    }

    /// A decision maker with no answer for any situation (no files).
    fn none(_: i32, _: &mut Rand) -> i32 {
        NONE
    }

    /// One answering `code` for the combat situations 107-111 and nothing for 100.
    fn says(code: i32) -> impl FnMut(i32, &mut Rand) -> i32 {
        move |s, _| if s == 100 { NONE } else { code }
    }

    #[test]
    fn level_byte_stands_for_bravery() {
        // Event 17's +0x64 = 3: a bravery 1 ped (level 0, always runs) fights the unarmed.
        let mut rng = Rand::new(5);
        let runs = (0..200).filter(|_| Combat { level: 3, ..calm() }.courage(&facts(1, 0), &mut rng)).count();
        assert_eq!(runs, 0);
    }

    #[test]
    fn courage_by_level_and_state() {
        let mut rng = Rand::new(5);
        let mut flees = |bravery, armed| (0..200).filter(|_| calm().courage(&facts(bravery, armed), &mut rng)).count();
        // Bravery 3 (level 2) fights the unarmed and melee-armed always, and runs from a damaging weapon (chance 0).
        assert_eq!((flees(3, 0), flees(3, 1)), (0, 0));
        assert!(flees(3, 2) > 195);
        // Level 0 (bravery 1) always runs, whatever the state; bravery 5 is level 3, not 4 (a mission ped's, a cop's).
        assert!(flees(1, 0) > 195 && flees(5, 2) > 195);
        // Level 1 is 0.5 x 0.66 against the unarmed: about two in three run.
        let n = flees(2, 0);
        assert!((100..170).contains(&n), "{n}");
    }

    #[test]
    fn courage_rules() {
        let mut rng = Rand::new(5);
        // Bit 20 with health under 120 + 20 x (4 - 3) = 140 runs at once; 140 and over rolls (and fights, level 2).
        let mut c = Combat { flags: MADE | COIN, ..calm() };
        assert!(c.courage(&Facts { health: 139.0, ..facts(3, 0) }, &mut rng) && c.flags & FLEE != 0);
        let mut c = Combat { flags: MADE | COIN, ..calm() };
        assert!(!c.courage(&Facts { health: 140.0, ..facts(3, 0) }, &mut rng));
        // The roll is made again only when the target's armed state rises; bit 28 stays.
        let mut c = calm();
        assert!(!c.courage(&facts(3, 1), &mut rng) && !c.courage(&facts(3, 1), &mut rng));
        assert!((0..50).any(|_| c.courage(&facts(3, 2), &mut rng)));
        assert!(c.courage(&facts(5, 0), &mut rng));
        // The first roll: a cop target runs, whatever the level.
        assert!(calm().courage(&Facts { target_cop: true, ..facts(3, 0) }, &mut rng));
        // A gang member never runs from a target it hates (level 4), but does from one it does not (level 3, a gun).
        let gang = |hates_target| Facts { gang: true, hates_target, ..facts(1, 2) };
        assert!((0..50).all(|_| !calm().courage(&gang(true), &mut rng)));
        assert!(calm().courage(&gang(false), &mut rng));
    }

    #[test]
    fn stops() {
        let mut rng = Rand::new(9);
        let f = facts(5, 0);
        // Made, it fires; the lifetime (0 = none) ends it after its seconds, counted by the control's rule 7 only (the
        // first pick 0x824A64D8 reads no +0x5C): 0.25 s is 0.15 and 0.05 after two control frames, out on the third.
        // No target ends it.
        let mut c = Combat::new(0.25, &mut rng);
        assert!(c.step(&f, &mut none, &mut rng) && c.sub == Some(Sub::Fire));
        assert!(c.step(&f, &mut none, &mut rng) && c.step(&f, &mut none, &mut rng) && !c.step(&f, &mut none, &mut rng));
        let mut c = Combat::new(0.0, &mut rng);
        assert!((0..500).all(|_| c.step(&f, &mut none, &mut rng)));
        assert!(!c.step(&Facts { target: false, ..f }, &mut none, &mut rng));
        // The +0x94 cap counts down the same way (the courage roll resets it, so after the first step).
        let mut c = calm();
        c.step(&f, &mut none, &mut rng);
        c.cap = 0.15;
        assert!(c.step(&f, &mut none, &mut rng) && !c.step(&f, &mut none, &mut rng));
    }

    #[test]
    fn retreat_and_rethink() {
        let mut rng = Rand::new(3);
        // A ped that runs from the first roll (bravery 1: level 0) is given Retreat, and it stays.
        let mut c = calm();
        assert!(c.step(&facts(1, 2), &mut none, &mut rng) && c.retreating());
        assert!((0..500).all(|_| c.step(&facts(1, 2), &mut none, &mut rng)) && c.retreating());
        // Fire's re-think timer starts at 2.5 s x 0.9..1.1; at 0 the pick is Fire again, ignored, and the timer restarts.
        let mut c = calm();
        c.step(&facts(3, 0), &mut none, &mut rng);
        assert!((2.25..=2.75).contains(&c.rethink), "{}", c.rethink);
        assert!((0..100).all(|_| c.step(&facts(3, 0), &mut none, &mut rng) && c.sub == Some(Sub::Fire) && c.rethink > 0.0));
    }

    #[test]
    fn the_decision_makers_answer() {
        let mut rng = Rand::new(3);
        // A brave unarmed ped told to charge (1905) charges and keeps at it; told to retreat (1904), it is committed.
        let mut c = calm();
        let f = facts(3, 0);
        assert!(c.step(&f, &mut says(CHARGE), &mut rng) && c.sub == Some(Sub::Charge));
        // Its re-think timer is 5.0 x rand(0.9, 1.1); at 0 the same answer makes nothing and the timer is set again.
        assert!((4.5..=5.5).contains(&c.rethink), "{}", c.rethink);
        assert!((0..100).all(|_| c.step(&f, &mut says(CHARGE), &mut rng) && c.sub == Some(Sub::Charge) && c.rethink > 0.0));
        let mut c = calm();
        assert!(c.step(&f, &mut says(RETREAT), &mut rng) && c.retreating() && c.flags & FLEE != 0);
        // The codes not ported (advance 1902) fire.
        let mut c = calm();
        assert!(c.step(&f, &mut says(1902), &mut rng) && c.sub == Some(Sub::Fire));
        // Rule 10: a Charge while the ped holds a damaging weapon is asked to abort (urgency 1) and picked again.
        let mut c = calm();
        assert!(c.step(&f, &mut says(CHARGE), &mut rng) && c.sub == Some(Sub::Charge));
        c.step(&Facts { armed: true, ..f }, &mut says(NONE), &mut rng);
        assert_eq!(c.abort(), Some(1));
    }

    /// The first pick asks 107, then 108 (109 at a target in a vehicle) when it has no answer, then defaults to Fire, whose
    /// maker asks 100; the draws in the game's order: the courage roll, the decision maker's, the re-think's rand.
    #[test]
    fn the_first_pick_asks_107_then_108() {
        let mut asked = vec![];
        let mut rng = Rand::new(11);
        let mut ask = |s: i32, r: &mut Rand| {
            asked.push(s);
            r.next();
            NONE
        };
        let mut c = calm();
        let before = rng;
        assert!(c.step(&facts(3, 0), &mut ask, &mut rng) && c.sub == Some(Sub::Fire));
        assert_eq!(asked, [107, 108, 100]);
        let mut want = before;
        want.float_in_range(0.0, 1.0);
        for _ in [107, 108, 100] {
            want.next();
        }
        let rethink = 2.5 * want.float_in_range(0.9, 1.1);
        assert!(rng == want && c.rethink == rethink, "{}", c.rethink);
        // 107 answered: 108 is not asked. A target in a vehicle: 109, and the re-think's base is 1.0.
        asked.clear();
        let mut c = calm();
        c.step(&facts(3, 0), &mut |s, _| { asked.push(s); if s == 107 { CHARGE } else { NONE } }, &mut rng);
        assert_eq!((asked.as_slice(), c.sub), ([107].as_slice(), Some(Sub::Charge)));
        asked.clear();
        let mut c = calm();
        c.step(&Facts { target_in_vehicle: true, ..facts(3, 0) }, &mut |s, _| { asked.push(s); NONE }, &mut rng);
        assert_eq!(asked, [107, 109, 100]);
        assert!((0.9..=1.1).contains(&c.rethink), "{}", c.rethink);
        // 0x824943D0 answering 1931: 1.5 x rand(0.9, 1.1).
        let mut c = calm();
        c.step(&facts(3, 0), &mut |s, _| if s == 100 { REPOSITION } else { NONE }, &mut rng);
        assert!((1.35..=1.65).contains(&c.rethink), "{}", c.rethink);
    }

    /// The next pick: an unanswered code (200) makes nothing, so a finished subtask ends the combat and a re-think keeps
    /// the running one; a re-think to the running code makes nothing (the subtask is not remade), another code is made.
    #[test]
    fn the_next_pick() {
        let mut rng = Rand::new(4);
        let f = facts(3, 0);
        let mut c = calm();
        assert!(c.step(&f, &mut none, &mut rng) && std::mem::take(&mut c.remade));
        assert!(!c.finished(&f, &mut none, &mut rng));
        assert!(c.finished(&f, &mut says(FIRE), &mut rng) && c.sub == Some(Sub::Fire));
        // No target: nothing, whatever the answer.
        assert!(!c.finished(&Facts { target: false, ..f }, &mut says(FIRE), &mut rng));
        let mut c = calm();
        c.step(&f, &mut none, &mut rng);
        c.remade = false;
        for ask in [&mut says(FIRE) as &mut Ask, &mut none] {
            c.rethink = 0.05;
            assert!(c.step(&f, ask, &mut rng) && !c.remade && c.sub == Some(Sub::Fire) && c.rethink > 2.0);
        }
        c.rethink = 0.05;
        assert!(c.step(&f, &mut says(CHARGE), &mut rng) && c.remade && c.sub == Some(Sub::Charge));
    }

    /// The retreat that fires: a Retreat whose next is Fire (+0x14 4, +0x20 1901) is followed by a Fire timed 5 s whose
    /// next is Retreat; when its time is out it ends and the Retreat comes back (bit 28 holds).
    #[test]
    fn the_retreat_that_fires() {
        let mut rng = Rand::new(6);
        let f = facts(1, 2);
        let mut c = calm();
        assert!(c.step(&f, &mut none, &mut rng) && c.retreating());
        c.fire_tick(0.1);
        assert!(c.finished(&f, &mut none, &mut rng) && c.retreating() && !c.fire_out());
        c.then = (4, FIRE);
        assert!(c.finished(&f, &mut none, &mut rng) && c.sub == Some(Sub::Fire) && c.then == (4, RETREAT));
        assert!((0..49).all(|_| {
            c.fire_tick(0.1);
            !c.fire_out()
        }));
        c.fire_tick(0.2);
        assert!(c.fire_out());
        assert!(c.finished(&f, &mut says(FIRE), &mut rng) && c.retreating());
    }

    #[test]
    fn mission_arrest_and_gang_rules() {
        let mut rng = Rand::new(5);
        // Case 3: a mission ped as the target of a non-player runs, unless it is the player's friend; case 5: an arrest.
        let f = facts(5, 0);
        assert!(calm().courage(&Facts { target_mission: true, ..f }, &mut rng));
        assert!(!calm().courage(&Facts { target_mission: true, target_friend: true, ..f }, &mut rng));
        assert!(!calm().courage(&Facts { target_mission: true, target_player: true, ..f }, &mut rng));
        assert!(calm().courage(&Facts { target_arrested: true, ..f }, &mut rng));
        // Rule 11: a gang member that hates its unarmed target still runs when 2 nearby gang peds are fighting; not at 1,
        // nor against a damaging weapon (it fights on, level 4).
        let g = |fights, armed| Facts { gang: true, hates_target: true, fights, target_armed: armed, ..facts(3, armed) };
        assert!(calm().courage(&g(2, 0), &mut rng) && !calm().courage(&g(1, 0), &mut rng) && !calm().courage(&g(2, 2), &mut rng));
        // Fighting on in group 6 against the player gives the 20 s cap; any other does not.
        let mut c = calm();
        assert!(!c.courage(&Facts { group6: true, target_player: true, ..g(0, 0) }, &mut rng) && c.cap == 20.0);
        let mut c = calm();
        assert!(!c.courage(&Facts { target_player: true, ..g(0, 0) }, &mut rng) && c.cap == 0.0);
    }

    #[test]
    fn firing_factor_rule() {
        // Factor: tripled each frame within 4 m, 0.2 hidden in cover farther off, else kept.
        assert_eq!((firing_factor(1.0, 15.9, false), firing_factor(3.0, 16.0, true), firing_factor(3.0, 16.0, false)), (3.0, 0.2, 3.0));
    }

    /// Slot 25's gun (0x824A1FD8): S 1 by FIRE_RATE at the ped alone (flags 0x2000008); S 0 / 2 the aim-only kind 2 at
    /// the ped and place, with the fire flag only with F; S 3 kind 2 that fires, and against a non-player it sets the
    /// Fire's bit 4, after which even S 1 aims only; arrest mode kind 2 for good (-1) and bit 5.
    #[test]
    fn the_fires_gun() {
        let mut rng = Rand::new(1);
        let mut bits = 0;
        assert_eq!(make_gun(false, 1, false, true, false, &mut bits, 0, &mut rng), GunMake { kind: 4, aim: 0.5, flags: 0x200_0008, at_place: false });
        assert_eq!(make_gun(false, 1, false, true, false, &mut bits, 2, &mut rng).kind, 7);
        let g = make_gun(false, 1, false, true, false, &mut bits, 3, &mut rng);
        assert!(g.kind == 5 && (1.8..=2.2).contains(&g.aim));
        assert_eq!(make_gun(false, 2, false, true, false, &mut bits, 0, &mut rng), GunMake { kind: 2, aim: 2.0, flags: 0x200_0001, at_place: true });
        assert_eq!(make_gun(false, 0, true, true, false, &mut bits, 0, &mut rng).flags, 0x200_0005);
        // S 3 at the player: the picker's gun (bit 4 is for a non-player); at another ped bit 4, then kind 2 that fires.
        assert_eq!((make_gun(false, 3, false, true, false, &mut bits, 0, &mut rng).flags, bits), (0x200_0008, 0));
        assert_eq!(make_gun(false, 3, false, false, false, &mut bits, 0, &mut rng).flags, 0x200_0005);
        assert_eq!((bits, make_gun(false, 1, false, false, false, &mut bits, 0, &mut rng).kind), (FIRE_AIM, 2));
        // The physical response a React (1312): flag 8 on any gun.
        let mut bits = 0;
        assert_eq!(make_gun(false, 2, false, true, true, &mut bits, 0, &mut rng).flags, 0x200_0009);
        // The picker draws the CRT's rand whatever the rate: rate 3's time is 2 x (r / 32767 x 0.2 + 0.9), bit for bit.
        let mut a = Rand::new(77);
        let mut b = a;
        let (k, t) = gun_kind(3, &mut a);
        let r = b.next() as f32 * f32::from_bits(0x3800_0100);
        assert_eq!((k, t.to_bits()), (5, (r.mul_add(f32::from_bits(0x3E4C_CCD0), f32::from_bits(0x3F66_6666)) * 2.0).to_bits()));
        let mut c = Rand::new(77);
        assert_eq!((gun_kind(0, &mut c), c), ((4, 0.5), a));
        let mut bits = 0;
        assert_eq!((make_gun(true, 1, false, true, false, &mut bits, 0, &mut rng).aim, bits), (-1.0, FIRE_ARREST));
        // Step 4: a gun that fires where it should not is remade; one that does not fire where it should, unless bit 7 or 4.
        assert!(gun_stale(0x200_0005, 1, false, 0) && !gun_stale(0x200_0008, 1, false, 0));
        assert!(gun_stale(0x200_0001, 3, false, 0) && !gun_stale(0x200_0001, 3, false, FIRE_AIM) && !gun_stale(0x200_0005, 2, true, 0));
        // The retarget's gun: kind 7 (2 with bit 7 or 5), aim rand(0.5, 0.75).
        let g = retarget_gun(0, &mut rng);
        assert!(g.kind == 7 && (0.5..=0.75).contains(&g.aim) && retarget_gun(FIRE_ARREST, &mut rng).kind == 2);
    }

    /// 0x825EB428: within the cone, the one turned least clockwise from the excluded ped (or the ped's forward); a blocked
    /// record is skipped when asked; 0x825E9CE8 answers 0 for a ped with no slot.
    #[test]
    fn the_targeting_cone() {
        let mut t = Targeting::<u32>::default();
        // From the origin facing +Y: 1 ahead, 2 a little to the right (heading below), 3 a little to the left, 4 behind.
        for (p, at) in [(1, [0.0, 10.0, 0.0]), (2, [3.0, 10.0, 0.0]), (3, [-3.0, 10.0, 0.0]), (4, [0.0, -10.0, 0.0])] {
            t.add(p, at, 1);
        }
        let ahead = crate::aisteer::heading(0.0, 1.0);
        let pick = |t: &Targeting<u32>, x: Option<u32>, skip| {
            let ex = x.map(|x| (x, t.slots.iter().flatten().find(|s| s.ped == x).unwrap().at));
            t.best_in_cone([0.0; 3], [0.0, 1.0], ahead, RETARGET_CONE, ex, skip, false)
        };
        // The forward's heading is 1's: 1 scores 0, the best.
        assert_eq!(pick(&t, None, true), Some(1));
        // Excluding 1, the next one round the other way from 1 (a turn of 0 or more scores -d; less, |d| - 1000).
        let h3 = crate::aisteer::heading(-3.0, 10.0);
        assert!(crate::actiontable::wrap(h3 - ahead) > 0.0);
        assert_eq!(pick(&t, Some(1), true), Some(3));
        // 4 is out of the cone; a blocked 3 is skipped.
        t.slots.iter_mut().flatten().find(|s| s.ped == 3).unwrap().state = 2;
        assert_eq!((pick(&t, Some(1), true), pick(&t, Some(1), false)), (Some(2), Some(3)));
        assert_eq!((t.state(3), t.state(9)), (2, 0));
        t.remove(3);
        assert_eq!(t.state(3), 0);
    }

    /// Item 1: the dying target (the upkeep keeps it only for a ped that goes for the injured, 0x822FF050, whose pick then
    /// asks and whose adjustments execute it, 1911 made as Fire); the skip set and a finished investigate are not
    /// reachable here (no such subtask runs); the advance's books; the hold.
    #[test]
    fn the_dying_target_and_the_hold() {
        let mut rng = Rand::new(2);
        // The first pick (0x824A64D8) wants only the target's pointer (+0x3C), so it makes its subtask; the control's
        // upkeep (rule 10, 0x82492440) drops the hurt target and stops it the next frame.
        let hurt = Facts { target_hurt: true, ..facts(3, 0) };
        let mut c = calm();
        assert!(c.step(&hurt, &mut none, &mut rng) && !c.step(&hurt, &mut none, &mut rng) && c.abort() == Some(0));
        let mut c = calm();
        let injured = Facts { for_injured: true, gang: true, population: true, gun: true, ..hurt };
        assert!(c.step(&injured, &mut none, &mut rng) && c.sub == Some(Sub::Fire));
        assert_eq!(c.adjust(&injured, CHARGE, &mut rng), EXECUTE);
        assert_eq!(c.adjust(&Facts { gang: false, injured_reaction: 2, ..injured }, CHARGE, &mut rng), EXECUTE);
        assert_eq!(c.adjust(&Facts { gun: false, ..injured }, CHARGE, &mut rng), CHARGE);
        assert!(SKIP.contains(&319) && !SKIP.contains(&FIRE));
        // The books: an advance that ended in cover counts up +0x9BD and sets +0x74 to 0.8-1.2 x 4; one that did not resets.
        let mut c = calm();
        c.books(true, true, &mut rng);
        assert!(c.covers == 1 && c.flags & 1 << 15 != 0 && (3.2..=4.8).contains(&c.t74));
        c.books(false, false, &mut rng);
        assert_eq!(c.covers, 0);
        // +0x74 holds the Fire's re-think until it is out (rule 2 counts it down).
        let f = facts(3, 0);
        let mut c = calm();
        c.step(&f, &mut none, &mut rng);
        (c.t74, c.rethink) = (0.25, 0.05);
        assert!(c.step(&f, &mut says(CHARGE), &mut rng) && c.sub == Some(Sub::Fire));
        (0..3).for_each(|_| assert!(c.step(&f, &mut says(CHARGE), &mut rng)));
        assert_eq!(c.sub, Some(Sub::Charge));
        // The hold: a re-think to advance while firing within 30 m at a clear record makes nothing until the time firing
        // reaches 0x824914C8's (CAUTION 1: 50 s); past 30 m, or blocked, the advance (made as Fire) is remade.
        let near = Facts { caution: 1, target_dist: 10.0, line_state: 1, ..f };
        let mut c = calm();
        c.step(&near, &mut none, &mut rng);
        c.remade = false;
        c.rethink = 0.05;
        assert!(c.step(&near, &mut says(ADVANCE), &mut rng) && !c.remade && c.firing > 0.0);
        for g in [Facts { target_dist: 31.0, ..near }, Facts { line_state: 2, ..near }] {
            c.rethink = 0.05;
            assert!(c.step(&g, &mut says(ADVANCE), &mut rng) && std::mem::take(&mut c.remade) && c.sub == Some(Sub::Fire));
        }
        c.rethink = 0.05;
        c.firing = 50.0;
        assert!(c.step(&near, &mut says(ADVANCE), &mut rng) && c.remade);
        assert_eq!((c.hold(&Facts { caution: 2, ..f }), c.hold(&Facts { caution: 0, ..f })), (25.0, 999.0));
        // Rule 11: the time firing goes back to 0 at an alertness of 70 when the move set's first case fails.
        let mut c = calm();
        c.step(&f, &mut none, &mut rng);
        c.step(&Facts { standing_style: 1, alertness: 69, ..f }, &mut none, &mut rng);
        assert!(c.firing > 0.0);
        c.step(&Facts { standing_style: 1, alertness: 70, ..f }, &mut none, &mut rng);
        assert_eq!(c.firing, 0.0);
    }

    /// Items 2 and 3: the Fire's cover chance is drawn only for a ped that can use cover (WILL_USE_COVER, set by the ped's
    /// setup), and the re-think after nothing new gets + rand(5, 8) for a population ped while the player is inside.
    #[test]
    fn cover_chance_and_indoor_rethink() {
        let armed = Facts { armed: true, ..facts(3, 0) };
        let draws = |f: &Facts| {
            let (mut c, mut rng) = (calm(), Rand::new(7));
            // 0x824847C0 has cleared the arrest bit (a non-cop), as every control frame does first.
            c.flags &= !ARREST;
            let before = rng;
            c.adjust(f, FIRE, &mut rng);
            rng != before
        };
        assert!(draws(&armed) && !draws(&Facts { no_cover: true, ..armed }) && !draws(&Facts { cover: true, ..armed }));
        assert_eq!(calm().adjust(&Facts { navigation_style: 1, ..armed }, SEEK_COVER, &mut Rand::new(1)), NONE);
        let mut rng = Rand::new(8);
        let inside = Facts { population: true, player_inside: true, ..facts(3, 0) };
        let mut c = calm();
        c.step(&inside, &mut none, &mut rng);
        c.rethink = 0.05;
        c.step(&inside, &mut none, &mut rng);
        assert!((2.25 + 5.0..=2.75 + 8.0).contains(&c.rethink), "{}", c.rethink);
        let mut c = calm();
        c.step(&inside, &mut says(CHARGE), &mut rng);
        c.rethink = 0.05;
        c.step(&inside, &mut says(CHARGE), &mut rng);
        assert!((4.5 + 5.0..=5.5 + 8.0).contains(&c.rethink), "{}", c.rethink);
        // Not inside, or a mission ped: none.
        let mut c = calm();
        c.step(&Facts { population: false, ..inside }, &mut none, &mut rng);
        c.rethink = 0.05;
        c.step(&Facts { population: false, ..inside }, &mut none, &mut rng);
        assert!(c.rethink < 3.0);
    }

    /// Item 7: where the control asks the running subtask to abort (0x82164FB8) and what a refusal leaves: a re-think's
    /// new subtask deleted and the timer set again; rule 10's re-pick not made (bit 28 kept, the rules after it run); a
    /// lifetime retried at 0.0001; a lost target's stop not made.
    #[test]
    fn a_refused_abort_keeps_the_subtask() {
        let mut rng = Rand::new(12);
        let f = facts(3, 0);
        let mut c = calm();
        c.step(&f, &mut none, &mut rng);
        assert_eq!(c.abort(), None);
        c.rethink = 0.05;
        assert!(c.step(&f, &mut says(CHARGE), &mut rng) && c.sub == Some(Sub::Charge) && c.abort() == Some(1));
        assert!(c.refused(&f, &mut says(CHARGE), &mut rng) && c.sub == Some(Sub::Fire) && (2.25..=2.75).contains(&c.rethink));
        // A gun drawn on it: the roll runs (bravery 3 against state 2), the re-pick is asked for.
        let armed = facts(3, 2);
        let before = c.firing;
        assert!(c.step(&armed, &mut none, &mut rng) && c.retreating() && c.abort() == Some(1));
        assert!(c.refused(&armed, &mut none, &mut rng) && c.sub == Some(Sub::Fire) && c.flags & FLEE != 0 && c.firing > before);
        // Lifetimes: out, a stop at urgency 1; refused, 0.0001.
        let mut c = Combat { life: 0.15, ..calm() };
        c.step(&f, &mut none, &mut rng);
        assert!(c.step(&f, &mut none, &mut rng) && !c.step(&f, &mut none, &mut rng) && c.abort() == Some(1));
        assert!(c.refused(&f, &mut none, &mut rng) && c.life == 0.0001 && c.sub == Some(Sub::Fire));
        // No target: a stop at urgency 0, kept when refused.
        let lost = Facts { target: false, ..f };
        assert!(!c.step(&lost, &mut none, &mut rng) && c.abort() == Some(0));
        assert!(c.refused(&lost, &mut none, &mut rng) && c.sub == Some(Sub::Fire));
        // Rule 11's move set: firing at STANDING_STYLE 0; not at 1 with the alertness under 70 and bits 21 / 22 clear.
        assert!(c.move_set(&f) && !c.move_set(&Facts { standing_style: 1, ..f }));
    }

    /// Just spotted: a cop's SPOT_SUSPECT once; then on a draw under 0.1 the car, crowd and weapon lines.
    #[test]
    fn fire_lines_by_case() {
        let mut first = true;
        let cop = FireIn { cop: true, firing: true, target_player: true, ..FireIn::default() };
        assert_eq!(fire_lines(&mut first, &cop, || 0.0), vec![LineAsk { context: "SPOT_SUSPECT", chance: None, repeat: true }]);
        assert!(!first);
        assert!(fire_lines(&mut first, &cop, || 0.2).is_empty());
        let car = FireIn { target_in_car: true, one_star: true, ..cop };
        assert_eq!(fire_lines(&mut first, &car, || 0.0)[0].context, "GET_OUT_OF_CAR");
        let crowd = FireIn { friends: 2, target_weapon: Some((2, 7)), cops_near: 3, swat_near: 1, ..cop };
        let c: Vec<_> = fire_lines(&mut first, &crowd, || 0.0).iter().map(|a| a.context).collect();
        assert_eq!(c, ["MOVE_IN", "SURROUNDED", "GET_DOWN", "EVACUATE_AREA", "SPOT_GUN"]);
        let alone = FireIn { cops_near: 1, wanted: 4, ..cop };
        let c: Vec<_> = fire_lines(&mut first, &FireIn { firing: false, ..alone }, || 0.0).iter().map(|a| a.context).collect();
        assert_eq!(c, ["REQUEST_BACKUP", "REQUEST_SWAT"]);
        assert!(fire_lines(&mut first, &FireIn { firing: false, ..crowd }, || 0.0).is_empty());
    }

    /// The speech step: TEAMWORK under 2 says nothing; a wait between lines; FIGHT with a melee weapon, TARGET with a gun.
    #[test]
    fn speech_step_by_case() {
        let mut c = Combat { flags: MADE, ..calm() };
        let base = SpeechIn { teamwork: 2, dt: 0.1, target_ok: true, firing: true, own_gun: true, target_player: true, ..SpeechIn::default() };
        assert!(c.speech_step(&SpeechIn { teamwork: 1, ..base }, |_, _| 0.0).is_empty());
        assert_eq!(c.speech_step(&base, |_, _| 0.0).iter().map(|a| a.context).collect::<Vec<_>>(), ["TARGET"]);
        assert!(c.speech_step(&base, |_, _| 0.0).is_empty());
        let mut c = Combat { flags: MADE, ..calm() };
        assert_eq!(c.speech_step(&SpeechIn { own_melee: true, ..base }, |_, _| 0.0)[0].context, "FIGHT");
        let mut c = Combat { flags: MADE | FLEE, ..calm() };
        assert_eq!(c.speech_step(&SpeechIn { target_gun: true, ..base }, |_, _| 0.0)[0].context, "GUN_RUN");
    }
}
