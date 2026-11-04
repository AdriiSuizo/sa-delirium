//! The anim blender (board row PD2): RAGE's anim player (CAnimPlayer, vtable 0x8201AC28) with its blend in and out,
//! loop or one-shot and phase advance; the ped's blender update (0x82449AF8: weights 0x82445728, synced phases
//! 0x824457E0, the priority weights 0x82445F80 and layering 0x82444428); crFrame's BlendN (0x82813D28) with the
//! weight correction filter (0x82443D40); the bone masks for partial-body clips (0x8247D7C8 / 0x8247DC90); and the
//! on-foot move blend's ratio easing (0x82415158) and split into gait clips (0x82414BF0). Single precision, fused
//! multiply-adds in the game's order. A player's fields are named for its offsets in the game's object.
//!
//! The mover channel's weights (0x82445BE0, [`mover_weights`]) say how much each player's mover track carries the ped.
//!
//! The move blend's clip list: its weights evened out (0x82412CE8, [`normalize_list`]) and, after a clip set change,
//! the old set's clips going (0x824185E8's clip loop, [`old_set_rate`], [`old_set_weight`]).
//!
//! Not here: the motion tree's nodes and requests (0x82446EC0), the mover channel's tree (+0x8B8), anim events
//! (0x82455610), the callbacks a player fires when it ends, the clip table by gait and direction (0x824131E0), IK and
//! facial.
use crate::xenon::fneg;

/// "Work it out" for `weight_step` / `phase_step` (-1.0, 0x820BECF4).
pub const UNSET: f32 = -1.0;

/// One anim player.
#[derive(Clone, Debug, PartialEq)]
pub struct Player {
    /// The clip's length in seconds (crAnimation +0x0C); `None` with no clip (+0x44 state not 1).
    pub duration: Option<f32>,
    /// +0x4C, +0x50: where it is (0-1) and where it was before this frame's advance.
    pub phase: f32,
    pub prev_phase: f32,
    /// +0x54: playback speed.
    pub rate: f32,
    /// +0x58 weight, +0x5C the weight it blends to (0 or 1), +0x60 how fast (per second).
    pub weight: f32,
    pub goal: f32,
    pub blend_rate: f32,
    /// +0x2C, +0x30: this frame's weight and phase steps when set (else `UNSET`).
    pub weight_step: f32,
    pub phase_step: f32,
    /// +0x3C: the blend rate used to blend out at the clip's end (-4).
    pub blend_out: f32,
    /// +0x08: its layer (0-9; higher layers take their weight first).
    pub priority: u8,
    /// +0x04 low nibble: its bone mask (`MASKS`; 0 = no mask).
    pub mask: u8,
    /// +0x46 flags: 1 playing, 2 paused, 4 reached an end this frame, 8 looping, 0x10 holds at its end, 0x20 blending.
    pub playing: bool,
    pub paused: bool,
    pub finished: bool,
    pub looped: bool,
    pub hold: bool,
    pub blending: bool,
    /// +0x04 flags: 0x10 active (cleared when a one-shot ends), 0x20 loops, 0x100 phase-synced with the others,
    /// 0x4000 removed once blended out, 0x8000 blends out at its end, 0x20000 on its own channel (out of the priority
    /// weights' sums), 0x400000 skips one weight update.
    pub active: bool,
    pub loops: bool,
    pub synced: bool,
    pub remove_blended_out: bool,
    pub blend_out_at_end: bool,
    pub own_channel: bool,
    pub skip_weight: bool,
}

impl Player {
    /// A player as 0x82454070 sets it up, holding a clip of `duration` s (+0x44 = 1).
    pub fn new(duration: f32) -> Self {
        Player {
            duration: Some(duration), phase: 0.0, prev_phase: 0.0, rate: 1.0, weight: 1.0, goal: 0.0, blend_rate: 0.0,
            weight_step: UNSET, phase_step: UNSET, blend_out: -4.0, priority: 0, mask: 0,
            playing: false, paused: false, finished: false, looped: false, hold: false, blending: false,
            active: false, loops: false, synced: false, remove_blended_out: false, blend_out_at_end: false,
            own_channel: false, skip_weight: false,
        }
    }

    /// 0x82455268: the blend rate while it blends to 0 or 1, else 0.
    pub fn blend_speed(&self) -> f32 {
        if self.blending && (self.goal == 1.0 || self.goal == 0.0) { self.blend_rate } else { 0.0 }
    }

