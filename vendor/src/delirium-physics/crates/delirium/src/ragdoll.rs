use crate::{Fma, Vd, Vx};
use std::sync::Arc;
use bevy_math::{DMat3, DVec3};
use crate::prelude::*;

use crate::artic::{self, Artic, V6, v6};
use crate::collision::Collision;
use crate::gjk;
use crate::nmdbm::sdot;
macro_rules! env_on {
    ($n:literal) => {{ static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new(); *ON.get_or_init(|| std::env::var($n).is_ok()) }};
}
#[path="ragdoll_contact_cost.rs"] mod contact_cost;
#[path="ragdoll_car_slots.rs"] mod car_slots;
#[path="ragdoll_child_bounds.rs"] mod child_bounds;
#[path="ragdoll_mesh_manifold.rs"] mod mesh_manifold;

#[allow(non_snake_case)]
#[inline]
pub fn GRAVITY() -> f32 { delirium_data::kv!(31) }
#[allow(non_snake_case)]
#[inline]
fn MARGIN() -> f32 { delirium_data::kv!(124) }
#[allow(non_snake_case)]
#[inline]
fn WORLD_MARGIN() -> f32 { delirium_data::kv!(565) }
fn part_friction(p: &PartDef) -> f32 {
    if p.name.ends_with("Foot") { 1.0 } else if p.name.ends_with("Hand") { delirium_data::kv!(130) } else { delirium_data::kv!(18) }
}
#[allow(non_snake_case)]
#[inline]
fn PART_ELASTICITY() -> f32 { delirium_data::kv!(106) }
#[allow(non_snake_case)]
#[inline]
fn ELASTICITY() -> f32 { delirium_data::kv!(73) }
pub static SURFACES: std::sync::RwLock<Vec<[f32; 2]>> = std::sync::RwLock::new(Vec::new());
#[allow(non_snake_case)]
#[inline]
fn SLEEP_V2() -> f32 { delirium_data::kv!(48) }
#[allow(non_snake_case)]
#[inline]
fn SLEEP_W2() -> f32 { delirium_data::kv!(565) }
#[allow(non_snake_case)]
#[inline]
fn SLEEP_MEAN2() -> f32 { delirium_data::kv!(57) }
#[allow(non_snake_case)]
#[inline]
fn SLEEP_STOP() -> u32 { delirium_data::kv!(191 int) }
#[allow(non_snake_case)]
#[inline]
fn SLEEP_SLEEP() -> u32 { delirium_data::kv!(118 int) }

#[derive(Clone, Copy, Debug)]
pub enum Limit {
    Cone { swing: f32, twist: f32 },
    Hinge { axis: Vec3, lo: f32, hi: f32, twist: f32 },
    Bone { pf: Quat, cf: Quat, lean: [f32; 2], twist: f32 },
    BoneHinge { pf: Quat, cf: Quat, lo: f32, hi: f32 },
    Locked,
}

impl Limit {
    pub fn from_bone(flags: u32, lo: [f32; 3], hi: [f32; 3], parent: Quat, child: Quat) -> Limit {
        let free: Vec<usize> = (0..delirium_data::kv!(0 int)).filter(|&k| flags & (delirium_data::kv!(51 int) << k) != 0 && flags & (0x2 << k) != 0 && lo[k] != hi[k]).collect();
        let e = [Vec3::X, Vec3::Y, Vec3::Z];
        let pi = std::f32::consts::PI;
        match free.as_slice() {
            [] => Limit::Locked,
            &[a] => {
                let f = Quat::from_rotation_arc(Vec3::Z, e[a]);
                let (lo, hi) = if lo[a] > pi || hi[a] > pi { (lo[a] - 2.0 * pi, hi[a] - 2.0 * pi) } else { (lo[a], hi[a]) };
                Limit::BoneHinge { pf: parent * f, cf: child * f, lo, hi }
            }
            _ => {
                let t = if free.len() == delirium_data::kv!(0 int) { 2 } else { (0..delirium_data::kv!(0 int)).find(|k| !free.contains(k)).unwrap() };
                let (x, y) = ((t + 1) % delirium_data::kv!(0 int), (t + 2) % delirium_data::kv!(0 int));
                let f = Quat::from_mat3(&Mat3::from_cols(e[x], e[y], e[t]));
                let half = |k: usize| { let h = (hi[k] - lo[k]) * 0.5; if h >= pi { delirium_data::kv!(108) * pi } else { h } };
                let twist = if free.len() == delirium_data::kv!(0 int) { if (hi[2] - lo[2]) * 0.5 >= pi { f32::MAX } else { ((hi[2] - lo[2]) * 0.5).max(delirium_data::kv!(48)) } } else { delirium_data::kv!(48) };
                let c = |k: usize| if free.contains(&k) { (lo[k] + hi[k]) * 0.5 } else { 0.0 };
                let centre = Quat::from_rotation_z(c(2)) * Quat::from_rotation_y(c(1)) * Quat::from_rotation_x(c(0));
                Limit::Bone { pf: parent * centre * f, cf: child * f, lean: [half(y), half(x)], twist }
            }
        }
    }
}

#[derive(Clone)]
pub struct Build {
    pub parts: Vec<PartDef>,
    pub nm: usize,
}

#[derive(Clone)]
pub struct PartDef {
    pub bone: usize,
    pub parent: Option<usize>,
    pub mass: f32,
    pub radius: f32,
    pub half: f32,
    pub offset: Transform,
    pub rest: Quat,
    pub anchor_parent: Vec3,
    pub anchor: Vec3,
    pub limit: Limit,
    pub lower: bool,
    pub reach: bool,
    pub name: String,
    pub bind: Quat,
    pub boxed: Option<Vec3>,
    pub inertia: Vec3,
    pub size: f32,
    pub heft: f32,
    pub grown: f32,
    /// Fitted rigs: turns the GTA IV joint frame on the child side so the
    /// target rig's own rest sits where IV's rest does (Build::fit). Identity
    /// for an unfitted body.
    pub recentre: Quat,
}

impl PartDef {
    fn moments(&self) -> Vec3 {
        if self.inertia.min_element() > 0.0 {
            return self.inertia;
        }
        let (m, r, l) = (self.mass.max(delirium_data::kv!(73)), self.radius, 2.0 * (self.half + self.radius));
        match self.boxed {
            Some(e) => Vec3::new(e.y * e.y + e.z * e.z, e.x * e.x + e.z * e.z, e.x * e.x + e.y * e.y) * (m / delirium_data::kv!(5)),
            None => Vec3::new(m * (delirium_data::kv!(5) * r * r + l * l) / delirium_data::kv!(21), 0.5 * m * r * r, m * (delirium_data::kv!(5) * r * r + l * l) / delirium_data::kv!(21)),
        }
    }
}

impl Build {
    pub fn undump(text: &str) -> (Build, Vec<Transform>) {
        let mut parts = vec![];
        let mut frames = vec![];
        let mut nm = crate::nmrs::HOMER;
        for line in text.lines().filter(|l| !l.trim().is_empty()) {
            if let Some(k) = line.strip_prefix("nm ") {
                nm = k.trim().parse().unwrap();
                continue;
            }
            let w: Vec<&str> = line.split_whitespace().collect();
            let f = |i: usize| w[i].parse::<f32>().unwrap();
            let v = |i: usize| Vec3::new(f(i), f(i + 1), f(i + 2));
            let q = |i: usize| Quat::from_xyzw(f(i), f(i + 1), f(i + 2), f(i + delirium_data::kv!(0 int)));
            let limit = match w.iter().position(|&t| t == "j") {
                Some(k) if w[k + 1] == "b" => Limit::Bone { pf: q(k + 2), cf: q(k + delirium_data::kv!(6 int)), lean: [f(k + delirium_data::kv!(77 int)), f(k + delirium_data::kv!(102 int))], twist: f(k + delirium_data::kv!(52 int)) },
                Some(k) if w[k + 1] == "h" => Limit::BoneHinge { pf: q(k + 2), cf: q(k + delirium_data::kv!(6 int)), lo: f(k + delirium_data::kv!(77 int)), hi: f(k + delirium_data::kv!(102 int)) },
                Some(_) => Limit::Locked,
                None if w[delirium_data::kv!(94 int)] == "c" => Limit::Cone { swing: f(delirium_data::kv!(95 int)), twist: f(delirium_data::kv!(96 int)) },
                None => Limit::Hinge { axis: v(delirium_data::kv!(95 int)), lo: f(delirium_data::kv!(98 int)), hi: f(delirium_data::kv!(328 int)), twist: f(delirium_data::kv!(81 int)) },
            };
            parts.push(PartDef { size: 1.0, heft: 1.0, grown: 1.0, recentre: Quat::IDENTITY, bone: parts.len(), parent: w[1].parse::<i32>().ok().filter(|&j| j >= 0).map(|j| j as usize), mass: f(2), radius: f(delirium_data::kv!(0 int)), half: f(delirium_data::kv!(1 int)),
                                 offset: Transform::from_translation(v(delirium_data::kv!(2 int))).with_rotation(q(delirium_data::kv!(23 int))), rest: q(delirium_data::kv!(52 int)), anchor_parent: v(delirium_data::kv!(51 int)), anchor: v(delirium_data::kv!(192 int)),
                                 limit, lower: w[delirium_data::kv!(474 int)] == "1", reach: w[delirium_data::kv!(274 int)] == "1", name: w[0].to_string(),
                                 bind: w.iter().position(|&t| t == "B").map_or(q(delirium_data::kv!(435 int)), |k| q(k + 1)), boxed: (w.get(delirium_data::kv!(382 int)) == Some(&"b")).then(|| v(delirium_data::kv!(418 int))),
                                 inertia: w.iter().position(|&t| t == "i").map_or(Vec3::ZERO, |k| v(k + 1)) });
            frames.push(Transform::from_translation(v(delirium_data::kv!(454 int))).with_rotation(q(delirium_data::kv!(435 int))));
        }
        let mut build = Build { parts, nm };
        crate::nmrs::asset_layout(&mut build, &mut frames);
        (build, frames)
    }

    pub fn dump(&self, frames: &[Transform]) -> String {
        let v = |x: Vec3| format!("{} {} {}", x.x, x.y, x.z);
        let q = |x: Quat| format!("{} {} {} {}", x.x, x.y, x.z, x.w);
        self.parts.iter().zip(frames).map(|(p, f)| {
            let limit = match p.limit {
                Limit::Cone { swing, twist } => format!("c {swing} {twist} 0 0 0 0"),
                Limit::Hinge { axis, lo, hi, twist } => format!("h {} {lo} {hi} {twist}", v(axis)),
                _ => "c 0 0 0 0 0 0".into(),
            };
            let bone = match p.limit {
                Limit::Bone { pf, cf, lean, twist } => format!(" j b {} {} {} {} {twist}", q(pf), q(cf), lean[0], lean[1]),
                Limit::BoneHinge { pf, cf, lo, hi } => format!(" j h {} {} {lo} {hi} 0", q(pf), q(cf)),
                Limit::Locked => " j l 0 0 0 1 0 0 0 1 0 0 0".into(),
                _ => String::new(),
            };
            format!("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {}{} i {}", p.name, p.parent.map_or(-1, |j| j as i32), p.mass, p.radius, p.half,
                    v(p.offset.translation), q(p.offset.rotation), q(p.rest), v(p.anchor_parent), v(p.anchor), limit,
                    p.lower as u8, p.reach as u8, v(f.translation), q(f.rotation), p.boxed.map_or(String::new(), |e| format!(" b {}", v(e))), v(p.inertia)) + &bone
                    + &format!(" B {}\n", q(p.bind))
        }).collect::<String>() + &if self.nm == crate::nmrs::HOMER { String::new() } else { format!("nm {}\n", self.nm) }
    }

    pub fn fit(&self, frames: &[Transform], joints: &[Vec3]) -> (Build, Vec<Transform>) {
        self.fit_with(frames, joints, false)
    }

    /// fit() onto a skin's bind pose (SA's T-pose), whose elbows and knees
    /// bend in the body's sagittal plane: each hinge limb is also rolled
    /// about its own bone so GTA IV's hinge axis lies across that plane.
    /// Positions alone leave that roll free; the arc fit carried IV's rest
    /// roll over and SA's real elbow/knee axes came out 28 deg (elbows),
    /// 14/5 deg (L/R knee) off, measured over 63 dumped skins, so every
    /// bent limb entered off its hinge (rigid::join snapped it) and the
    /// upper arms 20-30 deg over their twist limit.
    pub fn fit_skin(&self, frames: &[Transform], joints: &[Vec3]) -> (Build, Vec<Transform>) {
        self.fit_with(frames, joints, true)
    }

    fn fit_with(&self, frames: &[Transform], joints: &[Vec3], skin: bool) -> (Build, Vec<Transform>) {
        let n = self.parts.len();
        let pivot = |k: usize| match self.parts[k].parent {
            Some(_) => frames[k].translation + frames[k].rotation * self.parts[k].anchor,
            None => frames[k].translation,
        };
        let basis = |side: Vec3, up: Vec3| {
            let x = side.normalize_or(Vec3::X);
            let z = (up - x * up.dot(x)).normalize_or(Vec3::Z);
            Quat::from_mat3(&Mat3::from_cols(x, z.cross(x), z)).normalize()
        };
        // Skin fit: GTA IV's hinge axes at its rest (world) and the skin's
        // body forward, from the hips and the trunk's midline.
        let sagittal = skin.then(|| {
            let nm = crate::nmrs::joints(self, crate::nmrs::ASSET[self.nm].get()?)?;
            let side = |tag: &str| (0..n).find(|&c| self.parts[c].parent == Some(0) && self.parts[c].name.contains(tag));
            let mut top = 0;
            while let Some(c) = (0..n).find(|&c| self.parts[c].parent == Some(top) && !self.parts[c].name.contains("_L_") && !self.parts[c].name.contains("_R_")) { top = c; }
            let (l, r) = (side("_L_")?, side("_R_")?);
            let forward = (joints[top] - joints[0]).cross(joints[l] - joints[r]).try_normalize()?;
            let mut axis = vec![None; n];
            for j in nm.iter().filter(|j| !j.three) { axis[j.child] = Some(frames[j.child].rotation * (j.in_child * Vec3::Z)); }
            Some((forward, axis))
        }).flatten();
        let (mut size, mut turn) = (vec![1.0f32; n], vec![Quat::IDENTITY; n]);
        for k in 0..n {
            let kids: Vec<usize> = (0..n).filter(|&c| self.parts[c].parent == Some(k)).collect();
            let (s0, t0) = self.parts[k].parent.map_or((1.0, Quat::IDENTITY), |p| (size[p], turn[p]));
            let named = |tag: &str| kids.iter().copied().find(|&c| self.parts[c].name.contains(tag));
            let middle = kids.iter().copied().find(|&c| !self.parts[c].name.contains("_L_") && !self.parts[c].name.contains("_R_"));
            (size[k], turn[k]) = match kids[..] {
                [] => (s0, t0),
                [c] => {
                    let (ours, theirs) = (pivot(c) - pivot(k), joints[c] - joints[k]);
                    let arc = Quat::from_rotation_arc((t0 * ours).normalize_or(Vec3::Z), theirs.normalize_or(Vec3::Z));
                    let mut t = (arc * t0).normalize();
                    // Upper arm/thigh of a hinge: roll it (and so its forearm/calf,
                    // which follows) about the bone until the hinge axis is
                    // perpendicular to the body's forward direction.
                    // Arms only: rolling the thigh/calf turns the feet (balance support).
                    if let Some((forward, Some(h))) = sagittal.as_ref().filter(|_| !self.parts[c].lower).map(|(f, a)| (*f, a[c])) {
                        let d = theirs.normalize_or(Vec3::Z);
                        let flat = |v: Vec3| (v - d * v.dot(d)).normalize_or_zero();
                        let (h, w) = (flat(t * h), flat(d.cross(forward)));
                        let w = if h.dot(w) < 0.0 { -w } else { w };
                        if h != Vec3::ZERO && w != Vec3::ZERO {
                            t = (Quat::from_axis_angle(d, h.cross(w).dot(d).atan2(h.dot(w))) * t).normalize();
                        }
                    }
                    (theirs.length() / ours.length().max(delirium_data::kv!(68)), t)
                }
                _ => {
                    let spread = |at: &dyn Fn(usize) -> Vec3| kids.iter().map(|&c| (at(c) - at(k)).length()).sum::<f32>();
                    let s = spread(&|c| joints[c]) / spread(&pivot).max(delirium_data::kv!(68));
                    let t = match (named("_L_"), named("_R_"), middle) {
                        (Some(l), Some(r), Some(mut m)) => {
                            // A native torso pivot may coincide with pelvis
                            // (SA spine tag 2). Its body still extends to its
                            // child; do not derive pelvis orientation from the
                            // millimetre/noise-sized pivot offset. Use the same
                            // anatomical midline descendant in both skeletons.
                            let upright = |at: &dyn Fn(usize)->Vec3, m:usize| {
                                let side=at(l)-at(r);let up=at(m)-(at(l)+at(r))*0.5;
                                let width=side.length();
                                width>1e-4 && side.cross(up).length()/width>width*0.05
                            };
                            while joints[l].distance(joints[r])>1e-4 && pivot(l).distance(pivot(r))>1e-4
                                && (!upright(&|c|joints[c],m)||!upright(&pivot,m)) {
                                match (0..n).find(|&c|self.parts[c].parent==Some(m)
                                    && !self.parts[c].name.contains("_L_") && !self.parts[c].name.contains("_R_")) {
                                    Some(c)=>m=c,None=>break,
                                }
                            }
                            // Some target rigs (SA, including CJ) put both clavicle
                            // roots at the same point. Normalizing that near-zero
                            // span turns floating-point noise into a chest heading.
                            // Use the same bilateral descendants in both rigs until
                            // there is an anatomical span, or retain the parent turn.
                            let (mut left, mut right) = (l, r);
                            let usable = |a: usize, b: usize| {
                                let scale = (joints[m] - joints[k]).length().max(0.01);
                                (joints[a] - joints[b]).length() > scale * 0.05
                                    && (pivot(a) - pivot(b)).length() > 1e-4
                            };
                            while !usable(left, right) {
                                let next = |p| (0..n).find(|&c| self.parts[c].parent == Some(p));
                                match (next(left), next(right)) {
                                    (Some(a), Some(b)) => { left = a; right = b; }
                                    _ => break,
                                }
                            }
                            if !usable(left, right) { t0 } else {
                                let b = |at: &dyn Fn(usize) -> Vec3| basis(at(left) - at(right), at(m) - (at(left) + at(right)) * 0.5);
                                (b(&|c| joints[c]) * b(&pivot).inverse()).normalize()
                            }
                        }
                        _ => t0,
                    };
                    (s, t)
                }
            };
        }
        let mut build = self.clone();
        for (k, p) in build.parts.iter_mut().enumerate() {
            let s = size[k];
            (p.size, p.radius, p.half, p.anchor) = (p.size * s, p.radius * s, p.half * s, p.anchor * s);
            p.offset.translation *= s;
            (p.boxed, p.inertia) = (p.boxed.map(|e| e * s), p.inertia * s * s);
            p.anchor_parent *= p.parent.map_or(1.0, |q| size[q]);
        }
        let mut out: Vec<Transform> = frames.iter().zip(&turn).map(|(f, t)| Transform::from_rotation((*t * f.rotation).normalize())).collect();
        out[0].translation = joints[0];
        for c in 1..n {
            if let Some(p) = build.parts[c].parent {
                // One scalar per parent cannot reproduce different hip widths,
                // clavicle origins and torso lengths. Use the target rig's
                // actual shared pivot for both sides of each constraint. This
                // keeps the collision skeleton and rendered bind joints aligned.
                build.parts[c].anchor_parent = out[p].rotation.inverse()
                    * (joints[c] - out[p].translation);
                out[c].translation = joints[c] - out[c].rotation * build.parts[c].anchor;
                // Trunk and neck: the target rig's rest posture is its neutral
                // one (SA's neck sits 22-28 deg forward of IV's on the same
                // chest). Keep IV's limits and drive targets about that rest,
                // not about IV's, or every entry starts outside the neck/spine
                // limits and the first steps snap it back. Limbs keep IV's.
                let name = &self.parts[c].name;
                if !name.contains("_L_") && !name.contains("_R_") {
                    let drift = (out[p].rotation.inverse() * out[c].rotation).inverse() * (frames[p].rotation.inverse() * frames[c].rotation);
                    // An unchanged rest (IV onto IV proportions) keeps IV's exact frames.
                    if drift.xyz().length() > 1e-5 { build.parts[c].recentre = (drift * self.parts[c].recentre).normalize(); }
                }
            }
        }
        (build, out)
    }

    pub fn scaled(&self, frames: &[Transform], len: &[f32], m: f32) -> (Build, Vec<Transform>) {
        let pivot = |k: usize| self.parts[k].parent.map_or(frames[k].translation, |_| frames[k].translation + frames[k].rotation * self.parts[k].anchor);
        let mut joints = vec![frames[0].translation; self.parts.len()];
        for (c, p) in self.parts.iter().enumerate() {
            if let Some(q) = p.parent {
                joints[c] = joints[q] + (pivot(c) - pivot(q)) * len[q];
            }
        }
        let (mut b, f) = self.fit(frames, &joints);
        let w: f32 = self.parts.iter().map(|p| p.mass).sum();
        let v = self.parts.iter().zip(&b.parts).map(|(a, p)| a.mass * p.size.powi(3)).sum::<f32>() / w;
        let g = (m / v).sqrt();
        let legs = b.parts.iter().find(|p| p.lower && p.parent == Some(0)).map_or(1.0, |p| p.size);
        for p in &mut b.parts {
            (p.heft, p.grown, p.radius) = (p.heft * g * g, p.grown * legs, p.radius * g);
        }
        (b, f)
    }

    pub fn new(parts: &[delirium_data::ragdoll::Part], bind: &[Transform], bones: &[delirium_data::skeleton::Bone], nm: usize) -> Build {
        let linked = parts.len() == delirium_data::kv!(100 int) && crate::nmrs::PARTS[nm].get().is_some_and(|p| p.len() == parts.len());
        let frames: Vec<Transform> = parts.iter().enumerate().map(|(i, p)| {
            let [x, y, z, t] = p.frame;
            let m = Mat3::from_cols(Vec3::from(x), Vec3::from(y), Vec3::from(z));
            let link = if linked { crate::nmdbm::link(nm, i).inverse() } else { Quat::IDENTITY };
            Transform { translation: Vec3::from(t), rotation: (Quat::from_mat3(&m).normalize() * link).normalize(), scale: Vec3::ONE }
        }).collect();
        let pelvis = 0;
        let defs = parts.iter().enumerate().map(|(i, p)| {
            let parent = p.parent.or((i != pelvis).then_some(pelvis));
            let f = frames[i];
            let joint = bind[p.bone].translation;
            let (rest, anchor_parent) = parent.map_or((Quat::IDENTITY, Vec3::ZERO), |q| {
                let pf = frames[q];
                (f.rotation.inverse() * pf.rotation, pf.rotation.inverse() * (joint - pf.translation))
            });
            let bone = &bones[p.bone];
            let name = bone.name.as_str();
            let limit = parent.map_or(Limit::Locked, |q| Limit::from_bone(bone.flags, bone.rot_min, bone.rot_max,
                frames[q].rotation.inverse() * bind[p.bone].rotation, f.rotation.inverse() * bind[p.bone].rotation));
            let (radius, half) = if p.kind == delirium_data::kv!(0 int) { (p.extent.iter().copied().fold(f32::MAX, f32::min), 0.0) } else { (p.radius, p.half) };
            PartDef { size: 1.0, heft: 1.0, grown: 1.0, recentre: Quat::IDENTITY, bone: p.bone, parent, mass: p.mass, radius, half,
                      offset: Transform::from_matrix(bind[p.bone].to_matrix().inverse() * f.to_matrix()),
                      rest, anchor_parent, anchor: f.rotation.inverse() * (joint - f.translation), limit,
                      lower: i == pelvis || ["Thigh", "Calf", "Foot"].iter().any(|l| name.ends_with(l)),
                      reach: name.ends_with("UpperArm") || name.ends_with("Forearm"), name: name.to_string(), bind: f.rotation,
                      boxed: (p.kind == delirium_data::kv!(0 int)).then(|| Vec3::from(p.extent)), inertia: Vec3::from(p.inertia) }
        }).collect();
        Build { parts: defs, nm }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Muscle {
    pub target: Quat,
    pub stiffness: f32,
    pub damping: f32,
}

impl Muscle {
}

#[derive(Clone, Default)]
pub struct Kept {
    pub muscles: Vec<Muscle>,
    pub lift: f32,
    pub accel: Vec3,
    pub rigid: Vec<Rigid>,
    pub torque: Vec<Vec3>,
    pub force: Vec<Vec3>,
}

impl Kept {
    pub fn drive(&self) -> Drive<'_> {
        Drive { muscles: &self.muscles, lift: self.lift, accel: self.accel, rigid: &self.rigid, torque: &self.torque, force: &self.force }
    }
}

#[derive(Clone, Copy, Default)]
pub struct Drive<'a> {
    pub muscles: &'a [Muscle],
    pub lift: f32,
    pub accel: Vec3,
    pub rigid: &'a [Rigid],
    pub torque: &'a [Vec3],
    pub force: &'a [Vec3],
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Rigid {
    pub frame: Quat,
    pub child: Quat,
    pub limits: [(f32, f32); 3],
    pub max: [f32; 2],
    pub blend: f32,
    pub hinge: bool,
    pub desired: f32,
    pub jc: Option<crate::rigid::JointConst>,
    pub target: [f32; 3],
    pub gain: [f32; 2],
    pub soft: [crate::nmrs::SoftLimit; 3],
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Obstacle {
    pub id: i32,
    pub owner: Option<u64>,
    pub filter:CollisionFilter,
    pub at: Vec3,
    pub rot: Quat,
    pub centre: Vec3,
    pub half: Vec3,
    pub vel: Vec3,
    pub spin: Vec3,
    pub com: Vec3,
    pub parts: &'static [ObPart],
    pub last: Option<(Vec3, Quat)>,
}

#[derive(Clone, Copy, Debug)]
pub struct ObPart {
    pub shape: gjk::Shape,
    pub m: Mat3,
    pub at: Vec3,
    pub lo: Vec3,
    pub hi: Vec3,
    pub mesh: Option<&'static crate::sphere::Mesh>,
    pub filter:CollisionFilter,
}

#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct CollisionFilter {pub kind:u32,pub include:u32}
impl Default for CollisionFilter {fn default()->Self{Self{kind:u32::MAX,include:u32::MAX}}}
impl CollisionFilter {
    pub const NM_FRAGMENT:Self=Self{kind:0x40,include:u32::MAX};
    pub fn meets(self,other:Self)->bool{self.kind&other.include!=0&&other.kind&self.include!=0}
    fn animated_ped(self)->bool{self.kind==0x20&&self.include==0x79FE}
}

/// Contact categories of [`GapAudit`]: 0 world mesh, 1 vehicle or other
/// obstacle, 2 animated ped owner, 3 other capsule bodies, 4 own parts.
pub const GAP_CATEGORIES: usize = 5;
#[derive(Clone, Copy, Debug)]
pub struct GapAudit { pub gap: f32, pub part: i32, pub other: u64, pub rows: u32, pub deep10: u32, pub deep30: u32 }
impl Default for GapAudit { fn default() -> Self { Self { gap: f32::INFINITY, part: -1, other: 0, rows: 0, deep10: 0, deep30: 0 } } }
impl GapAudit {
    fn note(&mut self, gap: f32, part: usize, other: u64) {
        self.rows += 1;
        if gap < -0.01 { self.deep10 += 1; }
        if gap < -0.03 { self.deep30 += 1; }
        if gap < self.gap { (self.gap, self.part, self.other) = (gap, part as i32, other); }
    }
}

impl Obstacle {
    pub fn sphere(&self, c: Vec3, r: f32) -> Option<crate::sphere::Hit> {
        if self.parts.iter().all(|p| p.mesh.is_none()) {
            return None;
        }
        let (inst, ms): ([Vec3; 4], Vec<[Vec3; 4]>) = ([self.rot * Vec3::X, self.rot * Vec3::Y, self.rot * Vec3::Z, self.at],
                                                       self.parts.iter().map(|p| [p.m.x_axis, p.m.y_axis, p.m.z_axis, p.at]).collect());
        let parts: Vec<_> = self.parts.iter().zip(&ms).map(|(p, m)| (m, p.mesh)).collect();
        crate::sphere::probe(&inst, &parts, c, r)
    }

    pub fn find(obstacles: &[Obstacle], id: i32) -> Option<&Obstacle> {
        if id < 0 { None } else { obstacles.iter().find(|o| o.id == id) }
    }

    pub fn component(&self, k: usize) -> (Vec3, Mat3) {
        self.parts.get(k).map_or((self.at, Mat3::from_quat(self.rot)), |c| (c.at + self.at, c.m.transpose()))
    }

    pub fn to_world(&self, p: Vec3) -> Vec3 {
        self.at + self.rot * p
    }

    pub fn vel_at(&self, p: Vec3) -> Vec3 {
        let (r, w) = (p - self.com, self.spin);
        (-r.yzx()).mul_add(w.zxy(), r.zxy() * w.yzx()) + self.vel
    }
}
#[allow(non_snake_case)]
#[inline]
fn CAR_FRICTION() -> f32 { delirium_data::kv!(57) }

pub type Capsule = (Vec3, Vec3, f32, Vec3, bool);

pub fn nearest_point(a: Vec3, b: Vec3, x: Vec3) -> (Vec3, f32) {
    let d = b - a;
    let t = if d.length_squared() > delirium_data::kv!(72) { ((x - a).dot(d) / d.length_squared()).clamp(0.0, 1.0) } else { 0.0 };
    (a + d * t, t)
}

fn nearest(a0: Vec3, a1: Vec3, b0: Vec3, b1: Vec3) -> (Vec3, Vec3) {
    let (d1, d2, r) = (a1 - a0, b1 - b0, a0 - b0);
    let (a, e, f) = (d1.length_squared(), d2.length_squared(), d2.dot(r));
    let (c, b) = (d1.dot(r), d1.dot(d2));
    let den = a * e - b * b;
    let mut s = if den > delirium_data::kv!(72) { ((b * f - c * e) / den).clamp(0.0, 1.0) } else { 0.0 };
    let mut t = if e > delirium_data::kv!(72) { (b * s + f) / e } else { 0.0 };
    if t < 0.0 {
        (t, s) = (0.0, if a > delirium_data::kv!(72) { (-c / a).clamp(0.0, 1.0) } else { 0.0 });
    } else if t > 1.0 {
        (t, s) = (1.0, if a > delirium_data::kv!(72) { ((b - c) / a).clamp(0.0, 1.0) } else { 0.0 });
    }
    (a0 + d1 * s, b0 + d2 * t)
}

#[derive(Clone)]
struct Body {
    x: Vec3,
    r: [Vec3; 3],
    q: Quat,
    v: Vec3,
    w: Vec3,
    m: f32,
    i: Vec3,
    l: Quat,
    att: [Vec3; 3],
}

impl Body {
    fn set_q(&mut self, q: Quat) {
        let m = Mat3::from_quat(q * self.l);
        (self.q, self.r) = (q, [m.row(0), m.row(1), m.row(2)]);
    }

    fn set_r(&mut self, r: [Vec3; 3]) {
        self.r = r;
        self.q = (Quat::from_mat3(&Mat3::from_cols(r[0], r[1], r[2]).transpose()) * self.l.inverse()).normalize();
    }

    fn link(&self) -> crate::rigid::Link {
        crate::rigid::Link { r: self.r, d: self.x, m: self.m, v: self.v, w: self.w }
    }

    fn moved(&mut self, v: Vec3, w: Vec3, h: f32) {
        crate::rigid::integrate(&mut self.r, &mut self.x, v, w, h);
        self.set_r(self.r);
    }

    fn bound(&self) -> [Vec3; 3] {
        crate::rigid::rows_mul(&self.att, &[0, 1, 2].map(|k| crate::rigid::col(&self.r, k)))
    }

    fn cap(&self) -> Quat {
        Quat::from_mat3(&turn(&self.bound())).normalize()
    }

    fn tensor(&self) -> DMat3 {
        let r = DMat3::from_cols(self.r[0].as_dvec3(), self.r[1].as_dvec3(), self.r[2].as_dvec3()).transpose();
        r * DMat3::from_diagonal(self.i.as_dvec3()) * r.transpose()
    }
}

#[derive(Clone, Copy, Debug)]
enum Kind {
    Ball { lean: [f32; 2], twist: f32 },
    Hinge { lo: f32, hi: f32 },
}

#[derive(Clone, Copy, Debug)]
struct Joint {
    parent: usize,
    pf: Quat,
    cf: Quat,
    kind: Kind,
    stiff: f32,
    mode: u32,
    jc: crate::rigid::JointConst,
}

#[derive(Clone, Copy, Debug)]
pub struct JointInfo {
    pub parent: usize,
    pub hinge: bool,
    pub in_parent: Quat,
    pub in_child: Quat,
    pub limits: [f32; 3],
    pub anchor_parent: Vec3,
    pub anchor_child: Vec3,
}

pub fn joint_info(build: &Build, k: usize, rigid: Option<&Rigid>) -> Option<JointInfo> {
    let p = build.parts.get(k)?;
    let j = Joint::new(p, p.parent?, rigid);
    let (hinge, limits) = match j.kind {
        Kind::Ball { lean, twist } => (false, [lean[1], lean[0], twist]),
        Kind::Hinge { lo, hi } => (true, [lo, hi, 0.0]),
    };
    Some(JointInfo { parent: j.parent, hinge, in_parent: j.pf, in_child: j.cf, limits, anchor_parent: p.anchor_parent, anchor_child: p.anchor })
}

impl Joint {
    fn new(p: &PartDef, parent: usize, rigid: Option<&Rigid>) -> Joint {
        let (pf, cf, kind, stiff) = if let Some(r) = rigid.filter(|r| r.limits != [(0.0, 0.0); 3]) {
            let half = |k: usize| (r.limits[k].1 - r.limits[k].0) * 0.5;
            let kind = if r.hinge { Kind::Hinge { lo: r.limits[0].0, hi: r.limits[0].1 } } else { Kind::Ball { lean: [half(0), half(1)], twist: half(2) } };
            (r.frame, r.child, kind, r.blend)
        } else {
            let stiff = rigid.map_or(0.0, |r| r.blend);
            match p.limit {
                Limit::Bone { pf, cf, lean, twist } => (pf, cf, Kind::Ball { lean, twist }, stiff),
                Limit::BoneHinge { pf, cf, lo, hi } => (pf, cf, Kind::Hinge { lo, hi }, stiff),
                Limit::Locked => (p.rest.inverse(), Quat::IDENTITY, Kind::Ball { lean: [0.0; 2], twist: 0.0 }, LOCKED),
                Limit::Cone { swing, twist } => {
                    let z = Quat::from_rotation_x(-std::f32::consts::FRAC_PI_2);
                    (p.rest.inverse() * z, z, Kind::Ball { lean: [swing; 2], twist }, stiff)
                }
                Limit::Hinge { axis, lo, hi, .. } => {
                    let z = Quat::from_rotation_arc(Vec3::Z, axis.normalize());
                    (p.rest.inverse() * z, z, Kind::Hinge { lo, hi }, stiff)
                }
            }
        };
        Joint { parent, pf, cf, kind, stiff, mode: if rigid.is_some() { delirium_data::kv!(0 int) } else { 0 }, jc: Default::default() }
    }
}

const LOCKED: f32 = 1.0;

fn pair(joints: &[Option<Joint>], k: usize, t: DVec3, lt: &mut [V6]) {
    for c in 0..delirium_data::kv!(0 int) {
        lt[k][delirium_data::kv!(0 int) + c] += t[c];
        if let Some(j) = joints[k] {
            lt[j.parent][delirium_data::kv!(0 int) + c] -= t[c];
        }
    }
}

fn answer(artic: &Artic, k: usize, t: DVec3) -> DVec3 {
    artic.answer(k, t.as_vec3()).as_dvec3()
}

#[derive(Clone)]
struct Row {
    j: Vec<f64>,
    mj: Vec<f64>,
    eff: f64,
    target: f64,
    lambda: f64,
    kind: RowKind,
    at: Spot,
    relaxation:f64,
}

#[derive(Clone, Copy)]
struct Spot {
    at: [usize; 2],
    cut: usize,
}

impl Spot {
    const OWN: Spot = Spot { at: [0, 0], cut: usize::MAX };

    fn of(self, i: usize) -> usize {
        if i < self.cut { self.at[0] + i } else { self.at[1] + i - self.cut }
    }

    fn runs(self, len: usize) -> [(std::ops::Range<usize>, std::ops::Range<usize>); 2] {
        let c = self.cut.min(len);
        [(self.at[0]..self.at[0] + c, 0..c), (self.at[1]..self.at[1] + len - c, c..len)]
    }
}

#[derive(Clone, Copy, PartialEq)]
enum RowKind {
    Push,
    Equal,
    Friction { of: usize, mu: f64 },
}

impl Row {
    fn new(artic: &Artic, j: Vec<f64>, target: f64, kind: RowKind) -> Row {
        let mj = artic.solve_row(&j);
        Row::with(j, mj, target, kind)
    }

    fn with(j: Vec<f64>, mj: Vec<f64>, target: f64, kind: RowKind) -> Row {
        let eff = j.iter().zip(&mj).map(|(a, b)| a * b).sum::<f64>().max(delirium_data::kv!(66));
        Row { j, mj, eff, target, lambda: 0.0, kind, at: Spot::OWN,relaxation:1. }
    }
}

fn frame(n: Vec3) -> [Vec3; 3] {
    let t = if n.x.abs() >= n.y.abs() { Vec3::new(-n.z, 0.0, n.x) } else { Vec3::new(0.0, n.z, -n.y) }.normalize();
    [n, t, n.cross(t)]
}

#[derive(Clone)]
struct Block {
    // Coefficients are immutable during solve. Preview/position snapshots share
    // them; owner rebinding/splitting makes a private copy before any mutation.
    j: Arc<[Vec<f64>; 3]>,
    mj: Arc<[Vec<f64>; 3]>,
    nonzero: Arc<[usize]>,
    unbound: Option<(Arc<[Vec<f64>;3]>,Arc<[Vec<f64>;3]>,Arc<[usize]>)>,
    kinv: DMat3,
    mass_matrix:DMat3,
    axis: DVec3,
    target: DVec3,
    lambda: DVec3,
    mu: f64,
    held: Option<(usize, usize)>,
    scale: DVec3,
    warm_divisor:DVec3,
    ob: Option<(i32, usize, Vec3, Vec3)>,
    at: Spot,
    pin: bool,
    grip: Option<(usize, f32)>,
    external: Option<ExternalContact>,
}

#[derive(Clone,Copy,PartialEq)]
struct ExternalContact {id:u64,point:Vec3,basis:[Vec3;3]}

#[cfg(test)]thread_local!{static BOUND_COEFFICIENT_REUSE:std::cell::Cell<bool>=const{std::cell::Cell::new(true)};}
fn reuse_bound_coefficients()->bool{
    #[cfg(test)]{return BOUND_COEFFICIENT_REUSE.with(|v|v.get());}
    #[cfg(not(test))]{
        static ON:std::sync::OnceLock<bool>=std::sync::OnceLock::new();
        *ON.get_or_init(||!(std::env::var_os("GTASA_PASSIVE_TEST_WINDOW").is_some()&&std::env::var_os("GTASA_BOUND_COEFFICIENTS_BASELINE").is_some()))
    }
}

// Reuse a contact's exact response/mass only for bit-identical f64 rows
// on the same forced link and the SAME Artic instance. Targets, friction,
// warm impulses, contact identity and the ordered solve remain independent.
#[derive(Default)]
struct ContactResponses(std::collections::HashMap<(usize,[u64;18]),([Vec<f64>;3],DMat3)>);
impl ContactResponses{
 fn solve(&mut self,artic:&Artic,k:usize,j:&[Vec<f64>;3])->([Vec<f64>;3],DMat3){
  debug_assert!(j.iter().all(|r|r.iter().enumerate().all(|(i,v)|i/6==k||*v==0.)));
  let key=(k,std::array::from_fn(|i|j[i/6][k*6+i%6].to_bits()));
  self.0.entry(key).or_insert_with(||{let mj=artic.solve_rows([&j[0],&j[1],&j[2]]);let mass=Block::contact_mass(artic,j);(mj,mass)}).clone()
 }
}

impl Block {
    fn nonzero_indices(j:&[Vec<f64>;3])->Arc<[usize]>{
        // Same ordered entries as impulses()' original link scan, including
        // signed zero exclusion and nonfinite values. No magnitude threshold.
        (0..j[0].len()/6*6).filter(|&i|j.iter().any(|j|j[i]!=0.)).collect::<Vec<_>>().into()
    }
    fn new(artic: &Artic, j: [Vec<f64>; 3], target: DVec3, mu: f64, held: Option<(usize, usize)>, warm: Vec3, self_contact: bool) -> Block {
        let mj = artic.solve_rows([&j[0][..], &j[1], &j[2]]);
        Self::prepared(artic,j,mj,target,mu,held,warm,self_contact)
    }
    fn cached(artic:&Artic,k:usize,j:[Vec<f64>;3],target:DVec3,mu:f64,held:Option<(usize,usize)>,warm:Vec3,cache:&mut ContactResponses)->Self{
        let(mj,mass)=cache.solve(artic,k,&j);
        Self::with(j,mj,mass,target,mu,held,warm)
    }
    fn prepared(artic:&Artic,j:[Vec<f64>;3],mj:[Vec<f64>;3],target:DVec3,mu:f64,held:Option<(usize,usize)>,warm:Vec3,self_contact:bool)->Self{
        let baseline={static ON:std::sync::OnceLock<bool>=std::sync::OnceLock::new();*ON.get_or_init(||std::env::var_os("GTASA_PASSIVE_TEST_WINDOW").is_some()&&std::env::var_os("GTASA_BLOCK_MASS_BASELINE").is_some())};
        let k=if self_contact||baseline||!car_slot_index_enabled(){Self::original_mass(artic,&j,&mj,self_contact)}else{Self::contact_mass(artic,&j)};
        Block::with(j, mj, k, target, mu, held, warm)
    }

