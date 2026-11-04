//! The exe's own floor, ceiling, sine, cosine and arc tangent (its C runtime's; Xbox 360 TU8 addresses), step for step, so
//! ported code that calls them gives the game's bits. They are not correctly rounded: the sine and cosine
//! differ from an exact one in the last bits of a float now and then, and the ceiling of a number between -1
//! and 0 is +0 where an exact one gives -0.
//!
//! Checked against the exe through their callers (`tools/xbox360-re/checks/math_sound.py`).
use crate::xenon::fneg;

/// The exe's `fsel` on doubles: `a` if `test` is 0 or more, else `b` (a NaN picks `b`).
fn fsel(test: f64, a: f64, b: f64) -> f64 {
    if test >= 0.0 { a } else { b }
}

/// 0x82A00C00. Past 1e18 in size the number comes back as it is; a zero keeps its sign.
pub fn floor(x: f64) -> f64 {
    let whole = (x as i64) as f64;
    let r = fsel(x - whole, whole, whole - 1.0);
    let r = fsel(1e18 - x.abs(), r, x);
    fsel(-x.abs(), x, r)
}

/// 0x82A018C0.
pub fn ceil(x: f64) -> f64 {
    let whole = (x as i64) as f64;
    let r = fsel(whole - x, whole, whole + 1.0);
    let r = fsel(1e18 - x.abs(), r, x);
    fsel(-x.abs(), x, r)
}

const INV_PI: f64 = f64::from_bits(0x3FD4_5F30_6DC9_C883);
/// Pi in two parts, for taking whole multiples off without losing bits.
const PI_HIGH: f64 = f64::from_bits(0x4009_21FB_5440_0000);
const PI_LOW: f64 = f64::from_bits(0x3DE0_B461_1A60_0000);
/// Past this in size the result is a NaN.
const LIMIT: f64 = 220_000_000.0;
const NAN: f64 = f64::from_bits(0xFFF8_0000_0000_0000);
/// The series for sine over the angle, in powers of the angle squared, highest first (0x82B1E0E0 down to
/// 0x82B1E0A8); the last term is 1.
const SERIES: [u64; 8] = [
    0x3CE8_80FF_6993_DF95,
    0xBD6A_E420_DC08_499C,
    0x3DE6_123C_686A_D430,
    0xBE5A_E645_4B5D_C0AB,
    0x3EC7_1DE3_A524_F063,
    0xBF2A_01A0_1A01_3E1A,
    0x3F81_1111_1111_10B0,
    0xBFC5_5555_5555_5555,
];

/// The sine of `r` (within half a turn of 0) by the series: each step one fused multiply-add.
fn series(r: f64) -> f64 {
    let z = r * r;
    let mut p = f64::from_bits(SERIES[0]);
    for &c in &SERIES[1..] {
        p = p.mul_add(z, f64::from_bits(c));
    }
    p.mul_add(z, 1.0) * r
}

/// 0x829FFD48: sine. The size of the angle has the nearest whole number of half turns taken off, the series
/// gives the sine of what is left, and an odd number of half turns and a negative angle each turn the sign.
pub fn sin(x: f64) -> f64 {
    let ax = x.abs();
    let sign = fsel(x, 1.0, -1.0);
    let turns = (INV_PI * ax).round_ties_even();
    let r = fneg(PI_HIGH.mul_add(turns, -ax));
    let r = fneg(PI_LOW.mul_add(turns, -r));
    let mut v = series(r);
    if (turns as i64) & 1 != 0 {
        v = -v;
    }
    if ax.to_bits() == 0 {
        return x;
    }
    fsel(ax - LIMIT, NAN, v * sign)
}

/// 0x829FFE18: cosine, as the sine of the angle's size plus a quarter turn.
pub fn cos(x: f64) -> f64 {
    let ax = x.abs();
    let shifted = std::f64::consts::FRAC_PI_2 + ax;
    let turns = (INV_PI * shifted).round_ties_even();
    let half = turns - 0.5;
    let r = fneg(PI_HIGH.mul_add(half, -ax));
    let r = fneg(PI_LOW.mul_add(half, -r));
    let mut v = series(r);
    if (turns as i64) & 1 != 0 {
        v = -v;
    }
    if ax == 0.0 {
        return 1.0;
    }
    fsel(shifted - LIMIT, NAN, v)
}

