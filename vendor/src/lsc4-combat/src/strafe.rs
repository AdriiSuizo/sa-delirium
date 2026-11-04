//! The player's strafing move (board rows PD5d and PD5g): 0x823C78A0 turns the stick into two axes on the ped's own
//! right and forward, with a speed cap; 0x82418290 eases the move blend's two ratios toward those axes. Single
//! precision, fused multiply-adds in the game's order.
//!
//! The strafe clip blends 0x824157B0 (4 ways) and 0x82415F98 (8 ways) turn the two ratios into clips: `strafe_clips`.
//!
//! Standing, they play the turn-on-the-spot clips or the idle: `strafe_stand`. The strafing heading turn is 0x82413460:
//! `strafe_turn`.
//!
//! Not here: the network-game and carried-object and episode-2 cap rules and the script-hint turn (see the ponytail
//! notes). The move blend's clamp (0x82413F78) is not here either.

use crate::{actiontable::wrap, mathrt};

/// The raw stick range (0x82AA1B58 = 120).
const STICK_RANGE: f32 = 120.0;
/// A stick shorter than this is idle (0x82019674).
const IDLE: f32 = 0.25;
/// Walk, run and sprint caps (0x82AA1B5C, 0x82AA1B60, 0x82AA1B64).
const WALK: f32 = 1.0;
const RUN: f32 = 2.0;
const SPRINT: f32 = 3.0;
/// A slight push creeps up to this length (0x82AA1B68).
const CREEP: f32 = 0.15;
/// The task's ratio is held to this (0x8200C3B8).
const RATIO_MAX: f32 = 3.0;
/// The ease rate, per second (0x82A99220), and Ã—10 with +0x50 bit 15 (0x820188E0).
const EASE_RATE: f32 = 8.0;
const EASE_FAST: f32 = 10.0;

/// What one frame of the strafing move reads.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct StrafeInput {
    /// The raw stick x and y, as the pad gives them (y is negative when pushed up).
    pub stick: (i32, i32),
    /// The camera heading (0x821ED360 on the final camera frame).
    pub camera_heading: f32,
    /// The ped's matrix rows 0 (right) and 1 (forward), as x, y, z.
    pub right: [f32; 3],
    pub forward: [f32; 3],
    /// The run value from 0x823D0F30 (mode 0), or `None` when the run button does not apply: the sprint block
    /// (player info +0x404) is set, or 0x82AA1B19 is clear. Read it only for an off-centre stick.
    pub run: Option<f32>,
    /// 0x82FD1E3E: while the stick is moved, a run value is at least 1.
    pub run_hold: bool,
    /// The task's idle byte (+0x22) before this frame.
    pub idle: bool,
}

/// The result of one frame of the strafing move.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct StrafeMove {
    /// The move blend's ratios (0x82651600 takes forward, then sideways: +0x10 and +0xC).
    pub sideways: f32,
    pub forward: f32,
    /// The move length, held to 0..3, for the task's +0x18.
    pub length: f32,
    /// The task's idle byte (+0x22) after this frame.
    pub idle: bool,
}

