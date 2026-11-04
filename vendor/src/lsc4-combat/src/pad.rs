//! The game's pad, from the Xbox 360 exe (TU8 addresses; board rows I1a, I1b).
//!
//! Each pad object (the player's at 0x82B2A2F0, a second at 0x82B2B36C, an empty one at 0x82B2C3E8) holds at +0x00
//! RAGE's pad (what the console reports), then two copies of the game's own state: this frame's at +0x04 and last
//! frame's at +0x54, twenty words each. The frame's update (0x822094F8) copies this frame's state over last frame's,
//! then, when there is a RAGE pad, fills this frame's from it (0x82208F50):
//!
//! - words 0-3: the four stick bytes (RAGE pad +0x0C..+0x0F), each centred and scaled to -1..1, put through the
//!   0.2 dead zone (0x828CAD78), and back to -128..127 (rounded down);
//! - words 4-19: buttons, 0 or 255 from RAGE's button bits (+0x04), or for twelve of them, when the pad reports
//!   pressure (+0x28 not zero), the pressure bytes (+0x10..+0x1B) as they are.
//!
//! The words are named here by where they come from, not by what the game does with them: that is for the code
//! that reads them (the control map, row I1).
//!
//! The rumble (board row I1b) is kept in the pad at +0xA4 .. +0xB8 and drives RAGE's two motors (RAGE pad +0x32 /
//! +0x34, a speed 0..65535 each; +0x25 on; +0x26 marks them changed): SHAKE_PAD (0x82209630) starts one, the
//! frame's update ends it when its time is up, or fades it while the player is in state 3 (his info +0x4D0), and
//! a stop (0x82208B80) turns the motors off. Times are the game clock in ms (0x82C6C2A4, or 0x82C6C294 while
//! 0x82BFA144 is set): the caller passes it in.

/// The dead zone the pad conversion gives its sticks (0x8201A6BC).
pub const STICK_DEAD_ZONE: f32 = f32::from_bits(0x3E4C_CCCD);
/// Half the stick's range, 127.5 (0x82003C48), and 1 / 127 (0x82003C44): the exe scales the sticks by these.
const HALF_RANGE: f32 = f32::from_bits(0x42FF_0000);
const STICK_SCALE: f32 = f32::from_bits(0x3C01_0204);

/// A value past ±`t` scaled to 0..±1 from the edge of the zone, inside it 0 (0x828CAD78).
pub fn dead_zone(v: f32, t: f32) -> f32 {
    if v > t {
        (v - t) / (1.0 - t)
    } else if v < -t {
        (v + t) / (1.0 - t)
    } else {
        0.0
    }
}

/// What the conversion reads from RAGE's pad.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RagePad {
    /// Button bits (+0x04).
    pub buttons: u32,
    /// The four stick bytes, 0..255 with 127.5 the middle (+0x0C..+0x0F).
    pub sticks: [u8; 4],
    /// Pressure bytes (+0x10..+0x1B).
    pub pressure: [u8; 12],
    /// The pad reports pressure (+0x28 not zero): twelve buttons take their pressure byte instead of 0 / 255.
    pub has_pressure: bool,
    /// The motors on (+0x25), the motors changed since RAGE last sent them (+0x26), their speeds (+0x32, +0x34).
    pub motors_on: bool,
    pub motors_changed: bool,
    pub motor_speed: [u16; 2],
}

impl RagePad {
    /// One motor's speed, 0..1 (0x828CABF8): clamped, x 65535 cut to an integer, in double precision.
    pub fn set_motor(&mut self, v: f32, motor: usize) {
        let v = (v as f64).clamp(0.0, 1.0);
        self.motor_speed[motor] = (v * 65535.0) as i64 as u16;
        self.motors_changed = true;
    }

    /// The motors on or off (0x828CABE8).
    pub fn set_motors_on(&mut self, on: bool) {
        self.motors_on = on;
        self.motors_changed = true;
    }

