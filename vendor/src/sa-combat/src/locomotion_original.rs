// Generated from LSC4 380c8cb7d49a9b0c941d0ce8ffb58a0963e4c21e player.rs. See movement-source.json.
pub const CROUCH: [&str; 3] = ["move_crouch/idle", "move_crouch/walk", "move_crouch/run"];

pub const CROUCH_GRID: [[u32; 7]; 2] = [[0x20, 0x20, 0x20, 0x1C, 0x21, 0x21, 0x21], [0x2E, 0x2E, 0x2C, 0x28, 0x2D, 0x2F, 0x2F]];

pub const CROUCH_TURNS: [(u32, &str); 6] = [(0x20, "move_crouch/walk_turn_l"), (0x21, "move_crouch/walk_turn_r"),
    (0x2C, "move_crouch/run_turn_l"), (0x2D, "move_crouch/run_turn_r"), (0x2E, "move_crouch/run_turn_l2"), (0x2F, "move_crouch/run_turn_r2")];

pub const STOPS: [(u32, &str); 6] = [(movestop::WSTOP_L, "move_player/wstop_l"), (movestop::WSTOP_R, "move_player/wstop_r"),
    (movestop::RSTOP_L, "move_player/rstop_l"), (movestop::RSTOP_R, "move_player/rstop_r"), (movestop::SSTOP_L, "move_player/sstop_l"),
    (movestop::SSTOP_R, "move_player/sstop_r")];

pub const CROUCH_STOPS: [(u32, &str); 4] = [(movestop::WSTOP_L, "move_crouch/wstop_l"), (movestop::WSTOP_R, "move_crouch/wstop_r"),
    (movestop::RSTOP_L, "move_crouch/rstop_l"), (movestop::RSTOP_R, "move_crouch/rstop_r")];

pub const STARTS: [(u32, &str); 10] = [(movestart::WSTART, "move_player/wstart"),
    (movestart::WSTART_TURN_L90, "move_player/wstart_turn_l90"), (movestart::WSTART_TURN_R90, "move_player/wstart_turn_r90"),
    (movestart::WSTART_TURN_L180, "move_player/wstart_turn_l180"), (movestart::WSTART_TURN_R180, "move_player/wstart_turn_r180"),
    (movestart::RUNSTART_FWD, "move_player/runstart_fwd"), (movestart::RUNSTART_L_90, "move_player/runstart_l_90"),
    (movestart::RUNSTART_R_90, "move_player/runstart_r_90"), (movestart::RUNSTART_L_180, "move_player/runstart_l_180"),
    (movestart::RUNSTART_R_180, "move_player/runstart_r_180")];

pub const TURNS: [(u32, &str); 6] = [(moveturn::WALK_TURN_180_L, "move_player/walk_turn_180_l"),
    (moveturn::WALK_TURN_180_R, "move_player/walk_turn_180_r"), (moveturn::RUN_TURN_180_L, "move_player/run_turn_180_l"),
    (moveturn::RUN_TURN_180_R, "move_player/run_turn_180_r"), (moveturn::SPRINT_TURN_180_L, "move_player/sprint_turn_180_l"),
    (moveturn::SPRINT_TURN_180_R, "move_player/sprint_turn_180_r")];

pub const SPOTS: [(u32, &str); 2] = [(moveturn::TURN_360_L, "move_player/turn_360_l"), (moveturn::TURN_360_R, "move_player/turn_360_r")];

pub const CROUCH_STARTS: [(u32, &str); 1] = [(movestart::WSTART, "move_crouch/wstart")];

pub const CROUCH_SPOTS: [(u32, &str); 2] = [(moveturn::TURN_360_L, "move_crouch/turn_360_l"), (moveturn::TURN_360_R, "move_crouch/turn_360_r")];

pub const SWIM: [&str; 4] = ["swimming/idle", "swimming/walk", "swimming/run", "swimming/sprint"];

pub const SWIM_STOPS: [(u32, &str); 4] = [(movestop::WSTOP_L, "swimming/wstop_l"), (movestop::WSTOP_R, "swimming/wstop_r"),
    (movestop::RSTOP_L, "swimming/rstop_l"), (movestop::RSTOP_R, "swimming/rstop_r")];