/// The strafing move (0x823C78A0). `allowed` is the move-direction check (0x826041E8) given the world direction;
/// when it says no, both axes are zero.
pub fn strafe_move(input: &StrafeInput, allowed: impl FnOnce(&[f32; 3]) -> bool) -> StrafeMove {
    const INV_RANGE: f32 = 1.0 / STICK_RANGE;
    let x = (INV_RANGE * input.stick.0 as f32).clamp(-1.0, 1.0);
    let y = (INV_RANGE * -(input.stick.1 as f32)).clamp(-1.0, 1.0);
    let stick_len = y.mul_add(y, x * x).sqrt();
    // A centred stick returns here, before the run value is read or idle is set (as in the game).
    if stick_len <= 0.0 {
        return StrafeMove { sideways: 0.0, forward: 0.0, length: 0.0, idle: input.idle };
    }
    let mut idle = input.idle || stick_len < IDLE;

    // The stick turned by the camera: atan2(-x, y) + camera heading, wrapped (0x8245BC88). The world direction is
    // (-sin, cos, 0) (0x829FFFD8, 0x829FFD48, 0x829FFE18 in double, then single).
    let heading = wrap(mathrt::atan2(-x as f64, y as f64) as f32 + input.camera_heading);
    let (sin, cos) = (mathrt::sin(heading as f64) as f32, mathrt::cos(heading as f64) as f32);
    let dir = [-sin, cos, 0.0];
    let (sideways, forward) = if allowed(&dir) {
        let along = |row: &[f32; 3]| row[0].mul_add(dir[0], row[2].mul_add(dir[2], row[1] * dir[1]));
        ((along(&input.right) * stick_len).clamp(-1.0, 1.0), (along(&input.forward) * stick_len).clamp(-1.0, 1.0))
    } else {
        (0.0, 0.0)
    };

    // The cap. Default walk (0x82AA1B5C). Run value: above 1 sprints, above 0 runs (0x82AA1B60 / 1B64). A run value
    // above 0 clears the idle byte; with the hold, a moving stick keeps the run value at 1 or more.
    // ponytail: the network-game cap (0x826C4CA8 or 0x830FB0EA: 2, or 1 with the carried-object and episode-2 rules
    // at ped +0x80 and +0x270) is left out; a network game gets the single-player cap. Add when network play is ported.
    let mut cap = WALK;
    if let Some(run) = input.run {
        if run > 0.0 {
            idle = false;
        }
        // ponytail: the script-hint branch (0x82FD1E2C, melee turn 0x822F10A8) is left out. Add when script hints are.
        let run = if input.run_hold && !idle { run.max(1.0) } else { run };
        if run > 1.0 {
            cap = SPRINT;
        } else if run > 0.0 {
            cap = RUN;
        }
    }
    let (mut sideways, mut forward) = (cap * sideways, cap * forward);

    // A slight push creeps up to 0.15. ponytail: a vetoed push has length 0 and the game divides 0.15 by it (NaN
    // into the blend); here it stays a zero move. Add when the blend's clamp (0x82413F78) is ported and it matters.
    let len = forward.mul_add(forward, sideways * sideways).sqrt();
    if len > 0.0 && len < CREEP {
        let k = CREEP / len;
        sideways *= k;
        forward *= k;
    }
    let length = forward.mul_add(forward, sideways * sideways).sqrt().min(RATIO_MAX);
    StrafeMove { sideways, forward, length, idle }
}

/// The strafe step's two move-blend ratios (0x82418290): `sideways` (+4) eases toward `wanted_sideways` (+0xC) and
/// `forward` (+8) toward `wanted_forward` (+0x10).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct StrafeRatios {
    pub sideways: f32,
    pub forward: f32,
    pub wanted_sideways: f32,
    pub wanted_forward: f32,
}

impl StrafeRatios {
    /// One frame of the step. Returns true when the start timer (+0x74) is to be stamped: both ratios are near zero
    /// (squared length under 0.001, 0x82000D58) before the step. `dt` is the frame time (0x82C6C2AC). `fast` is
    /// +0x50 bit 15 (the ease Ã—10). `limit` holds both ratios to -1..1: set when the clip set and its partner
    /// both lack the running-strafe flag (byte +0x378, 0x01).
    // ponytail: the clip values (+2 into 0x82FD4510 / 4514) are left out. 0x82413460 (`strafe_turn`), the turn step
    // (its rate Ã— dt into +0x30 and 0x82FD44F0) and the clip blend (`strafe_clips` / `strafe_stand`) are the caller's.
    pub fn step(&mut self, dt: f32, fast: bool, limit: bool) -> bool {
        let near_zero = self.sideways.mul_add(self.sideways, self.forward * self.forward) < 0.001;
        let mut rate = EASE_RATE * dt;
        if fast {
            rate *= EASE_FAST;
        }
        // ponytail: the game's sign-change test uses a Ã— 1.0 (0x82A99224) on the rate, so a reversal is the same step;
        // not ported. Add when that constant is not 1.
        self.sideways = ease(self.sideways, self.wanted_sideways, rate);
        self.forward = ease(self.forward, self.wanted_forward, rate);
        if limit {
            self.sideways = self.sideways.clamp(-1.0, 1.0);
            self.forward = self.forward.clamp(-1.0, 1.0);
        }
        near_zero
    }
}

