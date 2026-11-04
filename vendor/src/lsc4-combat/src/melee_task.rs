//! CTaskComplexMelee (431, ctor 0x822C0130): the fight a ped's Charge (1905, 0x82498D18) runs at its target, for a ped
//! that is not the player. The Charge makes it with flags +0xF0 = 0x13 (the target set, 0x822BE8B0) after a battle cry
//! (0x8246B008, not played here). Each frame its control (vf20 0x822C6B58) asks 0x822C0A90 what to do, and while it
//! fights, 0x822C6848 whether to start a move; the move itself is the action table's (0x822C5B88, `melee::choose`).
//! Constants from 0x82A99CB0..0x82A99D50.
//! The area box (SET_CHAR_MELEE_MOVEMENT_CONSTAINT_BOX, 0x822F1750) holds the stance's move ([`keep_distance`]).
//! The seek's stop radii by stage ([`SeekStage`], 0x822C0688 / 0x822C56E0). Peds at one target take turns
//! ([`take_turns`], 0x822C0490): the one waiting keeps farther off and steps away from the one nearest it. A hit on a
//! ped already fighting is a request on its melee ([`request_way`], [`request_takes`]); one blocking drifts ([`Drift`]).
//! The player's side of both is the viewer's (meleetask.rs `Melee::step`: the quit timer, the next-action pick).
use crate::mathrt;
use crate::rng::Rand;

pub const KIND: u32 = 431;

/// 0x822C0A90's answer for a ped.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step {
    /// 0: keep fighting (the move subtask, 433).
    Fight,
    /// 1: farther than `SEEK_BEYOND`: run to it (CTaskComplexSeekEntity 907, first stop radius `SEEK_NEAR`).
    Seek,
    /// 2: within `ROUTE_WITHIN` while a movement task runs: a nav-mesh route (942) to `ROUTE_PAST` m beyond the target.
    Route,
    /// 3: the target is in a vehicle or getting into one (it runs 734): the melee ends.
    Quit,
}

/// 0x82A99D0C.
pub const SEEK_BEYOND: f32 = 5.5;
/// 0x82A99D10: the seek's stop radius the first time (+0xD8 = 0).
pub const SEEK_NEAR: f32 = 3.3;
/// 0x82A99D44.
pub const ROUTE_WITHIN: f32 = 1.5;
/// 0x82A99D4C.
pub const ROUTE_PAST: f32 = 5.0;

/// The seek's stop radius and the flat distance it fights within, by stage (+0xD8; 0x822C56E0, 0x82A99D10..0x82A99D34;
/// a stage past these has both 0). The seek runs till the target is that near on the flat, then the move starts (433).
pub const SEEK_STAGES: [(f32, f32); 3] = [(SEEK_NEAR, 3.5), (0.15, 0.35), (0.9, 1.3)];
/// 0x822C0688's look interval (0x82A99D54, ms), and the target moving away past which it chases (0x82A99D2C) and
/// within which it stops chasing (0x82A99D34).
pub const SEEK_LOOK_MS: u32 = 2000;
/// The first look's wait: 431's first subtask (vf19 0x822C6738) arms the look timer for 0x82A99D50 ms.
pub const SEEK_FIRST_LOOK_MS: u32 = 500;
const AWAY_BEYOND: f32 = 2.5;
const AWAY_WITHIN: f32 = 1.3;

/// 0x822C0688 (an AI ped's, each frame its control vf20 runs it): how near the seek goes (+0xD8, [`SEEK_STAGES`]).
/// Two looks at the target (async world probes 0x82556178, flags 0x8E: from the ped's chest to its chest and from its
/// right knee to its right knee, points 5 and 7 by 0x8265DCB0) are asked when none is out (+0xF1 0x10) and the look
/// timer (+0xE4 on, +0xDC its start, +0xE0 its length; 0x821650F0: start + length <= now) has run out or isn't on, or
/// the stage last taken is 1; their answers (0x82554F20 done, 0x825558A8 clear) restart the timer for `SEEK_LOOK_MS`
/// and, both clear, take stage 1 back to 0, else give 1. The ctor (0x822C0130) leaves it off; 431's first subtask
/// (vf19 0x822C6738) puts it on for `SEEK_FIRST_LOOK_MS` ([`SeekStage::start`]).
/// Then each frame: the target moving away (0x822BFEB0, not facing) past `AWAY_BEYOND` gives 2, and with 2 last taken
/// the target no longer moving away or within `AWAY_WITHIN` gives 0 (distances 3D).
/// The looks go to the async probe pool (0x825554F8, which wakes its worker thread, 0x82849860); the check right after
/// in the same call finds them not done, so they are read the frame after (the worker answers a two-line probe well
/// inside a frame), with the places they were asked from.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SeekStage {
    /// +0xD8 the stage wanted; +0xD4 the one the seek last took (0x822C56E0 redoes the seek when they differ).
    pub stage: u32,
    pub used: u32,
    /// The look timer's start and length (ms), none while it's off.
    timer: Option<(u32, u32)>,
    /// +0xF1 0x10: the looks out (+0xE8 / +0xEC), with their answers (chest, knee) for the frame that reads them.
    out: Option<(bool, bool)>,
    /// +0xF1 0x20: this frame's update gave stage 1 from a blocked look or 2 from the target moving away
    /// ([`step`] then seeks at any distance).
    pub changed: bool,
}

