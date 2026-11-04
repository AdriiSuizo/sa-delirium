//! The Balance task's GrabHelper (task +96, 128 bytes; init 8260E8D0): the grab a jump (task 211, 824DFFE8) or being
//! jacked (8241DFA8) gives a Balance, as the messages it sends NM. Its points come from 8260EA28 (the creator's); the
//! Balance start (82619F70) sets +96's 0xF0000 field to 1 and starts it (8260EC10), its control (8261A530) updates it
//! each frame (8260EFB8), its feedback handlers mark which hand holds (8260EB88 / 8260EB10), and its finish takes NMRelax
//! (2104) over the blend while a hand holds; the control's lean stops and its pedalling runs while one does
//! (`meleetask::balance_lean` / `grab_pedal`). The surface find (826155B8, Balance's finish first) runs only with +96's
//! bits 1 / 2, which nothing sets but a clone's source: `GrabHelper::find_surface`, there to be compared with the game.
use crate::prelude::*;
use crate::msg::Value;
use crate::shottask::Msg;
use crate::{Fma, Vd};

/// 8309C7B0, as the game fills it (frame50_1's snapshot): grabStrength 600, bodyStiffness 10, grabDist
/// 4.4, reachAngle 2.8, oneSideReachAngle 1.4, pullUpStrength 0, pullUpTime 1.
/// 82A63D50: the static init that writes this block (8309C7B0); BLOCK is its seven values.
const BLOCK: [f32; 7] = [600.0, 10.0, 4.4, 2.8, 1.4, 0.0, 1.0];
/// taskParams.txt's GrabHelper block names, in the block's order.
const NAMES: [&str; 7] = ["defaultGrabStrength", "grabBodyStiffness", "grabDistance", "grabReachAngle", "grabOneSideReachAngle",
                          "grabPullUpStrength", "grabPullUpTimer"];

/// 8260E6D8: the block as taskParams.txt's GrabHelper set gives it, each value missing from it left at the init's.
pub fn read_block(tp: &delirium_data::taskparams::TaskParams) -> [f32; 7] {
    std::array::from_fn(|k| tp.get("GrabHelper", "*", NAMES[k], BLOCK[k]))
}

#[derive(Clone, Debug, Default)]
pub struct GrabHelper {
    /// +0 pos1, +16 normalL, +32 pos2, +48 normalR, +64 pos3, +80 pos4 (zero: not sent).
    pub pos1: Vec3,
    pub normal_l: Vec3,
    pub pos2: Vec3,
    pub normal_r: Vec3,
    pub pos3: Vec3,
    pub pos4: Vec3,
    /// +96: 1 points set, 8 justbrace, 0x10 linegrab, 0x20 surfaceGrab (else), 0x100 started, 0x200 / 0x400 the left /
    /// right hand holds, 0x1000 a hand's grab failed.
    pub flags: u32,
    /// +104 the entity grabbed (its physics instance, +56 -> +8) and +108 its part: instanceIndex / instancePartIndex.
    pub instance: Option<(i32, i32)>,
    /// The block (8309C7B0) it reads at its start: BLOCK until `read_block` fills it.
    pub block: [f32; 7],
    /// +112 grabStrength (BLOCK's 600 at init).
    pub strength: f32,
    /// +116 the time left to grab, +120 to hold (s; 0 none: no creator sets them).
    pub find_left: f32,
    pub hold_left: f32,
}

impl GrabHelper {
    /// 8260E8D0 (no source).
    pub fn new() -> GrabHelper {
        GrabHelper { strength: BLOCK[0], block: BLOCK, ..Default::default() }
    }

    /// 8260EA28: the creator's points (each zero one left as it was) on `instance`.
    pub fn set_points(&mut self, instance: Option<(i32, i32)>, p: [Option<Vec3>; 6]) {
        self.instance = instance;
        for (to, v) in [&mut self.pos1, &mut self.normal_l, &mut self.pos2, &mut self.normal_r, &mut self.pos3, &mut self.pos4].into_iter().zip(p) {
            if let Some(v) = v {
                *to = v;
            }
        }
        self.flags |= 1;
    }

    /// 0x200 / 0x400: a hand holds.
    pub fn grabbing(&self) -> bool {
        self.flags & 0x600 != 0
    }

