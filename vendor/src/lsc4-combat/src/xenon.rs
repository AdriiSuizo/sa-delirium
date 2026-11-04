//! The Xbox 360 CPU's estimate instructions as the console computes them. The recompiled game works them out exactly
//! instead, so a reference made with it is a few bits off the console wherever the game uses one.
//!
//! vrsqrtefp's table and interpolation are Xenia's (has207/xenia-edge `src/xenia/cpu/backend/vrsqrte_table.cc`,
//! Copyright 2026 Ben Vanik, BSD licence, in NOTICE.md), checked there against a real Xbox 360 for positive normal
//! inputs. Its special values follow the PowerPC rules with the vector unit's denormals as zero.
//!
//! frsqrte is Xenia's too (has207/xenia-edge `src/xenia/cpu/backend/x64/x64_backend.cc`, `EmitFrsqrteHelper`: its
//! 16-entry table and special values, same licence), which gives a real Xbox 360's answers in
//! `src/xenia/cpu/ppc/testing/instr__gen_frsqrte.s` (zero, denormals, infinities and NaNs included).
//!
//! vrefp's table and lookup rule are izcarti's, measured on a real PS3, whose Cell has the same vector unit (RPCS3
//! `rpcs3/Emu/Cell/Common.h`, `ppu_vrefp_segments` / `ppu_vrefp`, commit cb450d7e8), given to this project under MIT
//! (RPCS3 PR #19783, 2026-10-15; NOTICE.md). It gives a real Xbox 360's answers in xenia-edge's
//! `src/xenia/cpu/ppc/testing/instr__gen_vrefp.s`.

const RSQRTE: [u32; 32] = [
    0x0568B4FD, 0x04F3AF97, 0x048DAAA5, 0x0435A618, 0x03E7A1E4, 0x03A29DFE, 0x03659A5C, 0x032E96F8,
    0x02FC93CA, 0x02D090CE, 0x02A88DFE, 0x02838B57, 0x026188D4, 0x02438673, 0x02268431, 0x020B820B,
    0x03D27FFA, 0x03807C29, 0x033878AA, 0x02F97572, 0x02C27279, 0x02926FB7, 0x02666D26, 0x023F6AC0,
    0x021D6881, 0x01FD6665, 0x01E16468, 0x01C76287, 0x01AF60C1, 0x01995F12, 0x01855D79, 0x01735BF4,
];

/// vrsqrtefp on one lane: 1 / sqrt(x) to about 12 bits.
pub fn vrsqrtefp(x: f32) -> f32 {
    let b = x.to_bits();
    let (neg, e, m) = (b >> 31 != 0, (b >> 23) & 0xFF, b & 0x7F_FFFF);
    let r = if e == 0xFF {
        if m != 0 { b | 0x40_0000 } else if neg { 0x7FC0_0000 } else { 0 }
    } else if e == 0 {
        if neg { 0xFF80_0000 } else { 0x7F80_0000 }
    } else if neg {
        0x7FC0_0000
    } else {
        // The table's value for an exponent of 126 or 127 (the same parity), then moved to x's.
        let c = RSQRTE[(((e & 1) << 4) | (m >> 19)) as usize];
        let mut est = ((c << 10) & 0x3FF_FC00).wrapping_sub(((m >> 9) & 1023) * (c >> 16));
        let mut adj = 0i32;
        if est & 0x200_0000 == 0 {
            let lz = (est & 0x1FF_FFFF).leading_zeros();
            adj += 6 - lz as i32;
            est <<= lz - 6;
        }
        if est & 5 != 0 && est & 2 != 0 {
            est += 4;
        }
        let r = 0x3F80_0000u32.wrapping_add((adj as u32).wrapping_mul(0x80_0000)) | ((est >> 2) & 0x7F_FFFF);
        r.wrapping_sub(((b >> 24).wrapping_sub(63)) << 23)
    };
    f32::from_bits(r)
}

