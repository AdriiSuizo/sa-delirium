//! The control map, from the Xbox 360 exe (TU8 addresses; board row I1d).
//!
//! The game reads its controls from a controls object (the player's comes from 0x821B41E8) holding four RAGE input
//! mappers (rage::ioMapper, at +0x000, +0x308, +0x610 and +0xD58) and the controls themselves, each a 12-byte
//! RAGE value (rage::ioValue) at +0x918 .. +0xD14. Its set-up (0x822B6560) empties the four mappers (their controls put back to rest, 0x828D2F00, and the count to 0) and
//! binds each control to a pad source (ioMapper::Map 0x828D2E18): a button (source 6, by its RAGE button bit), an
//! analog button (8, by its pressure byte's number) or an axis (9, by stick number 0..3). Which source feeds which
//! control depends on whether a controller is on the pad and, if one is, on the setting at 0x82BF9F24. Last it puts
//! 0x40 and 0x20 in the words at +0x1060 and +0x1064.
//!
//! Controls are named by their place in the object, not by what the game does with them: that is for the code
//! that reads them. Not here yet: the mappers' update, which gives each control its value every frame, and the
//! readers (0x822B7958, 0x822B7A50, 0x822B7BA0).
//!
//! The mappers themselves ([`IoMapper`], board row I1e): binding (0x828D2E18), putting the controls back to rest
//! (0x828D2F00) and the frame's update (0x828D3058), which reads every source kind RAGE has: keys, the mouse, and
//! the pads' buttons, pressure bytes, sticks and the sticks' eight directions.
//!
//! The controls object itself ([`Controls`], board row I1f): set up for a pad (0x822B7CD8), its frame update
//! (0x822B7DD0: the mappers, and the last time a control was in use) and the controls reset (0x822B74C0).

/// A pad source a control can be bound to.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Source {
    /// Source 6: a RAGE button bit (see `pad::RagePad::buttons`).
    Button(u32),
    /// Source 8: an analog button by its pressure byte's number.
    Analog(u32),
    /// Source 9: a stick axis, 0..3.
    Axis(u32),
}

impl Source {
    /// The source's kind and number as the exe passes them to ioMapper::Map.
    pub fn code(self) -> (u32, u32) {
        match self {
            Source::Button(b) => (6, b),
            Source::Analog(i) => (8, i),
            Source::Axis(i) => (9, i),
        }
    }
}

/// A mapper, by its place in the controls object.
pub type Mapper = u32;
pub const M0: Mapper = 0x000;
pub const M1: Mapper = 0x308;
pub const M2: Mapper = 0x610;
pub const M3: Mapper = 0xD58;
/// The four mappers in the order the set-up clears them.
pub const MAPPERS: [Mapper; 4] = [M0, M1, M2, M3];

/// One binding: in `mapper`, `source` feeds the control at `control` (its place in the controls object).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Binding {
    pub mapper: Mapper,
    pub source: Source,
    pub control: u32,
}

const fn b(mapper: Mapper, source: Source, control: u32) -> Binding {
    Binding { mapper, source, control }
}
use Source::{Analog, Axis, Button};

