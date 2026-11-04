//! The abort requests (vf +0x14, asked through 0x82164FB8) of the response tasks a driver runs and of CarDriveBasic's
//! other leaves (board V10, the event gap list; Xbox 360 exe, TU8). `driverabort` has the drive's chain; these are the
//! rest of it.
//!
//! - **CTaskComplexCombat** 0x8248AD30 ([`combat`]), type 0x76C.
//! - **CTaskComplexCombatRetreatSubtask** 0x8248C8A0 ([`retreat`]), type 0x770.
//! - **CTaskComplexScreamInCarThenLeave** 0x823EB268 ([`scream`]): its subtask's answer at urgency 2, else no.
//! - CarDriveBasic's leaves by state (0x823EC1D8): 0 StartCar and 3 Shunt share 0x82456C70 ([`clip_leaf`]); 2
//!   CloseDoorFromInside 0x824B7360 ([`close_door`]); 4 SmashCarWindow 0x82488848 ([`smash`]); 6 NewExitVehicle 0x8241DBE0
//!   ([`new_exit`]); 1 PlayRandomAmbients and 5 DoNothing are `driverabort`'s.
//!
//! The subtask's own request (0x82164FB8 on +8) is `Calls::ask_sub`. Checked by `tools/xbox360-re/checks/combatabort.py`
//! (each vf run in ppcrun with fake vtables for the event, its source and the subtask, every callee stubbed and logged)
//! against `examples/combatabort_cases.rs`.

/// The event: its type (vf +4), source (vf +0x34), vf +0x18, the weapon type at +0x48 (the damage events') and its age (+4).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Ev {
    pub kind: u32,
    pub source: Option<Source>,
    pub vf18: bool,
    pub w48: u32,
    pub age: i32,
}

/// The event's source entity.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Source {
    /// Its handle (compared with the Combat's target).
    pub id: u32,
    /// +0x28 & 0x3C00000 == 0xC00000: a ped.
    pub ped: bool,
    /// vf +0x114: a player.
    pub player: bool,
}

/// The ped asked.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Ped {
    pub id: u32,
    /// +0x214's +0x11C: 2 a cop; 0x822F2660 is 3..=14.
    pub kind: i32,
    /// +0x23C (bit 29: in a vehicle).
    pub w23c: u32,
    /// +0xA80: its vehicle (0 none) and that vehicle's driver (+0xF40).
    pub vehicle: u32,
    pub veh_driver: u32,
    /// +0xC9C and the 16 bytes at +0xCA0.
    pub wc9c: u32,
    pub wca0: [u32; 4],
    /// +0x1E2.
    pub h1e2: u16,
    /// The event handler (intelligence vf +0x20): its secondary task (+8) and its main task's type (+0xC, vf +0xC).
    pub secondary: u32,
    pub main: Option<u32>,
}

/// The Combat's target (+0x3C).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Target {
    pub id: u32,
    /// The bytes +0x210 and +0x211 (the player's).
    pub b210: u8,
    pub b211: u8,
    /// vf +0x114.
    pub player: bool,
    /// The wanted record's level (+0x220's +0x60 +0x68), read only for a target with +0x211 and no +0x210, the player.
    pub wanted: i32,
}

/// A subtask (+8): its type (vf +0xC) and byte +0x4C.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Sub {
    pub kind: u32,
    pub b4c: u8,
}

/// CTaskComplexCombat as its vf +0x14 reads it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Combat {
    pub w60: u32,
    pub target: Option<Target>,
    pub sub: Option<Sub>,
}

/// The ped at a retreat's +0x18: its handle and +0x214's +0x11C.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Owner {
    pub id: u32,
    pub kind: i32,
}

/// CTaskComplexCombatRetreatSubtask: +0x18 and the timer at +0x6C (its set byte, +0x70).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Retreat {
    pub owner: Option<Owner>,
    pub b6c: u8,
    pub w70: u32,
}

/// A clip helper's words: flags (+4) and +0x3C.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Helper {
    pub w4: u32,
    pub f3c: f32,
}

/// StartCar / Shunt (0x82456C70's): +0x18 flags, +0x14 helper (0 none).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ClipLeaf {
    pub id: u32,
    pub b18: u8,
    pub helper: u32,
}

/// CloseDoorFromInside: +0x18 helper (0 none), +0x24, +0x28.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CloseDoor {
    pub helper: u32,
    pub w24: u32,
    pub w28: u32,
}

/// SmashCarWindow: +0x10 helper (0 none).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Smash {
    pub id: u32,
    pub helper: u32,
}

