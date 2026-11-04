//! Animated peds' bodies against each other (board PH4o): the ped bound 0x822E5250 builds, which of its parts meet
//! another ped's, the contacts between two of them, the solver's row targets and push-out for such a contact (0x82976BC8,
//! with the bound's support point 0x8293A4D8) and the rows' solve.
//!
//! **The bound** (0x822E5250, every ped model's archetype; the matrices read live, `docs/vehicles/ai-left.md` T42) is
//! a composite (type 12) of four parts about the ped's root, all upright:
//! - 0: a curved geometry (type 5, 0x822E4E20(0.15, 1.27, 0.1)): four points at x = +-0.15, z = +-0.635 joined by
//!   curved edges of radius 0.15 at top and bottom, a margin of 0.1: a cylinder of radius 0.15, 0.635 either side of
//!   z = 0.085, grown by 0.1;
//! - 1: a capsule of radius 0.1 and length 0.65 (0x8295C5C8) at z = -0.875, its axis turned to z (0x821479F8, pi/2);
//! - 2: a capsule of radius 0.25 and length 0.97 at z = 0.085, turned the same, its type 0x20 and include 0x7800
//!   (0x822E54F0): it meets the map, not another ped's part 2;
//! - 3: a curved geometry 0x822E4E20(0.15, 0.3, 0.1) at z = -0.55, made when the model info's +0x8C byte is set (the
//!   live peds all had it).
//!
//! The other parts' type and include are all bits (0x82966A40). The archetype (0x825044A0) is type 0x20, include
//! 0x79FE, mass 25 (vf +0x3C, 820BECF8), its inertia the bound's for that mass. The composite's margin (+0x70) is the
//! bound's default, 0.04 (0x82939F18, 82B1AF10).
//!
//! **A pair's contacts.** Two peds' parts meet when each one's type has a bit of the other's include (the narrowphase
//! 0x829C06A0 on the part masks +0x94). A contact is made when the two are within 0.02 (phManifoldResult 0x829BFE10,
//! 82B1B300), with its normal from B to A, its depth (+0xC) and the point on each in its own space (+0x10 / +0x20).
//!
//! **The rows.** A plain contact's row (0x82976BC8) targets the speed that closes its depth less the slop and less
//! half of that again up to the slop, in one step. When either body's centre is about to pass the contact (the depth
//! past the smaller of the two sides' measures, the support point 0x8293A4D8 less the point, along the normal) the two
//! colliders are moved apart by hand (their vf +0x70) and the row's target is half that measure. The solve
//! (829CE7B0, kind 1 rows 829D0F18) sweeps the rows 8 times, each one's summed push kept at 0 or more.

use crate::aiahead::{vmadd, vrsq};

/// The archetype's mass (0x822E5250: vf +0x3C(25), 820BECF8): the same for every animated ped.
pub const MASS: f32 = 25.0;
/// The composite's margin (+0x70): the bound's default (0x82939F18, 82B1AF10).
pub const MARGIN: f32 = 0.04;
/// 82B1AF88: the depth a contact may sink before it pushes.
pub const SLOP: f32 = 0.007;
/// 82B1B300: past this distance no contact is made (Bullet's breaking threshold).
pub const BREAK: f32 = 0.02;
/// 82B1B380: the solve's sweeps.
pub const SWEEPS: usize = 8;
/// The physics sub-steps a frame (82AA4578).
pub const SUBSTEPS: usize = 2;
/// The two-collider row's over-relaxation (829CE7B0's f1, 82B1B394).
pub const SOR: f32 = 1.1;
/// 82976BC8: no measure on a side (1e30, 820A3D34).
const FAR: f32 = 1e30;

/// A part's shape, upright about the root: its core (a vertical segment `half` either side of its centre, widened
/// to a cylinder of `core` for a curved geometry) grown by `round` (a capsule's radius, a curved geometry's margin).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Part {
    pub z: f32,
    pub half: f32,
    pub core: f32,
    pub round: f32,
    pub types: u32,
    pub include: u32,
}