/// The 4-way blend's walk and run clips (0x8204374C, 0x82018A08), straight ahead and on round to the right: Walk,
/// Walk_Strafe_R, _B, _L; Run, Run_Strafe_R, _B, _L. The ids are 0xE + the index into the move clip names (0x82A97824).
const WALK_4: [u32; 4] = [0x1C, 0x3E, 0x3F, 0x3D];
const RUN_4: [u32; 4] = [0x28, 0x41, 0x42, 0x40];
/// The 8-way blend's (0x82018A3C, 0x82018A1C), with the 45s between: Walk, _R45, _R, _BR45, _B, _BL45, _L, _L45.
const WALK_8: [u32; 8] = [0x1C, 0x44, 0x3E, 0x46, 0x3F, 0x45, 0x3D, 0x43];
const RUN_8: [u32; 8] = [0x28, 0x48, 0x41, 0x4A, 0x42, 0x49, 0x40, 0x47];
/// Idle (0x0F).
const IDLE_CLIP: u32 = 0x0F;

/// The strafing heading turn's result (0x82413460).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct StrafeTurn {
    /// The ped's heading and wanted heading (+0x9F0, +0x9F4), wrapped (0x8245BC88).
    pub heading: f32,
    pub wanted: f32,
    /// The rate's target (+0x2C) and the rate (+0x28), in radians a second.
    pub target: f32,
    pub rate: f32,
    /// Under half a degree off, the gap itself goes into the step (+0x30). The strafe step overwrites it with rate Ã— dt.
    pub snap: Option<f32>,
}

/// The strafing heading turn (0x82413460, from the strafe step 0x82418290). The gap (wanted - heading, brought into
/// -pi..pi once) under half a degree (0x82018970) is snapped and the target is 0; otherwise the target is the gap Ã· 0.3
/// (Ã— 3.33333, 0x8201896C), held to the turn speed (ped +0x9F8, degrees) Ã— pi/180 Ã— 50. The rate (+0x28) moves toward
/// the target by 50 a second (0x82A99234 / 0x82A99238), never past it.
pub fn strafe_turn(heading: f32, wanted: f32, turn_speed: f32, rate: f32, dt: f32) -> StrafeTurn {
    let (heading, wanted) = (wrap(heading), wrap(wanted));
    let mut gap = wanted - heading;
    if gap > std::f32::consts::PI {
        gap -= std::f32::consts::TAU;
    } else if gap < -std::f32::consts::PI {
        gap += std::f32::consts::TAU;
    }
    let mut snap = None;
    if gap.abs() < 0.008_726_646 {
        snap = Some(gap);
        gap = 0.0;
    }
    let cap = turn_speed * 0.017_453_292 * 50.0;
    let mut target = gap * 3.333_333_3;
    if target > cap {
        target = cap;
    } else if target < -cap {
        target = -cap;
    }
    // Four branches in the game (by the rate's side of Â±0.01), all at 50 a second: toward the target, never past it.
    let rate = if target > rate { dt.mul_add(50.0, rate).min(target) } else { (-dt).mul_add(50.0, rate).max(target) };
    StrafeTurn { heading, wanted, target, rate, snap }
}

/// What the strafe clip blend's standing part reads (0x824157B0 / 0x82415F98 when both ratios are 0).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Stand {
    /// wrap(wanted - heading): ped +0x9F4 - +0x9F0 through 0x8245BC88.
    pub gap: f32,
    /// The turn rate (+0x28).
    pub rate: f32,
    /// +0x50 bit 15: blends at 4 a second, not 0.5.
    pub fast: bool,
    /// The set turns on the spot (+0x378 0x800000).
    pub turns: bool,
    /// The set has a shuffle (+0x378 0x200000, with its +0 or +4 dictionary set).
    pub shuffle: bool,
    /// Still for over 250 ms (now - +0x74) and crouched (0x822F03C0).
    pub crouched_still: bool,
    /// +0x14 and +0x18 are both 0.
    pub was_still: bool,
    /// Turn_360_L (0x10) and Turn_360_R (0x11) are on the clip player (ped +0x78, 0x824450D0).
    pub turn_l: bool,
    pub turn_r: bool,
    /// Shuffle_Stop (0x4B) on the clip player, and its +0x46 bit 2.
    pub shuffle_stop: Option<bool>,
    /// The 8-way blend (0x82415F98): without a shuffle it asks for nothing where the 4-way asks for the idle, and the
    /// other way round once Shuffle_Stop plays.
    pub eight: bool,
}