/// No controller on the pad (0x82208C50 false; 0x82165060 is always false on the 360).
const NO_PAD: &[Binding] = &[
    b(M0, Button(0x100), 0x918),
    b(M0, Button(0x40), 0x924),
    b(M0, Button(0x80), 0x930),
    b(M0, Button(0x20), 0x948),
    b(M0, Button(0x4), 0x954),
    b(M0, Button(0x10), 0x93C),
    b(M0, Button(0x8), 0x960),
    b(M0, Button(0x4), 0xA2C),
    b(M0, Button(0x2), 0x978),
    b(M0, Button(0x1), 0x984),
    b(M0, Button(0x200), 0xA08),
    b(M0, Button(0x400), 0x96C),
    b(M0, Button(0x8), 0xA68),
    b(M0, Button(0x20), 0xA74),
    b(M0, Button(0x1000), 0xA14),
    b(M0, Button(0x4000), 0xA20),
    b(M0, Axis(0x1), 0xA38),
    b(M0, Axis(0x1), 0xA44),
    b(M0, Axis(0x3), 0xA50),
    b(M0, Axis(0x3), 0xA5C),
    b(M0, Axis(0x0), 0x9A8),
    b(M0, Axis(0x0), 0x9B4),
    b(M0, Axis(0x1), 0x9C0),
    b(M0, Axis(0x1), 0x9CC),
    b(M0, Axis(0x2), 0x9D8),
    b(M0, Axis(0x2), 0x9E4),
    b(M0, Axis(0x3), 0x9F0),
    b(M0, Axis(0x3), 0x9FC),
    b(M1, Button(0x20), 0xBDC),
    b(M1, Button(0x10), 0xBE8),
    b(M1, Button(0x2), 0xBF4),
    b(M0, Button(0x4), 0xD14),
    b(M1, Button(0x80), 0xC00),
    b(M1, Button(0x40), 0xC0C),
    b(M2, Axis(0x0), 0xA80),
    b(M2, Axis(0x0), 0xA8C),
    b(M2, Axis(0x1), 0xA98),
    b(M2, Axis(0x1), 0xAA4),
    b(M2, Axis(0x2), 0xAB0),
    b(M2, Axis(0x2), 0xABC),
    b(M2, Axis(0x3), 0xAC8),
    b(M2, Axis(0x3), 0xAD4),
    b(M2, Button(0x4), 0xAE0),
    b(M2, Button(0x4), 0xAEC),
    b(M2, Button(0x10), 0xB1C),
    b(M2, Button(0x80), 0xB10),
    b(M2, Button(0x8), 0xB28),
    b(M2, Button(0x20), 0xB7C),
    b(M2, Button(0x400), 0xB70),
    b(M2, Button(0x2000), 0xB88),
    b(M2, Button(0x8000), 0xB94),
    b(M2, Button(0x200), 0xBA0),
    b(M2, Button(0x40), 0xAF8),
    b(M2, Button(0x80), 0xB04),
    b(M2, Button(0x40), 0xBAC),
    b(M2, Button(0x80), 0xBB8),
    b(M2, Button(0x1), 0xBC4),
];

/// A controller on the pad: first part, whatever the setting.
const PAD: &[Binding] = &[
    b(M0, Axis(0x0), 0x9A8),
    b(M0, Axis(0x0), 0x9B4),
    b(M0, Axis(0x1), 0x9C0),
    b(M0, Axis(0x1), 0x9CC),
    b(M0, Axis(0x2), 0x9D8),
    b(M0, Axis(0x2), 0x9E4),
    b(M0, Axis(0x3), 0x9F0),
    b(M0, Axis(0x3), 0x9FC),
    b(M0, Button(0x100), 0x918),
    b(M0, Button(0x40), 0x924),
    b(M0, Button(0x80), 0x930),
    b(M0, Button(0x10), 0x93C),
    b(M0, Button(0x200), 0xA08),
    b(M0, Button(0x400), 0x96C),
    b(M0, Button(0x2000), 0x978),
    b(M0, Button(0x8000), 0x984),
    b(M0, Axis(0x1), 0xA38),
    b(M0, Axis(0x1), 0xA44),
    b(M0, Axis(0x3), 0xA50),
    b(M0, Axis(0x3), 0xA5C),
    b(M0, Button(0x1000), 0xA14),
    b(M0, Button(0x4000), 0xA20),
    b(M1, Button(0x20), 0xBDC),
    b(M1, Button(0x10), 0xBE8),
    b(M1, Button(0x80), 0xC00),
    b(M1, Button(0x40), 0xC0C),
    b(M2, Axis(0x0), 0xA80),
    b(M2, Axis(0x0), 0xA8C),
    b(M2, Axis(0x1), 0xA98),
    b(M2, Axis(0x1), 0xAA4),
    b(M2, Axis(0x2), 0xAB0),
    b(M2, Axis(0x2), 0xABC),
    b(M2, Axis(0x3), 0xAC8),
    b(M2, Axis(0x3), 0xAD4),
    b(M2, Button(0x10), 0xB1C),
    b(M2, Button(0x400), 0xB70),
    b(M2, Button(0x2000), 0xB88),
    b(M2, Button(0x8000), 0xB94),
    b(M2, Button(0x200), 0xBA0),
];