/// The ped bound's parts (0x822E5250; the module's notes).
pub const PARTS: [Part; 4] = [
    Part { z: 0.085, half: 1.27 * 0.5, core: 0.15, round: 0.1, types: u32::MAX, include: u32::MAX },
    Part { z: -0.875, half: 0.65 * 0.5, core: 0.0, round: 0.1, types: u32::MAX, include: u32::MAX },
    Part { z: 0.085, half: 0.97 * 0.5, core: 0.0, round: 0.25, types: 0x20, include: 0x7800 },
    Part { z: -0.55, half: 0.3 * 0.5, core: 0.15, round: 0.1, types: u32::MAX, include: u32::MAX },
];

/// Whether two parts meet (the narrowphase's part masks, both ways).
pub fn meets(a: &Part, b: &Part) -> bool {
    a.types & b.include != 0 && b.types & a.include != 0
}

/// A body: its root (the instance matrix's place) and heading (its rows: (cos, sin, 0), (-sin, cos, 0), z), or its
/// rows whole (`turn`, a car's), its velocity, its collider's inverse mass (+0xC4) and state (+0x14: 1 is never pushed
/// out), and its composite bound's margin (+0x70: its support, 0x8293A4D8).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Body {
    pub root: [f32; 3],
    pub heading: f32,
    pub turn: Option<[[f32; 3]; 3]>,
    pub vel: [f32; 3],
    pub inv_mass: f32,
    pub state: u32,
    pub margin: [f32; 3],
    /// Its spin, its centre of mass (world) and its inverse inertia in the world's axes: a ped's collider has no turning
    /// axes (82973830: zero), a car's is its handling's.
    pub spin: [f32; 3],
    pub com: [f32; 3],
    pub inv_inertia: [[f32; 3]; 3],
}

impl Body {
    /// An animated ped's body (state 2, the archetype's mass, the composite's default margin).
    pub fn ped(root: [f32; 3], heading: f32, vel: [f32; 3]) -> Body {
        Body { root, heading, turn: None, vel, inv_mass: 1.0 / MASS, state: 2, margin: [MARGIN; 3], spin: [0.0; 3], com: root, inv_inertia: [[0.0; 3]; 3] }
    }

    /// The instance matrix's rows.
    pub fn rows(&self) -> [[f32; 3]; 3] {
        if let Some(t) = self.turn {
            return t;
        }
        let (s, c) = self.heading.sin_cos();
        [[c, s, 0.0], [-s, c, 0.0], [0.0, 0.0, 1.0]]
    }

    /// A point in its own space as a lever from its centre of mass (world axes).
    pub fn lever(&self, local: [f32; 3]) -> [f32; 3] {
        let r = self.rows();
        [0, 1, 2].map(|i| r[0][i] * local[0] + r[1][i] * local[1] + r[2][i] * local[2] + self.root[i] - self.com[i])
    }

    /// Its inverse inertia on a vector.
    fn turn_by(&self, v: [f32; 3]) -> [f32; 3] {
        self.inv_inertia.map(|r| dot(r, v))
    }

    /// A world point in its own space (minus its place, through its rows).
    pub fn local(&self, p: [f32; 3]) -> [f32; 3] {
        let d = [p[0] - self.root[0], p[1] - self.root[1], p[2] - self.root[2]];
        self.rows().map(|r| r[0] * d[0] + r[1] * d[1] + r[2] * d[2])
    }
}

/// A contact between bodies `a` and `b`: its normal from B to A (+0x50; B's +0x60 is its negative), depth (+0xC), and
/// the point on each in its own space (+0x10 / +0x20).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Contact {
    pub a: usize,
    pub b: usize,
    pub n: [f32; 3],
    pub depth: f32,
    pub at_a: [f32; 3],
    pub at_b: [f32; 3],
}