impl SeekStage {
    /// As 431 starts at `now` (ms): its first subtask (vf19 0x822C6738) puts the look timer on for
    /// `SEEK_FIRST_LOOK_MS`, so the first look waits that long.
    pub fn start(now: u32) -> Self {
        SeekStage { timer: Some((now, SEEK_FIRST_LOOK_MS)), ..Default::default() }
    }

    /// The stop radius and fight distance for the stage wanted.
    pub fn stop(&self) -> (f32, f32) {
        SEEK_STAGES.get(self.stage as usize).copied().unwrap_or((0.0, 0.0))
    }

    /// One frame at `now` (ms): `look` the two lines clear (chest, knee), asked when the looks go out, `away` the
    /// target moving away, `dist2` its distance squared.
    pub fn update(&mut self, now: u32, look: impl FnOnce() -> (bool, bool), away: bool, dist2: f32) {
        let (mut blocked, mut off) = (false, false);
        if self.out.is_none() && (self.timer.is_none_or(|(t, len)| now.wrapping_sub(t) >= len) || self.used == 1) {
            self.out = Some(look());
        } else if let Some((chest, knee)) = self.out.take() {
            self.timer = Some((now, SEEK_LOOK_MS));
            if !(chest && knee) {
                blocked = true;
                self.stage = 1;
            } else if self.used == 1 {
                self.stage = 0;
            }
        }
        if away && dist2 > AWAY_BEYOND * AWAY_BEYOND {
            off = true;
            self.stage = 2;
        }
        if self.used == 2 && (!away || dist2 < AWAY_WITHIN * AWAY_WITHIN) {
            self.stage = 0;
        }
        self.changed = blocked || off;
    }
}

/// 0x822C0A90 for a ped with the Charge's flags: `ped` its place, `target` the target's (0x822BFE40), `moving` a
/// movement task runs (0x82158ED8), `waiting` its turn (+0xF0 bit 0, [`take_turns`]; the route is only for a ped
/// waiting, 0x822C0D9C), `changed` [`SeekStage::changed`] (+0xF1 0x20). With a movement box (`area`, 0x822F1750; in
/// or out as [`keep_distance`]'s, 0x822C0CE0..0x822C0D80) the route is only for a ped in it and the seek only for a
/// target in it.
/// The quit (3) is for a target that is a ped (+0x28 type 3) sitting in a vehicle (+0x23C bit 29 and +0xA80) or running
/// CTaskComplexNewGetInVehicle (734; 0x82159078 on its intelligence's +0x2E0 list, the priority limit on), its walk to
/// the door as well. On it 431's control (vf20 0x822C6B58) asks its subtask to leave urgently (0x82164FB8 with 1; the
/// result 433's vf5 0x822C1708 and the stance 432's 0x826041E8 always agree), then 0x822BE788 blends in Idle_Outro
/// (anim 0x52, blend 12) of the trans group (0x8265C680) of the ped's move-blend set (+0x9D0 +0x3C) when that is a
/// melee one (0x822BE6F8), and the melee ends. 0x82158ED8 is the task manager's first secondary tree (+0x2C..+0x34)
/// with a task not done.
#[allow(clippy::too_many_arguments)]
pub fn step(target_in_vehicle: bool, target_boarding: bool, ped: [f32; 3], target: [f32; 3], moving: bool, waiting: bool,
            changed: bool, area: Option<[[f32; 3]; 2]>) -> Step {
    if target_in_vehicle || target_boarding {
        return Step::Quit;
    }
    let inside = |p: [f32; 3]| area.is_none_or(|[lo, hi]| (0..3).all(|i| !(lo[i] > p[i]) && !(p[i] > hi[i])));
    let d = [target[0] - ped[0], target[1] - ped[1], target[2] - ped[2]];
    let d2 = d[2].mul_add(d[2], d[1].mul_add(d[1], d[0] * d[0]));
    if inside(ped) && waiting && moving && d2 < ROUTE_WITHIN * ROUTE_WITHIN {
        Step::Route
    } else if inside(target) && (d2 > SEEK_BEYOND * SEEK_BEYOND || changed) {
        Step::Seek
    } else {
        Step::Fight
    }
}