/// A controller, the setting 0x82BF9F24 at 0: the triggers (analog buttons 10, 11) bound as analog.
const PAD_ANALOG_TRIGGERS: &[Binding] = &[
    b(M0, Analog(0xA), 0x960),
    b(M0, Analog(0xB), 0x948),
    b(M0, Analog(0xB), 0x954),
    b(M0, Button(0x4), 0xA2C),
    b(M0, Button(0x8), 0xA68),
    b(M0, Button(0x20), 0xA74),
    b(M1, Analog(0xB), 0xBF4),
    b(M1, Analog(0xB), 0xD14),
    b(M2, Analog(0xB), 0xAF8),
    b(M2, Analog(0xA), 0xB04),
    b(M2, Button(0x8), 0xB28),
    b(M2, Button(0x40), 0xB34),
    b(M2, Button(0x4), 0xAE0),
    b(M2, Button(0x80), 0xAEC),
    b(M2, Button(0x80), 0xB10),
    b(M2, Button(0x20), 0xB7C),
    b(M2, Analog(0xA), 0xB40),
    b(M2, Analog(0xB), 0xB4C),
    b(M2, Analog(0xB), 0xBAC),
    b(M2, Analog(0xA), 0xBB8),
    b(M2, Button(0x4), 0xBC4),
];

/// A controller, the setting 0x82BF9F24 not 0: what the triggers did goes to buttons.
const PAD_BUTTON_TRIGGERS: &[Binding] = &[
    b(M0, Button(0x8), 0x960),
    b(M0, Button(0x4), 0x948),
    b(M0, Button(0x20), 0x954),
    b(M0, Button(0x1), 0xA2C),
    b(M0, Button(0x2), 0xA68),
    b(M1, Button(0x4), 0xBF4),
    b(M1, Button(0x20), 0xD14),
    b(M2, Button(0x40), 0xAF8),
    b(M2, Button(0x80), 0xB04),
    b(M2, Button(0x8), 0xB28),
    b(M2, Button(0x8), 0xB34),
    b(M2, Button(0x4), 0xAE0),
    b(M2, Button(0x4), 0xAEC),
    b(M2, Button(0x1), 0xB10),
    b(M2, Button(0x20), 0xB7C),
    b(M2, Button(0x40), 0xB40),
    b(M2, Button(0x80), 0xB4C),
    b(M2, Button(0x40), 0xBAC),
    b(M2, Button(0x80), 0xBB8),
    b(M2, Analog(0xA), 0xBC4),
];

/// Every layout, after the one binding that differs (see `bindings`): the fourth mapper.
const TAIL: &[Binding] = &[
    b(M3, Button(0x800), 0xCA8),
    b(M3, Button(0x1000), 0xC24),
    b(M3, Button(0x4000), 0xC18),
    b(M3, Button(0x8000), 0xC30),
    b(M3, Button(0x2000), 0xC3C),
    b(M3, Axis(0x0), 0xC78),
    b(M3, Axis(0x1), 0xC84),
    b(M3, Axis(0x2), 0xC90),
    b(M3, Axis(0x3), 0xC9C),
    b(M3, Button(0x40), 0xCB4),
    b(M3, Button(0x20), 0xCC0),
    b(M3, Button(0x80), 0xCCC),
    b(M3, Button(0x10), 0xCD8),
    b(M3, Button(0x4), 0xCE4),
    b(M3, Button(0x8), 0xCF0),
    b(M3, Button(0x1), 0xCFC),
    b(M3, Button(0x2), 0xD08),
];

/// The words the set-up leaves at +0x1060 and +0x1064.
pub const SETUP_WORDS: [u32; 2] = [0x40, 0x20];

