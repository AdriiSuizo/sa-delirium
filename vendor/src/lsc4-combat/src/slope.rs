//! The on-foot blend's slope (board PD5e). 0x82414D80 reads the ground under a walking ped from its samples, and when
//! the player runs from a look-ahead probe; 0x824185E8 then swaps the walk and run clips for the slope ones
//! ([`clip_on_slope`]). Single precision, fused multiply-adds where the game fuses them.
//!
//! Not here: the probe itself (0x82360B70, the map's segment probe; the caller passes it in), and the step 0x82417438
//! that counts the timer down. The strafing path (0x82418290) never reads the slope.

/// The blend's slope fields: +0x98..+0xA4 (the last sample), +0xA8 bit 15, +0xAA (the slope), +0x78 (the timer), and
/// the look-ahead's two globals (0x82FD44FB, 0x82FD4500).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Slope {
    /// +0x98..+0xA0: where the ped was at the last sample.
    pub sample: [f32; 3],
    /// +0xA4: the ground height (ped +0xA60) at the last sample.
    pub sample_ground: f32,
    /// +0xA8 bit 15: the last sample stood on the ground. A slope is only read when this and the new sample both do.
    pub sample_grounded: bool,
    /// +0xAA: 1 uphill, -1 downhill, 0 flat.
    pub slope: i16,
    /// +0x78: seconds left on the slope, 1.0 while a slope is set. This function never lowers it.
    /// ponytail: the step's count-down of this timer (0x82417D7C) is not here, add when the on-foot step is ported.
    pub timer: f32,
    /// 0x82FD44FB: the last look-ahead found ground. While it is set, the player does not sample.
    pub probe_hit: bool,
    /// 0x82FD4500: when the last look-ahead ran, in ms (the game clock 0x82C6C2A4).
    /// ponytail: one clock per blend, the game's 0x82FD4500 is shared by all peds; only the player probes, add when a
    /// second ped probes.
    pub probe_ms: u32,
}

/// What one frame reads.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Input {
    /// The ped's position (its matrix +0x30).
    pub pos: [f32; 3],
    /// The ped's ground height (ped +0xA60).
    pub ground: f32,
    /// The ped stands on something: ped +0x14C bit 0, or the 0x82FD44FA override (set outside this function).
    pub stands: bool,
    /// The ped's velocity in m/s (its virtual +0xE8).
    pub velocity: [f32; 3],
    /// The blend's ratio (+8).
    pub ratio: f32,
    /// The blend is the player's (+0x50 bit 18).
    pub player: bool,
    /// The move set allows slopes (set flag +0x378 bit 20).
    pub set_allows: bool,
    /// The move blender is strafing (ped +0x9D0, +0x50 bit 30).
    pub strafing: bool,
}

/// The squared move that takes a sample: 0.5 m. The game computes (0.1 Ã— 5)Â² once and keeps it at 0x82FD463C.
const MOVED_SQ: f32 = 0.25;

/// The slope a sample's height change makes (0x82414F04): a rise of 0.1 m or more is uphill, a drop of 0.1 m or more
/// downhill.
fn sample_slope(rise: f32) -> i16 {
    if rise >= 0.1 {
        1
    } else if rise <= -0.1 {
        -1
    } else {
        0
    }
}

/// The slope a probe's hit makes (0x824150E4): more than 0.1 m above the feet (z - 1) is uphill, more than 0.1 m below
/// downhill.
fn probe_slope(rise: f32) -> i16 {
    if rise > 0.1 {
        1
    } else if rise < -0.1 {
        -1
    } else {
        0
    }
}

impl Slope {
    /// 0x82414D80, one frame. `now_ms` is the game clock, and `probe` the look-ahead's segment probe (0x82360B70) from
    /// `start` down to `end`: the z of a hit with flag bit 24, else None.
    pub fn update(&mut self, i: &Input, now_ms: u32, mut probe: impl FnMut([f32; 3], [f32; 3]) -> Option<f32>) {
        // ponytail: the switches 0x82AA3072 and 0x82AA3073 are not read (both are 1 in the shipped exe), add when a
        // build turns one off.
        // A set without bit 20, or a strafe, is flat. The timer is left alone (0x82414DA4-0x82414DC8, 0x82415140).
        if !i.set_allows || i.strafing {
            self.slope = 0;
            return;
        }
        // A player below ratio 2 loses the probe's hit (0x82414DE0).
        if i.player && i.ratio < 2.0 {
            self.probe_hit = false;
        }
        // Sampling (0x82414EB0 on): a move of more than 0.5 m on the flat. The player skips it while a probe hit.
        let (dx, dy) = (i.pos[0] - self.sample[0], i.pos[1] - self.sample[1]);
        let mut sampled = false;
        if !(i.player && self.probe_hit) && dx.mul_add(dx, dy * dy) > MOVED_SQ {
            sampled = i.stands;
            // Both samples on the ground: compare the heights. Otherwise flat, airborne included.
            self.slope = if i.stands && self.sample_grounded { sample_slope(i.ground - self.sample_ground) } else { 0 };
            self.sample_grounded = i.stands;
            self.sample = i.pos;
            self.sample_ground = i.ground;
        }
        // Look-ahead (0x82414F70 on): a running player, when the sample did not stand this frame, at most every 250 ms.
        if i.player && i.ratio >= 2.0 && !sampled && now_ms.wrapping_sub(self.probe_ms) >= 250 {
            self.probe_ms = now_ms;
            // Half a second of the velocity ahead (vmaddfp with 0.5), raised 1 m, then straight down 4 m.
            let v = i.velocity;
            let start = [v[0].mul_add(0.5, i.pos[0]), v[1].mul_add(0.5, i.pos[1]), v[2].mul_add(0.5, i.pos[2]) + 1.0];
            let end = [start[0], start[1], start[2] - 4.0];
            // The probe is the caller's closure, add when the map's segment probe is wired in here.
            match probe(start, end) {
                Some(z) => {
                    self.probe_hit = true;
                    // The ground there against the feet (z - 1).
                    self.slope = probe_slope(z - (i.pos[2] - 1.0));
                }
                None => {
                    self.probe_hit = false;
                    self.slope = 0;
                }
            }
        }
        if self.slope != 0 {
            self.timer = 1.0;
        }
    }
}

