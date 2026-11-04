// Adapted from LSC4 380c8cb7d49a viewer/meleetask.rs (GPL-3.0-only).
// Exact source hash and host substitutions: controller-source.json.
use bevy_math::{Vec2,Vec3,Quat};
use tracing::{debug,info};
use delirium_data::anim::Animation;
use lsc4_combat::actiontable::{wrap,Action,ResultDef};
use lsc4_combat::controls::{index,Controls,RageInput};
use lsc4_combat::melee::{self,Ask,Fighter,Held,Opponent,Target};
use crate::Data;
fn h(s:&str)->u32 {crate::hash(s)}
#[derive(Default)]
pub struct WeaponView {pub fire: &'static str, pub name: &'static str, pub damage_type: &'static str, pub slot: &'static str, pub held:Held}
pub fn held(w:&WeaponView)->Held {w.held}
/// attack, 0x10 Y attack2, 0x80 X kick, 0x40 A block, 0x1 / 0x2 LT / RT, 0x4 / 0x8 LB / RB), the left stick and
/// the triggers' pressure, through the control map's mappers each frame with their 64-frame histories.
#[derive(Default,Clone)]
pub struct Pad {
    pub controls: Controls,
    input: RageInput,
    set: bool,
}

impl Pad {
    pub fn feed(&mut self, buttons: u32, stick: [u8; 2], triggers: [u8; 2], now: u32) {
        if !self.set {
            self.controls.set_up_for_pad(0, now, true, 0);
            self.set = true;
        }
        let p = &mut self.input.pads[0];
        p.buttons = buttons;
        p.sticks = [stick[0], stick[1], 0x7F, 0x7F];
        (p.pressure[0xA], p.pressure[0xB]) = (triggers[0], triggers[1]);
        let sticks = self.controls.stick_values();
        self.controls.update(sticks, &mut self.input, now, false);
    }

    /// The control at `c` (its place in the object): its value this frame.
    pub fn value(&self, c: u32) -> u8 {
        self.controls.values[index(c)].get()
    }

    /// The control at `c` (its place in the object) went down this frame.
    pub fn pressed(&self, c: u32) -> bool {
        let v = &self.controls.values[index(c)];
        v.get() >= 0x80 && v.last ^ v.invert < 0x80
    }

    /// The control at `c` is down (its value, inverted as read, over 0x7F).
    pub fn held(&self, c: u32) -> bool {
        self.value(c) >= 0x80
    }

    /// The control at `c` came up this frame (0x822D3230).
    pub fn released(&self, c: u32) -> bool {
        let v = &self.controls.values[index(c)];
        v.get() < 0x80 && v.last ^ v.invert >= 0x80
    }
}

/// 0x823C4890: a melee button went down or up this frame; with LT held any of `MELEE_BUTTONS`, else attack alone; or,
/// `gun` ([`gun_in_hand`]), attack3 or attack4 (0xBF4 / 0xD14, both RT: 0x823C4A88 on).
pub(crate) fn starts(pad: &Pad, gun: bool) -> bool {
    let edge = |b: u32| pad.pressed(b) || pad.released(b);
    let melee = if pad.held(LOCK_LT) { MELEE_BUTTONS.iter().any(|&b| edge(b)) } else { edge(ATTACK) };
    melee || gun && GUN_BUTTONS.iter().any(|&b| edge(b))
}

/// 0x823C4A0C: the weapon in his hand (0x823D4D60 gives one) is no melee one (its info's +0xC, the damage type, not 1
/// MELEE); with [0x82B39504] 2 (The Ballad of Gay Tony) not a thrown one (info +4 8, THROWN) either. The game's
/// [0x82B39504] is IV's 0 here (scripts.rs GET_CURRENT_EPISODE), so that test never takes one out.
fn gun_in_hand(c: &Ctx) -> bool {
    c.in_hand && c.weapon.damage_type != "MELEE" && !(EPISODE == 2 && c.weapon.slot == "THROWN")
}

/// The fists or a melee weapon in his hand (0x822C3214 / 0x822C54D8: ped +0x294 has no weapon, or its info's +0xC is 1).
fn fists_or_melee(w: &WeaponView) -> bool {
    w.name == "UNARMED" || w.damage_type == "MELEE"
}

/// The episode word 0x82B39504: 0, the base game.
const EPISODE: u32 = 0;

/// The controls 0x823C4890 asks with LT held: attack, attack2, block, kick (without it attack alone).
const MELEE_BUTTONS: [u32; 4] = [0xBDC, 0xBE8, 0xC0C, 0xC00];
/// The controls 0x823C4890 asks with a gun in hand: attack3, attack4.
const GUN_BUTTONS: [u32; 2] = [0xBF4, 0xD14];
const ATTACK: u32 = 0xBDC;
/// The quit timer's limit (0x82A99CAC), the push-away distance (0x82AAE2C0) and its rate (0x82AAE2C4).
const QUIT: f32 = 1.5;
const TOO_CLOSE: f32 = 0.65;
const PUSH_RATE: f32 = 20.0;

/// A frame's distance homing (0x8265E510's distance part): stand `dist` off `point` along the way from it to the ped,
/// the forward speed's change held to `rate` x dt (a negative rate: set at once on the `snap` frame, else none), and
/// with `push` only ever backing off (0x822C1F58's push away within `TOO_CLOSE`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Home {
    pub point: Vec3,
    pub dist: f32,
    pub rate: f32,
    pub push: bool,
    pub snap: bool,
}

impl Home {
    /// 0x8265E510 -> 0x826599D0: the forward speed (m/s) the ped at `pos` facing `heading`, moving at `anim` (its
    /// frame's sideways / forward, +0xE0 / +0xE4: the clip's mover) gets this frame, none when a push would pull it in.
    /// The distance less the clip's forward `travel` so far (its mover from the start, 0x82446430) is held to
    /// `TOO_CLOSE` at least (the byte 0x82AAE2C8 = 1).
    pub fn speed(&self, pos: Vec3, heading: f32, anim: Vec2, travel: f32, dt: f32, rate_step: f32) -> Option<f32> {
        let dir = (pos - self.point).truncate().normalize_or_zero();
        let goal = self.point.truncate() + dir * (self.dist - travel).max(TOO_CLOSE);
        let v = (goal - pos.truncate()) / dt;
        let (right, fwd) = (Vec2::new(heading.cos(), heading.sin()), Vec2::new(-heading.sin(), heading.cos()));
        let (dx, mut dy) = (right.dot(v) - anim.x, fwd.dot(v) - anim.y);
        if self.push && dy > 0.0 {
            return None;
        }
        if self.rate >= 0.0 {
            let top = self.rate * rate_step;
            let l2 = dy.mul_add(dy, dx * dx);
            if l2 > top * top {
                dy = dy / l2.sqrt() * top;
            }
        } else if !self.snap {
            dy = 0.0;
        }
        Some(anim.y + dy)
    }
}

/// 0x822C1F58's homing on a frame of result `r` (its strike window open or not, its first frame or not), for the ped
/// at `pos` (its root height) facing `heading` against `foe`. Within `TOO_CLOSE` of the target's root (0x822BFE40) it
/// is pushed off its entity position, and nothing else. Else, the window open and the result's target point within
/// its Height / Distance / Angle ranges (when it names one), the distance homing (DesiredDistance) and the turn toward
/// the point's bearing + DesiredAngle at its rate (`heading` turned here; a negative rate snaps on the first frame).
/// A hit reaction is its melee task's running result too (`MeleeMove::given`), so it homes on its task's target, its
/// attacker, the same way.
pub fn homing(r: &ResultDef, foe: Option<Foe>, pos: Vec3, heading: &mut f32, window: bool, first: bool, dt: f32) -> Option<Home> {
    let f = foe?;
    let root = f.bones[0].unwrap_or(f.feet + Vec3::Z);
    if (root - pos).truncate().length_squared() < TOO_CLOSE * TOO_CLOSE {
        let point = f.point(0, pos.into()).map_or(f.feet + Vec3::Z, Vec3::from);
        return Some(Home { point, dist: TOO_CLOSE, rate: PUSH_RATE, push: true, snap: true });
    }
    if !window {
        return None;
    }
    let at = |k: u32| f.point(k, pos.into()).map(Vec3::from);
    let ranges = !r.homing || at(r.homing_target_type).is_some_and(|p| {
        let off = p - pos;
        let out = |x: f32, r: [f32; 2]| x < r[0] || x > r[1];
        !out(off.z, r.height_range) && !out(off.truncate().length(), r.distance_range)
            && melee::angle_in(wrap((-off.x).atan2(off.y)) - wrap(*heading), r.angle_range)
    });
    let [dist, rate] = r.desired_distance;
    let home = at(r.homing_target_type).filter(|_| ranges && rate != 0.0).map(|point| Home { point, dist, rate, push: false, snap: first });
    let [goal, rate] = r.desired_angle;
    if let Some(p) = at(r.homing_target_type).filter(|_| ranges && rate != 0.0) {
        let want = wrap(goal + (-(p.x - pos.x)).atan2(p.y - pos.y));
        let gap = wrap(want - wrap(*heading));
        if rate > 0.0 {
            *heading = wrap(*heading + gap.clamp(-rate * dt, rate * dt));
        } else if first {
            *heading = want;
        }
    }
    home
}

/// The hands' parts lose this at their start (0x8200C24C); the point spacing's 0.7 (0x820BEDF0); the most points
/// a part gives (0x822C22F8) and hit-list entries (+0x60).
const HAND_SHORT: f32 = 0.06;
const SPACING: f32 = 0.7;
const MOST_POINTS: usize = 16;
const HIT_LIST: usize = 8;
/// The hands (StrikeBone L_HAND / R_HAND); the held weapon's place in `Melee::last`.
const HANDS: [u32; 2] = [0x4C3, 0x4D0];
const WEAPON_KEY: u32 = 1 << 16;

/// The running result (CTaskSimpleMeleeActionResult): its action and result (a place in the table's results), the
/// clip (`dictionary/clip`, none for a result without one) at `clip_t` seconds of `dur`, its rate, this frame's step,
/// and whether it has struck something (+0x5E 0x40).
#[derive(Clone, Debug, PartialEq)]
pub struct MeleeMove {
    pub action: u32,
    pub result: usize,
    pub key: Option<String>,
    pub clip_t: f32,
    pub dur: f32,
    pub rate: f32,
    pub step: f32,
    pub struck: bool,
    /// Its result's ForceImmediateReaction (def +0x74), until the caller gives its target the reaction (the ctor,
    /// 0x822C40C0, site A 0x822c4598: [`react`] with `forced`).
    pub forced: bool,
    /// The ctor's swing sound (0x824D8120, not for an idle or a block result), until the caller asks for it.
    pub swing: bool,
    /// +0x5C 0x04 first frame, +0x5E 0x80 the strike window open.
    first: bool,
    window: bool,
    /// The strike bones live (+0x5D 0x10 / 0x04) and once live (0x08 / 0x02); who it has struck (+0x60).
    pub live: [bool; 2],
    was: [bool; 2],
    hits: Vec<u64>,
    /// The clip's events last step (its player's +0x74).
    events: u32,
    /// Its SpecialResult was applied (+0x5D 0x80), its SelfDamage (+0x5C 0x01).
    special: bool,
    hurt: bool,
    /// A reaction the dispatcher gave (+0x5C 0x02): a requested one goes in over it only at its Priority or above
    /// (0x822C6848, `melee_task::request_takes`). The caller sets it as the reaction starts.
    pub given: bool,
}

