// Unmodified functions extracted from LSC4; see upstream.json and LICENSE.
pub(crate) fn vmadd(a: f32, b: f32, c: f32) -> f32 {
    (a as f64 * b as f64 + c as f64) as f32
}

pub fn vrsq(n: f32) -> f32 {
    let mut e = crate::xenon::vrsqrtefp(n);
    if e == f32::INFINITY {
        e = 0.0;
    }
    let sq = e * e;
    let half = 0.5 * e;
    let t = (-(n as f64 * sq as f64 - 1.0)) as f32;
    vmadd(t, half, e)
}
