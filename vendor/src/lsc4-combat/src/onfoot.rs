//! The normal on-foot step (board row PD5f): the sharp-turn slowdown (0x82417438), the heading turn that the move
//! blend's ratio easing does (0x82415158, its speed part is `animblend::ease_ratio`) and the player's stride wobble
//! (0x82417438). Single precision, fused multiply-adds where the game has them. The step's inputs are plain numbers
//! and flags read off the ped and its move blend; the outputs are the new values to store back.
//!
//! Not here: the clip pick and the starts and stops (0x82294E90), the strafing step (0x82418290), the ratio split
//! (0x82414BF0, `animblend::split_ratio`, which the step's caller runs on its own clip struct), the early exit
//! 0x82416B00, and the blend's per-step turn into +0x30 on the paths that do not set it (see below).

use crate::actiontable::wrap;
use crate::animblend::ease_ratio;
use crate::mathrt::sin;

const PI: f32 = std::f32::consts::PI;
/// 2 pi as the game holds it (0x820BEEE0).
const TWO_PI: f32 = 6.283_185_5;
/// Degrees to radians (0x820BEEF4).
const DEG: f32 = 0.017_453_292;
/// 1 / 2 pi (0x820023A8).
const INV_TWO_PI: f32 = 0.159_154_94;
/// The turn rate's ramp is 50 a second, scaled by the error (0x82000A2C, 0x82A9922C / 0x82A99230).
const TURN_RATE: f32 = 50.0;
/// Half a degree: an error under it is taken in full (0x82018970).
const DONE_TURN: f32 = 0.008_726_646;
/// The wanted ratio is cut toward this on a sharp turn (0x82AA3078 / 0x82AA3080).
const SHARP_RATIO: f32 = 1.5;
/// The sharp-turn threshold: 30 degrees for peds (0x82AA307C), 45 for the player (0x82AA3074).
const SHARP_PED: f32 = 0.523_598_8;
const SHARP_PLAYER: f32 = 0.785_398_2;
/// The stride wobble: the phase runs 5 a second (0x82AA3088), the ratio moves by 0.1 sin (0x82AA3084).
const WOBBLE_SPEED: f32 = 5.0;
const WOBBLE_DEPTH: f32 = 0.1;

/// The turn state the easing keeps on the blend: +0x28 the rate, ramping toward +0x2C, the heading still to turn.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Turn {
    /// +0x28.
    pub rate: f32,
    /// +0x2C: the target turn after the divisor and the cap.
    pub left: f32,
}

/// What the step reads.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Input {
    /// Ped +0x9F0 (the heading now) and +0x9F4 (the wanted heading).
    pub heading: f32,
    pub wanted_heading: f32,
    /// Ped +0x9F8: the turn speed in degrees.
    pub turn_speed: f32,
    /// Move blend +8: the ratio now, and +0x10 the wanted one.
    pub ratio: f32,
    pub wanted_ratio: f32,
    /// Move blend +0x70: the ratio is held to this first.
    pub ratio_ceiling: f32,
    /// The clip set's top speed (+0x350); the ratio is held to it + 1 going up.
    pub clip_top: f32,
    /// Move blend +0x50 bit 18: the player (its own tuning).
    pub player: bool,
    /// Ped +0x118 bit 31: a network clone (no sharp-turn slowdown).
    pub clone: bool,
    /// Ped +0x68 holds an object whose byte +0xE is set (no sharp-turn slowdown, and the wanted ratio is not held
    /// on a boat). The object is not traced; the doc's "carried object" name is a guess.
    pub carried: bool,
    /// Standing on a vehicle of kind 2 (a boat).
    pub on_boat: bool,
    /// Ped +0x238 bit 20: the boat hold is not applied.
    pub boat_exempt: bool,
    /// Ped +0x23C bit 17, or +0x23C not negative with +0x26C bit 29 clear: the ratio is taken from the wanted one
    /// at once (the easing returns early).
    pub snap: bool,
    /// Move blend +0x50 bit 26, or +0x48 not -1: the easing's speed part is held.
    pub hold: bool,
    pub turn: Turn,
    /// 0x82FD44FC: the wobble's phase (radians, under 2 pi).
    pub wobble_phase: f32,
    /// 0x82FD44F9 is set (the wobble is switched on).
    pub wobble_on: bool,
    /// Not a network game (0x826C4CA8 is 0): the wobble is single player only.
    pub single_player: bool,
    /// The frame time (0x82C6C2AC).
    pub dt: f32,
}