    /// 0x82455BC0: the blend rate, negative while it blends out.
    pub fn signed_blend_rate(&self) -> f32 {
        if self.blending && self.goal == 1.0 {
            self.blend_speed()
        } else if self.blending && self.goal == 0.0 {
            -self.blend_speed()
        } else {
            0.0
        }
    }

    /// 0x82455C68: blend in (`delta` > 0, per second) or out (< 0); 0 stops blending. Ignored when already at the
    /// end it asks for and blending there no slower. The rate is at least 0.001.
    pub fn set_blend(&mut self, delta: f32) {
        if delta > 0.0 {
            if self.weight >= 1.0 && delta <= self.signed_blend_rate() {
                return;
            }
            (self.goal, self.blending) = (1.0, true);
            self.blend_rate = if delta - 0.001 >= 0.0 { delta } else { 0.001 };
        } else if delta < 0.0 {
            if self.weight <= 0.0 && delta >= self.signed_blend_rate() {
                return;
            }
            let d = -delta;
            (self.goal, self.blending) = (0.0, true);
            self.blend_rate = if d - 0.001 >= 0.0 { d } else { 0.001 };
        } else {
            self.blending = false;
        }
    }

    /// 0x82454340, a player taken up again (0x82449918): active, back at its start (its end for a negative rate), not
    /// paused, and playing if it holds a clip.
    pub fn restart(&mut self) {
        self.active = true;
        let at = if self.rate >= 0.0 { 0.0 } else { 1.0 };
        (self.phase, self.prev_phase, self.paused) = (at, at, false);
        if self.duration.is_some() {
            self.playing = true;
        }
    }

    /// vf1 (0x824541C0): this frame's weight step.
    fn weight_step_now(&self, dt: f32) -> f32 {
        if self.weight_step != UNSET { self.weight_step } else { self.blend_rate * dt }
    }

    /// vf2 (0x824541F0): this frame's phase step, 30 / (frames - 1) frames a second at `rate`.
    pub fn phase_step_now(&self, dt: f32) -> f32 {
        if self.phase_step != UNSET {
            return self.phase_step;
        }
        frame_rate(self.duration.unwrap_or(0.0)) * self.rate * dt
    }

    /// 0x82445728 for one player: while it blends, its step is the blend rate times `dt`, then 0x82455A08.
    /// True when it was removed (blended out with `remove_blended_out`).
    pub fn blend_weight(&mut self, dt: f32) -> bool {
        if self.duration.is_some() && self.blending {
            self.weight_step = self.blend_speed() * dt;
            return self.update_weight(dt);
        }
        false
    }

    /// 0x82455A08: move the weight a step toward its goal (held at 0 from below). Blended out and not blending with
    /// `remove_blended_out`, the player drops its clip (+0x44 = 0); true then.
    pub fn update_weight(&mut self, dt: f32) -> bool {
        if self.skip_weight {
            self.skip_weight = false;
        } else if self.blending {
            let step = self.weight_step_now(dt);
            let d = self.goal - self.weight;
            if !(d.abs() > step) {
                (self.weight, self.blending) = (self.goal, false);
            } else {
                let sign: f32 = if d >= 0.0 { if -d >= 0.0 { 0.0 } else { 1.0 } } else { -1.0 };
                let w = sign.mul_add(step, self.weight);
                self.weight = if w >= 0.0 { w } else { 0.0 };
            }
        }
        if self.weight > 0.0 {
            return false;
        }
        self.weight = 0.0;
        if self.blending || !self.remove_blended_out {
            return false;
        }
        // The callbacks it fires here and in 0x82455128 are not modelled; then +0x44 = 0.
        self.duration = None;
        true
    }

    /// 0x824552E0: step the phase. At an end a looping clip wraps; a one-shot stops there, held (its previous phase
    /// set to the end too) or no longer playing.
    pub fn advance_phase(&mut self, dt: f32) {
        self.prev_phase = self.phase;
        self.finished = false;
        if !self.playing || self.paused {
            return;
        }
        if self.duration.is_some() {
            self.phase = self.phase_step_now(dt) + self.phase;
            if self.phase >= 1.0 {
                self.finished = true;
                if self.looped {
                    self.phase -= self.phase.trunc();
                } else {
                    self.phase = 1.0;
                    if self.hold { self.prev_phase = 1.0 } else { self.playing = false }
                }
            } else if self.phase < 0.0 {
                self.finished = true;
                if self.looped {
                    self.phase = ((self.phase + 100.0) as f64 % 1.0) as f32;
                } else {
                    self.phase = 0.0;
                    if self.hold { self.prev_phase = 0.0 } else { self.playing = false }
                }
            }
        }
        if self.prev_phase > self.phase {
            self.prev_phase = 0.0;
        }
    }