/// 0x822C6848's ped chance, all read off the code (no fit): 0x822C69B8..0x822C69D4 takes `PLAYING` when the move
/// subtask's result (+0x14) is set; else 0x822C69D8..0x822C6A9C: t = (|target - ped| - NEAR) / (FAR - NEAR), clamped
/// to 0..1 by two `fsel` against the doubles 0.0 (0x820BED10) and 1.0 (0x820BED18); powf(1 - t, POWER) (0x82A02648,
/// 1.0 from 0x82000D48); times TOP, clamped by two `fsel` to FLOOR..TOP. 0x822C6AA0..0x822C6ABC: the draw
/// 0x82158E78(0, 1) under it wants a move. The pow is double (the FPRs), rounded to single by the frsp after it:
/// `mathrt::pow`, checked bit for bit on 1 - t for 200k singles t (all in 0.99..1, then random), exponent 7, by
/// `tools/xbox360-re/checks/mathrt.py`.
/// 0x82A99CB4.
const NEAR: f32 = 0.85;
/// 0x82A99CB8.
const FAR: f32 = 3.0;
/// 0x82A99CBC.
const POWER: f32 = 7.0;
/// 0x82A99CC0.
const FLOOR: f32 = 0.01;
/// 0x82A99CC4.
const TOP: f32 = 0.75;
/// 0x82A99CC8.
const PLAYING: f32 = 0.28;
/// 0x82A99CCC / 0x82A99CD0 (ints, ms; rand 0x82143C00 in vf20 0x822C6FF8..0x822C7020): after a move starts, the next
/// one comes within this long at the latest.
const WAIT_MS: (i32, i32) = (2000, 7000);

/// The chance a frame (0x822C6848) that a ped `dist` from its target starts a move.
pub fn chance(dist: f32, playing: bool) -> f32 {
    if playing {
        return PLAYING;
    }
    let t = ((dist - NEAR) / (FAR - NEAR)).clamp(0.0, 1.0);
    (TOP * mathrt::pow((1.0 - t) as f64, POWER as f64) as f32).clamp(FLOOR, TOP)
}

/// 0x822C6848's ped part: a move is wanted when the wait since the last one ran out (`left`, None before the first), or
/// the draw `roll` (0x82158E78, 0 to 1) is under `chance`. Whether one is started is then up to the action table.
/// The game draws once a frame at its 30 fps, so a frame of `dt` s draws 1 - (1 - chance)^(30 dt): the same chance a
/// second at any frame rate.
pub fn wants_move(left: Option<f32>, dist: f32, playing: bool, roll: f32, dt: f32) -> bool {
    left.is_some_and(|l| l <= 0.0) || roll < 1.0 - (1.0 - chance(dist, playing)).powf(30.0 * dt)
}

/// The wait set when a ped's move starts (vf20: +0x44 now, +0x48 rand(2000, 7000) ms, +0x4C armed), seconds.
pub fn wait(rng: &mut Rand) -> f32 {
    rng.int_in_range(WAIT_MS.0, WAIT_MS.1) as f32 / 1000.0
}

//// CTaskSimpleMoveMeleeMovement (432, ctor 0x822C0E60, AI step 0x822C3A58 from 0x822C3BEC): the stance a fighting
/// ped keeps between moves. 431 puts it in the ped's movement slot each frame it fights (0x822C37B8) with the keep
/// distances by its waiting flag (+0xF0 bit 0, [`take_turns`]) and the ped's turn speed (+0x9F8) 11 (0x8203C9EC) x 2
/// (0x82000DC8).
pub const TURN_SPEED: f32 = 22.0;

/// 0x822C1288's distances: the one it keeps, the dead band about it and the far end the speed is held at; the powers
/// too far and too close, and the least speed.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Keep {
    pub keep: f32,
    pub dead: f32,
    pub far: f32,
    pub far_pow: f32,
    pub close_pow: f32,
    pub least: f32,
}

/// Waiting its turn (+0xF0 bit 0 set): 432's +0x24, +0x28, +0x2C from 0x82A99D38, D3C, D40; the powers and least
/// speed 0x822C3A58 passes (0x82A99CE4, CE8, CEC).
pub const KEEP_WAITING: Keep = Keep { keep: 2.5, dead: 0.2, far: 3.0, far_pow: 2.0, close_pow: 1.0, least: 0.1 };
/// Its turn (+0xF0 bit 0 clear): 0x82A99CD8, CDC, CE0.
pub const KEEP_FIGHTING: Keep = Keep { keep: 1.0, dead: 0.2, far: 1.5, ..KEEP_WAITING };
/// Off the second ped (432's +0x34), backing off only: 0x82A99CF0 .. 0x82A99D04 (0x822C3D14).
pub const KEEP_OFF: Keep = Keep { keep: 1.6, dead: 0.2, far: 3.0, far_pow: 2.0, close_pow: 1.0, least: 0.1 };
/// The sideways step round the target when the second ped is nearer it (0x82A99D08).
const CIRCLE: f32 = 0.25;
/// The move ratio a full stance step asks (0x82A99D60: x2, a run).
const STANCE_RATIO: f32 = 2.0;