/// What the step gives back.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Output {
    pub heading: f32,
    pub wanted_heading: f32,
    pub ratio: f32,
    pub wanted_ratio: f32,
    pub turn: Turn,
    /// +0x30 as the easing sets it: the whole error when it was under half a degree (then `turn.left` is 0).
    /// `None` when the easing did not finish the turn this step.
    pub turn_done: Option<f32>,
    pub wobble_phase: f32,
}

/// The heading error the easing uses (0x82415158 at 0x824152B4-0x824152EC, then the single wrap at
/// 0x82415398-0x824153D8): both headings wrapped into -pi..pi, the wanted less the heading, wrapped once more.
pub fn heading_error(heading: f32, wanted_heading: f32) -> f32 {
    let e = wrap(wanted_heading) - wrap(heading);
    if e > PI {
        e - TWO_PI
    } else if e < -PI {
        e + TWO_PI
    } else {
        e
    }
}

/// The sharp-turn slowdown (0x82417438 at 0x824174A0-0x8241754C). While the ped moves at 1 or more and wants at least
/// 1.5, a heading error (`error`, wrapped) over 30 degrees (45 for the player) cuts the wanted ratio toward 1.5:
/// wanted - (|error| - threshold) / (pi - threshold) x (wanted - 1.5), not below 1.5. Gating for clones and carried
/// objects is the caller's.
pub fn sharp_turn(ratio: f32, wanted: f32, error: f32, player: bool) -> f32 {
    if ratio < 1.0 || wanted < SHARP_RATIO {
        return wanted;
    }
    let threshold = if player { SHARP_PLAYER } else { SHARP_PED };
    let over = error.abs() - threshold;
    if !(error.abs() > threshold) {
        return wanted;
    }
    let cut = over / (PI - threshold) * (wanted - SHARP_RATIO);
    let new = wanted - cut;
    if new - SHARP_RATIO >= 0.0 { new } else { SHARP_RATIO }
}

/// The turn cap (0x82415158 at 0x824155BC-0x82415628): the ped's turn speed in degrees, Ã— 8 on a boat (0x82004E0C),
/// Ã— pi/180, Ã— 50. The boat factor is applied to peds only: its switch (0x82FD4645) is a runtime global, taken as clear.
fn turn_cap(turn_speed: f32, on_boat: bool, player: bool) -> f32 {
    let speed = if on_boat && !player { turn_speed * 8.0 } else { turn_speed };
    speed * DEG * TURN_RATE
}

/// The heading turn of the easing (0x82415158 from 0x824152B4): the error is taken as the step's target. It is
/// under half a degree: taken in full (`done`), target 0. Otherwise it is divided by 0.25 for peds (0x82AA30B0) or
/// 0.5 for the player (0x82AA30AC) and held to the cap. The rate (+0x28) then moves toward the target by at most
/// dt Ã— 50 Ã— scale, where scale is clamp(|error| / 2 pi + 0.5, 0, 1) squared (0x82AA3059..3068 are on as shipped).
/// Rubber-banding and the sign-reversal clamp are not here: their switches are never set (see the doc). Checked against
/// the exe: `tools/xbox360-re/checks/headingturn.py`.
pub fn heading_turn(heading: f32, wanted_heading: f32, turn_speed: f32, player: bool, on_boat: bool, dt: f32, prev: Turn) -> (f32, f32, Turn, Option<f32>) {
    let error = heading_error(heading, wanted_heading);
    let (mut left, mut done) = (error, None);
    if error.abs() < DONE_TURN {
        done = Some(error);
        left = 0.0;
    }
    let scale = (left.abs().mul_add(INV_TWO_PI, 0.5)).min(1.0);
    let scale = (scale * scale).min(1.0);
    left /= if player { 0.5 } else { 0.25 };
    let cap = turn_cap(turn_speed, on_boat, player);
    left = if left > cap {
        cap
    } else if left < -cap {
        -cap
    } else {
        left
    };
    let rate = ramp(prev.rate, left, dt, TURN_RATE * scale);
    (wrap(heading), wrap(wanted_heading), Turn { rate, left }, done)
}