    fn original_mass(artic:&Artic,j:&[Vec<f64>;3],mj:&[Vec<f64>;3],self_contact:bool)->DMat3 {
        let on: [Vec<bool>; 3] = std::array::from_fn(|a| if self_contact { vec![] } else { j[a].chunks(delirium_data::kv!(6 int)).map(|x| x.iter().any(|&v| v != 0.0)).collect() });
        let ans: [Vec<V6>; 3] = std::array::from_fn(|b| on[0].iter().enumerate().map(|(c, _)| if on[0][c] || on[1][c] || on[2][c] {
            artic.own(c, std::array::from_fn(|i| j[b][delirium_data::kv!(6 int) * c + i]))
        } else { [0.0; 6] }).collect());
        let own = |a: usize, b: usize| -> f64 {
            (0..on[a].len()).filter(|&c| on[a][c] || on[b][c]).map(|c| j[a][delirium_data::kv!(6 int) * c..delirium_data::kv!(6 int) * c + delirium_data::kv!(6 int)].iter().zip(ans[b][c]).map(|(x, y)| x * y).sum::<f64>()).sum()
        };
        let k = DMat3::from_cols_array_2d(&std::array::from_fn(|a| std::array::from_fn(|b| {
            if self_contact { j[b].iter().zip(&mj[a]).map(|(j, m)| j * m).sum() } else { own(a, b) }
        })));
        k
    }

    fn contact_mass(artic:&Artic,j:&[Vec<f64>;3])->DMat3 {
        // Most external contacts act at exactly one link. Keep the original
        // component and dot-product order, but prepare only nonzero links.
        // This removes temporary arrays for EVERY zero link on EVERY triangle;
        // it does not merge contact points or approximate articulated response.
        let active:Vec<_>=(0..j[0].len()/6).filter(|&c|
            j.iter().any(|r|r[c*6..c*6+6].iter().any(|&v|v!=0.))).collect();
        let one=|c:usize| std::array::from_fn::<_,3,_>(|b|
            artic.own(c,std::array::from_fn(|i|j[b][6*c+i])));
        let dot=|a:usize,c:usize,answer:V6|j[a][6*c..6*c+6].iter().zip(answer).map(|(x,y)|x*y).sum::<f64>();
        if active.len()==1 {
            let c=active[0];let answers=one(c);
            return DMat3::from_cols_array_2d(&std::array::from_fn(|a|std::array::from_fn(|b|{
                if j[a][6*c..6*c+6].iter().chain(&j[b][6*c..6*c+6]).any(|&v|v!=0.){0.+dot(a,c,answers[b])}else{0.}
            })));
        }
        let answers:Vec<_>=active.iter().map(|&c|one(c)).collect();
        DMat3::from_cols_array_2d(&std::array::from_fn(|a|std::array::from_fn(|b|
            active.iter().zip(&answers).filter(|(c,_)|j[a][6**c..6**c+6].iter().chain(&j[b][6**c..6**c+6]).any(|&v|v!=0.))
                .map(|(&c,ans)|dot(a,c,ans[b])).sum()
        )))
    }

    fn with(j: [Vec<f64>; 3], mj: [Vec<f64>; 3], k: DMat3, target: DVec3, mu: f64, held: Option<(usize, usize)>, warm: Vec3) -> Block {
        let nonzero=Self::nonzero_indices(&j);
        Block { j:Arc::new(j), mj:Arc::new(mj), nonzero, unbound:None, kinv: ldl_inverse(k), mass_matrix:k, axis: k.row(0) / k.x_axis.x, target, lambda: warm.as_dvec3(), mu, held, scale: DVec3::ONE, warm_divisor:DVec3::ONE, ob: None, at: Spot::OWN, pin: false, grip: None, external:None }
    }

    /// Original standalone NM stores an unscaled solve impulse, then applies
    /// restitution in middle(). A shared island cannot apply that multiplier
    /// AFTER other finite/world rows have balanced the velocity. Express the
    /// same restitution target using the actual physical impulse instead.
    fn physical_impulse(&mut self,initial:&[f64]){
        let before=DVec3::from_array(std::array::from_fn(|a|self.j[a].iter().enumerate().map(|(k,j)|j*initial[self.at.of(k)]).sum()));
        self.target=self.scale*self.target+(DVec3::ONE-self.scale)*before;
        self.lambda*=self.scale;self.warm_divisor=self.scale;self.scale=DVec3::ONE;
    }

    fn bind_external(mut self,owner:&crate::external_mobility::ExternalOwner,offset:usize,owner_offset:usize,velocity:bool)->Self {
        let contact=self.external.unwrap();let r=contact.point-owner.com;
        let rows=contact.basis.map(|n|{let f=-n;let t=r.cross(f);[f.x as f64,f.y as f64,f.z as f64,t.x as f64,t.y as f64,t.z as f64]});
        let responses=rows.map(|j|owner.response(j));
        let k=DMat3::from_cols_array_2d(&std::array::from_fn(|a|std::array::from_fn(|b|rows[b].iter().zip(responses[a]).map(|(x,y)|x*y).sum::<f64>())));
        let own_len=self.j[0].len();
        if reuse_bound_coefficients(){self.unbound=Some((self.j.clone(),self.mj.clone(),self.nonzero.clone()));}
        for a in 0..3{Arc::make_mut(&mut self.j)[a].extend(rows[a]);Arc::make_mut(&mut self.mj)[a].extend(responses[a]);}
        let mut nonzero=self.nonzero.to_vec();
        nonzero.extend((own_len..own_len+6).filter(|&i|self.j.iter().any(|j|j[i]!=0.)));self.nonzero=nonzero.into();
        let mass=self.mass_matrix+k;self.mass_matrix=mass;self.kinv=ldl_inverse(mass);self.axis=mass.row(0)/mass.x_axis.x;
        if velocity {
            // Component motion relative to its owner (e.g. a native door) stays
            // on the RHS. The owner's shared velocity is now in the Jacobian.
            let v=owner.point_velocity(contact.point);
            self.target-=DVec3::from_array(contact.basis.map(|n|n.dot(v) as f64));
        }
        self.at=Spot{at:[offset,owner_offset],cut:own_len};self
    }
    fn bind_external_pair(self,push:Self,owner:&crate::external_mobility::ExternalOwner,offset:usize,owner_offset:usize)->(Self,Self){
        // Velocity and position contacts share their original coefficients,
        // but never their mutable targets/impulses. Bind their finite owner
        // once only when all coefficient inputs are actually identical.
        let same=reuse_bound_coefficients()&&Arc::ptr_eq(&self.j,&push.j)&&Arc::ptr_eq(&self.mj,&push.mj)
            &&self.external==push.external
            &&self.mass_matrix.to_cols_array().map(f64::to_bits)==push.mass_matrix.to_cols_array().map(f64::to_bits);
        let bound=self.bind_external(owner,offset,owner_offset,true);
        let push=if same{Self{j:bound.j.clone(),mj:bound.mj.clone(),nonzero:bound.nonzero.clone(),unbound:bound.unbound.clone(),kinv:bound.kinv,
            mass_matrix:bound.mass_matrix,axis:bound.axis,at:bound.at,..push}}
            else{push.bind_external(owner,offset,owner_offset,false)};
        (bound,push)
    }
    fn own_external_side(mut self)->Self {
        if self.external.is_some()&&self.at.cut!=usize::MAX {
            if let Some((j,mj,nonzero))=self.unbound.take(){self.j=j;self.mj=mj;self.nonzero=nonzero;}
            else{for j in Arc::make_mut(&mut self.j){j.truncate(self.at.cut);}for j in Arc::make_mut(&mut self.mj){j.truncate(self.at.cut);}
                self.nonzero=self.nonzero.iter().copied().filter(|&i|i<self.at.cut).collect::<Vec<_>>().into();}
        }
        self.at=Spot::OWN;self
    }

    fn apply(&self, d: DVec3, qd: &mut [f64]) {
        for (q, e) in self.at.runs(self.mj[0].len()) {
            for (((q, x), y), z) in qd[q].iter_mut().zip(&self.mj[0][e.clone()]).zip(&self.mj[1][e.clone()]).zip(&self.mj[2][e]) {
                *q += x * d.x + y * d.y + z * d.z;
            }
        }
    }