/// The clip a walk or run clip becomes on a slope (0x824185E8's swap): 0x1C goes to 0x4C uphill and 0x4D downhill, 0x28
/// to 0x4E uphill and 0x4F downhill. Other clips, and flat, stay.
// ponytail: the nine-entry loop over 0x82FD451C and the caller's gate (the walk step returned 0, not strafing) are not
// here, add when 0x824185E8's clip part is ported.
pub fn clip_on_slope(slope: i16, clip: u32) -> u32 {
    match (slope.signum(), clip) {
        (1, 0x1C) => 0x4C,
        (1, 0x28) => 0x4E,
        (-1, 0x1C) => 0x4D,
        (-1, 0x28) => 0x4F,
        _ => clip,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn walk(pos: [f32; 3], ground: f32) -> Input {
        let (velocity, ratio) = ([0.0; 3], 1.0);
        Input { pos, ground, stands: true, velocity, ratio, player: false, set_allows: true, strafing: false }
    }

    #[test]
    fn ramp_reads_uphill_and_flat() {
        let mut s = Slope::default();
        // The first grounded sample has nothing to compare with: flat.
        s.update(&walk([0.6, 0.0, 0.0], 0.0), 0, |_, _| None);
        assert_eq!(s.slope, 0);
        // 0.6 m on, 0.2 m up: uphill, and the timer is set.
        s.update(&walk([1.2, 0.0, 0.0], 0.2), 0, |_, _| None);
        assert_eq!((s.slope, s.timer), (1, 1.0));
        // 0.6 m on the flat: flat, and the timer is left alone.
        s.timer = 0.4;
        s.update(&walk([1.8, 0.0, 0.0], 0.2), 0, |_, _| None);
        assert_eq!((s.slope, s.timer), (0, 0.4));
    }

    #[test]
    fn airborne_is_flat_and_still_samples() {
        let mut s = Slope::default();
        s.update(&walk([0.6, 0.0, 0.0], 0.0), 0, |_, _| None);
        // In the air, a 0.6 m move up 0.5 m: flat, the sample is still taken, but not as grounded.
        s.update(&Input { stands: false, ..walk([1.2, 0.0, 0.0], 0.5) }, 0, |_, _| None);
        assert_eq!(s.slope, 0);
        assert_eq!(s.sample, [1.2, 0.0, 0.0]);
        assert!(!s.sample_grounded);
        // Back on the ground the first sample is flat, the next one reads the rise.
        s.update(&walk([1.8, 0.0, 0.0], 0.5), 0, |_, _| None);
        assert_eq!(s.slope, 0);
        s.update(&walk([2.4, 0.0, 0.0], 0.8), 0, |_, _| None);
        assert_eq!(s.slope, 1);
    }

    #[test]
    fn look_ahead_only_for_a_running_player() {
        let mut s = Slope::default();
        let air = |ratio: f32, player: bool| Input { stands: false, ratio, player, ..walk([0.0; 3], 0.0) };
        let mut calls = Vec::new();
        // A walking player, and a non-player at ratio 2: no probe.
        s.update(&air(1.5, true), 1000, |a, b| {
            calls.push((a, b));
            None
        });
        s.update(&air(2.0, false), 1000, |a, b| {
            calls.push((a, b));
            None
        });
        assert!(calls.is_empty());
        // A running player: half a second of the velocity ahead, raised 1 m, probed 4 m down.
        let run = Input { velocity: [4.0, 0.0, 0.0], ..air(2.0, true) };
        s.update(&run, 1000, |a, b| {
            calls.push((a, b));
            None
        });
        assert_eq!(calls, [([2.0, 0.0, 1.0], [2.0, 0.0, -3.0])]);
        // Not again within 250 ms.
        s.update(&run, 1200, |a, b| {
            calls.push((a, b));
            None
        });
        assert_eq!(calls.len(), 1);
    }

    #[test]
    fn look_ahead_hit_reads_the_slope() {
        let mut s = Slope::default();
        // The feet are at z = -1. A hit at 0.0 is 1 m up (uphill), at -1.0 flat, at -2.0 1 m down (downhill).
        let run = Input { stands: false, ratio: 2.0, player: true, ..walk([0.0; 3], 0.0) };
        s.update(&run, 1000, |_, _| Some(0.0));
        assert_eq!((s.slope, s.probe_hit, s.timer), (1, true, 1.0));
        s.update(&run, 2000, |_, _| Some(-1.0));
        assert_eq!((s.slope, s.probe_hit), (0, true));
        s.update(&run, 3000, |_, _| Some(-2.0));
        assert_eq!(s.slope, -1);
        s.update(&run, 4000, |_, _| None);
        assert_eq!((s.slope, s.probe_hit), (0, false));
    }

    #[test]
    fn clip_swap_on_slopes() {
        assert_eq!(clip_on_slope(1, 0x1C), 0x4C);
        assert_eq!(clip_on_slope(1, 0x28), 0x4E);
        assert_eq!(clip_on_slope(-1, 0x1C), 0x4D);
        assert_eq!(clip_on_slope(-1, 0x28), 0x4F);
        assert_eq!(clip_on_slope(0, 0x1C), 0x1C);
        assert_eq!(clip_on_slope(1, 0x30), 0x30);
    }
}