/// The bindings the set-up makes (0x822B6560), in its order: the layout's, then the control at +0xBD0 in the third
/// mapper (button 2 with no controller; button 8, or analog button 11 when the setting is not 0, with one), then the
/// fourth mapper's, the same in every layout.
pub fn bindings(controller: bool, setting: u32) -> Vec<Binding> {
    let mut v = vec![];
    let last = if !controller {
        v.extend_from_slice(NO_PAD);
        Button(0x2)
    } else {
        v.extend_from_slice(PAD);
        if setting == 0 {
            v.extend_from_slice(PAD_ANALOG_TRIGGERS);
            Button(0x8)
        } else {
            v.extend_from_slice(PAD_BUTTON_TRIGGERS);
            Analog(0xB)
        }
    };
    v.push(b(M2, last, 0xBD0));
    v.extend_from_slice(TAIL);
    v
}


/// One control's value, RAGE's ioValue (12 bytes in the controls object).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct IoValue {
    /// Xored into the value when it is read and compared (+0x00): 0, or 0xFF to turn the control round.
    pub invert: u8,
    /// The mapper update pass that last started this value's frame (+0x01).
    pub stamp: u8,
    /// The value as stored (+0x02; read it with [`IoValue::get`]) and last frame's (+0x03).
    pub value: u8,
    pub last: u8,
    /// Where in `history` this frame's entry is (+0x04).
    pub history_at: u8,
    /// Optional: the last 64 frames' values with their times (+0x08, 8 bytes each: the value, then the time).
    pub history: Option<Vec<(u8, u32)>>,
}

impl IoValue {
    pub fn get(&self) -> u8 {
        self.value ^ self.invert
    }

    /// The control as a stick, -1..1 past the 0.25 dead zone (0x822B7A50): (value - 127.5) / 127.5 within -1..1,
    /// single precision.
    pub fn stick(&self) -> f32 {
        let v = ((self.get() as f32 - f32::from_bits(0x42FF_0000)) * f32::from_bits(0x3C00_8081)).clamp(-1.0, 1.0);
        crate::pad::dead_zone(v, READ_DEAD_ZONE)
    }

    /// The same as -127..127, cut toward zero (0x822B7958).
    pub fn stick_int(&self) -> i32 {
        (self.stick() * f32::from_bits(0x42FF_0000)) as i32
    }

    /// The control as a button, 0..1 past the 0.25 dead zone (0x822B7BA0): value / 255.
    pub fn button(&self) -> f32 {
        crate::pad::dead_zone(self.get() as f32 * f32::from_bits(0x3B80_8081), READ_DEAD_ZONE)
    }
}

/// The dead zone the control readers give (0x8200BEFC).
pub const READ_DEAD_ZONE: f32 = 0.25;

/// The controls bound two to one axis (see the control map), and the readers that take one of each pair
/// (0x822B7FC0, 0x822B8018, 0x822B80C8, 0x822B8070, 0x822B8120, 0x822B8178, 0x822B81D0, 0x822B8228): the first
/// while its stick reads below 0, else the second.
pub const AXIS_PAIRS: [(u32, u32); 8] =
    [(0x9A8, 0x9B4), (0x9C0, 0x9CC), (0x9D8, 0x9E4), (0x9F0, 0x9FC), (0xA80, 0xA8C), (0xA98, 0xAA4), (0xAB0, 0xABC), (0xAC8, 0xAD4)];

/// Which control of a pair the readers take: `first`'s value decides.
pub fn pick(pair: (u32, u32), first: &IoValue) -> u32 {
    if first.stick_int() < 0 { pair.0 } else { pair.1 }
}

/// What RAGE's input reads, as the mapper update sees it (0x828D3058).
#[derive(Clone, Debug, PartialEq)]
pub struct RageInput {
    /// The four pads (0x831C4FF8 + 0x38 x pad: +0x04 buttons, +0x0C sticks, +0x10 pressure).
    pub pads: [crate::pad::RagePad; 4],
    /// The keyboard's keys this frame (0x831C5208 + 0x100 x the buffer 0x831C5508 points at), not zero when down.
    pub keys: [u8; 256],
    /// The mouse: its moves (0x831C5510, 0x831C551C) with their scales (0x831D4C7C, 0x831D4C84), the two words
    /// read as axes (0x831C5528, 0x831C5514), its buttons (0x831C5520), the wheel (0x831C552C, valid when 0x831C5524).
    pub mouse_move: [i32; 2],
    pub mouse_scale: [f32; 2],
    pub mouse_axis: [i32; 2],
    pub mouse_buttons: u32,
    pub wheel_valid: bool,
    pub wheel: i32,
    /// The update pass counter (0x831C550C): one more each time a mapper is updated.
    pub pass: u8,
}