impl MeleeMove {
    /// The clip's phase, 0-1.
    pub fn phase(&self) -> f32 {
        if self.dur > 0.0 { (self.clip_t / self.dur).clamp(0.0, 1.0) } else { 0.0 }
    }
}

/// Start action `a`'s result in `slot` (the ActionResult ctor and its clip start; helper for hit reactions too):
/// none for an action without one. `player` takes PlayerPlaybackMult; `dur` gives a loaded clip's length.
pub fn play_result(slot: &mut Option<MeleeMove>, d: &Data, a: &Action, player: bool, dur: &dyn Fn(&str) -> Option<f32>) {
    let Some(ri) = d.result_of(a) else {
        *slot = None;
        return;
    };
    let r = &d.table.results[ri];
    if let Some(m) = slot.as_mut().filter(|m| m.result == ri && m.key.is_some() && m.clip_t < m.dur * 0.999) {
        (m.action, m.struck, m.first, m.window, m.live, m.was) = (a.name, false, true, true, [false; 2], [false; 2]);
        m.forced = r.force_immediate_reaction;
        m.swing = !r.is_an_idle && !r.is_a_block;
        m.hits.clear();
        return;
    }
    let (pt, plen) = slot.as_ref().filter(|m| m.key.is_some()).map_or((0.0, 0.0), |m| (m.clip_t, m.dur));
    let key = d.clip_key(r).filter(|k| dur(k).is_some());
    let len = key.as_deref().and_then(dur).unwrap_or(0.0);
    let t = match r.sync_start_time {
        0 => r.start_time,
        1 => r.start_time + pt,
        2 => plen - pt + r.start_time,
        _ => 0.0,
    };
    let rate = r.playback_rate * if player { r.player_playback_mult } else { 1.0 };
    *slot = Some(MeleeMove { action: a.name, result: ri, key, clip_t: t.clamp(0.0, len), dur: len, rate, step: 0.0, struck: false, forced: r.force_immediate_reaction, swing: !r.is_an_idle && !r.is_a_block, first: true, window: true,
                          live: [false; 2], was: [false; 2], hits: vec![], events: 0, special: false, hurt: false, given: false });
}

/// The bones 0x8265DCB0 reads for pedRoot, pedHead, pedChest, pedKneeLeft and pedKneeRight (its cases 2, 4..7: the
/// tag handed to 0x822FC328, the bone's world position).
pub const POINT_BONES: [u16; 5] = [1202, 1205, 13985, 419, 424];

/// The other side of a fight, as the action test and homing read it: Niko or a ped.
#[derive(Clone, Copy, Debug)]
pub struct Foe {
    pub player: bool,
    pub feet: Vec3,
    pub heading: f32,
    pub health: f32,
    /// How it lies as the opponent part asks it (0x822FCB78, [`lies`]): 4 upright, 0 face, 1 right side, 2 back, 3
    /// left side.
    pub lies: u8,
    /// A ped's own melee stats (`Ped.fighter`); None for Niko or a ped without them (a bare AI's).
    pub me: Option<Me>,
    /// Its `POINT_BONES`' world positions (none: a bone it lacks).
    pub bones: [Option<Vec3>; 5],
    /// Its running melee result (433): the result's hash and its clip time and step, and whether it IsABlock.
    pub task: Option<(u32, Option<(f32, f32)>)>,
    pub block: bool,
    /// What the SpecialTest reads of it besides its place (`special`): its velocity, its +0x240 melee bits
    /// (`peds::Quirks::melee_flags`) and whether its melee move started from idle (`Melee::from_idle`).
    pub velocity: Vec3,
    pub flags_240: u32,
    pub from_idle: bool,
    /// On stairs ([`on_stairs`]).
    pub on_stairs: bool,
    /// The weapon in its hands (+0x294, [`held`]) and whether its running result struck (+0x5E 0x40), as its side of
    /// OpponentsActionToTest reads them.
    pub weapon: Option<Held>,
    pub struck: bool,
    /// Its box for the point type ClosestBoxPoint (0x822F95C0), min and max in its frame ([`Foe::rows`]): its
    /// fragment's parts as drawn ([`frag_box`]). None: a 0.3 m column at its feet stands in.
    /// ponytail: the game's is its fragment's composite bound's box (+0x704 -> +4 -> +0xC, +0x10 / +0x20) under its
    /// matrix; this is the drawn parts' capsules' box in the frame of its feet and heading (kept upright on the
    /// ragdoll).
    pub frame_box: Option<([f32; 3], [f32; 3])>,
}

impl Default for Foe {
    fn default() -> Foe {
        Foe { player: false, feet: Vec3::ZERO, heading: 0.0, health: 0.0, lies: 4, me: None, bones: [None; 5], task: None, block: false, velocity: Vec3::ZERO,
              flags_240: 0, from_idle: false, on_stairs: false, weapon: None, struck: false, frame_box: None }
    }
}

/// How a ped lies, 0x822FCB78 as the opponent part asks it of each side (0x8265CBE8 at 0x8265CE0C): its spine bone 0x4B2
/// (its `POINT_BONES`' first) as drawn, world, its two axes' heights and its own, with its matrix's height `ped_z`
/// (`fall::lies`); on its ragdoll (+0x38 is its ragdoll's instance +0x704) with its feet 0x1A4 and 0x1A9 as drawn
/// instead (`fall::lies_on_ragdoll`). Both read the skeleton's world matrices (0x822FC408; 0x822FC570 the same after
/// the refresh 0x8244C920). None (a skeleton without the bone): 4, as `peds::getup::Down`'s.
/// [`Foe::frame_box`] from fragment parts (each a capsule: its ends in the world and its radius), in the frame of
/// `feet` and `heading` ([`Foe::rows`]); None with no parts.
pub fn frag_box(feet: Vec3, heading: f32, parts: impl IntoIterator<Item = (Vec3, Vec3, f32)>) -> Option<([f32; 3], [f32; 3])> {
    let back = Quat::from_rotation_z(-heading);
    let origin = feet + Vec3::Z;
    parts.into_iter().flat_map(|(a, b, r)| [(a, r), (b, r)]).map(|(q, r)| {
        let l = back * (q - origin);
        (l - Vec3::splat(r), l + Vec3::splat(r))
    }).reduce(|x, y| (x.0.min(y.0), x.1.max(y.1))).map(|(lo, hi)| (lo.into(), hi.into()))
}

impl Foe {
    /// Its matrix's rows as the box point reads them: right and forward by its heading, up, and its position (feet +
    /// 1 m, as [`Foe::point`]'s entity position).
    pub fn rows(&self) -> [[f32; 3]; 4] {
        let h = self.heading;
        [[h.cos(), h.sin(), 0.0], [-h.sin(), h.cos(), 0.0], [0.0, 0.0, 1.0], (self.feet + Vec3::Z).into()]
    }

    /// It as the SpecialTest (0x82659CC8) reads it: aimed at its root bone (1202, 0x4B2; feet + 1 m without one), its
    /// forward along its heading.
    pub fn special(&self) -> melee::Special {
        let h = self.heading;
        melee::Special { on_stairs: self.on_stairs, velocity: self.velocity.into(), flags_240: self.flags_240, move_from_idle: self.from_idle,
                         point: self.bones[0].unwrap_or(self.feet + Vec3::Z).into(), forward: [-h.sin(), h.cos(), 0.0] }
    }

    /// Its running result as `task` and `block` read it.
    pub fn with_move(self, d: &Data, mv: Option<&MeleeMove>) -> Foe {
        let r = mv.map(|m| &d.table.results[m.result]);
        Foe { task: mv.zip(r).map(|(m, r)| (d.reaction_identity(r.name), m.key.is_some().then_some((m.clip_t, m.step)))), block: r.is_some_and(|r| r.is_a_block), ..self }
    }

    /// A point on it by point type (0x8265DCB0; actiontable's POINTS), seen from `from`: a ped's bone for the ped
    /// points (0x822FC328; the entity's position without a skeleton, 0x822FC370), the entity's position for
    /// entityRoot; ClosestBoxPoint (1, 3) the nearest point of its box (`melee::closest_box_point`, 0x8265C9D0, on
    /// [`Foe::frame_box`]). ponytail: the entity's position is feet + 1 m; with no box, the root height's nearest
    /// point of a 0.3 m column stands in.
    pub(crate) fn point(&self, kind: u32, from: [f32; 3]) -> Option<[f32; 3]> {
        let root = self.feet + Vec3::Z;
        let bone = |i: usize| self.bones[i].unwrap_or(root);
        let p = match kind {
            0 => root,
            1 | 3 if self.frame_box.is_some() => {
                let (lo, hi) = self.frame_box.unwrap_or_default();
                Vec3::from(melee::closest_box_point(self.rows(), lo, hi, from).0)
            }
            1 | 3 => {
                let f = Vec3::from(from);
                let flat = (f - self.feet).with_z(0.0).normalize_or_zero();
                self.feet.with_z(f.z.clamp(self.feet.z, self.feet.z + 1.8)) + flat * 0.3
            }
            2 => bone(0),
            4..=7 => bone(kind as usize - 3),
            _ => return None,
        };
        Some(p.into())
    }
}

/// A ped without its own stats: a bare AI's (attack 0.8, defend 1.3, martial 50, no groups).
/// Every melee dictionary is in: the viewer reads them all at start (main.rs, the weapon and melee clips).
const IN: fn(i32) -> bool = |_| true;

const BARE: Me = Me { attack: 0.8, defend: 1.3, martial: 50.0, groups: [-1; 7], go: false, moves: -1 };


pub static TIME_SCALE: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(u32::MAX);

/// A SpecialResult's slow-mo, if it has one, for the timer (`TIME_SCALE`).
pub fn slow_mo(fx: &melee::SpecialEffect) {
    if let Some(s) = fx.slow_mo {
        TIME_SCALE.store(s.to_bits(), std::sync::atomic::Ordering::Relaxed);
    }
}


/// DefendWeakness, MeleePercent and groups, and whether this frame's roll wants a move (`lsc4_combat::melee_task::wants_move`,
/// 0x822C6848: the AI asks for its next action, 0x822C5B88, only then).
#[derive(Clone, Copy, Debug)]
pub struct Me {
    pub attack: f32,
    pub defend: f32,
    pub martial: f32,
    pub groups: [i32; 7],
    pub go: bool,
    /// Its move group (pedpersonality +0x11C) as a store place, -1 for none.
    pub moves: i32,
}