/// The rate moves toward `target` by at most `dt` Ã— `per_second`, never past it (0x82415158 at 0x824156CC-0x8241578C;
/// the accelerate and the brake use the same rate here, since both are scaled alike).
fn ramp(rate: f32, target: f32, dt: f32, per_second: f32) -> f32 {
    if target > rate {
        let r = dt.mul_add(per_second, rate);
        if r > target { target } else { r }
    } else if target < rate {
        let r = (-dt).mul_add(per_second, rate);
        if r < target { target } else { r }
    } else {
        target
    }
}

/// The player's stride wobble (0x82417438 at 0x82417694-0x82417754): the phase runs dt Ã— 5 and is held under 2 pi,
/// the ratio gets 0.1 sin(phase) added and is held to 1..3. Only called when the gate holds (switch, single player,
/// the player, wanted at least 1), so the caller checks that.
pub fn wobble(ratio: f32, phase: f32, dt: f32) -> (f32, f32) {
    let mut phase = dt.mul_add(WOBBLE_SPEED, phase);
    while phase > TWO_PI {
        phase -= TWO_PI;
    }
    let s = sin(phase as f64) as f32;
    let r = s.mul_add(WOBBLE_DEPTH, ratio);
    let r = if r - 1.0 >= 0.0 { r } else { 1.0 };
    let r = if r - 3.0 >= 0.0 { 3.0 } else { r };
    (r, phase)
}

/// One normal on-foot step (0x82417438), in the game's order: the sharp-turn slowdown, the boat hold, the ratio's
/// ceiling, the easing (speed, then the heading turn), and the wobble.
pub fn step(i: Input) -> Output {
    // ponytail: the early exit 0x82416B00 (ends the step) is not modelled; add when a caller needs its ending.
    let mut ratio = i.ratio;
    let mut wanted_ratio = i.wanted_ratio;

    if !i.clone && !i.carried {
        let error = wrap(i.wanted_heading - i.heading);
        wanted_ratio = sharp_turn(ratio, wanted_ratio, error, i.player);
    }
    if i.on_boat && !i.boat_exempt {
        ratio = ratio.min(1.0);
        if !i.carried {
            wanted_ratio = wanted_ratio.min(1.0);
        }
    }
    ratio = ratio.min(i.ratio_ceiling);

    let (heading, wanted_heading, turn, turn_done, ratio) = if i.snap {
        // ponytail: the easing's clear of the sideways ratio (+4) is left out; the normal step has none.
        (i.heading, i.wanted_heading, Turn::default(), None, wanted_ratio)
    } else {
        let eased = if i.hold { ratio } else { ease_ratio(ratio, wanted_ratio, i.dt, i.player, i.clip_top) };
        let (h, w, t, d) = heading_turn(i.heading, i.wanted_heading, i.turn_speed, i.player, i.on_boat, i.dt, i.turn);
        (h, w, t, d, eased)
    };

    let (ratio, wobble_phase) = if i.wobble_on && i.single_player && i.player && wanted_ratio >= 1.0 {
        wobble(ratio, i.wobble_phase, i.dt)
    } else {
        (ratio, i.wobble_phase)
    };

    Output { heading, wanted_heading, ratio, wanted_ratio, turn, turn_done, wobble_phase }
}