/// The contacts between `bodies`, each standing pair's parts that meet within [`BREAK`]: the nearest points of the
/// two upright cores, each moved out by its rounding.
/// ponytail: Bullet's GJK (and its Minkowski sampling where the cores overlap) is worked out here directly for these
/// upright shapes, one point a part pair a step; the manifold's kept points (up to 6, matched within 2 cm, each with
/// its last push: the warm start) aren't kept.
pub fn contacts(bodies: &[Body], parts: usize) -> Vec<Contact> {
    let mut out = vec![];
    for a in 0..bodies.len() {
        for b in a + 1..bodies.len() {
            let (ra, rb) = (bodies[a].root, bodies[b].root);
            let (dx, dy) = (ra[0] - rb[0], ra[1] - rb[1]);
            if dx * dx + dy * dy > 1.0 || (ra[2] - rb[2]).abs() > 2.5 {
                continue;
            }
            for pa in &PARTS[..parts] {
                for pb in PARTS[..parts].iter().filter(|pb| meets(pa, pb)) {
                    if let Some(c) = nearest(ra, pa, rb, pb) {
                        let (n, depth, wa, wb) = c;
                        out.push(Contact { a, b, n, depth, at_a: bodies[a].local(wa), at_b: bodies[b].local(wb) });
                    }
                }
            }
        }
    }
    out
}

/// Two upright parts' nearest points (each core a vertical segment widened by `core`): the normal from B to A, the
/// depth (minus the distance past both roundings), and the point on each (world); none past [`BREAK`].
fn nearest(ra: [f32; 3], a: &Part, rb: [f32; 3], b: &Part) -> Option<([f32; 3], f32, [f32; 3], [f32; 3])> {
    let (alo, ahi) = (ra[2] + a.z - a.half, ra[2] + a.z + a.half);
    let (blo, bhi) = (rb[2] + b.z - b.half, rb[2] + b.z + b.half);
    let (dx, dy) = (ra[0] - rb[0], ra[1] - rb[1]);
    let flat = (dx * dx + dy * dy).sqrt();
    let (fx, fy) = if flat > 1e-6 { (dx / flat, dy / flat) } else { (1.0, 0.0) };
    // The cores' nearest heights: the overlap's middle (then the way is flat, the cores' rims may pass each other), or
    // the two ends that face each other (then from rim to rim, as far as they reach).
    let ((za, zb), n, core_gap) = if ahi < blo || bhi < alo {
        let (za, zb) = if ahi < blo { (ahi, blo) } else { (alo, bhi) };
        let across = (flat - a.core - b.core).max(0.0);
        let gap = ((za - zb) * (za - zb) + across * across).sqrt();
        ((za, zb), [fx * across / gap, fy * across / gap, (za - zb) / gap], gap)
    } else {
        let z = (alo.max(blo) + ahi.min(bhi)) * 0.5;
        ((z, z), [fx, fy, 0.0], flat - a.core - b.core)
    };
    let dist = core_gap - a.round - b.round;
    if dist > BREAK {
        return None;
    }
    // The core points: each one's rim toward the other.
    let cb = [rb[0] + fx * b.core, rb[1] + fy * b.core, zb];
    let ca = [ra[0] - fx * a.core, ra[1] - fy * a.core, za];
    let wb = [cb[0] + n[0] * b.round, cb[1] + n[1] * b.round, cb[2] + n[2] * b.round];
    let wa = [ca[0] - n[0] * a.round, ca[1] - n[1] * a.round, ca[2] - n[2] * a.round];
    Some((n, -dist, wa, wb))
}

/// The type of a ped's ragdoll fragment (fragInstNMGta's archetype, 0x822E5250's other arm: the animated bound's 0x20,
/// the ragdoll's 0x40).
pub const FRAGMENT: u32 = 0x40;

/// One side of a contact in the row pass: its instance's rows, the contact's point in its space (+0x10 / +0x20) and
/// the normal as that side has it (+0x50 / +0x60), its bound's margin vector (a composite's +0x70), and its collider's
/// inverse mass (+0xC4) and state (+0x14).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Side {
    pub rows: [[f32; 3]; 3],
    pub at: [f32; 3],
    pub n: [f32; 3],
    pub margin: [f32; 3],
    pub inv_mass: f32,
    pub state: u32,
}