/// What a frame of melee reads from the fighter's side (Niko's, or a ped's with `me`; a ped's foe is Niko).
pub struct Ctx<'a> {
    pub pad: &'a Pad,
    /// The ped root (feet + 1 m) and heading.
    pub pos: Vec3,
    pub health: f32,
    pub weapon: &'a WeaponView,
    pub foe: Option<Foe>,
    pub me: Option<Me>,
    /// Niko's ask is locked on (r1+0x15F: ped +0x338 was set, 0x823CCBC8). It is his task's +0xF0 0x02 too (0x822BEBA0
    /// each frame: his lock-on, 1, else the old target, 0). A ped's is its task's +0xF0 0x02 alone (its ask's own byte
    /// is `Ask::lock_on` below): its Charge's 431 is made locked (0x82498D18, r10 1; again on a new target,
    /// 0x824859A8 -> 0x822BE8B0 with 1), the dispatcher's (0x8226D690) and a forced one's (0x822C45F8) not.
    pub lock_on: bool,
    /// The ped's +0x240 melee bits the SpecialTest reads (`peds::Quirks::melee_flags`), and whether it's on stairs
    /// ([`on_stairs`]).
    pub flags_240: u32,
    pub on_stairs: bool,
    /// Its own `POINT_BONES` (none: one it lacks), for when it is its foe's target, and how it lies (its pose
    /// as the opponent part reads it, as `Foe::lies`).
    pub bones: [Option<Vec3>; 5],
    pub lies: u8,
    /// Niko as the foe (a ped's ask): his pad (his side's impulses) and his running result ([`FoeTask`]; none while
    /// his melee task doesn't run).
    pub foe_pad: Option<&'a Pad>,
    pub foe_task: Option<FoeTask>,
    /// Its weapon is in its hand (the weapon manager's 0x823D4D60 gives one: its object matches the slot, or a slot
    /// with none is the fists' or 10's). A ped's always is.
    pub in_hand: bool,
}

/// Niko as a ped's foe: his running CTaskSimpleMeleeActionResult (433; his 431 runs, the stance being one too) as the
/// opponent part reads it (0x8265D038..0x8265D080, through 0x823C0E98): its target (+0x1C, 0x82702218), lock-on (+0x20,
/// 0x82174900), a given reaction (+0x5C 0x02, 0x822BF350), IsABlock (+0x5C 0x20, 0x822BF340) and struck (+0x5E 0x40,
/// 0x822BF3C0).
#[derive(Clone, Copy, Debug)]
pub struct FoeTask {
    pub target: TaskTarget,
    pub lock: bool,
    pub given: bool,
    pub block: bool,
    pub struck: bool,
}

/// A running result's target (+0x1C) as a ped's step sees Niko's: this ped, another ped (as the frame started), none.
#[derive(Clone, Copy, Debug)]
pub enum TaskTarget {
    Me,
    Ped(Foe),
    Nobody,
}

/// A player's running 433 as [`FoeTask`] has it, its target a [`Side`]'s place.
#[derive(Clone, Copy)]
struct Task433 {
    target: Option<usize>,
    lock: bool,
    given: bool,
    block: bool,
    struck: bool,
}

/// One ped the opponent part reads (0x8265CBE8): its state (points, running result, pose, IsABlock), how the SpecialTest
/// reads it, its fighter and groups, and for the player his controls (0x823CF688) and running 433; an AI's none.
#[derive(Clone, Copy)]
struct Side<'a> {
    foe: Foe,
    special: melee::Special,
    fighter: Fighter,
    groups: [i32; 7],
    player: Option<(Option<&'a Controls>, Option<Task433>)>,
}

/// Side `e`'s Opponent as `tester` tests it (0x8265CF50..0x8265D0E0, the ask 0x82660170 makes with it as the ped,
/// r9): its running result and pose; a player (+0x210 0, +0x211 set) with his 433 asks with its target, lock-on (st77),
/// given (r10, receiving the hit), IsABlock (st57) and struck (st5F) bytes, without one with the tester as his target
/// and those 0; an AI (0x8265CFBC..0x8265D034) with the tester as its target and its result's IsABlock (+0x2D), the
/// rest 0. Impulses (st7F) the player's only, with his controls. Its target's side `depth` more under it (0: none),
/// with it as that one's tester. `k` gets it.
fn side_of<R>(s: &[Side], e: usize, tester: usize, depth: usize, k: &mut dyn FnMut(&Opponent) -> R) -> R {
    let me = &s[e];
    let (target, receiving_hit, blocking, just_struck, lock_on, controls) = match me.player {
        Some((pad, Some(t))) => (t.target, t.given, t.block, t.struck, t.lock, pad),
        Some((pad, None)) => (Some(tester), false, false, false, false, pad),
        None => (Some(tester), false, me.foe.block, false, false, None),
    };
    let ask = Ask { fighter: me.fighter, groups: me.groups, loaded: &IN, receiving_hit, blocking, just_struck, target: None, lock_on,
                    impulses: me.player.is_some(), controls, setting: 0, kind: None, idle: None, pos: (me.foe.feet + Vec3::Z).into(),
                    heading: me.foe.heading, opponent_res: 0, special: me.special };
    let lies = me.foe.lies;
    let Some(t) = target else { return k(&Opponent { task: me.foe.task, lies, crouched: false, ask: &ask }) };
    let point = |kind: u32, from: [f32; 3]| s[t].foe.point(kind, from);
    let mut with = |o: Option<&Opponent>| {
        let ask = Ask { target: Some(Target { entity_type: 3, point: &point, opponent: o, special: s[t].special }), ..ask };
        k(&Opponent { task: me.foe.task, lies, crouched: false, ask: &ask })
    };
    if depth == 0 { with(None) } else { side_of(s, t, e, depth - 1, &mut |o: &Opponent| with(Some(o))) }
}


/// LT, the lock-on's control (0x960).
pub const LOCK_LT: u32 = 0x960;
/// LT fully held (0x823C4728): 238 up (0x82AA1B0F).
pub const LOCK_FULL: u8 = 238;

/// sub_823D26E0's score T = D x H + TH for Niko's melee picks (flags 0x36F3 the lock-on, 0x823D3208; 0x2EF3 the free
/// pick, 0x823CCBC8). Bit 0x1 is cleared (0x82FD1EB6 = 0), so H is the camera term alone (x 2.0 x 0.5): 0x823D19A0
/// mode 0 with flag 0x10 along the camera's forward `look` from the ped at `from` (its cone 2 pi, no pitch test):
/// 1 within 1 m, else (look . u - 1) x min(d - 1, 1) + 1 (0x82015234 = 2). D (bit 0x400) = 0.8 (0x82AA1E18) /
/// max(d, 1)^0.25; TH (bit 0x200) = `threat` (0x823D0568) x 10 (0x82AA1E14). d the distance to `at`.
pub fn pick_score(from: Vec3, look: Vec3, at: Vec3, threat: f32) -> f32 {
    let d = from.distance(at);
    let h = if d < 1.0 { 1.0 } else { (((at - from) / d).dot(look) - 1.0) * (d - 1.0).min(1.0) + 1.0 };
    0.8 / d.max(1.0).powf(0.25) * h + 10.0 * threat
}


/// A CTaskComplexMelee: on, its running result (none: the stance between moves), the quit timer, whether this frame
/// started a result.
#[derive(Default)]
pub struct Melee {
    pub on: bool,
    pub mv: Option<MeleeMove>,
    timer: f32,
    pub started: bool,
    /// Last frame's strike-bone parts in the ped's frame (tag, ends): the composite's last matrices.
    last: Vec<(u32, Vec3, Vec3)>,
    last_weapon_root: Option<(u32, Vec3)>,
    /// This frame's distance homing.
    pub home: Option<Home>,
    /// This frame started SetOpponentStopMovesHoming: the caller shuts its opponent's window (`stop_homing`).
    pub stop_foe_homing: bool,
    /// This frame's SpecialResult (`melee::special_result`): the caller puts its +0x240 bits on the ped and its opponent
    /// (`SpecialEffect::apply` on their `Quirks::melee_flags`).
    pub special_fx: melee::SpecialEffect,
    /// This frame's DisarmOpponent: the caller gives it its opponent's weapon.
    pub disarm_foe: bool,
    /// This frame's DropWeapon (0x822BF448: +0x5C 0x10): the caller drops the ped's own weapon as a pickup (0x8222B278).
    pub drop_weapon: bool,
    /// This frame's SelfDamage (def +0x78, 433's +0x54; 0x822C1D58, once: +0x5C 0x01): the caller damages the ped by it
    /// through its damage path (the dispatcher 0x8226C9A8), the attacker its result's target (+0x1C, with +0x5C 0x02: a
    /// dispatcher's reaction) with that ped's weapon, else itself.
    pub self_damage: Option<f32>,
    /// +0xF0 bit 0x20: the move it plays started with no result playing (vf20 at 0x822C7048: when its next-action step
    /// 0x822C6848 starts one, set to 0x822C0330's "none playing"). The start from on foot leaves it clear.
    /// A step that starts none clears it (0x822C7080: vf20 after its next-action step), so it holds only the frame its
    /// move starts; the ctors leave it clear (0x822C01A8 / 0x822C0240).
    pub from_idle: bool,
    /// Its velocity and last frame's place, for the SpecialTest. ponytail: the game's is the physics' (vf 0xE8); this
    /// is how far `Ctx::pos` moved over the frame.
    pub velocity: Vec3,
    pos_was: Option<Vec3>,
    /// +0xF0 bit 0x10 (the ctor's r8, 0x822C0130): it fights, so its result over, vf18 (0x822C67A8 -> 0x822C5100) puts
    /// the stance in. The player's start with no result sets it, a start with a move (r8 0) and the dispatcher's 431
    /// (0x8226D698: r8 0) leave it clear; 0x822C5100 sets it as a result ends with the player locked on a target with
    /// his fists or a melee weapon. A new task (the caller's) starts it clear.
    pub fighting: bool,
    /// This frame's `play` ended its result (its clip's end, or a result with no clip): its 433 is done.
    ended: bool,
    /// The dispatcher's request on this running task ([`Request`]), for its next step to pick; cleared by that step.
    pub request: Option<Request>,
    /// This frame's step took the request: its reaction plays (the caller's: an AI's wait, its target).
    pub took: bool,
    /// +0xF0 bit 0x08: vf20's quit test (0x822C0A90 at 0x822C0AD8, before anything else it reads) clears it and returns
    /// 3, the task's end. Nothing sets it: the ctor's byte is (r24 << 4) | r29's bit 0 (0x822C0228..0x822C0240), and the
    /// image's other stores to a task's +0xF0 byte set 0x01 (0x822C0668, 0x822BEC1C), 0x02 (0x822BE8E4), 0x04
    /// (0x822C3410), 0x10 (0x822BED14, 0x822BEDE4, 0x822C5530, 0x822C5554) or 0x20 (0x822C706C) (0x822262C8's 0x08 is
    /// a 0x50-stride table's third entry); so it stays clear, as here.
    quit: bool,
}

/// The dispatcher's request on a running CTaskComplexMelee (0x8226D5C8 -> 0x822BE930): +0xCC the attacker (`from`,
/// none for Niko), +0xD0 its result (`blow.res`), +0xF1 0x40 set and 0x80 the dispatcher's r6 1 (the blow received), the
/// way from where the blow struck to its start (`melee_task::request_way`, the dispatcher's VMX at 0x8226D5E8..0x8226D64C).
/// The task's next step makes the pick with it (vf20 0x822C6B58 -> 0x822C6848, after the stance's drift 0x822C37B8 read
/// it) and clears it (vf20 after 0x822C6848: +0xCC, +0xD0 and +0xF1 0x40 / 0x80): [`Melee::step`]. The caller keeps the
/// attacker as it is then (`blow.by`, with its running result as its `task`) and the lock-on byte (`locked`: the
/// attacker is its result's target, +0x1C, and that result's +0x20).
#[derive(Clone, Copy)]
pub struct Request {
    pub blow: Blow,
    pub from: Option<u32>,
    pub way: [f32; 3],
    pub locked: bool,
}