    fn solve(&mut self, qd: &mut [f64], nz: &[(usize, usize)]) -> f64 {
        let v = DVec3::from_array(std::array::from_fn(|a| nz.iter().map(|&(i, g)| self.j[a][i] * qd[g]).sum()));
        let mut l = self.lambda - self.kinv * (v - self.target);
        let an = self.axis.dot(l);
        let tt = l.y * l.y + l.z * l.z;
        let g = DVec3::new(-tt.sqrt() * self.mu, l.y, l.z);
        let way = DVec3::new(l.x - an, l.y, l.z);
        let over = g.dot(l);
        if over > 0.0 && !self.pin {
            l -= way * (over / (g.dot(way) + delirium_data::kv!(242)));
        }
        if an < 0.0 && !self.pin {
            l = DVec3::ZERO;
        }
        let d = l - self.lambda;
        self.lambda = l;
        self.apply(d, qd);
        d.length_squared()
    }
}

fn ldl_inverse(k: DMat3) -> DMat3 {
    let (a00, a10, a20, a11, a21, a22) = (k.x_axis.x, k.x_axis.y, k.x_axis.z, k.y_axis.y, k.y_axis.z, k.z_axis.z);
    let eps = (a11 + a22 + a00) * delirium_data::kv!(566);
    let d0 = a00.max(eps);
    let i0 = 1.0 / d0;
    let (l10, l20) = (a10 * i0, a20 * i0);
    let b11 = (-a10).mul_add(l10, a11);
    let b21 = (-a10).mul_add(l20, a21);
    let b22 = (-a20).mul_add(l20, a22);
    let i1 = 1.0 / b11.max(eps);
    let l21 = i1 * b21;
    let m10 = i1 * l10;
    let c22 = (-l21).mul_add(b21, b22);
    let n10 = m10 * l10;
    let p = l21.mul_add(l10, -l20);
    let v22 = 1.0 / c22.max(eps);
    let v12 = -(v22 * l21);
    let v02 = p * v22;
    let v11 = (-v12).mul_add(l21, i1);
    let v01 = -(v02 * l21) - m10;
    let v00 = v02.mul_add(p, n10) + i0;
    DMat3::from_cols(DVec3::new(v00, v01, v02), DVec3::new(v01, v11, v12), DVec3::new(v02, v12, v22))
}

fn impulses(rows: &mut [Row], blocks: &mut [Block], qd: &mut [f64]) {
    impulses_observed(rows,blocks,qd,None);
}
// Optional CPU-only diagnostic, supplied only by an explicitly profiled group.
fn impulses_observed(rows:&mut [Row],blocks:&mut [Block],qd:&mut [f64],mut profile:Option<&mut ([f64;3],[usize;5])>){
    let mut timer=profile.as_ref().map(|_|std::time::Instant::now());
    let mut lap=|at:usize,p:&mut Option<&mut ([f64;3],[usize;5])>|{if let (Some(t),Some(p))=(timer,p.as_deref_mut()){
        let now=std::time::Instant::now();p.0[at]+=now.duration_since(t).as_secs_f64()*1000.;timer=Some(now);
    }};
    if let Some(p)=profile.as_deref_mut(){p.1[0]+=1;p.1[1]+=rows.len();p.1[2]+=blocks.len();}
    let apply = |row: &Row, d: f64, qd: &mut [f64]| {
        for (q, e) in row.at.runs(row.mj.len()) {
            for (q, m) in qd[q].iter_mut().zip(&row.mj[e]) {
                *q += m * d;
            }
        }
    };
    for b in blocks.iter() {
        b.apply(b.lambda, qd);
    }
    // Scalar normal rows now also carry original persistent-manifold pushes.
    // Apply that warm start exactly as for the vector contact blocks; merely
    // seeding lambda would subtract a force never applied to the velocity.
    for r in rows.iter(){apply(r,r.lambda,qd);}
    lap(0,&mut profile);
    let mut flat = Vec::with_capacity(delirium_data::kv!(23 int) * (blocks.len() + rows.len()));
    let mut nz = |j: &[&Vec<f64>], at: Spot| {
        let from = flat.len();
        for c in (0..j[0].len() / delirium_data::kv!(6 int)).filter(|&c| j.iter().fold(0, |a, j| j[delirium_data::kv!(6 int) * c..delirium_data::kv!(6 int) * c + delirium_data::kv!(6 int)].iter().fold(a, |a, x| a | x.to_bits() << 1)) != 0) {
            flat.extend((delirium_data::kv!(6 int) * c..delirium_data::kv!(6 int) * c + delirium_data::kv!(6 int)).filter(|&i| j.iter().any(|j| j[i] != 0.0)).map(|i| (i, at.of(i))));
        }
        from..flat.len()
    };
    // The immutable coefficient topology is already known. The original
    // scanner remains the passive exact control; only global Spot offsets
    // must be mapped anew for each island/preview solve.
    let baseline=!car_slot_index_enabled()||env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_CONTACT_ROW_INDEX_BASELINE");
    let (bnz, rnz): (Vec<_>, Vec<_>) = if baseline{
        (blocks.iter().map(|b| nz(&[&b.j[0], &b.j[1], &b.j[2]], b.at)).collect(), rows.iter().map(|r| nz(&[&r.j], r.at)).collect())
    }else{
        let rnz=rows.iter().map(|r|nz(&[&r.j],r.at)).collect();
        let bnz=blocks.iter().map(|b|{let from=flat.len();flat.extend(b.nonzero.iter().map(|&i|(i,b.at.of(i))));from..flat.len()}).collect();
        (bnz,rnz)
    };
    lap(1,&mut profile);
    if let Some(p)=profile.as_deref_mut(){p.1[3]+=flat.len();}
    // A body wedged under a car made thousands of rows: 206 ms in one frame, and the frame after it
    // worse (07/11). Ordinary solves end far under this; a wedged one stops with what it has.
    let budget=std::time::Instant::now();
    for sweep in 0..SWEEPS() {
        if sweep>=2&&budget.elapsed().as_micros()>2000{break;}
        if let Some(p)=profile.as_deref_mut(){p.1[4]+=1;}
        let mut moved: f64 = blocks.iter_mut().zip(&bnz).map(|(b, nz)| b.solve(qd, &flat[nz.clone()])).sum();
        for r in 0..rows.len() {
            let lo = if matches!(rows[r].kind, RowKind::Push) { 0.0 } else { f64::MIN };
            let row = &mut rows[r];
            let v: f64 = flat[rnz[r].clone()].iter().map(|&(i, g)| row.j[i] * qd[g]).sum();
            let was = row.lambda;
            row.lambda = (was + (row.target - v) / row.eff*row.relaxation).max(lo);
            let d = row.lambda - was;
            moved += d * d;
            apply(row, d, qd);
            if let RowKind::Friction { of, mu } = rows[r].kind {
                if r == of + 2 {
                    let (a, b) = (rows[of + 1].lambda, rows[of + 2].lambda);
                    let (len, most) = ((a * a + b * b).sqrt(), mu * rows[of].lambda);
                    if len > most {
                        let k = most / len;
                        for (t, l) in [(of + 1, a), (of + 2, b)] {
                            rows[t].lambda = l * k;
                            apply(&rows[t], l * k - l, qd);
                        }
                    }
                }
            }
        }
        if moved == 0.0 {
            break;
        }
    }
    lap(2,&mut profile);
}

#[allow(non_snake_case)]
#[inline]
fn ALLOW() -> f32 { delirium_data::kv!(567) }
fn elastic(e: f64) -> DVec3 {
    if env_on!("NM_NOSCALE") { return DVec3::ONE; }
    DVec3::splat(1.0 + e)
}
#[allow(non_snake_case)]
#[inline]
fn BOX_MARGIN() -> f32 { delirium_data::kv!(48) }

#[derive(Clone, Copy)]
struct Held { local: Vec3, ground: Vec3, gap: f32, age: u16, m: u8, fl:u8, imp: Vec3, pimp: Vec3, n: Vec3, inst: u32, off: bool }
fn hold(held: &mut Vec<Held>, new: Held) {
    if new.gap >= delirium_data::kv!(109) {
        return;
    }
    let mut near = (None, delirium_data::kv!(568));
    for (k, r) in held.iter().enumerate() {
        let d = r.local.distance_squared(new.local);
        if d < near.1 && r.age != 0 {
            near = (Some(k), d);
        }
    }
    match near.0 {
        Some(k) => held[k] = Held { imp: held[k].imp, pimp: held[k].pimp, ..new },
        None if held.len() < 6 => held.push(new),
        None => if let Some(k) = replaced(held, new.local) { held[k] = new },
    }
}

fn replaced(kept: &[Held], new: Vec3) -> Option<usize> {
    let mut p: Vec<Vec3> = kept.iter().map(|r| r.local).chain([new]).collect();
    let mut idx: Vec<usize> = (0..p.len()).collect();
    let (len, last) = (p.len(), kept.len());
    let a = p[1] - p[0];
    let mut n = a.cross(p[2] - p[0]);
    for k in delirium_data::kv!(0 int)..last {
        if n.length_squared() >= delirium_data::kv!(47) {
            break;
        }
        n = a.cross(p[k] - p[0]);
    }
    let d = n.cross(a);
    let (mut m, mut bar) = (0, Vec3::splat(delirium_data::kv!(569)));
    for (i, q) in p.iter().enumerate() {
        if d.dot(*q) < bar.max_element() {
            (m, bar) = (i, *q);
        }
    }
    p.swap(0, m);
    idx.swap(0, m);
    for _ in 0..last {
        let mut swapped = false;
        for i in 1..len - 1 {
            if n.dot((p[i] - p[0]).cross(p[i + 1] - p[0])) < 0.0 {
                p.swap(i, i + 1);
                idx.swap(i, i + 1);
                swapped = true;
            }
        }
        if !swapped {
            break;
        }
    }
    let (mut at, mut least) = (0, delirium_data::kv!(569));
    for k in 0..len {
        let (j, l) = ((k + 1) % len, (k + 2) % len);
        let s = n.dot((p[j] - p[k]).cross(p[l] - p[k]));
        if idx[j] == last {
            if s < 0.0 {
                return None;
            }
        } else if s < least {
            (at, least) = (j, s);
        }
    }
    Some(idx[at])
}

fn swept(now: (Vec3, Mat3), last: (Vec3, Mat3), half: Vec3) -> (Vec3, Vec3) {
    let r = now.1.transpose() * last.1;
    let t = now.1.transpose() * (last.0 - now.0);
    let h = Mat3::from_cols(r.x_axis.abs(), r.y_axis.abs(), r.z_axis.abs()) * half;
    let (lo, hi) = ((-half).min(t - h), half.max(t + h));
    ((lo + hi) * 0.5, (hi - lo) * 0.5)
}

fn swept_box(p: &PartDef, f: (Vec3, Mat3), s: (Vec3, Mat3)) -> (Vec3, Vec3) {
    let (o, half) = swept(f, s, bound_half(p));
    (f.0 + f.1 * o, half)
}

// Original child broadphase (829C06A0): both bodies move. A current-only
// obstacle bound can reject a crossing before the relative-motion cast runs.
// Keep the child's local bound centre; COL triangles need not be centred.
#[cfg(test)]
thread_local!{static LINEAR_CAR_SLOTS:std::cell::Cell<bool>=const{std::cell::Cell::new(false)};}
fn car_slot_index_enabled()->bool{
    #[cfg(test)] if LINEAR_CAR_SLOTS.with(|v|v.get()){return false;}
    !(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_LINEAR_HELD_CAR_BASELINE"))
}

fn moving_child_meets(a: (Vec3, Mat3), swept_a: (Vec3, Vec3),
                      child: (Vec3, Mat3), last_child: (Vec3, Mat3),
                      centre: Vec3, half: Vec3) -> bool {
    let (x, tm) = a;
    let (sc, sh) = swept_a;
    let (cx, cq) = child;
    let (lx, lq) = last_child;
    let (cs, ch) = swept((cx + cq * centre, cq), (lx + lq * centre, lq), half);
    let ac = cq.transpose() * (x + tm * sc - cx) - (centre + cs);
    let r = cq.transpose() * tm;
    let ah = Mat3::from_cols(r.x_axis.abs(), r.y_axis.abs(), r.z_axis.abs()) * sh;
    ac.abs().cmple(ah + ch + Vec3::splat(delirium_data::kv!(571))).all()
}

fn boxes_meet(ca: Vec3, a: Mat3, ha: Vec3, cb: Vec3, b: Mat3, hb: Vec3) -> bool {
    let ax = [a.x_axis, a.y_axis, a.z_axis];
    let bx = [b.x_axis, b.y_axis, b.z_axis];
    let d = cb - ca;
    let reach = |u: Vec3, ax: &[Vec3; 3], h: Vec3| (0..delirium_data::kv!(0 int)).map(|i| ax[i].dot(u).abs() * h[i]).sum::<f32>();
    let crosses = ax.into_iter().flat_map(|u| bx.map(|v| u.cross(v))).filter(|c| c.length_squared() > delirium_data::kv!(248));
    ax.into_iter().chain(bx).chain(crosses).all(|u| d.dot(u).abs() <= reach(u, &ax, ha) + reach(u, &bx, hb))
}

fn box_meets_tri(c: Vec3, m: Mat3, e: Vec3, t: [Vec3; 3]) -> bool {
    let v = t.map(|p| p - c);
    let apart = |a: Vec3| {
        let p = v.map(|x| x.dot(a));
        let r = (m.transpose() * a).abs().dot(e);
        p[0].min(p[1]).min(p[2]) > r || p[0].max(p[1]).max(p[2]) < -r
    };
    ![(v[1] - v[0]).cross(v[2] - v[0]), m.x_axis, m.y_axis, m.z_axis].into_iter().any(apart)
}

fn part_shape(p: &PartDef) -> gjk::Shape {
    match p.boxed {
        Some(e) => gjk::Shape::Box(e - Vec3::splat(BOX_MARGIN()), BOX_MARGIN()),
        None => gjk::Shape::Capsule(p.half, p.radius),
    }
}

/// A read-only query of the same part pair manifold used by the group solver.
/// `point_a - point_b` projected on `normal` is `gap` (negative penetration).
/// This is transport for an animation/physics ownership boundary, not a force.
#[derive(Clone, Copy, Debug)]
pub struct PartPairContact {
    pub a: usize,
    pub b: usize,
    pub point_a: Vec3,
    pub point_b: Vec3,
    pub normal: Vec3,
    pub gap: f32,
}

#[path = "ragdoll_animated.rs"]
mod animated_queries;
#[path = "ragdoll_external_pairs.rs"]
mod external_pairs;
#[path = "ragdoll_contact_diagnostics.rs"]
mod contact_diagnostics;
#[path = "ragdoll_external_world.rs"]
mod external_world;
#[path = "ragdoll_external_ground.rs"]
mod external_ground;
#[cfg(test)]
#[path = "ragdoll_external_drive_tests.rs"]
mod external_drive_tests;

impl Ragdoll {
    pub fn query_pose_contacts(&self, build: &Build, pose: &[Transform], previous: &[Transform],
                               other: &Self, other_build: &Build, other_pose: &[Transform], other_previous: &[Transform]) -> Vec<PartPairContact> {
        let valid = |d: &Self, b: &Build, p: &[Transform], old: &[Transform]|
            d.bodies.len() == b.parts.len() && p.len() == b.parts.len() && old.len() == p.len();
        if !valid(self, build, pose, previous) || !valid(other, other_build, other_pose, other_previous) { return vec![]; }
        // An NM part's collision axes include its link/attachment basis. Raw
        // bone quaternions are not collider quaternions (notably the pelvis).
        let frame = |body: &Body, p: &Transform| {
            let mut body = body.clone(); body.set_q(p.rotation);
            (p.translation, turn(&body.bound()))
        };
        let frames = |d: &Self, b: &Build, p: &[Transform], old: &[Transform]| -> Vec<_> {
            d.bodies.iter().enumerate().map(|(k, body)| {
                let (f, s) = (frame(body, &p[k]), frame(body, &old[k]));
                (f, s, swept_box(&b.parts[k], f, s))
            }).collect()
        };
        let (aa, bb) = (frames(self, build, pose, previous), frames(other, other_build, other_pose, other_previous));
        let mut out = vec![];
        for (a, &(fa, sa, ba)) in aa.iter().enumerate() {
            if (self.contact_off | self.gone) >> a & 1 != 0 { continue; }
            for (b, &(fb, sb, bc)) in bb.iter().enumerate() {
                if (other.contact_off | other.gone) >> b & 1 != 0 { continue; }
                if ba.0.distance_squared(bc.0) > (ba.1.length() + bc.1.length()).powi(2) { continue; }
                let mut held = vec![];
                Self::pair_manifold(&mut held, (&build.parts[a], fa, sa), (&other_build.parts[b], fb, sb), [ba, bc]);
                for r in held {
                    out.push(PartPairContact { a, b, point_a: fa.0 + fa.1 * r.local,
                        point_b: fb.0 + fb.1 * r.ground, normal: r.n, gap: r.gap });
                }
            }
        }
        out
    }
}

/// Penetration (m) a part on the ignored vehicle may still have and count as out of it: a resting contact
/// leaves a few cm, which the solver separates gently; a straddling thigh in the seat is far deeper.
const NC_OUT: f32 = 0.03;

fn bound_half(p: &PartDef) -> Vec3 {
    p.boxed.unwrap_or(Vec3::new(p.radius, p.half + p.radius, p.radius))
}

#[allow(non_snake_case)]
#[inline]
fn PUSH_KEEP() -> f32 { delirium_data::kv!(567) }
fn out(gap: f32) -> f32 {
    -gap - (-gap * 0.5).clamp(0.0, PUSH_KEEP())
}

impl Ragdoll {
    fn push_block(b: &Block, over: f32, h: f32, last: Vec3) -> Block {
        Block { target: DVec3::new((out(over) / h) as f64, 0.0, 0.0), lambda: last.as_dvec3(), held: None, scale: DVec3::ONE, ..b.clone() }
    }
}

#[allow(non_snake_case)]
#[inline]
fn SWEEPS() -> usize { delirium_data::kv!(23 int) }
#[allow(non_snake_case)]
#[inline]
fn MAX_SPEED() -> f32 { delirium_data::kv!(59) }
const MAX_SPIN: f32 = std::f32::consts::TAU;
#[allow(non_snake_case)]
#[inline]
pub(crate) fn MAX_SPIN_NM() -> f32 { delirium_data::kv!(5) * std::f32::consts::TAU }
#[allow(non_snake_case)]
#[inline]
pub(crate) fn DAMPING() -> f32 { delirium_data::kv!(73) }
#[allow(non_snake_case)]
#[inline]
fn LIMIT_MARGIN() -> f32 { delirium_data::kv!(122) }

#[allow(non_snake_case)]
#[inline]
fn LIMIT_GAIN() -> f32 { delirium_data::kv!(104) }

fn hinge_limits(lo: f32, hi: f32) -> (f32, f32) {
    let (l, h) = (LIMIT_MARGIN() + lo, hi - LIMIT_MARGIN());
    if l > h { ((lo + hi) * 0.5, (lo + hi) * 0.5) } else { (l, h) }
}

fn lean_limit(st: &crate::rigid::Swing, lean: [f32; 2]) -> Option<f32> {
    let [l0, l1] = lean.map(|l| (l - LIMIT_MARGIN()).max(0.0));
    let (l0, l1) = (l0.max(delirium_data::kv!(68)), l1.max(delirium_data::kv!(68)));
    let (c1, c0) = (sdot(st.s, crate::rigid::col(&st.pr, 1), b"yxz") / l0, sdot(st.s, crate::rigid::col(&st.pr, 0), b"yxz") / l1);
    let e = c0.fma(c0, c1 * c1) * st.theta * st.theta;
    (!(e < 1.0)).then(|| st.theta / e.sqrt())
}

fn twist_limit(st: &crate::rigid::Swing, twist: f32) -> Option<f32> {
    let l = (twist - LIMIT_MARGIN()).max(0.0);
    if !(st.twist - l < 0.0) { Some(l) } else if !(l + st.twist > 0.0) { Some(-l) } else { None }
}
pub const SUBSTEPS: usize = 2;
#[allow(non_snake_case)]
#[inline]
fn GHOST() -> f32 { delirium_data::kv!(57) }

pub struct Ragdoll {
    pub nm: usize,
    bodies: Vec<Body>,
    order: Vec<usize>,
    pub mode: u8,
    normal_support_owners:std::collections::BTreeSet<u64>,
    /// The external owner its ped collides with no longer (CPhysical +0x1F4,
    /// 8247EEA8 with flag 0x40: the jump out of a vehicle, 824BD6DC): that
    /// owner's points are found and dropped (82480B88 at impact stage 1), and
    /// whether any were found since the flag was last read (+0x1F1 bit 1;
    /// 8247EF08 ends the exclusion on a frame with none).
    pub no_collision:Option<u64>,
    pub no_collision_met:bool,
    /// Last step's no-collision evidence: 1 owner staged, 2 owner box overlaps the body, 4 a contact or child bound met, 8 a part came out by its own shape.
    pub no_collision_seen:u8,
    /// Parts still ignoring `no_collision` (bit per body; all bits until the first staged step picks them).
    pub no_collision_parts:u32,
    /// Parts the host keeps ignoring `no_collision` whatever they touch (a windscreen ejection: legs until past the glass).
    pub no_collision_hold:u32,
    still: [u32; 2],
    pub sleep_tol: [f32; 3],
    pub asleep: bool,
    pub age: f32,
    pub fresh: bool,
    pub touch: Vec<u8>,
    pub contact_off: u32,
    pub hit: Vec<Option<(Vec3, Vec3, i32)>>,
    pub contact: crate::bump::ContactRecord<i32>,
    pub ob_hits: Vec<(i32, Vec3, Vec3)>,
    /// Diagnostics only: the deepest signed gap of every contact this step's
    /// solve collected (state left by the previous step), per category.
    pub gap_audit: [GapAudit; GAP_CATEGORIES],
    held: Vec<Vec<Held>>,
    pub stage_vel: Vec<(Vec3, Vec3)>,
    pub stage_lt: Vec<V6>,
    /// Original jump-roll's acceleration applied as mass-scaled body forces.
    /// The host renews this lease each physics step while the task is active.
    pub roll_push: Option<(crate::jumproll::Set, Vec3, bool)>,
    pub stage_lim: Vec<Vec3>,
    stiff: Vec<f32>,
    pins: Vec<Option<Transform>>,
    cons: Vec<(Option<Vec3>, Option<Quat>)>,
    pub grips: Vec<(usize, Vec3, Vec3, f32, f32)>,
    /// Measured world point velocities, keyed by gripped part. An absent
    /// entry retains the stationary-anchor semantics of direct/static grips.
    pub grip_target_velocities: Vec<(usize, Vec3)>,
    pub grip_target_owners: Vec<(usize,u64)>,
    pub snapped: Vec<usize>,
    /// Lifetime peak axial grip impulse, its break limit, last break impulse,
    /// and break count. Observation only; never fed back into the solver.
    pub grip_load: [f32; 4],
    pub grip_solved_target: Option<Vec3>,
    pub gone: u32,
    grip_rows: Vec<(usize, usize, f32)>,
    pub pairs: Vec<(usize, usize)>,
    seen: Vec<(Vec3, Mat3)>,
    // Explicit animation-to-physics transport. World-space previous collider
    // frames survive recentering and are consumed by exactly the next solve.
    contact_history: Option<Vec<(Vec3, Mat3)>>,
    coll: Vec<(Vec3, [Vec3; 3])>,
    held_pairs: Vec<Vec<Held>>,
    held_car: Vec<(usize, i32, usize, Vec<Held>)>,
    pub prep_twist: Vec<Option<f32>>,
    pub rigid: Option<(Vec<crate::rigid::Link>, Vec3, Vec<crate::nmdbm::M34>)>,
    pub inertia: Option<Vec<Vec3>>,
    pub inst: Option<[Vec3; 4]>,
    pending: Vec<(usize, Vec3, Vec3, Vec3)>,
    pub out: Vec<(Vec3, Vec3)>,
    pub joint_torque: Vec<Vec3>,
    pub armature: Vec<f32>,
    o: Vec3,
    pub bound_off: Vec3,
    rejuv: u32,
    mwc: (u32, u32),
}

fn turn(r: &[Vec3; 3]) -> Mat3 {
    Mat3::from_cols(r[0], r[1], r[2])
}

pub fn orthonormalise(r: &mut [Vec3; 3]) {
    let norm = |v: Vec3| {
        let l = v.z.mul_add(v.z, v.x.mul_add(v.x, v.y * v.y));
        if l > 0.0 { v * (1.0 / l.sqrt()) } else { Vec3::X }
    };
    let (a, b) = (norm(r[0]), r[1]);
    let d = -a.x.mul_add(b.x, b.y.mul_add(a.y, b.z * a.z));
    let b = norm(Vec3::new(a.x.mul_add(d, b.x), a.y.mul_add(d, b.y), d.mul_add(a.z, b.z)));
    r[2] = Vec3::new(a.y.mul_add(b.z, -(b.y * a.z)), b.x.mul_add(a.z, -(b.z * a.x)), b.y.mul_add(a.x, -(a.y * b.x)));
    (r[0], r[1]) = (a, b);
}

pub fn start_motion(a: &[Vec3; 4], b: &[Vec3; 4], c: Vec3, inv_dt: f32, scale: f32, max_v: f32, max_w: f32, v_ref: Vec3) -> (Vec3, Vec3) {
    let d4 = |r: Vec3, p: f32| (((c.x as f64 * r.x as f64) + (c.z as f64 * r.z as f64)) + ((c.y as f64 * r.y as f64) + p as f64)) as f32;
    let dp = b[delirium_data::kv!(0 int)] - a[delirium_data::kv!(0 int)];
    let mv = Vec3::new(d4(b[0] - a[0], dp.x), d4(b[1] - a[1], dp.y), d4(b[2] - a[2], dp.z));
    let col = |m: &[Vec3; 4], i: usize| Vec3::new(m[0][i], m[1][i], m[2][i]);
    let row = |i: usize| Vec3::new(col(b, i).vd(col(a, 0)), col(b, i).vd(col(a, 1)), col(b, i).vd(col(a, 2)));
    let (axis, angle) = crate::nmhf::axis_angle(crate::nmhf::quat_of(&Mat3::from_cols(row(0), row(1), row(2))));
    let v = mv * inv_dt * scale - v_ref;
    (clamp_len(v, max_v) + v_ref, clamp_len(axis * angle * -inv_dt * scale, max_w))
}

fn clamp_len(v: Vec3, max: f32) -> Vec3 {
    let l2 = v.z.mul_add(v.z, v.x.mul_add(v.x, v.y * v.y));
    if l2 <= max * max {
        return v;
    }
    let (x, mut y) = (l2 as f64, crate::estimate::frsqrte(l2 as f64));
    for _ in 0..2 {
        y = (-(y * y)).mul_add(x, 1.0).mul_add(y * 0.5, y);
    }
    v * ((y as f32) * max)
}

fn short(q: Quat) -> Quat {
    if q.w < 0.0 { -q } else { q }
}

impl Ragdoll {
    pub fn new(build: &Build, frames: &[Transform], v: Vec3) -> Ragdoll {
        let o = frames.first().map_or(Vec3::ZERO, |f| f.translation);
        let nm = crate::nmrs::MASSES[build.nm].get().filter(|m| m.len() == build.parts.len());
        let bodies = build.parts.iter().zip(frames).enumerate().map(|(k, (p, f))| {
            let m = p.mass.max(delirium_data::kv!(73));
            let s = nm.map_or(1.0, |n| n[k] / m);
            let l = nm.map_or(Quat::IDENTITY, |_| crate::nmdbm::rage_link(build.nm, k));
            let z = p.size;
            let att = if nm.is_some() && k == 0 { Mat3::from_rotation_z(std::f32::consts::FRAC_PI_2) } else { Mat3::IDENTITY };
            let att = [att.x_axis, att.y_axis, att.z_axis];
            let i = p.moments();
            let i = if nm.is_some() && k == 0 { Vec3::new(i.y, i.x, i.z) } else { i };
            let mut b = Body { x: f.translation - o, r: [Vec3::ZERO; 3], q: f.rotation, v, w: Vec3::ZERO, m: m * s * z.powi(3) * p.heft, i: i * s * z.powi(3) * p.heft, l, att };
            b.set_q(f.rotation);
            b
        }).collect::<Vec<Body>>();
        let n = bodies.len();
        let mut order: Vec<usize> = (0..n).filter(|&k| build.parts[k].parent.is_none()).collect();
        let mut at = 0;
        while at < order.len() {
            let k = order[at];
            at += 1;
            order.extend((0..n).filter(|&c| build.parts[c].parent == Some(k)));
        }
        Ragdoll { nm: build.nm, pins: vec![None; n], cons: vec![(None, None); n], grips: vec![], grip_target_velocities: vec![], grip_target_owners:vec![], snapped: vec![], grip_load: [0.;4], grip_solved_target: None, gone: 0, grip_rows: vec![], touch: vec![0; n], contact_off: 0, hit: vec![None; n], contact: crate::bump::ContactRecord::new(), ob_hits: vec![], gap_audit: Default::default(), held: vec![vec![]; n], stage_vel: vec![], stage_lt: vec![], roll_push: None, stage_lim: vec![], stiff: vec![], bodies, order, mode: 1, still: [SLEEP_STOP() - 2, SLEEP_SLEEP() - 2],
                  sleep_tol: [SLEEP_V2(), SLEEP_W2(), SLEEP_MEAN2()], asleep: false, age: 0.0, fresh: false, pending: vec![], pairs: vec![], seen: vec![], contact_history: None, coll: vec![], held_pairs: vec![], held_car: vec![], prep_twist: vec![], rigid: None, inertia: None, inst: None, out: vec![(Vec3::ZERO, Vec3::ZERO); n], joint_torque: vec![Vec3::ZERO; n], armature: vec![0.0; n], o, bound_off: Vec3::ZERO,
                  rejuv: 0, mwc: (1, 0), normal_support_owners:Default::default(), no_collision:None, no_collision_met:false, no_collision_seen:0, no_collision_parts:0, no_collision_hold:0 }
    }

    pub fn set_body(&mut self, k: usize, x: Vec3, q: Quat, v: Vec3, w: Vec3) {
        let b = &mut self.bodies[k];
        (b.x, b.v, b.w) = (x - self.o, v, w);
        b.set_q(q.normalize());
        self.wake();
    }

    /// After setting a completed animation pose, carry its preceding actual
    /// pose into collision detection. Never infer this from the frozen body.
    /// Only collision history changes; no force or velocity is applied.
    pub fn set_contact_history(&mut self, previous: &[Transform]) -> bool {
        if previous.len()!=self.bodies.len() || previous.iter().any(|p|
            !p.translation.is_finite() || !p.rotation.is_finite() || !p.rotation.is_normalized()) { return false; }
        self.contact_history=Some(self.bodies.iter().zip(previous).map(|(body,p)| {
            let mut body=body.clone();body.set_q(p.rotation);
            (p.translation,turn(&body.bound()))
        }).collect());
        self.coll=self.comp(&self.inst());
        // Cached own-world rows describe the body before animation took over.
        // Rebuild them from these exact frames; do not warm-start stale loads.
        self.held.iter_mut().for_each(Vec::clear);
        self.held_pairs.iter_mut().for_each(Vec::clear);
        self.held_car.clear();
        self.wake();true
    }

    pub fn pin(&mut self, k: usize, to: Transform) {
        self.pins[k] = Some(Transform { translation: to.translation - self.o, ..to });
    }

    pub fn constrain(&mut self, k: usize, at: Option<Vec3>, turn: Option<Quat>) {
        self.cons[k] = (at, turn);
        if at.is_some() || turn.is_some() {
            self.wake();
        }
    }

    pub fn restiffen(&mut self) {
        self.stiff.clear();
    }

    pub fn set_stiff(&mut self, stiff: Vec<f32>) {
        self.stiff = stiff;
    }

    pub fn unpin(&mut self) {
        self.pins.iter_mut().for_each(|p| *p = None);
        self.age = 0.0;
    }

    pub fn com(&self) -> (Vec3, Vec3) {
        let (mut m, mut x, mut v) = (0.0, Vec3::ZERO, Vec3::ZERO);
        for b in &self.bodies {
            (m, x, v) = (m + b.m, x + b.x * b.m, v + b.v * b.m);
        }
        (x / m + self.o, v / m)
    }

    pub fn part_mass(&self, k: usize) -> f32 {
        self.bodies[k].m
    }

    pub fn mass(&self) -> f32 {
        self.bodies.iter().map(|b| b.m).sum()
    }

    pub fn kinetic(&self) -> f32 {
        let n4 = self.bodies.len() / delirium_data::kv!(1 int) * delirium_data::kv!(1 int);
        let (mut e, mut mass) = (0.0f32, 0.0f32);
        let links = self.rigid.as_ref().map(|r| &r.0);
        for (k, b) in self.bodies.iter().enumerate() {
            let (m, v, w) = match links.and_then(|l| l.get(k)) {
                Some(l) => (l.m, l.v, l.w),
                None => (b.m, b.v, b.w),
            };
            let sq = |v: Vec3| if k < n4 { v.z.fma(v.z, v.x.fma(v.x, v.y * v.y)) } else { v.x.fma(v.x, v.y.fma(v.y, v.z * v.z)) };
            e = (sq(v) * m).fma(0.5, e);
            let i = self.inertia.as_ref().and_then(|i| i.get(k)).copied().unwrap_or(b.i);
            e = (sq(w) * ((i.z + i.y + i.x) * delirium_data::kv!(570))).fma(0.5, e);
            mass += m;
        }
        e / mass
    }

    pub fn collided(&self, parts: &[usize]) -> bool {
        parts.iter().any(|&k| self.touch[k] & delirium_data::kv!(91 int) == delirium_data::kv!(7 int))
    }

    pub fn mobility(&self, build: &Build, rigid: &[Rigid], k: usize) -> [Vec3; 3] {
        let joints: Vec<Option<Joint>> = build.parts.iter().enumerate().map(|(i, p)| p.parent.map(|j| {
            let j = Joint::new(p, j, rigid.get(i));
            Joint { jc: self.jconst(build, i, &j, rigid.get(i)), ..j }
        })).collect();
        let artic = self.artic(&joints);
        [DVec3::X, DVec3::Y, DVec3::Z].map(|e| artic::parts(artic.own(k, v6(DVec3::ZERO, e))).1.as_vec3())
    }

    /// Normal point mobility J M^-1 J^T of the linked body. This is a read-only
    /// query of the same articulated system used for impulses, not the inverse
    /// mass of a detached hand. Includes torque at the actual surface point;
    /// ground/contact constraints and active joint-limit rows are not included.
    pub fn contact_inverse_mass(&self, build: &Build, rigid: &[Rigid], k: usize, at: Vec3, normal: Vec3) -> f32 {
        let joints: Vec<Option<Joint>> = build.parts.iter().enumerate().map(|(i, p)| p.parent.map(|j| {
            let j = Joint::new(p, j, rigid.get(i));
            Joint { jc: self.jconst(build, i, &j, rigid.get(i)), ..j }
        })).collect();
        let artic = self.artic(&joints);
        let root = self.bodies[self.order[0]].x.as_dvec3();
        let row = point_row(self.bodies.len(), root, k, (at - self.o).as_dvec3(), normal.as_dvec3());
        let response = artic.solve_row(&row);
        row.iter().zip(response).map(|(j, v)| j * v).sum::<f64>() as f32
    }

    pub fn vel(&self, k: usize) -> (Vec3, Vec3) { (self.bodies[k].v, self.bodies[k].w) }

    /// Point velocity including impulses queued since the last solver step.
    /// Uses the same articulated propagation as begin(); read-only, without
    /// gravity, drives, external contact constraints or advancing the clock.
    pub fn predicted_contact_velocity(&self, build: &Build, rigid: &[Rigid], k: usize, at: Vec3) -> Vec3 {
        let joints: Vec<Option<Joint>> = build.parts.iter().enumerate().map(|(i, p)| p.parent.map(|j| {
            let j = Joint::new(p, j, rigid.get(i));
            Joint { jc: self.jconst(build, i, &j, rigid.get(i)), ..j }
        })).collect();
        let artic = self.artic(&joints);
        let zero = vec![[0.0; 6]; self.bodies.len()];
        let (v, w) = self.velocities(&artic, &zero, 0.0, false)[k];
        v + w.cross(at - self.o - self.bodies[k].x)
    }

    pub fn tensor(&self, k: usize) -> DMat3 {
        self.bodies[k].tensor()
    }

    pub fn inertia(&self, k: usize, axis: Vec3) -> f32 {
        let b = &self.bodies[k];
        let a = (b.q * b.l).inverse() * axis;
        1.0 / a.dot(b.i.recip() * a).max(delirium_data::kv!(72))
    }

    pub fn link_rows(&self, k: usize) -> [Vec3; 3] {
        self.bodies[k].r
    }

    pub fn centre(&self, k: usize) -> Vec3 {
        self.bodies[k].x + self.o
    }

    pub fn held_world(&self, i: usize) -> Vec<(Vec3, Vec3, f32, u16)> {
        let (b, m) = (&self.bodies[i], self.turns()[i]);
        self.held[i].iter().map(|r| (b.x - self.bound_off + self.o + m * r.local, r.ground, r.gap, r.age)).collect()
    }

    fn inst(&self) -> [Vec3; 3] {
        let r = &self.bodies[self.order[0]].r;
        let c = crate::rigid::nrm(crate::rigid::col(r, 2));
        let a = crate::rigid::nrm(crate::rigid::col(r, 1).vx(c));
        [a, c.vx(a), c]
    }

    fn comp(&self, inst: &[Vec3; 3]) -> Vec<(Vec3, [Vec3; 3])> {
        let xr = self.bodies[self.order[0]].x;
        self.bodies.iter().map(|b| (crate::rigid::ldot(inst, b.x - xr), b.bound().map(|w| crate::rigid::ldot(inst, w)))).collect()
    }

    fn turns(&self) -> Vec<Mat3> {
        let inst = self.inst();
        self.comp(&inst).iter().map(|(_, c)| turn(&crate::rigid::rows_mul(c, &inst))).collect()
    }

    pub fn motion(&self, k: usize) -> (Vec3, Vec3) {
        (self.bodies[k].v, self.bodies[k].w)
    }

    pub fn spin(&mut self, k: usize, w: Vec3) {
        let b = &self.bodies[k];
        let ang = (b.tensor() * w.as_dvec3()).as_vec3();
        self.pending.push((k, b.x, Vec3::ZERO, ang));
        self.wake();
    }

    pub fn frames(&self) -> impl Iterator<Item = Transform> + '_ {
        self.bodies.iter().map(|b| Transform::from_translation(b.x + self.o).with_rotation(b.q))
    }

    // 82993750 / 82993660: hold the point in the collision component's
    // frame, not the NM joint frame or the render bone frame. This host stores
    // the component directly in world axes; the original comp/instance pair
    // composes to the same transform.
    pub(crate) fn grip_local(&self, k: usize, at: Vec3) -> Vec3 {
        let b=&self.bodies[k];
        crate::rigid::ldot(&b.bound(),at-(b.x+self.o))
    }

    pub(crate) fn grip_world(&self, k: usize, local: Vec3) -> Vec3 {
        let b=&self.bodies[k];let r=b.bound();
        b.x+self.o+r[0]*local.x+r[1]*local.y+r[2]*local.z
    }

    pub fn turned(&self, turn: Quat) -> impl Iterator<Item = Quat> + '_ {
        self.bodies.iter().map(move |b| (Quat::from_mat3(&Mat3::from_quat(turn * b.q * b.l)) * b.l.inverse()).normalize())
    }

