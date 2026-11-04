use crate::mathrt;
pub fn heading_between(x1: f32, y1: f32, x2: f32, y2: f32) -> f32 {
    const H: f32 = std::f32::consts::FRAC_PI_2;
    let (dx, mut dy) = (x2 - x1, y2 - y1);
    if dy == 0.0 {
        dy = 0.0001;
    }
    // 0x829FFEF0 is 0x829FFFD8's arc tangent for x = 1 (the same table and steps).
    let a = mathrt::atan2((dx / dy) as f64, 1.0) as f32;
    match (dx > 0.0, dy > 0.0) {
        (true, true) => (H - a) + H,
        (true, false) => H - (a + H),
        (false, true) => -H - (a + H),
        (false, false) => (H - a) - H,
    }
}