impl Melee {
    /// The frame: start, quit, the next action, homing (turns `heading`) and the clip (`clip` gives a loaded one by
    /// `dictionary/clip`) with its strike bones' windows; `strike` then sweeps them.
    pub fn step<'c>(&mut self, d: &Data, c: &Ctx, heading: &mut f32, dt: f32, clip: &dyn Fn(&str) -> Option<&'c Animation>, rand: &mut dyn FnMut() -> u32) {
        let dur = &|k: &str| clip(k).map(|a| a.duration);
        let request = self.request.take();
        self.took = false;
        self.started = false;
        self.home = None;
        self.stop_foe_homing = false;
        self.special_fx = Default::default();
        self.disarm_foe = false;
        self.drop_weapon = false;
        self.self_damage = None;
        if dt > 0.0 {
            self.velocity = self.pos_was.map_or(Vec3::ZERO, |w| (c.pos - w) / dt);
            self.pos_was = Some(c.pos);
        }
        // vf20's quit test first (0x822C0A90 at 0x822C0AD8): +0xF0 0x08 set, cleared, and the task ends (3, 0x822C6CC4).
        if self.on && std::mem::take(&mut self.quit) {
            (self.on, self.mv) = (false, None);
            return;
        }
        let p = &d.player;
        if let Some(me) = c.me {
            // A ped's task runs from its Charge (the caller's); the AI has no on-foot start nor quit timer.
            self.on = true;
            // The request first (0x822C6848): taken, its reaction plays; else the AI's own roll.
            if request.as_ref().is_some_and(|q| self.take_request(d, c, *heading, q, rand, dur)) {
                return self.play(d, c, heading, dt, clip);
            }
            if !me.go {
                // The roll said no (0x822C6848 starts none): vf20 clears it (0x822C7080).
                self.from_idle = false;
                return self.play(d, c, heading, dt, clip);
            }
        } else if !self.on && !(c.in_hand && (starts(&c.pad, gun_in_hand(c)) || fists_or_melee(c.weapon) && c.lock_on && c.foe.is_some())) {
            // No weapon gate (0x823CCBC8): any weapon in hand asks; not in hand, its swap weapon task instead.
            return;
        }
        let fighter = match c.me {
            Some(me) => Fighter { player: false, health: c.health, attack: me.attack, defend: me.defend, martial: me.martial, weapon: Some(held(c.weapon)) },
            None => Fighter { player: true, health: c.health, attack: p.attack, defend: p.defend, martial: p.melee_percent, weapon: Some(held(c.weapon)) },
        };
        // The sides the opponent part reads (0x8265CBE8: RequiredOpponentRes, OpponentsActionToTest...; [`side_of`]):
        // itself (0), its foe (1) and, for Niko as a ped's foe, the ped his running result is on when that's another (2).
        // Niko's side is the player's, a ped's an AI's (a ped's foe is Niko or a ped; Niko's a ped).
        let foe_player = c.me.is_some() && c.foe_pad.is_some();
        let ai = |f: &Foe| { let m = f.me.unwrap_or(BARE); Fighter { player: false, health: f.health, attack: m.attack, defend: m.defend, martial: m.martial, weapon: f.weapon } };
        let foe_fighter = |f: &Foe| if foe_player {
            Fighter { player: true, health: f.health, attack: p.attack, defend: p.defend, martial: p.melee_percent, weapon: f.weapon }
        } else {
            ai(f)
        };
        let foe_groups = |f: &Foe| if foe_player { d.player_groups } else { f.me.map_or([-1; 7], |m| m.groups) };
        // Itself as the SpecialTest reads it, as `Foe::special` has a foe.
        let yaw = *heading;
        let special = melee::Special { on_stairs: c.on_stairs, velocity: self.velocity.into(), flags_240: c.flags_240,
                                       move_from_idle: self.on && self.from_idle, point: c.pos.into(), forward: [-yaw.sin(), yaw.cos(), 0.0] };
        let base = |fighter: Fighter| Ask {
            fighter, groups: [-1; 7], loaded: &IN, receiving_hit: false, blocking: false, just_struck: false, target: None, lock_on: false, impulses: false,
            controls: None, setting: 0, kind: None, idle: None, pos: c.pos.into(), heading: *heading, opponent_res: 0, special,
        };
        let mv = self.mv.as_ref();
        let me_foe = Foe { feet: c.pos - Vec3::Z, heading: *heading, health: c.health, lies: c.lies, me: c.me, bones: c.bones,
                           weapon: Some(held(c.weapon)), struck: mv.is_some_and(|m| m.struck), ..Default::default() }.with_move(d, mv);
        // Niko's running 433 (his 431 runs; the stance is one too): its target his task's (+0x1C from +0x50, the foe), its
        // lock-on the task's +0xF0 0x02 (`Ctx::lock_on`), its flags his move's.
        let me_task = (c.me.is_none() && self.on).then(|| Task433 { target: c.foe.map(|_| 1), lock: c.lock_on, given: mv.is_some_and(|m| m.given),
                                                                   block: me_foe.block, struck: me_foe.struck });
        let me_side = Side { foe: me_foe, special, fighter, groups: c.me.map_or(d.player_groups, |m| m.groups),
                             player: c.me.is_none().then_some((Some(&c.pad.controls), me_task)) };
        let foe_task = c.foe_task.filter(|_| foe_player).map(|t| Task433 {
            target: match t.target { TaskTarget::Me => Some(0), TaskTarget::Ped(_) => Some(2), TaskTarget::Nobody => None },
            lock: t.lock, given: t.given, block: t.block, struck: t.struck,
        });
        let foe_side = c.foe.map_or(me_side, |f| Side { foe: f, special: f.special(), fighter: foe_fighter(&f), groups: foe_groups(&f),
                                                         player: foe_player.then_some((c.foe_pad.map(|p| &p.controls), foe_task)) });
        let third = match c.foe_task.filter(|_| foe_player).map(|t| t.target) {
            Some(TaskTarget::Ped(f)) => Side { foe: f, special: f.special(), fighter: ai(&f), groups: f.me.map_or([-1; 7], |m| m.groups), player: None },
            _ => me_side,
        };
        let main = match c.me {
            // The AI rows want a lock-on (RequireLockOn ai): a ped's task is made locked on by its Charge (0x82498D18 ->
            // ctor 0x822C0130 with r10 = 1, kept as +0xF0 0x02 by 0x822BE8B0 and put in each result's +0x20 by vf20),
            // and 0x822C5B88 asks with that byte (r1+0x15F), target or not.
            Some(me) => Ask { groups: me.groups, lock_on: true, ..base(fighter) },
            None => Ask { groups: d.player_groups, lock_on: c.lock_on, impulses: true, controls: Some(&c.pad.controls), ..base(fighter) },
        };
        // Its foe its target, with the foe's side under it as deep as the table's chains read (`Data::opponent_depth`).
        let Some(f) = c.foe else { return self.next_action(d, c, heading, dt, clip, rand, request, dur, main) };
        let point = |k: u32, from: [f32; 3]| f.point(k, from);
        let sides = [me_side, foe_side, third];
        side_of(&sides, 1, 0, d.opponent_depth() - 1, &mut |o: &Opponent| {
            let target = Target { entity_type: 3, point: &point, opponent: Some(o), special: f.special() };
            self.next_action(d, c, &mut *heading, dt, clip, &mut *rand, request, dur, Ask { target: Some(target), ..main })
        })
    }

    /// The rest of [`Melee::step`] with its ask (`ask`, its target's sides under it): the start from on foot, the quit
    /// timer, the request, the next action and the clip.
    #[allow(clippy::too_many_arguments)]
    fn next_action<'c>(&mut self, d: &Data, c: &Ctx, heading: &mut f32, dt: f32, clip: &dyn Fn(&str) -> Option<&'c Animation>,
                       rand: &mut dyn FnMut() -> u32, request: Option<Request>, dur: &dyn Fn(&str) -> Option<f32>, ask: Ask) {
        if !self.on {
            let Some(a) = melee::choose(&d.table, &Ask { kind: Some(h("FromOnFoot")), idle: Some(true), ..ask }, &mut *rand) else {
                // 0x822C3050's last branch: LT held with his fists or a melee weapon, the task with no result (its
                // stance, r8 1: fighting); else none (with a gun, its gun task goes on).
                if c.pad.held(LOCK_LT) && fists_or_melee(c.weapon) {
                    info!("melee: stance from on foot (LT held)");
                    (self.on, self.timer, self.from_idle, self.mv, self.fighting) = (true, 0.0, false, None, true);
                }
                return;
            };
            info!("melee: {} from on foot", d.table.results.get(d.result_of(a).unwrap_or(usize::MAX)).and_then(|r| d.clip_key(r)).unwrap_or_default());
            (self.on, self.timer, self.from_idle, self.fighting) = (true, 0.0, false, false);
            play_result(&mut self.mv, d, a, true, dur);
            return;
        }
        let r = self.mv.as_ref().map(|m| &d.table.results[m.result]);
        let block = r.is_some_and(|r| r.is_a_block);
        if c.me.is_none() && self.mv.is_none() && !c.in_hand {
            (self.on, self.mv) = (false, None);
            return;
        }
        let reset = c.lock_on && c.foe.is_some() || c.pad.held(ATTACK);
        self.timer = if reset { 0.0 } else { self.timer + dt * if block { 2.0 } else { 1.0 } };
        if c.me.is_none() && (self.mv.is_none() || block) && self.timer > QUIT {
            (self.on, self.mv) = (false, None);
            return;
        }
        let ask = Ask { blocking: block, just_struck: self.mv.as_ref().is_some_and(|m| m.struck), ..ask };
        // The request first (0x822C6848): taken, its reaction plays in place of the next action.
        if request.as_ref().is_some_and(|q| self.take_request(d, c, *heading, q, rand, dur)) {
            self.play(d, c, heading, dt, clip);
            if self.ended {
                self.next_subtask(c);
            }
            return;
        }
        let next = match (&self.mv, r) {
            (Some(m), Some(r)) if !r.is_an_idle => {
                let open = |[s, e]: [f32; 2]| if m.clip_t >= s { m.clip_t < e } else { s < m.clip_t + m.step };
                let hits = r.branches.iter().filter(|b| b.0 && m.key.is_some() && open(b.2))
                    .filter_map(|b| d.table.actions.iter().find(|a| a.name == b.1))
                    .filter(|a| melee::action_test(a, &d.table, &ask)).collect();
                melee::pick(hits, &mut *rand)
            }
            _ => melee::choose(&d.table, &Ask { kind: Some(h("Melee")), idle: Some(true), ..ask }, &mut *rand),
        };
        if let Some(a) = next {
            if self.mv.as_ref().is_none_or(|m| d.result_of(a) != Some(m.result)) {
                info!("melee: next {}", d.result_of(a).and_then(|r| d.clip_key(&d.table.results[r])).unwrap_or("stance".into()));
            }
            self.from_idle = self.mv.is_none();
            play_result(&mut self.mv, d, a, c.me.is_none(), dur);
            self.started = true;
        } else {
            self.from_idle = false;
        }
        self.play(d, c, heading, dt, clip);
        if c.me.is_none() && self.ended {
            self.next_subtask(c);
        }
    }

    /// The request's pick (0x822C6848's first branch, with +0xF1 0x40): 0x822C5B88 with r6 1 (receiving the hit), the
    /// attacker as its target (r7 +0xCC) and its result (r8 +0xD0): [`react`] with [`Pick::Request`] on itself as it
    /// is now. With a reaction the dispatcher gave playing (433's +0x5C 0x02), the action's result (0x8265C780) must be at
    /// that one's Priority or above (0x8218FB90 at 0x822C68E4, `melee_task::request_takes`); none fails. Taken, it plays
    /// as a given result (the ActionResult ctor's r8 1: +0x5C 0x02, and silent), from no result playing or not (+0xF0
    /// 0x20, 0x822C7048).
    fn take_request(&mut self, d: &Data, c: &Ctx, heading: f32, q: &Request, rand: &mut dyn FnMut() -> u32, dur: &dyn Fn(&str) -> Option<f32>) -> bool {
        let me = Foe { feet: c.pos - Vec3::Z, heading, health: c.health, me: c.me, bones: c.bones, velocity: self.velocity, flags_240: c.flags_240,
                       from_idle: self.on && self.from_idle, on_stairs: c.on_stairs, ..Default::default() };
        let r = react(d, &me, c.me.is_none(), Some(c.weapon), self.mv.as_ref(), &q.blow, false, [false; 2], Pick::Request { locked: q.locked }, rand);
        let Some(a) = r.action else { return false };
        if let Some(m) = self.mv.as_ref().filter(|m| m.given) {
            let playing = d.table.results[m.result].priority;
            if !r.result.is_some_and(|res| lsc4_combat::melee_task::request_takes(Some(playing), res.priority)) {
                return false;
            }
        }
        debug!("melee: request taken, {:?}", r.result.and_then(|r| d.clip_key(r)));
        let idle = self.mv.is_none();
        play_result(&mut self.mv, d, a, c.me.is_none(), dur);
        if let Some(m) = self.mv.as_mut() {
            (m.swing, m.given) = (false, true);
        }
        (self.from_idle, self.started, self.took) = (idle, true, true);
        true
    }

    /// The player's vf18 (0x822C67A8 -> 0x822C5100 with 0, its +0xF0 0x04 clear) as its result ends: fighting
    /// (+0xF0 0x10), the stance; else, locked on a target (+0x50 and +0xF0 0x02) with his fists or a melee weapon, it
    /// fights from now (0x822C5520) and takes the stance; else no subtask (0x822C5540: 0x10 still clear): the melee ends.
    /// +0xF0 0x04 is set only by its abort (vf5 0x822C3328 at 0x822C340C, the subtask let go), and an aborted task ends.
    fn next_subtask(&mut self, c: &Ctx) {
        if !self.fighting && c.lock_on && c.foe.is_some() && fists_or_melee(c.weapon) {
            self.fighting = true;
        }
        if !self.fighting {
            debug!("melee: over with its move");
            (self.on, self.mv) = (false, None);
        }
    }

    /// The opponent's SetOpponentStopMovesHoming (0x822BF5C0): its running result's window shuts (+0x5E 0x80 off).
    pub fn stop_homing(&mut self) {
        if let Some(m) = self.mv.as_mut() {
            m.window = false;
        }
    }

    /// The running result's frame: the strike window, homing, the clip and its strike bones' windows.
    fn play<'c>(&mut self, d: &Data, c: &Ctx, heading: &mut f32, dt: f32, clip: &dyn Fn(&str) -> Option<&'c Animation>) {
        self.ended = false;
        let Some(m) = self.mv.as_mut() else { return };
        let r = &d.table.results[m.result];
        // The strike window and homing (0x822C1F58), on the clip's time and events before this frame's step.
        let paired = |bit| m.key.as_deref().and_then(clip).is_some_and(|a| !a.events.is_empty() && a.count_between(bit, 0.0, 1.0) == 2);
        let (a, b) = (paired(0x200), paired(0x400));
        if m.clip_t > r.stop_time || r.stop_on_crit_frame && m.events & 0x10 != 0
            || (a || b) && (m.struck || a && m.was[0] || b && m.was[1]) {
            m.window = false;
        }
        if let Some(h) = homing(r, c.foe, c.pos, heading, m.window, m.first, dt) {
            self.home = Some(h);
        }
        m.first = false;
        // SpecialResult (0x822C4768 -> 0x822BF5C0, `melee::special_result`), once (+0x5D 0x80) while its clip plays: the
        // owner's and its opponent ped's +0x240 bits (the caller's: `special_fx`), SetStopMovesHoming shuts its own window
        // and SetOpponentStopMovesHoming (every dodge's) its opponent's (+0x5E 0x80), so that homing stops.
        // Set / UnsetSlowMoGameTime put the game's time scale (`slow_mo`).
        if m.key.is_some() && !m.special {
            m.special = true;
            let fx = melee::special_result(r.special_result);
            slow_mo(&fx);
            if fx.stop_homing {
                m.window = false;
            }
            (self.special_fx, self.stop_foe_homing) = (fx, fx.stop_opponent_homing);
        }
        // DisarmOpponent (def +0x7D, +0x5C 0x08; 0x822C4768 -> 0x822BF448): with a target (+0x1C) and locked on it
        // (+0x20: the task's +0xF0 0x02, which vf20 puts there each frame, 0x822C6C84; `Ctx::lock_on`), at its clip's
        // 0x200 event (its player's +0x74), or at once with no clip (0x822BF4CC..0x822BF524), the opponent ped's weapon
        // is taken. DropWeapon (+0x5C 0x10): the same at the 0x2000 event, the owner drops its own
        // weapon; only stun_punch_A_Res, a reaction, has it (Niko's and a ped's reactions both play here).
        self.disarm_foe = r.disarm_opponent && c.foe.is_some() && c.lock_on && (m.key.is_none() || m.events & 0x200 != 0);
        self.drop_weapon = r.drop_weapon && (m.key.is_none() || m.events & 0x2000 != 0);
        // SelfDamage (0x822C1D58): its amount (> 0) at the clip's 0x200 event (at once with no clip), once.
        if r.self_damage > 0.0 && !m.hurt && (m.key.is_none() || m.events & 0x200 != 0) {
            m.hurt = true;
            self.self_damage = Some(r.self_damage);
        }
        if m.key.is_none() {
            // A result with no clip is done after its frame.
            (self.mv, self.ended) = (None, true);
            return;
        }
        m.step = dt * m.rate;
        let from = m.phase();
        m.clip_t += m.step;
        if m.clip_t >= m.dur {
            if r.is_loop && m.dur > 0.0 {
                m.clip_t %= m.dur;
            } else {
                (self.mv, self.ended) = (None, true);
                return;
            }
        }
        // The strike bones' windows (0x822C5E90): this step's clip events toggle them.
        let ev = m.key.as_deref().and_then(clip).map_or(0, |a| a.advance(from, m.phase(), false, false).0);
        m.events = ev;
        if !r.is_an_idle && !r.is_a_block {
            for (n, bit) in [0x200, 0x400].into_iter().enumerate() {
                if ev & bit != 0 {
                    m.live[n] = !m.live[n];
                    m.was[n] |= m.live[n];
                }
            }
        }
    }

    /// The strike (0x822C5E90 / 0x822C22F8) for the ped at `feet` facing `heading`: `part` gives a strike bone's
    /// part by tag ([`bound_axis`]: its axis's ends and its centroid radius, GTA space, as drawn now); `weapon` the held weapon's bbox axis (its min and max
    /// ends, GTA space, as drawn now), the model's radius and where its origin is on the axis (0-1); `bodies` what
    /// it can meet (an id kept across frames,
    /// hit capsules: ends, radius, bone tag). Gives the newly struck: id, the capsule's bone tag, the point and
    /// OnHitDamage. Call it every frame the task runs, after the pose it reads is drawn.
    pub fn strike(&mut self, d: &Data, feet: Vec3, heading: f32, part: &dyn Fn(u32) -> Option<(Vec3, Vec3, f32, f32)>,
                  weapon: Option<(Vec3, Vec3, f32, f32)>, bodies: &[(u64, &[crate::geometry::Bound])]) -> Vec<(u64, u16, Vec3, f32, u32, Vec3, f32)> {
        let (turn, back) = (Quat::from_rotation_z(heading), Quat::from_rotation_z(-heading));
        let last = std::mem::take(&mut self.last);
        let last_weapon_root = self.last_weapon_root.take();
        let Some(m) = self.mv.as_mut() else { return vec![] };
        let r = &d.table.results[m.result];
        // This frame's parts in the ped's frame; the capsules each live point sweeps.
        let mut sweeps = vec![];
        for (n, &(some, tag, radius)) in r.strike_bones.iter().enumerate() {
            let Some((a, b, own, thickness)) = part(tag).filter(|_| some) else { continue };
            let radius = radius.min(thickness);
            // The part, then (R_HAND) the held weapon, keyed apart in `last`.
            let held = weapon.filter(|_| tag == HANDS[1]).map(|(a, b, big, origin)| (WEAPON_KEY, a, b, (big, origin)));
            for (key, a, b, kind) in [(tag, a, b, (own, f32::NAN))].into_iter().chain(held) {
                let weapon = key == WEAPON_KEY;
                let (a, b) = if weapon {
                    self.last_weapon_root = Some((m.action, feet));
                    (a, b)
                } else {
                    (back * (a - feet), back * (b - feet))
                };
                self.last.push((key, a, b));
                let live = m.live[n] && !r.is_an_idle && !r.is_a_block;
                let Some(&(_, la, lb)) = last.iter().find(|l| l.0 == key).filter(|_| live) else { continue };
                // The held model moves with the ped and the wrist. Keep both
                // world endpoints, but never sweep across a discontinuity.
                if weapon && !last_weapon_root.is_some_and(|(action, root)| {
                    action == m.action && feet.distance_squared(root) < TOO_CLOSE * TOO_CLOSE
                }) { continue; }
                let at = match kind {
                    (own, o) if o.is_nan() => points(a, b, own, tag, radius),
                    (big, origin) => weapon_points(a.distance(b), big, origin, radius),
                };
                for s in at {
                    let now = |p: Vec3, q: Vec3| {
                        let at = p.lerp(q, s);
                        if weapon { at } else { feet + turn * at }
                    };
                    sweeps.push((now(la, lb), now(a, b), radius, tag));
                }
            }
        }
        let mut out = vec![];
        for &(id, caps) in bodies {
            // Preserve authored sample order, but take the first struck body
            // surface along that sweep, not the most deeply penetrated part.
            let met = sweeps.iter().find_map(|&(p, q, r, striker)| crate::geometry::first_contact(caps,p,q,r).map(|(gap,tag,at,normal)|(gap,tag,at,striker,normal)));
            let Some((gap, tag, at, striker, normal)) = met else { continue };
            if m.hits.contains(&id) {
                continue;
            }
            if m.hits.len() < HIT_LIST {
                m.hits.push(id);
            }
            m.struck = true;
            out.push((id, tag, at, r.on_hit_damage, striker, normal, gap));
        }
        out
    }
}