    pub fn capsules(&self) -> impl Iterator<Item = Transform> + '_ {
        self.bodies.iter().map(|b| Transform::from_translation(b.x + self.o).with_rotation(b.cap()))
    }

    pub fn sever(&mut self, build: &Build, k: usize) -> Option<(Build, Ragdoll, Vec<usize>)> {
        if build.parts.get(k)?.parent.is_none() || self.gone >> k & 1 != 0 {
            return None;
        }
        let under = |mut at: Option<usize>| { while let Some(a) = at { if a == k { return true; } at = build.parts[a].parent; } false };
        let below: Vec<usize> = self.order.iter().copied().filter(|&c| under(Some(c))).collect();
        let parts = below.iter().map(|&c| PartDef { parent: build.parts[c].parent.and_then(|q| below.iter().position(|&b| b == q)), ..build.parts[c].clone() }).collect();
        let sub = Build { parts, nm: build.nm };
        let frames: Vec<Transform> = self.frames().collect();
        let mut doll = Ragdoll::new(&sub, &below.iter().map(|&c| frames[c]).collect::<Vec<_>>(), Vec3::ZERO);
        for (i, &c) in below.iter().enumerate() {
            let b = &self.bodies[c];
            doll.bodies[i] = Body { x: b.x + self.o - doll.o, ..b.clone() };
        }
        self.ghost(&below);
        doll.wake();
        Some((sub, doll, below))
    }

    pub fn ghost(&mut self, parts: &[usize]) {
        let was = self.gone;
        for &c in parts {
            if self.gone >> c & 1 != 0 {
                continue;
            }
            let b = &mut self.bodies[c];
            (b.m, b.i) = (b.m * GHOST(), b.i * GHOST());
            self.held[c].clear();
            self.gone |= 1 << c;
        }
        if self.gone != was {
            let gone = self.gone;
            self.grips.retain(|g| gone >> g.0 & 1 == 0);
            self.wake();
        }
    }

    pub fn nearest(&self, build: &Build, at: Vec3) -> usize {
        let at = at - self.o;
        self.bodies.iter().zip(&build.parts).enumerate().map(|(k, (b, p))| {
            let axis = b.cap() * Vec3::Y;
            let s = (at - b.x).dot(axis).clamp(-p.half, p.half);
            (k, (b.x + axis * s).distance(at) - p.radius)
        }).min_by(|a, b| a.1.total_cmp(&b.1)).map_or(0, |(k, _)| k)
    }

    pub fn probe(&self, build: &Build, a: Vec3, b: Vec3) -> Option<(usize, Vec3, Vec3)> {
        use crate::ragstart::{ProbePart, ProbeShape};
        let r = self.inst();
        let inst = [r[0], r[1], r[2], self.bodies[self.order[0]].x - self.bound_off + self.o];
        let parts: Vec<ProbePart> = self.comp(&r).into_iter().zip(&build.parts).map(|((d, m), p)| ProbePart {
            m: [m[0], m[1], m[2], d],
            cen: Vec3::ZERO,
            shape: p.boxed.map_or(ProbeShape::Capsule { len: 2.0 * p.half, radius: p.radius }, |e| ProbeShape::Box(2.0 * e)),
        }).collect();
        crate::ragstart::probe_parts(&inst, &parts, a, b).map(|h| (h.k, h.at, h.n))
    }

    pub fn push(&mut self, build: &Build, at: Vec3, impulse: Vec3) {
        let k = self.nearest(build, at);
        self.impulse(k, at, impulse);
    }

    pub fn impulse(&mut self, k: usize, at: Vec3, impulse: Vec3) {
        self.pending.push((k, at - self.o, impulse, Vec3::ZERO));
        self.wake();
    }

    pub fn equalized_impulse(&mut self, k: usize, impulse: Vec3, at: Option<Vec3>, equalize: f32) {
        let Some(b) = self.bodies.get(k) else { return };
        let mean = self.mass() / self.bodies.len() as f32;
        let at = at.unwrap_or(b.x + self.o);
        let s = 1.0 - equalize + equalize * b.m / mean;
        self.impulse(k, at, impulse * s);
    }

    pub fn root(&self) -> Vec3 {
        self.bodies[0].x + self.o
    }

    pub fn share(&self, k: usize) -> f32 {
        let mean = self.mass() / self.bodies.len() as f32;
        delirium_data::kv!(106) + delirium_data::kv!(110) * (self.bodies[k].m / mean).clamp(delirium_data::kv!(57), delirium_data::kv!(12))
    }

    pub fn kick(&mut self, v: Vec3) {
        for b in &mut self.bodies {
            b.v += v;
        }
        self.wake();
    }

    fn wake(&mut self) {
        (self.mode, self.asleep) = (1, false);
    }

    /// A corpse off its simulation where it lies (CTaskSimpleDead's freeze, 0x824C8EF8): asleep until a moving
    /// obstacle or an awake neighbour wakes it.
    /// It keeps rage's sleep tolerances from then on (an NM agent has none): a bookkeeping wake returns to sleep on its
    /// first quiet step, and only real motion keeps it simulated.
    pub fn freeze(&mut self) {
        (self.mode, self.asleep) = (2, true);
        self.sleep_tol = [SLEEP_V2(), SLEEP_W2(), SLEEP_MEAN2()];
        self.still = [SLEEP_STOP(), SLEEP_SLEEP()];
        self.bodies.iter_mut().for_each(|b| (b.v, b.w) = (Vec3::ZERO, Vec3::ZERO));
    }

    pub fn awake(&self) -> bool {
        self.mode == 1
    }

    fn sleep(&mut self) {
        let root = &self.bodies[0];
        let mean = self.bodies.iter().map(|b| b.v.length_squared() + b.w.length_squared()).sum::<f32>() / self.bodies.len() as f32;
        let [v2, w2, mean2] = self.sleep_tol;
        if root.v.length_squared() <= v2 && root.w.length_squared() <= w2 && mean <= mean2 {
            if self.mode == 1 {
                self.still[0] += 1;
                if self.still[0] >= SLEEP_STOP() {
                    self.mode = 0;
                }
            }
            if self.mode != 1 {
                self.still[1] += 1;
                if self.still[1] >= SLEEP_SLEEP() {
                    self.mode = 2;
                }
            }
        } else if self.mode == 1 {
            self.still = [0, 0];
        } else {
            self.mode = 1;
        }
    }

    fn artic(&self, joints: &[Option<Joint>]) -> Artic {
        let xr = self.bodies[self.order[0]].x;
        let links: Vec<artic::Link> = self.bodies.iter().map(|b| artic::Link { r: b.r, x: b.x - xr, m: b.m, i: b.i }).collect();
        let js: Vec<Option<artic::Joint>> = joints.iter().enumerate().map(|(k, j)| j.map(|j| {
            let axis = match j.kind {
                Kind::Ball { .. } => None,
                Kind::Hinge { .. } => Some(crate::rigid::hinge_axis(&j.jc, &self.bodies[k].link())),
            };
            artic::Joint { parent: j.parent, ca: j.jc.ca, axis, stiff: j.stiff, mode: j.mode }
        })).collect();
        Artic::new(&links, &js, &self.order)
    }

    fn muscles(&self, artic: &Artic, joints: &[Option<Joint>], drive: &Drive, h: f32, lt: &mut [V6], out: &mut [Vec3]) {
        let b = &self.bodies;
        for (i, m) in drive.muscles.iter().enumerate().filter(|&(i, m)| m.stiffness > 0.0 && self.gone >> i & 1 == 0) {
            let parent = joints[i].map(|j| j.parent);
            let (pq, pw) = parent.map_or((Quat::IDENTITY, Vec3::ZERO), |j| (b[j].q, b[j].w));
            let err = short((pq * m.target * b[i].q.inverse()).normalize()).to_scaled_axis();
            let rel = b[i].w - pw;
            let kn = |t: Vec3| { let u = t.normalize_or_zero().as_dvec3(); u.dot(answer(artic, i, u)) as f32 };
            if let Some(r) = drive.rigid.get(i) {
                let Some(j) = joints[i] else { continue };
                let (pl, cl) = (b[j.parent].link(), b[i].link());
                let [s, d] = r.gain;
                let most = |m: f32| m / (1.0 - r.blend).max(delirium_data::kv!(125));
                let speed = |a: Vec3, o: &[u8; 3]| sdot(cl.w, a, o) - sdot(pl.w, a, o);
                let held = |acc: f32, v: f32, at: f32, to: f32| if acc.abs() > delirium_data::kv!(47) && !((acc.fma(h, v).fma(h, at) - to) * (at - to) >= 0.0) { 0.0 } else { acc };
                if !r.hinge {
                    let st = crate::rigid::swing3(&j.jc, &pl, &cl);
                    let (cs, sn) = (sdot(st.s, crate::rigid::col(&st.pr, 1), b"zyx"), -sdot(st.s, crate::rigid::col(&st.pr, 0), b"yzx"));
                    let (q, ls, ts) = (speed(st.across, b"zyx") / st.k, speed(st.s, b"zyx"), speed(st.bis, b"zyx") * st.k);
                    let at = [st.theta * cs, st.theta * sn, st.twist];
                    let v = [ls.fma(cs, q * sn), ls.fma(sn, -(q * cs)), ts];
                    let mut e = std::array::from_fn::<f32, 3, _>(|k| r.target[k] - at[k]);
                    if matches!(j.kind, Kind::Ball { twist, .. } if twist - LIMIT_MARGIN() >= f32::MAX) {
                        use std::f32::consts::{PI, TAU};
                        e[2] = if e[2] > PI { e[2] - TAU } else if e[2] < -PI { e[2] + TAU } else { e[2] };
                    }
                    let acc: [f32; 3] = std::array::from_fn(|k| held(e[k] * s + (0.0 - v[k]) * d, v[k], at[k], r.target[k]));
                    let (sw, qw, tw) = (acc[0].fma(cs, acc[1] * sn), acc[0].fma(sn, -(acc[1] * cs)) * st.k, acc[2] / st.k);
                    let world = st.bis.mul_add(Vec3::splat(tw), st.across.mul_add(Vec3::splat(qw), st.s * sw));
                    let cols = [DVec3::X, DVec3::Y, DVec3::Z].map(|e| answer(artic, i, e));
                    let t = (DMat3::from_cols(cols[0], cols[1], cols[2]).inverse() * world.as_dvec3()).as_vec3();
                    let (ml, mt) = (most(r.max[0]), most(r.max[1]));
                    let (ts, ta) = (sdot(t, st.s, b"yzx"), sdot(t, st.across, b"yzx"));
                    let (t0, t1, t2) = (ts.fma(cs, ta * sn).clamp(-ml, ml), ts.fma(sn, -(ta * cs)).clamp(-ml, ml), sdot(t, st.bis, b"yzx").clamp(-mt, mt));
                    let t = st.bis.mul_add(Vec3::splat(t2), st.across.mul_add(Vec3::splat(t0.fma(sn, -(t1 * cs))), st.s * t0.fma(cs, t1 * sn)));
                    pair(joints, i, t.as_dvec3(), lt);
                    out[i] += t;
                    continue;
                }
                let n = crate::rigid::hinge_axis(&j.jc, &cl);
                let (at, w) = (crate::rigid::hinge(&j.jc, &pl, &cl), speed(n, b"yzx"));
                let acc = held((0.0 - w) * d + (r.target[0] - at) * s, w, at, r.target[0]);
                let k = n.as_dvec3().dot(answer(artic, i, n.as_dvec3())) as f32;
                let mt = most(r.max[0]);
                let t = if k > delirium_data::kv!(66) { n * (acc / k).clamp(-mt, mt) } else { Vec3::ZERO };
                pair(joints, i, t.as_dvec3(), lt);
                out[i] += t;
            } else if drive.rigid.is_empty() {
                let (s, d) = (m.stiffness * m.stiffness, 2.0 * m.damping * m.stiffness);
                let a = (err * s - rel * (d + h * s)) / (1.0 + h * d + h * h * s);
                let k = kn(a);
                if k > delirium_data::kv!(72) {
                    pair(joints, i, (a / k).as_dvec3(), lt);
                    out[i] += a / k;
                }
            }
        }
    }

    fn soft_limits(&self, artic: &Artic, joints: &[Option<Joint>], drive: &Drive, lt: &mut [V6], out: &mut [Vec3]) {
        let b = &self.bodies;
        for (i, r) in drive.rigid.iter().enumerate().filter(|(_, r)| r.soft.iter().any(|l| l.enabled)) {
            let Some(j) = joints.get(i).copied().flatten() else { continue };
            let (pl, cl) = (b[j.parent].link(), b[i].link());
            let speed = |a: Vec3, o: &[u8; 3]| sdot(cl.w, a, o) - sdot(pl.w, a, o);
            let t = if r.hinge {
                let n = crate::rigid::hinge_axis(&j.jc, &cl);
                let acc = r.soft[0].accel(crate::rigid::hinge(&j.jc, &pl, &cl), speed(n, b"yzx"));
                let k = n.as_dvec3().dot(answer(artic, i, n.as_dvec3())) as f32;
                if k > delirium_data::kv!(66) { n * (acc / k) } else { Vec3::ZERO }
            } else {
                let st = crate::rigid::swing3(&j.jc, &pl, &cl);
                let (cs, sn) = (sdot(st.s, crate::rigid::col(&st.pr, 1), b"zyx"), -sdot(st.s, crate::rigid::col(&st.pr, 0), b"yzx"));
                let (q, ls, ts) = (speed(st.across, b"zyx") / st.k, speed(st.s, b"zyx"), speed(st.bis, b"zyx") * st.k);
                let at = [st.theta * cs, st.theta * sn, st.twist];
                let v = [ls.fma(cs, q * sn), ls.fma(sn, -(q * cs)), ts];
                let acc: [f32; 3] = std::array::from_fn(|k| r.soft[k].accel(at[k], v[k]));
                let (sw, qw, tw) = (acc[0].fma(cs, acc[1] * sn), acc[0].fma(sn, -(acc[1] * cs)) * st.k, acc[2] / st.k);
                let world = st.bis.mul_add(Vec3::splat(tw), st.across.mul_add(Vec3::splat(qw), st.s * sw));
                let cols = [DVec3::X, DVec3::Y, DVec3::Z].map(|e| answer(artic, i, e));
                (DMat3::from_cols(cols[0], cols[1], cols[2]).inverse() * world.as_dvec3()).as_vec3()
            };
            if t != Vec3::ZERO {
                pair(joints, i, t.as_dvec3(), lt);
                out[i] += t;
            }
        }
    }

    fn limits(&self, artic: &Artic, joints: &[Option<Joint>], h: f32, lt: &mut [V6]) {
        let b = &self.bodies;
        for (k, j) in joints.iter().enumerate() {
            let Some(j) = *j else { continue };
            let (pl, cl) = (b[j.parent].link(), b[k].link());
            let speed = |a: Vec3| sdot(cl.w, a, b"yzx") - sdot(pl.w, a, b"yzx");
            let mut past: Vec<(Vec3, f32)> = vec![];
            match j.kind {
                Kind::Hinge { lo, hi } => {
                    let (lo, hi) = hinge_limits(lo, hi);
                    let (a, n) = (crate::rigid::hinge(&j.jc, &pl, &cl), crate::rigid::hinge_axis(&j.jc, &cl));
                    let (oh, ol) = (a - hi, a - lo);
                    if !(oh > 0.0) && ol >= 0.0 {
                        continue;
                    }
                    let over = if oh >= 0.0 { oh } else { ol };
                    let w = speed(n);
                    let f = if w * over >= 0.0 {
                        over
                    } else {
                        let sd = w * h;
                        let f = sd + over;
                        if sd * f >= 0.0 {
                            continue;
                        }
                        f
                    };
                    let r = n.as_dvec3().dot(answer(artic, k, n.as_dvec3()));
                    let t = -((if r != 0.0 { 1.0 / r } else { delirium_data::kv!(3) }) as f32 * LIMIT_GAIN() * f);
                    pair(joints, k, (n * t).as_dvec3(), lt);
                    continue;
                }
                Kind::Ball { lean, twist } => {
                    let st = crate::rigid::swing3(&j.jc, &pl, &cl);
                    if let Some(lim) = lean_limit(&st, lean) {
                        let (over, ls) = (st.theta - lim, speed(st.s));
                        let f = (if ls >= 0.0 { 0.0 } else { ls }).fma(h, over);
                        if f > 0.0 || ls > 0.0 {
                            past.extend([(st.s, -(LIMIT_GAIN() * f)), (st.across, -speed(st.across))]);
                        }
                    }
                    if let Some(lim) = twist_limit(&st, twist) {
                        let (mut over, mut ts, mut axis) = (st.twist - lim, speed(st.bis) * st.k, st.bis);
                        if lim < 0.0 {
                            (over, ts, axis) = (-over, -ts, -axis);
                        }
                        let f = (if ts >= 0.0 { 0.0 } else { ts }).fma(h, if over >= 0.0 { over } else { 0.0 });
                        if f > 0.0 || ts >= 0.0 {
                            past.push((axis, (if st.k != 0.0 { 1.0 / st.k } else { delirium_data::kv!(3) }) * -(LIMIT_GAIN() * f)));
                        }
                    }
                }
            }
            let resp: Vec<DVec3> = past.iter().map(|&(u, _)| answer(artic, k, u.as_dvec3())).collect();
            let m = |i: usize, c: usize| past[i].0.as_dvec3().dot(resp[c]);
            let w: Vec<f64> = past.iter().map(|&(_, w)| w as f64).collect();
            let rec = |d: f64| if d != 0.0 { 1.0 / d } else { delirium_data::kv!(3) };
            let x: Vec<f64> = match past.len() {
                1 => vec![w[0] * rec(m(0, 0))],
                2 => {
                    let r = rec(m(1, 1) * m(0, 0) - m(0, 1) * m(1, 0));
                    vec![(m(1, 1) * w[0] - m(0, 1) * w[1]) * r, (w[1] * m(0, 0) - m(1, 0) * w[0]) * r]
                }
                3 => {
                    let mm = DMat3::from_cols(DVec3::new(m(0, 0), m(1, 0), m(2, 0)), DVec3::new(m(0, 1), m(1, 1), m(2, 1)), DVec3::new(m(0, 2), m(1, 2), m(2, 2)));
                    (mm.inverse() * DVec3::new(w[0], w[1], w[2])).to_array().to_vec()
                }
                _ => continue,
            };
            let t: DVec3 = past.iter().zip(&x).map(|(&(u, _), &x)| u.as_dvec3() * x).sum();
            if t != DVec3::ZERO {
                pair(joints, k, t, lt);
            }
        }
    }

    fn limit_rows(&mut self, artic: &Artic, joints: &[Option<Joint>], h: f32, qd0: &[f64]) -> (Vec<Row>, Vec<Row>) {
        self.prep_twist = vec![None; joints.len()];
        let mut touched: Vec<bool> = self.held.iter().map(|h| !h.is_empty()).collect();
        touched.resize(joints.len(), false);
        for (&(a, c), _) in self.pairs.iter().zip(&self.held_pairs).filter(|(_, h)| !h.is_empty()) {
            (touched[a], touched[c]) = (true, true);
        }
        let b = &self.bodies;
        let (mut rows, mut push) = (vec![], vec![]);
        let mut found = vec![];
        let n = delirium_data::kv!(6 int) * b.len();
        for (k, j) in joints.iter().enumerate() {
            let Some(j) = *j else { continue };
            let (pl, cl) = (b[j.parent].link(), b[k].link());
            let mut dofs: Vec<(Vec3, f32, f32)> = vec![];
            match j.kind {
                Kind::Hinge { lo, hi } => {
                    let (lo, hi) = hinge_limits(lo, hi);
                    let (a, n) = (crate::rigid::hinge(&j.jc, &pl, &cl), crate::rigid::hinge_axis(&j.jc, &cl));
                    let side = if !(a - hi < 0.0) { 1 } else if a - lo <= 0.0 { -1 } else if touched[k] { if hi - a < a - lo { 1 } else { -1 } } else { 0 };
                    match side {
                        1 => dofs.push((n, a - hi, 1.0)),
                        -1 => dofs.push((-n, lo - a, 1.0)),
                        _ => {}
                    }
                }
                Kind::Ball { lean, twist } => {
                    let st = crate::rigid::swing3(&j.jc, &pl, &cl);
                    self.prep_twist[k] = Some(st.twist);
                    if let Some(lim) = lean_limit(&st, lean) {
                        dofs.push((st.s, st.theta - lim, 1.0));
                        let w = sdot(cl.w, st.across, b"zyx") - sdot(pl.w, st.across, b"zyx");
                        dofs.push((if w < 0.0 { -st.across } else { st.across }, 0.0, 1.0));
                    }
                    if let Some(lim) = twist_limit(&st, twist) {
                        dofs.push(if lim > 0.0 { (st.bis, st.twist - lim, st.k) } else { (-st.bis, -st.twist + lim, st.k) });
                    }
                }
            }
            for (u, o, c) in dofs {
                let mut jr = vec![0.0; n];
                for i in 0..delirium_data::kv!(0 int) {
                    jr[delirium_data::kv!(6 int) * k + delirium_data::kv!(0 int) + i] = -u[i] as f64;
                    jr[delirium_data::kv!(6 int) * j.parent + delirium_data::kv!(0 int) + i] += u[i] as f64;
                }
                let b0 = -jr.iter().zip(qd0).map(|(x, y)| x * y).sum::<f64>();
                let to = (c as f64 - 1.0) * b0 - ((LIMIT_MARGIN() - o).max(0.0) / h) as f64;
                found.push((jr, to, o, c));
            }
        }
        for g in found.chunks(delirium_data::kv!(1 int)) {
            let mj = artic.solve_rows(std::array::from_fn::<_, 4, _>(|i| &g[i.min(g.len() - 1)].0[..]));
            for ((jr, to, o, c), mj) in g.iter().cloned().zip(mj) {
                let mut r = Row::with(jr, mj, to, RowKind::Push);
                r.eff *= c as f64;
                push.push(Row { j: r.j.clone(), mj: r.mj.clone(), target: ((o - LIMIT_MARGIN()) / h) as f64, ..r });
                rows.push(r);
            }
        }
        (rows, push)
    }

    #[allow(clippy::too_many_arguments)]
    fn contacts(&mut self, build: &Build, world: &Collision, artic: &Artic, h: f32, pose: &[Option<(Vec3, Quat)>], obstacles: &[Obstacle],
                others: &[Capsule], free: &[f64]) -> (Vec<Row>, Vec<Block>, Vec<Block>) {
        let mut audit=contact_cost::Cost::new(obstacles.iter().map(|o|o.parts.len()).sum());
        let (mut rows, mut blocks, mut push_blocks) = (vec![], vec![], vec![]);
        let fv = |j: &[f64]| j.iter().zip(free).map(|(a, b)| a * b).sum::<f64>();
        let (o, turns) = (self.o, self.turns());
        let d = |v: Vec3| v.as_dvec3();
        let (n_links, xr) = (self.bodies.len(), self.bodies[self.order[0]].x.as_dvec3());
        let lrow = |k: usize, at: DVec3, n: DVec3, t: DVec3| -> Vec<f64> {
            let mut r = vec![0.0; delirium_data::kv!(6 int) * n_links];
            r[delirium_data::kv!(6 int) * k..delirium_data::kv!(6 int) * k + delirium_data::kv!(6 int)].copy_from_slice(&v6(n, (at - xr).cross(n) + t));
            r
        };
        for (k, p) in pose.iter().enumerate() {
            let Some((x, q)) = *p else { continue };
            let b = &self.bodies[k];
            let (v, w) = ((x - b.x) / h, short(q * b.q.inverse()).to_scaled_axis() / h);
            for e in [Vec3::X, Vec3::Y, Vec3::Z] {
                for (j, to) in [(lrow(k, d(b.x), d(e), DVec3::ZERO), v.dot(e)), (lrow(k, d(b.x), DVec3::ZERO, d(e)), w.dot(e))] {
                    let to = to as f64 - fv(&j);
                    rows.push(Row::new(artic, j, to, RowKind::Equal));
                }
            }
        }
        for (k, &(at, turn)) in self.cons.iter().enumerate().filter(|&(k, _)| pose[k].is_none()) {
            let b = &self.bodies[k];
            for e in [Vec3::X, Vec3::Y, Vec3::Z] {
                if let Some(at) = at {
                    let j = lrow(k, d(b.x), d(e), DVec3::ZERO);
                    let to = ((at - o - b.x) / h).dot(e) as f64 - fv(&j);
                    rows.push(Row::new(artic, j, to, RowKind::Equal));
                }
                if let Some(q) = turn {
                    let j = lrow(k, d(b.x), DVec3::ZERO, d(e));
                    let to = (short(q * b.q.inverse()).to_scaled_axis() / h).dot(e) as f64 - fv(&j);
                    rows.push(Row::new(artic, j, to, RowKind::Equal));
                }
            }
        }
        self.grip_rows.clear();
        self.gap_audit = Default::default();
        let mut hits: Vec<(usize, Vec3, Vec3, f32, f32, Vec3, f32, Option<(usize, usize)>)> = vec![];
        let fresh = self.fresh;
        let mesh_components:Vec<_>=obstacles.iter().map(|ob|mesh_manifold::component(ob)&&
            !(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_TRIANGLE_MANIFOLD_BASELINE"))).collect();
        self.held_car.retain(|h| !h.3.is_empty() && !fresh && pose[h.0].is_none() && obstacles.iter().any(|o| o.id == h.1 && !o.parts.is_empty()));
        // Rebuild AFTER retain compacts the vector. Only lookup is indexed:
        // the original vector still owns every manifold and its solver order.
        // Preserve first-match semantics even if an imported list has duplicates.
        let mut car_slots=car_slot_index_enabled().then(||car_slots::Index::new(self.bodies.len(),obstacles,&self.held_car));
        // The box round each obstacle's children, in its own frame, to skip whole vehicles a part cannot touch.
        // A sphere from its origin took in every car within some 5 m, and preparing those was the contact stage's cost (07/11).
        let ob_box:Vec<(Vec3,Vec3)>=obstacles.iter().map(|ob|ob.parts.iter().fold((Vec3::INFINITY,Vec3::NEG_INFINITY),|(lo,hi),cp|{
            let(centre,half)=(cp.at+cp.m*((cp.lo+cp.hi)*0.5),Mat3::from_cols(cp.m.x_axis.abs(),cp.m.y_axis.abs(),cp.m.z_axis.abs())*((cp.hi-cp.lo)*0.5));
            (lo.min(centre-half),hi.max(centre+half))
        })).collect();
        let child_prepare=audit.timer();
        let child_rejection=(!fresh&&pose.iter().any(Option::is_none)&&car_slot_index_enabled()&&
            !(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_CHILD_REJECTION_BASELINE"))).then(||{
            obstacles.iter().map(|_|std::cell::OnceCell::new()).collect::<Vec<_>>()
        });
        // Prepared on first use: every child of every vehicle in the scene, per body and substep, was the
        // largest part of the contact stage, and bodies touch one or two of them (07/11).
        let prepare_children=|ob:&Obstacle|{
            let(rot,lrot)=(Mat3::from_quat(ob.rot),Mat3::from_quat(ob.last.map_or(ob.rot,|l|l.1)));
            let lat=ob.last.map_or(ob.at,|l|l.0);
            let mut shared:Option<(Vec3,Mat3,child_bounds::Frames)>=None;
            child_bounds::Prepared::new(ob.parts.iter().map(|cp|{
                if !shared.as_ref().is_some_and(|s|s.0==cp.at&&s.1==cp.m){
                    shared=Some((cp.at,cp.m,child_bounds::Frames::new((ob.at-o+rot*cp.at,rot*cp.m),(lat-o+lrot*cp.at,lrot*cp.m))));
                }
                shared.as_ref().unwrap().2.child((cp.lo+cp.hi)*0.5,(cp.hi-cp.lo)*0.5)
            }).collect::<Vec<_>>())
        };
        audit.add(1,child_prepare);
        self.held_pairs.resize(self.pairs.len(), vec![]);
        let car_base = self.held.len() + self.held_pairs.len();
        let mut hit_ob: Vec<(usize, i32, Vec3, Vec3,Option<ExternalContact>)> = vec![];
        for (_, held) in self.held.iter_mut().enumerate().filter(|&(i, _)| pose[i].is_some() || self.fresh) {
            held.clear();
        }
        // Parts of the ignored vehicle's riders still inside it this pass, by their own shape (see below).
        let (mut nc_inside, mut nc_evaluated) = (0u32, 0u32);
        for (i, p) in build.parts.iter().enumerate().filter(|&(i, _)| pose[i].is_none() && !self.fresh) {
            if i < 32 { nc_evaluated |= 1 << i; }
            let world_tick=audit.timer();
            let (b, tm) = (&self.bodies[i], turns[i]);
            let x = b.x - self.bound_off;
            let mut acc = (Vec3::ZERO, Vec3::ZERO, 0.0, -1);
            let mut add = |at: Vec3, n: Vec3, depth: f32, inst: i32| {
                let w = depth.max(delirium_data::kv!(48));
                acc = (acc.0 + at * w, acc.1 + n * w, acc.2 + w, inst);
            };
            let held = &mut self.held[i];
            let shape = match p.boxed {
                Some(e) => gjk::Shape::Box(e - Vec3::splat(BOX_MARGIN()), BOX_MARGIN()),
                None => gjk::Shape::Capsule(p.half, p.radius),
            };
            let last = self.seen.get(i).map_or(x, |s| s.0 - self.bound_off);
            let fast = (tm.transpose() * (x - last)).abs().cmpgt(bound_half(p)).any();
            let leaving = |local: Vec3, face: Vec3| self.seen.get(i).is_some_and(|&(lx, lm)| ((b.x + tm * local) - (lx + lm * local)).dot(face) > delirium_data::kv!(125));
            let last_m = self.seen.get(i).map_or(tm, |s| s.1);
            let (sc, sh) = swept((x, tm), (last, last_m), bound_half(p));
            let (sm, sh) = (tm, sh + Vec3::splat(WORLD_MARGIN()));
            let sc = x + o + sm * sc;
            let found = {
                let reach = Vec3::splat(bound_half(p).length() + delirium_data::kv!(73));
                let new = |q: gjk::Closest, at: Vec3| {
                    let local = tm.transpose() * (q.a - at);
                    let ground = q.b - q.n * WORLD_MARGIN() + o;
                    Held { local, ground, gap: (x + o + tm * local - ground).dot(q.n) - WORLD_MARGIN(), age: 0, m: 0, fl:0, imp: Vec3::ZERO, pimp: Vec3::ZERO, n: q.n, inst: 0, off: false }
                };
                let frame = |m: Mat3, at: Vec3| crate::bvh::Frame { rows: [m.x_axis, m.y_axis, m.z_axis], t: at + o };
                // Preserve the original SAT predicate exactly, but do not build
                // adjacency/cone metadata for triangles it immediately rejects.
                let early_sat=animated_queries::early_bounds_enabled();
                let triangles=if early_sat{
                    world.tris_in_part_filtered(x.min(last)+o-reach,x.max(last)+o+reach,
                        &frame(tm,x),&frame(last_m,last),-bound_half(p),bound_half(p),|t|box_meets_tri(sc,sm,sh,t))
                }else{world.tris_in_part(x.min(last)+o-reach,x.max(last)+o+reach,
                    &frame(tm,x),&frame(last_m,last),-bound_half(p),bound_half(p))};
                let mut found: Vec<(Held, crate::collision::Poly)> = triangles.into_iter().filter_map(|(t, m, dirs, mask, inst, poly)| {
                    if !early_sat && !box_meets_tri(sc, sm, sh, t) {
                        return None;
                    }
                    let (tri, back) = (gjk::Shape::Tri(t.map(|v| v - t[0]), WORLD_MARGIN()), t[0] - o);
                    let query = |at: Vec3| gjk::closest(&shape, tm, at + o - t[0], &tri, Mat3::IDENTITY, Vec3::ZERO, Vec3::X, f32::INFINITY, Some(gjk::Pen::Tri(dirs)))
                        .map(|q| gjk::Closest { a: q.a + back, b: q.b + back, ..q });
                    let h = if fast {
                        let (t, q) = Self::advance(|t| query(last + (x - last) * t), last - x)?;
                        new(q, last + (x - last) * t)
                    } else {
                        let q = query(x).filter(|q| q.dist < delirium_data::kv!(109))?;
                        new(q, x)
                    };
                    let across_ok = |at: Vec3, n: Vec3| (0..delirium_data::kv!(0 int)).filter(|k| mask & 1 << k != 0).all(|k| {
                        let (p, e) = (t[k], t[(k + 1) % delirium_data::kv!(0 int)] - t[k]);
                        let (s, ee) = ((at - p).dot(e), e.dot(e));
                        let c = if !(s > delirium_data::kv!(66)) { p } else if ee - s > delirium_data::kv!(66) { p + e * (s / ee) } else { p + e };
                        if (c - at).length_squared() > delirium_data::kv!(65) * WORLD_MARGIN() * WORLD_MARGIN() {
                            return true;
                        }
                        let u = e.normalize();
                        let across = if u.dot(n) == 0.0 { n } else { (n - u * u.dot(n)).normalize() };
                        dirs[k + 1].dot(across) >= dirs[0].dot(dirs[k + 1]).min(1.0 - delirium_data::kv!(47)) - delirium_data::kv!(125)
                    });
                    if leaving(h.local, dirs[0]) || !across_ok(h.ground + h.n * WORLD_MARGIN(), h.n) {
                        return None;
                    }
                    Some((Held { m, inst, ..h }, poly))
                }).collect();
                for k in 0..found.len() {
                    let poly = found[k].1;
                    if found[..k].iter().any(|f| f.1.first == poly.first) {
                        continue;
                    }
                    let mut hits: Vec<(Vec3, Vec3)> = found[k..].iter().filter(|f| f.1.first == poly.first).map(|f| (f.0.ground + f.0.n * WORLD_MARGIN(), f.0.n)).collect();
                    poly.clamp(&mut hits, WORLD_MARGIN());
                    for (f, (at, n)) in found[k..].iter_mut().filter(|f| f.1.first == poly.first).zip(hits) {
                        (f.0.ground, f.0.n) = (at - n * WORLD_MARGIN(), n);
                    }
                }
                found.into_iter().map(|f| f.0).collect::<Vec<Held>>()
            };
            #[cfg(feature = "portal")]
            let found: Vec<Held> = found.into_iter().filter(|h| !world.in_hole(h.ground)).collect();
            #[cfg(feature = "portal")]
            held.retain(|h| !world.in_hole(h.ground));
            let mut groups: Vec<(u32, Vec<Held>)> = vec![];
            for r in held.drain(..) {
                match groups.iter_mut().find(|g| g.0 == r.inst) {
                    Some(g) => g.1.push(r),
                    None => groups.push((r.inst, vec![r])),
                }
            }
            for new in found {
                let g = match groups.iter().position(|g| g.0 == new.inst) {
                    Some(g) => g,
                    None => { groups.push((new.inst, vec![])); groups.len() - 1 }
                };
                hold(&mut groups[g].1, new);
            }
            for (_, held) in groups.iter_mut() {
                for r in held.iter_mut() {
                    if r.age != 0 {
                        r.gap = (x + o + tm * r.local - r.ground).dot(r.n) - WORLD_MARGIN();
                    }
                    r.age += if r.age == 1 { 2 } else { 1 };
                }
                let slid = |r: &Held| { let d = x + o + tm * r.local - r.ground; (d - r.n * d.dot(r.n)).length_squared() };
                let (mut k, mut worst) = (0, (None, 0.0));
                while k < held.len() {
                    if held[k].gap >= MARGIN() {
                        held.swap_remove(k);
                        continue;
                    }
                    let d = slid(&held[k]);
                    if held[k].age > delirium_data::kv!(1 int) && d > worst.1 && d > delirium_data::kv!(568) {
                        worst = (Some(k), d);
                    }
                    k += 1;
                }
                if let Some(k) = worst.0 {
                    held.swap_remove(k);
                }
            }
            *held = groups.into_iter().flat_map(|g| g.1).collect();
            for (k, r) in held.iter().enumerate() {
                let at = x + tm * r.local;
                let row = SURFACES.read().ok().and_then(|s| s.get(r.m as usize).copied()).unwrap_or([1.0, ELASTICITY()]);
                self.touch[i] |= delirium_data::kv!(7 int);
                add(at + o, r.n, -r.gap, -1);
                hits.push((i, at, r.n, r.gap, (part_friction(p) * row[0]).max(0.0), Vec3::ZERO, (PART_ELASTICITY() * row[1]).min(1.0), Some((i, k))));
            }
            audit.add(0,world_tick);let vehicle_tick=audit.timer();
            let a_last = (last, self.seen.get(i).map_or(tm, |s| s.1));
            for (ob_index,ob) in obstacles.iter().enumerate().filter(|(_,ob)| !ob.parts.is_empty()) {
                if !ob.filter.meets(CollisionFilter::NM_FRAGMENT){continue;}
                let (rot, lrot) = (Mat3::from_quat(ob.rot), Mat3::from_quat(ob.last.map_or(ob.rot, |l| l.1)));
                let lat = ob.last.map_or(ob.at, |l| l.0);
                let (sc, sh) = swept((x, tm), a_last, bound_half(p));
                // A body at rest paid about 5 ms a step testing every vehicle in the scene (07/11). Only an empty
                // manifold may be skipped: one with points must still be updated to let them go.
                // sc is in the part's frame: the distance is from the part, not from the body's origin.
                let clear=sh.length()+0.5+(ob.at-lat).length();let swept_at=x+tm*sc;
                let outside=|local:Vec3|{let(lo,hi)=ob_box[ob_index];(local-local.clamp(lo,hi)).length()>clear};
                let far=outside(rot.transpose()*(swept_at-(ob.at-o)))&&outside(lrot.transpose()*(swept_at-(lat-o)));
                if far&&mesh_components[ob_index]{
                    let key=(i,ob.id,0);
                    let slot=match &car_slots{Some(index)=>index.get(i,ob_index,0),None=>self.held_car.iter().position(|h|(h.0,h.1,h.2)==key)};
                    if slot.is_none_or(|s|self.held_car[s].3.is_empty()){continue;}
                }
                // Out of reach no child meets, which empties each child's manifold.
                if far&&!mesh_components[ob_index]{if let Some(index)=&car_slots{
                    for c in 0..ob.parts.len(){if let Some(s)=index.get(i,ob_index,c){self.held_car[s].3.clear();}}
                    continue;
                }}
                let body_rejection=child_bounds::Rejection::body((x,tm),(sc,sh));
                let prepared=child_rejection.as_ref().map(|v|v[ob_index].get_or_init(||prepare_children(ob)));
                let candidate_batches=prepared.map(|p|p.candidates(body_rejection,tm,sh));
                if mesh_components[ob_index] {
                    // A registered moving component is ONE mesh shape. Its
                    // polygons feed one persistent manifold, as TU8 829C3A40 /
                    // 829C2CF8 do; polygon IDs are retained by Held::inst.
                    // 8296E288 limits the SHAPE PAIR to six points, not each
                    // triangle. Every source polygon remains in narrowphase.
                    let key=(i,ob.id,0);
                    let s=match &car_slots{Some(index)=>index.get(i,ob_index,0),None=>self.held_car.iter().position(|h|(h.0,h.1,h.2)==key)}.unwrap_or_else(||{
                        let s=self.held_car.len();self.held_car.push((i,ob.id,0,vec![]));
                        if let Some(index)=&mut car_slots{index.insert(i,ob_index,0,s);}s
                    });
                    let candidates=ob.parts.iter().enumerate().filter_map(|(c,cp)|{
                        if !cp.filter.meets(CollisionFilter::NM_FRAGMENT){return None}
                        // Asked first: most children of a nearby vehicle fail here, and the frames below were paid for each (07/11).
                        let cached=prepared.map(|bounds|candidate_batches.as_ref().unwrap()[c/child_bounds::BATCH]&&bounds.children[c].meets(body_rejection,tm,sh));
                        if cached==Some(false){return None}
                        let(cq,cx)=(rot*cp.m,ob.at-o+rot*cp.at);
                        let(cent,half)=((cp.lo+cp.hi)*0.5,(cp.hi-cp.lo)*0.5);
                        let last_child=(lat-o+lrot*cp.at,lrot*cp.m);
                        let meet=cached.unwrap_or_else(||moving_child_meets((x,tm),(sc,sh),(cx,cq),last_child,cent,half));
                        meet.then_some((c,cp.shape,half,(cx,cq),last_child))
                    });
                    // A part inside a closed mesh touches no polygon and gives no point, yet its pair is still
                    // live (the bounds meet): no_collision must hold until the part is out, or it ends inside
                    // the seat and the first polygon met from within throws the body out.
                    let bounds_met=std::cell::Cell::new(false);
                    let candidates=candidates.inspect(|_|bounds_met.set(true));
                    let manifold_tick=audit.timer();
                    let calls=mesh_manifold::update(&mut self.held_car[s].3,(shape,bound_half(p),(x,tm),a_last),(ob.at-o,rot),candidates);
                    audit.add(2,manifold_tick);for _ in 0..calls{audit.called_manifold();}
                    if ob.owner.is_some()&&ob.owner==self.no_collision&&self.no_collision_parts>>i&1!=0{
                        // The vehicle this part collides with no longer: its points are found and dropped (no row, no touch).
                        let met=bounds_met.get()||!self.held_car[s].3.is_empty();self.no_collision_seen|=if met{4}else{0};
                        // Enclosed by the mesh (bounds meet, no polygon met) or deeper than NC_OUT: still inside.
                        if i<32&&((bounds_met.get()&&self.held_car[s].3.is_empty())||self.held_car[s].3.iter().any(|r|r.gap< -NC_OUT)){nc_inside|=1<<i;}
                        self.held_car[s].3.clear();continue;
                    }
                    for(k,r)in self.held_car[s].3.iter().enumerate(){
                        let(at,on)=(x+tm*r.local,ob.at-o+rot*r.ground);
                        self.touch[i]|=delirium_data::kv!(7 int);add(at+o,r.n,-r.gap,ob.id);
                        let external=ob.owner.map(|id|ExternalContact{id,point:on+o,basis:frame(r.n)});
                        hit_ob.push((hits.len(),ob.id,at+o,r.n,external));
                        hits.push((i,at,r.n,r.gap,part_friction(p)*CAR_FRICTION(),ob.vel_at(on+o),PART_ELASTICITY()*ELASTICITY(),Some((car_base+s,k))));
                    }
                    continue;
                }
                for (c, cp) in ob.parts.iter().enumerate() {
                    if ob.owner.is_some_and(|id|self.normal_support_owners.contains(&id))&&(c==1||(c==3&&self.mass()>200.)){continue;}
                    if !cp.filter.meets(CollisionFilter::NM_FRAGMENT){continue;}
                    if matches!(cp.shape, gjk::Shape::Hull(v, _) if v.is_empty()) {
                        continue;
                    }
                    let key=(i,ob.id,c);
                    let slot=match &car_slots{Some(index)=>index.get(i,ob_index,c),None=>self.held_car.iter().position(|h|(h.0,h.1,h.2)==key)};
                    let cached_meet=prepared.map(|bounds|candidate_batches.as_ref().unwrap()[c/child_bounds::BATCH]&&bounds.children[c].meets(body_rejection,tm,sh));
                    if cached_meet==Some(false){
                        if let Some(s)=slot{self.held_car[s].3.clear();}continue;
                    }
                    let (cq, cx) = (rot * cp.m, ob.at - o + rot * cp.at);
                    let (cent, half) = ((cp.lo + cp.hi) * 0.5, (cp.hi - cp.lo) * 0.5);
                    let last_child = (lat - o + lrot * cp.at, lrot * cp.m);
                    let meet = cached_meet.unwrap_or_else(||moving_child_meets((x, tm), (sc, sh), (cx, cq), last_child, cent, half));
                    if !meet {
                        if let Some(s) = slot { self.held_car[s].3.clear(); }
                        continue;
                    }
                    let s=slot.unwrap_or_else(||{
                        let slot=self.held_car.len();self.held_car.push((i,ob.id,c,vec![]));
                        if let Some(index)=&mut car_slots{index.insert(i,ob_index,c,slot);}slot
                    });
                    let side_c = (cp.shape, half, (cx, cq), last_child);
                    let manifold_tick=audit.timer();
                    Self::manifold(&mut self.held_car[s].3, (shape, bound_half(p), (x, tm), a_last), side_c);
                    audit.add(2,manifold_tick);audit.called_manifold();
                    if ob.owner.is_some()&&ob.owner==self.no_collision&&self.no_collision_parts>>i&1!=0{
                        let met=!self.held_car[s].3.is_empty();self.no_collision_seen|=if met{4}else{0};
                        if i<32&&self.held_car[s].3.iter().any(|r|r.gap< -NC_OUT){nc_inside|=1<<i;}
                        self.held_car[s].3.clear();continue;
                    }
                    for (k, r) in self.held_car[s].3.iter().enumerate() {
                        let (at, on) = (x + tm * r.local, cx + cq * r.ground);
                        self.touch[i] |= delirium_data::kv!(7 int);
                        add(at + o, r.n, -r.gap, ob.id);
                        let external=ob.owner.map(|id|ExternalContact{id,point:on+o,basis:frame(r.n)});
                        hit_ob.push((hits.len(), ob.id, at + o, r.n,external));
                        hits.push((i, at, r.n, r.gap, part_friction(p) * CAR_FRICTION(), ob.vel_at(on + o), PART_ELASTICITY() * ELASTICITY(), Some((car_base + s, k))));
                    }
                }
            }
            for ob in obstacles.iter().filter(|ob| ob.parts.is_empty()) {
                if !ob.filter.meets(CollisionFilter::NM_FRAGMENT){continue;}
                let car = gjk::Shape::Box(ob.half, 0.0);
                let Some(q) = gjk::closest(&shape, tm, x, &car, Mat3::from_quat(ob.rot), ob.centre - o, Vec3::Z, f32::MAX.sqrt(), Some(gjk::Pen::Sphere)) else { continue };
                if q.dist >= MARGIN() {
                    continue;
                }
                self.touch[i] |= delirium_data::kv!(7 int);
                add(q.a + o, q.n, -q.dist, ob.id);
                let external=ob.owner.map(|id|ExternalContact{id,point:q.b+o,basis:frame(q.n)});
                hit_ob.push((hits.len(), ob.id, q.a + o, q.n,external));
                hits.push((i, q.a, q.n, q.dist, part_friction(p) * CAR_FRICTION(), ob.vel_at(q.b + o), PART_ELASTICITY() * ELASTICITY(), None));
            }
            audit.add(1,vehicle_tick);
            if acc.2 > 0.0 {
                self.hit[i] = Some((acc.0 / acc.2, (acc.1 / acc.2).normalize_or_zero(), acc.3));
            }
            for &(o0, o1, or, ov, doll) in others {
                let axis = tm * Vec3::Y * p.half;
                let (here, there) = nearest(b.x - axis, b.x + axis, o0 - o, o1 - o);
                let (gap, room) = (here - there, p.radius + or);
                let dist = gap.length();
                if dist < room && dist > delirium_data::kv!(4) {
                    hits.push((i, here, gap / dist, (dist - room) * if doll { 0.5 } else { 1.0 }, part_friction(p) * delirium_data::kv!(18), ov, PART_ELASTICITY() * PART_ELASTICITY(), None));
                }
            }
        }
        // A part's exclusion ends once its own shape is out of the vehicle (resting on it counts as out), not
        // when its bound sphere leaves the boxes: a shot rider slumped onto the tank kept that sphere touching,
        // ignored the parked bike for good, sank through it and never pushed it over (06/11). Out parts collide
        // from the next step: the body lies on the bike and its weight, through the finite vehicle owner, tips it.
        if self.no_collision.is_some() && nc_evaluated != 0 {
            let before = self.no_collision_parts;
            self.no_collision_parts = self.no_collision_parts & (nc_inside | !nc_evaluated) | self.no_collision_hold;
            if self.no_collision_parts != before { self.no_collision_seen |= 8; }
        }
        let self_tick=audit.timer();
        let boxes: Vec<(Vec3, Vec3)> = (0..n_links).map(|k| { let f = (self.bodies[k].x, turns[k]); swept_box(&build.parts[k], f, self.seen.get(k).copied().unwrap_or(f)) }).collect();
        for (pi, &(a, c)) in self.pairs.clone().iter().enumerate() {
            let (fa, fc) = ((self.bodies[a].x, turns[a]), (self.bodies[c].x, turns[c]));
            let ((xa, qa), (xc, qc)) = (fa, fc);
            let seen = |k: usize, f: (Vec3, Mat3)| self.seen.get(k).copied().unwrap_or(f);
            let (sa, sc) = (seen(a, fa), seen(c, fc));
            Self::pair_manifold(&mut self.held_pairs[pi], (&build.parts[a], fa, sa), (&build.parts[c], fc, sc), [boxes[a], boxes[c]]);
            let held = &mut self.held_pairs[pi];
            let ends = |r: &Held| (xa + qa * r.local, xc + qc * r.ground);
            let off = (self.contact_off >> a | self.contact_off >> c) & 1 != 0;
            held.iter_mut().for_each(|r| r.off |= off);
            for (x, r) in held.iter().enumerate().filter(|(_, r)| !r.off) {
                let (at_a, at_c) = ends(r);
                let (n, over) = (r.n, r.gap + ALLOW());
                let pair = |dir: Vec3| -> Vec<f64> {
                    lrow(a, d(at_a), d(dir), DVec3::ZERO).iter().zip(lrow(c, d(at_c), d(dir), DVec3::ZERO)).map(|(x, y)| x - y).collect()
                };
                let e = (PART_ELASTICITY() * PART_ELASTICITY()) as f64;
                let j = frame(n).map(pair);
                let to = DVec3::new((-over.max(0.0) / h) as f64 - fv(&j[0]) / (1.0 + e), -fv(&j[1]), -fv(&j[2]));
                blocks.push(Block { scale: elastic(e), ..Block::new(artic, j, to, (part_friction(&build.parts[a]) * part_friction(&build.parts[c])) as f64, Some((n_links + pi, x)), r.imp, true) });
                push_blocks.push(Self::push_block(blocks.last().unwrap(), over, h, r.pimp));
            }
        }
        for (pi, &(a, _)) in self.pairs.iter().enumerate() {
            for r in self.held_pairs[pi].iter().filter(|r| !r.off) { self.gap_audit[4].note(r.gap, a, 0); }
        }
        // Sources are sparse in hit order. Index only their identity; retain
        // the first-match rule and the original hit/manifold/solver sequence.
        // Scanning the entire source list twice for every mesh contact was
        // quadratic (over a million comparisons for a 1,221-contact rider).
        let source_slots=car_slot_index_enabled().then(||{
            let mut slots=vec![None;hits.len()];
            for (slot,source) in hit_ob.iter().enumerate(){slots[source.0].get_or_insert(slot);}
            slots
        });
        let source_for=|i:usize|if let Some(slots)=&source_slots{slots[i].map(|slot|&hit_ob[slot])}else{hit_ob.iter().find(|x|x.0==i)};
        for (i, h) in hits.iter().enumerate() {
            let (category, other) = match source_for(i) {
                Some(x) => { let ob = obstacles.iter().find(|o| o.id == x.1);
                    let ped = ob.is_some_and(|o| o.owner.is_some() && o.filter.animated_ped());
                    (if ped { 2 } else { 1 }, ob.and_then(|o| o.owner).unwrap_or(x.1 as u32 as u64)) }
                None if h.7.is_some() => (0, 0),
                None => (3, 0),
            };
            self.gap_audit[category].note(h.3, h.0, other);
        }
        audit.add(3,self_tick);
        let n_self = blocks.len();
        #[allow(unused_mut)]
        let mut gwarm = vec![];
        for (k, held) in self.held.iter_mut().enumerate() {
            let on = self.contact_off >> k & 1 == 0;
            held.iter_mut().for_each(|r| r.off = crate::rigid::contact_pass(if r.off { 0 } else { delirium_data::kv!(249 int) }, 0, on) < delirium_data::kv!(249 int));
        }
        let on = |&(_, h): &(usize, &(usize, Vec3, Vec3, f32, f32, Vec3, f32, Option<(usize, usize)>))| self.contact_off >> h.0 & 1 == 0 && h.7.map_or(true, |r| !self.rec(r).off);
        let hits: Vec<_> = hits.iter().enumerate().filter(on).map(|(i, h)| (i, *h)).collect();
        let hit_count=hits.len();let block_tick=audit.timer();
        let mut response_cache=ContactResponses::default();
        let cache_responses=hit_count>n_links&&car_slot_index_enabled()&&!(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_CONTACT_RESPONSE_BASELINE"));
        for (i, (k, at, n, gap, mu, v, e, held)) in hits {
            let gap = gap + ALLOW();
            let ts = frame(n);
            let j = ts.map(|t| lrow(k, d(at), d(t), DVec3::ZERO));
            let to = DVec3::new((-gap.max(0.0) / h + v.dot(n)) as f64 - fv(&j[0]) / (1.0 + e as f64), v.dot(ts[1]) as f64 - fv(&j[1]), v.dot(ts[2]) as f64 - fv(&j[2]));
            let warm = held.map_or(gwarm.get(i).copied().unwrap_or(Vec3::ZERO), |r| self.rec(r).imp);
            let source=source_for(i);
            let ob = source.map(|&(_, id, at, n,_)| (id, k, at, n));
            let external=source.and_then(|x|x.4);
            let block=if cache_responses{Block::cached(artic,k,j,to,mu as f64,held,warm,&mut response_cache)}else{Block::new(artic,j,to,mu as f64,held,warm,false)};
            blocks.push(Block { scale: elastic(e as f64), ob, external, ..block });
            push_blocks.push(Self::push_block(blocks.last().unwrap(), gap, h, held.map_or(Vec3::ZERO, |r| self.rec(r).pimp)));
        }
        audit.add(4,block_tick);
        // LSC4 0b0aef3a, 82993F98 / 8299B768: the grip's separation is
        // positional error, not a launch velocity. A frictionless contact
        // handles velocity, then the existing push pass closes the gap.
        // Below 1 cm this is a type-14 point constraint (three axes).
        // Keep the actual finite break strength; no stronger grip or timer.
        for &(k, local, to, len, strength) in &self.grips {
            let at=self.grip_world(k,local)-o;
            let gap=to-(at+o);let dist=gap.length();
            let pin=len<0.01;
            if dist<len||(!pin&&dist<=1.0e-6) {continue}
            // A coincident point still constrains relative velocity. Its
            // three-axis basis is arbitrary when there is no position error.
            let normal=if dist>1.0e-6 {gap/dist} else {Vec3::X};let ts=frame(normal);
            let j=ts.map(|t|lrow(k,d(at),d(t),DVec3::ZERO));
            let measured=self.grip_target_velocities.iter().find(|x|x.0==k).map_or(Vec3::ZERO,|x|x.1);
            // Diagnostic A/B only in the owned passive test process. Preserve
            // the old world-static velocity row without changing poses, load
            // thresholds, positional repair or controller clocks.
            let velocity=if env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_GRIP_STATIC_BASELINE") {Vec3::ZERO} else {measured};
            let target=DVec3::from_array(std::array::from_fn(|i|ts[i].dot(velocity) as f64-fv(&j[i])));
            let external=self.grip_target_owners.iter().find(|x|x.0==k).map(|x|ExternalContact{id:x.1,point:to,basis:ts});
            let block=Block{pin,grip:Some((k,strength)),external,..Block::new(artic,j,target,0.,None,Vec3::ZERO,false)};
            let depth=-(dist-len).max(0.).min(0.6);
            let mut push=Self::push_block(&block,depth+ALLOW(),h,Vec3::ZERO);
            if pin {push.target=DVec3::new((-depth/h) as f64,0.,0.);}
            blocks.push(block);push_blocks.push(push);
        }
        capture_dense_construction(artic,&blocks[n_self..]);
        for v in [&mut blocks, &mut push_blocks] {
            v.rotate_left(n_self);
            island_sort(v);
        }

        audit.finish(hit_count,blocks.len(),self.held_car.len());
        (rows, blocks, push_blocks)
    }

    fn rec(&self, (k, x): (usize, usize)) -> &Held {
        let (g, pp) = (self.held.len(), self.held_pairs.len());
        if k < g { &self.held[k][x] } else if k < g + pp { &self.held_pairs[k - g][x] } else { &self.held_car[k - g - pp].3[x] }
    }

    fn rec_mut(&mut self, (k, x): (usize, usize)) -> &mut Held {
        let (g, pp) = (self.held.len(), self.held_pairs.len());
        if k < g { &mut self.held[k][x] } else if k < g + pp { &mut self.held_pairs[k - g][x] } else { &mut self.held_car[k - g - pp].3[x] }
    }

    fn pair_manifold(held: &mut Vec<Held>, (a, fa, sa): (&PartDef, (Vec3, Mat3), (Vec3, Mat3)), (c, fc, sc): (&PartDef, (Vec3, Mat3), (Vec3, Mat3)),
                     [(ca, ha), (cc, hc)]: [(Vec3, Vec3); 2]) {
        let ((_, qa), (_, qc)) = (fa, fc);
        let meet = boxes_meet(ca, qa, ha, cc, qc, hc);
        if !meet {
            held.clear();
            return;
        }
        Self::manifold(held, (part_shape(a), bound_half(a), fa, sa), (part_shape(c), bound_half(c), fc, sc));
    }

    fn manifold(held: &mut Vec<Held>, (a, ha, fa, sa): (gjk::Shape, Vec3, (Vec3, Mat3), (Vec3, Mat3)), (c, hc, fc, sc): (gjk::Shape, Vec3, (Vec3, Mat3), (Vec3, Mat3))) {
        let ((xa, qa), (xc, qc)) = (fa, fc);
        let (la, lc) = (sa.0, sc.0);
        let moved = (xa - la) - (xc - lc);
        let fast = (qa.transpose() * moved).abs().cmpgt(ha).any() || (qc.transpose() * moved).abs().cmpgt(hc).any();
        let found = if held.is_empty() && fast {
            Self::cast_shapes((&a, &c), (la, xa, qa), (lc, xc, qc)).map(|(at_a, at_c, n)| {
                let (pa, pc) = (xa + qa * at_a, xc + qc * at_c);
                (pa, pc, n, (pa - pc).dot(n))
            })
        } else {
            Self::shape_contact((&a, &c), fa, fc, MARGIN(), Vec3::Z).map(|q| (q.a, q.b, q.n, q.dist))
        };
        if let Some((at_a, at_c, n, gap)) = found {
            hold(held, Held { local: qa.transpose() * (at_a - xa), ground: qc.transpose() * (at_c - xc), gap, age: 0, m: 0, fl:0, imp: Vec3::ZERO, pimp: Vec3::ZERO, n, inst: 0, off: false });
        }
        let ends = |r: &Held| (xa + qa * r.local, xc + qc * r.ground);
        for r in held.iter_mut() {
            if r.age != 0 {
                let (pa, pc) = ends(r);
                r.gap = (pa - pc).dot(r.n);
            }
            r.age += if r.age == 1 { 2 } else { 1 };
        }
        let slid = |r: &Held| { let (pa, pc) = ends(r); let d = pc - pa; (d - r.n * d.dot(r.n)).length_squared() };
        let (mut k, mut worst) = (0, (None, 0.0));
        while k < held.len() {
            if held[k].gap >= MARGIN() {
                held.swap_remove(k);
                continue;
            }
            let d = slid(&held[k]);
            if held[k].age > delirium_data::kv!(1 int) && d > worst.1 && d > delirium_data::kv!(568) {
                worst = (Some(k), d);
            }
            k += 1;
        }
        if let Some(k) = worst.0 {
            held.swap_remove(k);
        }
    }

    fn cast_shapes((a, c): (&gjk::Shape, &gjk::Shape), (la, xa, qa): (Vec3, Vec3, Mat3), (lc, xc, qc): (Vec3, Vec3, Mat3)) -> Option<(Vec3, Vec3, Vec3)> {
        let at = |t: f32| (la + (xa - la) * t, lc + (xc - lc) * t);
        let axis = std::cell::Cell::new(Vec3::Z);
        let query = |t: f32| {
            let (pa, pc) = at(t);
            let hit = Self::shape_contact((a, c), (pa, qa), (pc, qc), f32::INFINITY, axis.get())?;
            axis.set(hit.n);
            Some(hit)
        };
        let (t, q) = Self::advance(query, (xc - lc) - (xa - la))?;
        let (pa, pc) = at(t);
        Some((qa.transpose() * (q.a - pa), qc.transpose() * (q.b - pc), q.n))
    }

    fn advance(query: impl Fn(f32) -> Option<gjk::Closest>, r: Vec3) -> Option<(f32, gjk::Closest)> {
        let q=query(0.0)?;
        Self::advance_from(query,r,q)
    }
    fn advance_from(query: impl Fn(f32) -> Option<gjk::Closest>, r: Vec3,initial:gjk::Closest) -> Option<(f32, gjk::Closest)> {
        let (mut t, mut prev, mut q) = (0.0f32, 0.0f32, initial);
        let mut step = q.dist;
        if step > delirium_data::kv!(125) {
            for it in 1.. {
                let nr = q.n.dot(r);
                let dt = if nr != 0.0 { step / nr } else { f32::MAX };
                t += dt;
                if it > delirium_data::kv!(52 int) || dt < 0.0 || t > 1.0 {
                    return None;
                }
                if t <= prev {
                    break;
                }
                prev = t;
                q = query(t)?;
                if q.dist <= delirium_data::kv!(572) {
                    break;
                }
                step = q.dist - delirium_data::kv!(109);
            }
        }
        Some((t, q))
    }
    fn shape_contact((a, c): (&gjk::Shape, &gjk::Shape), ba: (Vec3, Mat3), bc: (Vec3, Mat3), reach: f32, axis: Vec3) -> Option<gjk::Closest> {
        gjk::closest(a, ba.1, ba.0, c, bc.1, bc.0, axis, f32::MAX.sqrt(), Some(gjk::Pen::Sphere))
            .filter(|q| q.dist < reach)
    }

    fn velocities(&self, artic: &Artic, lt: &[V6], h: f32, forces: bool) -> Vec<(Vec3, Vec3)> {
        let root = self.order[0];
        let xr = self.bodies[root].x;
        let x: Vec<Vec3> = self.bodies.iter().map(|b| b.x - xr).collect();
        let (v, w): (Vec<Vec3>, Vec<Vec3>) = self.bodies.iter().map(|b| (b.v, b.w)).unzip();
        let acc: Vec<artic::P2> = lt.iter().zip(&x).map(|(&a, &x)| {
            let (f, t) = artic::parts(a);
            (f.as_vec3(), (t + x.as_dvec3().cross(f)).as_vec3())
        }).collect();
        let mut imp: Vec<artic::P2> = vec![(Vec3::ZERO, Vec3::ZERO); x.len()];
        for &(k, at, i, a) in self.pending.iter().filter(|_| !forces) {
            let a = a.as_dvec3() + (at - xr).as_dvec3().cross(i.as_dvec3());
            imp[k] = (imp[k].0 + i, imp[k].1 + a.as_vec3());
        }
        artic.velocities(root, &x, &v, &w, &acc, &imp, h, forces)
    }

    fn rejoin(&mut self, joints: &[Option<Joint>], pose: &[Option<(Vec3, Quat)>]) {
        for &k in &self.order {
            let Some(j) = joints[k] else {
                if let Some((x, q)) = pose[k] {
                    self.bodies[k].x = x;
                    self.bodies[k].set_q(q);
                }
                continue;
            };
            let p = self.bodies[j.parent].link();
            let b = &mut self.bodies[k];
            let mut c = b.link();
            crate::rigid::join(&j.jc, &p, &mut c, matches!(j.kind, Kind::Hinge { .. }));
            (b.x, b.v, b.w) = (c.d, c.v, c.w);
            b.set_r(c.r);
        }
    }

    fn jconst(&self, build: &Build, k: usize, j: &Joint, rigid: Option<&Rigid>) -> crate::rigid::JointConst {
        // Delirium v0.6.0 b1c6b1e: authored angular constraints remain valid,
        // but their source-body pivots do not belong to a fitted SA skeleton.
        let grown = |jc: crate::rigid::JointConst| {
            if build.parts[j.parent].size == 1.0 && build.parts[k].size == 1.0 { jc } else {
                let (lp, lc, part) = (self.bodies[j.parent].l.inverse(), self.bodies[k].l.inverse(), &build.parts[k]);
                crate::rigid::JointConst { pa: lp * part.anchor_parent, ca: lc * part.anchor, ..jc }
            }
        };
        rigid.and_then(|r| r.jc).map(grown).unwrap_or_else(|| {
            let (lp, lc, part) = (self.bodies[j.parent].l.inverse(), self.bodies[k].l.inverse(), &build.parts[k]);
            let rows = |q: Quat| { let m = Mat3::from_quat(q); [m.row(0), m.row(1), m.row(2)] };
            crate::rigid::JointConst { pm: rows(lp * j.pf), pa: lp * part.anchor_parent, ca: lc * part.anchor, cm: rows(lc * j.cf) }
        })
    }

    fn recentre(&mut self, pose: &mut [Option<(Vec3, Quat)>]) -> Vec3 {
        let d0 = self.bodies[self.order[0]].x;
        let back = Vec3::ZERO - d0;
        self.o += d0;
        self.bodies.iter_mut().for_each(|b| b.x += back);
        self.pins.iter_mut().flatten().for_each(|p| p.translation += back);
        self.pending.iter_mut().for_each(|p| p.1 += back);
        pose.iter_mut().flatten().for_each(|p| p.0 += back);
        back
    }

    fn rejuvenate(&mut self) {
        self.rejuv += 1;
        if self.rejuv <= delirium_data::kv!(7 int) {
            return;
        }
        self.bodies.iter_mut().for_each(|b| b.set_r(crate::rigid::resquare(b.r)));
        let x = self.mwc.0 as u64 * delirium_data::kv!(184 int) + self.mwc.1 as u64;
        self.mwc = (x as u32, (x >> delirium_data::kv!(7 int)) as u32);
        self.rejuv = (x as u32 & delirium_data::kv!(527 int)) % delirium_data::kv!(51 int);
    }

    pub fn step(&mut self, build: &Build, world: &Collision, dt: f32, drive: Drive, obstacles: &[Obstacle], others: &[Capsule]) {
        let Some(st) = self.begin(build, world, dt, &drive, obstacles, others) else { return };
        self.finish(st, drive.lift);
    }

    fn finish(&mut self, mut st: Stage, lift: f32) {
        impulses(&mut st.rows, &mut st.blocks, &mut st.qd0.clone());
        if !self.middle(&mut st) {
            return;
        }
        let mut qp = vec![0.0; st.qd0.len()];
        impulses(&mut st.push, &mut st.push_blocks, &mut qp);
        self.end(st, &qp, lift);
    }

    fn begin(&mut self, build: &Build, world: &Collision, dt: f32, drive: &Drive, obstacles: &[Obstacle], others: &[Capsule]) -> Option<Stage> {
        static PROFILE:std::sync::OnceLock<Option<std::sync::Mutex<std::fs::File>>>=std::sync::OnceLock::new();
        let profile=PROFILE.get_or_init(||std::env::var_os("GTASA_PREPARATION_PROFILE").and_then(|p|std::fs::OpenOptions::new().create(true).append(true).open(std::path::PathBuf::from(p)).ok()).map(std::sync::Mutex::new));
        let started=profile.as_ref().map(|_|std::time::Instant::now());
        #[cfg(windows)] fn cycles()->u64{
            #[link(name="kernel32")] unsafe extern "system"{fn QueryThreadCycleTime(thread:*mut std::ffi::c_void,cycles:*mut u64)->i32;}
            let mut n=0;unsafe{QueryThreadCycleTime((-2isize) as *mut _,&mut n)};n
        }
        #[cfg(not(windows))] fn cycles()->u64{0}
        let cpu_started=profile.as_ref().map(|_|cycles());

        // The SDK leases this controller for a whole update, including every
        // internal substep. Reevaluate it from the current physical velocity.
        let roll_accel=self.roll_push.and_then(|(set,v0,rolled)|crate::jumproll::push(&set,self.motion(0).0,v0,rolled)).unwrap_or(Vec3::ZERO);
        if roll_accel != Vec3::ZERO { self.wake(); }
        self.snapped.clear();
        self.rigid = None;
        self.contact_off |= self.gone;
        if self.gone != 0 {
            let g = self.gone;
            self.grips.retain(|x| g >> x.0 & 1 == 0);
            for (k, (p, c)) in self.pins.iter_mut().zip(&mut self.cons).enumerate().filter(|(k, _)| g >> k & 1 != 0) {
                let _ = k;
                (*p, *c) = (None, (None, None));
            }
        }
        // Only a moving obstacle that reaches the body wakes it (a passer-by or a bike's spinning wheel metres
        // away kept a frozen corpse simulated, and pushing what it lay on, every frame).
        let (lo, hi) = self.bodies.iter().fold((Vec3::splat(f32::MAX), Vec3::splat(f32::MIN)), |(l, h), b| (l.min(b.x + self.o), h.max(b.x + self.o)));
        let (lo, hi) = (lo - Vec3::splat(0.6), hi + Vec3::splat(0.6));
        let reaches = |o: &Obstacle| {
            let r = Mat3::from_quat(o.rot);
            let e = Mat3::from_cols(r.x_axis.abs(), r.y_axis.abs(), r.z_axis.abs()) * o.half + Vec3::splat(2.0 * o.vel.length() * dt); // two steps of travel
            (o.centre - e).cmple(hi).all() && lo.cmple(o.centre + e).all()
        };
        let moving = obstacles.iter().any(|o| (o.vel.length() > 0.5 || o.spin.length() > 0.5) && reaches(o));
        // The ignored entity ends only once the bounds no longer touch (SA CPhysical::ProcessCollisionSectorList,
        // GetIsTouching on the bound sphere; IV 8247EF08). The contact path alone never saw the bike it was set
        // for: it ended on the first step with the legs still inside the frame, and the next contact threw the
        // body up out of the bike, up to 8 m (lethal bike falls, 06/11). Per part, as NM bodies are articulated:
        // only the parts inside the vehicle when it is first staged ignore it, each until its own bound sphere
        // leaves every component box. A whole-body exclusion let a shot rider's torso sink through the tank.
        // A step without that vehicle staged (frozen corpse, another world) proves nothing.
        self.no_collision_seen = 0;
        if let Some(id) = self.no_collision {
            let owned: Vec<&Obstacle> = obstacles.iter().filter(|o| o.owner == Some(id)).collect();
            if owned.is_empty() {
                self.no_collision_met = true;
            } else {
                let mut touching = 0u32;
                for (k, b) in self.bodies.iter().enumerate().take(32) {
                    let (c, r) = (b.x + self.o, bound_half(&build.parts[k]).length());
                    if owned.iter().any(|o| {
                        let local = Mat3::from_quat(o.rot).transpose() * (c - o.centre);
                        (local.abs() - o.half).max(Vec3::ZERO).length() < r
                    }) {
                        touching |= 1 << k;
                    }
                }
                self.no_collision_parts = self.no_collision_parts & touching | self.no_collision_hold;
                self.no_collision_seen = 1 | if self.no_collision_parts != 0 { 2 } else { 0 };
                self.no_collision_met |= self.no_collision_parts != 0;
            }
        }
        if self.asleep && !moving && self.pending.is_empty() {
            self.out.fill((Vec3::ZERO, Vec3::ZERO));
            return None;
        }
        if moving {
            self.wake();
        }
        let d = self.bodies[0].x;
        if d.length_squared() > delirium_data::kv!(28) * delirium_data::kv!(28) {
            self.o += d;
            self.bodies.iter_mut().for_each(|b| b.x -= d);
            self.pins.iter_mut().flatten().for_each(|p| p.translation -= d);
            self.pending.iter_mut().for_each(|p| p.1 -= d);
        }
        self.age += dt;
        for t in &mut self.touch {
            *t = (*t & !delirium_data::kv!(90 int)) | ((*t >> delirium_data::kv!(0 int)) & delirium_data::kv!(1 int));
        }
        self.hit.iter_mut().for_each(|h| *h = None);
        let joints: Vec<Option<Joint>> = build.parts.iter().enumerate().map(|(i, p)| p.parent.map(|j| {
            let j = Joint::new(p, j, drive.rigid.get(i));
            Joint { jc: self.jconst(build, i, &j, drive.rigid.get(i)), ..j }
        })).collect();
        let subs = 1;
        let h = dt / subs as f32;
        let spin = if drive.rigid.is_empty() { MAX_SPIN } else { MAX_SPIN_NM() };
        let fall = Vec3::Z * -GRAVITY() * (1.0 - drive.lift) + drive.accel + roll_accel;
        let (start, o0) = (self.bodies.iter().map(|b| (b.x, b.q)).collect::<Vec<_>>(), self.o);
        let along = 1.0 / subs as f32;
        let mut pose: Vec<Option<(Vec3, Quat)>> = self.pins.iter().zip(&start).map(|(p, &(x, q))| {
            p.map(|to| (x.lerp(to.translation, along), q.slerp(to.rotation, along)))
        }).collect();
        let was: Vec<Option<Joint>> = joints.iter().zip(&self.stiff).map(|(j, &stiff)| j.map(|j| Joint { stiff, ..j })).collect();
        let artic = self.artic(if was.len() == joints.len() { &was } else { &joints });
        self.stiff = joints.iter().map(|j| j.map_or(0.0, |j| j.stiff)).collect();
        let mut lt: Vec<V6> = self.bodies.iter().enumerate().map(|(k, b)| {
            v6((fall * b.m + drive.force.get(k).filter(|_| self.gone >> k & 1 == 0).copied().unwrap_or(Vec3::ZERO)).as_dvec3(), drive.torque.get(k).filter(|_| self.gone >> k & 1 == 0).copied().unwrap_or(Vec3::ZERO).as_dvec3())
        }).collect();
        let mut jt = vec![Vec3::ZERO; self.bodies.len()];
        self.muscles(&artic, &joints, &drive, h, &mut lt, &mut jt);
        self.soft_limits(&artic, &joints, &drive, &mut lt, &mut jt);
        for (k, b) in self.bodies.iter().enumerate() {
            let f = (fall + Vec3::Z * GRAVITY()) * b.m + drive.force.get(k).filter(|_| self.gone >> k & 1 == 0).copied().unwrap_or(Vec3::ZERO);
            self.out[k] = (f, drive.torque.get(k).filter(|_| self.gone >> k & 1 == 0).copied().unwrap_or(Vec3::ZERO) + jt[k]);
        }
        self.joint_torque = jt.clone();
        for (k, j) in joints.iter().enumerate() {
            if let Some(j) = j {
                self.out[j.parent].1 -= jt[k];
            }
        }
        let inst = self.inst();
        if self.coll.len() != self.bodies.len() {
            self.coll = self.comp(&inst);
        }
        let xr = self.bodies[self.order[0]].x;
        let mut now: Vec<(Vec3, Mat3)> = self.coll.iter().map(|(d, c)| (xr + crate::rigid::row_mul(*d, &inst), turn(&crate::rigid::rows_mul(c, &inst)))).collect();
        let d = self.bodies[self.order[0]].v * h;
        self.pending.iter_mut().for_each(|p| p.1 += d);
        for b in &mut self.bodies {
            let (v, w) = (b.v, b.w);
            b.moved(v, w, h);
        }
        self.rejoin(&joints, &pose);
        let back = self.recentre(&mut pose);
        now.iter_mut().for_each(|f| f.0 += back);
        self.rejuvenate();
        let artic = self.artic(&joints);
        let before: Vec<V6> = lt.clone();
        self.limits(&artic, &joints, h, &mut lt);
        self.stage_lim = lt.iter().zip(&before).map(|(a, b)| (artic::parts(*a).1 - artic::parts(*b).1).as_vec3()).collect();
        {
            let xr = self.bodies[self.order[0]].x;
            self.stage_lt = lt.iter().zip(&self.bodies).map(|(x, b)| { let (f, t) = artic::parts(*x); v6(f, t + (b.x - xr).as_dvec3().cross(f)) }).collect();
        }
        let dvw: Vec<(Vec3, Vec3)> = self.velocities(&artic, &lt, h, true).iter().zip(&self.bodies).map(|(&(v, w), b)| (v - b.v, w - b.w)).collect();
        if env_on!("NM_DVW") { eprintln!("dvw {}", dvw.iter().zip(&self.bodies).enumerate().map(|(k, ((v, w), b))| format!("{k}:{:.5},{:.5},{:.5},{:.5},{:.5},{:.5},{:.4},{:.4},{:.4}", v.x, v.y, v.z, w.x, w.y, w.z, b.x.x, b.x.y, b.x.z)).collect::<Vec<_>>().join(" ")); }
        let free_vel: Vec<(Vec3, Vec3)> = self.bodies.iter().zip(&dvw).map(|(b, d)| (b.v + d.0, b.w + d.1)).collect();
        self.armature = artic.arm.clone();
        let x0 = self.bodies[self.order[0]].x.as_dvec3();
        let qd0 = spatial(&self.bodies, x0);
        let free = spatial_of(&dvw, &self.bodies, x0);
        self.seen = self.contact_history.take().map_or(now,|old|
            old.into_iter().map(|(p,q)|(p-self.o,q)).collect());
        self.coll = self.comp(&self.inst());
        let before_contacts=started.map(|t|t.elapsed().as_secs_f64()*1000.);
        let (mut rows, blocks, push_blocks) = self.contacts(build, world, &artic, h, &pose, obstacles, others, &free);
        let after_contacts=started.map(|t|t.elapsed().as_secs_f64()*1000.);

        let (lr, push) = self.limit_rows(&artic, &joints, h, &qd0);
        rows.extend(lr);
        if let(Some(file),Some(started))=(profile,started){
            let ms=started.elapsed().as_secs_f64()*1000.;
            let cpu_cycles=cycles().saturating_sub(cpu_started.unwrap());
            if ms>=0.5{if let Ok(mut f)=file.lock(){use std::io::Write;
                let _=writeln!(f,"{{\"pid\":{},\"ms\":{},\"cpu_cycles\":{},\"pre_contacts_ms\":{},\"contacts_ms\":{},\"limit_rows_ms\":{},\"obstacles\":{},\"components\":{},\"rows\":{},\"blocks\":{},\"body_world\":{:?}}}",std::process::id(),ms,cpu_cycles,before_contacts.unwrap(),after_contacts.unwrap()-before_contacts.unwrap(),ms-after_contacts.unwrap(),obstacles.len(),obstacles.iter().map(|o|o.parts.len()).sum::<usize>(),rows.len(),blocks.len(),(self.bodies[0].x+self.o).to_array());
            }}
        }
        Some(Stage { joints, pose, start, o0, artic, lt, x0, qd0, rows, blocks, push, push_blocks, h, spin, free_vel, damp: 0.0, free })
    }

    fn middle(&mut self, st: &mut Stage) -> bool {
        let mass = self.mass();
        self.ob_hits.clear();
        for b in &st.blocks {
            let Some((id, k, at, n)) = b.ob else { continue };
            let ts = frame(n);
            let l = (b.lambda/b.warm_divisor).as_vec3();
            let impulse = ts[0] * l.x + ts[1] * l.y + ts[2] * l.z;
            let a = (b.lambda * b.scale).as_vec3();
            // Per-character contact evidence remains available for original
            // road/bump decisions. Finite mobility is committed ONCE through
            // Group's aggregate result: hosts must not reapply these impulses
            // when the obstacle has a staged finite owner.
            self.ob_hits.push((id, -(ts[0] * a.x + ts[1] * a.y + ts[2] * a.z), at));
            let c = crate::bump::Contact { other: Some(id), other_ragdoll: true, impulse, normal: [n.x, n.y, n.z, 0.0], point: [at.x, at.y, at.z, 0.0],
                                           component: k as i32, extra: [0; 2] };
            self.contact.add(crate::bump::Gate { ragdoll: true, flagged: false, damages: false }, &c, mass, 0);
        }
        for b in &st.blocks {
            match b.held {
                Some(r) => self.rec_mut(r).imp = (b.lambda/b.warm_divisor).as_vec3(),
                None => {}
            }
        }
        for b in &st.blocks {
            if let Some((k,strength))=b.grip {
                let load=b.lambda.x.abs() as f32;
                if load>self.grip_load[0] {self.grip_load[0]=load;self.grip_load[1]=strength;}
                if strength>=0.0&&load>strength {
                    self.snapped.push(k);self.grip_load[2]=load;self.grip_load[3]+=1.;
                }
            }
        }
        self.grip_solved_target=self.grips.first().map(|g|g.2);
        let mut gi = vec![0.0; st.qd0.len()];
        for r in &st.rows {
            gi.iter_mut().zip(&r.j).for_each(|(g, j)| *g += j * r.lambda);
        }
        for b in &st.blocks {
            for (j, l) in b.j.iter().zip((b.lambda * b.scale).to_array()) {
                gi.iter_mut().zip(j).for_each(|(g, j)| *g += j * l);
            }
        }
        for (k, g) in gi.chunks(delirium_data::kv!(6 int)).enumerate().filter(|(_, g)| g.iter().any(|&x| x != 0.0)) {
            self.pending.push((k, st.x0.as_vec3(), DVec3::new(g[0], g[1], g[2]).as_vec3(), DVec3::new(g[delirium_data::kv!(0 int)], g[delirium_data::kv!(1 int)], g[delirium_data::kv!(2 int)]).as_vec3()));
        }
        let vw = self.velocities(&st.artic, &st.lt, st.h, false);
        self.pending.clear();
        for (b, (v, w)) in self.bodies.iter_mut().zip(vw) {
            (b.v, b.w) = (v, w);
        }
        self.stage_vel = self.bodies.iter().map(|b| (b.v, b.w)).collect();
        if env_on!("NM_SVFREE") { self.stage_vel = st.free_vel.clone(); }
        if env_on!("NM_VELONLY") { return false; }
        let damp = (-DAMPING()).fma(st.h, 1.0);
        for b in &mut self.bodies {
            b.v = b.v.clamp_length_max(MAX_SPEED()) * damp;
            b.w = b.w.clamp_length_max(st.spin) * damp;
        }
        st.damp = damp;
        true
    }

    fn end(&mut self, mut st: Stage, qp: &[f64], lift: f32) {
        if env_on!("NM_GRIP_AUDIT") && !self.grips.is_empty() {
            for b in st.push_blocks.iter().filter(|b| b.grip.is_some()) {
                eprintln!("grip_push {:?} target={:?} lambda={:?} kinv={:?} qp_finite={} qp_max={}",b.grip,b.target,b.lambda,b.kinv,qp.iter().all(|x|x.is_finite()),qp.iter().fold(0f64,|a,x|a.max(x.abs())));
            }
        }
        for (p, b) in st.push_blocks.iter().zip(&st.blocks) {
            match b.held {
                Some(r) => self.rec_mut(r).pimp = p.lambda.as_vec3(),
                None => {}
            }
        }
        for (k, b) in self.bodies.iter_mut().enumerate() {
            let (pv, pw) = artic::parts(std::array::from_fn(|c| qp[delirium_data::kv!(6 int) * k + c]));
            let (v, w) = ((pv + pw.cross(b.x.as_dvec3() - st.x0)).as_vec3(), pw.as_vec3());
            b.moved(v.clamp_length_max(delirium_data::kv!(554) / st.h) * st.damp, w.clamp_length_max(delirium_data::kv!(573) / st.h) * st.damp, st.h);
        }
        // middle()/Artic::velocities already store linear velocity at each
        // body's COM, not the spatial linear coefficient at the old origin.
        // The split displacement above is geometric repair, without physical
        // momentum. recentre() translates coordinates only: adding omega x
        // root.x here incorrectly turned that repair into a launch impulse.
        // Joint reattachment below still enforces the new joint geometry.
        self.rejoin(&st.joints, &st.pose);
        self.recentre(&mut st.pose);
        if self.bodies.iter().any(|b| !(b.x.is_finite() && b.r.iter().all(|r| r.is_finite()) && b.v.is_finite() && b.w.is_finite())) {
            let back = st.o0 - self.o;
            for (b, &(x, q)) in self.bodies.iter_mut().zip(&st.start) {
                (b.x, b.v, b.w) = (x + back, Vec3::ZERO, Vec3::ZERO);
                b.set_q(q);
            }
        }
        self.sleep();
        self.asleep = self.mode == 2 && lift == 0.0 && self.pins.iter().all(Option::is_none) && self.grips.is_empty()
            && self.cons.iter().all(|c| c.0.is_none() && c.1.is_none());
    }
}

struct Stage {
    joints: Vec<Option<Joint>>,
    pose: Vec<Option<(Vec3, Quat)>>,
    start: Vec<(Vec3, Quat)>,
    o0: Vec3,
    artic: Artic,
    lt: Vec<V6>,
    x0: DVec3,
    qd0: Vec<f64>,
    rows: Vec<Row>,
    blocks: Vec<Block>,
    push: Vec<Row>,
    push_blocks: Vec<Block>,
    h: f32,
    spin: f32,
    free_vel: Vec<(Vec3, Vec3)>,
    damp: f32,
    free: Vec<f64>,
}

#[cfg(test)]
thread_local!{static POSITION_PREVIEW_REUSE:std::cell::Cell<bool>=const{std::cell::Cell::new(true)};}

#[derive(Default)]
pub struct Group {
    pub profile_enabled:bool,
    pub profile_ms:[f64;4], // native ground, articulated preparation, owner pairs, remaining solve
    pub profile_impulses:([f64;3],[usize;5]),
    pub profile_solve_ms:[f64;8],
    pub profile_solve_cycles:[u64;8],
    pub profile_pair_prepare_ms:f64,
    pub profile_pair_counts:[usize;4],
    pub profile_shape:[usize;3], // bodies solved alone, coupled islands, largest island
    held: std::collections::HashMap<(usize, usize, usize, usize), Vec<Held>>,
    external: crate::external_mobility::ExternalRegistry,
    external_failure:Option<crate::external_mobility::ExternalError>,
    external_geometry:Vec<Obstacle>,
    external_environment_geometry:Vec<Obstacle>,
    external_environment_failure:Option<crate::external_mobility::ExternalError>,
    external_contacts:Vec<crate::external_mobility::ExternalContact>,
    external_predictors:std::collections::BTreeMap<u64,crate::external_mobility::ExternalPredictor>,
    external_placements:Vec<crate::external_mobility::ExternalPlacement>,
    external_placement_diagnostics:Vec<crate::external_mobility::ExternalPlacementDiagnostic>,
    external_drives:std::collections::BTreeMap<u64,crate::external_mobility::ExternalDrive>,
    external_drives_staged:bool,
    ground:external_ground::Registry,
    external_drive_consumed_epoch:Option<u64>,
    external_replay_steps:Vec<(crate::external_mobility::ExternalOwner,f32,Vec3,Vec3,Vec3,u32)>,
    external_failed_replays:Vec<crate::external_mobility::ExternalPlacementReplay>,
}
impl Group {
    pub fn stage_external(&mut self,epoch:u64,owners:&[crate::external_mobility::ExternalOwner])->Result<(),crate::external_mobility::ExternalError>{self.external_failure=None;self.external.stage(epoch,owners)?;self.external_geometry.clear();self.external_environment_geometry.clear();self.external_environment_failure=None;self.external_contacts.clear();self.external_predictors.clear();self.external_placements.clear();self.external_placement_diagnostics.clear();self.external_replay_steps.clear();self.external_failed_replays.clear();self.external_drives.clear();self.external_drives_staged=false;self.ground.clear_stage();Ok(())}
    pub fn stage_external_drives(&mut self,epoch:u64,values:&[crate::external_mobility::ExternalDrive])->Result<(),crate::external_mobility::ExternalError>{
        use crate::external_mobility::ExternalError as E;
        if epoch!=self.external.epoch{return Err(E::WrongEpoch(epoch));}
        if self.external_drive_consumed_epoch==Some(epoch){return Err(E::ConsumedDriveEpoch(epoch));}
        let mut next=std::collections::BTreeMap::new();
        for &d in values{
            if !self.external.owners.contains_key(&d.id){return Err(E::MissingSnapshot(d.id));}
            if !d.impulse.is_finite()||!d.torque.is_finite(){return Err(E::InvalidDrive(d.id));}
            if let Some(old)=next.insert(d.id,d){if old!=d{return Err(E::DivergentDrive(d.id));}}
        }
        // An explicit zero and an absent drive have the same physical meaning.
        next.retain(|_,d|d.impulse!=Vec3::ZERO||d.torque!=Vec3::ZERO);
        if self.external_drives_staged&&next!=self.external_drives{
            let id=next.keys().chain(self.external_drives.keys()).copied().find(|id|next.get(id)!=self.external_drives.get(id)).unwrap_or(0);
            return Err(E::DivergentDrive(id));
        }
        self.external_drives=next;self.external_drives_staged=true;Ok(())
    }
    fn external_drive(&self,id:u64)->crate::external_mobility::ExternalDrive{
        if self.external_drive_consumed_epoch==Some(self.external.epoch){return crate::external_mobility::ExternalDrive{id,..Default::default()};}
        let mut d=self.external_drives.get(&id).copied().unwrap_or(crate::external_mobility::ExternalDrive{id,..Default::default()});
        let support=external_ground::drive(self,id);d.impulse+=support.impulse;d.torque+=support.torque;d
    }
    pub fn stage_external_predictors(&mut self,epoch:u64,values:&[crate::external_mobility::ExternalPredictor])->Result<(),crate::external_mobility::ExternalError>{
        use crate::external_mobility::ExternalError as E;
        self.external_predictors.clear();self.external_placements.clear();self.external_placement_diagnostics.clear();self.external_replay_steps.clear();self.external_failed_replays.clear();
        if epoch!=self.external.epoch{return Err(E::WrongEpoch(epoch));}
        let mut next=std::collections::BTreeMap::new();
        for &p in values {
            if !self.external.owners.contains_key(&p.id){return Err(E::MissingSnapshot(p.id));}
            if !p.pending_position.is_finite()||!p.pending_rotation.is_finite()||!p.pending_rotation.is_normalized(){return Err(E::InvalidPredictor(p.id));}
            if let Some(old)=next.insert(p.id,p){if old!=p{return Err(E::DivergentSnapshot(p.id));}}
        }
        for &id in self.external.owners.keys(){if !next.contains_key(&id){return Err(E::MissingPredictor(id));}}
        self.external_predictors=next;Ok(())
    }
    pub fn external_placements(&self)->&[crate::external_mobility::ExternalPlacement]{&self.external_placements}
    pub fn external_placement_diagnostics(&self)->&[crate::external_mobility::ExternalPlacementDiagnostic]{&self.external_placement_diagnostics}
    pub fn external_failed_placement_replays(&self)->&[crate::external_mobility::ExternalPlacementReplay]{&self.external_failed_replays}
    /// Error-only snapshot of vehicle placement admission. This replays the
    /// exact final proposal, not the full articulated constraint solve.
    pub fn external_placement_replays(&self,world:&Collision)->Vec<crate::external_mobility::ExternalPlacementReplay>{
        use crate::external_mobility::*;
        self.external_replay_steps.iter().filter_map(|&(owner,dt,delta,turn,requested,contacts)|{
            let predictor=*self.external_predictors.get(&owner.id)?;
            let geometry:Vec<_>=self.external_world_geometry().copied().filter(|o|o.owner==Some(owner.id)).collect();
            if geometry.iter().any(|o|o.filter.animated_ped()){return None;}
            let mut g=Group::default();g.stage_external(owner.epoch,&[owner]).ok()?;
            g.stage_external_predictors(owner.epoch,&[predictor]).ok()?;g.stage_external_geometry(&geometry).ok()?;
            let mut queries=vec![];
            let(placement,diagnostic)=external_world::placement_scene(&g,Some(world),None,Some(&mut queries),owner.id,delta,turn,requested,contacts,Default::default());
            Some(ExternalPlacementReplay{owner,predictor,drive:self.external_drives.get(&owner.id).copied().unwrap_or(ExternalDrive{id:owner.id,..Default::default()}),geometry,dt,delta,turn,requested,contacts,placement,diagnostic,queries,world_rows:vec![],environment_override:self.external_environment_geometry.iter().any(|o|o.owner==Some(owner.id))})
        }).collect()
    }
    pub fn replay_external_placement(r:&crate::external_mobility::ExternalPlacementReplay)->Result<(crate::external_mobility::ExternalPlacement,crate::external_mobility::ExternalPlacementDiagnostic),String>{
        if r.geometry.iter().any(|o|o.filter.animated_ped()){return Err("animated owner sweep replay requires its full collision context".into());}
        let mut g=Group::default();g.stage_external(r.owner.epoch,&[r.owner]).map_err(|e|e.to_string())?;
        g.stage_external_predictors(r.owner.epoch,&[r.predictor]).map_err(|e|e.to_string())?;
        g.stage_external_geometry(&r.geometry).map_err(|e|e.to_string())?;
        Ok(external_world::placement_scene(&g,None,Some(&r.queries),None,r.owner.id,r.delta,r.turn,r.requested,r.contacts,Default::default()))
    }
    pub fn external_results(&self)->&[crate::external_mobility::ExternalResult]{&self.external.results}
    pub fn external_error(&self)->Option<crate::external_mobility::ExternalError>{self.external_failure}
    pub fn external_contacts(&self)->&[crate::external_mobility::ExternalContact]{&self.external_contacts}
    pub fn stage_external_geometry(&mut self,obstacles:&[Obstacle])->Result<(),crate::external_mobility::ExternalError>{
        for o in obstacles {if let Some(id)=o.owner{if !self.external.owners.contains_key(&id){return Err(crate::external_mobility::ExternalError::MissingSnapshot(id));}}}
        self.external_geometry=obstacles.iter().filter(|o|o.owner.is_some()).copied().collect();Ok(())
    }
    /// An explicit environment collision envelope for native finite owners.
    /// Its provenance differs from articulated peer/render geometry. Omitting
    /// an owner retains its existing geometry; a present owner replaces all
    /// of that owner's environment parts, never its peer contact parts.
    pub fn stage_external_environment_geometry(&mut self,epoch:u64,obstacles:&[Obstacle])->Result<(),crate::external_mobility::ExternalError>{
        use crate::external_mobility::ExternalError as E;
        self.external_environment_geometry.clear();self.external_placements.clear();self.external_placement_diagnostics.clear();self.external_replay_steps.clear();self.external_failed_replays.clear();
        let check=(||{
        if epoch!=self.external.epoch{return Err(E::WrongEpoch(epoch));}
        let mut ids=std::collections::BTreeSet::new();
        for ob in obstacles{
            let Some(id)=ob.owner else{return Err(E::InvalidEnvironment(0))};
            if !self.external.owners.contains_key(&id){return Err(E::MissingSnapshot(id));}
            if !ids.insert(ob.id){return Err(E::DuplicateEnvironment(ob.id));}
            if ob.id<0||!ob.at.is_finite()||!ob.rot.is_finite()||!ob.rot.is_normalized()||!ob.centre.is_finite()||!ob.com.is_finite()||!ob.vel.is_finite()||!ob.spin.is_finite()||!ob.half.is_finite()||ob.half.min_element()<0.||
                ob.parts.iter().any(|p|!p.at.is_finite()||!p.m.is_finite()||!p.lo.is_finite()||!p.hi.is_finite()||p.lo.cmpgt(p.hi).any())||ob.last.is_some_and(|(p,q)|!p.is_finite()||!q.is_finite()||!q.is_normalized()){
                return Err(E::InvalidEnvironment(id));
            }
        }
        Ok(())})();
        self.external_environment_failure=check.err();
        if let Some(e)=self.external_environment_failure{return Err(e);}
        self.external_environment_geometry=obstacles.to_vec();Ok(())
    }
    fn external_world_geometry(&self)->impl Iterator<Item=&Obstacle>{
        self.external_environment_geometry.iter().chain(self.external_geometry.iter().filter(|o|!self.external_environment_geometry.iter().any(|e|e.owner==o.owner)))
    }
    pub fn contact_summary(&self)->[f32;4] {
        let mut pairs=std::collections::HashSet::new();
        let (mut points,mut depth,mut impulse)=(0,0f32,0f32);
        for (&(a,_,b,_),rows) in &self.held {
            for r in rows.iter().filter(|r|!r.off) {
                pairs.insert((a,b));points+=1;depth=depth.max(-r.gap);impulse+=r.imp.x.abs();
            }
        }
        [pairs.len() as f32,points as f32,depth,impulse]
    }
}

pub struct Member<'a> {
    pub doll: &'a mut Ragdoll,
    pub id: usize,
    pub build: &'a Build,
    pub drive: Drive<'a>,
    pub obstacles: &'a [Obstacle],
    pub others: &'a [Capsule],
}