pub const SWIM_STARTS: [(u32, &str); 2] = [(movestart::WSTART, "swimming/wstart"), (movestart::RUNSTART_FWD, "swimming/runstart_fwd")];

pub const SWIM_TURNS: [(u32, &str); 6] = [(0x20, "swimming/walk_turn_l"), (0x21, "swimming/walk_turn_r"), (0x2C, "swimming/run_turn_l"),
    (0x2D, "swimming/run_turn_r"), (0x33, "swimming/sprint_turn_l"), (0x34, "swimming/sprint_turn_r")];

pub const TURN_CLIPS: [(u32, &str); 12] = [(0x20, "move_player/walk_turn_l"), (0x21, "move_player/walk_turn_r"),
    (0x22, "move_player/walk_turn_l2"), (0x23, "move_player/walk_turn_r2"), (0x24, "move_player/walk_turn_l3"),
    (0x25, "move_player/walk_turn_r3"), (0x2C, "move_player/run_turn_l"), (0x2D, "move_player/run_turn_r"),
    (0x2E, "move_player/run_turn_l2"), (0x2F, "move_player/run_turn_r2"), (0x33, "move_player/sprint_turn_l"),
    (0x34, "move_player/sprint_turn_r")];

pub const SLOPES: [(u32, &str); 4] = [(0x4C, "move_player/walk_up"), (0x4D, "move_player/walk_down"), (0x4E, "move_player/run_up"), (0x4F, "move_player/run_down")];

pub const ON_FOOT:usize=0;pub const CROUCHED:usize=1;pub const SWIMMING:usize=3;pub const TURN_SPEED:f32=11.;

#[derive(Clone,Copy,Debug,PartialEq)]
pub enum Gait {
    Idle,
    Walk,
    Run,
    Sprint,
}

impl Gait {
    /// The move state (+0xABC) of a blend ratio (0x82411B50): a sprint at 3, a run from 1.5, a walk above 0.
    pub fn of(ratio: f32) -> Gait {
        if ratio >= 3.0 {
            Gait::Sprint
        } else if ratio >= 1.5 {
            Gait::Run
        } else if ratio > 0.0 {
            Gait::Walk
        } else {
            Gait::Idle
        }
    }
}

const GAITS:[Gait;4]=[Gait::Idle,Gait::Walk,Gait::Run,Gait::Sprint];

pub static SET_FLAGS:std::sync::OnceLock<[u32;4]>=std::sync::OnceLock::new();

#[derive(Clone,Debug)]
pub struct Stop {
    pub key: &'static str,
    pub id: u32,
    pub phase: f32,
    pub weight: f32,
    pub rate: f32,
    pub from: (f32, f32),
}

#[derive(Clone,Debug)]
pub struct Start {
    pub key: &'static str,
    pub id: u32,
    pub phase: f32,
    pub weight: f32,
    pub rate: f32,
    pub spin: f32,
    pub turn: f32,
    pub goal0: f32,
}

#[derive(Clone,Debug)]
pub struct Turn {
    pub key: &'static str,
    pub id: u32,
    pub phase: f32,
    pub weight: f32,
    pub rate: f32,
    pub free: Option<f32>,
    /// Asked for this frame (main.rs): the clip enters at phase and weight 0 and steps from next frame.
    pub fresh: bool,
    /// The gait ratio it set off from: that gait clip plays on under it as it blends in (`animate`'s mover blend).
    pub from: f32,
}

#[derive(Clone,Debug)]
pub struct Spot {
    pub key: &'static str,
    pub id: u32,
    pub phase: f32,
    pub weight: f32,
    pub rate: f32,
    /// Its playback rate (+0x54).
    pub speed: f32,
}

impl Start {
    /// Whether `spin` turns him this frame (0x82414638's tail, 0x82414978): a turning start (B +0x50 bit 0x400) with
    /// the stick's heading on the same side as at the pick (> 0, 0, < 0) and within 32 degrees of it (0x820189F8).
    /// Else the easing's gap turns him (main.rs). ponytail: a straight start after a turning one (0x400 left set) is
    /// taken as straight.
    pub fn fixed(&self, goal: f32) -> bool {
        let g = lsc4_combat::actiontable::wrap(goal);
        self.turn != 0.0 && g.signum() == self.goal0.signum() && (g == 0.0) == (self.goal0 == 0.0)
            && lsc4_combat::actiontable::wrap(self.goal0 - g).abs() < 0.558_505_4
    }
}