/// The move blend's turn grid (0x82666248), the walk, run and sprint rows: the clip ids from the hardest left turn to
/// the hardest right (Walk_Turn_L3, L2, L, Walk, R, R2, R3), the straight clip in the middle. The run and sprint rows'
/// outer columns repeat their neighbours.
pub const TURN_GRID: [[u32; 7]; 3] = [
    [0x24, 0x22, 0x20, 0x1C, 0x21, 0x23, 0x25],
    [0x2E, 0x2E, 0x2C, 0x28, 0x2D, 0x2F, 0x2F],
    [0x33, 0x33, 0x33, 0x32, 0x34, 0x34, 0x34],
];
/// The middle column: straight on.
pub const STRAIGHT: f32 = 3.0;

/// A grid clip's turn rate (0x826666F8): its mover's yaw over the clip (end less start), at least -pi in a left
/// column and at most pi in a right one, over its length.
pub fn clip_turn_rate(yaw: f32, col: usize, duration: f32) -> f32 {
    let yaw = if col < 3 && yaw < -PI {
        -PI
    } else if col > 3 && yaw > PI {
        PI
    } else {
        yaw
    };
    yaw / duration
}

/// The column whose clips turn at `want` (0x82294B80), given the columns' rates (each gait row's by its weight):
/// from the middle outward on the turn's side to the first pair of columns it falls between, the column and the
/// fraction to the next; at the grid's edge that edge; none, the middle.
// ponytail: at a want of exactly 0 the game's search never moves off the middle column (and never ends); the middle here.
pub fn turn_column(rates: &[f32; 7], want: f32) -> f32 {
    let dir: i32 = if want > 0.0 {
        -1
    } else if want < 0.0 {
        1
    } else {
        return STRAIGHT;
    };
    let mut c = 3;
    while (0..7).contains(&c) {
        let n = c + dir;
        if n < 0 || n == 7 {
            return c as f32;
        }
        let (a, b) = (rates[c as usize], rates[n as usize]);
        if dir > 0 && !(want > a) && want >= b {
            return c as f32 + (want - a) / (b - a);
        }
        if dir < 0 && !(want < a) && want <= b {
            return c as f32 - (want - a) / (b - a);
        }
        c = n;
    }
    STRAIGHT
}