#[derive(Clone, Copy)]
enum Src {
    Own(usize, usize),
    Cross((usize, usize, usize, usize), usize),
}

#[derive(Clone)]
struct Entry {
    node: usize,
    a: usize,
    b: usize,
    at: usize,
    src: Src,
}

impl Ragdoll {
    pub fn step_group(ms: &mut [Member], world: &Collision, dt: f32, group: &mut Group) {
        if !(dt>0.)||!dt.is_finite(){return;}
        if let Some(e)=group.external_environment_failure{group.external_failure=Some(e);return;}
        // A missing/stale finite participant must not silently become an
        // infinite-mass obstacle. Reject before advancing any body.
        group.external_failure=None;
        for m in ms.iter(){
            for id in m.obstacles.iter().filter_map(|o|o.owner).chain(m.doll.grip_target_owners.iter().map(|x|x.1)){
                if !group.external.owners.contains_key(&id){group.external_failure=Some(crate::external_mobility::ExternalError::MissingSnapshot(id));return;}
            }
        }
        group.external_replay_steps.clear();
        group.external_failed_replays.clear();
        group.profile_impulses=([0.;3],[0;5]);group.profile_ms=[0.;4];group.profile_solve_ms=[0.;8];group.profile_solve_cycles=[0;8];
        group.profile_pair_prepare_ms=0.;group.profile_pair_counts=[0;4];group.profile_shape=[0;3];
        let timer=group.profile_enabled.then(std::time::Instant::now);
        if let Err(e)=external_ground::begin(group,ms,world,dt){group.external_failure=Some(e);return;}
        if let Some(t)=timer{group.profile_ms[0]=t.elapsed().as_secs_f64()*1000.;}
        let n = ms.len();
        let mut st: Vec<Option<Stage>> = (0..n).map(|_| None).collect();
        Self::group_wake(ms,dt);
        let timer=group.profile_enabled.then(std::time::Instant::now);
        // Full articulated preparation is one independent 21-part character per
        // job. Batching three characters made groups of 2..5 entirely serial.
        par_each_grain(&mut ms.iter_mut().zip(st.iter_mut()).collect::<Vec<_>>(),1, |(m, s)| **s = m.doll.begin(m.build, world, dt, &m.drive, m.obstacles, m.others));
        if let Some(t)=timer{group.profile_ms[1]=t.elapsed().as_secs_f64()*1000.;}
        let timer=group.profile_enabled.then(std::time::Instant::now);
        Self::group_solve(ms, st, group,dt,world);
        if let Some(t)=timer{group.profile_ms[3]=t.elapsed().as_secs_f64()*1000.-group.profile_ms[2];}
        external_ground::finish(group);
        group.external_drive_consumed_epoch=Some(group.external.epoch);
    }