const VREFP: [[u32; 2]; 32] = [
    [0x7FF800, 0x3E1], [0x783800, 0x3A7], [0x70EA00, 0x371], [0x6A0800, 0x340], [0x638800, 0x313], [0x5D6200, 0x2EA],
    [0x579000, 0x2C4], [0x520800, 0x2A0], [0x4CC800, 0x27F], [0x47CA00, 0x261], [0x430800, 0x245], [0x3E8000, 0x22A],
    [0x3A2C00, 0x212], [0x360800, 0x1FB], [0x321400, 0x1E5], [0x2E4A00, 0x1D1], [0x2AA800, 0x1BE], [0x272C00, 0x1AC],
    [0x23D600, 0x19B], [0x209E00, 0x18B], [0x1D8800, 0x17C], [0x1A9000, 0x16E], [0x17AE00, 0x15B], [0x14F800, 0x15B],
    [0x124400, 0x143], [0x0FBE00, 0x143], [0x0D3800, 0x12D], [0x0ADE00, 0x12D], [0x088400, 0x11A], [0x065000, 0x11A],
    [0x041C00, 0x108], [0x020C00, 0x106],
];

/// -x kept a negation of the rounded value, as fnmadds / fnmsubs negate theirs: arm64's LLVM folds -(a.mul_add(b, c))
/// into fnmadd (and its kin), which negates the inputs instead, so an exact zero comes out +0 where the console gives -0.
pub fn fneg<T: Fneg>(x: T) -> T {
    x.fneg()
}

pub trait Fneg {
    fn fneg(self) -> Self;
}
impl Fneg for f32 {
    #[inline(always)]
    fn fneg(self) -> f32 {
        #[cfg(target_arch = "aarch64")]
        {
            let r: f32;
            unsafe { core::arch::asm!("fneg {0:s}, {1:s}", out(vreg) r, in(vreg) self, options(pure, nomem, nostack)) };
            r
        }
        #[cfg(not(target_arch = "aarch64"))]
        -self
    }
}
impl Fneg for f64 {
    #[inline(always)]
    fn fneg(self) -> f64 {
        #[cfg(target_arch = "aarch64")]
        {
            let r: f64;
            unsafe { core::arch::asm!("fneg {0:d}, {1:d}", out(vreg) r, in(vreg) self, options(pure, nomem, nostack)) };
            r
        }
        #[cfg(not(target_arch = "aarch64"))]
        -self
    }
}
impl<T: Fneg, const N: usize> Fneg for [T; N] {
    fn fneg(self) -> Self {
        self.map(Fneg::fneg)
    }
}

/// vrefp on one lane: 1 / x to about 12 bits, in the vector unit's non-Java mode (a denormal in or out is zero).
/// The console's own results (`vrefp_as_the_console`) are in that mode; Java mode (denormals kept) isn't here.
pub fn vrefp(x: f32) -> f32 {
    let b = x.to_bits();
    let (s, e, m) = (b & 0x8000_0000, (b >> 23) & 0xFF, b & 0x7F_FFFF);
    let r = if e == 0xFF {
        if m != 0 { b | 0x40_0000 } else { s }
    } else if e == 0 {
        s | 0x7F80_0000
    } else if e >= 253 {
        s
    } else {
        // A line in each of 32 segments (the mantissa's top 5 bits), along its next 9; the exponent mirrored.
        let [base, slope] = VREFP[(m >> 18) as usize];
        s | (253 - e) << 23 | (base - slope * ((m >> 9) & 0x1FF))
    };
    f32::from_bits(r)
}

/// frsqrte: 1 / sqrt(x) to about 5 bits, in double. The FPU's IEEE mode: a denormal is scaled, not flushed.
pub fn frsqrte(x: f64) -> f64 {
    const TABLE: [u64; 16] = [241, 216, 192, 168, 152, 136, 128, 112, 96, 76, 60, 48, 32, 24, 16, 8];
    const EXP: u64 = 0x7FF0_0000_0000_0000;
    const QNAN: u64 = 0x7FF8_0000_0000_0000;
    let b = x.to_bits();
    let r = if b << 1 == 0 {
        b & 1 << 63 | EXP
    } else if !b & EXP == 0 {
        if b == EXP { 0 } else if b << 12 == 0 { QNAN } else { b | QNAN }
    } else if b >> 63 != 0 {
        QNAN
    } else {
        let (mut e, mut m) = ((b >> 52) as i32, b & 0xF_FFFF_FFFF_FFFF);
        if e == 0 {
            // A denormal, normalised (its exponent goes below 1).
            let lz = m.leading_zeros();
            m <<= lz - 11;
            e = 12 - lz as i32;
        }
        // The table keys on the exponent's parity and the top 3 mantissa bits; the exponent halves.
        let i = ((m >> 49) as usize & 7 | (e as usize & 1) << 3) ^ 8;
        TABLE[i] << 44 | ((1022 - ((e - 1023) >> 1)) as u64) << 52
    };
    f64::from_bits(r)
}