    /// 0x82455FB8 (anim events left out): looping as asked, else held at the end; step the phase; a one-shot that
    /// reached its end goes inactive and, with `blend_out_at_end`, blends out at `blend_out` to be removed.
    pub fn update(&mut self, dt: f32) {
        if self.loops {
            self.looped = true;
        } else {
            (self.looped, self.hold) = (false, true);
        }
        self.advance_phase(dt);
        if self.looped || !self.finished {
            return;
        }
        self.active = false;
        if self.blend_out_at_end {
            self.remove_blended_out = true;
            self.set_blend(self.blend_out);
        }
    }
}

/// Frames a second over a clip's frames (0x824541F0, 0x824457E0): 30 / max(duration Ã— 30 + 1 - 1, 1).
fn frame_rate(duration: f32) -> f32 {
    let frames = duration.mul_add(30.0, 1.0) - 1.0;
    30.0 / if 1.0 - frames >= 0.0 { 1.0 } else { frames }
}

/// 0x824457E0: the phase steps of the active players, then each one's update. Players marked `synced` share one
/// cycle: each steps as if its clip were the weighted average length (Î£ duration Ã— weight / rate over Î£ weight), so
/// walk and run feet stay in step while they blend.
pub fn sync_phases(players: &mut [Player], dt: f32) {
    let (mut sum_w, mut sum_d) = (0.0f32, 0.0f32);
    for p in players.iter().filter(|p| p.active && p.synced) {
        if let Some(d) = p.duration {
            sum_w = p.weight + sum_w;
            sum_d = d.mul_add(p.weight / p.rate, sum_d);
        }
    }
    for p in players.iter_mut().filter(|p| p.active) {
        let Some(d) = p.duration else { continue };
        p.phase_step = if sum_d == 0.0 || !p.synced {
            // ponytail: the clip lookup (0x82832E50 / 0x822E65F8) done here when nothing is synced has no effect.
            frame_rate(d) * p.rate * dt
        } else {
            let average = sum_d / sum_w;
            (d / p.rate / average) * (frame_rate(d) * p.rate * dt)
        };
    }
    for p in players.iter_mut() {
        if p.active && p.duration.is_some() {
            p.update(dt);
        } else {
            p.prev_phase = p.phase;
        }
    }
}

/// 0x82445F80: the body channel's weight of each player (those with no clip get 0). Per layer from 9 down: the
/// layer's players are scaled to sum to 1 when they sum over 1 (always on layer 0); a lone player on layer 0 gets
/// 1. Players on their own channel are scaled with their layer but left out of its sum. ponytail: the blender's
/// extra player (+0x1A40, layer 8) is not modelled.
pub fn priority_weights(players: &[Player]) -> Vec<f32> {
    let mut out = vec![0.0; players.len()];
    let (mut count0, mut only0) = (0, 0);
    for layer in (0..=9u8).rev() {
        let mut sum = 0.0f32;
        for (i, p) in players.iter().enumerate() {
            if p.duration.is_some() && !p.own_channel && p.priority == layer {
                sum = p.weight + sum;
                if layer == 0 {
                    (count0, only0) = (count0 + 1, i);
                }
            }
        }
        let scale = if sum > 0.0 { 1.0 / sum } else { 1.0 };
        for (o, p) in out.iter_mut().zip(players) {
            if p.duration.is_some() && p.priority == layer {
                *o = if layer == 0 || sum > 1.0 { p.weight * scale } else { p.weight };
            }
        }
    }
    if count0 == 1 {
        out[only0] = 1.0;
    }
    out
}

/// Layering by priority, from layer `top` down (0x82444428 with 9, the weight correction filter 0x82443D40 with 8):
/// each layer takes what is left of 1; a layer that wants more than is left gets the rest (its weights scaled by
/// it, or a lone weight set to it) and the layers under it get nothing.
pub fn layer_weights(weights: &mut [f32], priority: &[u8], top: u8) {
    let mut left = 1.0f32;
    for layer in (0..=top).rev() {
        let (mut sum, mut count, mut only) = (0.0f32, 0, 0);
        for (i, (&w, _)) in weights.iter().zip(priority).enumerate().filter(|&(_, (_, &p))| p == layer) {
            (sum, count, only) = (w + sum, count + 1, i);
        }
        if left < sum {
            if count > 1 {
                for (w, _) in weights.iter_mut().zip(priority).filter(|&(_, &p)| p == layer) {
                    *w *= left;
                }
            } else {
                weights[only] = left;
            }
            left = 0.0;
        } else {
            left -= sum;
        }
    }
}