    fn group_boxes(ms: &[Member]) -> Vec<(Vec3, Vec3)> {
        let reach = |m: &Member| -> (Vec3, Vec3) {
            let d = &m.doll;
            let (lo, hi) = d.bodies.iter().zip(&m.build.parts).enumerate().fold((Vec3::INFINITY, Vec3::NEG_INFINITY), |(lo, hi), (k,(b, p))| {
                let r = Vec3::splat(bound_half(p).length() + MARGIN());
                // Group broadphase must contain the same swept trajectory as
                // pair_manifold, including explicit recovery pose transport.
                // Circumscribed spheres are conservative for all rotations.
                let old=d.contact_history.as_ref().and_then(|h|h.get(k)).map(|f|f.0-d.o)
                    .or_else(||d.seen.get(k).map(|f|f.0)).unwrap_or(b.x);
                (lo.min(b.x-r).min(old-r),hi.max(b.x+r).max(old+r))
            });
            (lo + d.o, hi + d.o)
        };
        ms.iter().map(reach).collect()
    }

    fn group_wake(ms: &mut [Member], dt: f32) {
        let n = ms.len();
        let boxes = Self::group_boxes(ms);
        let meet = |i: usize, k: usize| boxes[i].0.cmple(boxes[k].1).all() && boxes[k].0.cmple(boxes[i].1).all();
        // Rage wakes a sleeper from an actual pair, not from whole-body boxes: a body
        // writhing beside two corpses woke (and re-froze) both on every step.
        let touch = |i: usize, k: usize| {
            let (a, b) = (&ms[i], &ms[k]);
            a.doll.bodies.iter().zip(&a.build.parts).any(|(ba, pa)| {
                let (xa, ra) = (ba.x + a.doll.o, bound_half(pa).length() + MARGIN());
                b.doll.bodies.iter().zip(&b.build.parts).any(|(bb, pb)| {
                    let reach = ra + bound_half(pb).length() + MARGIN() + 2.0 * bb.v.length() * dt;
                    xa.distance_squared(bb.x + b.doll.o) <= reach * reach
                })
            })
        };
        let woken: Vec<usize> = (0..n).filter(|&i| ms[i].doll.asleep && (0..n).any(|k| i != k && !ms[k].doll.asleep && meet(i, k) && touch(i, k))).collect();
        for i in woken {
            ms[i].doll.wake();
        }
    }

    fn group_solve(ms: &mut [Member], mut st: Vec<Option<Stage>>, group: &mut Group,dt:f32,world:&Collision) {
        #[cfg(windows)] fn cycles()->u64{
            #[link(name="kernel32")] unsafe extern "system"{fn QueryThreadCycleTime(thread:*mut std::ffi::c_void,cycles:*mut u64)->i32;}
            let mut n=0;unsafe{QueryThreadCycleTime((-2isize) as *mut _,&mut n)};n
        }
        #[cfg(not(windows))] fn cycles()->u64{0}
        let mut last=group.profile_enabled.then(||(std::time::Instant::now(),cycles()));
        let mut lap=|i:usize,g:&mut Group|{if let Some((t,c))=last{
            let now=std::time::Instant::now();let end=cycles();
            g.profile_solve_ms[i]+=now.duration_since(t).as_secs_f64()*1000.;g.profile_solve_cycles[i]+=end.saturating_sub(c);last=Some((now,end));
        }};

        let n = ms.len();
        let boxes = Self::group_boxes(ms);
        let mut world_stats=std::collections::BTreeMap::<u64,external_world::WorldStats>::new();
        let meet = |i: usize, k: usize| boxes[i].0.cmple(boxes[k].1).all() && boxes[k].0.cmple(boxes[i].1).all();
        let owner_nodes:std::collections::BTreeMap<u64,usize>=group.external.owners.keys().enumerate().map(|(i,&id)|(id,n+i)).collect();
        let graph_len=n+owner_nodes.len();
        let mut joined: Vec<usize> = (0..graph_len).collect();
        let timer=group.profile_enabled.then(std::time::Instant::now);
        let mut owner_pairs:Vec<_>=external_pairs::collect(group,dt).into_iter().map(Some).collect();
        if let Some(t)=timer{group.profile_ms[2]=t.elapsed().as_secs_f64()*1000.;}
        fn root(j: &mut [usize], mut i: usize) -> usize {
            while j[i] != i {
                i = j[i];
            }
            i
        }
        let frame_of = |d: &Ragdoll, t: &[Mat3], a: usize| {
            let f = (d.bodies[a].x + d.o, t[a]);
            (f, d.seen.get(a).map_or(f, |s| (s.0 + d.o, s.1)))
        };
        let parts: Vec<Vec<((Vec3, Mat3), (Vec3, Mat3), (Vec3, Vec3))>> = (0..n).map(|i| if st[i].is_none() { vec![] } else {
            let d = &*ms[i].doll;
            let t = d.turns();
            (0..d.bodies.len()).map(|a| { let (f, s) = frame_of(d, &t, a); (f, s, swept_box(&ms[i].build.parts[a], f, s)) }).collect()
        }).collect();
        struct Job { i: usize, k: usize, cross: Vec<(usize, usize, Block, Block, Src)>, held: Vec<((usize, usize, usize, usize), Vec<Held>)>, joined: bool }
        let mut jobs = vec![];
        for i in 0..n {
            for k in i + 1..n {
                let (i, k) = if ms[i].id < ms[k].id { (i, k) } else { (k, i) };
                if st[i].is_some() && st[k].is_some() && meet(i, k) {
                    jobs.push(Job { i, k, cross: vec![], held: vec![], joined: false });
                }
            }
        }
        let kept = std::mem::take(&mut group.held);
        {
            let (ms, st, parts, kept) = (&*ms, &st, &parts, &kept);
            par_each(&mut jobs, |job| {
                let (i, k) = (job.i, job.k);
                let (Some(si), Some(sk)) = (&st[i], &st[k]) else { return };
                let (di, dk) = (&*ms[i].doll, &*ms[k].doll);
                for (a, &(fa, sa, ba)) in parts[i].iter().enumerate() {
                    for (c, &(fc, sc, bc)) in parts[k].iter().enumerate() {
                        if ba.0.distance_squared(bc.0) > (ba.1.length() + bc.1.length()).powi(2)
                            || !boxes_meet(ba.0, fa.1, ba.1, bc.0, fc.1, bc.1) {
                            continue;
                        }
                        let key = (ms[i].id, a, ms[k].id, c);
                        let mut h = kept.get(&key).cloned().unwrap_or_default();
                        let (pa, pc) = (&ms[i].build.parts[a], &ms[k].build.parts[c]);
                        Self::manifold(&mut h, (part_shape(pa), bound_half(pa), fa, sa), (part_shape(pc), bound_half(pc), fc, sc));
                        if h.is_empty() {
                            continue;
                        }
                        let off = (di.contact_off >> a | dk.contact_off >> c) & 1 != 0;
                        h.iter_mut().for_each(|r| r.off |= off);
                        for (x, r) in h.iter().enumerate().filter(|(_, r)| !r.off) {
                            let (at_a, at_c) = (fa.0 + fa.1 * r.local, fc.0 + fc.1 * r.ground);
                            let (nn, over) = (r.n, r.gap + ALLOW());
                            let rows = |d: &Ragdoll, s: &Stage, k: usize, at: Vec3, sign: f64| frame(nn).map(|t| {
                                let mut j = point_row(d.bodies.len(), s.x0, k, (at - d.o).as_dvec3(), t.as_dvec3());
                                j.iter_mut().for_each(|x| *x *= sign);
                                let imp: Vec<V6> = j.chunks(delirium_data::kv!(6 int)).map(|c| std::array::from_fn(|i| c[i])).collect();
                                let mj: Vec<f64> = s.artic.solve(&imp).into_iter().flatten().collect();
                                (j, mj)
                            });
                            let (ra, rc) = (rows(di, si, a, at_a, 1.0), rows(dk, sk, c, at_c, -1.0));
                            let dot = |x: &[f64], y: &[f64]| x.iter().zip(y).map(|(x, y)| x * y).sum::<f64>();
                            let k3 = DMat3::from_cols_array_2d(&std::array::from_fn(|p| std::array::from_fn(|q| dot(&ra[q].0, &ra[p].1) + dot(&rc[q].0, &rc[p].1))));
                            let fv = |t: usize| dot(&ra[t].0, &si.free) + dot(&rc[t].0, &sk.free);
                            let e = (PART_ELASTICITY() * PART_ELASTICITY()) as f64;
                            let to = DVec3::new((-over.max(0.0) / si.h) as f64 - fv(0) / (1.0 + e), -fv(1), -fv(2));
                            let both = |t: usize, m: bool| -> Vec<f64> { let (x, y) = if m { (&ra[t].1, &rc[t].1) } else { (&ra[t].0, &rc[t].0) }; x.iter().chain(y).copied().collect() };
                            let b = Block { scale: elastic(e), ..Block::with(std::array::from_fn(|t| both(t, false)), std::array::from_fn(|t| both(t, true)), k3, to,
                                (part_friction(pa) * part_friction(pc)) as f64, None, r.imp) };
                            let pb = Self::push_block(&b, over, si.h, r.pimp);
                            job.cross.push((i, k, b, pb, Src::Cross(key, x)));
                        }
                        job.joined |= h.iter().any(|r| !r.off);
                        job.held.push((key, h));
                    }
                }
            });
        }
        let mut held = std::collections::HashMap::new();
        let mut cross: Vec<Option<(usize, usize, Block, Block, Src)>> = vec![];
        for job in jobs {
            cross.extend(job.cross.into_iter().map(Some));
            held.extend(job.held);
            if job.joined {
                let (ri, rk) = (root(&mut joined, job.i), root(&mut joined, job.k));
                joined[ri.max(rk)] = ri.min(rk);
            }
        }
        group.held = held;lap(0,group);
        // Distinct characters touching different shapes of one car/ped still
        // exchange momentum through that single finite participant.
        for i in 0..n {if let Some(s)=&st[i]{for id in s.blocks.iter().filter_map(|b|b.external.map(|e|e.id)){
            let(a,b)=(root(&mut joined,i),root(&mut joined,owner_nodes[&id]));joined[a.max(b)]=a.min(b);
        }}}
        for pair in owner_pairs.iter().flatten(){let(a,b)=(root(&mut joined,owner_nodes[&pair.a]),root(&mut joined,owner_nodes[&pair.b]));joined[a.max(b)]=a.min(b);}
        let tops: Vec<usize> = (0..graph_len).map(|i| root(&mut joined, i)).collect();
        let alone: Vec<bool> = (0..n).map(|i| st[i].as_ref().is_some_and(|s|s.blocks.iter().all(|b|b.external.is_none())) && (0..n).filter(|&k| st[k].is_some() && tops[k] == tops[i]).count() == 1).collect();
        group.profile_shape[0]=alone.iter().filter(|a|**a).count();
        for (m, s) in ms.iter_mut().zip(st.iter_mut()).zip(&alone).filter(|x| *x.1).map(|x| x.0) {
            m.doll.finish(s.take().unwrap(), m.drive.lift);
        }
        lap(1,group);
        for top in 0..graph_len {
            let mut is: Vec<usize> = (0..n).filter(|&i| st[i].is_some() && tops[i] == top).collect();
            let owner_ids:std::collections::BTreeSet<u64>=owner_nodes.iter().filter(|(_,node)|tops[**node]==top).map(|(&id,_)|id).collect();
            if is.is_empty()&&owner_ids.is_empty() {
                continue;
            }
            group.profile_shape[1]+=1;group.profile_shape[2]=group.profile_shape[2].max(is.len());
            is.sort_by_key(|&i| ms[i].id);
            let mut off = vec![0; n];
            let mut total = 0;
            for &i in &is {
                off[i] = total;
                total += st[i].as_ref().unwrap().qd0.len();
            }
            let external_h=is.first().map_or(dt,|&i|st[i].as_ref().unwrap().h);
            let mut owner_offsets=std::collections::BTreeMap::new();
            for &id in &owner_ids {owner_offsets.insert(id,total);total+=6;}
            let alone_at = |at: usize| Spot { at: [at, 0], cut: usize::MAX };
            let lay_row = |r: Row, at: Spot, base: isize| Row { kind: match r.kind { RowKind::Friction { of, mu } => RowKind::Friction { of: of.wrapping_add_signed(base), mu }, k => k }, at, ..r };
            let mut firsts = vec![(0, 0); n];
            let mut counts = vec![(0, 0); n];
            let (mut rows, mut push): (Vec<Row>, Vec<Row>) = (vec![], vec![]);
            let (mut bank, mut push_bank): (Vec<Option<Block>>, Vec<Option<Block>>) = (vec![], vec![]);
            let mut list: Vec<Entry> = vec![];
            let mut nodes: Vec<(Option<usize>, Option<usize>)> = vec![];
            let owner_graph_base=ms.iter().map(|m|m.id).max().unwrap_or(0)+2;
            let owner_graph:std::collections::BTreeMap<u64,usize>=owner_ids.iter().enumerate().map(|(i,&id)|(id,owner_graph_base+i)).collect();
            let mut owner_nodes=std::collections::BTreeMap::<(usize,u64),usize>::new();
            for &i in &is {
                let s = st[i].as_mut().unwrap();
                let (rb, pbase) = (rows.len(), push.len());
                (firsts[i], counts[i]) = ((rb, pbase), (s.rows.len(), s.push.len()));
                rows.extend(std::mem::take(&mut s.rows).into_iter().map(|r| lay_row(r, alone_at(off[i]), rb as isize)));
                push.extend(std::mem::take(&mut s.push).into_iter().map(|r| lay_row(r, alone_at(off[i]), pbase as isize)));
                let s = &*s;
                let mut idx: Vec<usize> = (0..s.blocks.len()).collect();
                island_sort(&mut idx);
                let mut pre = vec![0; idx.len()];
                for (p, &o) in idx.iter().enumerate() {
                    pre[o] = p;
                }
                let n_links = ms[i].doll.bodies.len();
                let is_self = |p: usize| s.blocks[p].held.is_some_and(|(k, _)| k >= n_links);
                let (ground, own) = (nodes.len(), ms[i].id + 1);
                if pre.iter().any(|&p| !is_self(p)&&s.blocks[p].external.is_none()) {
                    nodes.push((Some(own), None));
                }
                let own_self = nodes.len();
                if pre.iter().any(|&p| is_self(p)) {
                    nodes.push((Some(own), Some(own)));
                }
                let base = bank.len();
                for &p in &pre {
                    let (node,other)=if let Some(e)=s.blocks[p].external {
                        let node=*owner_nodes.entry((own,e.id)).or_insert_with(||{let node=nodes.len();nodes.push((Some(own),Some(owner_graph[&e.id])));node});
                        (node,owner_graph[&e.id])
                    }else if is_self(p){(own_self,own)}else{(ground,0)};
                    list.push(Entry{node,a:own,b:other,at:base+p,src:Src::Own(i,p)});
                }
                let s = st[i].as_mut().unwrap();
                let velocity=std::mem::take(&mut s.blocks);let position=std::mem::take(&mut s.push_blocks);
                assert_eq!(velocity.len(),position.len(),"each contact must retain its position block");
                for(b,p)in velocity.into_iter().zip(position){
                    let(b,p)=if let Some(e)=b.external{
                        // Keep the independent path if a future controller
                        // supplies position coefficients for another owner.
                        if p.external==b.external{b.bind_external_pair(p,&group.external.owners[&e.id],off[i],owner_offsets[&e.id])}
                        else{(b.bind_external(&group.external.owners[&e.id],off[i],owner_offsets[&e.id],true),
                            if let Some(pe)=p.external{p.bind_external(&group.external.owners[&pe.id],off[i],owner_offsets[&pe.id],false)}else{Block{at:alone_at(off[i]),..p}})}
                    }else{(Block{at:alone_at(off[i]),..b},if let Some(e)=p.external{p.bind_external(&group.external.owners[&e.id],off[i],owner_offsets[&e.id],false)}else{Block{at:alone_at(off[i]),..p}})};
                    bank.push(Some(b));push_bank.push(Some(p));
                }
            }
            let mut pairs_across: Vec<((usize, usize), usize)> = vec![];
            for (i, k, b, pb, from) in cross.iter_mut().filter(|c| c.as_ref().is_some_and(|c| is.contains(&c.0))).map(|c| c.take().unwrap()) {
                let (i, k) = (&i, &k);
                let spot = Spot { at: [off[*i], off[*k]], cut: st[*i].as_ref().unwrap().qd0.len() };
                let node = match pairs_across.iter().find(|x| x.0 == (*i, *k)) {
                    Some(x) => x.1,
                    None => {
                        nodes.push((Some(ms[*i].id + 1), Some(ms[*k].id + 1)));
                        pairs_across.push(((*i, *k), nodes.len() - 1));
                        nodes.len() - 1
                    }
                };
                list.push(Entry { node, a: ms[*i].id + 1, b: ms[*k].id + 1, at: bank.len(), src: from });
                bank.push(Some(Block { at: spot, ..b }));
                push_bank.push(Some(Block { at: spot, ..pb }));
            }
            let at: Vec<usize> = island_order(&nodes).iter().enumerate().fold(vec![0; nodes.len()], |mut at, (r, &x)| { at[x] = r; at });
            list.sort_by_key(|x| at[x.node]);
            let mut pushed = list.clone();
            sgi_sort(&mut list, |x| x.a);
            sgi_sort(&mut pushed, |x| x.b);
            let mut blocks: Vec<Block> = list.iter().map(|x| bank[x.at].take().unwrap()).collect();
            let mut push_blocks: Vec<Block> = pushed.iter().map(|x| push_bank[x.at].take().unwrap()).collect();
            let mut qd: Vec<f64> = is.iter().flat_map(|&i| st[i].as_ref().unwrap().qd0.clone()).collect();
            for &id in &owner_ids {
                let owner=&group.external.owners[&id];let drive=group.external_drive(id);
                let v=owner.linear_inverse*drive.impulse;let w=owner.angular_inverse*drive.torque;
                let delta=[v.x as f64,v.y as f64,v.z as f64,w.x as f64,w.y as f64,w.z as f64];
                qd.extend(owner.qd().into_iter().zip(delta).map(|(v,d)|v+d));
            }
            let mut pair_rows=vec![];
            let mut direct_moves=std::collections::BTreeMap::<u64,Vec3>::new();
            for slot in &mut owner_pairs {
                if !slot.as_ref().is_some_and(|p|owner_ids.contains(&p.a)){continue;}
                let p=slot.take().unwrap();let index=rows.len();
                rows.push(Row{at:Spot{at:[owner_offsets[&p.a],owner_offsets[&p.b]],cut:6},..p.row});
                *direct_moves.entry(p.a).or_default()+=p.move_a;*direct_moves.entry(p.b).or_default()+=p.move_b;
                pair_rows.push((index,p.a,p.b,p.child_a,p.child_b,p.point_a,p.point_b,p.normal));
            }
            let mut ground_rows=vec![];
            for item in external_ground::rows(group,external_h){
                if !owner_ids.contains(&item.id){continue;}
                let index=rows.len();rows.push(Row{at:alone_at(owner_offsets[&item.id]),..item.row});
                ground_rows.push((index,item.id,item.child,item.point,item.normal,item.slot));
            }
            lap(2,group);
            let qd_start=qd.clone();
            if !owner_ids.is_empty(){for b in &mut blocks{b.physical_impulse(&qd_start);}}
            let mut world_rows:Vec<(usize,external_world::WorldContact)>=vec![];
            let non_world_rows=rows.len();
            // Discover contacts along the correction proposed by the SAME
            // coupled island. New impulses may bring a previously distant
            // chassis face to a wall; query that whole path before committing.
            for pass in 0..4 {
                let proposals:std::collections::BTreeMap<_,_>=owner_ids.iter().map(|&id|{
                    let o=owner_offsets[&id];let old=&group.external.owners[&id];
                    let v=Vec3::new(qd[o] as f32,qd[o+1] as f32,qd[o+2] as f32)-old.velocity;
                    let w=Vec3::new(qd[o+3] as f32,qd[o+4] as f32,qd[o+5] as f32)-old.spin;
                    (id,(v*external_h,w*external_h))
                }).collect();
                let retained:Vec<_>=world_rows.iter().map(|(_,c)|*c).collect();
                let contacts:Vec<_>=external_world::collect_retained(group,world,&proposals,&mut world_stats,&retained).into_iter().filter(|c|owner_ids.contains(&c.owner)).collect();
                if pass>0&&contacts.iter().eq(world_rows.iter().map(|(_,c)|c)){break;}
                // A repeated witness is not an unchanged constraint: its gap
                // belongs to the latest proposed transform, especially after
                // an authored edge cone changes the reaction normal. Replace
                // the previous linearization instead of discarding updates.
                rows.truncate(non_world_rows);world_rows.clear();
                for c in contacts{
                    let index=rows.len();rows.push(Row{at:alone_at(owner_offsets[&c.owner]),..c.row(&group.external.owners[&c.owner],external_h,false)});
                    world_rows.push((index,c));
                }
                if pass>0{
                    qd.clone_from(&qd_start);for row in &mut rows{row.lambda=0.;}for b in &mut blocks{b.lambda=DVec3::ZERO;}
                }
                impulses_observed(&mut rows,&mut blocks,&mut qd,group.profile_enabled.then_some(&mut group.profile_impulses));
            }
            lap(3,group);
            let mut owner_impulses:std::collections::BTreeMap<u64,(Vec3,Vec3)>=owner_ids.iter().map(|&id|{let d=group.external_drive(id);(id,(d.impulse,d.torque))}).collect();
            for b in &blocks {if let Some(e)=b.external{
                let l=(b.lambda*b.scale).as_vec3();
                let j=-(e.basis[0]*l.x+e.basis[1]*l.y+e.basis[2]*l.z);
                let t=(e.point-group.external.owners[&e.id].com).cross(j);
                let value=owner_impulses.entry(e.id).or_default();value.0+=j;value.1+=t;
            }}
            for &(index,a,b,child_a,child_b,point_a,point_b,normal) in &pair_rows {
                let j=normal*rows[index].lambda as f32;
                if rows[index].lambda<=0.{continue;}
                let oa=&group.external.owners[&a];let ob=&group.external.owners[&b];
                let prior=|owner:&crate::external_mobility::ExternalOwner,point:Vec3|{
                    let d=group.external_drive(owner.id);owner.point_velocity(point)+owner.linear_inverse*d.impulse+(owner.angular_inverse*d.torque).cross(point-owner.com)
                };
                group.external_contacts.push(crate::external_mobility::ExternalContact{a,b,impulse:j,point:point_a,normal,child_a,child_b,
                    velocity_a:prior(oa,point_a),velocity_b:prior(ob,point_b)});
                let value=owner_impulses.entry(a).or_default();value.0+=j;value.1+=(point_a-oa.com).cross(j);
                let value=owner_impulses.entry(b).or_default();value.0-=j;value.1-=(point_b-ob.com).cross(j);
            }
            for &(index,id,child,point,normal,slot) in &ground_rows{
                let j=normal*rows[index].lambda as f32;
                let value=owner_impulses.entry(id).or_default();value.0+=j;value.1+=(point-group.external.owners[&id].com).cross(j);
                external_ground::set_push(group,id,child,slot,rows[index].lambda as f32);
            }
            for (index,c) in &world_rows {
                let j=c.normal*rows[*index].lambda as f32;if j==Vec3::ZERO{continue;}
                let value=owner_impulses.entry(c.owner).or_default();
                // Same generalized angular row as the finite-rotation world
                // constraint; do not commit a different zero-origin torque.
                value.0+=j;value.1+=c.angular*rows[*index].lambda as f32;
            }
            let part = |b: &Block, first: bool| {
                let side = |j: &Vec<f64>| if first { j[..b.at.cut].to_vec() } else { j[b.at.cut..].to_vec() };
                let j=b.j.each_ref().map(side);let nonzero=Block::nonzero_indices(&j);
                Block { j: Arc::new(j), mj: Arc::new(b.mj.each_ref().map(side)),nonzero, held: None, at: Spot::OWN, ..b.clone() }
            };
            let mut own: Vec<Vec<Option<Block>>> = (0..n).map(|_| vec![]).collect();
            let mut theirs: Vec<Vec<Block>> = (0..n).map(|_| vec![]).collect();
            for (x, b) in list.iter().zip(blocks) {
                match x.src {
                    Src::Own(i, p) => {
                        if own[i].len() <= p {
                            own[i].resize_with(p + 1, || None);
                        }
                        own[i][p] = Some(b.own_external_side());
                    }
                    Src::Cross(key, at) => {
                        group.held.get_mut(&key).unwrap()[at].imp = (b.lambda/b.warm_divisor).as_vec3();
                        let (i, k) = is.iter().fold((usize::MAX, usize::MAX), |acc, &m| if ms[m].id == key.0 { (m, acc.1) } else if ms[m].id == key.2 { (acc.0, m) } else { acc });
                        for (m, first) in [(i, true), (k, false)] {
                            theirs[m].push(part(&b, first));
                        }
                    }
                }
            }
            let mut rows = rows.into_iter();
            for &i in &is {
                let s = st[i].as_mut().unwrap();
                s.rows = rows.by_ref().take(counts[i].0).map(|r| lay_row(r, Spot::OWN, -(firsts[i].0 as isize))).collect();
                s.blocks = std::mem::take(&mut own[i]).into_iter().map(|b| b.unwrap()).chain(std::mem::take(&mut theirs[i])).collect();
            }
            let mut go = vec![false; n];
            for &i in &is {
                go[i] = ms[i].doll.middle(st[i].as_mut().unwrap());
            }
            let mut qp = vec![0.0; total];
            for &id in &owner_ids{
                let o=owner_offsets[&id];
                // Same timing as doll.begin()/middle()/end() and TU8 rigid
                // collider82972440 ->82972650 -> solve ->829726A0: the host
                // already integrated its incoming twist. New contact momenta
                // change the NEXT prediction, not the elapsed interval. Only
                // positional push enters this shared split-position solve.
                let v=direct_moves.get(&id).copied().unwrap_or_default()/external_h;
                qp[o..o+6].copy_from_slice(&[v.x as f64,v.y as f64,v.z as f64,0.,0.,0.]);
            }
            lap(4,group);
            let mut requested_qp=qp.clone();
            let mut preview_push=push.clone();let mut preview_push_blocks=push_blocks.clone();
            impulses_observed(&mut preview_push,&mut preview_push_blocks,&mut requested_qp,group.profile_enabled.then_some(&mut group.profile_impulses));
            let push_was: Vec<f64> = push.iter().map(|r| r.lambda).collect();
            let position_start=qp.clone();let source_push=push.clone();let source_push_blocks=push_blocks.clone();
            let mut proposed=requested_qp.clone();let mut position_contacts=vec![];
            let continue_position=!(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_WORLD_POSITION_RESTART_BASELINE"));
            for pass in 0..4 {
                let proposals:std::collections::BTreeMap<_,_>=owner_ids.iter().map(|&id|{
                    let o=owner_offsets[&id];(id,(Vec3::new(proposed[o] as f32,proposed[o+1] as f32,proposed[o+2] as f32)*external_h,
                        Vec3::new(proposed[o+3] as f32,proposed[o+4] as f32,proposed[o+5] as f32)*external_h))
                }).collect();
                let contacts=external_world::collect_retained(group,world,&proposals,&mut world_stats,if continue_position{&position_contacts}else{&[]});
                if pass>0&&contacts==position_contacts&&(!continue_position||contacts.is_empty()){break;}
                // The preview solved these EXACT pristine rows/blocks from
                // position_start. With no world rows, resolving them is an
                // identical duplicate. Reuse all solved lambdas as well as qp;
                // never skip the world query, sweeps, or a contact-bearing solve.
                #[cfg(test)] let reuse=POSITION_PREVIEW_REUSE.with(|v|v.get());
                #[cfg(not(test))] let reuse=!(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_DUPLICATE_POSITION_SOLVE_BASELINE"));
                if pass==0&&contacts.is_empty()&&reuse{
                    qp.clone_from(&requested_qp);push=std::mem::take(&mut preview_push);push_blocks=std::mem::take(&mut preview_push_blocks);break;
                }
                let mut warm:Vec<_>=position_contacts.iter().copied().zip(push[source_push.len()..].iter().map(|r|Some(r.lambda))).collect();
                qp.clone_from(&position_start);
                if pass==0||!continue_position{push.clone_from(&source_push);push_blocks.clone_from(&source_push_blocks);}else{
                    // Re-linearization changes world rows, not the physical
                    // island or its accumulated contact solution. Resetting
                    // all lambdas threw away each preceding eight-sweep solve.
                    // Reapply the retained warm solution once to position_start.
                    push.truncate(source_push.len());
                }
                position_contacts=contacts;
                for c in &position_contacts{
                    let lambda=if continue_position{external_world::take_position_warm(c,&mut warm)}else{0.};
                    push.push(Row{at:alone_at(owner_offsets[&c.owner]),lambda,..c.row(&group.external.owners[&c.owner],external_h,true)});
                }
                impulses_observed(&mut push,&mut push_blocks,&mut qp,group.profile_enabled.then_some(&mut group.profile_impulses));
                proposed.clone_from(&qp);
                if position_contacts.is_empty(){break;}
            }
            lap(5,group);
            let mut own: Vec<Vec<Option<Block>>> = (0..n).map(|_| vec![]).collect();
            for (x, b) in pushed.iter().zip(push_blocks) {
                match x.src {
                    Src::Own(i, p) => {
                        if own[i].len() <= p {
                            own[i].resize_with(p + 1, || None);
                        }
                        own[i][p] = Some(b.own_external_side());
                    }
                    Src::Cross(key, at) => group.held.get_mut(&key).unwrap()[at].pimp = b.lambda.as_vec3(),
                }
            }
            let mut push = push.into_iter();
            for &i in &is {
                let s = st[i].as_mut().unwrap();
                let was = &push_was[firsts[i].1..];
                s.push = push.by_ref().take(counts[i].1).zip(was).map(|(r, &lambda)| Row { lambda, ..lay_row(r, Spot::OWN, -(firsts[i].1 as isize)) }).collect();
                s.push_blocks = std::mem::take(&mut own[i]).into_iter().map(|b| b.unwrap()).collect();
            }
            for &i in &is {
                let s = st[i].take().unwrap();
                if go[i] {
                    let len = s.qd0.len();
                    ms[i].doll.end(s, &qp[off[i]..off[i] + len], ms[i].drive.lift);
                }
            }
            lap(6,group);
            for &id in &owner_ids {
                let o=owner_offsets[&id];let h=external_h;
                let total_position=Vec3::new(qp[o] as f32,qp[o+1] as f32,qp[o+2] as f32)*h;
                let total_orientation=Vec3::new(qp[o+3] as f32,qp[o+4] as f32,qp[o+5] as f32)*h;
                let position=total_position;let orientation=total_orientation;
                if let Some(p)=group.external_predictors.get(&id){
                    let requested=p.pending_position+Vec3::new(requested_qp[o] as f32,requested_qp[o+1] as f32,requested_qp[o+2] as f32)*h;
                    let contacts=position_contacts.iter().filter(|c|c.owner==id).count() as u32;
                    group.external_replay_steps.push((group.external.owners[&id],h,total_position,total_orientation,requested,contacts));
                    let predictor=*p;let mut queries=vec![];
                    let(placement,diagnostic)=external_world::placement_scene(group,Some(world),None,Some(&mut queries),id,total_position,total_orientation,requested,contacts,world_stats.get(&id).copied().unwrap_or_default());
                    if !placement.valid{
                        use crate::external_mobility::{ExternalPlacementReplay,ExternalWorldRowReplay,ExternalDrive};
                        group.external_failed_replays.push(ExternalPlacementReplay{owner:group.external.owners[&id],predictor,
                            drive:group.external_drives.get(&id).copied().unwrap_or(ExternalDrive{id,..Default::default()}),
                            geometry:group.external_world_geometry().copied().filter(|o|o.owner==Some(id)).collect(),dt:h,delta:total_position,turn:total_orientation,requested,contacts,placement,diagnostic:world_stats.get(&id).copied().unwrap_or_default().admission_only(diagnostic),queries,
                            world_rows:position_contacts.iter().filter(|c|c.owner==id).map(|c|ExternalWorldRowReplay{point:c.point,normal:c.normal,angular:Some(c.angular),gap:c.gap,native_gap:c.native_gap}).collect(),environment_override:group.external_environment_geometry.iter().any(|o|o.owner==Some(id))});
                    }
                    group.external_placements.push(placement);
                    group.external_placement_diagnostics.push(diagnostic);
                }
                let(j,t)=owner_impulses.get(&id).copied().unwrap_or_default();
                group.external.commit(id,j,t,position,orientation);
            }
            lap(7,group);
        }
    }
}