/// A part's strike points (0x822C22F8) as fractions from `a` to `b`, its bound's longest axis ([`bound_axis`]), a
/// hand's 0.06 m short at its start; one point at its middle up to 2r long, else points from r in to r short of its
/// end; a radius past the bound's centroid radius `own` (+0x08) takes its centre.
fn points(a: Vec3, b: Vec3, own: f32, tag: u32, r: f32) -> Vec<f32> {
    let len = a.distance(b);
    if !(len > 0.0) || r > own {
        return vec![0.5];
    }
    along(if HANDS.contains(&tag) { HAND_SHORT } else { 0.0 }, len, len, r)
}

/// A bound's strike axis (0x822C22F8 at 0x822C288C): its bbox's longest extent (x past both others, else y past
/// both, else z) through its origin, from the bbox's min to its max on it, as `frame` places them; and its
/// centroid radius `centroid`, the bound's own +0x08 (`lsc4_combat::ragdoll::Part::centroid`, read at 0x822C24D4). A
/// fragment capsule (`radius`, `half` its segment's half along y) or a box (`boxed` half extents), both about the
/// frame's origin.
pub fn bound_axis(frame: impl Fn(Vec3) -> Vec3, radius: f32, half: f32, boxed: Option<Vec3>, centroid: f32) -> (Vec3, Vec3, f32) {
    let e = boxed.unwrap_or(Vec3::new(radius, half + radius, radius));
    let axis = if e.x > e.y && e.x > e.z { Vec3::X * e.x } else if e.y > e.x && e.y > e.z { Vec3::Y * e.y } else { Vec3::Z * e.z };
    (frame(-axis), frame(axis), centroid)
}

