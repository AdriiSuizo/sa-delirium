//! The game's two random number generators, from the Xbox 360 exe (TU8 addresses).
//!
//! 1. The C runtime's `rand` (0x829FFB80; `srand` 0x829FFB50), which most of the game uses, through two
//!    helpers: a float in a range (0x82158E78, 145 callers) and an int in a range (0x82143C00, 122 callers).
//!    At start-up (0x821417C8) it is seeded with the low word of the CPU's time base, read until non-zero.
//! 2. The script commands' own generator (state at 0x82A9478C / 0x82A94790): a multiply-with-carry,
//!    GENERATE_RANDOM_INT (0x825D5000), _FLOAT (0x825D4FA0), _INT_IN_RANGE (0x825D5168),
//!    _FLOAT_IN_RANGE (0x825D50F8), SET_RANDOM_SEED (0x825D3928). The same start-up code seeds it from the
//!    same time base word.
//!
//! 3. The audio code's generator ([`AudioRand`], state at 0x82B1A3BC / 0x82B1A3C0, range helper 0x8290EE18):
//!    another multiply-with-carry. It starts from the two words the exe holds there (1 and 0x10001); whether
//!    anything seeds it afterwards is not read.
//!
//! Every operation here is in the game's order and precision: single-precision multiplies stay single, and
//! where the game uses a fused multiply-add (`fmadds`) so does this. A run seeded like the game's gives the
//! game's numbers.
//!
//! Checked against the exe's own code, run in `tools/xbox360-re/ppcrun.py`: the two range helpers
//! (`checks/rng_ranges.py`, 800 cases) and the script generator's seed and four draws (`checks/script_random.py`,
//! 2,400 draws) agree bit for bit, and the audio generator's range helper (`checks/sound_headers.py`). `rand` itself and the start-up seeding are not run there.

/// The C runtime's `rand`: one seed (per thread in the game; the game's threads each start at 1).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rand {
    pub seed: u32,
}

impl Default for Rand {
    /// The runtime's seed before any `srand`.
    fn default() -> Self {
        Rand { seed: 1 }
    }
}

impl Rand {
    /// `srand` (0x829FFB50).
    pub fn new(seed: u32) -> Self {
        Rand { seed }
    }

    /// `rand` (0x829FFB80): 0..=32767.
    pub fn next(&mut self) -> u32 {
        self.seed = self.seed.wrapping_mul(0x343FD).wrapping_add(0x269EC3);
        (self.seed >> 16) & 0x7FFF
    }

    /// A float from `min` to `max` (0x82158E78): `rand` times 1/32767 (the float 0x38000100), then
    /// `fmadds` with the range and `min`. `max` itself can come out.
    pub fn float_in_range(&mut self, min: f32, max: f32) -> f32 {
        float_from_draw(self.next(), min, max)
    }

    /// An int from `min` up to but not including `max` (0x82143C00): `rand` times 1/32768, times the
    /// range as a float, cut toward zero, plus `min`.
    pub fn int_in_range(&mut self, min: i32, max: i32) -> i32 {
        int_from_draw(self.next(), min, max)
    }
}

/// The script commands' generator: multiply-with-carry over two words.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ScriptRand {
    /// 0x82A9478C.
    pub low: u32,
    /// 0x82A94790: the carry.
    pub carry: u32,
}

impl ScriptRand {
    /// As the game's start-up leaves it (0x821417C8), from the time base word it read.
    pub fn at_startup(time_base: u32) -> Self {
        ScriptRand { low: if time_base == 0 { 1 } else { time_base }, carry: time_base }
    }

    /// SET_RANDOM_SEED (0x825D3928): the low word is the seed (1 for 0), the carry the seed xor itself
    /// rotated 16 bits. (The command also clears a byte and a float beside the state, 0x82A94794 and
    /// 0x82A94798, which these commands do not read.)
    pub fn seeded(seed: u32) -> Self {
        ScriptRand { low: if seed == 0 { 1 } else { seed }, carry: seed.rotate_left(16) ^ seed }
    }