/// 0x822C1288: the move along the way to `target` (a root bone, 0x822BFE40) that keeps the ped `k.keep` from it: the
/// gap e = keep - |target - ped| (3D), held to `far`, does nothing under `dead`, else ((|e| - dead) / (far - dead)) to
/// `far_pow` (too far) or `close_pow` (too close), toward it when too far and away when too close, at least `least`.
/// `no_closer` (the caller's byte +0x57) never steps toward it. Given on the ped's `right` and `forward` (matrix +0x00,
/// +0x10): (sideways, forward).
/// The ped's movement box (`area`: SET_CHAR_MELEE_MOVEMENT_CONSTAINT_BOX's corners, 0x822F1750; 0x822C1424) holds that
/// speed: with its target out of the box the ped may not step toward it, out of the box itself not away, both out not
/// at all. In or out is on x, y and z (the mask 0x831AB6F0): not under the first corner nor over the second.
/// The way is made unit the vector unit's way ([`crate::melee::vunit`]).
#[allow(clippy::too_many_arguments)]
pub fn keep_distance(k: &Keep, no_closer: bool, ped: [f32; 3], right: [f32; 3], forward: [f32; 3], target: [f32; 3],
                     area: Option<[[f32; 3]; 2]>) -> (f32, f32) {
    let d = [target[0] - ped[0], target[1] - ped[1], target[2] - ped[2]];
    let len = d[0].mul_add(d[0], d[1].mul_add(d[1], d[2] * d[2])).sqrt();
    let e = k.keep - len;
    let a = e.abs().min(k.far);
    let speed = if a < k.dead {
        0.0
    } else {
        // 0x82A02648 in double, rounded to single.
        let p = mathrt::pow(((a - k.dead) / (k.far - k.dead)) as f64, if e < 0.0 { k.far_pow } else { k.close_pow } as f64) as f32;
        let s = -(e.signum() * p);
        // 0x822C13FC's fsel pair: a zero stays zero.
        if s == 0.0 { 0.0 } else if s.abs() < k.least { s.signum() * k.least } else { s }
    };
    let speed = if no_closer && speed > 0.0 { 0.0 } else { speed };
    let speed = match area {
        Some([lo, hi]) => {
            let inside = |p: [f32; 3]| (0..3).all(|i| !(lo[i] > p[i]) && !(p[i] > hi[i]));
            let (toward, away) = (inside(target), inside(ped));
            if (!toward && speed > 0.0) || (!away && speed < 0.0) { 0.0 } else { speed }
        }
        None => speed,
    };
    let way = crate::melee::vunit(d);
    let dot = |r: [f32; 3]| r[0].mul_add(way[0], r[1].mul_add(way[1], r[2] * way[2]));
    (dot(right) * speed, dot(forward) * speed)
}

/// 0x822C3A58's AI step: `keep_distance` from the target (by `waiting`, [`KEEP_WAITING`] or [`KEEP_FIGHTING`]) plus,
/// with a second ped (432's +0x34, [`take_turns`]), the step off it ([`KEEP_OFF`], backing off only); and when that ped
/// is nearer the target than this one, a sideways step round it: - `CIRCLE` x (its way to the target . ours) x the sign
/// of (ours x its).z. Each held to -1..1, then to length 1, times `STANCE_RATIO`: the move blend's sideways and forward
/// ratios (+4, +8 by 0x82651600's +0xC, +0x10; set as they are, no easing, by 0x822BEF58).
/// The knock drift ([`Drift`], 0x822BEF58) is the caller's, over these ratios.
#[allow(clippy::too_many_arguments)]
pub fn stance(ped: [f32; 3], right: [f32; 3], forward: [f32; 3], target: [f32; 3], area: Option<[[f32; 3]; 2]>,
              waiting: bool, second: Option<[f32; 3]>) -> (f32, f32) {
    let (mut s, mut f) = keep_distance(if waiting { &KEEP_WAITING } else { &KEEP_FIGHTING }, false, ped, right, forward, target, area);
    if let Some(o) = second {
        let (mut s2, f2) = keep_distance(&KEEP_OFF, true, ped, right, forward, o, area);
        let sub = |a: [f32; 3], b: [f32; 3]| [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
        let len2 = |v: [f32; 3]| v[2].mul_add(v[2], v[1].mul_add(v[1], v[0] * v[0]));
        let (theirs, ours) = (sub(target, o), sub(target, ped));
        if len2(theirs) < len2(ours) {
            let (a, b) = (crate::melee::vunit(theirs), crate::melee::vunit(ours));
            let dot = a[2].mul_add(b[2], a[0].mul_add(b[0], a[1] * b[1]));
            let z = (-a[0]).mul_add(b[1], a[1] * b[0]);
            let sign = if z > 0.0 { 1.0 } else if z == 0.0 { 0.0 } else { -1.0 };
            s2 -= dot * sign * CIRCLE;
        }
        (s, f) = (s + s2, f + f2);
    }
    let (mut s, mut f) = (s.clamp(-1.0, 1.0), f.clamp(-1.0, 1.0));
    let l2 = f.mul_add(f, s * s);
    if l2 > 1.0 {
        let k = 1.0 / l2.sqrt();
        (s, f) = (s * k, f * k);
    }
    if f.mul_add(f, s * s) > 0.0 {
        (s, f) = (s * STANCE_RATIO, f * STANCE_RATIO);
    }
    (s, f)
}

/// Another ped fighting the same target, as 0x822C0490 reads it: its distance squared to the target, its own 431's
/// waiting flag (+0xF0 bit 0) and whether it is a gang's (0x822F2660).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rival {
    pub d2: f32,
    pub waiting: bool,
    pub gang: bool,
}