impl Default for RageInput {
    fn default() -> Self {
        RageInput { pads: Default::default(), keys: [0; 256], mouse_move: [0; 2], mouse_scale: [0.0; 2], mouse_axis: [0; 2],
                    mouse_buttons: 0, wheel_valid: false, wheel: 0, pass: 0 }
    }
}

/// A pad stick read for the directions (0x82208C68, 0x82208CC0, 0x82208D18, 0x82208D70 for sticks 0..3):
/// (byte - 127.5) / 127 within -1..1.
fn stick(p: &crate::pad::RagePad, axis: usize) -> f32 {
    ((p.sticks[axis] as f32 - f32::from_bits(0x42FF_0000)) * f32::from_bits(0x3C01_0204)).clamp(-1.0, 1.0)
}

/// Two sources read as one axis: only the first down 0, only the second 0xFF, neither or both 0x80.
fn pair(first: bool, second: bool) -> u8 {
    match (second, first) {
        (false, true) => 0,
        (true, false) => 0xFF,
        _ => 0x80,
    }
}

/// One source's value 0..255 this frame (0x828D3058's switch). `pad` is the pad the binding reads.
fn source_value(kind: u32, index: u32, pad: &crate::pad::RagePad, input: &RageInput) -> u8 {
    const NEAR: f32 = f32::from_bits(0x3F11_EB85); // 0.57 (0x820131C8), and -0.57 (0x82095638)
    const FAR: f32 = f32::from_bits(0x3F4C_CCCD); // 0.8 (0x820BED00), and -0.8 (0x8200F268)
    let on = |b: bool| if b { 0xFF } else { 0 };
    match kind {
        0 => on(input.keys[index as usize & 0xFF] != 0),
        1 => {
            let k = match index { 0 => 0, 1 => 1, _ => return 0 };
            ((input.mouse_move[k] as f32 * input.mouse_scale[k]) as f64 * 255.0) as i64 as u8
        }
        2 => match index { 0 => (input.mouse_axis[0] + 0x80) as u8, 1 => (input.mouse_axis[1] + 0x80) as u8, _ => 0 },
        3 => on(input.mouse_buttons & index == index),
        4 => on(input.mouse_buttons & index != 0),
        5 if input.wheel_valid => match index { 0 => on(input.wheel >= 1), 1 => on(input.wheel < 0), _ => 0 },
        6 => on(pad.buttons & index == index),
        7 => on(pad.buttons & index != 0),
        8 => pad.pressure.get(index as usize).copied().unwrap_or(0),
        9 => {
            let (a0, a1, a2, a3) = (stick(pad, 0), stick(pad, 1), stick(pad, 2), stick(pad, 3));
            match index {
                0..=3 => pad.sticks[index as usize],
                4 => on(a1 < -FAR && a0.abs() < NEAR),
                5 => on(a1 > FAR && a0.abs() < NEAR),
                6 => on(a0 < -FAR && a1.abs() < NEAR),
                7 => on(a0 > FAR && a1.abs() < NEAR),
                8 => on(a1 < -NEAR && a0 > NEAR),
                9 => on(a1 < -NEAR && a0 < -NEAR),
                10 => on(a1 > NEAR && a0 > NEAR),
                11 => on(a1 > NEAR && a0 < -NEAR),
                12 => on(a3 < -FAR && a2.abs() < NEAR),
                13 => on(a3 > FAR && a2.abs() < NEAR),
                14 => on(a2 < -FAR && a3.abs() < NEAR),
                15 => on(a2 > FAR && a3.abs() < NEAR),
                16 => on(a3 < -NEAR && a2 > NEAR),
                17 => on(a3 < -NEAR && a2 < -NEAR),
                18 => on(a3 > NEAR && a2 > NEAR),
                19 => on(a3 > NEAR && a2 < -NEAR),
                0x14 => pair(pad.buttons & 0xA000 == 0x8000, pad.buttons & 0xA000 == 0x2000),
                0x15 => pair(pad.buttons & 0x5000 == 0x1000, pad.buttons & 0x5000 == 0x4000),
                _ => 0,
            }
        }
        14 => pair(input.keys[index as usize & 0xFF] != 0, input.keys[(index >> 8) as usize & 0xFF] != 0),
        // The shift is the CPU's: 32 or more gives 0.
        15 => {
            let bit = |n: u32| 1u32.checked_shl(n & 0x3F).unwrap_or(0);
            pair(pad.buttons & bit(index) != 0, pad.buttons & bit(index >> 8) != 0)
        }
        _ => 0,
    }
}