/// 0x829FFFD8, the exe's `atan2(y, x)`: the smaller of |y|, |x| over the larger, past tan 15 degrees turned by 30
/// (fused), a rational polynomial (its sums fused) on the square, added to its table's angle (0, pi/6, pi/2, pi/3),
/// then into the quadrant by `x`'s sign (`fsel`, so -0 counts as 0 or more) and `y`'s sign bit. Both 0: `y` as it
/// is for a positive `x`, else pi with `y`'s sign.
pub fn atan2(y: f64, x: f64) -> f64 {
    const PI: f64 = 3.141592653589793;
    let r;
    if x == 0.0 {
        if y == 0.0 {
            if x.to_bits() >> 63 == 0 {
                return y;
            }
            return if y.to_bits() >> 63 == 0 { PI } else { -PI };
        }
        r = 1.5707963267948966;
    } else {
        let (mut num, mut den, mut quadrant) = (y.abs(), x.abs(), 0usize);
        if num > den {
            (num, den, quadrant) = (den, num, 2);
        }
        let mut t = num / den;
        if t > 0.2679491924311227 {
            quadrant += 1;
            let sqrt3: f64 = 1.7320508075688772;
            t = sqrt3.mul_add(t, -1.0) / (sqrt3 + t);
        }
        let z = t * t;
        let mut p = (-0.8375829936815006f64).mul_add(z, -8.494624035132068);
        let mut q = 15.024001160028575 + z;
        q = q.mul_add(z, 59.57843614259735);
        p = p.mul_add(z, -20.505855195861653);
        q = q.mul_add(z, 86.15734959713025);
        p = p.mul_add(z, -13.688768894191927);
        q = q.mul_add(z, 41.06630668257578);
        let mut v = p * z * t / q + t;
        if quadrant > 1 {
            v = -v;
        }
        r = [0.0, 0.5235987755982989, 1.5707963267948966, 1.0471975511965979][quadrant] + v;
    }
    let v = fsel(x, r, PI - r);
    if y.to_bits() >> 63 == 0 { v } else { -v }
}

/// 0x82A020C8, the exe's `acos` (0x82A01F08 with its second argument 1): past |x| = 0.5 on z = (1 - |x|) / 2 and
/// s = -2 sqrt(z), else on z = x^2 and s = |x|; r = s + s z P(z) / Q(z) (both sums fused, Q's lead 1), then
/// 0 - r (pi/2 - r) for x >= 0 (`fsel`, so -0 too), pi + r (pi/2 + r) otherwise. Its table at 0x82B1E230.
pub fn acos(x: f64) -> f64 {
    let (r, small) = arc(x.abs());
    let (lo, hi) = if small { (HALF_PI, HALF_PI) } else { (0.0, 3.141592653589793) };
    if x >= 0.0 { lo - r } else { hi + r }
}

const HALF_PI: f64 = 1.5707963267948966;

/// The series acos and asin share, on `a` = |x|: r as `acos` says, and whether `a` was 0.5 or less.
fn arc(a: f64) -> (f64, bool) {
    const P: [u64; 5] = [0xC03B5E55A83A0A62, 0x404C9AA7360AD48A, 0xC043D82CA9A6DA9F, 0x40244E1764EC3927, 0xBFE64BBDB5E61E65];
    const Q: [u64; 5] = [0xC06486C03E2B87CC, 0x407A124F101EB843, 0xC077DDCEFC56A848, 0x4062DE7C96591C70, 0xC037D2E86EF9861F];
    let (p, q) = (P.map(f64::from_bits), Q.map(f64::from_bits));
    let (z, s, small) = if !(a > 0.5) { (a * a, a, true) } else {
        let z = (1.0 - a) * 0.5;
        (z, -(z.sqrt() * 2.0), false)
    };
    let top = p[4].mul_add(z, p[3]).mul_add(z, p[2]).mul_add(z, p[1]).mul_add(z, p[0]);
    let bottom = (q[4] + z).mul_add(z, q[3]).mul_add(z, q[2]).mul_add(z, q[1]).mul_add(z, q[0]);
    (top * z * s / bottom + s, small)
}

/// 0x82A02000, the exe's `asin`: the same series as `acos`, then r (pi/2 + r past |x| = 0.5) with x's sign
/// (`fsel`, so -0 counts as 0 or more). A zero comes back as it is.
pub fn asin(x: f64) -> f64 {
    if x == 0.0 {
        return x;
    }
    let (r, small) = arc(x.abs());
    let r = r + if small { 0.0 } else { HALF_PI };
    if x >= 0.0 { r } else { -r }
}