/// What a blow meets of a fragment part: a capsule's own segment and radius, as `frame` places it; a box as the
/// capsule along its longest extent with its middle one as the radius.
/// ponytail: the game's level test meets the box itself (0x82369158).
pub fn frag_capsule(frame: impl Fn(Vec3) -> Vec3, radius: f32, half: f32, boxed: Option<Vec3>) -> (Vec3, Vec3, f32) {
    match boxed {
        None => (frame(Vec3::Y * -half), frame(Vec3::Y * half), radius),
        Some(e) => {
            let mut v = [(e.x, Vec3::X), (e.y, Vec3::Y), (e.z, Vec3::Z)];
            v.sort_by(|a, b| a.0.total_cmp(&b.0));
            let ((mid, _), (big, axis)) = (v[1], v[2]);
            (frame(axis * -(big - mid)), frame(axis * (big - mid)), mid)
        }
    }
}

/// A weapon model's bbox axis (0x822C22F8): its longest (x past both others, else y past both, else z) through the
/// model's origin, from the bbox's min to its max on it, and where the origin is on it (0-1).
pub fn weapon_axis(min: Vec3, max: Vec3) -> (Vec3, Vec3, f32) {
    let e = (max - min).abs();
    let axis = if e.x > e.y && e.x > e.z { Vec3::X } else if e.y > e.x && e.y > e.z { Vec3::Y } else { Vec3::Z };
    let (lo, hi) = (min.dot(axis), max.dot(axis));
    (axis * lo, axis * hi, if hi > lo { -lo / (hi - lo) } else { 0.5 })
}

/// A held weapon's strike points (0x822C22F8's second pass) as fractions of its bbox axis `seg` long: its origin
/// (at `origin`) for a strike radius `r` past the model's `big`, else from 0.06 m in at its min end.
fn weapon_points(seg: f32, big: f32, origin: f32, r: f32) -> Vec<f32> {
    if seg <= 0.0 || r > big {
        return vec![origin];
    }
    along(HAND_SHORT, seg, seg, r)
}

/// Points from `lo` to `hi` along a segment `seg` long, as fractions of it: one at the middle up to 2r long, else
/// from r in to r short of the end.
fn along(lo: f32, hi: f32, seg: f32, r: f32) -> Vec<f32> {
    let len = hi - lo;
    let at = |x: f32| x / seg;
    if len <= 2.0 * r {
        return vec![at(lo + len * 0.5)];
    }
    let gap = 2.0 * r * (1.0 - SPACING * SPACING).sqrt();
    let n = (((len - 2.0 * r) / gap).ceil() as usize + 1).min(MOST_POINTS);
    (0..n).map(|i| at(lo + r + (len - 2.0 * r) * i as f32 / (n - 1) as f32)).collect()
}

/// The closest points of segments p-q and a-b.
fn closest(p: Vec3, q: Vec3, a: Vec3, b: Vec3) -> (Vec3, Vec3) {
    let (d1, d2, w) = (q - p, b - a, p - a);
    let (aa, ee, f) = (d1.length_squared(), d2.length_squared(), d2.dot(w));
    let (s, t) = if aa <= 1e-12 && ee <= 1e-12 {
        (0.0, 0.0)
    } else if aa <= 1e-12 {
        (0.0, (f / ee).clamp(0.0, 1.0))
    } else {
        let c = d1.dot(w);
        if ee <= 1e-12 {
            ((-c / aa).clamp(0.0, 1.0), 0.0)
        } else {
            let bb = d1.dot(d2);
            let den = aa * ee - bb * bb;
            let s = if den > 1e-12 { ((bb * f - c * ee) / den).clamp(0.0, 1.0) } else { 0.0 };
            let t = (bb * s + f) / ee;
            if t < 0.0 {
                ((-c / aa).clamp(0.0, 1.0), 0.0)
            } else if t > 1.0 {
                (((bb - c) / aa).clamp(0.0, 1.0), 1.0)
            } else {
                (s, t)
            }
        }
    };
    (p + d1 * s, a + d2 * t)
}

/// A strike bone's part on a skeleton by tag (`at` a bone's position by tag, GTA space) as [`bound_axis`] gives
/// one: the viewer's hit capsule for it (peds.rs PARTS: to the next bone, else 0.18 m on along the line from the one
/// before), its caps on.
/// ponytail: the stand-in without a ragdoll, not the fragment's bound; arms, legs and head only (the pelvis's next
/// bone has no tag); a foot runs 0.18 m on from the calf, not to the toe.
pub fn part(tag: u32, at: &dyn Fn(u16) -> Option<Vec3>) -> Option<(Vec3, Vec3, f32)> {
    // (tag, the other bone, it is the one before, radius)
    const LINES: [(u16, u16, bool, f32); 13] = [
        (0x4C1, 0x4C2, false, 0.07), (0x4C2, 0x4C3, false, 0.06), (0x4C3, 0x4C2, true, 0.05),
        (0x4C8, 0x4C9, false, 0.07), (0x4C9, 0x4D0, false, 0.06), (0x4D0, 0x4C9, true, 0.05),
        (0x1A2, 0x1A3, false, 0.09), (0x1A3, 0x1A4, false, 0.07), (0x1A4, 0x1A3, true, 0.06),
        (0x1A7, 0x1A8, false, 0.09), (0x1A8, 0x1A9, false, 0.07), (0x1A9, 0x1A8, true, 0.06),
        (0x4B5, 0x4B4, true, 0.12),
    ];
    let &(bone, other, before, radius) = LINES.iter().find(|l| l.0 as u32 == tag)?;
    let (a, o) = (at(bone)?, at(other)?);
    Some(capsule_axis(a, if before { a + (a - o).normalize_or_zero() * 0.18 } else { o }, radius))
}

/// A capsule `a`-`b` of `radius` as [`bound_axis`] gives it: the segment with its caps, its centroid radius.
pub fn capsule_axis(a: Vec3, b: Vec3, radius: f32) -> (Vec3, Vec3, f32) {
    let d = (b - a).normalize_or_zero() * radius;
    (a - d, b + d, a.distance(b) * 0.5 + radius)
}

/// The blow a hit reaction answers: the attacker, whether it is the player, its result's hash (0 for none) and that
/// result's clip time and this frame's step.
#[derive(Clone, Copy)]
pub struct Blow {
    pub by: Foe,
    pub player: bool,
    pub res: u32,
    pub clip: Option<(f32, f32)>,
}

/// The dispatcher's HitReaction pick for a victim (0x8226CDC8, `melee::hit_reaction`): `me` the victim, the player
/// or not, its weapon, its running result (blocking when IsABlock, st57); the attacker as its target with its result
/// as `opponent_res`. `gate` the NM gate's answer, `tasks` running 1900 / 1904.
/// A ped's stats and groups are its own (`Foe::me`), a bare AI's without them. Not locked on: st6F is the
/// dispatcher's 0 (r15, at 0x8226CE04).
/// [`Pick::Forced`]: the attacker's result has ForceImmediateReaction, so the ActionResult ctor's own pick instead
/// (site A 0x822c4598): Type HitReaction without the AvailableFromIdle or MustBeRecievingHit checks, no NM gate; its
/// action's result plays at once (a new CTaskComplexMelee, 0x822C0130). Its stack bytes (0x822C4528..0x822C4598, read
/// by 0x8265FF00 at r1+0x1D7 / 0x1DF / 0x1EF / 0x1F7): st57 (blocking), st5F (struck) and st6F (locked on) are r30, 0
/// (0x822C4124); st77 (impulses) is 1 for the player victim (+0x210 0, +0x211 set: r23, 1), whose controls 0x8265FF00
/// then reads (0x823CF688, `pad`), else 0.
/// [`Pick::Request`]: the victim's melee (431) already runs, so the dispatcher's request on it (0x822BE930,
/// `melee_task::request_way`) and its next-action step's pick (0x822C6848 -> 0x822C5B88): Type HitReaction (0x830B70C8),
/// AvailableFromIdle, receiving the hit, its own result's block (st57) and struck (+0x5E 0x40, st5F) bytes, locked on
/// (st6F) when the attacker is its result's target and that result is; no NM gate (the dispatcher's came first). Made on
/// the victim's next step ([`Melee::step`] with its [`Request`]), its opponent the attacker as it is then: its running
/// result (`blow.by.task`, read by OpponentsActionToTest) and the blow's result as `opponent_res` (+0xD0).
#[allow(clippy::too_many_arguments)]
pub fn react<'t>(d: &'t Data, me: &Foe, player: bool, weapon: Option<&WeaponView>, mine: Option<&MeleeMove>, blow: &Blow,
                 gate: bool, tasks: [bool; 2], pick: Pick<'_>, rand: &mut dyn FnMut() -> u32) -> melee::Reaction<'t> {
    let p = &d.player;
    let fighter = |f: &Foe, player: bool| if player {
        Fighter { player, health: f.health, attack: p.attack, defend: p.defend, martial: p.melee_percent, weapon: f.weapon }
    } else {
        let m = f.me.unwrap_or(BARE);
        Fighter { player, health: f.health, attack: m.attack, defend: m.defend, martial: m.martial, weapon: f.weapon }
    };
    let base = |f: &Foe, player: bool| Ask {
        fighter: fighter(f, player), groups: if player { d.player_groups } else { f.me.map_or([-1; 7], |m| m.groups) }, loaded: &IN, receiving_hit: false, blocking: false, just_struck: false,
        target: None, lock_on: false, impulses: false, controls: None, setting: 0, kind: None, idle: None, pos: (f.feet + Vec3::Z).into(),
        heading: f.heading, opponent_res: 0, special: f.special(),
    };
    let by_ask = base(&blow.by, blow.player);
    let task = match pick {
        Pick::Request { .. } => blow.by.task,
        _ => (blow.res != 0).then_some((blow.res, blow.clip)),
    };
    let opponent = Opponent { task, lies: blow.by.lies, crouched: false, ask: &by_ask };
    let point = |k: u32, from: [f32; 3]| blow.by.point(k, from);
    let mut ask = base(me, player);
    ask.fighter.weapon = weapon.map(held);
    ask.blocking = mine.is_some_and(|m| d.table.results[m.result].is_a_block);
    ask.target = Some(Target { entity_type: 3, point: &point, opponent: Some(&opponent), special: blow.by.special() });
    ask.opponent_res = blow.res;
    let ask = match pick {
        Pick::Dispatcher => return melee::hit_reaction(&d.table, &ask, gate, tasks, rand),
        Pick::Forced { pad } => Ask { kind: Some(h("HitReaction")), blocking: false, impulses: player, controls: pad.filter(|_| player), ..ask },
        Pick::Request { locked } => Ask { kind: Some(h("HitReaction")), idle: Some(true), receiving_hit: true,
                                          just_struck: mine.is_some_and(|m| m.struck), lock_on: locked, ..ask },
    };
    let action = melee::choose(&d.table, &ask, rand);
    melee::Reaction { action, result: action.and_then(|a| d.result_of(a)).map(|r| &d.table.results[r]), ragdoll: false }
}