/// One degree of freedom of a crFrame (a track's value).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Dof {
    /// +0x05 the track (0 bone translation, 1 bone rotation, ...) and +0x06 its id (a bone's tag).
    pub track: u8,
    pub id: u16,
    /// +0x04 low nibble: 0 a vector, 1 a quaternion (x, y, z, w), 2 a float, other kinds copied whole.
    pub kind: u8,
    /// +0x04 bit 0x10: no value.
    pub invalid: bool,
    pub value: [f32; 4],
}

/// 0x82813D28 (BlendN, frames of the same layout): `dest` mixed with each source frame by its weight. The weights'
/// total (held to 1) leaves `dest` the rest (none past 0.999). Per DOF: a source without a value counts 0; with
/// `priority`, bone and 0x15 tracks are layered (crFrameFilterWeightCorrection, 0x82443D40); then each weight is
/// taken over the running total and the sources go on in turn: quaternions by a normalised lerp along the shorter
/// way, vectors and floats by a lerp (copied from 0.999), other values copied from 0.5.
pub fn blend_n(dest: &mut [Dof], sources: &[&[Dof]], weights: &[f32], priority: Option<&[u8]>) {
    let total = weights.iter().fold(0.0f32, |s, &w| s + w);
    let total = if total - 1.0 >= 0.0 { 1.0 } else { total };
    let rest = if total > 0.999 { 0.0 } else { 1.0 - total };
    let mut w = vec![0.0f32; weights.len()];
    for (d, dof) in dest.iter_mut().enumerate() {
        for (i, s) in sources.iter().enumerate() {
            w[i] = if s[d].invalid { 0.0 } else { weights[i] };
        }
        if let Some(p) = priority.filter(|_| dof.track < 2 || dof.track == 0x15) {
            layer_weights(&mut w, p, 8);
        }
        let mut acc = rest;
        for w in w.iter_mut() {
            acc = *w + acc;
            if acc >= 0.001 {
                let q = *w / acc;
                *w = if !(q >= 0.0) { 0.0 } else if q >= 1.0 { 1.0 } else { q };
            } else {
                (*w, acc) = (0.0, 0.0);
            }
        }
        for (s, &t) in sources.iter().zip(&w).filter(|&(_, &t)| t > 0.0) {
            blend_dof(dof, &s[d], t);
        }
    }
}

fn blend_dof(d: &mut Dof, s: &Dof, t: f32) {
    match d.kind {
        1 if !d.invalid => {
            let [mut dx, mut dy, mut dz, mut dw] = d.value;
            let [sx, sy, sz, sw] = s.value;
            let dot = sz.mul_add(dz, sy.mul_add(dy, dx.mul_add(sx, dw * sw)));
            if dot < 0.0 {
                (dx, dy, dz, dw) = (-dx, -dy, -dz, -dw);
            }
            let u = 1.0 - t;
            let x = dx.mul_add(u, sx * t);
            let y = sy.mul_add(t, dy * u);
            let z = sz.mul_add(t, dz * u);
            let w = dw.mul_add(u, t * sw);
            let len2 = w.mul_add(w, z.mul_add(z, x.mul_add(x, y * y)));
            // frsqrte and two Newton steps in double, to single. ponytail: the estimate taken as the exact 1/sqrt.
            let r = if -len2 >= 0.0 { 0.0 } else { (1.0 / (len2 as f64).sqrt()) as f32 };
            d.value = [r * x, r * y, r * z, r * w];
        }
        1 => (d.value, d.invalid) = (s.value, false),
        0 if t > 0.001 => {
            if t < 0.999 && !d.invalid {
                for (a, &b) in d.value.iter_mut().zip(&s.value) {
                    *a = (b - *a).mul_add(t, *a);
                }
            } else {
                d.value = s.value;
            }
            d.invalid = false;
        }
        0 => {}
        2 if t < 0.999 && !d.invalid => (d.value[0], d.invalid) = ((s.value[0] - d.value[0]).mul_add(t, d.value[0]), s.invalid),
        2 if t > 0.001 => (d.value[0], d.invalid) = (s.value[0], s.invalid),
        2 => {}
        _ if t >= 0.5 => (d.value[0], d.invalid) = (s.value[0], false),
        _ => {}
    }
}