/// 0x822C0490's answer: this ped's waiting flag, the second ped (the rival nearest the target: 432's +0x34 by 0x822C37B8
/// when there is one) and the rival told to wait in its place.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Turns {
    pub waiting: bool,
    pub second: Option<usize>,
    pub relieve: Option<usize>,
}

/// 0x822C0490 (431's control vf20 runs it each frame, 0x822C6BD4): who takes the fight to a target in turn. `rivals`
/// are the peds near this one (its intelligence's 16, +0x168) with a 431 (0x823C0E88) at the same target (+0x50).
/// None: it fights (clears its +0xF0 bit 0). Fewer than its limit fighting (waiting clear; 2 for a gang's ped, else 1):
/// it fights too. Else nearer the target than the nearest of them (squared, from the matrices' +0x30), neither a
/// gang's: it fights and the farthest is told to wait (its bit 0 set). Else its flag stays (the Charge's 431 starts
/// waiting, +0xF0 = 0x13). The nearest is the second ped whenever there are rivals.
pub fn take_turns(waiting: bool, gang: bool, d2: f32, rivals: &[Rival]) -> Turns {
    if rivals.is_empty() {
        return Turns { waiting: false, second: None, relieve: None };
    }
    let (mut near, mut near_d2, mut far, mut far_d2) = (None, f32::MAX, None, 0.0);
    for (i, r) in rivals.iter().enumerate() {
        if r.d2 < near_d2 {
            (near, near_d2) = (Some(i), r.d2);
        }
        if r.d2 > far_d2 {
            (far, far_d2) = (Some(i), r.d2);
        }
    }
    let fighting = rivals.iter().filter(|r| !r.waiting).count();
    let limit = if gang { 2 } else { 1 };
    let mut t = Turns { waiting, second: near, relieve: None };
    if fighting < limit {
        t.waiting = false;
    } else if d2 < near_d2 && !gang && !near.is_some_and(|i| rivals[i].gang) {
        (t.waiting, t.relieve) = (false, far);
    }
    t
}

/// A hit on a ped whose melee (431) runs, not NM's (the damage dispatcher 0x8226C9A8 at 0x8226D5C8), is no new 431
/// but a request on it (0x822BE930): +0xCC the attacker, +0xD0 its result's name, +0xF1 0x40 (asked) and 0x80 (a blow
/// received, always from there), +0x20 this way. Its next-action step (0x822C6848) then picks the HitReaction against
/// the attacker (0x822C5B88: Type 0x830B70C8 "HitReaction", AvailableFromIdle, receiving the hit, its own result's
/// block and struck bytes, locked on when the attacker is its result's target; [`request_takes`]); vf20 clears it after
/// (0x822C6EB8). The way (0x8226D5D8): from where the blow struck (the dispatcher's r8 +0x10) to its start (r7),
/// normalized (vrsqrtefp + one Newton step).
pub fn request_way(start: [f32; 3], hit: [f32; 3]) -> [f32; 3] {
    crate::melee::vunit([start[0] - hit[0], start[1] - hit[1], start[2] - hit[2]])
}

/// 0x822C6848: with a reaction the dispatcher gave playing (433's +0x5C 0x02), the requested result goes in only at
/// that one's Priority or above (its def +4 via 0x8218FB90 against 433's +0x30, unsigned: 0x822C68E8).
pub fn request_takes(playing: Option<i32>, priority: i32) -> bool {
    playing.is_none_or(|p| priority as u32 >= p as u32)
}

/// The knock drift (432's +0x50 way, +0x60 the momentum, +0x64 seconds left): a blocking ped pushed along by the
/// blow it takes.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Drift {
    pub way: [f32; 2],
    pub momentum: f32,
    pub left: f32,
}

/// Its way's least length (0x82019678).
const DRIFT_LEAST: f32 = 0.1;
/// The drift ratio per unit of momentum (0x82A99D60) and the countdown's rate (0x82A99DD0, a second a second).
const DRIFT_RATIO: f32 = 2.0;
const DRIFT_FALL: f32 = 1.0;

impl Drift {
    /// 0x822C37B8 as 431 puts its stance in, the request still pending: a blow `received` (+0xF1 0x80) whose `way`
    /// (+0x20) is longer than `DRIFT_LEAST`, while the ped's own result is a block (433's +0x5C 0x20, the caller's r6)
    /// starts the drift: the way, and the blow's result's AppliedPedMoveMomentum (+0x84, found by +0xD0's name,
    /// 0x8265C730) as both the momentum and the seconds left.
    pub fn start(way: [f32; 3], received: bool, blocking: bool, momentum: f32) -> Option<Drift> {
        let len = way[2].mul_add(way[2], way[0].mul_add(way[0], way[1] * way[1])).sqrt();
        (received && blocking && len > DRIFT_LEAST).then_some(Drift { way: [way[0], way[1]], momentum, left: momentum })
    }