#[cfg(test)]
mod tests {
    use super::{frsqrte, vrefp, vrsqrtefp};

    #[test]
    fn vrefp_as_the_console() {
        // A real Xbox 360's answers (xenia-edge src/xenia/cpu/ppc/testing/instr__gen_vrefp.s, each lane once).
        let console: [(u32, u32); 65] = [
            (0x00000000, 0x7F800000), (0x00000001, 0x7F800000), (0x0000007F, 0x7F800000), (0x0000FF00, 0x7F800000),
            (0x0000FFFF, 0x7F800000), (0x00010080, 0x7F800000), (0x00010203, 0x7F800000), (0x00020003, 0x7F800000),
            (0x00020081, 0x7F800000), (0x00030082, 0x7F800000), (0x000D000D, 0x7F800000), (0x00112233, 0x7F800000),
            (0x00FFFF00, 0x7E000106), (0x04010203, 0x7AFE039F), (0x04040404, 0x7AF830B2), (0x04050607, 0x7AF6598B),
            (0x07070707, 0x77F2B28B), (0x08080808, 0x76F0DC3C), (0x08090A0B, 0x76EF204B), (0x0C0D0E0F, 0x72E85140),
            (0x11223344, 0x6DCA0A99), (0x12121212, 0x6CE05955), (0x12345678, 0x6CB5B2D7), (0x3F800000, 0x3F7FF800),
            (0x3F8CCCCD, 0x3F68BC80), (0x3FC00000, 0x3F2AA800), (0x3FF33333, 0x3F06C176), (0x41200000, 0x3DCCC800),
            (0x41700000, 0x3D888400), (0x41A00000, 0x3D4CC800), (0x41F00000, 0x3D088400), (0x44556677, 0x3A999016),
            (0x55667788, 0x298E308F), (0x7070FFFF, 0x0E87F81A), (0x7F800203, 0x7FC00203), (0x7F800A0B, 0x7FC00A0B),
            (0x7FFFFFFF, 0x7FFFFFFF), (0x80081010, 0xFF800000), (0x808F0000, 0xFE652800), (0x87654321, 0xF78EF2DD),
            (0x8899AABB, 0xF65542EC), (0x8FFFFFFF, 0xEF000106), (0xBF800000, 0xBF7FF800), (0xBF8CCCCD, 0xBF68BC80),
            (0xBFC00000, 0xBF2AA800), (0xBFF33333, 0xBF06C176), (0xC1200000, 0xBDCCC800), (0xC1700000, 0xBD888400),
            (0xC1A00000, 0xBD4CC800), (0xCCDDEEFF, 0xB213A933), (0xCDCDCDCD, 0xB11F3B1E), (0xF8F9FAFB, 0x86031718),
            (0xFCFDFEFF, 0x82010706), (0xFEFEFEFE, 0x80000000), (0xFF0000FF, 0x80000000), (0xFFFCFF7D, 0xFFFCFF7D),
            (0xFFFCFFFD, 0xFFFCFFFD), (0xFFFDFF7E, 0xFFFDFF7E), (0xFFFEFF7F, 0xFFFEFF7F), (0xFFFEFFFF, 0xFFFEFFFF),
            (0xFFFF0000, 0xFFFF0000), (0xFFFF00FF, 0xFFFF00FF), (0xFFFF0101, 0xFFFF0101), (0xFFFFFF80, 0xFFFFFF80),
            (0xFFFFFFFF, 0xFFFFFFFF),
        ];
        for (x, want) in console {
            assert_eq!(vrefp(f32::from_bits(x)).to_bits(), want, "{x:08X}");
        }
        // VMX promises 12 bits; every 4099th mantissa of every exponent with a normal answer.
        for e in 1..253u32 {
            for m in (0..1u32 << 23).step_by(4099) {
                let x = f32::from_bits(e << 23 | m);
                assert!((vrefp(x) as f64 * x as f64 - 1.0).abs() < 1.0 / 4096.0, "{x}: {}", vrefp(x));
            }
        }
    }

