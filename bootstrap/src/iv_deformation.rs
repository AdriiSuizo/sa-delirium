// From SuperSloppers/large-steal-car-4 ec249ba7570b80e2e548113a699d600f7d513d85, rust/crates/gta4-data/src/deformation.rs.
//! Car dents (CVehicleDeformation, the vehicle's +0x10D0). A 128 x 128 map, indexed by direction from the model
//! origin, holds an offset per texel (three bytes packed in a float, each (c - 128) / 128, so -1..0.99); the vehicle
//! shader's `damageTex` moves each vertex by it. A collision queues an impact (822833A8, at most 4 a frame, the
//! weakest replaced); the update (822855E8) stamps each into the map (82280FB0).
//!
//! Direction to texel (82280AE8): 64 + 32 (1 - z) (x, y) / |(x, y)| for the unit direction, so straight up is the
//! middle and straight down the rim. Lookup (82280E68 -> 82280CB8): bilinear, wrapping at 128, then
//! offset x 0.5 x min(|p| / radius, 1), radius = the model's bounding radius (model info +0x1C; 82280A88 stores its
//! inverse at +0x8C).

pub const SIZE: usize = 128;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Impact {
    /// Model space, already scaled (texel units).
    pub impulse: [f32; 3],
    /// Angular radius (rad).
    pub radius: f32,
    /// Unit direction from the model origin to the hit.
    pub dir: [f32; 3],
}

pub struct Deformation {
    pub texels: Vec<[f32; 3]>,
    pub queue: Vec<Impact>,
    /// The model's bounding radius (m).
    pub radius: f32,
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

/// Texel coordinates (0..128) of a unit direction.
pub fn texel(d: [f32; 3]) -> (f32, f32) {
    let l = (d[0] * d[0] + d[1] * d[1]).sqrt();
    let s = if l > 0.0 { (1.0 - d[2]) * 0.25 / l } else { 0.0 };
    ((d[0] * s + 0.5) * SIZE as f32, (d[1] * s + 0.5) * SIZE as f32)
}

impl Deformation {
    pub fn new(radius: f32) -> Self {
        Deformation { texels: vec![[0.0; 3]; SIZE * SIZE], queue: vec![], radius }
    }

    /// The offset (m, model space) at model-space point `p`.
    pub fn at(&self, p: [f32; 3]) -> [f32; 3] {
        let len = dot(p, p).sqrt();
        if len * len <= 1e-8 {
            return [0.0; 3];
        }
        let (u, v) = texel(p.map(|c| c / len));
        let (x, y) = (u as i32, v as i32);
        let (fx, fy) = (u - x as f32, v - y as f32);
        let t = |x: i32, y: i32| self.texels[(y & 127) as usize * SIZE + (x & 127) as usize];
        let (a, b, c, d) = (t(x, y), t(x + 1, y), t(x, y + 1), t(x + 1, y + 1));
        let scale = 0.5 * (len / self.radius).min(1.0);
        std::array::from_fn(|k| {
            (a[k] * (1.0 - fx) * (1.0 - fy) + b[k] * fx * (1.0 - fy) + c[k] * (1.0 - fx) * fy + d[k] * fx * fy) * scale
        })
    }

    /// CVehicleDeformation_ApplyCollisionImpact 822833A8: `impulse` (N s) at `p`, both model space, on a car of
    /// `mass` kg whose handling deformation multiplier (+0xDC) is `mult`. The vehicle's own multiplier (+0x12BC, the
    /// script's SET_VEHICLE_DEFORMATION_MULT) is taken as 1; so is the x2 / x0.5 for some other entities.
    pub fn impact(&mut self, impulse: [f32; 3], p: [f32; 3], mass: f32, mult: f32) {
        self.impact_with_footprint(impulse, p, mass, mult, false);
    }