/// A row's target and the push-out it made: the speed it wants along the normal (+0x10's x), and each collider's move
/// by hand (vf +0x70), none when not pushed.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Target {
    pub speed: f32,
    pub moves: Option<([f32; 3], [f32; 3])>,
}

/// The vector unit's three-lane dot (vmsum3fp): the products summed in double from +0, rounded once.
fn vdot3(a: [f32; 3], b: [f32; 3]) -> f32 {
    ((0.0f64 + a[0] as f64 * b[0] as f64) + a[1] as f64 * b[1] as f64 + a[2] as f64 * b[2] as f64) as f32
}

/// 0x8293A4D8 for a composite (type 12): its support along `d` (its own space) is the origin moved by the margin
/// vector along `d` made unit (vrsqrtefp and a Newton step; a zero `d` gives x).
fn support(d: [f32; 3], margin: [f32; 3]) -> [f32; 3] {
    let dd = vdot3(d, d);
    let r = vrsq(dd);
    let u = if dd > 0.0 { d.map(|v| r * v) } else { [1.0, 0.0, 0.0] };
    [0, 1, 2].map(|k| vmadd(u[k], margin[k], 0.0))
}

/// 82976BC8's measure for one side: its support point along the normal (taken into its space by its rows), less the
/// contact's point, along that normal; at least 0.
fn measure(s: &Side) -> f32 {
    let n = s.rows.map(|r| vdot3(r, s.n));
    let p = support(n, s.margin);
    let d = [p[0] - s.at[0], p[1] - s.at[1], p[2] - s.at[2]];
    let m = n[2].mul_add(d[2], n[0].mul_add(d[0], d[1] * n[1]));
    if m >= 0.0 { m } else { 0.0 }
}

/// 82976BC8 for a plain contact (kind 0) between two colliders on composite bounds, at `inv_dt` (the group's +0x44):
/// the depth less the slop, less min(half of that, the slop) when above 0; past the smaller measure (both colliders'
/// states not 1), the two are moved apart by that less half the measure, along the normal, shared by the inverse masses
/// (the larger squared over the smaller: s = 1 / (sqrt(small / large) + 1) to the larger, the rest to the other; half
/// each when equal), and the target keeps half the measure.
pub fn target(depth: f32, inv_dt: f32, a: &Side, b: &Side) -> Target {
    let d = depth - SLOP;
    let half = d * 0.5;
    let keep = if half - SLOP >= 0.0 { SLOP } else { half };
    let keep = if -keep >= 0.0 { 0.0 } else { keep };
    let d = d - keep;
    let (ma, mb) = (measure(a), measure(b));
    let m = if ma - mb >= 0.0 { mb } else { ma };
    // The two fnegs cancel; then the manifold's +0x90 is added (taken as 0: its writer isn't traced), which turns a
    // -0 to +0.
    let speed = |d: f32| -(-(d * inv_dt)) + 0.0;
    if a.state == 1 || b.state == 1 || m == FAR || !(d > 0.0) || !(d > m) {
        return Target { speed: speed(d), moves: None };
    }
    let kept = m * 0.5;
    let total = d - kept;
    let by = a.n.map(|v| v * total);
    let (a2, b2) = (a.inv_mass * a.inv_mass, b.inv_mass * b.inv_mass);
    let (sa, sb) = if a2 > b2 {
        let s = 1.0 / ((b2 / a2).sqrt() + 1.0);
        (s, 1.0 - s)
    } else if b2 > a2 {
        let s = 1.0 / ((a2 / b2).sqrt() + 1.0);
        (1.0 - s, s)
    } else {
        (0.5, 0.5)
    };
    let nb = -sb;
    Target { speed: speed(kept), moves: Some((by.map(|v| v * sa), by.map(|v| v * nb))) }
}