    /// 8260EC10: the grab message, with pos1 or pos2 set; its start only the first time.
    pub fn start(&mut self) -> Option<Msg> {
        use Value::{Bool as B, Float as F, Int as I, Vec3 as V};
        if self.pos1 == Vec3::ZERO && self.pos2 == Vec3::ZERO {
            return None;
        }
        let mut p = vec![];
        if self.flags & 0x100 == 0 {
            p.push(("start", B(true)));
            self.flags |= 0x100;
        }
        for (n, v) in [("pos1", self.pos1), ("normalL", self.normal_l), ("pos2", self.pos2), ("normalR", self.normal_r), ("pos3", self.pos3), ("pos4", self.pos4)] {
            if v != Vec3::ZERO {
                p.push((n, V(v)));
            }
        }
        if let Some((i, part)) = self.instance {
            p.extend([("instanceIndex", I(i)), ("instancePartIndex", I(part))]);
        }
        p.extend([("useLeft", B(self.pos1 != Vec3::ZERO)), ("useRight", B(self.pos2 != Vec3::ZERO))]);
        if self.flags & 8 != 0 {
            p.push(("justbrace", B(true)));
        }
        if self.flags & 0x10 != 0 {
            p.push(("linegrab", B(true)));
        } else if self.flags & 0x20 != 0 {
            p.push(("surfaceGrab", B(true)));
        }
        let b = self.block;
        p.extend([("bodyStiffness", F(b[1])), ("grabStrength", F(self.strength)), ("grabDist", F(b[2])),
                  ("reachAngle", F(b[3])), ("oneSideReachAngle", F(b[4])), ("dontLetGo", B(true)),
                  ("pullUpStrengthLeft", F(b[5])), ("pullUpStrengthRight", F(b[5])), ("pullUpTime", F(b[6]))]);
        Some(("grab", p))
    }

    /// 8260EFB8 each frame (`dt` s): holding, past its hold time, or the player letting go (`let_go`: ped +528 / +529
    /// and 82163CE0 on the pad's +2364: only a ped with +528 zero and +529 set, and its control's byte 2 xor byte 0 over 127
    /// but not byte 3 xor byte 0 (nmgrab.txt)), the grab stops; not yet holding, past its time to grab, too. Stopping clears
    /// every flag.
    pub fn update(&mut self, dt: f32, let_go: bool) -> Option<Msg> {
        let holding = self.grabbing();
        let mut stop = false;
        if holding && self.hold_left > 0.0 {
            self.hold_left -= dt;
            stop = self.hold_left <= 0.0;
        }
        stop |= let_go;
        let mut out = None;
        if stop && holding {
            self.flags = 0;
            out = Some(("grab", vec![("start", Value::Bool(false))]));
        }
        if !holding && self.find_left > 0.0 {
            self.find_left -= dt;
            if self.find_left <= 0.0 {
                self.flags = 0;
                out = Some(("grab", vec![("start", Value::Bool(false))]));
            }
        }
        out
    }

    /// The Balance's feedback handlers on `name`: success (8260EB88) LeftArmGrab 0x200, RightArmGrab 0x400; failure
    /// (8260EB10) of either 0x1000.
    pub fn feedback(&mut self, success: bool, name: &str) {
        // The game compares the first 11 / 12 bytes (strncmp), so a longer name with that start counts too.
        let (left, right) = (name.starts_with("LeftArmGrab"), name.starts_with("RightArmGrab"));
        if left || right {
            self.flags |= if !success { 0x1000 } else if left { 0x200 } else { 0x400 };
        }
    }
}

/// What the probe found (826155B8's sphere test 82952EF8, its result block at +144: the hit's instance, point +160, normal +176).
#[derive(Clone, Copy, Debug)]
pub struct Hit {
    pub point: Vec3,
    pub normal: Vec3,
    /// The instance's entity (82476868: +12 when the instance's u16 +10 has 0x40), if any.
    pub entity: Option<Entity>,
}

/// The fields of the entity hit that 826155B8 reads.
#[derive(Clone, Copy, Debug)]
pub struct Entity {
    /// +40 bits 22..25: 1 building, 2 vehicle, 3 ped, 4 object.
    pub kind: u32,
    /// +36's sign bit (ped, vehicle or object alike: only these may be grabbed).
    pub grabbable: bool,
    /// The u16 at +482, low nibble.
    pub n482: u32,
    /// The byte at +555 (an object's: fixed).
    pub b555: u8,
    /// Its matrix (8217DA38: +32): rows +0, +16, +32 and the place +48.
    pub rows: [Vec3; 4],
    /// Its physics instance (+56 -> +8), as `instance` takes it.
    pub instance: i32,
}