impl Turn {
    /// Whether it holds his heading (main.rs does not turn him).
    pub fn holds(&self) -> bool {
        self.rate > 0.0 && !moveturn::turn_free(self.free, self.phase)
    }
}

#[derive(Clone,Copy,Debug)]
pub struct Stamina {
    pub left: f32,
    pub breathless: bool,
    /// The run button's pressure (player info +0x3A8).
    pub pressure: f32,
}

impl Default for Stamina {
    fn default() -> Self {
        Stamina { left: 600.0, breathless: false, pressure: 0.0 }
    }
}

impl Stamina {
    /// The run button (0x823D0F30, on foot, single player's tapping) for one frame of `dt` s: the run value, 0 walk, up
    /// to 1 run, above 1 sprint. Its tuning is 0x82AA1D88's first row.
    /// - Out of breath clears once stamina is above 0.
    /// - The pressure: a tap adds 4, up to 10; held, it falls by 0.7 a frame-step (dt x 50) but not below 1; released, by
    ///   0.2 down to 0. The value is pressure / 5.
    /// - Above 1, not out of breath, he sprints: stamina is spent at 0.5 through 0x823D0DA0 (dt x rate x 50, down to
    ///   -150) and he gets 1 + (value - 1) x 0.3. At -150 it is refused: he is out of breath, and that frame still gets
    ///   the value (0x823D1398).
    /// - Otherwise held is a run (1), and the pressure is set to 4.99 while stamina is above 0, so a tap sprints;
    ///   released, the value is held to 1.
    /// ponytail: the free sprint (network game, SET_PLAYER_NEVER_GETS_TIRED +0x53A, +0x3C0 bit 0x40000000,
    /// 0x82FD1EB4), the no-sprint flag (+0x404) and a carried object's are not ported; add with those scripts.
    pub fn run(&mut self, tapped: bool, held: bool, dt: f32) -> f32 {
        if self.breathless && self.left > 0.0 {
            self.breathless = false;
        }
        let p = self.pressure;
        self.pressure = if tapped {
            (p + 4.0).min(10.0)
        } else if held {
            (-(0.7 * dt)).mul_add(50.0, p).max(1.0)
        } else if p > 0.0 {
            (-(0.2 * dt)).mul_add(50.0, p).max(0.0)
        } else {
            p
        };
        let s = if self.pressure > 0.0 { self.pressure / 5.0 } else { 0.0 };
        if s > 1.0 && !self.breathless {
            if self.left <= -150.0 {
                self.breathless = true;
                return s;
            }
            // fsel: the most of the new value and -150.
            let l = self.left - dt * 0.5 * 50.0;
            self.left = if -150.0 - l >= 0.0 { -150.0 } else { l };
            return (s - 1.0).mul_add(0.3, 1.0);
        }
        if held {
            if self.left > 0.0 {
                self.pressure = 5.0 - 0.01;
            }
            1.0
        } else {
            s.min(1.0)
        }
    }

    /// The player's process (`CPlayerPed` vf28, 0x823D1CE0) refills by move state (+0xABC) through 0x823D0DA0: 1 still
    /// or walking, 0.3 running, none sprinting; dt x rate x 25 a frame while below 600.
    pub fn recover(&mut self, gait: Gait, dt: f32) {
        let rate = match gait {
            Gait::Sprint => return,
            Gait::Run => 0.3,
            _ => 1.0,
        };
        self.refill(rate, dt);
    }

    /// In a vehicle: the seat (CTaskSimpleCarSetPedInVehicle 0x824BCBC0, 0x822F57C8(ped, 0) at 0x824BCCEC) leaves move
    /// state 0, which 0x823D1CE0 refills at 1 when he is in it (+0x23C bit 0x20000000 and +0xA80, 0x823D21EC..0x823D2218).
    pub fn recover_in_vehicle(&mut self, dt: f32) {
        self.refill(1.0, dt);
    }