/// One anim player as the mover blend (0x82445BE0) reads it: a node of the ped's blender list.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct MoverPlayer {
    /// +0x44 is 1 and +0x40, its clip, is not null: it holds a clip. One with none has no place in the blend.
    pub clip: bool,
    /// +0x08: its layer (0 to 8: the sums and the normalising stop at 8).
    pub layer: i32,
    /// +0x58: its weight.
    pub weight: f32,
    /// +0x04: its flags.
    pub flags: u32,
    /// The clip's flags (+6) bit 0x10: it has a mover track.
    pub mover_track: bool,
}

/// Flag 0x10000 of a player: the full weight in the mover blend. Flag 0x20000: its own channel, so its mover track does not
/// count (the drive-by fires).
pub const FULL_MOVER_WEIGHT: u32 = 0x1_0000;
pub const OWN_CHANNEL: u32 = 0x2_0000;

/// 0x82445BE0: the weight each player's mover track has in the blender's mover channel (the ped is carried by the blend of
/// the tracks). In the list's order the players that hold a clip get a place: the weight of one that moves the ped (a
/// mover track the row does not leave out, not on its own channel, and a bone mask of none or that passes the root, or any
/// mover bit: the test of `staticcount::anim_moves`, 0x824543A0 without the weight limit), else 0; the NM frozen pose
/// (the extra player at +0x1A40, layer 8) follows when its weight `frozen` is above 0. Then per layer 0 to 8 the sum of the
/// weights; the players with flag 0x10000 get 1, the others are scaled to sum to 1 when the layer is 0 or the sum is over 1;
/// one player on layer 0 gets 1; at last each layer takes what is left of 1 from layer 9 down (`layer_weights`, 0x82444428).
/// The answer has a weight for each of `players` (0 for one with no clip) and, last, for the frozen pose.
pub fn mover_weights(players: &[MoverPlayer], frozen: f32) -> Vec<f32> {
    use crate::staticcount::{MOVER_BITS, NO_MOVER_TRACK};
    let (mut w, mut layer, mut full, mut from) = (vec![], vec![], vec![], vec![]);
    let (mut count0, mut lowest) = (0, 0);
    for (k, p) in players.iter().enumerate().filter(|(_, p)| p.clip) {
        let root = |mask: u32| mask == 0 || mask_passes(mask as u8, 0);
        let counts = p.flags & NO_MOVER_TRACK == 0 && p.mover_track && p.flags & OWN_CHANNEL == 0 && root(p.flags & 0xF)
            || p.flags & MOVER_BITS != 0;
        if counts && p.layer == 0 {
            (count0, lowest) = (count0 + 1, w.len());
        }
        w.push(if counts { p.weight } else { 0.0 });
        layer.push(p.layer);
        full.push(p.flags & FULL_MOVER_WEIGHT != 0);
        from.push(k);
    }
    if frozen > 0.0 {
        (w.push(frozen), layer.push(8), full.push(false), from.push(players.len()));
    }
    let mut sums = [0.0f32; 9];
    for (&l, &x) in layer.iter().zip(&w) {
        if let Some(s) = usize::try_from(l).ok().and_then(|l| sums.get_mut(l)) {
            *s = x + *s;
        }
    }
    for (l, &sum) in sums.iter().enumerate() {
        let inv = if sum > 0.0 { 1.0 / sum } else { 1.0 };
        for i in (0..w.len()).filter(|&i| layer[i] == l as i32) {
            if full[i] {
                w[i] = 1.0;
            } else if l == 0 || sum > 1.0 {
                w[i] *= inv;
            }
        }
    }
    if count0 == 1 {
        w[lowest] = 1.0;
    }
    layer_weights(&mut w, &layer.iter().map(|&l| u8::try_from(l).unwrap_or(255)).collect::<Vec<_>>(), 9);
    let mut out = vec![0.0; players.len() + 1];
    for (&k, &x) in from.iter().zip(&w) {
        out[k] = x;
    }
    out
}