/// 0x82A02450, the exe's whole-number test on a double: 0 not whole, 1 odd, 2 even. Rounds through a 64-bit
/// integer (saturating), so past 2^63 a number is "not whole" (2^63 itself counts as whole).
fn whole_kind(y: f64) -> u8 {
    if ((y.round_ties_even() as i64) as f64) != y {
        return 0;
    }
    let h = y * 0.5;
    if ((h.round_ties_even() as i64) as f64) == h { 2 } else { 1 }
}

/// 0x82A06978, the exe's `frexp` for a positive number: (m, e) with x = m 2^e, m in [0.5, 1).
fn frexp(x: f64) -> (f64, i32) {
    let field = ((x.to_bits() >> 52) & 0x7FF) as i32;
    if field == 0 {
        // The exe shifts a smaller number's mantissa up a bit at a time; the same as scaling by 2^54 first.
        let (m, e) = frexp(x * f64::from_bits(0x4350_0000_0000_0000));
        return (m, e - 54);
    }
    (f64::from_bits(x.to_bits() & 0x800F_FFFF_FFFF_FFFF | 0x3FE0_0000_0000_0000), field - 0x3FE)
}

/// 0x82A068A0: the exponent `frexp` would give a positive normal number.
fn exponent(x: f64) -> i32 {
    ((x.to_bits() >> 52) & 0x7FF) as i32 - 0x3FE
}

/// 0x82A06878: `x` with its exponent field set to `e + 0x3FE` (so `x` moved to exponent `e` in `frexp`'s terms).
fn set_exponent(x: f64, e: i32) -> f64 {
    f64::from_bits(x.to_bits() & 0x800F_FFFF_FFFF_FFFF | ((e + 0x3FE) as u64) << 52)
}

/// 2^(-k/16) for k = 0..16 (0x820A8F50, 8 bytes each).
const POW2: [u64; 17] = [
    0x3FF0_0000_0000_0000, 0x3FEE_A4AF_A2A4_90DA, 0x3FED_5818_DCFB_A487, 0x3FEC_199B_DD85_529C,
    0x3FEA_E89F_995A_D3AD, 0x3FE9_C491_82A3_F090, 0x3FE8_ACE5_422A_A0DB, 0x3FE7_A114_73EB_0187,
    0x3FE6_A09E_667F_3BCD, 0x3FE5_AB07_DD48_5429, 0x3FE4_BFDA_D536_2A27, 0x3FE3_DEA6_4C12_3422,
    0x3FE3_06FE_0A31_B715, 0x3FE2_387A_6E75_6238, 0x3FE1_72B8_3C7D_517B, 0x3FE0_B558_6CF9_890F,
    0x3FE0_0000_0000_0000,
];
/// The small corrections to the table's centres, for the log (0x820A8FD8 + 8 i, i = 1..8).
const CORR: [u64; 9] = [
    0, 0xBC8E_A000_0000_0000, 0x3C71_1000_0000_0000, 0x3C6C_6000_0000_0000, 0xBC74_1000_0000_0000,
    0x3C86_3000_0000_0000, 0x3C7A_E000_0000_0000, 0x3C89_B000_0000_0000, 0x3C88_A800_0000_0000,
];
/// log2 e (0x820A9020).
const LOG2E: f64 = f64::from_bits(0x3FF7_1547_652B_82FE);

/// 0x82A03580, the exe's natural log for a positive finite number: x = m 2^k with m in [0.5, 1), moved to
/// (u - 1) / (u + 1) with u = m (past 0.7071, 0x820A90A8) or 2m, w = twice that, then a rational polynomial in w^2
/// (0x820A90D0..0x820A9118; sums fused as the exe does) and k times ln 2 in two parts (0x820A90B0, 0x820A9108).
fn ln(x: f64) -> f64 {
    if x == 1.0 {
        return 0.0;
    }
    // A number below the smallest normal is scaled by 2^53 first (0x820A9120), the exponent taken off by 0x433.
    let (x, bias) = if x < f64::MIN_POSITIVE { (x * f64::from_bits(0x4340_0000_0000_0000), 0x433) } else { (x, 0x3FE) };
    let mut k = ((x.to_bits() >> 52) & 0x7FF) as i32 - bias;
    let m = f64::from_bits(x.to_bits() & 0x800F_FFFF_FFFF_FFFF | 0x3FE0_0000_0000_0000);
    let (num, den);
    if m > f64::from_bits(0x3FE6_A09E_667F_3BCD) {
        num = (m - 0.5) - 0.5;
        den = (m + 1.0) * 0.5;
    } else {
        num = m - 0.5;
        k -= 1;
        den = (num + 1.0) * 0.5;
    }
    let w = num / den;
    let z = w * w;
    let a = f64::from_bits(0x4030_624A_2016_AFED);
    let top = fneg(z.mul_add(f64::from_bits(0x3FE9_4415_B356_BD29), -a));
    let sum = (z - f64::from_bits(0x4041_D580_4B67_CE0F)).mul_add(z, f64::from_bits(0x4073_8083_FA15_267E));
    let top = top.mul_add(z, -f64::from_bits(0x4050_07FF_12B3_B59A));
    let bottom = sum.mul_add(z, -f64::from_bits(0x4088_0BFE_9C0D_9077));
    let r = (top * z / bottom + 1.0).mul_add(w, -(k as f64 * f64::from_bits(0x3F2B_D010_5C61_0CA8)));
    (k as f64).mul_add(f64::from_bits(0x3FE6_3000_0000_0000), r)
}