    /// One step: low * 0x5CDCFAA7 + carry in 64 bits; the top half is the new carry.
    fn step(&mut self) -> u32 {
        let t = u64::from(self.low) * 0x5CDC_FAA7 + u64::from(self.carry);
        self.low = t as u32;
        self.carry = (t >> 32) as u32;
        self.low
    }

    /// GENERATE_RANDOM_INT (0x825D5000): 0..=32767.
    pub fn int(&mut self) -> i32 {
        (self.step() & 0x7FFF) as i32
    }

    /// GENERATE_RANDOM_FLOAT (0x825D4FA0): 23 bits times 2^-23, so 0 up to but not including 1.
    pub fn float(&mut self) -> f32 {
        (self.step() & 0x7F_FFFF) as f32 * f32::from_bits(0x3400_0000)
    }

    /// GENERATE_RANDOM_INT_IN_RANGE (0x825D5168): 31 bits times the range, shifted down 31, plus `min`.
    /// `max` itself never comes out.
    pub fn int_in_range(&mut self, min: i32, max: i32) -> i32 {
        let bits = i64::from(self.step() & 0x7FFF_FFFF);
        let range = i64::from(max.wrapping_sub(min));
        ((bits.wrapping_mul(range) as u64 >> 31) as u32 as i32).wrapping_add(min)
    }

    /// GENERATE_RANDOM_FLOAT_IN_RANGE (0x825D50F8): the float above, then `fmadds` with the range and `min`.
    pub fn float_in_range(&mut self, min: f32, max: f32) -> f32 {
        let unit = (self.step() & 0x7F_FFFF) as f32 * f32::from_bits(0x3400_0000);
        unit.mul_add(max - min, min)
    }
}

/// The float range helper's arithmetic on one `rand` draw (0x82158E78). Checked against the exe:
/// `tools/xbox360-re/checks/rng_ranges.py`.
pub fn float_from_draw(draw: u32, min: f32, max: f32) -> f32 {
    let unit = draw as f32 * f32::from_bits(0x3800_0100);
    unit.mul_add(max - min, min)
}

/// The int range helper's arithmetic on one `rand` draw (0x82143C00). Checked against the exe the same way.
pub fn int_from_draw(draw: u32, min: i32, max: i32) -> i32 {
    let unit = (draw & 0xFFFF) as f32 * f32::from_bits(0x3800_0000);
    let scaled = unit * max.wrapping_sub(min) as f32;
    (scaled as i32).wrapping_add(min)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rand_is_the_runtimes() {
        // The first values of this generator from seed 1, as every program built with this runtime prints.
        let mut r = Rand::default();
        let got: Vec<u32> = (0..5).map(|_| r.next()).collect();
        assert_eq!(got, [41, 18467, 6334, 26500, 19169]);
    }

    #[test]
    fn ranges() {
        let mut r = Rand::default();
        // 41 / 32767 of the way from 10 to 20, and 18467 / 32768 of 100 cut down.
        assert_eq!(r.float_in_range(10.0, 20.0), (41.0f32 * f32::from_bits(0x3800_0100)).mul_add(10.0, 10.0));
        assert_eq!(r.int_in_range(0, 100), 56);
        // The float helper can return max; the int helper never does.
        let mut top = Rand::new(0);
        while top.next() != 32767 {}
        let before = Rand::new(top.seed.wrapping_sub(0x269EC3).wrapping_mul(0xB9B33155)); // one step back
        assert_eq!({ let mut b = before; b.next() }, 32767);
        assert_eq!({ let mut b = before; b.float_in_range(0.0, 1.0) }, 1.0);
        assert_eq!({ let mut b = before; b.int_in_range(0, 10) }, 9);
    }

    /// 0x8290EEE0: 1 and over (and NaN) without a draw; else one step, its low 23 bits x 2^-23 under the chance.
    #[test]
    fn audio_chance() {
        let mut r = AudioRand::default();
        assert!(r.chance(1.0) && r.chance(f32::NAN));
        assert_eq!(r, AudioRand::default());
        // The first step from (1, 0x10001): 0x5CDCFAA7 + 0x10001 = 0x5CDDFAA8, low 23 bits 0x5DFAA8.
        let first = 0x5D_FAA8 as f32 / 8_388_608.0;
        assert!(!{ let mut a = AudioRand::default(); a.chance(first) });
        assert!({ let mut a = AudioRand::default(); a.chance(first + 1e-6) });
        assert!(!r.chance(0.0));
        assert_eq!(r, AudioRand { value: 0x5CDD_FAA8, carry: 0 });
    }

    #[test]
    fn script_generator() {
        let mut s = ScriptRand::seeded(0);
        assert_eq!(s, ScriptRand { low: 1, carry: 0 });
        assert_eq!(s.int(), (0x5CDC_FAA7u32 & 0x7FFF) as i32);
        assert_eq!(s, ScriptRand { low: 0x5CDC_FAA7, carry: 0 });
        // Second step by hand: 0x5CDCFAA7 squared.
        let t = 0x5CDC_FAA7u64 * 0x5CDC_FAA7;
        s.int();
        assert_eq!(s, ScriptRand { low: t as u32, carry: (t >> 32) as u32 });
        let seeded = ScriptRand::seeded(0x1234_5678);
        assert_eq!(seeded, ScriptRand { low: 0x1234_5678, carry: 0x5678_1234 ^ 0x1234_5678 });
        // Ranges stay inside, and a negative range works as the 64-bit shift makes it.
        let mut r = ScriptRand::seeded(7);
        for _ in 0..10_000 {
            let i = r.int_in_range(-5, 5);
            assert!((-5..5).contains(&i));
            let f = r.float_in_range(2.0, 3.0);
            assert!((2.0..3.0).contains(&f));
            let u = r.float();
            assert!((0.0..1.0).contains(&u));
        }
    }
}

