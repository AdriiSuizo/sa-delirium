//! The on-foot blend's turns (board PD5): the 180 turn, and turning on the spot (`stand`, below).
//!
//! The 180: the player moving who pushes the stick the other way plays a turn-180 clip.
//! The on-foot step 0x82417438 starts it (0x82417E6C on) when 0x82414CD0 says so; while it plays, 0x82416B00 (called
//! at 0x82417664) holds his heading until the clip's 0x4000 event and the step skips the rest of its work. Read in
//! `docs/peds/movement.md` (180 turns).

/// The turn clips' ids (the move clip names at 0x82A97824, id = index + 0xE): walk, run and sprint, left and right.
pub const WALK_TURN_180_L: u32 = 0x37;
pub const WALK_TURN_180_R: u32 = 0x38;
pub const RUN_TURN_180_L: u32 = 0x39;
pub const RUN_TURN_180_R: u32 = 0x3A;
pub const SPRINT_TURN_180_L: u32 = 0x3B;
pub const SPRINT_TURN_180_R: u32 = 0x3C;

/// A turn clip blends in at 4 a second (0x82AA3060); a walk turn let go of blends out at 4 (0x820BED04).
pub const BLEND_IN: f32 = 4.0;
/// The clip event that frees the heading (0x82416D60).
pub const FREE: u32 = 0x4000;
/// No new turn within 3000 ms of the last (now less move blend +0x7C, 0x82414D34).
pub const WAIT_MS: u32 = 3000;
/// The gap that asks for one: 172 degrees (0x82AA3064).
const GAP: f32 = f32::from_bits(0x4040_2038);

/// Whether the step starts a turn (0x82414CD0; the step asks only while its state, 0x82FD4514, is above 1). Besides
/// these it needs single player (0x826C4CA8) and the runtime switch 0x830FB0EA clear. `set_flag` is the clip set's
/// +0x378 bit 22, `player` the blend's +0x50 bit 18, `since_ms` the time since the last turn (u32, wrapping) and `gap`
/// the wanted heading less the heading (+0x9F4 âˆ’ +0x9F0), wrapped to Â±Ï€ (0x8245BC88).
pub fn turn_wanted(set_flag: bool, player: bool, since_ms: u32, gap: f32) -> bool {
    set_flag && player && since_ms >= WAIT_MS && gap.abs() >= GAP
}

/// The clip (0x82417E84-0x82417F1C): walk, run or sprint by the ratio (int(ratio âˆ’ 0.5), at least 0; the ratio is at
/// most 3), left unless the heading still to turn (+0x2C) is below 0.
pub fn turn_clip(ratio: f32, to_turn: f32) -> u32 {
    let gait = (ratio - 0.5).max(0.0) as u32;
    WALK_TURN_180_L + gait * 2 + u32::from(to_turn < 0.0)
}

/// A walk turn is let go of (0x82416B7C-0x82416BC8) when the wanted move (blend +0xC sideways, +0x10 ahead) is at
/// most 0.1 long (its square at most 0.01, 0x820BECD8): it blends out and the blend's +0x50 bit 14 is set.
pub fn turn_cancel(id: u32, side: f32, ahead: f32) -> bool {
    matches!(id, WALK_TURN_180_L | WALK_TURN_180_R) && side.mul_add(side, ahead * ahead) <= f32::from_bits(0x3C23_D70A)
}

/// The turn is over (0x82416BCC-0x82416BF4) when its phase (+0x4C) reaches 1 less the clip's vf +8 (taken as this
/// frame's phase step, as the start's `start_done`); the step then goes on with +0x50 bit 14 set.
pub fn turn_done(phase: f32, step: f32) -> bool {
    phase >= 1.0 - step
}

/// The heading is free once the phase reaches the clip's first 0x4000 event (`Animation::first_event`; 0x82416D58-
/// 0x82416D8C): then the step turns him by the blend's rate (+0x30 = +0x28 Ã— frame time). Before it, or with no such
/// event (the sprint turns), the rate is 0 and the wanted heading is set to the heading (0x822F10A8).
pub fn turn_free(event: Option<f32>, phase: f32) -> bool {
    event.is_some_and(|e| e <= phase)
}

/// The turn-on-the-spot clips (move clip ids): Turn_360_L / Turn_360_R, and the idle.
pub const TURN_360_L: u32 = 0x10;
pub const TURN_360_R: u32 = 0x11;
pub const IDLE: u32 = 0xF;

/// A turn-on-the-spot clip as 0x82414140 reads it: its phase (+0x4C), +0x46 bit 2, its flags (+4) and its length
/// (0x824542D8).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SpotClip {
    pub phase: f32,
    pub held: bool,
    pub flags: u32,
    pub length: f32,
}