/// What the standing part asks of the clip player.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum StandAsk {
    /// Blend clip `id` in at `blend` a second (0x82413068), and when it plays, set its rate (+0x54).
    Play { id: u32, blend: f32, rate: Option<f32> },
    /// Blend clip `id` out (0x82455C68 with -1).
    BlendOut(u32),
}

/// The standing part's turn step factor (0x82AA3054): +0x30 Ã— 2.75 on the frame it starts the idle or Shuffle_Stop.
pub const STAND_STEP: f32 = 2.75;

/// The strafe clip blend standing (0x824157B0 to 0x82415BA8; 0x82415F98's is the same). With a set that turns and not
/// crouched still: a turn rate over 0.1 degree a second either way plays Turn_360_L (positive) or _R at a rate of
/// |8 Ã— gap| held to 0.25..1.5 (0x82AA3048..3050). Otherwise the turn clips blend out, the heading snaps to the wanted
/// (`snap`, sets that turn) and the idle (0x0F) or Shuffle_Stop (0x4B) is asked for. The caller scales its turn step by
/// `STAND_STEP` after this.
pub fn strafe_stand(s: &Stand) -> (Vec<StandAsk>, bool) {
    let blend = if s.fast { 4.0 } else { 0.5 };
    let mut asks = vec![];
    if s.turns && !s.crouched_still {
        let turn = if s.rate > 0.001_745_329_3 { Some(0x10) } else if s.rate < -0.001_745_329_3 { Some(0x11) } else { None };
        if let Some(id) = turn {
            let r = (8.0 * s.gap).abs();
            let r = if r - 0.25 >= 0.0 { r } else { 0.25 };
            let r = if r - 1.5 >= 0.0 { 1.5 } else { r };
            return (vec![StandAsk::Play { id, blend: 4.0, rate: Some(r) }], false);
        }
        asks.extend(s.turn_l.then_some(StandAsk::BlendOut(0x10)));
        asks.extend(s.turn_r.then_some(StandAsk::BlendOut(0x11)));
    }
    let (mut turn_l, mut turn_r) = (s.turn_l, s.turn_r);
    if !s.turns {
        asks.extend(turn_l.then_some(StandAsk::BlendOut(0x10)));
        asks.extend(turn_r.then_some(StandAsk::BlendOut(0x11)));
        (turn_l, turn_r) = (false, false);
    }
    let idle = |blend| StandAsk::Play { id: IDLE_CLIP, blend, rate: None };
    let shuffle = |blend| StandAsk::Play { id: 0x4B, blend, rate: None };
    if !s.crouched_still && !s.fast && !(s.was_still && !turn_l && !turn_r) {
        if s.shuffle {
            asks.push(shuffle(blend));
        } else if !s.eight {
            asks.push(idle(0.2));
        }
    } else {
        match s.shuffle_stop {
            Some(true) => asks.push(idle(0.2)),
            Some(false) if s.shuffle => asks.push(shuffle(1.0)),
            Some(false) if s.eight => asks.push(idle(0.2)),
            Some(false) => {}
            None => asks.push(idle(blend)),
        }
    }
    (asks, s.turns)
}