    /// 0x822BEF58 (after the stance step and its player's, 0x822C3FEC / 0x822BF2C0): while time is left, the move
    /// blend's ratios become sideways (right . w) x 2m and forward -(forward . w) x 2m, w the way on the flat (x, y,
    /// 0) normalized (vrsqrtefp + one Newton step); a w of no length gives 0, 0. None: the stance's stand.
    pub fn ratios(&self, right: [f32; 3], forward: [f32; 3]) -> Option<(f32, f32)> {
        if !(self.left > 0.0) {
            return None;
        }
        let w = crate::melee::vunit([self.way[0], self.way[1], 0.0]);
        if !(w[0].mul_add(w[0], w[1].mul_add(w[1], w[2] * w[2])).sqrt() > 0.0) {
            return Some((0.0, 0.0));
        }
        let k = self.momentum * DRIFT_RATIO;
        // + w.z x 0: a -0 dot comes out +0.
        let dot = |r: [f32; 3]| r[1].mul_add(w[1], r[0] * w[0]) + w[2] * 0.0;
        Some((dot(right) * k, -dot(forward) * k))
    }

    /// The end of 432's AI step (0x822C4074, after the drift): the time left runs down by the frame's step
    /// (0x82C6C2AC) to 0. (A drift running also keeps the step's body on, +0x68 0x80 at 0x822C3AD8.)
    pub fn step(&mut self, dt: f32) {
        self.left = (self.left - dt * DRIFT_FALL).max(0.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_stance() {
        let (r, f) = ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
        // Its turn: within the dead band (0.8 to 1.2 m) it stands; far it runs in, the speed held to 1 at 1.5 m out.
        assert_eq!(stance([0.0; 3], r, f, [0.0, 1.0, 0.0], None, false, None), (0.0, 0.0));
        assert_eq!(stance([0.0; 3], r, f, [0.0, 3.0, 0.0], None, false, None), (0.0, 1.9999999)); // the estimate + Newton step leaves the way 1 ulp short
        // Waiting: it stands 2.3 to 2.7 m off and is held to 1 at 5.5 m.
        assert_eq!(stance([0.0; 3], r, f, [0.0, 2.5, 0.0], None, true, None), (0.0, 0.0));
        assert_eq!(stance([0.0; 3], r, f, [0.0, 1.0, 0.0], None, true, None).1 < 0.0, true);
        assert_eq!(stance([0.0; 3], r, f, [0.0, 6.0, 0.0], None, true, None), (0.0, 1.9999999));
        // Just past the band it walks in at the least speed.
        assert_eq!(keep_distance(&KEEP_FIGHTING, false, [0.0; 3], r, f, [0.0, 1.22, 0.0], None).1, KEEP_FIGHTING.least);
        // Too close, it backs off, on its right when the target is on its left.
        let (s, fw) = keep_distance(&KEEP_FIGHTING, false, [0.0; 3], r, f, [-0.05, 0.0, 0.0], None);
        assert!(s > 0.0 && fw == 0.0);
        // Backing off only: never in, but away.
        assert_eq!(keep_distance(&KEEP_OFF, true, [0.0; 3], r, f, [0.0, 6.0, 0.0], None), (0.0, 0.0));
        assert!(keep_distance(&KEEP_OFF, true, [0.0; 3], r, f, [0.0, 0.5, 0.0], None).1 < 0.0);
    }

    #[test]
    fn the_second_ped() {
        let (r, f) = ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
        let near = |a: (f32, f32), b: (f32, f32)| (a.0 - b.0).abs() < 1e-5 && (a.1 - b.1).abs() < 1e-5;
        // 1 m on its right, farther from the target than it: it only steps off, (1.6 - 1 - 0.2) / 2.8 to the left.
        let off = 0.4 / 2.8;
        assert!(near(stance([0.0; 3], r, f, [0.0, 1.0, 0.0], None, false, Some([1.0, 0.0, 0.0])), (-off * 2.0, 0.0)));
        // Ahead on its right and nearer the target: it goes on in and steps round to the left, 0.25 x cos 45; on its
        // left, round to the right.
        let fwd = (0.8f32 / 1.3).powi(2);
        let round = 0.25 * std::f32::consts::FRAC_1_SQRT_2;
        assert!(near(stance([0.0; 3], r, f, [0.0, 2.0, 0.0], None, false, Some([0.5, 1.5, 0.0])), (-round * 2.0, fwd * 2.0)));
        assert!(near(stance([0.0; 3], r, f, [0.0, 2.0, 0.0], None, false, Some([-0.5, 1.5, 0.0])), (round * 2.0, fwd * 2.0)));
        // Straight in line with it: no side to go round by.
        assert!(near(stance([0.0; 3], r, f, [0.0, 4.0, 0.0], None, false, Some([0.0, 3.0, 0.0])), (0.0, 2.0)));
    }

    #[test]
    fn the_turns() {
        let fighting = |d2| Rival { d2, ..Default::default() };
        let waiting = |d2| Rival { d2, waiting: true, ..Default::default() };
        // Alone at the target: it fights.
        assert_eq!(take_turns(true, false, 4.0, &[]), Turns { waiting: false, second: None, relieve: None });
        // One already fighting, it farther off: it waits, kept off the nearest.
        assert_eq!(take_turns(true, false, 4.0, &[fighting(1.0)]), Turns { waiting: true, second: Some(0), relieve: None });
        // A gang's ped goes two at a time.
        assert_eq!(take_turns(true, true, 4.0, &[fighting(1.0)]).waiting, false);
        assert_eq!(take_turns(true, true, 4.0, &[fighting(1.0), fighting(2.0)]).waiting, true);
        // Only waiting ones: it fights.
        assert_eq!(take_turns(true, false, 4.0, &[waiting(1.0)]).waiting, false);
        // Nearer than all of them: it takes the turn and the farthest waits.
        let rivals = [fighting(1.0), waiting(9.0), fighting(3.0)];
        assert_eq!(take_turns(true, false, 0.5, &rivals), Turns { waiting: false, second: Some(0), relieve: Some(1) });
        // Not with a gang's ped nearest.
        assert_eq!(take_turns(true, false, 0.5, &[Rival { gang: true, ..fighting(1.0) }]).relieve, None);
        // A flag already clear stays clear.
        assert_eq!(take_turns(false, false, 4.0, &[fighting(1.0)]).waiting, false);
    }

    #[test]
    fn the_knock_drift() {
        let (r, f) = ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
        // Only a received blow, while blocking, along a way past 0.1.
        assert_eq!(Drift::start([0.0, 1.0, 0.0], false, true, 0.5), None);
        assert_eq!(Drift::start([0.0, 1.0, 0.0], true, false, 0.5), None);
        assert_eq!(Drift::start([0.0, 0.05, 0.0], true, true, 0.5), None);
        // The blow's start ahead (way +y): back at 2 x 0.5 for 0.5 s (the estimate's unit an ulp short); on the right
        // (+x): to the right.
        let mut d = Drift::start([0.0, 1.0, 0.0], true, true, 0.5).unwrap();
        assert_eq!(d.ratios(r, f), Some((0.0, -0.99999994)));
        assert_eq!(Drift::start([2.0, 0.0, 0.5], true, true, 0.5).unwrap().ratios(r, f), Some((0.99999994, 0.0)));
        d.step(0.3);
        assert!(d.ratios(r, f).is_some());
        d.step(0.3);
        assert_eq!((d.left, d.ratios(r, f)), (0.0, None));
        // Straight up: no flat way, it stands.
        assert_eq!(Drift::start([0.0, 0.0, 1.0], true, true, 0.5).unwrap().ratios(r, f), Some((0.0, 0.0)));
    }

    #[test]
    fn the_request() {
        assert_eq!(request_way([0.0, 3.0, 0.0], [0.0, 1.0, 0.0]), [0.0, 0.99999994, 0.0]);
        // A dispatcher's reaction playing lets in one of its Priority or above, unsigned.
        assert!(request_takes(None, 0));
        assert!(request_takes(Some(2), 2) && !request_takes(Some(2), 1));
        assert!(request_takes(Some(2), -1));
    }


    #[test]
    fn the_movement_box() {
        let (r, f) = ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
        let kd = |p: [f32; 3], t: [f32; 3], a| keep_distance(&KEEP_WAITING, false, p, r, f, t, a);
        let area = Some([[-3.0, -3.0, -1.0], [3.0, 3.0, 2.0]]);
        // (Under 2.3 m off the target it backs off: keep - dead.)
        let near = [0.0, 0.05, 0.0];
        // Both in: as without it.
        assert_eq!(kd([0.0; 3], [0.0, 2.9, 0.0], area), kd([0.0; 3], [0.0, 2.9, 0.0], None));
        // Its target out: it does not step in, but backs off.
        assert_eq!(kd([0.0; 3], [0.0, 4.0, 0.0], area), (0.0, 0.0));
        assert!(kd([0.0, 3.0, 0.0], [0.0, 3.05, 0.0], area).1 < 0.0);
        // Itself out: it steps in, but does not back off.
        assert!(kd([0.0, -4.0, 0.0], [0.0, 0.0, 0.0], area).1 > 0.0);
        assert!(kd([0.0, -3.05, 0.0], [0.0, -2.98, 0.0], None).1 < 0.0);
        assert_eq!(kd([0.0, -3.05, 0.0], [0.0, -2.98, 0.0], area), (0.0, 0.0));
        // Both out: not even backing off. z counts: a target over the box's top is out.
        assert_eq!(kd([5.0, 0.0, 0.0], [5.0, 0.05, 0.0], area), (0.0, 0.0));
        assert_eq!(kd([0.0; 3], [0.0, 3.0, 2.1], area), (0.0, 0.0));
        // Swapped corners: nothing is in, so it never moves.
        assert!(kd([0.0; 3], near, None).1 < 0.0);
        assert_eq!(kd([0.0; 3], near, Some([[3.0; 3], [-3.0; 3]])), (0.0, 0.0));
    }

    #[test]
    fn the_seek_stages() {
        let mut s = SeekStage::default();
        assert_eq!(s.stop(), (3.3, 3.5));
        // A blocked look, read the frame after: right up to it; in stage 1 it looks again at once and, both clear,
        // goes back to 0.
        s.update(0, || (true, false), false, 25.0);
        assert_eq!(s.stage, 0, "not read in the frame it goes out");
        s.update(33, || unreachable!(), false, 25.0);
        s.used = s.stage;
        assert_eq!(s.stop(), (0.15, 0.35));
        s.update(66, || (true, true), false, 25.0);
        s.update(100, || unreachable!(), false, 25.0);
        assert_eq!(s.stage, 0);
        s.used = s.stage;
        // In stage 0 the next look waits 2 s from the frame the last was read.
        s.update(2099, || unreachable!(), false, 25.0);
        assert_eq!(s.stage, 0);
        s.update(2100, || (false, false), false, 25.0);
        s.update(2133, || unreachable!(), false, 25.0);
        assert_eq!(s.stage, 1);
        // Running off past 2.5 m: 0.9 m; then within 1.3 m, or no longer running off: 0.
        let mut s = SeekStage::default();
        s.update(0, || (true, true), true, 9.0);
        s.used = s.stage;
        assert_eq!(s.stop(), (0.9, 1.3));
        s.update(50, || (true, true), true, 1.0);
        assert_eq!(s.stage, 0);
        assert_eq!(SeekStage { stage: 3, ..s }.stop(), (0.0, 0.0));
        // Started (vf19): no look for the first 0.5 s, then every 2 s.
        let mut s = SeekStage::start(1000);
        s.update(1499, || unreachable!(), false, 25.0);
        s.update(1500, || (false, false), false, 25.0);
        s.update(1533, || unreachable!(), false, 25.0);
        assert_eq!(s.stage, 1);
    }

    #[test]
    fn the_melee_steps() {
        let at = |d: f32| [d, 0.0, 0.0];
        assert_eq!(step(true, false, [0.0; 3], at(1.0), true, true, false, None), Step::Quit);
        assert_eq!(step(false, true, [0.0; 3], at(9.0), true, true, false, None), Step::Quit);
        assert_eq!(step(false, false, [0.0; 3], at(1.0), true, true, false, None), Step::Route);
        assert_eq!(step(false, false, [0.0; 3], at(1.0), true, false, false, None), Step::Fight);
        assert_eq!(step(false, false, [0.0; 3], at(1.0), false, true, false, None), Step::Fight);
        assert_eq!(step(false, false, [0.0; 3], at(5.5), true, true, false, None), Step::Fight);
        assert_eq!(step(false, false, [0.0; 3], at(5.6), true, true, false, None), Step::Seek);
        // A stage just changed (a blocked look, the target running off) seeks at any distance.
        assert_eq!(step(false, false, [0.0; 3], at(2.0), false, false, true, None), Step::Seek);
        // The movement box: the route only with the ped in it, the seek only with the target in it.
        let bx = Some([[-1.0, -1.0, -1.0], [3.0, 1.0, 1.0]]);
        assert_eq!(step(false, false, [0.0; 3], at(1.0), true, true, false, bx), Step::Route);
        assert_eq!(step(false, false, at(-2.0), at(1.0), true, true, false, bx), Step::Fight);
        assert_eq!(step(false, false, [0.0; 3], at(9.0), true, true, false, bx), Step::Fight);
        assert_eq!(step(false, false, at(-9.0), at(2.0), false, false, false, bx), Step::Seek);
    }

    #[test]
    fn the_attack_chance() {
        // Within 0.85 m the top, past 3 m the floor, between the power curve; 0.28 while a move plays.
        assert_eq!(chance(0.5, false), 0.75);
        assert_eq!(chance(4.0, false), 0.01);
        assert!((chance(1.3875, false) - 0.75 * 0.75f32.powi(7)).abs() < 1e-6);
        assert_eq!(chance(0.5, true), 0.28);
        assert!(wants_move(Some(0.0), 9.0, false, 0.99, 1.0 / 30.0) && !wants_move(Some(0.1), 9.0, false, 0.5, 1.0 / 30.0) && !wants_move(None, 9.0, false, 0.5, 1.0 / 30.0));
        // Two 60 fps frames together draw the 30 fps frame's chance.
        let c = |dt: f32| 1.0 - (1.0 - chance(9.0, false)).powf(30.0 * dt);
        assert!(((1.0 - c(1.0 / 60.0)).powi(2) - (1.0 - c(1.0 / 30.0))).abs() < 1e-6 && (c(1.0 / 30.0) - FLOOR).abs() < 1e-6);
        let mut r = Rand::default();
        assert!((0..100).map(|_| wait(&mut r)).all(|w| (2.0..7.0).contains(&w)));
    }
}