    /// 0x823D0DA0 with a rate above 0.
    fn refill(&mut self, rate: f32, dt: f32) {
        if self.left < 600.0 {
            self.left = (dt * rate).mul_add(25.0, self.left);
        }
    }
}
impl Clips<'_> {
    pub fn speed_at(&self, ratio: f32, set: usize, slope: i16, col: f32) -> f32 {
        self.gait_parts(ratio, set, slope, col).iter().map(|(c, w)| c.speed * w).sum()
    }
    fn gait_parts(&self, ratio: f32, set: usize, slope: i16, col: f32) -> Vec<(&Clip, f32)> {
        let col = col.clamp(0.0, 6.0);
        let (i, f) = (col.floor() as usize, col.fract());
        animblend::split_ratio(ratio).into_iter().filter(|s| s.0 >= 0).flat_map(|(s, w)| {
            let gait = GAITS[(s as usize).min(3)];
            [(self.grid_clip(gait, set, slope, i), w * (1.0 - f)), (self.grid_clip(gait, set, slope, (i + 1).min(6)), w * f)]
        }).filter_map(|(c, w)| Some((c?.0, w))).filter(|p| p.1 > 0.0).collect()
    }
    fn grid_clip(&self, gait: Gait, set: usize, slope: i16, col: usize) -> Option<(&Clip, bool)> {
        let base = self.gait(gait, set, slope);
        let row = match gait { Gait::Walk => 0, Gait::Run => 1, Gait::Sprint => 2, Gait::Idle => return Some((base, false)) };
        let (id, turns) = match set {
            CROUCHED if row == 2 => return None,
            CROUCHED => (CROUCH_GRID[row][col], &CROUCH_TURNS[..]),
            SWIMMING => (onfoot::TURN_GRID[row][col], &SWIM_TURNS[..]),
            _ => (onfoot::TURN_GRID[row][col], &TURN_CLIPS[..]),
        };
        let filled = set == CROUCHED && row == 0 && matches!(col, 0 | 1 | 5 | 6);
        Some((turns.iter().find(|t| t.0 == id).and_then(|t| self.extra.get(t.1)).unwrap_or(base), filled))
    }
    fn turn_rates(&self, ratio: f32, set: usize, slope: i16) -> [f32; 7] {
        let mut rates = [0.0; 7];
        for (s, w) in animblend::split_ratio(ratio).into_iter().filter(|s| s.0 > 0) {
            let gait = GAITS[(s as usize).min(3)];
            for (k, r) in rates.iter_mut().enumerate() {
                let Some((c, false)) = self.grid_clip(gait, set, slope, k) else { continue };
                let a = &c.anim;
                let yaw = lsc4_combat::actiontable::wrap(mover_yaw(a, 1.0)) - lsc4_combat::actiontable::wrap(mover_yaw(a, 0.0));
                *r = w.mul_add(onfoot::clip_turn_rate(yaw, k, a.duration), *r);
            }
        }
        rates
    }
    fn gait(&self, gait: Gait, set: usize, slope: i16) -> &Clip {
        let own = match set {
            CROUCHED => self.extra.get(CROUCH[(gait as usize).min(2)]),
            SWIMMING => self.extra.get(SWIM[gait as usize]),
            _ => None,
        };
        let id = match gait { Gait::Walk => 0x1C, Gait::Run => 0x28, _ => 0 };
        let sloped = (set == ON_FOOT).then(|| SLOPES.iter().find(|s| s.0 == lsc4_combat::slope::clip_on_slope(slope, id))).flatten()
            .and_then(|s| self.extra.get(s.1));
        own.or(sloped).unwrap_or(&self.clips[gait as usize])
    }
}
fn set_of(key: &str) -> usize {
    if key.starts_with("move_crouch/") {
        CROUCHED
    } else if key.starts_with("swimming/") {
        SWIMMING
    } else {
        ON_FOOT
    }
}

fn stops_of(set: usize) -> &'static [(u32, &'static str)] {
    match set { CROUCHED => &CROUCH_STOPS, SWIMMING => &SWIM_STOPS, _ => &STOPS }
}

pub fn set_flags(k: usize) -> u32 {
    SET_FLAGS.get().map_or([0xFDD0_0000, 0x9FA0_0000, 0xFFF0_0000, 0x9C00_0000][k], |f| f[k])
}