    fn impact_with_footprint(&mut self, impulse: [f32; 3], p: [f32; 3], mass: f32, mult: f32, corner: bool) {
        let len = dot(p, p).sqrt();
        if len <= 0.0 {
            return;
        }
        let dir = p.map(|c| c / len);
        let s = mult / mass;
        let [x, y, z] = impulse.map(|c| c * s);
        // Sideways 0.1; forward 0.08 (front hits push back) or 0.06; up 0.1 or 0.12. Only inward dents.
        let mut j = [x * 0.1, y * if y >= 0.0 { 0.08 } else { 0.06 }, z * if z >= 0.0 { 0.1 } else { 0.12 }];
        for k in 0..3 {
            if dir[k] * j[k] > 0.0 {
                j[k] = 0.0;
            }
        }
        let m = dot(j, j).sqrt();
        let f = if m > 0.5 { (m - 0.5 + 1.0) * 0.5 } else { m }.min(1.5);
        if f > 1.0 / 128.0 {
            j = j.map(|c| c * f / m);
        }
        if j.iter().all(|c| c.abs() < 1.0 / 128.0) {
            return;
        }
        let mut radius = (f * 0.3).min(0.5);
        if radius < 0.01 {
            return;
        }
        if corner {
            // Confine a verified edge hit to the panels next to its contact.
            // Give the low-poly SA meshes enough coverage to sample it.
            radius = (radius * 0.78).clamp((0.32 / len).atan(), (0.72 / len).atan());
        }
        let new = Impact { impulse: j, radius, dir };
        if self.queue.len() < 4 {
            self.queue.push(new);
        } else if let Some(weak) = self.queue.iter_mut().filter(|q| dot(q.impulse, q.impulse) < dot(j, j))
            .min_by(|a, b| dot(a.impulse, a.impulse).total_cmp(&dot(b.impulse, b.impulse))) {
            *weak = new;
        }
    }

    /// 822855E8 -> 82280FB0: stamps the queued impacts; `random` gives 0..1 (the game's rand() / 32767). True if
    /// anything was queued.
    /// Ports: 0x82280FB0
    pub fn update(&mut self, mut random: impl FnMut() -> f32) -> bool {
        let queue = std::mem::take(&mut self.queue);
        for q in &queue {
            self.stamp(q, &mut random);
        }
        !queue.is_empty()
    }