/// Which pick [`react`] makes for a victim.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Pick<'p> {
    /// The dispatcher's (0x8226CDC8), NM's or a reaction on its feet.
    Dispatcher,
    /// The attacker's ForceImmediateReaction (site A 0x822c4598); `pad` the player victim's controls (0x823CF688: this
    /// frame's pad), whose impulses it asks.
    Forced { pad: Option<&'p Controls> },
    /// The request on its running melee; `locked` its result's lock-on byte (433's +0x20) when the attacker is that
    /// result's target, else false.
    Request { locked: bool },
}


#[cfg(test)]
mod upstream_regressions {
use super::*;
use lsc4_combat::actiontable::ActionTable;
fn init_assets() {
    static INIT: std::sync::Once = std::sync::Once::new();
    INIT.call_once(|| {
        let root=std::path::PathBuf::from(std::env::var_os("NM_TEST_ASSETS").expect("NM_TEST_ASSETS must name the owned test assets"));
        let bytes=std::fs::read(root.join("fred.xrt")).expect("owned asset missing");
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
    });
}
    #[test]
    fn the_forced_pick_asks_the_players_impulses() {
        init_assets();
        // Site A's ask (0x822C4528..0x822C4598): the player victim's impulses (st77) on his pad, never blocking (st57 0).
        // One row wants attack held (impulse 1, state 1), one MustBeBlocking.
        let act = |name: &str, res: &str| Action {
            name: h(name), priority: 4, bias: 1.0, kind: h("HitReaction"), weapon_class: h("any"), ranges: [[0.0, 1000.0]; 4],
            opponent_res: h("shove_back"), opponent_pose: 1, move_: h(res), require_lock_on: 3, impulse_op: 1, ..Default::default()
        };
        let mut held = act("held", "held_res");
        held.impulses[0] = lsc4_combat::actiontable::Impulse { check: true, impulse: 1, state: 1, range: [0.0, 1.0], interval: 0.0 };
        let block = Action { flags: [false, false, true, false], ..act("block", "block_res") };
        let res = |name: &str, is_a_block: bool| ResultDef { name: h(name), anim_group: -1, anim: -1, is_a_block, ..Default::default() };
        let d = Data { table: ActionTable { actions: vec![held, block], results: vec![res("held_res", false), res("block_res", false), res("guard", true)], completed: true },
                       groups: vec![], player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let foe = |x: f32| Foe { feet: Vec3::new(x, 0.0, 0.0), health: 200.0, ..Default::default() };
        let blow = Blow { by: foe(1.0), player: false, res: h("shove_back"), clip: Some((0.15, 0.0)) };
        let pad = |buttons: u32| {
            let mut p = Pad::default();
            p.feed(buttons, [0x80; 2], [0; 2], 0);
            p.feed(buttons, [0x80; 2], [0; 2], 33);
            p
        };
        let (attack, idle) = (pad(0x20), pad(0));
        let mut guard = None;
        play_result(&mut guard, &d, &Action { move_: h("guard"), ..Default::default() }, true, &|_| None);
        let pick = |player: bool, pad: &Pad, mine: Option<&MeleeMove>| react(&d, &foe(0.0), player, None, mine, &blow, false, [false; 2],
                                                                            Pick::Forced { pad: Some(&pad.controls) }, &mut || 0).action.map(|a| a.name);
        // The player: the row passes only with attack held on his pad; his own block isn't asked, so never the block row.
        assert_eq!(pick(true, &attack, None), Some(h("held")));
        assert_eq!(pick(true, &idle, None), None);
        assert_eq!(pick(true, &idle, guard.as_ref()), None);
        assert_eq!(pick(true, &attack, guard.as_ref()), Some(h("held")));
        // A ped (st77 0): no impulses asked, its pad (none) unread.
        assert_eq!(pick(false, &idle, guard.as_ref()), Some(h("held")));
    }

    #[test]
    fn opponents_action_to_test_reads_both_sides() {
        init_assets();
        // A ped's action whose OpponentsActionToTest names a Niko action wanting, as its RequiredOpponentRes, the result
        // this ped runs: Niko's side has this ped as his target (his running result's +0x1C) and this ped's side under
        // it (0x8265CBE8, two deep: the table's chain).
        let row = |name: &str| Action { name: h(name), priority: 4, bias: 1.0, kind: h("Melee"), weapon_class: h("any"), ranges: [[0.0, 1000.0]; 4],
                                        available_from_idle: true, opponent_pose: 1, require_lock_on: 3, require_target: 0, ..Default::default() };
        // The ped's rows AI only (AIPlayerfilter 2), Niko's the player's (1); all need a target (RequireTarget all), so the
        // pick's try without one (`melee::choose`) takes none.
        let hook = Action { opponents_action_to_test: h("niko_counter"), move_: h("hook_res"), ai_player_filter: 2, ..row("ped_hook") };
        let counter = Action { opponent_res: h("guard_res"), ai_player_filter: 1, ..row("niko_counter") };
        // Needs a ped target (RequiredTargetType a ped).
        let aimed = Action { required_target_type: 2, ai_player_filter: 1, ..row("niko_aimed") };
        let looks = Action { opponents_action_to_test: h("niko_aimed"), move_: h("hook_res"), ai_player_filter: 2, ..row("ped_looks") };
        let res = |name: &str| ResultDef { name: h(name), anim_group: -1, anim: -1, is_an_idle: true, ..Default::default() };
        let data = |actions: Vec<Action>| Data { table: ActionTable { actions, results: vec![res("hook_res"), res("guard_res"), res("other_res")], completed: true },
                                                 groups: vec![], player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let (pad, niko_pad) = (Pad::default(), Pad::default());
        let weapon = WeaponView { name: "UNARMED".into(), ..Default::default() };
        let niko = Foe { feet: Vec3::new(1.0, 0.0, 0.0), health: 200.0, ..Default::default() };
        let me = Me { attack: 1.0, defend: 1.0, martial: 1.0, groups: [-1; 7], go: true, moves: -1 };
        let task = |target| FoeTask { target, lock: true, given: false, block: false, struck: false };
        // The ped's pick from its idle result `running`, Niko's task `foe_task`: whether it moves (its one action).
        let pick = |d: &Data, running: &str, foe_task: Option<FoeTask>| {
            let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::Z, health: 200.0, weapon: &weapon, foe: Some(niko), me: Some(me), lock_on: true,
                          flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: Some(&niko_pad), foe_task };
            let mut m = Melee { on: true, ..Default::default() };
            play_result(&mut m.mv, d, &Action { move_: h(running), ..Default::default() }, false, &|_| None);
            m.step(d, &c, &mut 0.0, 1.0 / 30.0, &|_| None, &mut || 0);
            m.started
        };
        let d = data(vec![hook.clone(), counter.clone()]);
        assert_eq!(d.opponent_depth(), 2);
        assert!(pick(&d, "guard_res", Some(task(TaskTarget::Me))));
        assert!(!pick(&d, "other_res", Some(task(TaskTarget::Me))));
        // His running result on another ped: that one's side (an AI's, its result none) is under his.
        let other = Foe { feet: Vec3::new(0.0, 2.0, 0.0), health: 200.0, ..Default::default() };
        assert!(!pick(&d, "guard_res", Some(task(TaskTarget::Ped(other)))));
        let d = data(vec![looks.clone(), aimed.clone()]);
        // His melee task not running (no 433): this ped is his target all the same (0x8265CBE8's r4); his result on none,
        // no target.
        assert!(pick(&d, "guard_res", None));
        assert!(!pick(&d, "guard_res", Some(task(TaskTarget::Nobody))));
        assert!(pick(&d, "guard_res", Some(task(TaskTarget::Ped(other)))));
        // Niko's own pick: a ped's side has him, the tester, as its target.
        let aimed_ai = Action { ai_player_filter: 2, ..aimed };
        let niko_looks = Action { ai_player_filter: 1, ..looks };
        let d = data(vec![niko_looks, aimed_ai]);
        let ped = Foe { feet: Vec3::new(1.0, 0.0, 0.0), health: 200.0, me: Some(me), ..Default::default() };
        let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::Z, health: 200.0, weapon: &weapon, foe: Some(ped), me: None, lock_on: true,
                      flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
        let mut m = Melee { on: true, ..Default::default() };
        m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &|_| None, &mut || 0);
        assert!(m.started);
    }