/// One step of `bodies`' contacts at sub-step `h`: each contact's row target and push-out (82976BC8), then the rows'
/// solve (829CE7B0): `SWEEPS` sweeps, each row's summed push along its normal kept at 0 or more, the change over-relaxed
/// ([`SOR`]); the velocities changed, and each body's move: the push-out and the velocity's change over `h`; and each
/// contact's summed push along its normal (N s, on A; B takes its negative), what the contact code hands each side's
/// record (0x82476E38 -> 0x8247EAE0).
/// Each row has both bodies' turn in it (829CE7B0's K: the levers' moments through the inverse inertias, a car's), so a
/// push off its centre turns the car; a ped's collider has none. Without friction (materials.dat's PED has none, so the pair's is 0: 8296C0C8); the vector unit's
/// 3x3 row (829D0F18) not followed lane for lane; the manifold's +0x90 not added (its writer isn't traced); no warm start.
pub fn step(bodies: &mut [Body], contacts: &[Contact], h: f32) -> (Vec<[f32; 3]>, Vec<f32>) {
    let mut moves = vec![[0.0f32; 3]; bodies.len()];
    let was: Vec<[f32; 3]> = bodies.iter().map(|b| b.vel).collect();
    let side = |b: &Body, at: [f32; 3], n: [f32; 3]| Side { rows: b.rows(), at, n, margin: b.margin, inv_mass: b.inv_mass, state: b.state };
    let rows: Vec<(usize, usize, [f32; 3], f32, f32, [f32; 3], [f32; 3])> = contacts.iter().map(|c| {
        let (ba, bb) = (&bodies[c.a], &bodies[c.b]);
        let t = target(c.depth, 1.0 / h, &side(ba, c.at_a, c.n), &side(bb, c.at_b, c.n.map(|v| -v)));
        if let Some((ma, mb)) = t.moves {
            moves[c.a] = add(moves[c.a], ma);
            moves[c.b] = add(moves[c.b], mb);
        }
        let (ra, rb) = (ba.lever(c.at_a), bb.lever(c.at_b));
        let (ta, tb) = (cross(ra, c.n), cross(rb, c.n));
        let k = ba.inv_mass + bb.inv_mass + dot(ta, ba.turn_by(ta)) + dot(tb, bb.turn_by(tb));
        (c.a, c.b, c.n, t.speed, if k > 0.0 { 1.0 / k } else { 0.0 }, ta, tb)
    }).collect();
    let mut sum = vec![0.0f32; rows.len()];
    for _ in 0..SWEEPS {
        let mut moved = 0.0f32;
        for (r, &(a, b, n, speed, mass, ta, tb)) in rows.iter().enumerate() {
            let v = dot(n, bodies[a].vel) + dot(ta, bodies[a].spin) - dot(n, bodies[b].vel) - dot(tb, bodies[b].spin);
            let total = (sum[r] + (speed - v) * mass * SOR).max(0.0);
            let l = total - sum[r];
            sum[r] = total;
            moved += l * l;
            bodies[a].vel = add(bodies[a].vel, n.map(|x| x * l * bodies[a].inv_mass));
            bodies[b].vel = add(bodies[b].vel, n.map(|x| x * -l * bodies[b].inv_mass));
            let (wa, wb) = (bodies[a].turn_by(ta), bodies[b].turn_by(tb));
            bodies[a].spin = add(bodies[a].spin, wa.map(|x| x * l));
            bodies[b].spin = add(bodies[b].spin, wb.map(|x| x * -l));
        }
        if moved == 0.0 {
            break;
        }
    }
    for (k, m) in moves.iter_mut().enumerate() {
        let dv = [0, 1, 2].map(|i| bodies[k].vel[i] - was[k][i]);
        *m = add(*m, dv.map(|v| v * h));
    }
    (moves, sum)
}

