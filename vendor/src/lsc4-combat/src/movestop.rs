//! The on-foot blend's stop (board PD5): 0x82294E90, called from the on-foot step (0x82417438 at 0x824177C8) after
//! the ratio easing. A ped that wants to stand (wanted ratio 0) while it still moves gets a stop clip by its speed and
//! its stride, its ratio goes to 0 at once, and the clip's mover carries it to a halt. Read in `docs/peds/movement.md`
//! (Stops).
//!
//! [`pick`] is the whole function, checked against the exe (`tools/xbox360-re/checks/clippick.py`): stops, halts, a
//! stop's and a start's end, and the state it gives the step.

/// The stop clips' ids (the move clip names at 0x82A97824, id = index + 0xE): WStop_L/R, RStop_L/R, SStop_L/R.
pub const WSTOP_L: u32 = 0x26;
pub const WSTOP_R: u32 = 0x27;
pub const RSTOP_L: u32 = 0x30;
pub const RSTOP_R: u32 = 0x31;
pub const SSTOP_L: u32 = 0x35;
pub const SSTOP_R: u32 = 0x36;

/// A stop clip blends in at 4 a second (0x82000D6C) and, done or cut short, out at 1 a second (0x820BECF4).
pub const BLEND_IN: f32 = 4.0;
pub const BLEND_OUT: f32 = 1.0;

/// The stop for a ped that wants ratio 0 (0x82294FB8-0x822951CC): by the eased ratio, a walk stop under 1.5
/// (0x82002D60), a run stop under 2.7 (0x82014A9C), a sprint stop up to 3 (0x82019944); carrying something (ped +0x68
/// with its byte +0xE set) under 1 and under 2. The right-foot clip; [`stop_foot`] picks the foot. None when the ratio
/// before the easing (`before`) was not above 0, or the ratio is above 3.
pub fn stop_clip(ratio: f32, before: f32, carrying: bool) -> Option<u32> {
    let (walk, run) = if carrying { (1.0, 2.0) } else { (1.5, 2.7) };
    if !(before > 0.0) || ratio > 3.0 {
        return None;
    }
    Some(if ratio < walk {
        WSTOP_R
    } else if ratio < run {
        RSTOP_R
    } else {
        SSTOP_R
    })
}

/// The foot (0x82295150-0x82295234): the stride's phase (0-1, the walk cycle clip's +0x4C) picks the clip and where it
/// starts. Under 0.25: the left foot, from the same phase; under 0.5: the right, from 0; under 0.75: the right, from
/// phase - 0.5; else the left, from 0.
pub fn stop_foot(right: u32, phase: f32) -> (u32, f32) {
    let left = right - 1;
    if phase < 0.25 {
        (left, phase)
    } else if phase >= 0.75 {
        (left, 0.0)
    } else if phase < 0.5 {
        (right, 0.0)
    } else {
        (right, phase - 0.5)
    }
}

/// The stop is done (0x822953E4): its phase and this frame's step reach 0.98 (0x82A9924C). It then blends out.
pub fn stop_done(phase: f32, step: f32) -> bool {
    !(step + phase < f32::from_bits(0x3F7A_E148))
}

/// The clip blend (0x82412E88, run by the on-foot step right after the pick): with the pick's state 2 or more
/// (0x82000DC8: walking or faster) a stop clip still on the ped's clip player fades out at 4 a second (0x820BED04).
/// The pick ends a stop only at ratio 0 ([`pick`]), so this is what ends one when the ratio is already back up. Its
/// list is WStop_L / R, RStop_L / R and SStop_L (0x26, 0x27, 0x30, 0x31, 0x35): SStop_R is not in it, as shipped.
/// ponytail: the blend's other part (the set's other clips, +0xB0's seven, fading at 1 a second) is not here.
pub const STOP_FADE: f32 = 4.0;
pub fn stop_fades(state: f32, id: u32) -> bool {
    state >= 2.0 && [WSTOP_L, WSTOP_R, RSTOP_L, RSTOP_R, SSTOP_L].contains(&id)
}