/// The value a control rests at for a source (0x828D2F00): 0x80 for the mouse's axes (1, 2), the key and button pairs
/// (14, 15), the sticks' own axes and the two D-pad pairs (source 9: 0..3, 0x14, 0x15); otherwise 0.
fn rest_value(kind: u32, index: u32) -> u8 {
    match kind {
        1 | 2 | 14 | 15 => 0x80,
        9 if matches!(index & 0xFF_FFFF, 0..=3 | 0x14 | 0x15) => 0x80,
        _ => 0,
    }
}

/// A RAGE input mapper (rage::ioMapper): up to 64 bindings, each a source (kind, and a code whose top byte is the pad,
/// 0xFF for the mapper's own) and the control it feeds (an index into the controls' values).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct IoMapper {
    /// The pad bindings read when theirs is 0xFF (+0x04).
    pub default_pad: u32,
    /// (kind +0x08, code +0x108, control +0x208) for each binding, in order.
    pub bindings: Vec<(u32, u32, usize)>,
}

impl IoMapper {
    /// Add a binding (0x828D2E18); `pad` -1 for the mapper's own.
    pub fn map(&mut self, kind: u32, index: u32, control: usize, pad: i32) {
        self.bindings.push((kind, ((pad as u32) << 24) | index, control));
    }

    /// Every bound control back to its rest value (0x828D2F00), this frame's history entry too.
    pub fn reset(&self, values: &mut [IoValue]) {
        for &(kind, code, control) in &self.bindings {
            let v = &mut values[control];
            v.value = rest_value(kind, code & 0xFF_FFFF);
            let at = v.history_at as usize;
            if let Some(h) = v.history.as_mut() {
                h[at].0 = v.value;
            }
        }
    }

    /// One frame (0x828D3058): a new update pass; each bound control not yet started in it keeps its value as last
    /// frame's, goes to 0 and takes a new history entry at `time`; then each binding raises its control to its
    /// source's value when that is higher than the control (read through its invert byte).
    pub fn update(&self, input: &mut RageInput, values: &mut [IoValue], time: u32) {
        input.pass = input.pass.wrapping_add(1);
        for &(_, _, control) in &self.bindings {
            let v = &mut values[control];
            if v.stamp != input.pass {
                v.last = v.value;
                v.value = 0;
                if let Some(h) = v.history.as_mut() {
                    let next = v.history_at.wrapping_add(1);
                    v.history_at = if next < 0x40 { next } else { 0 };
                    h[v.history_at as usize] = (0, time);
                }
                v.stamp = input.pass;
            }
        }
        for &(kind, code, control) in &self.bindings {
            let pad = if code >> 24 == 0xFF { self.default_pad } else { code >> 24 };
            let s = source_value(kind, code & 0xFF_FFFF, &input.pads[pad as usize], input);
            let v = &mut values[control];
            if v.value ^ v.invert < s {
                v.value = s;
                let at = v.history_at as usize;
                if let Some(h) = v.history.as_mut() {
                    h[at].0 = s;
                }
            }
        }
    }
}