fn point_row(n_links: usize, xr: DVec3, k: usize, at: DVec3, n: DVec3) -> Vec<f64> {
    let mut r = vec![0.0; delirium_data::kv!(6 int) * n_links];
    r[delirium_data::kv!(6 int) * k..delirium_data::kv!(6 int) * k + delirium_data::kv!(6 int)].copy_from_slice(&v6(n, (at - xr).cross(n)));
    r
}

fn spatial(bodies: &[Body], x0: DVec3) -> Vec<f64> {
    spatial_of(&bodies.iter().map(|b| (b.v, b.w)).collect::<Vec<_>>(), bodies, x0)
}

fn spatial_of(vw: &[(Vec3, Vec3)], bodies: &[Body], x0: DVec3) -> Vec<f64> {
    vw.iter().zip(bodies).flat_map(|(&(v, w), b)| {
        let w = w.as_dvec3();
        v6(v.as_dvec3() - w.cross(b.x.as_dvec3() - x0), w)
    }).collect()
}

pub fn pose_frames(parents: &[Option<usize>], pose: &[(Vec3, Quat)], root: Transform) -> Vec<(Vec3, Quat)> {
    let mut out: Vec<(Vec3, Quat)> = Vec::with_capacity(pose.len());
    for (i, &(t, r)) in pose.iter().enumerate() {
        let (pt, pr) = parents[i].map_or((root.translation, root.rotation), |p| out[p]);
        out.push((pt + pr * t, pr * r));
    }
    out
}

pub struct Rig {
    pub build: Build,
    part_of: Vec<Option<usize>>,
    parents: Vec<Option<usize>>,
}

impl Rig {
    pub fn new(parts: &[delirium_data::ragdoll::Part], parents: &[Option<usize>], bind: &[(Vec3, Quat)], bones: &[delirium_data::skeleton::Bone], nm: usize) -> Option<Rig> {
        if parts.is_empty() {
            return None;
        }
        let model: Vec<Transform> = pose_frames(parents, bind, Transform::IDENTITY).into_iter()
            .map(|(t, r)| Transform::from_translation(t).with_rotation(r)).collect();
        let build = Build::new(parts, &model, bones, nm);
        let part_of = (0..bind.len()).map(|b| build.parts.iter().position(|d| d.bone == b)).collect();
        Some(Rig { build, part_of, parents: parents.to_vec() })
    }

    pub fn place(&self, pose: &[(Vec3, Quat)], body: Transform) -> Vec<Transform> {
        let bones = pose_frames(&self.parents, pose, body);
        self.build.parts.iter().map(|d| {
            let (bt, br) = bones[d.bone];
            Transform::from_translation(bt + br * d.offset.translation).with_rotation(br * d.offset.rotation)
        }).collect()
    }

    pub fn start(&self, pose: &[(Vec3, Quat)], body: Transform, prev: Option<(&[(Vec3, Quat)], Transform, f32)>, v_ref: Vec3) -> Ragdoll {
        let now = self.place(pose, body);
        let mut frames = now.clone();
        for c in 0..frames.len() {
            if let Some(p) = self.build.parts[c].parent {
                let pivot = frames[p].translation + frames[p].rotation * self.build.parts[c].anchor_parent;
                frames[c].translation = pivot - frames[c].rotation * self.build.parts[c].anchor;
            }
        }
        let mut doll = Ragdoll::new(&self.build, &frames, Vec3::ZERO);
        if let Some((pp, pb, dt)) = prev.filter(|p| p.2 > 0.0) {
            let rows = |t: &Transform| { let m = Mat3::from_quat(t.rotation); [m.x_axis, m.y_axis, m.z_axis, t.translation] };
            for ((b, c), p) in doll.bodies.iter_mut().zip(&now).zip(self.place(pp, pb)) {
                (_, b.v, b.w) = crate::ragstart::start_part(rows(c), Some(&rows(&p)), Vec3::ZERO, dt, [0.5, delirium_data::kv!(5), 2.0], v_ref);
            }
        }
        let off = -crate::rigid::row_mul(crate::nmrs::cg(doll.nm), &doll.inst());
        doll.bodies.iter_mut().for_each(|b| b.x += off);
        doll
    }

    pub fn stand(&mut self, idle: &[(Vec3, Quat)]) {
        let mut frames = self.place(idle, Transform::from_rotation(Quat::from_rotation_z(-std::f32::consts::FRAC_PI_2)));
        for (p, f) in self.build.parts.iter_mut().zip(&frames) {
            p.bind = f.rotation;
        }
        crate::nmrs::asset_layout(&mut self.build, &mut frames);
    }

    pub fn derive_links(&self, idle: &[(Vec3, Quat)]) -> bool {
        let nm = self.build.nm;
        if (nm == crate::nmrs::HOMER && crate::nmdbm::logged_links().is_some()) || crate::nmdbm::DERIVED_LINKS[nm].get().is_some() {
            return false;
        }
        let stand: Vec<Quat> = self.place(idle, Transform::from_rotation(Quat::from_rotation_z(-std::f32::consts::FRAC_PI_2))).iter().map(|f| f.rotation).collect();
        crate::nmrs::derive_links(&self.build, &stand).is_some_and(|l| crate::nmdbm::DERIVED_LINKS[nm].set(l).is_ok())
    }

    pub fn dump(&self, pose: &[(Vec3, Quat)], body: Transform) -> String {
        self.build.dump(&self.place(pose, body))
    }

    pub fn rise(&self, doll: &Ragdoll, firsts: &[Vec<(Vec3, Quat)>]) -> (usize, f32, Vec3) {
        let now: Vec<Transform> = doll.frames().collect();
        let spine = self.build.parts.iter().position(|d| d.parent == Some(0) && !d.lower).unwrap_or(0);
        let yaw = |q: Quat| { let a = q * Vec3::Y; a.y.atan2(a.x) };
        firsts.iter().enumerate().map(|(k, pose)| {
            let first = self.place(pose, Transform::IDENTITY);
            let h = yaw(now[spine].rotation) - yaw(first[spine].rotation);
            let turn = Quat::from_rotation_z(h);
            ((turn * first[spine].rotation).angle_between(now[spine].rotation), k, h, now[0].translation - turn * first[0].translation)
        }).min_by(|a, b| a.0.total_cmp(&b.0)).map(|(_, k, h, at)| (k, h, at)).expect("a get-up clip")
    }

    pub fn settle(&self, heading: f32, end: &[(Vec3, Quat)], end_turn: Quat, next: &[(Vec3, Quat)]) -> (Vec3, f32) {
        let (a, _, _) = end_turn.to_euler(EulerRot::ZXY);
        let pelvis = |pose: &[(Vec3, Quat)]| self.place(pose, Transform::IDENTITY)[0].translation;
        let moved = Quat::from_rotation_z(heading) * pelvis(end) - Quat::from_rotation_z(heading + a) * pelvis(next);
        (moved.with_z(0.0), a)
    }

    pub fn bones(&self, doll: &Ragdoll, pose: &[(Vec3, Quat)], facing: Quat) -> (Transform, Vec<(Vec3, Quat)>) {
        let frames: Vec<Transform> = doll.frames().collect();
        let bone_at = |k: usize| {
            let (f, o) = (frames[k], self.build.parts[k].offset);
            let r = f.rotation * o.rotation.inverse();
            (f.translation - r * o.translation, r)
        };
        let (pt, pr) = bone_at(0);
        let (lt, lr) = pose[self.build.parts[0].bone];
        let root = (pt - (pr * lr.inverse()) * lt, pr * lr.inverse());
        let mut placed: Vec<(Vec3, Quat)> = Vec::with_capacity(pose.len());
        for (b, &(lt, lr)) in pose.iter().enumerate() {
            placed.push(match (self.part_of[b], self.parents[b]) {
                (Some(k), _) => bone_at(k),
                (None, Some(q)) => (placed[q].0 + placed[q].1 * lt, placed[q].1 * lr),
                (None, None) => root,
            });
        }
        let body = Transform::from_translation(frames[0].translation).with_rotation(facing);
        let local = (0..pose.len()).map(|b| {
            let (pt, pr) = self.parents[b].map_or((body.translation, body.rotation), |q| placed[q]);
            (pr.inverse() * (placed[b].0 - pt), pr.inverse() * placed[b].1)
        }).collect();
        (body, local)
    }
}

pub(crate) fn island_order(nodes: &[(Option<usize>, Option<usize>)]) -> Vec<usize> {
    struct Forest {
        parent: Vec<Option<usize>>,
        rank: Vec<u32>,
        kids: Vec<Vec<usize>>,
        roots: Vec<usize>,
    }
    impl Forest {
        fn hang(&mut self, x: usize, p: usize) {
            if self.parent[x] == Some(p) {
                return;
            }
            match self.parent[x] {
                Some(q) => self.kids[q].retain(|&c| c != x),
                None => self.roots.retain(|&c| c != x),
            }
            self.parent[x] = Some(p);
            self.kids[p].push(x);
        }
        fn find(&mut self, x: usize) -> usize {
            let mut r = x;
            while let Some(p) = self.parent[r] {
                r = p;
            }
            let mut c = x;
            while c != r {
                let next = self.parent[c].unwrap();
                self.hang(c, r);
                c = next;
            }
            r
        }
        fn join(&mut self, x: usize, y: usize) -> usize {
            if x == y {
                x
            } else if self.rank[x] > self.rank[y] {
                self.hang(y, x);
                x
            } else if self.rank[x] < self.rank[y] {
                self.hang(x, y);
                y
            } else {
                self.hang(y, x);
                self.rank[x] += 1;
                x
            }
        }
        fn walk(&self, x: usize, out: &mut Vec<usize>) {
            out.push(x);
            for &k in &self.kids[x] {
                self.walk(k, out);
            }
        }
    }
    let n = nodes.len();
    let mut f = Forest { parent: vec![None; n], rank: vec![0; n], kids: vec![vec![]; n], roots: vec![] };
    let mut last = std::collections::HashMap::new();
    for (m, &(a, b)) in nodes.iter().enumerate() {
        f.roots.push(m);
        let mut cur = m;
        if let Some(&l) = a.and_then(|a| last.get(&a)) {
            let x = f.find(l);
            cur = f.join(x, cur);
        }
        if let Some(b) = b.filter(|&b| Some(b) != a) {
            if let Some(&l) = last.get(&b) {
                let x = f.find(l);
                cur = f.join(x, cur);
            }
            last.insert(b, cur);
        }
        if let Some(a) = a {
            last.insert(a, cur);
        }
    }
    let mut out = vec![];
    for &r in &f.roots.clone() {
        f.walk(r, &mut out);
    }
    out
}

fn island_sort<T>(v: &mut [T]) {
    sgi_sort(v, |_| 0);
}

fn sgi_sort<T>(v: &mut [T], key: impl Fn(&T) -> usize + Copy) {
    fn split<T>(mut v: &mut [T], key: impl Fn(&T) -> usize + Copy) {
        while v.len() > delirium_data::kv!(51 int) {
            let (a, b, c) = (key(&v[0]), key(&v[v.len() / 2]), key(&v[v.len() - 1]));
            let pivot = if a < b { if b < c { b } else if a < c { c } else { a } } else if a < c { a } else if b < c { c } else { b };
            let (mut f, mut l) = (0, v.len());
            loop {
                while key(&v[f]) < pivot {
                    f += 1;
                }
                l -= 1;
                while pivot < key(&v[l]) {
                    l -= 1;
                }
                if f >= l {
                    break;
                }
                v.swap(f, l);
                f += 1;
            }
            let (lo, hi) = std::mem::take(&mut v).split_at_mut(f);
            split(hi, key);
            v = lo;
        }
    }
    split(v, key);
    for i in 1..v.len() {
        let mut j = i;
        while j > 0 && key(&v[j]) < key(&v[j - 1]) {
            v.swap(j, j - 1);
            j -= 1;
        }
    }
}

#[test]
fn island_sort_as_the_game() {
    moving_obstacle_regression::constants();
    let sorted = |n: usize| { let mut v: Vec<usize> = (0..n).collect(); island_sort(&mut v); v };
    assert_eq!(sorted(delirium_data::kv!(51 int)), (0..delirium_data::kv!(51 int)).collect::<Vec<_>>());
    assert_eq!(sorted(delirium_data::kv!(454 int)), (0..delirium_data::kv!(454 int)).rev().collect::<Vec<_>>());
    assert_eq!(sorted(delirium_data::kv!(91 int)), (delirium_data::kv!(118 int)..delirium_data::kv!(91 int)).chain(0..delirium_data::kv!(118 int)).collect::<Vec<_>>());
}