/// The lines said (0x8246B008's).
pub const GOOD_CITIZEN: &str = "GOOD_CITIZEN";
pub const FALL_BACK: &str = "FALL_BACK";
pub const GET_DOWN: &str = "GET_DOWN";
pub const TAKE_COVER: &str = "TAKE_COVER";
pub const DUCK: &str = "DUCK";
/// Types the code names.
pub const COMBAT: u32 = 0x76C;
pub const FALL_BACK_SUB: u32 = 0x76F;
pub const PERSUE_IN_CAR: u32 = 0x774;
pub const RETREAT: u32 = 0x770;
pub const IN_VEHICLE: u32 = 0x2000_0000;

/// The calls, named by address.
pub trait Calls {
    /// 0x82164FB8(task +8, ped, urgency, event): the subtask's answer.
    fn ask_sub(&mut self, urgency: i32) -> bool;
    /// 0x82158E78(0.0, 1.0).
    fn rand(&mut self) -> f32;
    /// 0x8246B008(speaker + 0x510, line, 0, 0, 0, -1, 0, 0, 1.0) with the stack words +0x5C `w5c` and +0x64 0.
    fn say(&mut self, speaker: u32, line: &'static str, w5c: u32);
    /// 0x82299AD0(weapon)'s +0xC: the weapon info's fire type (a type from 0x3C up reads type 0's).
    fn fire_type(&mut self, weapon: u32) -> u32;
    /// 0x823B0928(0x82FC5C00, the target's place, kind, 0).
    fn report(&mut self, target: u32, kind: u32);
    /// 0x8221E1E8(the target's wanted record, its place, 2, 0, 1).
    fn raise(&mut self, target: u32);
    /// 0x8246EA20(handler): the main task's vf 0(1), its delete; +0xC is then 0.
    fn drop_main(&mut self);
    /// 0x823A1A60(handle).
    fn c823a1a60(&mut self, h: u32);
    /// 0x8254F768(ped +0xC9C, ped).
    fn c8254f768(&mut self, w: u32);
    /// Ped vf +0x100(ped, 1).
    fn ped_vf100(&mut self);
    /// 0x82455C68(helper, blend).
    fn blend(&mut self, helper: u32, f: f32);
    /// 0x82454148(helper, task).
    fn release(&mut self, helper: u32, task: u32);
    /// 0x82380AA8(+0x24, ped, +0x28, helper, 0).
    fn c82380aa8(&mut self, w24: u32, w28: u32, helper: u32);
}

fn src_id(ev: &Ev) -> u32 {
    ev.source.map_or(0, |s| s.id)
}

/// 0x8248AD30(task, ped, urgency, event).
pub fn combat(t: &mut Combat, ped: &mut Ped, urgency: i32, mut ev: Option<&mut Ev>, c: &mut impl Calls) -> bool {
    let target = t.target.map_or(0, |t| t.id);
    if let Some(e) = ev.as_deref() {
        if src_id(e) == target && matches!(e.kind, 0x31 | 0x74 | 9) && (3..=14).contains(&ped.kind) && t.w60 & 0x20000 == 0 {
            t.w60 = (t.w60 & !0x1400_0000) | 0x20000;
        }
    }
    let ask = 'case: {
        let Some(e) = ev.as_deref_mut() else { break 'case true };
        if urgency == 2 {
            break 'case true;
        }
        let main = match e.kind {
            2 | 11 => return false,
            9 => {
                if ped.main.is_none_or(|k| k == COMBAT || k == 0x16C) {
                    break 'case refuse(ped, e, c);
                }
                break 'case true;
            }
            79 => {
                if t.w60 & 0x20_0000 != 0 {
                    return false;
                }
                break 'case true;
            }
            15 | 31 | 41 | 49 | 116 => true,
            36 | 103 => false,
            _ => break 'case true,
        };
        let main = main && e.source.is_some_and(|s| s.ped) && src_id(e) == target;
        if !main {
            break 'case refuse(ped, e, c);
        }
        if let Some(tg) = t.target {
            if tg.b210 == 0 && tg.b211 != 0 && ped.kind == 2 && tg.wanted < 2 {
                if matches!(e.kind, 0x31 | 0x74) {
                    let kind = if c.fire_type(e.w48) == 4 { 0x1E } else { 0xE };
                    c.report(tg.id, kind);
                }
                c.raise(tg.id);
            }
        }
        t.w60 &= !0x80000;
        let r = c.rand();
        if ped.w23c & IN_VEHICLE == 0 && r < 0.5 {
            // 0x82483690(target): 1 for none or not a player.
            let w5c = u32::from(t.target.is_none_or(|t| !t.player));
            let cop = ped.kind == 2;
            if t.sub.is_some_and(|s| s.kind == FALL_BACK_SUB) {
                if cop {
                    c.say(ped.id, FALL_BACK, w5c);
                }
            } else if r < 0.33 {
                if cop {
                    c.say(ped.id, GET_DOWN, w5c);
                }
            } else if r < 0.66 && ped.w23c & IN_VEHICLE == 0 {
                c.say(ped.id, TAKE_COVER, w5c);
            } else {
                c.say(ped.id, DUCK, w5c);
            }
        }
        // The subtask is read with no null test.
        let sub = t.sub.as_mut().expect("a Combat with no subtask");
        if sub.kind == PERSUE_IN_CAR && ped.vehicle != 0 && ped.veh_driver == ped.id {
            sub.b4c = 1;
        }
        refuse(ped, e, c)
    };
    if !ask || !c.ask_sub(urgency) {
        return false;
    }
    if ped.wc9c != 0 {
        ped.w23c &= !0x80000;
    }
    let bit = ev.is_some_and(|e| e.kind == 0x4F);
    t.w60 = (t.w60 & !0x20_0000) | (u32::from(bit) << 21);
    true
}

/// 0x8248B394: with no secondary task the event ages a step and the answer is no; else the main task goes and the
/// subtask is asked.
fn refuse(ped: &mut Ped, e: &mut Ev, c: &mut impl Calls) -> bool {
    if ped.secondary == 0 {
        e.age = e.age.wrapping_add(1);
        return false;
    }
    if ped.main.take().is_some() {
        c.drop_main();
    }
    true
}

/// 0x8248C8A0(task, ped, urgency, event).
pub fn retreat(t: &mut Retreat, ped: &mut Ped, urgency: i32, ev: Option<&Ev>, c: &mut impl Calls) -> bool {
    if !c.ask_sub(urgency) {
        return false;
    }
    if let Some(s) = ev.and_then(|e| e.source) {
        if s.ped && s.player && ped.secondary != 0 {
            if let Some(o) = t.owner.filter(|o| o.kind == 2) {
                c.say(o.id, GOOD_CITIZEN, 0);
            }
        }
    }
    // 0x822598F0(task +0x6C).
    if t.b6c != 0 {
        if t.w70 != 0 {
            c.c823a1a60(t.w70);
            t.w70 = 0;
        }
        t.b6c = 0;
    }
    // 0x822F2258(ped).
    if ped.wc9c != 0 {
        c.c8254f768(ped.wc9c);
        ped.wc9c = 0;
        ped.wca0 = [0; 4];
    }
    true
}

/// 0x823EB268(task, ped, urgency, event).
pub fn scream(urgency: i32, c: &mut impl Calls) -> bool {
    urgency == 2 && c.ask_sub(2)
}

/// 0x8241DBE0(task, ped, urgency, event): `t34` is the task's +0x34.
pub fn new_exit(t34: i32, ped: &Ped, urgency: i32, ev: Option<&Ev>, c: &mut impl Calls) -> bool {
    if urgency != 2 && !ev.is_some_and(|e| e.vf18) && t34 == 0x18 {
        return false;
    }
    let r = c.ask_sub(urgency);
    if r && ped.w23c & IN_VEHICLE == 0 && ped.h1e2 & 0xF != 0 {
        c.ped_vf100();
    }
    r
}

/// 0x82456C70(task, ped, urgency, event); `mode` is [0x82B39504], `h` the helper's words.
pub fn clip_leaf(t: &mut ClipLeaf, h: &mut Helper, urgency: i32, ev: Option<&Ev>, mode: u32, c: &mut impl Calls) -> bool {
    if urgency != 2 && t.b18 & 0x40 != 0 && ev.is_some_and(|e| !e.vf18 && e.kind != 0x20) {
        return false;
    }
    if !(urgency == 1 || urgency == 2 || t.b18 & 0x20 != 0) {
        return false;
    }
    if t.helper == 0 {
        return true;
    }
    let mut f = if t.b18 & 8 != 0 { -16.0 } else { -4.0 };
    if urgency == 2 {
        f = -1000.0;
    }
    if mode == 2 && h.f3c != -4.0 {
        f = h.f3c;
    }
    h.w4 = (h.w4 & !0x8000) | 0x4000;
    c.blend(t.helper, f);
    c.release(t.helper, t.id);
    t.helper = 0;
    t.b18 |= 0x80;
    true
}

/// 0x824B7360(task, ped, urgency, event); the helper stays at +0x18.
pub fn close_door(t: &CloseDoor, h: &mut Helper, urgency: i32, c: &mut impl Calls) -> bool {
    if urgency != 2 {
        return false;
    }
    if t.helper != 0 {
        c.blend(t.helper, -1000.0);
        h.w4 |= 0x4000;
        if t.w24 != 0 {
            c.c82380aa8(t.w24, t.w28, t.helper);
        }
    }
    true
}

/// 0x82488848(task, ...): 0x82411C68 then yes.
pub fn smash(t: &mut Smash, h: &mut Helper, c: &mut impl Calls) -> bool {
    if t.helper != 0 {
        c.blend(t.helper, -8.0);
        c.release(t.helper, t.id);
        h.w4 |= 0x4000;
        t.helper = 0;
    }
    true
}