fn pick(clips: &Clips, p: &mut Player, before: f32, wanted: f32, set: usize, dt: f32) {
    if p.forced.is_some() {
        (p.state, p.turn_end) = (0.0, false);
        return;
    }
    let stops = stops_of(set);
    const WALK: u32 = 0x1C;
    let step = |key: &str| dt / clips.extra[key].anim.duration.max(0.01);
    let mut playing = vec![movestop::PickClip { id: WALK, phase: p.phase, flags: 0x100, ..Default::default() }];
    if let Some(s) = &p.stop {
        playing.push(movestop::PickClip { id: s.id, phase: s.phase, flags: 0x10, rate: s.rate, step: step(s.key), ..Default::default() });
    }
    if let Some(s) = &p.start {
        let advance = step(s.key);
        playing.push(movestop::PickClip { id: s.id, phase: s.phase, flags: 0x10, rate: s.rate, step: advance, time: s.phase, advance,
                                          length: 1.0, ..Default::default() });
    }
    let current = playing.iter().find(|c| (movestart::WSTART..=movestart::RUNSTART_R_180).contains(&c.id)).copied();
    let has = movestop::stop_clip(p.ratio, before, false).map(|r| movestop::stop_foot(r, p.phase).0)
        .is_none_or(|id| stops.iter().any(|s| s.0 == id && clips.extra.contains_key(s.1)));
    let q = movestop::Pick { want: wanted, ratio: p.ratio, before, current, state: p.state, set_bits: set_flags(set),
                             in_set: has, started: has, set_ids: [-1; 7], walk: before > 0.0, switch: true, want_heading: p.goal,
                             heading: p.heading, playing, bits: if std::mem::take(&mut p.turn_end) { 0x4000 } else { 0 }, ..Default::default() };
    let mut b = movestop::PickBlend { ratio: p.ratio, b48: -1, stops: [WALK as i32, -1, -1, -1], b28: p.turning.rate, b2c: p.turning.left,
                                      ..Default::default() };
    let o = movestop::pick(&q, &mut b);
    (p.state, p.ratio, p.turning.rate, p.turning.left) = (o.state, b.ratio, b.b28, b.b2c);
    // Stopping (0x82417438's state -1 block, 0x82417A58): the wanted heading is his own, so he turns no more; on the
    // frame the stop is asked the heading's step (0x822FAFA8) finds no gap and turns him none of it either.
    // ponytail: carrying and the task type (vf +0xC == 4) are taken as the on-foot player's.
    let turned = std::mem::take(&mut p.turned);
    if o.state == -1.0 {
        if o.ask.is_some() {
            p.heading -= turned;
        }
        p.goal = p.heading;
    }
    for (id, rate) in o.blend_outs {
        if let Some(s) = p.stop.as_mut().filter(|s| s.id == id) {
            s.rate = rate;
        }
    }
    if o.removed {
        (p.start, p.phase) = (None, 0.0);
    }
    if let (Some(id), Some(phase)) = (o.ask, o.phase) {
        if let Some(&(id, key)) = stops.iter().find(|s| s.0 == id) {
            p.stop = Some(Stop { key, id, phase, weight: 0.0, rate: movestop::BLEND_IN, from: (gait_ratio(q.ratio, q.state), q.playing[0].phase) });
        }
    }
}

fn stop(clips: &Clips, p: &mut Player, set: usize, dt: f32) -> Option<(Vec2, f32)> {
    let s = p.stop.as_mut()?;
    let anim = &clips.extra[s.key].anim;
    let step = dt / anim.duration.max(0.01);
    if s.rate > 0.0 && set != set_of(s.key) {
        s.rate = -movestop::BLEND_OUT;
    }
    let next = (s.phase + step).min(1.0);
    let out = (dt > 0.0).then(|| (mover(anim, next) - mover(anim, s.phase)).truncate() / dt);
    s.phase = next;
    s.weight = (s.weight + s.rate * dt).clamp(0.0, 1.0);
    let out = out.map(|m| (m, s.weight));
    if s.rate < 0.0 && s.weight == 0.0 {
        p.stop = None;
    }
    out
}

