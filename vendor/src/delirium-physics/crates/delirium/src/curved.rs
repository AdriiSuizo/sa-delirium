use crate::prelude::*;
use crate::estimate::{vrsqrtefp,vrefp};
use crate::{Fma, Vd, Vx};

#[derive(Clone, Copy, Debug)]
struct Face {
    centre: Vec3,
    axis: Vec3,
    radius: f32,
    from: usize,
    to: usize,
}

#[derive(Clone, Debug, Default)]
pub struct Curved {
    verts: Vec<Vec3>,
    faces: Vec<Face>,
}
/// Lossless diagnostic transport: retain the authored circular faces rather
/// than substituting their vertex hull (which has different support points).
#[derive(Clone,Debug)]
pub struct CurvedSnapshot {pub verts:Vec<Vec3>,pub faces:Vec<(Vec3,Vec3,f32,usize,usize)>}
impl Curved {
    pub fn snapshot(&self)->CurvedSnapshot {CurvedSnapshot{verts:self.verts.clone(),faces:self.faces.iter().map(|f|(f.centre,f.axis,f.radius,f.from,f.to)).collect()}}
    pub fn from_snapshot(s:CurvedSnapshot)->Result<Self,String>{
        if s.verts.iter().any(|v|!v.is_finite())||s.faces.iter().any(|&(c,a,r,f,t)|!c.is_finite()||!a.is_finite()||!r.is_finite()||f>=s.verts.len()||t>=s.verts.len()) {return Err("invalid curved snapshot".into());}
        Ok(Self{verts:s.verts,faces:s.faces.into_iter().map(|(centre,axis,radius,from,to)|Face{centre,axis,radius,from,to}).collect()})
    }
}

fn be32(d: &[u8], at: usize) -> Result<u32, String> {
    d.get(at..at + delirium_data::kv!(1 int)).map(|b| u32::from_be_bytes(b.try_into().unwrap())).ok_or_else(|| "Read out of range".to_string())
}
fn bef(d: &[u8], at: usize) -> Result<f32, String> {
    be32(d, at).map(f32::from_bits)
}
fn vec3(d: &[u8], at: usize) -> Result<Vec3, String> {
    Ok(Vec3::new(bef(d, at)?, bef(d, at + delirium_data::kv!(1 int))?, bef(d, at + delirium_data::kv!(23 int))?))
}
fn ptr(p: u32) -> Result<usize, String> {
    if p >> delirium_data::kv!(81 int) == delirium_data::kv!(2 int) { Ok((p & delirium_data::kv!(82 int)) as usize) } else { Err(format!("Bad bounds pointer {p:08X}")) }
}

pub fn read(d: &[u8], at: usize) -> Result<Curved, String> {
    let (scale, offset) = (vec3(d, at + delirium_data::kv!(83 int))?, vec3(d, at + delirium_data::kv!(84 int))?);
    let (nv, nf) = (be32(d, at + delirium_data::kv!(85 int))? as usize, be32(d, at + delirium_data::kv!(86 int))? as usize);
    let mut c = Curved::default();
    if nv > 0 {
        let vp = ptr(be32(d, at + delirium_data::kv!(87 int))?)?;
        for i in 0..nv {
            let q = |k: usize| d.get(vp + i * delirium_data::kv!(6 int) + k * 2..vp + i * delirium_data::kv!(6 int) + k * 2 + 2).map(|b| i16::from_be_bytes([b[0], b[1]]) as f32).ok_or("Read out of range");
            c.verts.push(Vec3::new(q(0)?.mul_add(scale.x, offset.x), q(1)?.mul_add(scale.y, offset.y), q(2)?.mul_add(scale.z, offset.z)));
        }
    }
    if nf > 0 {
        let fp = ptr(be32(d, at + delirium_data::kv!(88 int))?)?;
        for i in 0..nf {
            let p = fp + i * delirium_data::kv!(89 int);
            let idx = |o: usize| {
                let v = be32(d, p + o)? as i32;
                if v >= 0 && (v as usize) < nv { Ok(v as usize) } else { Err(format!("Face {i} indexes vertex {v} of {nv}")) }
            };
            c.faces.push(Face { centre: vec3(d, p)?, axis: vec3(d, p + delirium_data::kv!(51 int))?, radius: bef(d, p + delirium_data::kv!(7 int))?, from: idx(delirium_data::kv!(90 int))?, to: idx(delirium_data::kv!(91 int))? });
        }
    }
    Ok(c)
}

/// The vector unit's max / min on one lane (vmaxfp / vminfp): of +0 and -0, -0 is the smaller.
fn vmax(a: f32, b: f32) -> f32 {
    if a > b || (a == b && a.is_sign_positive()) { a } else { b }
}
fn vmin(a: f32, b: f32) -> f32 {
    if a < b || (a == b && a.is_sign_negative()) { a } else { b }
}