    #[test]
    fn the_request_is_picked_on_the_next_step() {
        init_assets();
        // An AI ped's 431 holds the dispatcher's request (its roll said no move this frame): its step picks the reaction
        // (Type HitReaction, receiving the hit) and plays it as a given result, from no result playing.
        let act = |name: &str, res: &str| Action {
            name: h(name), priority: 4, bias: 1.0, kind: h("HitReaction"), weapon_class: h("any"), ranges: [[0.0, 1000.0]; 4],
            available_from_idle: true, flags: [false, true, false, false], opponent_res: h("shove_back"),
            opponent_pose: 1, move_: h(res), require_lock_on: 3, ..Default::default()
        };
        let res = |name: &str, priority: i32| ResultDef { name: h(name), anim_group: -1, anim: -1, priority, ..Default::default() };
        let d = Data { table: ActionTable { actions: vec![act("recoil", "recoil_res")], results: vec![res("recoil_res", 5), res("big_res", 9)], completed: true },
                       groups: vec![], player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let foe = Foe { feet: Vec3::new(1.0, 0.0, 0.0), health: 200.0, task: Some((h("shove_back"), Some((0.15, 0.0)))), ..Default::default() };
        let blow = Blow { by: foe, player: false, res: h("shove_back"), clip: Some((0.15, 0.0)) };
        let me = Me { attack: 1.0, defend: 1.0, martial: 1.0, groups: [-1; 7], go: false, moves: -1 };
        let weapon = WeaponView { name: "UNARMED".into(), ..Default::default() };
        let pad = Pad::default();
        let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::Z, health: 200.0, weapon: &weapon, foe: Some(foe), me: Some(me), lock_on: false,
                      flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
        let request = Request { blow, from: None, way: [-1.0, 0.0, 0.0], locked: false };
        let clip = |_: &str| None;
        let mut m = Melee { on: true, request: Some(request), ..Default::default() };
        m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0);
        // From no result playing (+0xF0 0x20); its result has no clip, so it is done after its frame.
        assert!(m.took && m.request.is_none() && m.from_idle && m.started && m.mv.is_none());
        // Over a given reaction of a higher Priority it is not let in; the request goes all the same.
        let mut m = Melee { on: true, request: Some(request), ..Default::default() };
        play_result(&mut m.mv, &d, &Action { move_: h("big_res"), ..Default::default() }, false, &|_| None);
        m.mv.as_mut().unwrap().given = true;
        m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0);
        assert!(!m.took && m.request.is_none());
        // Without one, nothing is picked for it.
        let mut m = Melee { on: true, ..Default::default() };
        m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0);
        assert!(!m.took && m.mv.is_none());
    }

    #[test]
    fn the_window_shuts_on_the_crit_frame_and_a_begun_strike() {
        init_assets();
        let run = |crit: bool, events: Vec<u32>| {
            let r = ResultDef { stop_time: 9.0, stop_on_crit_frame: crit, strike_bones: [(true, 0x4C3, 0.06), (false, 0, 0.0)], ..Default::default() };
            let d = Data { table: ActionTable { actions: vec![], results: vec![r], completed: true }, groups: vec![],
                           player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
            let a = Animation { name: String::new(), hash: 0, frames: 31, duration: 1.0, tracks: vec![], mover_rotation: None, events };
            let clip = |_: &str| Some(&a);
            let (pad, weapon) = (Pad::default(), WeaponView::default());
            let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::ZERO, health: 200.0, weapon: &weapon, foe: None, me: None, lock_on: false, flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
            let mut m = Melee { on: true, mv: Some(MeleeMove { action: 0, result: 0, key: Some("g/c".into()), clip_t: 0.0, dur: 1.0, rate: 1.0, step: 0.0,
                                                               struck: false, forced: false, swing: false, first: true, window: true, live: [false; 2], was: [false; 2], hits: vec![], events: 0, special: false, hurt: false, given: false }),
                                ..Default::default() };
            // The frame the window shut on.
            (1..30).find(|_| { m.play(&d, &c, &mut 0.0, 1.0 / 30.0, &clip); !m.mv.as_ref().unwrap().window })
        };
        let mut ev = vec![0; 31];
        (ev[10], ev[20]) = (0x200, 0x200);
        // The strike bone goes live at frame 10: the next frame shuts the window.
        assert_eq!(run(false, ev.clone()), Some(11));
        // One 0x200 frame alone isn't a pair: open till the end.
        ev[20] = 0;
        assert_eq!(run(false, ev.clone()), None);
        // The crit frame shuts it with stopOnCritFrame only.
        ev[5] = 0x10;
        assert_eq!(run(true, ev.clone()), Some(6));
        assert_eq!(run(false, ev), None);
    }

    #[test]
    fn a_move_from_on_foot_ends_with_its_clip() {
        init_assets();
        // A move that isn't fighting (+0xF0 0x10 clear): its clip over, vf18 (0x822C5100) gives no stance and the melee
        // ends, unless he is locked on a target with his fists or a melee weapon, which then fights (0x10 set).
        let r = ResultDef { stop_time: 9.0, ..Default::default() };
        let d = Data { table: ActionTable { actions: vec![], results: vec![r], completed: true }, groups: vec![],
                       player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let a = Animation { name: String::new(), hash: 0, frames: 31, duration: 1.0, tracks: vec![], mover_rotation: None, events: vec![] };
        let clip = |_: &str| Some(&a);
        let pad = Pad::default();
        let fists = WeaponView { name: "UNARMED".into(), damage_type: "MELEE".into(), ..Default::default() };
        let pistol = WeaponView { name: "PISTOL".into(), damage_type: "BULLET".into(), ..Default::default() };
        let mv = MeleeMove { action: 0, result: 0, key: Some("g/c".into()), clip_t: 0.95, dur: 1.0, rate: 1.0, step: 0.0, struck: false, forced: false, swing: false,
                             first: true, window: true, live: [false; 2], was: [false; 2], hits: vec![], events: 0, special: false, hurt: false, given: false };
        let run = |weapon: &WeaponView, lock_on: bool, foe: Option<Foe>, fighting: bool| {
            let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::ZERO, health: 200.0, weapon, foe, me: None, lock_on, flags_240: 0, on_stairs: false,
                          bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
            let mut m = Melee { on: true, mv: Some(mv.clone()), fighting, ..Default::default() };
            // The clip's last 0.05 s ends on the second frame.
            let on: Vec<bool> = (0..4).map(|_| { m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0); m.on }).collect();
            (on, m.fighting)
        };
        assert_eq!(run(&fists, false, None, false), (vec![true, false, false, false], false));
        assert_eq!(run(&fists, true, Some(Foe::default()), false), (vec![true; 4], true));
        assert_eq!(run(&pistol, true, Some(Foe::default()), false).0, [true, false, false, false]);
        assert_eq!(run(&fists, true, None, false).0, [true, false, false, false]);
        // Fighting already (the LT stance's start): the stance after it, the quit timer then runs.
        assert_eq!(run(&pistol, false, None, true), (vec![true; 4], true));
    }

    #[test]
    fn the_players_quit() {
        init_assets();
        let d = Data { table: ActionTable { actions: vec![], results: vec![], completed: true }, groups: vec![],
                       player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let clip = |_: &str| None;
        let weapon = WeaponView::default();
        let (idle, mut attack) = (Pad::default(), Pad::default());
        attack.controls.values[index(ATTACK)].value = 0xFF;
        let ctx = |pad, in_hand, lock_on, foe| Ctx { in_hand, pad, pos: Vec3::ZERO, health: 200.0, weapon: &weapon, foe, me: None, lock_on,
                                                    flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
        // Seconds (at 30 fps) till his melee ends, nothing playing.
        let run = |c: &Ctx| {
            let mut m = Melee { on: true, ..Default::default() };
            (1..=90).find(|_| { m.step(&d, c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0); !m.on }).map(|f| f as f32 / 30.0)
        };
        // His weapon not in hand: at once.
        assert_eq!(run(&ctx(&idle, false, false, None)), Some(1.0 / 30.0));
        // Past 1.5 s, unless attack is held or he is locked on a target (locked on with none it runs out).
        assert!(run(&ctx(&idle, true, false, None)).is_some_and(|t| t > 1.5 && t < 1.6));
        assert_eq!(run(&ctx(&attack, true, false, None)), None);
        assert_eq!(run(&ctx(&attack, true, true, Some(Foe::default()))), None);
        assert_eq!(run(&ctx(&idle, true, true, Some(Foe::default()))), None);
        assert!(run(&ctx(&idle, true, true, None)).is_some_and(|t| t > 1.5 && t < 1.6));
        assert!(run(&ctx(&idle, true, false, Some(Foe::default()))).is_some_and(|t| t > 1.5 && t < 1.6));
        // +0xF0 0x08 (0x822C0AD8) ends it first, held attack or not, and is cleared; a ped's too. Never set, it never does.
        for me in [None, Some(BARE)] {
            let c = Ctx { me, ..ctx(&attack, true, true, Some(Foe::default())) };
            let mut m = Melee { on: true, quit: true, ..Default::default() };
            m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0);
            assert!(!m.on && !m.quit && m.mv.is_none());
            let mut m = Melee { on: true, ..Default::default() };
            m.step(&d, &c, &mut 0.0, 1.0 / 30.0, &clip, &mut || 0);
            assert!(m.on);
        }
    }

    #[test]
    fn a_dodge_stops_its_opponents_homing_once() {
        init_assets();
        let r = ResultDef { stop_time: 9.0, special_result: h("SetOpponentStopMovesHoming"), ..Default::default() };
        let d = Data { table: ActionTable { actions: vec![], results: vec![r], completed: true }, groups: vec![],
                       player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let a = Animation { name: String::new(), hash: 0, frames: 31, duration: 1.0, tracks: vec![], mover_rotation: None, events: vec![] };
        let clip = |_: &str| Some(&a);
        let (pad, weapon) = (Pad::default(), WeaponView::default());
        let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::ZERO, health: 200.0, weapon: &weapon, foe: None, me: None, lock_on: false, flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
        let mv = MeleeMove { action: 0, result: 0, key: Some("g/c".into()), clip_t: 0.0, dur: 1.0, rate: 1.0, step: 0.0, struck: false, forced: false, swing: false,
                             first: true, window: true, live: [false; 2], was: [false; 2], hits: vec![], events: 0, special: false, hurt: false, given: false };
        let mut m = Melee { on: true, mv: Some(mv), ..Default::default() };
        m.play(&d, &c, &mut 0.0, 1.0 / 30.0, &clip);
        assert!(m.stop_foe_homing);
        m.stop_foe_homing = false;
        m.play(&d, &c, &mut 0.0, 1.0 / 30.0, &clip);
        assert!(!m.stop_foe_homing);
        // On the opponent's side: its window shuts.
        m.stop_homing();
        assert!(!m.mv.unwrap().window);
    }

    #[test]
    fn a_disarm_takes_the_weapon_at_its_0x200_event() {
        init_assets();
        let r = ResultDef { stop_time: 9.0, disarm_opponent: true, ..Default::default() };
        let d = Data { table: ActionTable { actions: vec![], results: vec![r], completed: true }, groups: vec![],
                       player_groups: [-1; 7], player: Default::default(), personalities: Default::default(), gym: Default::default() };
        let mut events = vec![0; 31];
        events[10] = 0x200;
        let a = Animation { name: String::new(), hash: 0, frames: 31, duration: 1.0, tracks: vec![], mover_rotation: None, events };
        let clip = |_: &str| Some(&a);
        let (pad, weapon) = (Pad::default(), WeaponView::default());
        let run = |foe: Option<Foe>, lock_on: bool| {
            let c = Ctx { in_hand: true, pad: &pad, pos: Vec3::ZERO, health: 200.0, weapon: &weapon, foe, me: None, lock_on, flags_240: 0, on_stairs: false, bones: [None; 5], lies: 4, foe_pad: None, foe_task: None };
            let mv = MeleeMove { action: 0, result: 0, key: Some("g/c".into()), clip_t: 0.0, dur: 1.0, rate: 1.0, step: 0.0, struck: false, forced: false, swing: false,
                                 first: true, window: true, live: [false; 2], was: [false; 2], hits: vec![], events: 0, special: false, hurt: false, given: false };
            let mut m = Melee { on: true, mv: Some(mv), ..Default::default() };
            let mut at = vec![];
            for f in 0..29 {
                m.disarm_foe = false;
                m.play(&d, &c, &mut 0.0, 1.0 / 30.0, &clip);
                if m.disarm_foe {
                    at.push(f);
                }
            }
            at
        };
        // The frame after the step that met it (0x822BF448 runs before the clip's step), locked on a target.
        assert_eq!(run(Some(Foe::default()), true), [10]);
        // Not locked (+0x20 clear: a dispatcher's 431, Niko let go of LT) or no target (+0x1C null): nothing taken.
        assert!(run(Some(Foe::default()), false).is_empty());
        assert!(run(None, true).is_empty());
    }

}