/// What the standing step reads (0x82414140; the on-foot step calls it at 0x82417DF4 with its state at 0 or below,
/// the blend's +0x50 bit 19 clear, blend-in 4 and `no_turn` 0).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Stand {
    /// The ped's turn rate (+0x9F8, 0x822F1108).
    pub turn_rate: f32,
    /// The idle's blend-in (f1).
    pub blend_in: f32,
    /// r7's byte: no turning.
    pub no_turn: bool,
    /// The blend's +0x50.
    pub bits: u32,
    /// The switch 0x82AA3038 (1 as shipped).
    pub switch: bool,
    /// The clip set's +0x378 and +4.
    pub set_bits: u32,
    pub set_flags: u32,
    /// The ped's +0x1E2 halfword, +0xC80 float and +0x211 byte.
    pub ped_1e2: u16,
    pub ped_c80: f32,
    pub fast: bool,
    /// The wanted heading (+0x9F4) and heading (+0x9F0).
    pub want: f32,
    pub heading: f32,
    /// A start clip playing (r5, else 0x15, 0x16, 0x13 or 0x14 on the ped's clip player).
    pub start: bool,
    /// Turn_360_L / R playing.
    pub left: Option<SpotClip>,
    pub right: Option<SpotClip>,
    /// What 0x82413068 gives back for a turn clip it starts.
    pub fresh: SpotClip,
    /// 0x82159078 on the ped's +0x21C +0x2E0 for task 0x259 (asked on left turns only).
    pub busy: bool,
    /// The blend's +0x28 and +0x68, and the frame time (0x82C6C2AC).
    pub rate: f32,
    pub rate68: f32,
    pub dt: f32,
}

/// The clip it asks 0x82413068 for: the idle (with the set's +4 flags, 0x31 for âˆ’1; None: the ped's, 0x822F0588) or a
/// turn clip with the set's +4.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Ask {
    Idle { blend: f32, flags: Option<u32> },
    Turn { id: u32, blend: f32, flags: u32 },
}

/// What it does: the ask; the turn clip after (its phase, flags) and its rate (+0x54); the blend's +0x68 and +0x30 (the
/// heading's step this frame); +0x50 bit 11 (0x800, turning); the ped's +0x26C |= 0x300 (the head IK held). It always
/// sets +0x40 to âˆ’1 and returns 1.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Stood {
    pub ask: Option<Ask>,
    pub clip: Option<SpotClip>,
    pub clip_rate: Option<f32>,
    pub rate68: f32,
    pub step: f32,
    pub turning: bool,
    pub ik: bool,
}