/// The bones the masks know (0x82AA4BF0, by tag), in their slots.
pub const MASK_BONES: [u16; 133] = [
    0, 417, 1202, 1203, 13984, 13985, 1204, 1205, 14240, 1223, 1224, 1225, 1232, 13744, 13745, 13746, 13747, 13748,
    13749, 13750, 13751, 13752, 13753, 13760, 13761, 13762, 13763, 13764, 14753, 14754, 14752, 15873, 1216, 1217, 1218,
    1219, 13776, 13777, 13778, 13779, 13780, 13781, 13782, 13783, 13784, 13785, 13792, 13793, 13794, 13795, 13796, 14497,
    14498, 14496, 15857, 418, 419, 420, 421, 14512, 423, 424, 425, 1200, 14768, 30967, 30968, 30969, 30970, 30971, 30972,
    30979, 31220, 30980, 30981, 30982, 30983, 30984, 30985, 30986, 30987, 30988, 31219, 30995, 30996, 30997, 30998, 32660,
    32667, 32679, 32678, 32666, 32677, 32664, 32665, 32669, 32661, 32676, 32663, 32662, 32668, 43287, 43269, 43254, 43255,
    43256, 43257, 43258, 43259, 43260, 43267, 43268, 43286, 43270, 43271, 43272, 43273, 43274, 43275, 43276, 43283, 43284,
    43285, 43253, 43252, 51524, 51527, 51528, 51526, 51525, 45156, 45157, 45158,
];

/// The 12 bone masks (0x8247D7C8, from the slot lists at 0x82AA45E0..0x82AA4B88) as runs of slots. 0 is every
/// bone; 1 the upper body (all but the root, the pelvis and the legs' slots 55-58 and 60-63); 2-11 are smaller sets
/// (the spine and head, the arms, the face).
pub const MASKS: [&[(u8, u8)]; 12] = [
    &[(0, 132)],
    &[(2, 54), (59, 59), (64, 132)],
    &[(2, 7)],
    &[(32, 50)],
    &[(9, 27)],
    &[(0, 64)],
    &[(65, 86)],
    &[(87, 100)],
    &[(6, 7), (9, 27), (32, 50)],
    &[(6, 7), (32, 50)],
    &[(6, 7), (9, 27)],
    &[(0, 7), (9, 12), (32, 35), (55, 58), (60, 63)],
];

/// 0x8247DC90: does `mask` pass the bone with tag `bone`? A bone the masks do not know never passes.
/// (crFrameFilterBoneMask, 0x82443CD8, drops a masked clip's bone tracks that fail.)
pub fn mask_passes(mask: u8, bone: u16) -> bool {
    MASK_BONES.iter().position(|&b| b == bone)
        .is_some_and(|slot| MASKS[mask as usize].iter().any(|&(a, b)| (a as usize..=b as usize).contains(&slot)))
}

/// The on-foot move blend's ratio easing (0x82415158): toward `wanted` at 4 a second up and 2 down for the player
/// (2 and 8 for other peds; 0x82A99210..0x82A9921C), held to `top` + 1 going up (`top`: the clip set's top speed,
/// +0x350) and to 0 going down.
pub fn ease_ratio(ratio: f32, wanted: f32, dt: f32, player: bool, top: f32) -> f32 {
    let (up, down) = if player { (4.0, 2.0) } else { (2.0, 8.0) };
    if wanted > ratio {
        let r = dt.mul_add(up, ratio);
        let r = if r > wanted { wanted } else { r };
        if r > top + 1.0 { top + 1.0 } else { r }
    } else if wanted < ratio {
        let r = (-dt).mul_add(down, ratio);
        let r = if r < wanted { wanted } else { r };
        if r < 0.0 { 0.0 } else { r }
    } else {
        wanted
    }
}

/// 0x82414BF0: a move blend ratio split between the gait clips either side of it: (slot, weight) for the lower and,
/// unless the ratio is within 0.01 above a whole gait, the upper (weights 1 - fraction and fraction).
/// The player's stick step (0x823C7388): the move ratio for the stick's push `m` (its length, held to 1) and the run
/// value `s` (0x823D0F30): a walk at most 1 for s <= 0, a run at most 2 up to 1, a sprint at most 3 above (the tuning
/// 0x82AA1B5C..64). ponytail: a carried object's walk (ped +0x294) is not here.
pub fn move_ratio(m: f32, s: f32) -> f32 {
    let cap = if s > 1.0 { 3.0 } else if s > 0.0 { 2.0 } else { 1.0 };
    (cap * m).min(cap)
}