/// The strafe clip blend while moving (0x824157B0 from 0x82415BAC; 0x82415F98, `eight`, when the set has +0x378 0x02):
/// the clips added to the move blend's list (0x82FD451C, by 0x82412AB8) as (clip id, weight, rate).
///
/// The move m is the ratios' length held to the blend's `ceiling` (+0x70). The run weight is m - 1 held to 0..1; the walk
/// weight is m under 1, else 1 - run; the idle weight is what a move under 1 leaves. The way, in degrees round to the
/// right from straight ahead, picks the clip or the two either side of it (90 or 45 degrees apart) by its place between
/// them. Run clips are left out when `walk_only` (the step's limit: no running-strafe flag on the set or its partner).
/// `rate` is the walk and run clips' rate (0x82FD44EC); the idle's is 1.
// ponytail: the standing part (both ratios 0: the idle, Shuffle_Stop and Turn_360 clips on the clip player) is not here,
// add when the viewer plays aiming turns. The list's nine-entry limit is not kept (at most five are added here).
pub fn strafe_clips(sideways: f32, forward: f32, ceiling: f32, eight: bool, walk_only: bool, rate: f32) -> Vec<(u32, f32, f32)> {
    let len = sideways.mul_add(sideways, forward * forward).sqrt();
    let m = if len - ceiling >= 0.0 { ceiling } else { len };
    let run = if m <= 1.0 { 0.0 } else if m >= 2.0 { 1.0 } else { m - 1.0 };
    let walk = if m < 1.0 { m } else { 1.0 - run };
    let idle = if m < 1.0 { 1.0 - walk } else { 0.0 };
    // 0x8245C140: the heading from the ratios to the origin in degrees (Ã— 57.29578), then turned to the right, 0..360.
    let mut way = -(crate::pedgroups::heading_between(sideways, forward, 0.0, 0.0) * 57.295_776);
    if way < 0.0 {
        way += 360.0;
    }
    let (walks, runs, step): (&[u32], &[u32], f32) = if eight { (&WALK_8, &RUN_8, 45.0) } else { (&WALK_4, &RUN_4, 90.0) };
    let n = walks.len();
    let mut out = vec![];
    let (mut low, mut high) = (0.0f32, step);
    for i in 0..n {
        // On a clip's own way it plays alone; between two, each by its nearness.
        let (at, next, t) = if way == low {
            (i, i, 0.0)
        } else if way == high {
            ((i + 1) % n, (i + 1) % n, 0.0)
        } else if low < way && way < high {
            (i, (i + 1) % n, if low == high { 1.0 } else { (way - low) / (high - low) })
        } else {
            low = high;
            high += step;
            continue;
        };
        for (clips, w, on) in [(walks, walk, true), (runs, run, !walk_only)] {
            if w == 0.0 || !on {
                continue;
            }
            if at == next {
                out.push((clips[at], w, rate));
            } else {
                out.push((clips[at], (1.0 - t) * w, rate));
                out.push((clips[next], t * w, rate));
            }
        }
        break;
    }
    if idle != 0.0 {
        out.push((IDLE_CLIP, idle, 1.0));
    }
    out
}