    fn stamp(&mut self, q: &Impact, random: &mut impl FnMut() -> f32) {
        // The box: elevation and heading +- the radius, as texels; its nearest edge sets the falloff.
        let (el, az) = (q.dir[2].clamp(-1.0, 1.0).asin(), f32::atan2(-q.dir[0], q.dir[1]));
        let (cx, cy) = texel(q.dir);
        let (mut lo, mut hi) = ((cx, cy), (cx, cy));
        for (e, a) in [(el - q.radius, az - q.radius), (el + q.radius, az - q.radius), (el - q.radius, az + q.radius),
                       (el + q.radius, az + q.radius)] {
            let (u, v) = texel([-a.sin() * e.cos(), a.cos() * e.cos(), e.sin()]);
            (lo, hi) = ((lo.0.min(u), lo.1.min(v)), (hi.0.max(u), hi.1.max(v)));
        }
        let near = (cx - lo.0).min(hi.0 - cx).min(cy - lo.1).min(hi.1 - cy);
        let r2 = near * near;
        let size = dot(q.impulse, q.impulse).sqrt();
        for x in (lo.0 as i32).max(0)..=(hi.0 as i32).min(SIZE as i32 - 1) {
            for y in (lo.1 as i32).max(0)..=(hi.1 as i32).min(SIZE as i32 - 1) {
                let d2 = (x as f32 - cx).powi(2) + (y as f32 - cy).powi(2);
                let w = (d2 / r2).min(1.0);
                let (r1, r2) = (random(), random());
                let s = (1.0 - w.max(0.0)) * (0.8 + 0.25 * r1) + (0.2 * r2 - 0.1) * size;
                if s > 0.0 {
                    let t = &mut self.texels[y as usize * SIZE + x as usize];
                    // Stored as bytes: (v x 128 + 128) truncated.
                    *t = std::array::from_fn(|k| (((t[k] + q.impulse[k] * s).clamp(-1.0, 0.99) * 128.0 + 128.0).trunc() - 128.0) / 128.0);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mapping() {
        assert_eq!(texel([0.0, 0.0, 1.0]), (64.0, 64.0));
        assert_eq!(texel([0.0, 1.0, 0.0]), (64.0, 96.0));
        assert_eq!(texel([-1.0, 0.0, 0.0]), (32.0, 64.0));
        let (u, v) = texel([0.0, -0.6, -0.8]);
        assert!(u == 64.0 && (v - 6.4).abs() < 1e-4);
    }

    #[test]
    fn corner_stamp_stays_at_the_impact_side() {
        let mut d = Deformation::new(2.8);
        let edge = [1.0, 2.2, 0.25];
        d.impact_with_footprint([-8500.0, -10500.0, 0.0], edge, 1400.0, 1.0, true);
        assert_eq!(d.queue.len(), 1);
        assert!(d.queue[0].radius < 0.2);
        assert!(d.update(|| 0.5));
        let moved = d.at(edge);
        assert!(moved[0] < -0.10 && moved[1] < -0.07, "{moved:?}");
        assert_eq!(d.at([-1.0, 2.2, 0.25]), [0.0; 3]);
        assert_eq!(d.at([1.0, -2.2, 0.25]), [0.0; 3]);
    }

    /// A 10 m/s nose-on hit on a 1400 kg car (impulse 14000 N s backwards at the front bumper) dents the front in by
    /// about 22 cm, a little less to the side, and leaves the back alone.
    #[test]
    fn front_hit() {
        let mut d = Deformation::new(2.6);
        d.impact([0.0, -14000.0, 0.0], [0.0, 2.3, 0.2], 1400.0, 1.0);
        assert_eq!(d.queue.len(), 1);
        assert!((d.queue[0].impulse[1] + 0.55).abs() < 0.01 && (d.queue[0].radius - 0.165).abs() < 0.01, "{:?}", d.queue[0]);
        assert!(d.update(|| 0.5));
        let front = d.at([0.0, 2.3, 0.2]);
        let corner = d.at([0.2, 2.28, 0.2]);
        let back = d.at([0.0, -2.3, 0.2]);
        assert!(front[1] < -0.18 && front[1] > -0.26, "{front:?}");
        assert!(corner[1] < 0.0 && corner[1] > front[1], "{corner:?}");
        assert_eq!(back, [0.0; 3]);
        // Outward impulses do nothing; the queue keeps the 4 strongest.
        d.impact([0.0, 14000.0, 0.0], [0.0, 2.3, 0.2], 1400.0, 1.0);
        assert!(d.queue.is_empty());
        for k in 1..=5 {
            d.impact([-3000.0 * k as f32, 0.0, 0.0], [0.9, 0.0, 0.3], 1400.0, 1.0);
        }
        assert_eq!(d.queue.len(), 4);
        assert!(d.queue.iter().all(|q| q.impulse[0] < -0.4));
    }
}


// SA adapter for the checked IV map. Keep its random draws local to each car.
struct SaDeformation {
    map: Deformation,
    seed: u32,
}

#[unsafe(no_mangle)]
pub extern "C" fn sa_deformation_create(radius: f32, seed: u32) -> *mut std::ffi::c_void {
    if !radius.is_finite() || radius <= 0.0 { return std::ptr::null_mut(); }
    Box::into_raw(Box::new(SaDeformation { map: Deformation::new(radius), seed: seed | 1 })).cast()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_deformation_destroy(handle: *mut std::ffi::c_void) {
    if !handle.is_null() { drop(unsafe { Box::from_raw(handle.cast::<SaDeformation>()) }); }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_deformation_impact(handle: *mut std::ffi::c_void, impulse: *const [f32; 3],
    point: *const [f32; 3], mass: f32, multiplier: f32) {
    if let (Some(dent), Some(impulse), Some(point)) = unsafe { (handle.cast::<SaDeformation>().as_mut(), impulse.as_ref(), point.as_ref()) } {
        dent.map.impact(*impulse, *point, mass, multiplier);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_deformation_corner_impact(handle: *mut std::ffi::c_void, impulse: *const [f32; 3],
    point: *const [f32; 3], mass: f32, multiplier: f32) {
    if let (Some(dent), Some(impulse), Some(point)) = unsafe { (handle.cast::<SaDeformation>().as_mut(), impulse.as_ref(), point.as_ref()) } {
        dent.map.impact_with_footprint(*impulse, *point, mass, multiplier, true);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_deformation_update(handle: *mut std::ffi::c_void) -> i32 {
    let Some(dent) = (unsafe { handle.cast::<SaDeformation>().as_mut() }) else { return 0; };
    let seed = &mut dent.seed;
    i32::from(dent.map.update(|| {
        *seed ^= *seed << 13;
        *seed ^= *seed >> 17;
        *seed ^= *seed << 5;
        ((*seed >> 17) & 32767) as f32 / 32767.0
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_deformation_at(handle: *const std::ffi::c_void, point: *const [f32; 3], out: *mut [f32; 3]) {
    if let (Some(dent), Some(point), Some(out)) = unsafe { (handle.cast::<SaDeformation>().as_ref(), point.as_ref(), out.as_mut()) } {
        *out = dent.map.at(*point);
    }
}