pub fn split_ratio(ratio: f32) -> [(i32, f32); 2] {
    let frac = (ratio as f64 % 1.0) as f32;
    let slot = ratio as i32;
    if frac.abs() < 0.01 { [(slot, 1.0), (-1, 0.0)] } else { [(slot, 1.0 - frac), (slot + 1, frac)] }
}

/// How far over 1 the clip list's weights may sum before 0x82412CE8 evens them out (0x820BEF4C).
const LIST_SLACK: f32 = 1.05;

/// 0x82412CE8: the move blend's clip list (0x82FD4518: its weights, in the list's order) evened out. Each weight is held
/// to 0..1; when they sum under 1 or over 1.05, all but the last are scaled by frsqrte(sum)^2 (about 1 / sum) and the
/// last takes what is left of 1. The list's players then take these weights as they are (0x82454300, at 0x82419454).
pub fn normalize_list(w: &mut [f32]) {
    let Some(last) = w.len().checked_sub(1) else { return };
    for x in w.iter_mut() {
        if !(*x >= 0.0) {
            *x = 0.0;
        } else if *x > 1.0 {
            *x = 1.0;
        }
    }
    let sum = w.iter().fold(0.0f32, |s, &x| s + x);
    if !(sum < 1.0) && sum <= LIST_SLACK {
        return;
    }
    let e = crate::xenon::frsqrte(sum as f64) as f32;
    let k = e * e;
    let mut rest = 0.0f32;
    for x in &mut w[..last] {
        *x *= k;
        rest = *x + rest;
    }
    w[last] = 1.0 - rest;
}

/// 0x824185E8 at 0x82418FE4: how fast a clip of the clip set the move blend just left (+0x40, set by 0x826511D8 when the
/// set +0x3C changes) goes: 1 a second (0x82000D48) between 0x36 and the ped's own set (ped +0xAC8) either way, and
/// between 0x37 and 0x32 either way; 2 a second (0x82000DC8) for any other change.
pub fn old_set_rate(old: i32, new: i32, own: i32) -> f32 {
    let slow = (old == 0x36 && new == own) || (old == own && new == 0x36) || (old == 0x37 && new == 0x32)
        || (old == 0x32 && new == 0x37);
    if slow { 1.0 } else { 2.0 }
}