/// The audio code's generator: multiply-with-carry over two words.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AudioRand {
    pub value: u32,
    pub carry: u32,
}

impl Default for AudioRand {
    /// The two words as the exe holds them before anything runs.
    fn default() -> Self {
        AudioRand { value: 1, carry: 0x10001 }
    }
}

impl AudioRand {
    /// An int from `min` to `max`, both included (0x8290EE18): the next value's low 31 bits, modulo the
    /// range.
    ///
    /// # Panics
    /// Like the original trap, a zero wrapping width (`max - min + 1`) fails
    /// before either state word changes, including the complete i32 interval.
    pub fn int_in_range(&mut self, min: i32, max: i32) -> i32 {
        let range = max.wrapping_sub(min).wrapping_add(1);
        assert_ne!(range, 0, "audio random integer range has zero wrapping width");
        let x = (self.value as u64).wrapping_mul(0x5CDC_FAA7).wrapping_add(self.carry as u64);
        (self.value, self.carry) = (x as u32, (x >> 32) as u32);
        ((self.value & 0x7FFF_FFFF) as i32).wrapping_rem(range).wrapping_add(min)
    }

    /// A chance of `p` (0x8290EEE0): true without a draw when `p` is 1 or more (or NaN: `blt` is not taken), else the
    /// next value's low 23 bits times 2^-23 under `p`.
    pub fn chance(&mut self, p: f32) -> bool {
        if !(p < 1.0) {
            return true;
        }
        let x = (self.value as u64).wrapping_mul(0x5CDC_FAA7).wrapping_add(self.carry as u64);
        (self.value, self.carry) = (x as u32, (x >> 32) as u32);
        ((self.value & 0x7F_FFFF) as f32) * f32::from_bits(0x3400_0000) < p
    }

    /// A float from `min` to `max` (0x8290EE80): the next value's low 23 bits times the range, then
    /// `fmadds` with 2^-23 and `min`.
    pub fn float_in_range(&mut self, min: f32, max: f32) -> f32 {
        let x = (self.value as u64).wrapping_mul(0x5CDC_FAA7).wrapping_add(self.carry as u64);
        (self.value, self.carry) = (x as u32, (x >> 32) as u32);
        ((max - min) * (self.value & 0x7F_FFFF) as f32).mul_add(1.0 / 8_388_608.0, min)
    }
}