/// The stop gives way (0x822953A8, with 0x82A9920A on as shipped): the ped wants to move again, or, unless ped +0x26C
/// bit 29, its heading is more than 22.5 degrees (0x8200A608) from the wanted one (`gap`, wrapped by 0x82294E10).
pub fn stop_cut(wanted: f32, gap: f32) -> bool {
    wanted != 0.0 || gap.abs() > 0.392_699_1
}

/// A clip on the ped's clip player as the pick reads it: its id (+0xC), phase (+0x4C), flags (+4), +0x46 bit 2, blend
/// rate (0x82455BC0; below 0 it is blending out), phase step (its vf +8), time (0x82454250), time step (0x82454280) and
/// length (0x824542D8).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PickClip {
    pub id: u32,
    pub phase: f32,
    pub flags: u32,
    pub held: bool,
    pub rate: f32,
    pub step: f32,
    pub time: f32,
    pub advance: f32,
    pub length: f32,
}

/// What the clip pick reads besides the blend fields it writes ([`PickBlend`]).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Pick {
    /// The wanted ratio (move blend +0x10), the ratio after the easing (the step's copy of +8) and before it.
    pub want: f32,
    pub ratio: f32,
    pub before: f32,
    /// The clip the blend's vf +0x54 (0x82413D98) gives.
    pub current: Option<PickClip>,
    /// The blend's state (+0x20) and +0x50; the time since its start timer (now 0x82C6C2A4 âˆ’ +0x74) and the limit
    /// (0x82CB1B04).
    pub state: f32,
    pub bits: u32,
    pub age: u32,
    pub age_max: u32,
    /// The clip set's +0x378; whether it has the stop clip (0x821E2E20 on its +0), and its other set (+4, None for âˆ’1).
    pub set_bits: u32,
    pub in_set: bool,
    pub alt: Option<bool>,
    /// The set's 7 ids at +0xB0 (every 0x18 bytes).
    pub set_ids: [i32; 7],
    /// 0x82294818 and 0x822948E0 (set, ped, 2); 0x82651140 (blend, clip player).
    pub stopping: bool,
    pub walk: bool,
    pub quick: bool,
    /// The ped's +0x68 with its byte +0xE set.
    pub carrying: bool,
    /// The switch 0x82A9920A (1 as shipped), the ped's +0x26C, its wanted heading (+0x9F4) and heading (+0x9F0).
    pub switch: bool,
    pub ped_26c: u32,
    pub want_heading: f32,
    pub heading: f32,
    /// 0x82413068 gives the stop clip it starts.
    pub started: bool,
    /// The clips playing, as the clip player's find (0x824450D0) gives them: the first with the id.
    pub playing: Vec<PickClip>,
}

/// The move blend's fields the pick writes: +8 (the ratio), +0x54, +0x48, the four stop ids +0xAC-+0xB8, +0x1C, +0x28,
/// +0x2C and +0x78.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PickBlend {
    pub ratio: f32,
    pub rate54: f32,
    pub b48: i32,
    pub stops: [i32; 4],
    pub b1c: f32,
    pub b28: f32,
    pub b2c: f32,
    pub b78: f32,
}

/// What the pick gives: the state (0x82FD4514), the step's ratio after, the stop clip it hands back (r5), the clips it
/// blends out (0x82455C68, in order), the stop clip it asks for (0x82413068 at blend-in 4) and its phase (0x82454308),
/// whether it called 0x824467A0 (clip player, 0) and 0x822949C8 (set, clip player: the start clip removed).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Picked {
    pub state: f32,
    pub ratio: f32,
    pub out: Option<u32>,
    pub blend_outs: Vec<(u32, f32)>,
    pub ask: Option<u32>,
    pub phase: Option<f32>,
    pub cleared: bool,
    pub removed: bool,
}