/// 0x822E4E20: a ped bound's curved part (type 5; the ped bound 0x822E5250 makes (0.15, 1.27, 0.1) for part 0 and
/// (0.15, 0.3, 0.1) for part 3): four points at x = +-width, y = 0, z = +-height / 2 and four half circles of radius
/// width about z, two at the bottom and two at the top, so an upright cylinder. The margin only grows the box the
/// points are quantized in; the bound's own margin (+0x70, vf +0x28 with the same value) is the caller's to add.
/// The points are as `read` gives them back: quantized to int16 in the box (0x8223B070) and dequantized.
// The bound's other fields are its holder's: the box (+0x10 / +0x20, the same max / min as below) is the obstacle part's
// `lo` / `hi`, the margin (+0x70, vf +0x28) `Shape::Curved`'s radius, the material (vf +0x60 with the name "ped",
// 0x82005658, looked up by 0x831CC904 vf +0x18) the obstacle's PED friction. 0x82939A08 sets the centroid offset
// (+0x50) to 0x831AB680, zero, with its flag (+6): no shift. The base's allocations (0x82962FB8: +0x8C, +0xD0, +0xB4 /
// +0x88) are storage.
pub fn ped_part(width: f32, height: f32, margin: f32) -> Curved {
    let (lo, hi) = (height * -0.5, height * 0.5); // 820BED8C, 82000D74
    let max = Vec3::new(width + margin, width + margin, hi + margin);
    let min = Vec3::new(-width - margin, -width - margin, lo - margin);
    // +0x90 the half extent over 32767 (82B07200's halves, then 820BEDDC), +0xA0 the box's centre
    let scale = (max - min) * 0.5 * f32::from_bits(0x3800_0100);
    let offset = (min + max) * 0.5;

    // 0x8223B070: the point clamped to the box, less the centre, over the scale (vrefp and one Newton step), + 0.5
    // and cut toward zero (fctiwz; its low 16 bits kept). Then back as `read` does it: q * scale + offset, fused.
    let quant = |p: Vec3| {
        let lane = |i: usize| {
            let c = vmin(max[i], vmax(min[i], p[i])) - offset[i];
            let est = vrefp(scale[i]);
            let inv = est.mul_add(-est.mul_add(scale[i], -1.0), est);
            let q = ((c * inv + 0.5) as i32) as i16;
            (q as f32).mul_add(scale[i], offset[i])
        };
        Vec3::new(lane(0), lane(1), lane(2))
    };
    let verts: Vec<Vec3> = [(width, lo), (-width, lo), (width, hi), (-width, hi)].iter().map(|&(x, z)| quant(Vec3::new(x, 0.0, z))).collect();

    // 0x8296D1F8(face, from, to, radius, axis, up): the circle through the two points about `axis`, its centre off
    // their midpoint along -`up` by what's left of the radius past half their distance; the arc runs from -> to
    // about the axis, so the two swap when (up x d) . axis is below 0. (from == to, a full circle: the centre is
    // the midpoint + up * radius; never here.)
    let face = |from: usize, to: usize, axis: Vec3, up: Vec3| {
        let (a, b) = (verts[from], verts[to]);
        let mid = (b + a) * 0.5;
        if from == to {
            let c = Vec3::new(up.x.mul_add(width, mid.x), up.y.mul_add(width, mid.y), up.z.mul_add(width, mid.z));
            return Face { centre: c, axis, radius: width, from, to };
        }
        let d = b - a;
        let q = d.x.mul_add(d.x, d.z.mul_add(d.z, d.y * d.y)) * 0.25; // 82019674
        let s2 = width.mul_add(width, -q);
        let s = if s2 > 0.0 { s2.sqrt() } else { 0.0 };
        // vnmsubfp: -(up * s - mid), so a zero lane comes out -0
        let centre = Vec3::new(-up.x.mul_add(s, -mid.x), -up.y.mul_add(s, -mid.y), -up.z.mul_add(s, -mid.z));
        let c = Vec3::new(
            -d.y.mul_add(up.z, -(d.z * up.y)),
            -d.z.mul_add(up.x, -(d.x * up.z)),
            -d.x.mul_add(up.y, -(d.y * up.x)),
        );
        let k = c.x.mul_add(axis.x, c.y.mul_add(axis.y, c.z * axis.z));
        let (from, to) = if k >= 0.0 { (from, to) } else { (to, from) };
        Face { centre, axis, radius: width, from, to }
    };
    // 82B071E0 (0, 0, 1), 82B071D0 (0, 1, 0), each negated as 0 - it
    let (z, y) = (Vec3::Z, Vec3::Y);
    let faces = vec![face(0, 1, Vec3::ZERO - z, y), face(1, 0, Vec3::ZERO - z, Vec3::ZERO - y), face(2, 3, z, Vec3::ZERO - y), face(3, 2, z, y)];
    Curved { verts, faces }
}


impl Curved {
    pub fn support(&self, d: Vec3) -> Vec3 {
        let dd = d.vd(d);
        let est = vrsqrtefp(dd);
        let est = if est == f32::INFINITY { 0.0 } else { est };
        let u = d * Fma::fma(Fma::fma(-dd, est * est, 1.0), 0.5 * est, est);

        let (mut best, mut pt) = (-f32::MAX, Vec3::ZERO);
        for &p in &self.verts {
            let k = u.vd(p);
            if k > best {
                (best, pt) = (k, p);
            }
        }
        let (best0, pt0) = (best, pt);
        for f in &self.faces {
            let perp = f.axis.vx(u).vx(f.axis);
            let len2 = perp.vd(perp);
            if len2 > delirium_data::kv!(92 f32) {
                let est = vrsqrtefp(len2);
                let est = if est == f32::INFINITY { 0.0 } else { est };
                let n = Fma::fma(Fma::fma(-len2, est * est, 1.0), 0.5 * est, est);
                let e = perp * f.radius * n;
                let (a, b) = (self.verts[f.from] - f.centre, self.verts[f.to] - f.centre);
                let neg = |s: f32| 0.0 > s;
                let c1 = neg(f.axis.vd(a.vx(e)));
                let c2 = neg(f.axis.vd(a.vx(b)));
                let c3 = neg(f.axis.vd(e.vx(b)));
                let on = f.from == f.to || (!c1 && !c2 && !c3) || (c2 && !(c1 && c3));
                let cand = e + f.centre;
                let k = u.vd(cand);
                if on && k > best {
                    (best, pt) = (k, cand);
                }
            }
            if !(f.radius > 0.0) {
                (best, pt) = (best0, pt0);
            }
        }
        pt
    }
}