/// One ratio's step: `rate` toward `wanted`, never past it.
fn ease(cur: f32, wanted: f32, rate: f32) -> f32 {
    if wanted > cur {
        (cur + rate).min(wanted)
    } else if wanted < cur {
        (cur - rate).max(wanted)
    } else {
        wanted
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turns_then_settles() {
        // A quarter turn right: the rate builds at 50 a second, Turn_360_R plays at its top rate; facing, he snaps.
        let t = strafe_turn(0.0, -1.5, 11.0, 0.0, 0.02);
        assert_eq!((t.target, t.rate, t.snap), (-5.0, -1.0, None));
        let s = Stand { gap: -1.5, rate: t.rate, turns: true, ..Default::default() };
        assert_eq!(strafe_stand(&s), (vec![StandAsk::Play { id: 0x11, blend: 4.0, rate: Some(1.5) }], false));
        let s = Stand { turn_r: true, ..s };
        let s = Stand { rate: 0.0, gap: 0.0, ..s };
        assert_eq!(strafe_stand(&s).0, vec![StandAsk::BlendOut(0x11), StandAsk::Play { id: IDLE_CLIP, blend: 0.2, rate: None }]);
        assert!(strafe_stand(&s).1);
    }

    fn input(stick: (i32, i32), run: Option<f32>) -> StrafeInput {
        StrafeInput {
            stick,
            camera_heading: 0.0,
            right: [1.0, 0.0, 0.0],
            forward: [0.0, 1.0, 0.0],
            run,
            run_hold: false,
            idle: false,
        }
    }

    fn near(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-5
    }

    #[test]
    fn full_push_takes_the_cap_from_the_run_value() {
        // Full push forward: the forward axis is 1 and the cap is walk 1, run 2 or sprint 3.
        let walk = strafe_move(&input((0, -120), None), |_| true);
        assert!(near(walk.forward, 1.0) && near(walk.sideways, 0.0) && near(walk.length, 1.0));
        let run = strafe_move(&input((0, -120), Some(0.5)), |_| true);
        assert!(near(run.forward, 2.0) && near(run.length, 2.0));
        let sprint = strafe_move(&input((0, -120), Some(1.3)), |_| true);
        assert!(near(sprint.forward, 3.0) && near(sprint.length, 3.0));
    }

    #[test]
    fn a_small_push_creeps_at_0_15() {
        // A tenth of the stick is idle and creeps up to 0.15.
        let creep = strafe_move(&input((0, -12), None), |_| true);
        assert!(creep.idle && near(creep.forward, 0.15) && near(creep.length, 0.15));
        // Sprinting, a tenth is 0.3, above the creep, so it stays.
        let sprint = strafe_move(&input((0, -12), Some(1.3)), |_| true);
        assert!(near(sprint.length, 0.3));
    }

    #[test]
    fn axes_are_held_to_one() {
        // A stick past its range is one, and a ped axis longer than one still gives one.
        let mut i = input((1000, 0), None);
        i.right = [2.0, 0.0, 0.0];
        let m = strafe_move(&i, |_| true);
        assert!(near(m.sideways, 1.0) && near(m.length, 1.0));
    }

    #[test]
    fn centred_stick_and_a_veto_give_no_move() {
        let centred = strafe_move(&input((0, 0), Some(1.3)), |_| true);
        assert_eq!((centred.sideways, centred.forward, centred.length, centred.idle), (0.0, 0.0, 0.0, false));
        let vetoed = strafe_move(&input((0, -120), None), |_| false);
        assert_eq!((vetoed.sideways, vetoed.forward, vetoed.length), (0.0, 0.0, 0.0));
    }

    #[test]
    fn strafe_clips_pick_by_way_and_speed() {
        // A walk straight ahead: Walk alone. Straight right is a hair past 90 degrees (0x8245BD88 takes a dy of 0 as
        // 0.0001), so Walk_Strafe_B gets a sliver.
        assert_eq!(strafe_clips(0.0, 1.0, 3.0, false, false, 1.0), vec![(0x1C, 1.0, 1.0)]);
        let r = strafe_clips(1.0, 0.0, 3.0, false, false, 1.0);
        assert!(r[0].0 == 0x3E && r[0].1 > 0.999 && r[1].0 == 0x3F);
        // A run half way between ahead and right, 8 ways: Run_Strafe_R45 (no walk weight at m = 2).
        let s = std::f32::consts::SQRT_2;
        let c = strafe_clips(s, s, 3.0, true, false, 1.0);
        assert!(c.iter().all(|c| c.0 == 0x48 && c.1 > 0.999 || c.1 < 0.001));
        // A half walk back: Walk_Strafe_B at half, the idle the other half.
        assert_eq!(strafe_clips(0.0, -0.5, 3.0, false, false, 1.0), vec![(0x3F, 0.5, 1.0), (0x0F, 0.5, 1.0)]);
        // Walk only: a run's clips are left out.
        assert_eq!(strafe_clips(0.0, 2.0, 3.0, false, true, 1.0), vec![]);
    }

    #[test]
    fn ease_moves_eight_a_second_and_stops_at_wanted() {
        let mut r = StrafeRatios { wanted_sideways: 5.0, wanted_forward: -0.05, ..Default::default() };
        // 8 a second over 0.01 s is 0.08; forward stops at its wanted -0.05.
        assert!(r.step(0.01, false, false));
        assert!(near(r.sideways, 0.08) && near(r.forward, -0.05));
        // Fast is Ã—10: 0.8 more on top, and the start timer is not stamped again.
        assert!(!r.step(0.01, true, false));
        assert!(near(r.sideways, 0.88) && near(r.forward, -0.05));
        // The limit holds both to -1..1 after the step.
        let mut t = StrafeRatios { wanted_sideways: 5.0, ..Default::default() };
        t.step(1.0, false, true);
        assert_eq!(t.sideways, 1.0);
    }
}