/// The controls a controls object holds: 87 values from +0x918, 12 bytes each; control `i` is at 0x918 + 12 x i.
pub const CONTROLS: usize = 87;

/// A control's index from its place in the controls object.
pub const fn index(offset: u32) -> usize {
    ((offset - 0x918) / 12) as usize
}

/// The controls given a history when a controls object is set up for a pad (0x822B5888 on each, from 0x822B7CD8).
const KEEP_HISTORY: [u32; 19] = [0xBDC, 0xBE8, 0xBF4, 0xD14, 0xC00, 0xC0C, 0x960, 0x9D8, 0x9E4, 0x9A8, 0x9B4, 0x9C0, 0x9CC, 0xB1C,
                                 0xB88, 0xB94, 0xBA0, 0xA20, 0xB10];

/// The controls the reset puts back, by mapper (0x822B74C0, tables 0x8200BE00, 0x8200BE78, 0x8200BE90): the fourth
/// mapper's are left.
const RESET_TABLES: [&[usize]; 3] = [
    &[0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
      0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D],
    &[0x3B, 0x3C, 0x3D, 0x55, 0x3E, 0x3F],
    &[0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x30, 0x31, 0x32, 0x33, 0x34,
      0x35, 0x36, 0x37, 0x38, 0x39, 0x3A],
];

/// A controls object (the player's at 0x82B2A2F0, others at 0x82B2B36C, 0x82B2C3E8 (the empty one), 0x82B2D464).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Controls {
    /// The four mappers (+0x000, +0x308, +0x610, +0xD58).
    pub mappers: [IoMapper; 4],
    /// The controls (+0x918 ..).
    pub values: Vec<IoValue>,
    /// Cleared each update (+0xD50).
    pub flag_d50: u8,
    /// The pad it reads (+0xD54).
    pub pad: u32,
    /// The words the set-up leaves (+0x1060, +0x1064).
    pub setup_words: [u32; 2],
    /// When a control was last in use (+0x1068).
    pub last_used: u32,
    /// Four sticks as ints, read from the player's controls before each update (+0x106C .. +0x1078).
    pub sticks: [i32; 4],
}

impl Default for Controls {
    fn default() -> Self {
        Controls { mappers: Default::default(), values: vec![IoValue::default(); CONTROLS], flag_d50: 0, pad: 0, setup_words: [0; 2],
                   last_used: 0, sticks: [0; 4] }
    }
}

impl Controls {
    /// The control map's set-up (0x822B6560): each mapper's controls back to rest and the mapper emptied, then the
    /// layout's bindings (see [`bindings`]) and the two words.
    pub fn set_up(&mut self, controller: bool, setting: u32) {
        for m in &mut self.mappers {
            m.reset(&mut self.values);
            m.bindings.clear();
        }
        for b in bindings(controller, setting) {
            let k = MAPPERS.iter().position(|&m| m == b.mapper).unwrap();
            let (kind, n) = b.source.code();
            self.mappers[k].map(kind, n, index(b.control), -1);
        }
        self.setup_words = SETUP_WORDS;
    }

    /// Set the object up for a pad (0x822B7CD8): the pad for it and its four mappers, the saved sticks cleared, the
    /// last-used time `now`, the control map's set-up, and a history for nineteen of the controls.
    pub fn set_up_for_pad(&mut self, pad: u32, now: u32, controller: bool, setting: u32) {
        self.pad = pad;
        self.sticks = [0; 4];
        for m in &mut self.mappers {
            m.default_pad = pad;
        }
        self.last_used = now;
        self.set_up(controller, setting);
        for c in KEEP_HISTORY {
            let v = &mut self.values[index(c)];
            if v.history.is_none() {
                v.history = Some(vec![(0, 0); 64]);
            }
        }
    }

    /// The four sticks the update saves (+0xC78, +0xC84, +0xC9C, +0xC90, as ints).
    pub fn stick_values(&self) -> [i32; 4] {
        [0xC78, 0xC84, 0xC9C, 0xC90].map(|c| self.values[index(c)].stick_int())
    }