pub fn turn_new(clips: &Clips, p: &mut Player, set: usize) {
    if p.forced.is_some() {
        return;
    }
    let moving = p.ratio > 0.0 && p.start.is_none() && p.stop.as_ref().is_none_or(|s| s.rate < 0.0);
    if let (true, Some(heading)) = (set_flags(set) & lsc4_combat::moveset::TURN_180 != 0 && moving && p.turn.is_none(), p.wish_heading) {
        let gap = (heading - p.heading + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
        if moveturn::turn_wanted(true, true, p.ms.wrapping_sub(p.turn_at), gap) {
            let id = moveturn::turn_clip(p.ratio, gap);
            if let Some(&(id, key)) = TURNS.iter().find(|t| t.0 == id && clips.extra.contains_key(t.1)) {
                let free = clips.extra[key].anim.first_event(moveturn::FREE, 0.0, 1.0);
                p.turn = Some(Turn { key, id, phase: 0.0, weight: 0.0, rate: moveturn::BLEND_IN, free, fresh: true, from: gait_ratio(p.ratio, p.state) });
                (p.ratio, p.turn_at) = (0.0, p.ms);
            }
        }
    }
}

fn turn(clips: &Clips, p: &mut Player, wanted: f32, set: usize, dt: f32) -> Option<(Vec2, f32)> {
    if p.forced.is_some() {
        return None;
    }
    let t = p.turn.as_mut()?;
    if std::mem::take(&mut t.fresh) {
        return None;
    }
    let anim = &clips.extra[t.key].anim;
    let step = dt / anim.duration.max(0.01);
    if t.rate > 0.0 && (set != set_of(t.key) || moveturn::turn_cancel(t.id, 0.0, wanted) || moveturn::turn_done(t.phase, step)) {
        t.rate = -moveturn::BLEND_IN;
        p.turn_end = true;
    }
    let next = (t.phase + step).min(1.0);
    let out = (dt > 0.0).then(|| (mover(anim, next) - mover(anim, t.phase)).truncate() / dt);
    let yaw = lsc4_combat::actiontable::wrap(mover_yaw(anim, next) - mover_yaw(anim, t.phase));
    t.phase = next;
    t.weight = (t.weight + t.rate * dt).clamp(0.0, 1.0);
    let share = if t.weight > 0.0 { 1.0 / (2.0 - t.weight) } else { 0.0 };
    p.yaw_was[1] += yaw * share;
    let out = out.map(|m| (m, share));
    if t.rate < 0.0 && t.weight == 0.0 {
        p.turn = None;
    }
    out
}

fn spot(clips: &Clips, p: &mut Player, set: usize, dt: f32) {
    let spots: &[(u32, &str)] = match set { CROUCHED => &CROUCH_SPOTS, SWIMMING => &[], _ => &SPOTS };
    let standing = !spots.is_empty() && p.state <= 0.0 && p.forced.is_none() && p.cover.is_none() && spots.iter().all(|s| clips.extra.contains_key(s.1));
    p.stand_step = None;
    if let Some(s) = p.spot.as_mut().filter(|s| set_of(s.key) != set) {
        s.rate = -movestop::BLEND_IN;
    }
    if standing {
        let length = clips.extra[spots[0].1].anim.duration;
        let playing = |id: u32| p.spot.as_ref().filter(|s| s.id == id && set_of(s.key) == set).map(|s| moveturn::SpotClip { phase: s.phase, length, ..Default::default() });
        // A start clip blending out, or a walk start with a turn playing (0x15, 0x16, 0x13, 0x14).
        let start = p.start.as_ref().is_some_and(|s| s.rate < 0.0 || (movestart::WSTART_TURN_L90..=movestart::WSTART_TURN_R180).contains(&s.id));
        let out = moveturn::stand(&moveturn::Stand {
            turn_rate: TURN_SPEED, blend_in: movestop::BLEND_IN, switch: true, set_bits: set_flags(set), want: p.goal, heading: p.heading, start,
            left: playing(moveturn::TURN_360_L), right: playing(moveturn::TURN_360_R), fresh: moveturn::SpotClip { length, ..Default::default() },
            rate: p.turning.rate, rate68: p.spot_turn, dt, ..Default::default()
        });
        (p.spot_turn, p.stand_step) = (out.rate68, Some(out.step));
        match out.ask {
            Some(moveturn::Ask::Turn { id, blend, .. }) => {
                let &(id, key) = spots.iter().find(|s| s.0 == id).expect("a spot turn id");
                p.spot = Some(Spot { key, id, phase: out.clip.map_or(0.0, |c| c.phase), weight: 0.0, rate: blend, speed: 1.0 });
            }
            Some(moveturn::Ask::Idle { blend, .. }) => p.spot.iter_mut().for_each(|s| s.rate = -blend),
            None => {}
        }
    } else {
        p.spot_turn = 0.0;
        p.spot.iter_mut().for_each(|s| s.rate = -movestop::BLEND_IN);
    }
    let Some(s) = p.spot.as_mut() else { return };
    // Its rate (+0x54) is his turn speed over 11: 1.
    let anim = &clips.extra[s.key].anim;
    let next = (s.phase + dt / anim.duration.max(0.01)).fract();
    if let Some(step) = p.stand_step.as_mut() {
        *step += lsc4_combat::actiontable::wrap(mover_yaw(anim, next) - mover_yaw(anim, s.phase)) * s.weight;
    }
    s.phase = next;
    s.weight = (s.weight + s.rate * dt).clamp(0.0, 1.0);
    if s.rate < 0.0 && s.weight == 0.0 {
        p.spot = None;
    }
}

fn start_new(clips: &Clips, p: &mut Player, wanted: f32, set: usize) {
    if p.forced.is_some() {
        return;
    }
    if let (true, Some(heading)) = (p.state == 1.0 && p.start.is_none() && p.turn.is_none(), p.wish_heading) {
        let gap = (heading - p.heading + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
        // The set's flags say whether it has run starts and turning starts (move_crouch neither: WStart, turned by `spin`).
        let f = set_flags(set);
        let turns = !(f & lsc4_combat::moveset::STARTS != 0 && f & lsc4_combat::moveset::TURN_STARTS == 0);
        let (id, turn) = movestart::start_clip(gap, movestart::start_run(wanted, true, f & lsc4_combat::moveset::RUN_STARTS != 0, 3.0, false), turns);
        let starts: &[(u32, &str)] = match set { CROUCHED => &CROUCH_STARTS, SWIMMING => &SWIM_STARTS, _ => &STARTS };
        if let Some(&(id, key)) = starts.iter().find(|s| s.0 == id && clips.extra.contains_key(s.1)) {
            let duration = clips.extra[key].anim.duration.max(0.01);
            let own = if clips.extra[key].anim.mover_rotation.is_some() { 0.0 } else { turn / duration };
            let spin = own + movestart::start_rate(gap, turn, duration, TURN_SPEED);
            p.start = Some(Start { key, id, phase: 0.0, weight: 0.0, rate: movestart::BLEND_IN, spin, turn, goal0: heading });
        }
    }
    // The gait holds at idle under it (the pick has read the eased ratio).
    if p.start.as_ref().is_some_and(|s| s.rate > 0.0) {
        p.ratio = 0.0;
    }
}

fn start(clips: &Clips, p: &mut Player, wanted: f32, set: usize, dt: f32) -> Option<(Vec2, f32)> {
    if p.forced.is_some() {
        return None;
    }
    let goal = p.goal;
    let s = p.start.as_mut()?;
    let anim = &clips.extra[s.key].anim;
    let step = dt / anim.duration.max(0.01);
    if s.rate > 0.0 && (set != set_of(s.key) || wanted == 0.0) {
        s.rate = -movestart::BLEND_IN;
    }
    let next = (s.phase + step).min(1.0);
    let out = (dt > 0.0).then(|| (mover(anim, next) - mover(anim, s.phase)).truncate() / dt);
    if s.rate > 0.0 && dt > 0.0 {
        let spin = if s.fixed(goal) { s.spin } else { 0.0 };
        p.heading += spin.mul_add(dt, lsc4_combat::actiontable::wrap(mover_yaw(anim, next) - mover_yaw(anim, s.phase)));
    }
    s.phase = next;
    s.weight = (s.weight + s.rate * dt).clamp(0.0, 1.0);
    let out = out.map(|m| (m, s.weight));
    if s.rate < 0.0 && s.weight == 0.0 {
        p.start = None;
    }
    out
}

fn gait_ratio(ratio: f32, state: f32) -> f32 {
    if ratio > 0.0 && ratio <= 1.0 && state >= 2.0 { 1.0 } else { ratio }
}

fn gait_step(clips: &Clips, ratio: f32, col: f32, phase: f32, set: usize, slope: i16, dt: f32) -> f32 {
    let mut synced: Vec<animblend::Player> = clips.gait_parts(ratio, set, slope, col).into_iter()
        .map(|(c, w)| animblend::Player { phase, weight: w, active: true, loops: true, synced: true, playing: true,
                                          ..animblend::Player::new(c.anim.duration) })
        .collect();
    animblend::sync_phases(&mut synced, dt);
    synced[0].phase
}

fn gait_left(p: &mut Player, dt: f32) {
    if p.ratio > 0.0 && !p.turn.as_ref().is_some_and(|t| t.rate > 0.0) {
        p.gait_w = 1.0;
        return;
    }
    p.gait_w = (-2.0f32).mul_add(dt, p.gait_w).max(0.0);
    if p.gait_w == 0.0 {
        p.phase = 0.0;
    }
}

fn turn_column(clips: &Clips, p: &mut Player, set: usize, dt: f32) -> f32 {
    if p.state <= 1.0 {
        p.turn_ramp = 1.0;
    }
    let k = if p.turn_ramp > 0.0 {
        let k = (1.0 - p.turn_ramp).max(0.01);
        p.turn_ramp -= dt;
        k
    } else {
        1.0
    };
    let target = onfoot::turn_column(&clips.turn_rates(gait_ratio(p.ratio, p.state), set, p.slope.slope), p.turning.rate * k);
    onfoot::ease_column(p.turn_col, target, dt)
}

#[cfg(test)] mod stamina_original_tests { use super::*;
#[test]
    fn holding_the_run_button_runs_and_tapping_sprints() {
        let mut s = Stamina::default();
        // Pressed and held: a run, the pressure kept just under the threshold.
        assert_eq!(s.run(true, true, 0.02), 1.0);
        assert_eq!(s.run(false, true, 0.02), 1.0);
        assert_eq!(s.pressure, 4.99);
        // A tap on top sprints: 8.99 / 5 gives 1 + 0.798 x 0.3, and 0.5 of stamina a 50th of a second.
        let v = s.run(true, true, 0.02);
        assert!((v - 1.2394).abs() < 1e-4 && s.left == 599.5, "{v} {s:?}");
        // Let go, the pressure falls 0.2 a 50th: a sprint for 19 frames, a run down to 0, then a walk.
        let v: Vec<f32> = (0..60).map(|_| s.run(false, false, 0.02)).collect();
        assert!(v[..18].iter().all(|v| *v > 1.0) && v[20..43].iter().all(|v| *v > 0.0 && *v <= 1.0) && v[46..].iter().all(|v| *v == 0.0), "{v:?}");
    }
#[test]
    fn sprint_spends_stamina_then_he_runs_until_it_recovers() {
        let mut s = Stamina::default();
        // Tapped every half-second frame: the first press runs, then 12.5 a frame takes 60 frames from 600 to -150 and
        // the 61st is refused but still a sprint.
        assert_eq!(s.run(true, true, 0.5), 1.0);
        for _ in 0..61 {
            assert!(s.run(true, true, 0.5) > 1.0);
        }
        assert_eq!((s.left, s.breathless), (-150.0, true));
        // Out of breath the button runs, and running he recovers 0.3 x 25 a second (3.75 a frame) until above 0.
        let mut runs = 0;
        while s.run(true, true, 0.5) == 1.0 {
            s.recover(Gait::Run, 0.5);
            runs += 1;
        }
        assert!((40..=41).contains(&runs) && !s.breathless, "{runs} {s:?}");
        // Standing he recovers 25 a second, past 600 by at most one frame.
        for _ in 0..100 {
            s.recover(Gait::Idle, 0.5);
        }
        assert!(s.left >= 600.0 && s.left < 612.5, "{s:?}");
    }
#[test]
    fn in_a_vehicle_stamina_refills_at_the_still_rate() {
        // Move state 0 in the car: rate 1, 25 a second, as standing; nothing past 600.
        let (mut car, mut still) = (Stamina { left: -150.0, ..Default::default() }, Stamina { left: -150.0, ..Default::default() });
        for _ in 0..80 {
            car.recover_in_vehicle(0.5);
            still.recover(Gait::Idle, 0.5);
            assert_eq!(car.left, still.left);
        }
        assert_eq!(car.left, 600.0);
        car.recover_in_vehicle(0.5);
        assert_eq!(car.left, 600.0);
    }
}