/// The clip pick 0x82294E90 in full: stops, a stop's end, a start's end, and the state the on-foot step goes on with
/// (âˆ’2 halted, âˆ’1 stopping, 0 standing, 1 starting, else the ratio + 1). See `docs/peds/movement.md` (The clip pick).
pub fn pick(p: &Pick, b: &mut PickBlend) -> Picked {
    let find = |id: u32| p.playing.iter().find(|c| c.id == id).copied();
    let mut o = Picked { ratio: p.ratio, ..Default::default() };
    let mut want = p.want;
    // A clip blending out under 0.85 (0x820BEF3C): taken as wanting to stand.
    if p.current.is_some_and(|c| c.rate < 0.0 && c.phase < f32::from_bits(0x3F59_999A)) {
        want = 0.0;
    }
    let moving = p.ratio > 0.0 || p.state > 1.0 || (p.state == -2.0 && (p.stopping || p.current.is_some()));
    if moving && want == 0.0 && p.bits & 0x0400_0000 == 0 && b.b48 == -1 {
        let halt = |b: &mut PickBlend, o: Picked| {
            (b.ratio, b.rate54, b.stops) = (0.0, 3.0, [-1; 4]);
            Picked { state: -2.0, ..o }
        };
        if (!(p.ratio >= 1.5) && !p.quick && p.age < p.age_max) || p.current.is_some() || p.bits & 0x80_0000 != 0 {
            return halt(b, o);
        }
        if let Some(right) = stop_clip(p.ratio, p.before, p.carrying) {
            if p.set_bits & if right == WSTOP_R { 0x1000_0000 } else { 0x0800_0000 } == 0 {
                return halt(b, o);
            }
            // The stride: the phase of the last stop id's clip with flag 0x100.
            let mut phase = 0.0;
            for s in &mut b.stops {
                if *s != -1 {
                    if let Some(c) = find(*s as u32).filter(|c| c.flags & 0x100 != 0) {
                        phase = c.phase;
                    }
                    *s = -1;
                }
            }
            let (id, phase) = stop_foot(right, phase);
            if !p.in_set && p.alt != Some(true) {
                return o;
            }
            (o.cleared, o.ask) = (true, Some(id));
            if p.started {
                (o.phase, o.state, b.ratio, b.rate54) = (Some(phase), -1.0, 0.0, 3.0);
            }
            return o;
        }
    }
    if p.ratio == 0.0 {
        let Some(c) = [SSTOP_L, SSTOP_R, RSTOP_L, RSTOP_R, WSTOP_L, WSTOP_R].into_iter().find_map(find) else {
            o.state = if p.current.is_some() { -2.0 } else { 0.0 };
            return o;
        };
        o.out = Some(c.id);
        let cut = p.switch && p.ped_26c & 0x2000_0000 == 0 && stop_cut(want, crate::ik::wrap(p.want_heading - p.heading));
        if !cut {
            if c.flags & 0x10 != 0 && !stop_done(c.phase, c.step) {
                o.state = -1.0;
            } else {
                o.blend_outs.push((c.id, -BLEND_OUT));
                o.out = None;
            }
            return o;
        }
    }
    if let Some(id) = o.out.take() {
        o.blend_outs.push((id, -BLEND_OUT));
    }
    b.b48 = -1;
    use crate::movestart::*;
    let start = [WSTART, WSTART_TURN_L180, WSTART_TURN_R180, WSTART_TURN_L90, WSTART_TURN_R90, RUNSTART_FWD, RUNSTART_L_180,
                 RUNSTART_R_180, RUNSTART_L_90, RUNSTART_R_90].into_iter().find_map(find);
    if let Some(c) = start.filter(|c| !(c.rate < 0.0) && !(c.held && c.rate == 0.0)) {
        if !(c.flags & 0x10 == 0 || c.held || start_done(c.time, c.advance, c.length)) {
            o.state = 1.0;
            return o;
        }
        if (RUNSTART_L_90..=RUNSTART_R_180).contains(&c.id) {
            (b.b1c, b.b28, b.b2c, b.b78) = (3.0, 0.0, 0.0, 1.0);
        }
        o.removed = true;
        for id in p.set_ids {
            b.stops.iter_mut().filter(|s| **s == id).for_each(|s| *s = -1);
        }
        if find(crate::moveturn::IDLE).is_some() {
            o.blend_outs.push((crate::moveturn::IDLE, -500.0));
        }
        (o.ratio, b.ratio) = (start_ratio(c.id), start_ratio(c.id));
        o.state = o.ratio + 1.0;
        return o;
    }
    let r = p.ratio;
    o.state = if r == 0.0 {
        0.0
    } else if !(r > 1.0) {
        // Starting (1) until the start clip blends out, unless the set has no starts (+0x378 bit 31), 0x822948E0 says
        // walk, or the 180 turn ended this frame (+0x50 bit 14).
        let walk = p.walk || p.set_bits & 0x8000_0000 == 0 || p.bits & 0x4000 != 0 || start.is_some_and(|c| c.rate >= 0.0);
        if walk { 2.0 } else { 1.0 }
    } else if r <= 3.0 {
        r + 1.0
    } else {
        0.0
    };
    o
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_speed_picks_walk_run_or_sprint_stops() {
        assert_eq!(stop_clip(1.2, 1.3, false), Some(WSTOP_R));
        assert_eq!(stop_clip(2.0, 2.1, false), Some(RSTOP_R));
        assert_eq!(stop_clip(2.8, 2.9, false), Some(SSTOP_R));
        // Carrying something, 1.2 is already a run.
        assert_eq!(stop_clip(1.2, 1.3, true), Some(RSTOP_R));
        // Not moving before the easing: no stop.
        assert_eq!(stop_clip(0.5, 0.0, false), None);
    }

    #[test]
    fn the_stride_picks_the_foot() {
        assert_eq!(stop_foot(WSTOP_R, 0.1), (WSTOP_L, 0.1));
        assert_eq!(stop_foot(WSTOP_R, 0.3), (WSTOP_R, 0.0));
        assert_eq!(stop_foot(RSTOP_R, 0.6), (RSTOP_R, 0.6 - 0.5));
        assert_eq!(stop_foot(SSTOP_R, 0.8), (SSTOP_L, 0.0));
    }

    #[test]
    fn moving_again_fades_the_stop() {
        // Walking (2) or running (ratio + 1): the walk and run stops fade; standing (0), starting (1) or stopping (-1)
        // they don't; SStop_R never does (the game's list leaves it out).
        assert!(stop_fades(2.0, WSTOP_R) && stop_fades(3.4, RSTOP_R) && stop_fades(4.0, SSTOP_L));
        assert!(!stop_fades(1.0, WSTOP_L) && !stop_fades(0.0, WSTOP_L) && !stop_fades(-1.0, RSTOP_L));
        assert!(!stop_fades(4.0, SSTOP_R) && !stop_fades(2.0, 0x1C));
    }

    #[test]
    fn a_stop_ends_near_its_end_or_when_he_moves_or_turns() {
        assert!(!stop_done(0.9, 0.05));
        assert!(stop_done(0.95, 0.05));
        assert!(stop_cut(1.0, 0.0));
        assert!(stop_cut(0.0, 0.5));
        assert!(!stop_cut(0.0, 0.3));
    }

    #[test]
    fn the_pick_stops_a_walker_and_ends_a_start() {
        let p = Pick { ratio: 1.2, before: 1.3, state: 2.0, set_bits: 0x1000_0000, in_set: true, started: true, switch: true,
                       ..Default::default() };
        let mut b = PickBlend { b48: -1, ratio: 1.2, ..Default::default() };
        let out = pick(&p, &mut b);
        assert_eq!((out.state, out.ask, b.ratio, b.rate54), (-1.0, Some(WSTOP_L), 0.0, 3.0));
        // A walk start whose clip has run its length: walking (2), the ratio 1.
        let start = PickClip { id: crate::movestart::WSTART, flags: 0x10, time: 1.0, advance: 0.1, length: 1.0, ..Default::default() };
        let p = Pick { want: 1.0, ratio: 0.5, playing: vec![start], ..Default::default() };
        let out = pick(&p, &mut PickBlend::default());
        assert_eq!((out.state, out.ratio, out.removed), (2.0, 1.0, true));
        // Still playing: starting (1); the 180 turn's end (+0x50 bit 14) makes it walking.
        let p = Pick { playing: vec![PickClip { time: 0.0, ..start }], set_bits: 0x8000_0000, ..p };
        assert_eq!(pick(&p, &mut PickBlend::default()).state, 1.0);
        let gone = PickClip { rate: -1.0, ..start };
        let p = Pick { playing: vec![gone], ..p };
        assert_eq!(pick(&p, &mut PickBlend::default()).state, 1.0);
        assert_eq!(pick(&Pick { bits: 0x4000, ..p }, &mut PickBlend::default()).state, 2.0);
    }
}