/// The standing step 0x82414140: the idle, or a turn on the spot toward the wanted heading. The clip's rate is the
/// ped's turn rate over 11 (at least 1.8 with +0x211). Past 6 degrees (0x82AA303C) it plays Turn_360_L (gap above 0)
/// or R, blending in at 16 Ã— the gap's share of a full turn (held to 1); from a start clip at phase 0.25 (0x82454308).
/// A new clip turns the gap past what its rest turns (a quarter turn Ã— (1 âˆ’ phase)) over its length; one held (+0x46
/// bit 2) the whole gap over its length. Within 6 degrees, or with no gap, the idle at the blend-in (4 if Turn_360_L
/// plays; the ped's flags) and the heading at the blend's rate Ã— 3 Ã— the clip rate.
pub fn stand(s: &Stand) -> Stood {
    let mut rate = s.turn_rate / f32::from_bits(0x4130_0000);
    let idle_step = |rate: f32| s.rate * s.dt * f32::from_bits(0x4040_0000) * rate;
    let plain = s.no_turn
        || s.bits & 0x40_0000 != 0
        || !s.switch
        || s.set_bits & 0x80_0000 == 0
        || (s.ped_1e2 & 0xF >= 2 && s.ped_c80 == 0.0);
    if plain {
        let flags = if s.set_flags == u32::MAX { 0x31 } else { s.set_flags };
        let ask = Ask::Idle { blend: 1.0, flags: Some(flags) };
        return Stood { ask: Some(ask), step: idle_step(rate), rate68: s.rate68, ..Default::default() };
    }
    if s.fast {
        let fast = f32::from_bits(0x3FE6_6666);
        rate = if rate - fast >= 0.0 { rate } else { fast };
    }
    let gap = crate::actiontable::wrap(s.want - s.heading);
    let share = gap.abs() * f32::from_bits(0x3E22_F983);
    let blend = (if 1.0 - share >= 0.0 { share } else { 1.0 }) * 16.0;
    let near = f32::from_bits(0x3DD6_7750);
    let quarter = f32::from_bits(0x3FC9_0FDB);
    let side = if gap == 0.0 {
        None
    } else if gap > near {
        Some((TURN_360_L, s.left, quarter))
    } else if gap < -near {
        Some((TURN_360_R, s.right, -quarter))
    } else {
        None
    };
    let Some((id, playing, quarter)) = side else {
        // Within 6 degrees the idle blends in at 4 while Turn_360_L plays (0x82414554); with no gap, at the blend-in.
        let blend = if gap != 0.0 && s.left.is_some() { 4.0 } else { s.blend_in };
        return Stood { ask: Some(Ask::Idle { blend, flags: None }), step: idle_step(rate), rate68: s.rate68, ..Default::default() };
    };
    let left = id == TURN_360_L;
    let mut out = Stood { turning: true, ik: !left || !s.busy, rate68: s.rate68, ..Default::default() };
    // Past (left) or before (right) the bound, by the turn's sign.
    let beyond = |a: f32, b: f32| if left { a > b } else { a < b };
    let mut clip = match playing {
        Some(c) => {
            if c.held {
                out.rate68 = gap / c.length;
            }
            c
        }
        None => {
            out.ask = Some(Ask::Turn { id, blend, flags: s.set_flags });
            let mut c = s.fresh;
            if s.start {
                c.phase = 0.25;
            }
            let rest = (1.0 - c.phase) * quarter;
            if beyond(gap, rest) {
                let more = gap - rest;
                if beyond(more, 0.0) {
                    out.rate68 = more / c.length;
                }
            } else {
                out.rate68 = 0.0;
            }
            c
        }
    };
    if s.start {
        clip.flags |= 0x1_0000;
    } else {
        clip.flags &= !0x1_0000;
    }
    (out.clip, out.clip_rate) = (Some(clip), Some(rate));
    out.step = out.rate68 * s.dt;
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_reversed_stick_turns_once_every_3_s() {
        assert!(turn_wanted(true, true, 3000, 3.1));
        assert!(turn_wanted(true, true, 3000, -3.01));
        assert!(!turn_wanted(true, true, 2999, 3.1));
        assert!(!turn_wanted(true, true, 3000, 2.9));
        assert!(!turn_wanted(true, false, 3000, 3.1));
        assert!(!turn_wanted(false, true, 3000, 3.1));
    }

    #[test]
    fn the_ratio_and_side_pick_the_clip() {
        assert_eq!(turn_clip(1.0, 3.0), WALK_TURN_180_L);
        assert_eq!(turn_clip(1.4, -3.0), WALK_TURN_180_R);
        assert_eq!(turn_clip(2.0, 0.0), RUN_TURN_180_L);
        assert_eq!(turn_clip(2.5, -1.0), SPRINT_TURN_180_R);
        assert_eq!(turn_clip(3.0, 1.0), SPRINT_TURN_180_L);
        assert_eq!(turn_clip(0.2, 1.0), WALK_TURN_180_L);
    }

    #[test]
    fn standing_turns_past_6_degrees_and_idles_within() {
        let s = Stand { turn_rate: 11.0, blend_in: 4.0, switch: true, set_bits: 0x80_0000, want: 1.0, rate: 2.0, dt: 0.5,
                        fresh: SpotClip { length: 2.0, ..Default::default() }, ..Default::default() };
        let out = stand(&s);
        assert_eq!(out.ask, Some(Ask::Turn { id: TURN_360_L, blend: f32::from_bits(0x3E22_F983) * 16.0, flags: 0 }));
        assert!(out.turning && out.ik && out.clip_rate == Some(1.0));
        // A fresh clip's rest turns a quarter turn, more than the one radian asked.
        assert_eq!(out.rate68, 0.0);
        let out = stand(&Stand { want: -2.0, ..s });
        assert!(matches!(out.ask, Some(Ask::Turn { id: TURN_360_R, .. })));
        assert_eq!(out.rate68, (-2.0 + f32::from_bits(0x3FC9_0FDB)) / 2.0);
        let out = stand(&Stand { want: 0.05, ..s });
        assert_eq!(out.ask, Some(Ask::Idle { blend: 4.0, flags: None }));
        assert_eq!(out.step, 2.0 * 0.5 * 3.0);
        let out = stand(&Stand { switch: false, set_flags: u32::MAX, ..s });
        assert_eq!(out.ask, Some(Ask::Idle { blend: 1.0, flags: Some(0x31) }));
    }

    #[test]
    fn walk_turns_cancel_and_the_event_frees_the_heading() {
        assert!(turn_cancel(WALK_TURN_180_R, 0.0, 0.09));
        assert!(!turn_cancel(WALK_TURN_180_R, 0.0, 0.2));
        assert!(!turn_cancel(RUN_TURN_180_L, 0.0, 0.0));
        assert!(turn_done(0.95, 0.05) && !turn_done(0.9, 0.05));
        assert!(!turn_free(Some(0.8), 0.7) && turn_free(Some(0.8), 0.8) && !turn_free(None, 1.0));
    }
}