    #[test]
    fn frsqrte_as_the_console() {
        // A real Xbox 360's answers (xenia-edge src/xenia/cpu/ppc/testing/instr__gen_frsqrte.s).
        let console: [(u64, u64); 12] = [
            (0x0000000000000000, 0x7FF0000000000000), (0x8000000000000000, 0xFFF0000000000000),
            (0x0000000000000001, 0x617F100000000000), (0x000FFFFFFFFFFFFF, 0x5FE0800000000000),
            (0x3FF0000000000000, 0x3FEF100000000000), (0xBFF0000000000000, 0x7FF8000000000000),
            (0xC1E0000000000000, 0x7FF8000000000000), (0x41DFFFFFFFC00000, 0x3EF7000000000000),
            (0x7FF0000000000000, 0x0000000000000000), (0xFFF0000000000000, 0x7FF8000000000000),
            (0xFFF8000000000000, 0xFFF8000000000000), (0xFFF4000000000000, 0xFFFC000000000000),
        ];
        for (x, want) in console {
            assert_eq!(frsqrte(f64::from_bits(x)).to_bits(), want, "{x:016X}");
        }
        // PowerPC promises 1/32; four times the input, half the estimate.
        for x in [1.0f64, 1.3, 2.0, 2.9, 3.99, 1e-300, 7e300] {
            assert!((frsqrte(x) * x.sqrt() - 1.0).abs() < 1.0 / 32.0, "{x}");
            assert_eq!(frsqrte(4.0 * x), 0.5 * frsqrte(x));
        }
    }

    #[test]
    fn rsqrte_as_the_console() {
        // VMX promises 12 bits; every 4099th mantissa of every exponent.
        for e in 1..255u32 {
            for m in (0..1u32 << 23).step_by(4099) {
                let x = f32::from_bits(e << 23 | m);
                let r = vrsqrtefp(x) as f64 * (x as f64).sqrt();
                assert!((r - 1.0).abs() < 1.0 / 4096.0, "{x}: {}", vrsqrtefp(x));
            }
        }
        // Four times the input, half the estimate, bit for bit (the table keys on the exponent's parity).
        for x in [1.0f32, 1.7, 2.3, 3.99, 1e-20, 7e30] {
            assert_eq!(vrsqrtefp(4.0 * x), 0.5 * vrsqrtefp(x));
        }
        // A real Xbox 360's answers (xenia-edge src/xenia/cpu/ppc/testing/instr_vrsqrtefp.s).
        let console: [(f32, u32); 20] = [
            (1.0, 0x3F7FF400), (2.0, 0x3F34FD00), (4.0, 0x3EFFF400), (8.0, 0x3EB4FD00), (3.0, 0x3F13CA00),
            (5.0, 0x3EE4F200), (10.0, 0x3EA1E400), (16.0, 0x3E7FF400), (18.0, 0x3E715400), (20.0, 0x3E64F200),
            (22.0, 0x3E5A4C00), (24.0, 0x3E510200), (0.25, 0x3FFFF400), (0.5, 0x3FB4FD00), (0.75, 0x3F93CA00),
            (0.875, 0x3F88D400), (50.0, 0x3E10CE00), (100.0, 0x3DCCCA00), (200.0, 0x3D90CE00), (400.0, 0x3D4CCA00),
        ];
        for (x, want) in console {
            assert_eq!(vrsqrtefp(x).to_bits(), want, "{x}");
        }
        assert_eq!(vrsqrtefp(1.5).to_bits(), 0x3F51_0200); // as the oracle's console copy (x360_estimate.h)
        assert_eq!(vrsqrtefp(0.0), f32::INFINITY);
        assert_eq!(vrsqrtefp(-0.0), f32::NEG_INFINITY);
        assert_eq!(vrsqrtefp(f32::from_bits(1)), f32::INFINITY);
        assert_eq!(vrsqrtefp(f32::INFINITY), 0.0);
        assert!(vrsqrtefp(-1.0).is_nan() && vrsqrtefp(f32::NEG_INFINITY).is_nan() && vrsqrtefp(f32::NAN).is_nan());
    }
}
