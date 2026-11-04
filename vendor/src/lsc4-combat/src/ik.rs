use std::f32::consts::{PI,TAU};
pub(crate) fn wrap(x: f32) -> f32 {
    let h = if x >= 0.0 { PI } else { -PI };
    let r = (((h + x) as f64) % TAU as f64) as f32 - h;
    if r == -PI { PI } else { r }
}