#[cfg(test)]
mod moving_obstacle_regression {
    use super::*;
    pub(super) fn constants() {
        static INIT: std::sync::Once = std::sync::Once::new();
        INIT.call_once(|| {
            let root = std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
            let bytes = std::fs::read(root.join("fred.xrt")).unwrap();
            assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
        });
    }
    fn isolated_grip_body(velocity: Vec3) -> (Build, Ragdoll) {
        constants();
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        let (source,_)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
        let mut part=source.parts[16].clone();part.parent=None;part.bone=0;
        let build=Build{parts:vec![part],nm:source.nm};
        let doll=Ragdoll::new(&build,&[Transform::from_translation(Vec3::Z*15.)],velocity);
        (build,doll)
    }
    #[test]
    fn moving_grip_preserves_comoving_velocity_and_released_inertia() {
        // A Galilean translation must not turn a coincident, co-moving hand
        // into a world-fixed pin. Exercise both a zero and nonzero gap.
        for velocity in [Vec3::X*0.5,Vec3::Y*4.,Vec3::new(-3.,2.,0.)] {
            for gap in [Vec3::ZERO,Vec3::Y*0.0002] {
                let (build,mut doll)=isolated_grip_body(velocity);
                let initial=doll.grip_world(0,Vec3::ZERO);
                doll.grips.push((0,Vec3::ZERO,initial+gap,0.,100.));
                doll.grip_target_velocities.push((0,velocity));
                // Include gravity: an exactly coincident pin must still
                // constrain its velocity, rather than skipping the row.
                let world=Collision::default();let drive=Drive::default();
                let dt=1./120.;
                for step in 0..24 {
                    // begin() predicts body positions before it builds the
                    // contacts. The host supplies the obstacle at that same
                    // END-of-substep timestamp, not the preceding one.
                    let target=initial+gap+velocity*((step+1) as f32*dt);
                    doll.grips[0].2=target;
                    if step==0&&gap==Vec3::ZERO {
                        let predicted=doll.grip_world(0,Vec3::ZERO)+doll.motion(0).0*dt;
                        assert!(predicted.distance(target)<1e-6,"fixture must reach the zero-gap velocity branch");
                    }
                    doll.step(&build,&world,dt,drive,&[],&[]);
                    assert!(doll.snapped.is_empty());
                    let actual=doll.motion(0).0;
                    assert!((actual-velocity).length()<0.02,"co-moving pin lost inertia: {actual:?} vs {velocity:?}");
                    assert!(doll.grip_world(0,Vec3::ZERO).distance(target)<0.001,
                        "co-moving material point missed end-of-step anchor: {:?} vs {target:?}",doll.grip_world(0,Vec3::ZERO));
                }
                let before=doll.motion(0).0;
                let frames=doll.frames().collect::<Vec<_>>();
                let mut free=Ragdoll::new(&build,&frames,before);
                free.set_body(0,frames[0].translation,frames[0].rotation,before,doll.motion(0).1);
                doll.grips.clear();doll.grip_target_velocities.clear();
                assert_eq!(doll.motion(0).0,before,"release itself must not rewrite velocity");
                for _ in 0..12 {for body in [&mut doll,&mut free] {body.step(&build,&world,dt,drive,&[],&[]);}}
                assert!((doll.motion(0).0-free.motion(0).0).length()<0.001,
                    "release must continue under the same forces as a free body: {:?} vs {:?}",doll.motion(0),free.motion(0));
                assert!(doll.frames().next().unwrap().translation.distance(free.frames().next().unwrap().translation)<0.001);
            }
        }
    }
    #[test]
    fn moving_anchor_transfers_velocity_through_constraint_without_position_kick() {
        let velocity=Vec3::X*0.5;
        let (build,mut moving)=isolated_grip_body(Vec3::ZERO);
        let (_,mut fixed)=isolated_grip_body(Vec3::ZERO);
        let initial=moving.grip_world(0,Vec3::ZERO);
        for doll in [&mut moving,&mut fixed] {doll.grips.push((0,Vec3::ZERO,initial+Vec3::Y*0.1,0.,100.));}
        moving.grip_target_velocities.push((0,velocity));
        let world=Collision::default();let drive=Drive{lift:1.,..Default::default()};
        for doll in [&mut moving,&mut fixed] {doll.step(&build,&world,1./120.,drive,&[],&[]);}
        assert!((moving.motion(0).0-fixed.motion(0).0-velocity).length()<0.02,
            "constraint did not transfer measured anchor velocity: {:?} vs {:?}",moving.motion(0),fixed.motion(0));
        assert!(fixed.motion(0).0.length()<0.02,"position repair injected launch velocity");
        assert!(moving.snapped.is_empty());
    }
    #[test]
    fn held_point_round_trips_through_rotated_hand_component() {
        constants();
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        let (build,mut frames)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
        for angle in [0.,0.7,2.1] {
            frames[16].rotation=Quat::from_rotation_z(angle)*Quat::from_rotation_y(0.4);
            let mut doll=Ragdoll::new(&build,&frames,Vec3::ZERO);
            // Exercise a collision component distinct from its render bone.
            doll.bodies[16].att=[Vec3::Y,-Vec3::X,Vec3::Z];
            let point=frames[16].translation+Vec3::new(0.03,-0.02,0.01);
            let local=doll.grip_local(16,point);
            assert!(doll.grip_world(16,local).distance(point)<1e-5);
            assert!((frames[16].translation+frames[16].rotation*local).distance(point)>0.01,
                "fixture must detect the former joint/bone-frame mixup");
        }
    }
    #[test]
    fn thin_car_child_crosses_a_stationary_limb_between_endpoints() {
        constants();
        // 25 m/s at 60 Hz: both endpoints clear the limb, but the surface
        // crosses it. The old current-child broadphase incorrectly rejected it.
        let a = (Vec3::ZERO, Mat3::IDENTITY);
        let ah = Vec3::new(0.04, 0.15, 0.04);
        let half = Vec3::new(0.01, 0.5, 0.5);
        let before = (Vec3::new(-25. / 120., 0., 0.), Mat3::IDENTITY);
        let now = (Vec3::new(25. / 120., 0., 0.), Mat3::IDENTITY);
        let sa = swept(a, a, ah);
        assert!(!moving_child_meets(a, sa, now, now, Vec3::ZERO, half));
        assert!(!moving_child_meets(a, sa, before, before, Vec3::ZERO, half));
        assert!(moving_child_meets(a, sa, now, before, Vec3::ZERO, half));
        let mut held = vec![];
        Ragdoll::manifold(&mut held, (gjk::Shape::Capsule(0.11, 0.04), ah, a, a),
            (gjk::Shape::Box(half, 0.), half, now, before));
        assert!(!held.is_empty(), "relative-motion cast must receive the crossing pair");
        assert!(held.iter().all(|h| h.n.x > 0.99 && h.gap < 0.));
    }
    #[test]
    fn moving_child_miss_is_not_a_contact_and_offset_bounds_rotate() {
        constants();
        let a = (Vec3::ZERO, Mat3::IDENTITY);
        let ah = Vec3::splat(0.05);
        let half = Vec3::splat(0.02);
        let sa = swept(a, a, ah);
        assert!(!moving_child_meets(a, sa, (Vec3::new(0.3, 2., 0.), Mat3::IDENTITY),
            (Vec3::new(-0.3, 2., 0.), Mat3::IDENTITY), Vec3::ZERO, half));
        // An off-centre COL child starts at the limb and rotates away. Its
        // last bound must still reach the pair; no extra collision padding.
        let c = Vec3::X;
        let last = (-Vec3::X, Mat3::IDENTITY);
        let now = (-Vec3::X, Mat3::from_rotation_z(0.5));
        assert!(!moving_child_meets(a, sa, now, now, c, half));
        assert!(moving_child_meets(a, sa, now, last, c, half));
    }
    #[test]
    fn position_split_preserves_com_velocity_when_root_origin_is_rebased() {
        // A positional penetration repair has no physical impulse. Exercise
        // the real begin/middle/end stages with spin: translating the solver
        // origin must not turn omega cross the repair into launch velocity.
        for offset in [Vec3::ZERO,Vec3::new(2430.,-1662.,0.)] {
            for spin in [Vec3::X*2.,Vec3::Y*-3.,Vec3::new(1.,-2.,3.)] {
                let (build,mut doll)=isolated_grip_body(Vec3::new(1.2,-0.7,0.4));
                let at=doll.frames().next().unwrap().translation+offset;
                doll.set_body(0,at,Quat::IDENTITY,Vec3::new(1.2,-0.7,0.4),spin);
                let drive=Drive{lift:1.,..Default::default()};
                let mut stage=doll.begin(&build,&Collision::default(),1./120.,&drive,&[],&[]).unwrap();
                assert!(doll.middle(&mut stage));
                let (before_v,before_w)=doll.motion(0);
                let before_p=doll.frames().next().unwrap().translation;
                let repair=Vec3::new(0.012,-0.008,0.006);
                let mut qp=vec![0.;stage.qd0.len()];
                qp[..3].copy_from_slice(&(repair/(stage.h*stage.damp)).as_dvec3().to_array());
                doll.end(stage,&qp,1.);
                let after_p=doll.frames().next().unwrap().translation;
                assert!(after_p.distance(before_p)>0.01,"split repair must remain active");
                assert!((after_p-before_p-repair).length()<0.0004,"requested geometry repair was not retained");
                let (after_v,after_w)=doll.motion(0);
                assert!(after_v.distance(before_v)<1e-6,
                    "position-only repair manufactured COM velocity: {before_v:?} -> {after_v:?}");
                assert!(after_w.distance(before_w)<1e-6);
                assert!((after_v.length_squared()-before_v.length_squared()).abs()<1e-6,
                    "position-only repair changed physical kinetic energy");
            }
        }
    }
    pub(super) fn finite_probe_build(mass:f32)->Build {
        let (mut b,_)=isolated_grip_body(Vec3::ZERO);let p=&mut b.parts[0];
        p.mass=mass;p.inertia=Vec3::splat(0.4*mass*0.2*0.2);p.radius=0.2;p.half=0.;p.boxed=None;
        p.size=1.;p.heft=1.;p.grown=1.;p.anchor=Vec3::ZERO;p.anchor_parent=Vec3::ZERO;
        p.offset=Transform::IDENTITY;p.bind=Quat::IDENTITY;p.rest=Quat::IDENTITY;b
    }
    #[test]
    fn finite_external_owner_shares_mass_across_characters_and_preserves_momentum(){
        use crate::external_mobility::ExternalOwner;
        for mass in [80.,300.,1200.] {for count in [1,4] {for reverse in [false,true] {for high in [false,true] {
            let build=finite_probe_build(80.);let h=1./600.;let damp=1.-DAMPING()*h;
            let at=Vec3::Z*15.;let half=Vec3::new(0.8,1.8,0.65);let velocity=Vec3::X*4.5;
            let inertia=Vec3::new(half.y*half.y+half.z*half.z,half.x*half.x+half.z*half.z,half.x*half.x+half.y*half.y)*(mass/3.);
            let owner=ExternalOwner{id:47,epoch:3,com:at,velocity,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/mass,angular_inverse:Mat3::from_diagonal(inertia.recip())};
            let obstacles=[Obstacle{id:13,owner:Some(47),at,centre:at,com:at,half,vel:velocity,..Default::default()}];
            let mut dolls:Vec<_>=(0..count).map(|i|Ragdoll::new(&build,&[Transform::from_translation(at+Vec3::new(0.98,(i as f32-(count-1)as f32/2.)*0.5,if high{0.4}else{0.}))],Vec3::ZERO)).collect();
            let drive=Drive{lift:1.,..Default::default()};let mut group=Group::default();group.stage_external(3,&[owner,owner]).unwrap();
            let mut members:Vec<_>=dolls.iter_mut().enumerate().map(|(i,d)|Member{doll:d,id:100+i,build:&build,drive,obstacles:&obstacles,others:&[]}).collect();
            if reverse{members.reverse();}Ragdoll::step_group(&mut members,&Collision::default(),h,&mut group);drop(members);
            assert!(group.external_error().is_none());assert_eq!(group.external_results().len(),1);
            let result=group.external_results()[0];assert!(result.impulse.length()>1.);
            let car_v=velocity+result.delta_velocity;let car_w=result.delta_spin;
            let mut momentum=car_v*mass;let mut energy=0.5*mass*car_v.length_squared()+0.5*(inertia*car_w).dot(car_w);
            let mut evidence=Vec3::ZERO;
            for d in &dolls {let(v,w)=d.motion(0);let(v,w)=(v/damp,w/damp);momentum+=v*80.;energy+=40.*v.length_squared()+0.5*(build.parts[0].inertia*w).dot(w);assert!(!d.ob_hits.is_empty(),"finite contacts must retain per-character event evidence");for &(_,j,_) in &d.ob_hits{evidence+=j;}}
            assert!(evidence.distance(result.impulse)<0.001,"pair evidence must match the one aggregate commit");
            assert!(momentum.distance(velocity*mass)<0.002,"finite shared owner lost momentum: {mass} {count} {reverse} {high}: {momentum:?}");
            assert!(energy<=0.5*mass*velocity.length_squared()*1.00001,"finite shared owner creates energy: {mass} {count} {high} {energy}");
            if !high {
                let expected=(1.+PART_ELASTICITY()*ELASTICITY())*4.5/(1./mass+1./(count as f32*80.));
                assert!((-result.impulse.x/expected-1.).abs()<0.001,"shared mass analytic mismatch: {:?}, expected{expected}",result.impulse);
            }
            assert!(result.position_correction.length()>0.0001,"split correction must be returned separately");
        }}}}
    }
    #[test]
    fn finite_grip_is_reciprocal_and_preserves_component_relative_velocity(){
        use crate::external_mobility::ExternalOwner;
        for relative in [Vec3::ZERO,Vec3::Y*0.7] {
            let build=finite_probe_build(25.);let at=Vec3::Z*15.;let h=1./600.;let damp=1.-DAMPING()*h;
            let velocity=Vec3::X*4.;
            let owner=ExternalOwner{id:9,epoch:1,com:at,velocity,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/25.,angular_inverse:Mat3::ZERO};
            let mut doll=Ragdoll::new(&build,&[Transform::from_translation(at)],Vec3::ZERO);
            doll.grips.push((0,Vec3::ZERO,at,0.,1000.));doll.grip_target_velocities.push((0,velocity+relative));doll.grip_target_owners.push((0,9));
            let mut group=Group::default();group.stage_external(1,&[owner]).unwrap();
            Ragdoll::step_group(&mut [Member{doll:&mut doll,id:1,build:&build,drive:Drive{lift:1.,..Default::default()},obstacles:&[],others:&[]}],&Collision::default(),h,&mut group);
            let r=group.external_results()[0];let v=doll.motion(0).0/damp;
            assert!((v-(velocity+r.delta_velocity)-relative).length()<0.001,"relative component velocity was erased");
            assert!((v*25.+r.impulse).length()<0.001,"grip reaction not reciprocal");
            assert!(doll.snapped.is_empty());
        }
    }
    #[test]
    fn missing_finite_snapshot_rejects_before_any_body_advances(){
        let build=finite_probe_build(25.);let at=Vec3::Z*15.;let mut doll=Ragdoll::new(&build,&[Transform::from_translation(at)],Vec3::X);
        let obstacles=[Obstacle{owner:Some(999),..Default::default()}];let mut group=Group::default();
        let before=doll.frames().next().unwrap();
        Ragdoll::step_group(&mut[Member{doll:&mut doll,id:1,build:&build,drive:Drive::default(),obstacles:&obstacles,others:&[]}],&Collision::default(),1./60.,&mut group);
        assert_eq!(group.external_error(),Some(crate::external_mobility::ExternalError::MissingSnapshot(999)));
        assert_eq!(doll.frames().next().unwrap(),before);
    }
    pub(super) fn original_ped_obstacle(id:u64,at:Vec3,velocity:Vec3)->Obstacle {
        let parts:Vec<_>=lsc4_combat::pedbodies::PARTS.iter().enumerate().map(|(i,p)|{
            let(shape,m,half)=if i==0||i==3{
                (gjk::Shape::Curved(Box::leak(Box::new(crate::curved::ped_part(p.core,2.*p.half,p.round))),p.round),Mat3::IDENTITY,Vec3::new(p.core+p.round,p.core+p.round,p.half+p.round))
            }else{(gjk::Shape::Capsule(p.half,p.round),Mat3::from_rotation_x(std::f32::consts::FRAC_PI_2),Vec3::new(p.round,p.half+p.round,p.round))};
            ObPart{shape,m,at:Vec3::Z*p.z,lo:-half,hi:half,mesh:None,filter:CollisionFilter{kind:p.types,include:p.include}}
        }).collect();
        Obstacle{id:id as i32,owner:Some(id),filter:CollisionFilter{kind:0x20,include:0x79FE},at,centre:at,com:at,half:Vec3::new(0.25,0.25,1.3),vel:velocity,parts:Box::leak(parts.into_boxed_slice()),..Default::default()}
    }
    pub(super) fn original_ped_owner(id:u64,epoch:u64,at:Vec3,velocity:Vec3)->crate::external_mobility::ExternalOwner {
        crate::external_mobility::ExternalOwner{id,epoch,com:at,velocity,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/25.,angular_inverse:Mat3::ZERO}
    }
    #[test]
    fn original_owner_pair_solves_without_nm_members_and_retains_component_filters(){
        constants();let at=Vec3::Z*15.;let left=at;let right=at+Vec3::X*0.45;
        assert_eq!(SWEEPS(),lsc4_combat::pedbodies::SWEEPS,"shared island must retain the original animated-owner sweep count");
        let mut group=Group::default();let owners=[original_ped_owner(1,1,left,Vec3::X*1.5),original_ped_owner(2,1,right,Vec3::ZERO)];
        group.stage_external(1,&owners).unwrap();
        group.stage_external_geometry(&[original_ped_obstacle(1,left,owners[0].velocity),original_ped_obstacle(2,right,owners[1].velocity)]).unwrap();
        Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut group);
        assert!(group.external_error().is_none());assert_eq!(group.external_results().len(),2);
        let contacts=group.external_contacts();assert!(!contacts.is_empty());
        assert!(contacts.iter().all(|c|!(c.child_a==2&&c.child_b==2)),"part2/2 must be excluded by original masks");
        assert!(contacts.iter().any(|c|c.child_a==2||c.child_b==2),"part2 must remain available against other compound children");
        assert!(contacts.iter().all(|c|c.impulse.length()>0.&&c.velocity_a==Vec3::X*1.5&&c.velocity_b==Vec3::ZERO));
        let sum=group.external_results().iter().map(|r|r.impulse).sum::<Vec3>();assert!(sum.length()<0.001);
        let a=group.external_results().iter().find(|r|r.id==1).unwrap();let b=group.external_results().iter().find(|r|r.id==2).unwrap();
        assert!(a.delta_velocity.x<0.&&b.delta_velocity.x>0.);
        assert_eq!(a.delta_spin,Vec3::ZERO);assert_eq!(b.delta_spin,Vec3::ZERO);
        assert!(!CollisionFilter{kind:0x20,include:0x7800}.meets(CollisionFilter::NM_FRAGMENT));
    }
    #[test]
    fn nm_owner_owner_chain_uses_one_coupled_island(){
        let build=finite_probe_build(80.);let at=Vec3::Z*15.;let h=1./60.;let damp=1.-DAMPING()*h;
        let owner_a=original_ped_owner(1,1,at,Vec3::ZERO);let owner_b=original_ped_owner(2,1,at+Vec3::X*0.51,Vec3::ZERO);
        let obstacles=[original_ped_obstacle(1,owner_a.com,Vec3::ZERO),original_ped_obstacle(2,owner_b.com,Vec3::ZERO)];
        let velocity=Vec3::X*4.5;let mut doll=Ragdoll::new(&build,&[Transform::from_translation(at-Vec3::X*0.43)],velocity);
        let mut group=Group::default();group.stage_external(1,&[owner_a,owner_b]).unwrap();group.stage_external_geometry(&obstacles).unwrap();
        Ragdoll::step_group(&mut[Member{doll:&mut doll,id:100,build:&build,drive:Drive{lift:1.,..Default::default()},obstacles:&obstacles,others:&[]}],&Collision::default(),h,&mut group);
        assert!(group.external_error().is_none());
        let b=group.external_results().iter().find(|r|r.id==2).unwrap();
        assert!(b.impulse.x>0.5,"NM impulse must reach ownerB via ownerA in this same island: {b:?}");
        let sum=group.external_results().iter().map(|r|r.impulse).sum::<Vec3>()+doll.motion(0).0/damp*80.-velocity*80.;
        assert!(sum.length()<0.003,"mixed island momentum: {sum:?}");
        assert!(group.external_contacts().iter().any(|c|c.a==1&&c.b==2&&c.impulse.length()>0.));
    }
}

#[test]
fn island_order_as_the_game() {
    moving_obstacle_regression::constants();
    assert_eq!(island_order(&[(Some(1), None), (Some(1), Some(1))]), [0, 1]);
    assert_eq!(island_order(&[(Some(1), None), (Some(1), Some(1)), (Some(2), None), (Some(2), Some(2)), (Some(1), Some(2))]), [2, delirium_data::kv!(0 int), 0, 1, delirium_data::kv!(1 int)]);
    assert_eq!(island_order(&[(Some(1), Some(2)), (Some(1), None), (Some(1), Some(1)), (Some(2), None), (Some(2), Some(2))]), [0, 1, 2, delirium_data::kv!(0 int), delirium_data::kv!(1 int)]);
    assert_eq!(island_order(&[(Some(1), None), (Some(2), None), (Some(delirium_data::kv!(0 int)), None), (Some(1), Some(2)), (Some(2), Some(delirium_data::kv!(0 int)))]), [0, delirium_data::kv!(0 int), 1, delirium_data::kv!(1 int), 2]);
}

pub fn contact_cost_take()->[f64;12]{contact_cost::take()}

pub(crate) fn par_each<T: Send>(items: &mut [T], f: impl Fn(&mut T) + Sync) {
    par_each_grain(items,delirium_data::kv!(0 int),f)
}
fn par_each_grain<T:Send>(items:&mut[T],grain:usize,f:impl Fn(&mut T)+Sync){
    // v0.5.0 b013841: reuse the engine's Bevy workers across native frames.
    // Keep preparation's measured one-character grain; pair jobs retain IV's.
    let pool=bevy_tasks::ComputeTaskPool::get_or_init(bevy_tasks::TaskPool::default);
    let cores=pool.thread_num().min(items.len()/grain);
    if cores < 2 {
        return items.iter_mut().for_each(f);
    }
    let f = &f;
    if env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_PHYSICS_THREAD_SCOPE_BASELINE"){
        std::thread::scope(|s|items.chunks_mut(items.len().div_ceil(cores)).for_each(|c|{s.spawn(move||c.iter_mut().for_each(f));}));
    }else{
        pool.scope(|s|items.chunks_mut(items.len().div_ceil(cores)).for_each(|c|s.spawn(async move{c.iter_mut().for_each(f)})));
    }
}

#[cfg(test)]
mod dense_vehicle_slot_tests{
 use super::*;
 // A body in traffic: what a step costs with a dozen car meshes in reach (cargo test --release -- --ignored --nocapture).
 #[test]#[ignore]
 fn traffic_step_cost(){
  moving_obstacle_regression::constants();
  let build=moving_obstacle_regression::finite_probe_build(80.);
  let mut triangles=vec![];
  for i in 0..190{let f=i as f32;let v=Vec3::new(f/190.*4.8-2.4,(f*0.73).cos()*1.,(f*0.11).sin()*0.8);
   let t=[v,v+Vec3::new(0.3,0.,0.),v+Vec3::new(0.,0.3,0.1)];
   triangles.push(ObPart{shape:gjk::Shape::Tri(t,0.002),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:t[0].min(t[1]).min(t[2]),hi:t[0].max(t[1]).max(t[2]),mesh:None,filter:Default::default()});}
  let all:&'static[ObPart]=Box::leak(triangles.into_boxed_slice());
  for (count,radius,children) in [(0usize,3.2f32,190usize),(12,30.,190),(12,3.2,190),(12,3.2,19),(1,3.2,190)]{
   let parts=&all[..children];
   let mut doll=Ragdoll::new(&build,&[Transform::from_translation(Vec3::new(0.,0.,15.))],Vec3::ZERO);
   let obs:Vec<_>=(0..count).map(|k|{let a=k as f32*0.52;let at=Vec3::new(a.cos()*radius,a.sin()*radius,15.);
    Obstacle{id:k as i32+10,at,centre:at,com:at,rot:Quat::IDENTITY,last:Some((at,Quat::IDENTITY)),half:Vec3::new(2.4,1.,0.8),parts,vel:Vec3::ZERO,..Default::default()}}).collect();
   let t=std::time::Instant::now();
   for _ in 0..300{
    doll.set_body(0,Vec3::new(0.,0.,15.),Quat::IDENTITY,Vec3::ZERO,Vec3::ZERO);
    doll.step(&build,&Collision::default(),1./120.,Drive{lift:1.,..Default::default()},&obs,&[]);
   }
   println!("traffic_step_ms={} obs={count} radius={radius} children={children}",t.elapsed().as_secs_f64()*1000./300.);
  }
 }
 #[test]
 fn indexed_manifolds_preserve_dense_moving_geometry_and_warm_start_bits(){
  moving_obstacle_regression::constants();
  let build=moving_obstacle_regression::finite_probe_build(80.);
  let mut triangles=vec![];
  for z in 0..16{for x in 0..16{
   let v=|x:usize,z:usize|Vec3::new(x as f32*0.08-0.64,0.,z as f32*0.08-0.64);
   for t in [[v(x,z),v(x+1,z),v(x+1,z+1)],[v(x,z),v(x+1,z+1),v(x,z+1)]]{
    triangles.push(ObPart{shape:gjk::Shape::Tri(t,0.002),m:Mat3::IDENTITY,at:Vec3::ZERO,
     lo:t[0].min(t[1]).min(t[2]),hi:t[0].max(t[1]).max(t[2]),mesh:None,filter:Default::default()});
   }
  }}
  let parts:&'static[ObPart]=Box::leak(triangles.into_boxed_slice());
  let run=|linear:bool,grouped:bool,sat:bool|{
   LINEAR_CAR_SLOTS.with(|v|v.set(linear));
   mesh_manifold::ENABLED.with(|v|v.set(grouped));
   mesh_manifold::SAT_ENABLED.with(|v|v.set(sat));
   let mut doll=Ragdoll::new(&build,&[Transform::from_translation(Vec3::new(0.,-0.15,15.))],Vec3::ZERO);
   let mut bits=vec![];let mut occupied=0;let mut compacted=false;
   for frame in 0..64{
    let turn=Quat::from_rotation_z((frame as f32*0.07).sin()*0.25);
    let old=Quat::from_rotation_z(((frame as f32-1.)*0.07).sin()*0.25);
    let at=Vec3::new((frame as f32*0.2).sin()*0.1,-0.15,15.);
    doll.set_body(0,at,Quat::IDENTITY,Vec3::Y*0.2,Vec3::ZERO);
    let ids=if frame<32{[17,29]}else{[29,17]};
    let obs:Vec<_>=ids.into_iter().map(|id|Obstacle{id,at:Vec3::Z*15.,centre:Vec3::Z*15.,com:Vec3::Z*15.,
     rot:turn,last:Some((Vec3::Z*15.,old)),half:Vec3::splat(0.7),parts,vel:Vec3::ZERO,..Default::default()}).collect();
    let obs=if frame==31{&[][..]}else{&obs[..]};
    doll.step(&build,&Collision::default(),1./120.,Drive{lift:1.,..Default::default()},obs,&[]);
    if frame==31{compacted=doll.held_car.is_empty();}
    occupied=occupied.max(doll.held_car.iter().filter(|h|!h.3.is_empty()).count());
    let f=doll.frames().next().unwrap();let(v,w)=doll.motion(0);
    for x in f.translation.to_array().into_iter().chain(f.rotation.to_array()).chain(v.to_array()).chain(w.to_array()){bits.push(x.to_bits()as u64);}
    bits.push(doll.held_car.len()as u64);
    for(k,id,c,hs)in &doll.held_car{
     bits.extend([*k as u64,*id as u32 as u64,*c as u64,hs.len()as u64]);
     for h in hs{for v in [h.local,h.ground,h.n,h.imp,h.pimp]{bits.extend(v.to_array().map(|v|v.to_bits()as u64));}bits.extend([h.gap.to_bits()as u64,h.age as u64,h.off as u64]);}
    }
   }
   assert!(compacted&&if grouped{occupied==2}else{occupied>10},"must cover dense source geometry and compaction, grouped={grouped}: {occupied} {compacted}");
   bits
  };
  for grouped in [false,true]{let old=run(true,grouped,true);let new=run(false,grouped,true);assert_eq!(old,new,"lookup cannot alter manifold order, warm impulses, poses or velocities");}
  assert_eq!(run(false,true,false),run(false,true,true),"SAT rejection cannot alter the stepped mesh's contacts, warm starts, poses or velocity bits");
  LINEAR_CAR_SLOTS.with(|v|v.set(false));
  mesh_manifold::ENABLED.with(|v|v.set(true));
  mesh_manifold::SAT_ENABLED.with(|v|v.set(true));
 }
}

#[cfg(test)]
mod contact_mass_equivalence {
 use super::*;
 #[test]
 fn sparse_mass_keeps_original_matrix_bits_for_articulated_contacts(){
  moving_obstacle_regression::constants();
  let links:Vec<_>=(0..21).map(|k|artic::Link{r:Mat3::from_rotation_z(k as f32*0.13).to_cols_array_2d().map(Vec3::from_array),x:Vec3::new(k as f32*0.07,0.1,0.03*k as f32),m:2.+k as f32,i:Vec3::new(0.1,0.2,0.3)}).collect();
  let joints:Vec<_>=(0..21).map(|k|if k==0{None}else{Some(artic::Joint{parent:(k-1)/2,ca:Vec3::Z*0.04,axis:if k%3==0{Some(Vec3::Y)}else{None},stiff:0.8,mode:0})}).collect();
  let art=Artic::new(&links,&joints,&(0..21).collect::<Vec<_>>());
  let mut seed=1234567u32;let mut sample=||{seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);(seed>>8)as f64/16777216.-0.5};
  let mut inputs=vec![];
  for n in 0..4096{
   let mut j:[Vec<f64>;3]=std::array::from_fn(|_|vec![0.;126]);
   for c in 0..=n%3{let k=(n+c*7)%21;for a in 0..3{for i in 0..6{j[a][k*6+i]=if (n+a+i)%7==0{-0.}else{sample()};}}}
   let mj=art.solve_rows([&j[0],&j[1],&j[2]]);
   let old=Block::original_mass(&art,&j,&mj,false);let new=Block::contact_mass(&art,&j);
   assert_eq!(old.to_cols_array().map(f64::to_bits),new.to_cols_array().map(f64::to_bits),"case {n}");
   inputs.push((j,mj));
  }
  for baseline in [true,false,true,false]{
   let start=std::time::Instant::now();
   for (j,mj) in &inputs{std::hint::black_box(if baseline{Block::original_mass(&art,j,mj,false)}else{Block::contact_mass(&art,j)});}
   println!("contact_mass baseline={baseline} cases={} ms={}",inputs.len(),start.elapsed().as_secs_f64()*1000.);
  }
 }
}

// Opt-in one-shot construction workload from the real native scene. It does
// not claim to serialize/replay the later whole coupled island.
fn capture_dense_construction(artic:&Artic,blocks:&[Block]){
 static PATH:std::sync::OnceLock<Option<std::path::PathBuf>>=std::sync::OnceLock::new();
 static DONE:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);
 let path=PATH.get_or_init(||(std::env::var_os("GTASA_PASSIVE_TEST_WINDOW").is_some()).then(||std::env::var_os("GTASA_DENSE_CONSTRUCTION_CAPTURE").map(std::path::PathBuf::from)).flatten());
 let Some(path)=path else{return};if blocks.len()<512||DONE.swap(true,std::sync::atomic::Ordering::SeqCst){return}
 let write=||->std::io::Result<()>{
  use std::io::Write;let mut f=std::io::BufWriter::new(std::fs::File::create(path)?);write!(f,"{{")?;
  if !artic.write_audit_source(&mut f)?{return Err(std::io::Error::other("missing captured articulated inputs"))}
  write!(f,"\"jacobians\":[],\"jacobian_bits\":[")?;for(i,b)in blocks.iter().enumerate(){if i>0{write!(f,",")?}let bits=b.j.each_ref().map(|j|j.iter().map(|x|x.to_bits()).collect::<Vec<_>>());write!(f,"{:?}",bits)?}write!(f,"],\"expected\":[")?;
  for(i,b)in blocks.iter().enumerate(){if i>0{write!(f,",")?}let bits:Vec<_>=b.mj.iter().flatten().chain(b.mass_matrix.to_cols_array().iter()).map(|x|x.to_bits()).collect();write!(f,"{:?}",bits)?}write!(f,"],\"contact_features\":[")?;
  // Actual two-sided witnesses, before binding or island sorting. This optional
  // evidence distinguishes repeated triangle contacts from distinct surfaces;
  // the construction replay alone cannot establish that from its Jacobians.
  for(i,b)in blocks.iter().enumerate(){if i>0{write!(f,",")?}if let(Some((ob,part,a,n)),Some(ext))=(b.ob,b.external){write!(f,"{{\"obstacle\":{ob},\"part\":{part},\"owner\":{},\"a\":{:?},\"b\":{:?},\"normal\":{:?},\"held\":",ext.id,a.to_array(),ext.point.to_array(),n.to_array())?;if let Some((x,y))=b.held{write!(f,"[{x},{y}]")?}else{write!(f,"null")?}write!(f,"}}")?}else{write!(f,"null")?}}
  write!(f,"]}}")?;f.flush()
 };let _=write();
}
pub fn audit_contact_construction(links:&[[f32;16]],joints:&[[f32;10]],order:&[usize],jacobians:&[[Vec<f64>;3]],baseline:bool)->Vec<Vec<u64>>{
 let links:Vec<_>=links.iter().map(|r|artic::Link{r:std::array::from_fn(|i|Vec3::from_slice(&r[i*3..i*3+3])),x:Vec3::from_slice(&r[9..12]),m:r[12],i:Vec3::from_slice(&r[13..16])}).collect();
 let joints:Vec<_>=joints.iter().map(|r|if r[0]<0.{None}else{Some(artic::Joint{parent:r[0]as usize,ca:Vec3::from_slice(&r[1..4]),axis:(r[7]>0.).then(||Vec3::from_slice(&r[4..7])),stiff:r[8],mode:r[9]as u32})}).collect();
 let art=Artic::new(&links,&joints,order);let mut cache=ContactResponses::default();
 jacobians.iter().map(|j|{let active:Vec<_>=(0..links.len()).filter(|&k|j.iter().any(|r|r[k*6..k*6+6].iter().any(|&v|v!=0.))).collect();
 let(mj,k)=if !baseline&&active.len()==1{cache.solve(&art,active[0],j)}else{let mj=art.solve_rows([&j[0],&j[1],&j[2]]);let k=if baseline{Block::original_mass(&art,j,&mj,false)}else{Block::contact_mass(&art,j)};(mj,k)};
  mj.iter().flatten().chain(k.to_cols_array().iter()).map(|x|x.to_bits()).collect()
 }).collect()
}

#[cfg(test)]
mod fitted_pivot_regression {
    use super::*;
    fn source() -> (Build,Vec<Transform>) {
        moving_obstacle_regression::constants();
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap())
    }
    #[test]
    fn fitted_character_uses_its_measured_pivots_with_authored_rigid_drives() {
        let (source,frames)=source();
        let joints:Vec<_>=source.parts.iter().enumerate().map(|(k,p)|{
            let pivot=frames[k].translation+frames[k].rotation*p.anchor;
            pivot*Vec3::new(1.13,0.87,1.08)
        }).collect();
        let (fitted,placed)=source.fit(&frames,&joints);
        let original=Ragdoll::new(&source,&frames,Vec3::ZERO);
        let target=Ragdoll::new(&fitted,&placed,Vec3::ZERO);
        let mut checked=0;
        for (k,p)in fitted.parts.iter().enumerate(){
            let Some(parent)=p.parent else{continue};
            if p.size==1. && fitted.parts[parent].size==1.{continue}
            let authored=Joint::new(&source.parts[k],parent,None);
            let cached=original.jconst(&source,k,&authored,None);
            let drive=Rigid{jc:Some(cached),..Default::default()};
            let joint=Joint::new(p,parent,Some(&drive));
            let actual=target.jconst(&fitted,k,&joint,Some(&drive));
            let expected=target.jconst(&fitted,k,&joint,None);
            assert!(actual.pa.distance(expected.pa)<1e-6,"part {k}: parent pivot came from the source body");
            assert!(actual.ca.distance(expected.ca)<1e-6,"part {k}: child pivot came from the source body");
            assert_eq!(actual.pm,cached.pm,"authored angular constraint must remain unchanged");
            assert_eq!(actual.cm,cached.cm,"authored angular constraint must remain unchanged");
            checked+=1;
        }
        assert!(checked>=16);
    }
    #[test]
    fn unscaled_authored_constraints_keep_all_their_original_bits() {
        let (source,frames)=source();
        let body=Ragdoll::new(&source,&frames,Vec3::ZERO);
        for(k,p)in source.parts.iter().enumerate(){
            let Some(parent)=p.parent else{continue};
            assert_eq!(p.size,1.);assert_eq!(source.parts[parent].size,1.);
            let joint=Joint::new(p,parent,None);
            let cached=body.jconst(&source,k,&joint,None);
            let drive=Rigid{jc:Some(cached),..Default::default()};
            let actual=body.jconst(&source,k,&joint,Some(&drive));
            for(a,b)in actual.pm.into_iter().chain([actual.pa,actual.ca]).chain(actual.cm)
                .zip(cached.pm.into_iter().chain([cached.pa,cached.ca]).chain(cached.cm)){
                assert_eq!(a.to_array().map(f32::to_bits),b.to_array().map(f32::to_bits));
            }
        }
    }
}

#[cfg(test)]mod shared_block_ownership {
 use super::*;
 #[test]fn binding_and_splitting_one_snapshot_preserve_other_response_rows(){
  moving_obstacle_regression::constants();
  let art=Artic::new(&[artic::Link{r:[Vec3::X,Vec3::Y,Vec3::Z],x:Vec3::ZERO,m:25.,i:Vec3::ONE}],&[None],&[0]);
  let j=std::array::from_fn(|a|{let mut j=vec![0.;6];j[a]=1.;j});
  let block=Block{external:Some(ExternalContact{id:9,point:Vec3::ZERO,basis:[Vec3::X,Vec3::Y,Vec3::Z]}),..Block::new(&art,j,DVec3::ZERO,0.5,None,Vec3::ZERO,false)};
  let original=block.clone();assert!(Arc::ptr_eq(&block.j,&original.j)&&Arc::ptr_eq(&block.mj,&original.mj));
  let owner=crate::external_mobility::ExternalOwner{id:9,epoch:1,com:Vec3::ZERO,velocity:Vec3::X,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/100.,angular_inverse:Mat3::IDENTITY};
  let bound=block.bind_external(&owner,0,6,true);
  assert_eq!(original.j[0].len(),6);assert_eq!(original.mj[0].len(),6);assert_eq!(bound.j[0].len(),12);
  assert_eq!(original.target,DVec3::ZERO);assert!(bound.target!=original.target);
  let saved=bound.clone();let split=bound.own_external_side();
  assert_eq!(split.j[0].len(),6);assert_eq!(saved.j[0].len(),12);assert_eq!(saved.mj[0].len(),12);
  assert!(Arc::ptr_eq(&split.j,&original.j)&&Arc::ptr_eq(&split.mj,&original.mj),"restore original rows without copying/truncating a shared snapshot");
 }
 #[test]fn paired_owner_binding_preserves_solved_velocity_position_and_original_row_bits(){
  moving_obstacle_regression::constants();
  let links:Vec<_>=(0..21).map(|k|artic::Link{r:[Vec3::X,Vec3::Y,Vec3::Z],x:Vec3::Z*(k as f32*0.07),m:2.+k as f32,i:Vec3::ONE}).collect();
  let joints:Vec<_>=(0..21).map(|k|if k==0{None}else{Some(artic::Joint{parent:(k-1)/2,ca:Vec3::Z*0.04,axis:None,stiff:0.8,mode:0})}).collect();
  let art=Artic::new(&links,&joints,&(0..21).collect::<Vec<_>>());
  for case in 0..256{
   let k=case%21;let point=Vec3::new(case as f32*0.017,0.2,-0.1);
   let basis=frame(Vec3::new(0.1,(case as f32*0.13).sin(),1.).normalize());
   let j=basis.map(|n|{let mut row=vec![0.;126];row[k*6..k*6+6].copy_from_slice(&v6(n.as_dvec3(),point.cross(n).as_dvec3()));row});
   let b=Block{external:Some(ExternalContact{id:9,point,basis}),..Block::new(&art,j,DVec3::new(0.2,-0.1,0.1),0.5,None,Vec3::new(0.2,0.,0.),false)};
   let p=Ragdoll::push_block(&b,-0.03,1./120.,Vec3::ZERO);
   let owner=crate::external_mobility::ExternalOwner{id:9,epoch:1,com:Vec3::new(0.2,0.1,0.),velocity:Vec3::X*2.,spin:Vec3::Z*0.3,linear_inverse:Mat3::IDENTITY/(100.+case as f32),angular_inverse:Mat3::IDENTITY*0.2};
   let run=|reuse|{
    BOUND_COEFFICIENT_REUSE.with(|v|v.set(reuse));
    LINEAR_CAR_SLOTS.with(|v|v.set(!reuse)); // Original index scanner as the independent solve control.
    let(v,p)=b.clone().bind_external_pair(p.clone(),&owner,3,132);
    if reuse{assert!(Arc::ptr_eq(&v.j,&p.j)&&Arc::ptr_eq(&v.mj,&p.mj));}
    let mut result=vec![];
    for mut block in [v,p]{
     let mut q:Vec<_>=(0..138).map(|i|(i as f64*0.11).sin()).collect();
     impulses(&mut [],std::slice::from_mut(&mut block),&mut q);
     result.extend(q.into_iter().map(f64::to_bits));
     result.extend(block.lambda.to_array().map(f64::to_bits));
     result.extend(block.target.to_array().map(f64::to_bits));
     result.extend(block.mass_matrix.to_cols_array().map(f64::to_bits));
     let split=block.own_external_side();
     result.extend(split.j.iter().flatten().chain(split.mj.iter().flatten()).map(|v|v.to_bits()));
    }result
   };
   assert_eq!(run(false),run(true),"finite owner contact case{case}");
  }
  BOUND_COEFFICIENT_REUSE.with(|v|v.set(true));
  LINEAR_CAR_SLOTS.with(|v|v.set(false));
 }
}
