// Exact extracted helpers; npc-decision-source.json.
fn f(w: u32) -> f32 {
    f32::from_bits(w)
}

fn fsel(test: f32, a: f32, b: f32) -> f32 {
    if test >= 0.0 { a } else { b }
}

pub(crate) fn atan(x: f64) -> f64 {
    use std::f64::consts::{FRAC_PI_2, FRAC_PI_3, FRAC_PI_6};
    let mut t = x.abs();
    let mut q = 0usize;
    let r = 'r: {
        if t > 1.0 {
            if t > f64::from_bits(0x7FD0_0000_0000_0000) {
                break 'r FRAC_PI_2;
            }
            q = 2;
            t = 1.0 / t;
        }
        if t > 0.2679491924311227 {
            q += 1;
            let sqrt3: f64 = 1.7320508075688772;
            t = sqrt3.mul_add(t, -1.0) / (sqrt3 + t);
        }
        let z = t * t;
        let mut b = 15.024001160028575 + z;
        let mut a = (-0.8375829936815006f64).mul_add(z, -8.494624035132068);
        b = b.mul_add(z, 59.57843614259735);
        a = a.mul_add(z, -20.505855195861653);
        b = b.mul_add(z, 86.15734959713025);
        a = a.mul_add(z, -13.688768894191927);
        let b = b.mul_add(z, 41.06630668257578);
        let mut v = a * z * t / b + t;
        if q > 1 {
            v = -v;
        }
        [0.0, FRAC_PI_6, FRAC_PI_2, FRAC_PI_3][q] + v
    };
    if x.abs().to_bits() == 0 {
        x
    } else if x >= 0.0 {
        r
    } else {
        -r
    }
}

pub(crate) fn angle(x: f32, y: f32) -> f32 {
    if x == 0.0 && y == 0.0 {
        return 0.0;
    }
    let at = |v: f32| atan(v as f64) as f32;
    let over = |d: f32, n: f32| (-1.0 / d) * n;
    let (quarter, three, half, whole) = (f(0x3FC9_0FDB), f(0x4096_CBE4), f(0x4049_0FDB), f(0x40C9_0FDB));
    if fsel(x, x, -x) < fsel(y, y, -y) {
        if y > 0.0 {
            if x > 0.0 { quarter - at(x / y) } else { at(over(y, x)) + quarter }
        } else if x > 0.0 {
            at(over(y, x)) + three
        } else {
            three - at(x / y)
        }
    } else if y > 0.0 {
        if x > 0.0 { at(y / x) } else { half - at(over(x, y)) }
    } else if x > 0.0 {
        whole - at(over(x, y))
    } else {
        at(y / x) + half
    }
}