    /// The frame's update (0x822B7DD0): `player` the player's controls' [`Controls::stick_values`] (0x821B42B8: the
    /// player's, or the empty object's), saved; the first three mappers updated unless the game is paused
    /// (0x82BFA144), the fourth always, at `now`; then any control in use stamps `now` as the last-used time. In use:
    /// controls 0x0C..0x25 other than 0x14..0x17, 0x1C, 0x1D by their stick reading not 0; the rest by their value
    /// reaching 0x80; controls 0x40..0x54 not counted.
    pub fn update(&mut self, player: [i32; 4], input: &mut RageInput, now: u32, paused: bool) {
        self.sticks = player;
        for (k, m) in self.mappers.iter().enumerate() {
            if k == 3 || !paused {
                m.update(input, &mut self.values, now);
            }
        }
        self.flag_d50 = 0;
        for (i, v) in self.values.iter().enumerate() {
            if (0x40..=0x54).contains(&i) {
                continue;
            }
            let in_use = match i {
                0x0C..=0x25 if !matches!(i, 0x14..=0x17 | 0x1C | 0x1D) => v.stick_int() != 0,
                _ => v.get() >= 0x80,
            };
            if in_use {
                self.last_used = now;
            }
        }
    }

    /// Put the controls back (0x822B74C0): for each control in the first three mappers' tables, its binding in that
    /// mapper decides: a mouse axis (1), a stick (9), kinds 11, 14 and 15 rest at 0x7F, the rest at 0; value, last
    /// value and every history entry, with `now` in each entry's time. As in the exe, a control with no binding there
    /// takes the mapper's pad number (+0x04) as its kind.
    pub fn reset(&mut self, now: u32) {
        for (m, table) in self.mappers.iter().zip(RESET_TABLES) {
            for &c in table {
                let kind = m.bindings.iter().find(|b| b.2 == c).map_or(m.default_pad, |b| b.0);
                let rest = if matches!(kind, 1 | 9 | 11 | 14 | 15) { 0x7F } else { 0 };
                let v = &mut self.values[c];
                v.value = rest;
                v.last = rest;
                if let Some(h) = v.history.as_mut() {
                    h.fill((rest, now));
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layouts() {
        // Every layout binds the same special control and ends with the fourth mapper's seventeen.
        for (controller, setting) in [(false, 0), (true, 0), (true, 1)] {
            let v = bindings(controller, setting);
            assert_eq!(v.iter().filter(|b| b.control == 0xBD0).count(), 1);
            assert_eq!(&v[v.len() - TAIL.len()..], TAIL);
        }
        assert_eq!(bindings(false, 0).len(), NO_PAD.len() + 1 + TAIL.len());
        assert_eq!(bindings(true, 7), bindings(true, 1));
    }

    #[test]
    fn strongest_source_wins() {
        // Two sources on one control: it takes the higher; the next frame starts again from 0.
        let mut m = IoMapper::default();
        m.map(8, 0, 0, 0);
        m.map(8, 1, 0, 0);
        let mut input = RageInput::default();
        input.pads[0].pressure[0] = 40;
        input.pads[0].pressure[1] = 200;
        let mut vals = vec![IoValue::default()];
        m.update(&mut input, &mut vals, 0);
        assert_eq!((vals[0].get(), vals[0].last), (200, 0));
        input.pads[0].pressure[1] = 0;
        m.update(&mut input, &mut vals, 1);
        assert_eq!((vals[0].get(), vals[0].last), (40, 200));
    }

    #[test]
    fn set_up_for_pad() {
        let mut c = Controls::default();
        c.set_up_for_pad(2, 100, true, 0);
        assert!(c.mappers.iter().all(|m| m.default_pad == 2));
        assert_eq!(c.mappers.iter().map(|m| m.bindings.len()).sum::<usize>(), bindings(true, 0).len());
        assert_eq!(c.values.iter().filter(|v| v.history.is_some()).count(), KEEP_HISTORY.len());
        assert_eq!((c.last_used, c.setup_words), (100, SETUP_WORDS));
    }
}