    /// Both motors stopped at once (0x828CCDB0).
    /// ponytail: the exe also sends the stop to the console's pad there (and keeps whether that worked at +0x24).
    pub fn stop_now(&mut self) {
        self.motor_speed = [0, 0];
    }
}

/// For each button word (4-19): its RAGE button bit, and the pressure byte it takes instead (an index into
/// [`RagePad::pressure`], counted from +0x10) when the pad reports pressure. Words 6, 9, 18 and 19 are bits always.
const BUTTONS: [(u32, Option<usize>); 16] = [
    (0x0004, Some(0x08)), // word 4
    (0x0001, Some(0x0A)), // word 5
    (0x0200, None),       // word 6
    (0x0008, Some(0x09)), // word 7
    (0x0002, Some(0x0B)), // word 8
    (0x0400, None),       // word 9
    (0x0080, Some(0x07)), // word 10
    (0x0010, Some(0x04)), // word 11
    (0x0020, Some(0x05)), // word 12
    (0x0040, Some(0x06)), // word 13
    (0x1000, Some(0x02)), // word 14
    (0x4000, Some(0x03)), // word 15
    (0x8000, Some(0x01)), // word 16
    (0x2000, Some(0x00)), // word 17
    (0x0800, None),       // word 18
    (0x0100, None),       // word 19
];

/// One frame of the game's pad state (pad +0x04 or +0x54): four sticks, sixteen buttons.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PadState(pub [i32; 20]);

impl PadState {
    /// This frame's state from RAGE's pad (0x82208F50).
    pub fn from_rage(p: &RagePad) -> PadState {
        let mut w = [0i32; 20];
        for (k, &b) in p.sticks.iter().enumerate() {
            w[k] = stick(b);
        }
        for (k, &(bit, pressure)) in BUTTONS.iter().enumerate() {
            w[4 + k] = match pressure {
                Some(i) if p.has_pressure => p.pressure[i] as i32,
                _ => if p.buttons & bit != 0 { 255 } else { 0 },
            };
        }
        PadState(w)
    }
}

/// One stick byte to -128..127 (0x82208F50): (byte - 127.5) / 127 within -1..1, the dead zone, then x 127.5 rounded
/// down. All in single precision, as the exe; its rounding down is the add-and-subtract of 2^23 then a step back
/// when that went up, which is `floor` for numbers this small.
fn stick(b: u8) -> i32 {
    let v = ((b as f32 - HALF_RANGE) * STICK_SCALE).clamp(-1.0, 1.0);
    (dead_zone(v, STICK_DEAD_ZONE) * HALF_RANGE).floor() as i32
}

/// A pad: its two frames of state (pad +0x04 this frame, +0x54 last frame) and its rumble.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Pad {
    pub now: PadState,
    pub last: PadState,
    /// When the rumble's motors were last set (+0xA4).
    pub rumble_start: u32,
    /// How long it runs, ms; 0 for none (+0xA8).
    pub rumble_time: u32,
    /// Its strength, 256 the full motor (+0xAC).
    pub rumble_strength: i32,
    /// Until then a shake no stronger than `hold_strength` is ignored (+0xB0, +0xB4).
    pub hold_until: u32,
    pub hold_strength: i32,
    /// Stopped this frame (+0xB8): shakes are ignored until the next update.
    pub stopped: bool,
}

/// What lets SHAKE_PAD through (0x82209630): vibration on in the settings (0x82BF9F08 not zero), and unless the
/// shake is forced, neither 0x821E3788 nor the byte 0x82BEF99D saying no.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ShakeGate {
    pub vibration_on: bool,
    pub blocked: bool,
}