/// 0x82A024C0, the exe's `pow` for an infinite base or exponent (neither a NaN, exponent not 0).
fn pow_inf(x: f64, y: f64) -> f64 {
    let ax = x.abs();
    if y == f64::INFINITY {
        return if ax > 1.0 { f64::INFINITY } else if ax < 1.0 { 0.0 } else { 1.0 };
    }
    if y == f64::NEG_INFINITY {
        // |x| = 1 gives a NaN here (0x82B1E758), not 1.
        return if ax > 1.0 { 0.0 } else if ax < 1.0 { f64::INFINITY } else { NAN };
    }
    if x == f64::INFINITY {
        return if y > 0.0 { f64::INFINITY } else { 0.0 };
    }
    // -inf: the sign follows an odd whole exponent.
    match (y > 0.0, whole_kind(y) == 1) {
        (true, true) => f64::NEG_INFINITY,
        (true, false) => f64::INFINITY,
        (false, true) => -0.0,
        (false, false) => 0.0,
    }
}

/// 0x82A02A90..0x82A02AAC, the end of `pow`: `p` (a normal number) moved to exponent `e` in `frexp`'s terms, times
/// `sign`; past 0x400 infinity, below -0x3FD (no subnormals) a zero.
fn pow_end(p: f64, e: i32, sign: f64) -> f64 {
    if e > 0x400 {
        return f64::INFINITY * sign;
    }
    if e < -0x3FD {
        return sign * 0.0;
    }
    set_exponent(p, e) * sign
}