/// 826155B8's inputs from the functions it calls: the two velocities (824806D8 on the ped, sp+256 and sp+240), the hand's
/// bone matrix (822FC408, bone 13985: its second row and place), and the ped's matrix (+32: row 0 and place).
#[derive(Clone, Copy, Debug)]
pub struct FindIn {
    pub vel: [Vec3; 2],
    pub hand_dir: Vec3,
    pub hand_pos: Vec3,
    pub ped_row0: Vec3,
    pub ped_pos: Vec3,
}

impl GrabHelper {
    /// 826155B8 (Balance's finish, first): with +96 bit 1 and 0x10000, or bit 2 and any of 0x70000, the ped moving (the two
    /// velocities' sum over 0.3 / 0.5 m/s) and no points yet (bit 0), a sphere of 0.6 at the hand's place plus 0.3 along its
    /// second row is tested (`probe`, 82952EF8); what it hits becomes the grab's point (left of the ped's row 0: pos1, else
    /// pos2), in the entity's own frame (its rows 0, 1, 2 as x, y, z) when it is a vehicle, ped or object, and the entity
    /// the grab holds unless it is a fixed object; a ped or vehicle that cannot be grabbed ends it. Then the start
    /// (8260EC10) and its message. `probe` is given the sphere's place.
    pub fn find_surface(&mut self, i: &FindIn, probe: impl FnOnce(Vec3) -> Option<Hit>) -> Option<Msg> {
        const SPEED: [f32; 2] = [0.3, 0.5]; // 82AAD4E0 / 82AAD4E4
        const REACH: f32 = 0.3; // 82AADD98
        const LIFT: f32 = 0.0; // 8309C798, the hand's offset up (0 in the snapshot)
        let sum = i.vel[0] + i.vel[1];
        let fast = |t: f32| sum.x.fma(sum.x, sum.y.fma(sum.y, sum.z * sum.z)) > t * t;
        let mut go = false;
        if self.flags & 2 != 0 && self.flags & 0x10000 != 0 {
            go |= fast(SPEED[0]);
        }
        if self.flags & 4 != 0 && self.flags & 0x70000 != 0 {
            go |= fast(SPEED[1]);
        }
        if !go || self.flags & 1 != 0 {
            return None;
        }
        let start = i.hand_pos + Vec3::new(0.0, 0.0, LIFT);
        let end = Vec3::new(i.hand_dir.x.fma(REACH, start.x), i.hand_dir.y.fma(REACH, start.y), i.hand_dir.z.fma(REACH, start.z));
        let hit = probe(end)?;
        let (mut point, mut normal, mut held) = (hit.point, hit.normal, None);
        if let Some(e) = hit.entity.filter(|e| (2..5).contains(&e.kind)) {
            let d = hit.point - e.rows[3];
            let local = |v: Vec3| Vec3::new(e.rows[0].vd(v), e.rows[1].vd(v), e.rows[2].vd(v));
            (point, normal) = (local(d), local(hit.normal));
            if !(e.kind == 4 && e.b555 != 0) {
                if !e.grabbable || e.n482 >= 2 {
                    return None;
                }
                held = Some(e);
            }
        }
        // The ped's side: the hit (in the world) against its row 0.
        let d = hit.point - i.ped_pos;
        let left = i.ped_row0.x.fma(d.x, i.ped_row0.y.fma(d.y, i.ped_row0.z * d.z)) > 0.0;
        self.instance = held.map(|e| (e.instance, self.instance.map_or(0, |x| x.1)));
        let (zero, a, b) = (Vec3::ZERO, point, normal);
        (self.pos1, self.normal_l, self.pos2, self.normal_r) = if left { (a, b, zero, zero) } else { (zero, zero, a, a) }; // the game stores the point in both (+48's normalR too)
        self.start()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn grab_helper() {
        let mut g = GrabHelper::new();
        assert!(g.start().is_none()); // no points
        g.set_points(Some((7, 2)), [Some(Vec3::X), None, None, None, None, None]);
        let (name, p) = g.start().unwrap();
        assert_eq!(name, "grab");
        assert!(matches!(p[0], ("start", Value::Bool(true))));
        assert!(p.iter().any(|&(n, ref v)| n == "useLeft" && matches!(v, Value::Bool(true))));
        assert!(p.iter().any(|&(n, ref v)| n == "useRight" && matches!(v, Value::Bool(false))));
        assert!(p.iter().any(|&(n, ref v)| n == "grabStrength" && matches!(v, Value::Float(s) if *s == 600.0)));
        assert!(g.start().unwrap().1.iter().all(|&(n, _)| n != "start")); // started once
        assert!(g.update(0.02, true).is_none()); // not holding: nothing to let go
        g.feedback(true, "LeftArmGrab");
        assert!(g.grabbing());
        assert!(g.update(0.02, false).is_none());
        assert!(g.update(0.02, true).is_some());
        assert_eq!(g.flags, 0);
    }

    /// The game's GrabHelper (NM_COVA nmgrab.txt, nm-workbench oracle/grab.py through the calls mode on frame50_1's ped): the
    /// ctor 8260E8D0, 260 starts (8260EC10: the points and normals set or zero or minus zero, the flags, an instance or none,
    /// the strength), 300 frames (8260EFB8: the hold and find times against the frame's dt, the player letting go by the
    /// ped's +528 / +529 and the pad's +2364 control, 82163CE0) and the feedback
    /// handlers (8260EB88 / 8260EB10 on names, the game compares their first 11 / 12 bytes): every message, the flags and
    /// the times after, bit for bit.
    #[test]
    fn grab_helper_as_the_game() {
        let Ok(dir) = std::env::var("NM_COVA") else { return };
        let Ok(log) = std::fs::read_to_string(std::path::Path::new(&dir).join("nmgrab.txt")) else { return };
        let h = |x: &str| u32::from_str_radix(x, 16).unwrap();
        let lines: Vec<&str> = log.lines().collect();
        let (mut n, mut sent) = ([0; 3], 0);
        let mut i = 0;
        while i < lines.len() {
            let w: Vec<&str> = lines[i].split_whitespace().collect();
            i += 1;
            if w[0] == "block" {
                // 8309C7B0: what 8260E6D8 read from taskParams.txt's GrabHelper block (82A63D50 writes its defaults first).
                assert_eq!(w[2..].iter().map(|x| h(x)).collect::<Vec<_>>(), BLOCK.map(f32::to_bits));
                if let Ok(tp) = std::env::var("TP") {
                    let tp = delirium_data::taskparams::read(&std::fs::read_to_string(tp).unwrap());
                    assert_eq!(read_block(&tp).map(f32::to_bits), BLOCK.map(f32::to_bits), "taskParams.txt's GrabHelper block");
                }
                continue;
            }
            if w[0] == "ctor" {
                // +96..+124: the flags, the clock, the instance and part, the strength, the times; ours' defaults.
                let g = GrabHelper::new();
                let words: Vec<u32> = w[2..].iter().map(|x| h(x)).collect();
                assert_eq!((words[0], words[2], words[3], words[4], words[5], words[6]), (g.flags, 0, 0, g.strength.to_bits(), g.find_left.to_bits(), g.hold_left.to_bits()));
                assert!(g.instance.is_none());
                continue;
            }
            let mut gmsg = vec![];
            while lines[i].starts_with("gmsg") {
                gmsg.push(&lines[i]["gmsg ".len()..]);
                i += 1;
            }
            let after: Vec<Vec<u32>> = (0..if w[0] == "up" { 2 } else { 1 }).map(|k| lines[i + k].split_whitespace().skip(1).map(|x| h(x)).collect()).collect();
            i += after.len();
            let f = |k: usize| f32::from_bits(h(w[k]));
            match w[0] {
                "st" => {
                    let v = |k: usize| Vec3::new(f(2 + 3 * k), f(3 + 3 * k), f(4 + 3 * k));
                    let mut g = GrabHelper { pos1: v(0), normal_l: v(1), pos2: v(2), normal_r: v(3), pos3: v(4), pos4: v(5), flags: h(w[1]),
                                             instance: (w[20] == "1").then(|| (7, h(w[21]) as i32)), strength: f(22), ..Default::default() };
                    let msgs: Vec<_> = g.start().into_iter().collect();
                    sent += crate::shottask::tests::same_as_game(&msgs, &gmsg, "start", n[0]);
                    assert_eq!(g.flags, after[0][0], "{w:?}");
                    n[0] += 1;
                }
                "up" => {
                    let (p528, p529, b): (bool, bool, Vec<u32>) = (w[5] != "0", w[6] != "0", w[7..11].iter().map(|x| x.parse().unwrap()).collect());
                    let let_go = !p528 && p529 && (b[2] ^ b[0]) & 255 > 127 && (b[3] ^ b[0]) & 255 <= 127;
                    let mut g = GrabHelper { pos1: Vec3::X, flags: h(w[1]), hold_left: f(2), find_left: f(3), strength: 600.0, ..Default::default() };
                    let msgs: Vec<_> = g.update(f(4), let_go).into_iter().collect();
                    sent += crate::shottask::tests::same_as_game(&msgs, &gmsg, "update", n[1]);
                    assert_eq!((g.flags, g.find_left.to_bits(), g.hold_left.to_bits()), (after[0][0], after[1][0], after[1][1]), "{w:?}");
                    n[1] += 1;
                }
                _ => {
                    let mut g = GrabHelper { flags: h(w[2]), ..Default::default() };
                    g.feedback(w[1] == "1", if w[3] == "-" { "" } else { w[3] });
                    assert_eq!(g.flags, after[0][0], "{w:?}");
                    n[2] += 1;
                }
            }
        }
        assert_eq!(n, [260, 300, 80]);
        assert!(sent > 100);
    }

    /// The game's surface find (NM_COVA nmgrabfind.txt, nm-workbench oracle/gf.py through the calls mode on frame50_1's
    /// ped): 826155B8 on a GrabHelper with +96 set, the hand's bone matrix, the velocity, the ped's matrix and the hit
    /// entity's type, grab bit, +482 and +555 varied, the probe the game's own on its world; the inputs are the function's
    /// own frame read back (the velocities, the hand, the probe's hit). The six points and normals (x y z), the flags
    /// after the start and whether an entity is held, bit for bit.
    #[test]
    fn find_surface_as_the_game() {
        let Ok(dir) = std::env::var("NM_COVA") else { return };
        let Ok(log) = std::fs::read_to_string(std::path::Path::new(&dir).join("nmgrabfind.txt")) else { return };
        let h = |x: &str| u32::from_str_radix(x, 16).unwrap();
        let v3 = |w: &[&str]| Vec3::new(f32::from_bits(h(w[0])), f32::from_bits(h(w[1])), f32::from_bits(h(w[2])));
        let words = |l: &str| l.split_whitespace().skip(1).map(|x| x.to_string()).collect::<Vec<_>>();
        let lines: Vec<&str> = log.lines().collect();
        let (mut n, mut hit, mut held, mut said) = (0, 0, 0, 0);
        let mut i = 0;
        while i < lines.len() {
            let w = words(lines[i]);
            let w: Vec<&str> = w.iter().map(|x| x.as_str()).collect();
            let fr = words(lines[i + 1]);
            let fr: Vec<&str> = fr.iter().map(|x| x.as_str()).collect();
            i += 2;
            let rows = lines[i].starts_with(" ent").then(|| { let r = words(lines[i]); i += 1; r });
            let after = words(lines[i]);
            i += 1;
            let after: Vec<u32> = after.iter().map(|x| h(x)).collect();
            let inp = FindIn { vel: [v3(&fr[0..3]), v3(&fr[3..6])], hand_pos: v3(&fr[6..9]), hand_dir: v3(&fr[9..12]), ped_pos: v3(&w[1..4]), ped_row0: v3(&w[4..7]) };
            let entity = (h(w[7]) != 0 && h(fr[15]) != 0).then(|| {
                let r: Vec<&str> = rows.as_ref().unwrap().iter().map(|x| x.as_str()).collect();
                Entity { kind: w[8].parse().unwrap(), grabbable: w[9] == "1", n482: w[10].parse().unwrap(), b555: w[11].parse().unwrap(),
                         rows: [v3(&r[0..3]), v3(&r[4..7]), v3(&r[8..11]), v3(&r[12..15])], instance: 7 }
            });
            let mut g = GrabHelper::new();
            g.flags = h(w[0]);
            let end = v3(&fr[12..15]);
            let msg = g.find_surface(&inp, |p| {
                assert_eq!(p.to_array().map(f32::to_bits), end.to_array().map(f32::to_bits), "{:?} the sphere's place", w);
                hit += 1;
                (h(fr[15]) != 0).then(|| Hit { point: v3(&fr[16..19]), normal: v3(&fr[20..23]), entity })
            });
            for (k, to) in [g.pos1, g.normal_l, g.pos2, g.normal_r, g.pos3, g.pos4].into_iter().enumerate() {
                assert_eq!(to.to_array().map(f32::to_bits), [after[4 * k], after[4 * k + 1], after[4 * k + 2]], "{w:?} point {k}");
            }
            assert_eq!(g.flags, after[24], "{w:?} flags");
            assert_eq!(g.instance.is_some(), after[26] != 0, "{w:?} instance");
            held += g.instance.is_some() as u32;
            said += msg.is_some() as u32;
            n += 1;
        }
        assert_eq!(n, 703);
        eprintln!("surface find: {n} cases, {hit} probes, {held} held, {said} grabs started");
        assert!(held > 20 && said > 100);
    }
}