/// 0x824185E8 at 0x82419180: a clip of the old set that the new list doesn't name (the move blend's clip loop, from
/// 0x824190B0) loses `rate` (`old_set_rate`) x the frame time from its player's weight (+0x58) and goes back into the
/// list at that (0x82412AB8, no blend, rate 1); the list is then evened out ([`normalize_list`]). `None` once that is 0
/// or below: the clip stops (0x82448858). With none of the old set left, +0x40 goes back to -1 (0x82419264).
pub fn old_set_weight(weight: f32, rate: f32, dt: f32) -> Option<f32> {
    let w = fneg(dt.mul_add(rate, -weight));
    if w <= 0.0 { None } else { Some(w) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_push_and_run_value_give_the_ratio() {
        assert_eq!(move_ratio(0.5, 0.0), 0.5);
        assert_eq!(move_ratio(1.0, 0.0), 1.0);
        assert_eq!(move_ratio(0.75, 1.0), 1.5);
        assert_eq!(move_ratio(1.0, 0.2), 2.0);
        assert_eq!(move_ratio(0.5, 1.3), 1.5);
        assert_eq!(move_ratio(1.0, 1.3), 3.0);
    }

    #[test]
    fn walk_and_run_blend_in_step_and_out() {
        // A walk (1 s) at 0.25 and a run (0.5 s) at 0.75, synced and looping: both step by dt over the average
        // length, 0.25 Ã— 1 + 0.75 Ã— 0.5 = 0.625 s.
        let clip = |d: f32, w: f32| Player { weight: w, active: true, loops: true, synced: true, playing: true, ..Player::new(d) };
        let mut ps = [clip(1.0, 0.25), clip(0.5, 0.75)];
        sync_phases(&mut ps, 0.1);
        for p in &ps {
            assert!((p.phase - 0.1 / 0.625).abs() < 1e-6, "{}", p.phase);
        }
        // Blending the walk out at 4 a second takes it to 0 in 0.0625 s; marked, it is removed.
        ps[0].set_blend(-4.0);
        ps[0].remove_blended_out = true;
        assert!(!ps[0].blend_weight(0.05));
        assert!((ps[0].weight - 0.05).abs() < 1e-6);
        assert!(ps[0].blend_weight(0.05));
        assert_eq!((ps[0].weight, ps[0].duration), (0.0, None));
        // A one-shot holds at its end, goes inactive and blends out to be removed.
        let mut shot = Player { active: true, playing: true, blend_out_at_end: true, ..Player::new(1.0) };
        shot.update(1.5);
        assert_eq!((shot.phase, shot.prev_phase, shot.active, shot.blending, shot.goal), (1.0, 1.0, false, true, 0.0));
    }

    #[test]
    fn layers_split_ratio_and_masks() {
        let mut w = [0.5, 0.75, 1.0];
        layer_weights(&mut w, &[2, 1, 0], 9);
        assert_eq!(w, [0.5, 0.5, 0.0]);
        assert_eq!(split_ratio(1.25), [(1, 0.75), (2, 0.25)]);
        assert_eq!(split_ratio(2.005), [(2, 1.0), (-1, 0.0)]);
        assert!(mask_passes(1, 1232) && !mask_passes(1, 0) && !mask_passes(1, 418) && !mask_passes(0, 7));
        let q = |v: [f32; 4]| Dof { track: 1, id: 0, kind: 1, invalid: false, value: v };
        let mut dest = [q([0.0, 0.0, 0.0, 1.0])];
        // b is a's far side (dot < 0), so they mix the short way: halfway is (0, 0, 0.3, 0.9) normalised (either sign).
        let (a, b) = ([q([0.0, 0.0, 0.0, 1.0])], [q([0.0, 0.0, -0.6, -0.8])]);
        blend_n(&mut dest, &[&a, &b], &[0.5, 0.5], None);
        let want = [0.0, 0.0, 0.3 / 0.9f32.sqrt(), 0.9 / 0.9f32.sqrt()];
        let dot: f32 = dest[0].value.iter().zip(want).map(|(x, y)| x * y).sum();
        assert!((dot.abs() - 1.0).abs() < 1e-5, "{:?}", dest[0].value);
    }

    #[test]
    fn clip_list_evens_out() {
        // Within 1..1.05 the list stays; each weight is held to 0..1 first.
        let mut w = [0.6, 0.42, -0.2];
        normalize_list(&mut w);
        assert_eq!(w, [0.6, 0.42, 0.0]);
        // Over: all but the last scaled by frsqrte(sum)^2 (a 5-bit 1 / sum), the last what's left of 1.
        let mut w = [1.0, 0.9, 1.5];
        normalize_list(&mut w);
        let e = crate::xenon::frsqrte(2.9) as f32;
        assert_eq!(w[..2], [e * e, 0.9 * (e * e)]);
        assert_eq!(w[2], 1.0 - (w[1] + w[0]));
        assert!((w[0] - 1.0 / 2.9).abs() < 0.03, "{w:?}");
        // Under 1 too.
        let mut w = [0.25, 0.25];
        normalize_list(&mut w);
        assert_eq!(w[1], 1.0 - w[0]);
        assert!(w[0] > 0.45 && w[0] < 0.55, "{w:?}");
        let mut w: [f32; 0] = [];
        normalize_list(&mut w);
    }

    #[test]
    fn old_set_goes() {
        assert_eq!(old_set_rate(0x3A, 0x2F, 0x2F), 2.0);
        assert_eq!(old_set_rate(0x36, 0x2F, 0x2F), 1.0);
        assert_eq!(old_set_rate(0x2F, 0x36, 0x2F), 1.0);
        assert_eq!((old_set_rate(0x37, 0x32, 0x2F), old_set_rate(0x32, 0x37, 0x2F), old_set_rate(0x36, 0x31, 0x2F)), (1.0, 1.0, 2.0));
        assert_eq!(old_set_weight(1.0, 2.0, 0.25), Some(0.5));
        assert_eq!(old_set_weight(0.5, 2.0, 0.25), None);
        // Out of the melee stance at 30 fps: the stance clip shares the list with a gait clip of weight 1 each frame
        // (about half at once) and is gone within a quarter of a second.
        let (mut w, mut frames) = (1.0f32, 0);
        while let Some(x) = old_set_weight(w, 2.0, 1.0 / 30.0) {
            let mut list = [1.0, x];
            normalize_list(&mut list);
            assert!(list[1] < w);
            w = list[1];
            frames += 1;
        }
        assert!((3..=7).contains(&frames), "{frames} {w}");
    }
}