fn add(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ped_parts_meet_all_but_the_two_bodies() {
        // Part 2 (type 0x20, include 0x7800) meets the others' parts but not another part 2.
        assert!(!meets(&PARTS[2], &PARTS[2]));
        assert!(meets(&PARTS[2], &PARTS[0]) && meets(&PARTS[0], &PARTS[2]) && meets(&PARTS[1], &PARTS[3]));
        // Two peds 0.4 m apart overlap by 0.1 (radius 0.25 each, parts 0 and 2), along x from B to A.
        let bodies = [Body::ped([0.4, 0.0, 1.0], 0.0, [0.0; 3]), Body::ped([0.0, 0.0, 1.0], 0.5, [0.0; 3])];
        let c = contacts(&bodies, 4);
        let deepest = c.iter().map(|c| c.depth).fold(f32::MIN, f32::max);
        assert!((deepest - 0.1).abs() < 1e-6 && c.iter().all(|c| c.n[0] > 0.99));
        // Apart, or a storey apart: none.
        assert!(contacts(&[bodies[0], Body::ped([1.0, 0.0, 1.0], 0.0, [0.0; 3])], 4).is_empty());
        assert!(contacts(&[bodies[0], Body::ped([0.4, 0.0, 4.0], 0.0, [0.0; 3])], 4).is_empty());
    }

    #[test]
    fn a_contact_is_closed_by_the_rows_or_pushed_out_past_its_measure() {
        let s = |n: [f32; 3], at: [f32; 3]| Side { rows: [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]], at, n, margin: [MARGIN; 3], inv_mass: 1.0 / MASS, state: 2 };
        // 0.1 deep, the points 0.25 behind each root's side: the measures 0.29, no push-out; the target closes
        // 0.1 - 0.007 - 0.007 in the step.
        let t = target(0.1, 60.0, &s([1.0, 0.0, 0.0], [-0.25, 0.0, 0.0]), &s([-1.0, 0.0, 0.0], [0.25, 0.0, 0.0]));
        assert!(t.moves.is_none() && (t.speed - 0.086 * 60.0).abs() < 1e-4);
        // Deeper than the measure: moved by hand, half each (equal masses), the target half the measure.
        let t = target(0.5, 60.0, &s([1.0, 0.0, 0.0], [-0.25, 0.0, 0.0]), &s([-1.0, 0.0, 0.0], [0.25, 0.0, 0.0]));
        let (ma, mb) = t.moves.unwrap();
        assert!((ma[0] - mb[0].abs()).abs() < 1e-6 && ma[0] > 0.0 && (t.speed - 0.145 * 60.0).abs() < 1e-3);
        // Two peds walking into each other at 1.5 m/s each are stopped: neither goes on into the other.
        let mut bodies = [Body::ped([0.45, 0.0, 1.0], 0.0, [-1.5, 0.0, 0.0]), Body::ped([0.0, 0.0, 1.0], 0.0, [1.5, 0.0, 0.0])];
        let c = contacts(&bodies, 4);
        step(&mut bodies, &c, 1.0 / 60.0);
        assert!(bodies[0].vel[0] >= -1e-4 && bodies[1].vel[0] <= 1e-4);
    }

    #[test]
    fn a_push_off_a_cars_centre_turns_it() {
        // A ped walking at 1.5 m/s into a 1500 kg car's side 1.5 m ahead of its centre: the car yaws, a ped doesn't.
        let mut car = Body::ped([0.0, 0.0, 1.0], 0.0, [0.0; 3]);
        (car.inv_mass, car.inv_inertia) = (1.0 / 1500.0, [[0.0; 3], [0.0; 3], [0.0, 0.0, 1.0 / 2500.0]]);
        let mut bodies = [Body::ped([1.5, 1.2, 1.0], 0.0, [0.0, -1.5, 0.0]), car];
        let c = Contact { a: 0, b: 1, n: [0.0, 1.0, 0.0], depth: 0.01, at_a: [0.0, -0.25, 0.0], at_b: [1.5, 0.95, 0.0] };
        step(&mut bodies, &[c], 1.0 / 60.0);
        assert!(bodies[1].spin[2] < -1e-4 && bodies[0].spin == [0.0; 3]);
        // Through its centre, no turn.
        let mut bodies = [Body::ped([0.0, 1.2, 1.0], 0.0, [0.0, -1.5, 0.0]), car];
        step(&mut bodies, &[Contact { at_b: [0.0, 0.95, 0.0], ..c }], 1.0 / 60.0);
        assert!(bodies[1].spin[2].abs() < 1e-7);
    }
}