/// The turn column eased toward `target` (0x824180BC-0x824181EC): back toward the middle at 20 columns a second,
/// out from it at 10 (5 within a column of the middle), and within a quarter column of the middle at most twice the
/// gap a second.
pub fn ease_column(prev: f32, target: f32, dt: f32) -> f32 {
    let (to, from) = (target - STRAIGHT, prev - STRAIGHT);
    let gap = to - from;
    let mut s = if to.abs() < 0.25 { (gap.abs() * 2.0).min(1.0) } else { 1.0 };
    s *= dt;
    let next = if gap > 0.0 {
        if from < 0.0 {
            20.0f32.mul_add(s, from)
        } else {
            if to < 1.0 {
                s *= 0.5;
            }
            10.0f32.mul_add(s, from)
        }
        .min(to)
    } else {
        if from > 0.0 {
            (-20.0f32).mul_add(s, from)
        } else {
            if to > -1.0 {
                s *= 0.5;
            }
            (-10.0f32).mul_add(s, from)
        }
        .max(to)
    };
    next + STRAIGHT
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turn_column_falls_between_rates() {
        // Rates fall to the right: a right turn (negative) walks right from the middle.
        let rates = [3.0, 2.0, 1.0, 0.0, -1.0, -2.0, -3.0];
        assert!(close(turn_column(&rates, -1.5), 4.5));
        assert!(close(turn_column(&rates, 0.5), 2.5));
        assert_eq!(turn_column(&rates, -9.0), 6.0);
        assert_eq!(turn_column(&rates, 0.0), STRAIGHT);
        assert_eq!(clip_turn_rate(-4.0, 1, 2.0), -PI / 2.0);
        assert_eq!(clip_turn_rate(-4.0, 4, 2.0), -2.0);
    }

    #[test]
    fn column_eases_out_slowly_and_back_fast() {
        // Out past a column from the middle at 10 a second; back at 20; within a column of it at 5.
        assert!(close(ease_column(4.0, 6.0, 0.1), 5.0));
        assert!(close(ease_column(5.0, 3.0, 0.02), 4.6));
        assert!(close(ease_column(3.0, 3.8, 0.02), 3.1));
        assert!(close(ease_column(3.0, 3.1, 0.1), 3.1));
    }

    fn close(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-4
    }

    #[test]
    fn small_error_finishes_in_one_step() {
        // 0.005 rad is under half a degree: the whole error is taken (+0x30), the target goes to 0.
        let (_, _, t, done) = heading_turn(0.0, 0.005, 11.0, false, false, 0.02, Turn::default());
        assert_eq!(done, Some(0.005));
        assert_eq!(t.left, 0.0);
    }

    #[test]
    fn big_error_is_capped() {
        // Turn speed 1 deg: cap = 1 x pi/180 x 50 = 0.8727. A ped's error of 1 rad / 0.25 = 4 is held to it.
        let (_, _, t, done) = heading_turn(0.0, 1.0, 1.0, false, false, 0.02, Turn::default());
        assert_eq!(done, None);
        assert!(close(t.left, 50.0 * DEG));
        assert!(t.rate > 0.0 && t.rate <= t.left);
    }

    #[test]
    fn heading_error_wraps_across_pi() {
        // From 3 rad to -3 rad is 0.283 rad the short way (through pi), not -6.
        assert!(close(heading_error(3.0, -3.0), TWO_PI - 6.0));
        assert!(close(heading_error(-3.0, 3.0), 6.0 - TWO_PI));
    }

    #[test]
    fn player_divides_by_half_and_ped_by_quarter() {
        // 0.1 rad, cap far away (turn speed 100): the ped's target is 0.4, the player's 0.2.
        let (_, _, ped, _) = heading_turn(0.0, 0.1, 100.0, false, false, 0.02, Turn::default());
        let (_, _, player, _) = heading_turn(0.0, 0.1, 100.0, true, false, 0.02, Turn::default());
        assert!(close(ped.left, 0.4));
        assert!(close(player.left, 0.2));
    }

    #[test]
    fn sharp_turn_cuts_the_wanted_ratio() {
        // Running (2) wanting 3, turning 90 degrees: peds cut past 30 degrees to 2.4, the player past 45 to 2.5.
        assert!(close(sharp_turn(2.0, 3.0, PI / 2.0, false), 2.4));
        assert!(close(sharp_turn(2.0, 3.0, PI / 2.0, true), 2.5));
        // Under the threshold, or at a walk, nothing changes.
        assert_eq!(sharp_turn(2.0, 3.0, 0.4, false), 3.0);
        assert_eq!(sharp_turn(0.5, 3.0, PI / 2.0, false), 3.0);
    }

    #[test]
    fn wobble_moves_the_phase_and_the_ratio() {
        // Phase 0 + 0.02 x 5 = 0.1; the ratio 2 gets 0.1 sin(0.1) added.
        let (r, phase) = wobble(2.0, 0.0, 0.02);
        assert!(close(phase, 0.1));
        assert!(close(r, 2.0 + 0.1 * 0.1f32.sin()));
        // The phase wraps under 2 pi, and the ratio is held to 1 at least.
        assert_eq!(wobble(1.0, 4.0, 0.0), (1.0, 4.0));
        let (r, phase) = wobble(1.0, 6.2, 0.1);
        assert!(phase < TWO_PI && phase > 0.0);
        assert!(r >= 1.0 && r <= 3.0);
    }

    #[test]
    fn snap_takes_the_wanted_ratio_and_no_turn() {
        let out = step(Input {
            heading: 0.0, wanted_heading: 1.0, turn_speed: 11.0, ratio: 0.5, wanted_ratio: 2.0, ratio_ceiling: 9.0,
            clip_top: 3.0, player: false, clone: false, carried: false, on_boat: false, boat_exempt: false,
            snap: true, hold: false, turn: Turn { rate: 1.0, left: 1.0 }, wobble_phase: 0.0, wobble_on: false,
            single_player: true, dt: 0.02,
        });
        assert_eq!(out.ratio, 2.0);
        assert_eq!(out.turn, Turn::default());
        assert_eq!(out.heading, 0.0);
    }
}