impl Pad {
    /// The frame's update (0x822094F8): last frame's state is this frame's, then this frame's comes from RAGE's pad
    /// when there is one (with none it stays as it was). Then a running rumble: while the player is in state 3 and
    /// a fifth of its strength is over 10 it fades by that fifth; otherwise it stops once its time is up.
    pub fn update(&mut self, mut rage: Option<&mut RagePad>, now: u32, player_state_3: bool) {
        self.last = self.now;
        if let Some(p) = rage.as_deref() {
            self.now = PadState::from_rage(p);
        }
        if self.rumble_time != 0 {
            let fifth = (self.rumble_strength as f64 * 0.2) as i32;
            if player_state_3 && fifth > 10 {
                self.rumble_strength -= fifth;
                if let Some(p) = rage.as_deref_mut() {
                    self.set_rumble(p, self.rumble_strength, 0, now);
                }
            } else if self.rumble_time < now.wrapping_sub(self.rumble_start) {
                self.stop_rumble(rage, false);
            }
        }
        self.stopped = false;
    }

    /// Both motors from two strengths, 256 the full motor (0x82209360): each set to 0, then each strength above 0
    /// set and the motors turned on, and the start time taken.
    pub fn set_rumble(&mut self, rage: &mut RagePad, a: i32, b: i32, now: u32) {
        rage.set_motor(0.0, 0);
        rage.set_motor(0.0, 1);
        for (motor, s) in [(0, a), (1, b)] {
            if s > 0 {
                rage.set_motor((s as f64 * (1.0 / 256.0)) as f32, motor);
                rage.set_motors_on(true);
                self.rumble_start = now;
            }
        }
    }

    /// Stop the rumble (0x82208B80): a running one turns the motors off and clears its time, strength and start,
    /// with the hold left at strength 1. Asked to (`mark`), and not done yet this frame, the motors are stopped at
    /// once and the pad marked stopped for the rest of the frame.
    pub fn stop_rumble(&mut self, mut rage: Option<&mut RagePad>, mark: bool) {
        if self.rumble_time != 0 || self.rumble_strength > 0 {
            if let Some(p) = rage.as_deref_mut() {
                p.set_motors_on(false);
                p.set_motor(0.0, 0);
                p.set_motor(0.0, 1);
            }
            self.rumble_time = 0;
            self.rumble_strength = 0;
            self.rumble_start = 0;
            self.hold_strength = 1;
            self.hold_until = 0;
        }
        if mark && !self.stopped {
            if let Some(p) = rage {
                p.stop_now();
                self.stopped = true;
            }
        }
    }

    /// SHAKE_PAD (0x82209630): two motors, each with a time (ms) and a strength (256 the full motor), and a hold
    /// (ms) during which a shake no stronger is ignored. Both strengths 0 stops the rumble. The rumble's time and
    /// strength become the longer of the two, if longer than what runs.
    #[allow(clippy::too_many_arguments)]
    pub fn shake(&mut self, rage: &mut RagePad, gate: ShakeGate, force: bool, time_a: u32, strength_a: i32, time_b: u32,
                 strength_b: i32, hold: u32, now: u32) {
        if !gate.vibration_on || (gate.blocked && !force) || self.stopped {
            return;
        }
        if strength_a == 0 && strength_b == 0 {
            self.stop_rumble(Some(rage), false);
            return;
        }
        let top = if strength_a <= strength_b { strength_b } else { strength_a };
        if now < self.hold_until && top <= self.hold_strength {
            return;
        }
        let (time, strength) = if time_b <= time_a { (time_a, strength_a) } else { (time_b, strength_b) };
        if self.rumble_time < time {
            self.rumble_time = time;
            self.rumble_strength = strength;
        }
        self.hold_strength = top;
        self.hold_until = now.wrapping_add(hold);
        self.set_rumble(rage, strength_a, strength_b, now);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sticks_centre_and_ends() {
        // The middle and the dead zone read 0; the ends are -128 and 127.
        assert_eq!(stick(127), 0);
        assert_eq!(stick(128), 0);
        assert_eq!(stick(0), -128);
        assert_eq!(stick(255), 127);
    }
}
