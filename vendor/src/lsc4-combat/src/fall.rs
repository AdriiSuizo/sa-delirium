pub fn lean(x_z: f32, y_z: f32) -> u8 {
    if x_z.abs() > y_z.abs() {
        if x_z < 0.0 { 1 } else { 3 }
    } else if y_z < 0.0 {
        0
    } else {
        2
    }
}
pub fn lies(x_z: f32, y_z: f32, spine_z: f32, ped_z: f32) -> u8 {
    if y_z.abs() > 0.6 || (spine_z - ped_z).abs() >= 0.35 {
        lean(x_z, y_z)
    } else {
        4
    }
}
pub fn lies_on_ragdoll(x_z: f32, y_z: f32, spine_z: f32, feet_z: [f32; 2]) -> u8 {
    if y_z.abs() > 0.6 {
        return lean(x_z, y_z);
    }
    let low = spine_z - 0.5;
    if feet_z[0] < low || !(feet_z[1] >= low) {
        4
    } else {
        lean(x_z, y_z)
    }
}