/// 0x82A02648, the exe's `pow(x, y)`. y 0 gives 1. A base of 0 gives the zero or infinity by y's sign and whether
/// it is an odd whole number; a NaN gives x + y; an infinity goes to `pow_inf`; a negative base needs a whole y
/// (odd turns the sign) else NaN; |y| past 1e20 gives 0, 1 or infinity from x alone. Else x = m 2^e: a whole x and a
/// whole y in 1..=128 multiply m up by squaring (0x82A027E0..0x82A02820); everything else takes the log2 of x
/// in two parts (a table of 2^(-i/16) centres, `ln` when x is within 2^(1/16) of 1), times y split by rounding
/// to sixteenths three times (0x82A02970..0x82A029D8), then 2^f from a degree-7 polynomial (0x82A02A18..0x82A02A80)
/// and the table. Results are put together by exponent field, so one below the smallest normal is 0.
pub fn pow(x: f64, y: f64) -> f64 {
    if y == 0.0 {
        return 1.0;
    }
    if x == 0.0 {
        let kind = whole_kind(y);
        if y < 0.0 {
            return if kind == 1 { f64::INFINITY.copysign(x) } else { f64::INFINITY };
        }
        if y > 0.0 {
            return if kind == 1 { x } else { 0.0 };
        }
    }
    if x.is_nan() || y.is_nan() {
        return x + y;
    }
    if x.is_infinite() || y.is_infinite() {
        return pow_inf(x, y);
    }
    let (mut x, mut sign) = (x, 1.0);
    if x < 0.0 {
        match whole_kind(y) {
            1 => sign = -1.0,
            2 => {}
            _ => return NAN,
        }
        x = -x;
    }
    if y.abs() > 1e20 {
        if y < 0.0 {
            x = 1.0 / x;
        }
        return if x > 1.0 { f64::INFINITY * sign } else if x < 1.0 { sign * 0.0 } else { sign };
    }
    let (m, e) = frexp(x);
    if y <= 128.0 && whole_kind(x) != 0 && whole_kind(y) != 0 && y > 0.0 {
        let (mut n, mut sq, mut p) = (y as i32, m, 1.0);
        let total = e.wrapping_mul(n);
        while n != 0 {
            if n & 1 != 0 {
                p *= sq;
            }
            sq *= sq;
            n >>= 1;
        }
        let r = exponent(p).wrapping_add(total);
        if r > 0xA00 {
            return f64::INFINITY * p * sign;
        }
        if r < -0x9FD {
            return p * sign * 0.0;
        }
        return pow_end(p, r, sign);
    }
    let table = POW2.map(f64::from_bits);
    // The centre i (odd, 1..=15) nearest m, by three compares (0x82A02854..0x82A028A0).
    let mut i = if m > table[8] { 1 } else { 9 };
    if !(m > table[i + 3]) {
        i += 4;
    }
    if !(m > table[i + 1]) {
        i += 2;
    }
    let p = 16 * e - i as i32;
    // log2 x = hi + lo: hi = p / 16, lo from the series on (m - c) / (m + c) (0x82A028E0..0x82A0296C).
    let (hi, lo);
    if p.abs() == 1 {
        hi = ln(x) * LOG2E;
        lo = 0.0;
    } else {
        let c = table[i];
        let s = (m - c - f64::from_bits(CORR[(i + 1) / 2])) / (c + m);
        let w = s * 2.0;
        let z = w * w;
        let t = z.mul_add(f64::from_bits(0x3F3C_78FD_DB4A_FC28), f64::from_bits(0x3F62_4924_2E27_8DAC));
        let t = t.mul_add(z, f64::from_bits(0x3F89_9999_999E_080E));
        let t = t.mul_add(z, f64::from_bits(0x3FB5_5555_5555_554D));
        hi = p as f64 * 0.0625;
        lo = (t * z * w).mul_add(LOG2E, w * f64::from_bits(0x3FDC_551D_94AE_0BF8)) + w;
    }
    // Round to a sixteenth through a 64-bit integer, as the exe's fctid / fcfid pairs do.
    let sixteenth = |v: f64| (v * 16.0).round_ties_even() as i64 as f64 * 0.0625;
    let yh = sixteenth(y);
    let f11 = (y - yh).mul_add(hi, lo * y);
    let f10 = sixteenth(f11);
    let f13 = yh.mul_add(hi, f10);
    let f11 = f11 - f10;
    let f12 = sixteenth(f13);
    let f13 = f13 - f12 + f11;
    let f11 = sixteenth(f13);
    let f12 = f12 + f11;
    let mut f13 = f13 - f11;
    let sixteenths = f12 * 16.0;
    if sixteenths > 40960.0 {
        return f64::INFINITY * sign;
    }
    if sixteenths < -40928.0 {
        return sign * 0.0;
    }
    let mut n = sixteenths as i32;
    if f13 > 0.0 {
        n += 1;
        f13 -= 0.0625;
    }
    // Whole 16ths of exponent into r30 (rounded toward 0, plus 1 from 0 up), the rest into the table index.
    let r30 = (n >= 0) as i32 + n / 16;
    let idx = (16 * r30 - n) as usize;
    let mut q = f13.mul_add(f64::from_bits(0x3EEF_4EDD_E392_CC80), f64::from_bits(0x3F24_2F7A_E038_4C74));
    for c in [0x3F55_D87E_18D7_CD9F, 0x3F83_B2AB_6E13_1D98, 0x3FAC_6B08_D703_026D, 0x3FCE_BFBD_FF82_C4CE, 0x3FE6_2E42_FEFA_39EF] {
        q = q.mul_add(f13, f64::from_bits(c));
    }
    let v = q.mul_add(f13, 1.0) * table[idx];
    pow_end(v, exponent(v) + r30, sign)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn close_to_the_exact_ones_and_odd_where_the_exe_is() {
        for i in -2000..2000 {
            let x = i as f64 * 0.0137;
            assert!((sin(x) - x.sin()).abs() < 1e-12, "{x}");
            assert!((cos(x) - x.cos()).abs() < 1e-12, "{x}");
            assert_eq!(floor(x), x.floor());
            assert_eq!(ceil(x), x.ceil());
        }
        assert!(ceil(-0.5).is_sign_positive());
        assert!(floor(-0.0).is_sign_negative());
        assert!(sin(3e8).is_nan());
        assert_eq!(cos(0.0), 1.0);
        for i in -100..=100 {
            let x = i as f64 * 0.01;
            assert!((asin(x) - x.asin()).abs() < 1e-12, "{x}");
        }
        assert!(asin(-0.0).is_sign_negative());
    }
}
