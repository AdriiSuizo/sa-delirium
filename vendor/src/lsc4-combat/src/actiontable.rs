//! `data/action_table.csv` (level file kind ACTIONTABLE): the melee actions and their results, as the game reads them
//! (0x8265EBD8, for each such file from 0x826603B8, through the CSV reader `csv::CsvReader`; Xbox 360 exe, TU8). Board
//! rows C1fg2 (actions) and C1fg3 (results).
//!
//! The file's lines are counted first (one more than the times 0x826C0BD0 gets past a line end before the end), then
//! that many rows are read from the start. Each row starts with a quoted field: one starting `//` is a comment (its
//! hash equal to `// ACTIONS:`'s turns action rows on, `// RESULTS:`'s turns them off; rows start as actions) and the
//! rest of its line is skipped. Other rows are read field by field (the columns `ACTION_COLUMNS` / `RESULT_COLUMNS`)
//! and made into an action (0xD4 bytes, 0x8265C210, kept at the melee manager's +0x58) or a result (0xDC bytes,
//! 0x8265B4B0, +0x60). An action whose ImpulseOp is not `and`, `or` or `xor` ends the whole read.
//!
//! The fields are the reader's stack (r1+0x12D0 is its frame), kept from row to row: an int or float whose field fills
//! its 32 bytes keeps the row before's (ranges are cleared first, strings always written; ponytail: before the first
//! row ours are 0, the game's are what its stack held). Names are compared by their
//! hash (0x8284E060, `map::hash_bytes`); the hashes of `none` (830B7034) and `any` (830B7038) are set at start-up.

use crate::csv::CsvReader;
use crate::map::hash_bytes;

/// The action columns, in the order they are read.
pub const ACTION_COLUMNS: [&str; 41] = [
    "Name", "Priority", "Bias", "Type", "AIPlayerfilter", "AvailableFromIdle", "MustBeRecievingHit", "MustBeBlocking",
    "MustHaveJustStruckSomething", "HealthRange", "attackMultRange", "defenseMultRange", "martialLevelRange", "WeaponClass",
    "ImpulseOp", "CheckImpulse", "Impulse", "ImpulseState", "ImpulseRange", "ImpulseInterval", "CheckImpulse", "Impulse",
    "ImpulseState", "ImpulseRange", "ImpulseInterval", "RequireTarget", "RequireLockOn", "RequiredTargetType",
    "RequiredOpponentRes", "CheckOpponentAnimTime", "OpponentAnimTime", "RequiredOpponentPose", "CheckRelPos", "AngleRange",
    "DistanceRange", "HeightRange", "OpponentsActionToTest", "SpecialTest", "AdditionalActionTest", "Move", "Prefer Ragdol",
];

/// The result columns, in the order they are read.
pub const RESULT_COLUMNS: [&str; 42] = [
    "Name", "Priority", "AnimGroup", "AnimName", "SyncStartTime", "StartTime", "Playback Rate", "PlayerPlaybackMult",
    "IsLoop", "BlendInFactor", "BlendOutFactor", "IsAnIdle", "IsABlock", "PlayerLockEnablesMeleeMode", "HomingTargetType",
    "AngleRange", "DistanceRange", "HeightRange", "stopOnCirtFrame", "stopTime", "DesiredAngleAndRate",
    "DesiredDistanceAndRate", "DesiredHeightAndRate", "ForceImmediateReaction", "SelfDamage", "DropWeapon",
    "DisarmOpponent", "StrikeBone0", "StrikeBone0Radius", "StrikeBone1", "StrikeBone1Radius", "OnHitDamage",
    "AppliedPedMoveMomentum", "ObjectDamageMult", "Branch0Action", "Branch0StartEnd", "Branch1Action", "Branch1StartEnd",
    "Branch2Action", "Branch2StartEnd", "SpecialResult", "SoundName",
];

/// Impulse names (index from 1; 0 for `none` or a name not here).
const IMPULSES: [&str; 9] =
    ["attack", "attack2", "attack3", "attack4", "kick", "block", "analogLeftRight", "analogUpDown", "analogFromCenter"];
/// Impulse states (from 1).
const STATES: [&str; 6] = ["heldDown", "heldUp", "pressedAndHeld", "releasedAndHeld", "pressed", "released"];
/// Poses (from 2; `any` is 1).
const POSES: [&str; 7] = ["standing", "crouched", "onGround", "onFront", "onRHS", "onBack", "onLHS"];
/// 0x8265A850's names (their place; 16 for others).
const POINTS: [&str; 16] = [
    "entityRoot", "entityClosestBoxPoint", "pedRoot", "pedClosestBoxPoint", "pedHead", "pedChest", "pedKneeLeft",
    "pedKneeRight", "vehicleRoot", "vehicleClosestBoxPoint", "vehicleBonnet", "vehicleBoot", "vehicleClosestDoor",
    "vehicleClosestWindow", "vehicleClosestLight", "vehicleClosestTyre",
];
/// 0x8265A518's bones (the first name, 0x8201CEB4's, and others give 0).
const BONES: [(&str, u32); 14] = [
    ("PELVIS", 0x1A1), ("HEAD", 0x4B5), ("R_UPPERARM", 0x4C8), ("R_FOREARM", 0x4C9), ("R_HAND", 0x4D0), ("L_UPPERARM", 0x4C1),
    ("L_FOREARM", 0x4C2), ("L_HAND", 0x4C3), ("L_THIGH", 0x1A2), ("L_CALF", 0x1A3), ("L_FOOT", 0x1A4), ("R_THIGH", 0x1A7),
    ("R_CALF", 0x1A8), ("R_FOOT", 0x1A9),
];

fn h(s: &[u8]) -> u32 {
    hash_bytes(s, 0)
}

/// The hash of `none` (830B7034).
pub fn none() -> u32 {
    h(b"none")
}

/// A name's place in `list` by hash (from 0), or None.
fn place(list: &[&str], s: &[u8]) -> Option<usize> {
    let x = h(s);
    list.iter().position(|n| h(n.as_bytes()) == x)
}

/// 0x8265A780: `all` 0, `player` 1, `ai` 2, anything else 3.
fn who(s: &[u8]) -> u32 {
    match place(&["all", "player", "ai"], s) {
        Some(i) => i as u32,
        None => 3,
    }
}

/// 0x8265A850.
fn point(s: &[u8]) -> u32 {
    place(&POINTS, s).map_or(16, |i| i as u32)
}

/// 0x8265A518.
fn bone(s: &[u8]) -> u32 {
    let x = h(s);
    BONES.iter().find(|b| h(b.0.as_bytes()) == x).map_or(0, |b| b.1)
}

/// The hash of a name, 0 for `none`.
fn hash_or_0(s: &[u8]) -> u32 {
    let x = h(s);
    if x == none() { 0 } else { x }
}

/// Degrees to radians as the reader does it (`fmuls` by 0.017453292).
fn rad(d: f32) -> f32 {
    d * f32::from_bits(0x3C8E_FA35)
}

/// The angle range's ends (0x8265F68C): exactly 180 is pi, exactly -180 is -pi, else radians.
fn rad_end(d: f32) -> f32 {
    if d == 180.0 {
        std::f32::consts::PI
    } else if d == -180.0 {
        -std::f32::consts::PI
    } else {
        rad(d)
    }
}

/// 0x8245BE78: `from` turned toward `to` by at most `step` (their gap brought into -pi..pi by 2 pi steps), wrapped.
pub fn turn_toward(from: f32, to: f32, step: f32) -> f32 {
    let (pi, two_pi) = (std::f32::consts::PI, f32::from_bits(0x40C9_0FDB));
    let mut d = to - from;
    while d > pi {
        d -= two_pi;
    }
    while d < -pi {
        d += two_pi;
    }
    wrap(if d.abs() < step { to } else if d < 0.0 { from - step } else { from + step })
}

/// 0x8245BC88: an angle into -pi..pi (held to -25..25 first; 2 pi steps in single precision).
pub fn wrap(mut x: f32) -> f32 {
    let (pi, two_pi) = (std::f32::consts::PI, f32::from_bits(0x40C9_0FDB));
    let low = if x >= -25.0 {
        if x > 25.0 || x > pi {
            if x > 25.0 {
                x = 25.0;
            }
            loop {
                x -= two_pi;
                if !(x > pi) {
                    break;
                }
            }
        }
        x < -pi
    } else {
        x = -25.0;
        true
    };
    if low {
        loop {
            x += two_pi;
            if !(x < -pi) {
                break;
            }
        }
    }
    x
}

/// A range sorted low to high (the reader swaps the ends when the first is above the second).
fn sorted(r: [f32; 2]) -> [f32; 2] {
    if r[0] > r[1] { [r[1], r[0]] } else { r }
}

/// One of an action's two impulses (24 bytes at +0x48 and +0x60).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Impulse {
    pub check: bool,
    /// [`IMPULSES`] from 1, 0 for none.
    pub impulse: u32,
    /// [`STATES`] from 1, 0 for none.
    pub state: u32,
    pub range: [f32; 2],
    pub interval: f32,
}

/// An action (0x8265C210's 0xD4 bytes).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Action {
    /// +0x00 the name's hash, +0x04 Priority, +0x08 Bias.
    pub name: u32,
    pub priority: i32,
    pub bias: f32,
    /// +0x0C AIPlayerfilter (0x8265A780).
    pub ai_player_filter: u32,
    /// +0x11 AvailableFromIdle, +0x12 MustBeRecievingHit, +0x13 MustBeBlocking, +0x14 MustHaveJustStruckSomething
    /// (each not 0; +0x10 is 0).
    pub flags: [bool; 4],
    /// +0x18 HealthRange, attackMultRange, defenseMultRange, martialLevelRange.
    pub ranges: [[f32; 2]; 4],
    /// +0x38 WeaponClass's hash, +0x3C Type's hash.
    pub weapon_class: u32,
    pub kind: u32,
    /// +0x40 AvailableFromIdle not 0 (again), +0x44 ImpulseOp (`or` 0, `and` 1, `xor` 2).
    pub available_from_idle: bool,
    pub impulse_op: u32,
    /// +0x48, +0x60.
    pub impulses: [Impulse; 2],
    /// +0x78 RequireTarget, +0x7C RequireLockOn (0x8265A780), +0x80 RequiredTargetType (0x8265A850).
    pub require_target: u32,
    pub require_lock_on: u32,
    pub required_target_type: u32,
    /// +0x84 RequiredOpponentRes starts with `!`, +0x88 its hash (after the `!`; 0 for none), +0x8C
    /// CheckOpponentAnimTime, +0x90 OpponentAnimTime, +0x98 RequiredOpponentPose (none 0, any 1, [`POSES`] from 2),
    /// +0x9C OpponentsActionToTest's hash (0 for none).
    pub opponent_res_not: bool,
    pub opponent_res: u32,
    pub check_opponent_anim_time: bool,
    pub opponent_anim_time: [f32; 2],
    pub opponent_pose: u32,
    pub opponents_action_to_test: u32,
    /// +0xA0 CheckRelPos, +0xA4 RequiredTargetType again, +0xA8 HeightRange and +0xB0 DistanceRange (sorted), +0xB8
    /// AngleRange in radians wrapped to -pi..pi.
    pub check_rel_pos: bool,
    pub rel_point: u32,
    pub height_range: [f32; 2],
    pub distance_range: [f32; 2],
    pub angle_range: [f32; 2],
    /// +0xC0 SpecialTest, +0xC4 AdditionalActionTest, +0xC8 Move (hashes, 0 for none); +0xCC is 0; +0xD0 Prefer Ragdol.
    pub special_test: u32,
    pub additional_action_test: u32,
    pub move_: u32,
    pub prefer_ragdoll: bool,
}

/// A result (0x8265B4B0's 0xDC bytes, with what the reader adds).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ResultDef {
    /// +0x00 the name's hash, +0x04 Priority, +0x08 AnimGroup (its place in the anim group store, 0x82248CF0; -1 for
    /// none), +0x0C AnimName (that group's anim, 0x82248C50; -1 for none), +0x10 SyncStartTime (0 0, 1 1, -1 2, else 0).
    pub name: u32,
    pub priority: i32,
    pub anim_group: i32,
    pub anim: i32,
    pub sync_start_time: u32,
    /// +0x14 StartTime, +0x18 Playback Rate, +0x1C PlayerPlaybackMult, +0x24 BlendInFactor, +0x28 BlendOutFactor.
    pub start_time: f32,
    pub playback_rate: f32,
    pub player_playback_mult: f32,
    pub blend_in: f32,
    pub blend_out: f32,
    /// +0x20 IsLoop, +0x2C IsAnIdle, +0x2D IsABlock, +0x2E PlayerLockEnablesMeleeMode.
    pub is_loop: bool,
    pub is_an_idle: bool,
    pub is_a_block: bool,
    pub player_lock_enables_melee_mode: bool,
    /// +0x30 HomingTargetType is one (not 16), +0x34 HomingTargetType (0x8265A850), +0x38 HeightRange and +0x40
    /// DistanceRange (sorted), +0x48 AngleRange (radians, wrapped).
    pub homing: bool,
    pub homing_target_type: u32,
    pub height_range: [f32; 2],
    pub distance_range: [f32; 2],
    pub angle_range: [f32; 2],
    /// +0x50 stopOnCirtFrame, +0x54 stopTime; +0x58 HomingTargetType again, +0x5C DesiredHeightAndRate, +0x64
    /// DesiredDistanceAndRate, +0x6C DesiredAngleAndRate (both ends in radians).
    pub stop_on_crit_frame: bool,
    pub stop_time: f32,
    pub desired_height: [f32; 2],
    pub desired_distance: [f32; 2],
    pub desired_angle: [f32; 2],
    /// +0x74 ForceImmediateReaction, +0x78 SelfDamage, +0x7C DropWeapon, +0x7D DisarmOpponent, +0x80 OnHitDamage,
    /// +0x84 AppliedPedMoveMomentum, +0x88 ObjectDamageMult, +0x8C SpecialResult's hash, +0x90 SoundName's hash.
    pub force_immediate_reaction: bool,
    pub self_damage: f32,
    pub drop_weapon: bool,
    pub disarm_opponent: bool,
    pub on_hit_damage: f32,
    pub applied_ped_move_momentum: f32,
    pub object_damage_mult: f32,
    pub special_result: u32,
    pub sound: u32,
    /// +0x94: the three branches (not none, the action's hash or 0, the range), 16 bytes each.
    pub branches: [(bool, u32, [f32; 2]); 3],
    /// +0xC4: the two strike bones (not none, 0x8265A518's bone, the radius), 12 bytes each.
    pub strike_bones: [(bool, u32, f32); 2],
}

/// The reader's stack fields that rows leave for the next.
#[derive(Default)]
struct Kept {
    ints: std::collections::HashMap<u16, i32>,
    floats: std::collections::HashMap<u16, f32>,
}

impl Kept {
    fn int(&mut self, r: &mut CsvReader, at: u16) -> i32 {
        if let Some(v) = r.int() {
            self.ints.insert(at, v);
        }
        self.ints.get(&at).copied().unwrap_or(0)
    }
    fn float(&mut self, r: &mut CsvReader, at: u16) -> f32 {
        if let Some(v) = r.float() {
            self.floats.insert(at, v);
        }
        self.floats.get(&at).copied().unwrap_or(0.0)
    }
}

fn s(r: &mut CsvReader) -> Vec<u8> {
    r.field(128, true).1
}

fn range(r: &mut CsvReader) -> [f32; 2] {
    let (_, lo, hi) = r.range();
    [lo, hi]
}

/// What the read makes, in order, and whether it read to its end (false when an action's ImpulseOp was not one of
/// the three).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ActionTable {
    pub actions: Vec<Action>,
    pub results: Vec<ResultDef>,
    pub completed: bool,
}

/// 0x8265EBD8 over `file`. `group(name)` is the anim group store's place for a name (0x82248CF0, -1 when none) and
/// `anim(group, name)` that group's anim's id (0x82248C50, -1 when none).
pub fn read(file: &[u8], group: impl Fn(&[u8]) -> i32, anim: impl Fn(i32, &[u8]) -> i32) -> ActionTable {
    let mut r = CsvReader::new(file, b',');
    let mut out = ActionTable::default();
    let mut count: u32 = 1;
    if !r.at_end() {
        loop {
            if !r.skip_lines(1) {
                break;
            }
            count += 1;
            if r.at_end() {
                break;
            }
        }
    }
    r.rewind();
    let mut actions = true;
    let mut k = Kept::default();
    let mut i = 0;
    while i < count {
        if r.at_end() {
            break;
        }
        let name = s(&mut r);
        let name_hash = h(&name);
        if name.starts_with(b"//") {
            if name_hash == h(b"// ACTIONS:") {
                actions = true;
            }
            if name_hash == h(b"// RESULTS:") {
                actions = false;
            }
            r.skip_lines(1);
        } else if actions {
            match read_action(&mut r, &mut k, name_hash) {
                Some(a) => out.actions.push(a),
                None => return out,
            }
        } else {
            out.results.push(read_result(&mut r, &mut k, name_hash, &group, &anim));
        }
        i += 1;
    }
    out.completed = true;
    out
}

fn read_action(r: &mut CsvReader, k: &mut Kept, name: u32) -> Option<Action> {
    let priority = k.int(r, 576);
    let bias = k.float(r, 504);
    let kind = s(r);
    let filter = s(r);
    let flags = [k.int(r, 256), k.int(r, 288), k.int(r, 556), k.int(r, 304)].map(|v| v != 0);
    let health = range(r);
    let attack = range(r);
    let defense = range(r);
    let martial = range(r);
    let weapon = s(r);
    let op = s(r);
    let mut raw = [(0i32, vec![], vec![], [0f32; 2], 0f32), (0, vec![], vec![], [0.0; 2], 0.0)];
    for (n, imp) in raw.iter_mut().enumerate() {
        imp.0 = k.int(r, 864 + 4 * n as u16);
        imp.1 = s(r);
        imp.2 = s(r);
        imp.3 = range(r);
        imp.4 = k.float(r, 920 + 4 * n as u16);
    }
    let require_target = s(r);
    let require_lock_on = s(r);
    let target_type = s(r);
    let opponent_res = s(r);
    let check_anim_time = k.int(r, 416);
    let anim_time = range(r);
    let pose = s(r);
    let check_rel_pos = k.int(r, 488);
    let angle = range(r);
    let distance = range(r);
    let height = range(r);
    let opponents_action = s(r);
    let special = s(r);
    let additional = s(r);
    let move_ = s(r);
    let prefer_ragdoll = k.int(r, 632);
    r.skip_line();
    let op = match place(&["and", "or", "xor"], &op) {
        Some(0) => 1,
        Some(1) => 0,
        Some(2) => 2,
        _ => return None,
    };
    let impulses = raw.map(|(check, imp, state, range, interval)| {
        let ih = h(&imp);
        let impulse = if ih == none() { 0 } else { place(&IMPULSES, &imp).map_or(0, |i| i as u32 + 1) };
        let sh = h(&state);
        let state = if sh == none() { 0 } else { place(&STATES, &state).map_or(0, |i| i as u32 + 1) };
        Impulse { check: check != 0, impulse, state, range, interval }
    });
    let (not, res) = match opponent_res.strip_prefix(b"!") {
        Some(rest) => (true, rest.to_vec()),
        None => (false, opponent_res.clone()),
    };
    let ph = h(&pose);
    let opponent_pose = if ph == none() {
        0
    } else if ph == h(b"any") {
        1
    } else {
        place(&POSES, &pose).map_or(0, |i| i as u32 + 2)
    };
    let point = point(&target_type);
    Some(Action {
        name,
        priority,
        bias,
        ai_player_filter: who(&filter),
        flags,
        ranges: [health, attack, defense, martial],
        weapon_class: h(&weapon),
        kind: h(&kind),
        available_from_idle: flags[0],
        impulse_op: op,
        impulses,
        require_target: who(&require_target),
        require_lock_on: who(&require_lock_on),
        required_target_type: point,
        opponent_res_not: not,
        opponent_res: hash_or_0(&res),
        check_opponent_anim_time: check_anim_time != 0,
        opponent_anim_time: anim_time,
        opponent_pose,
        opponents_action_to_test: hash_or_0(&opponents_action),
        check_rel_pos: check_rel_pos != 0,
        rel_point: point,
        height_range: sorted(height),
        distance_range: sorted(distance),
        angle_range: [wrap(rad_end(angle[0])), wrap(rad_end(angle[1]))],
        special_test: hash_or_0(&special),
        additional_action_test: hash_or_0(&additional),
        move_: hash_or_0(&move_),
        prefer_ragdoll: prefer_ragdoll != 0,
    })
}

fn read_result(
    r: &mut CsvReader, k: &mut Kept, name: u32, group: &impl Fn(&[u8]) -> i32, anim: &impl Fn(i32, &[u8]) -> i32,
) -> ResultDef {
    let priority = k.int(r, 640);
    let group_name = s(r);
    let anim_name = s(r);
    let sync = k.int(r, 512);
    let start_time = k.float(r, 508);
    let playback_rate = k.float(r, 500);
    let player_playback_mult = k.float(r, 492);
    let is_loop = k.int(r, 584);
    let blend_in = k.float(r, 484);
    let blend_out = k.float(r, 568);
    let is_an_idle = k.int(r, 520);
    let is_a_block = k.int(r, 648);
    let lock = k.int(r, 528);
    let homing = s(r);
    let angle = range(r);
    let distance = range(r);
    let height = range(r);
    let stop_on_crit = k.int(r, 616);
    let stop_time = k.float(r, 608);
    let desired_angle = range(r);
    let desired_distance = range(r);
    let desired_height = range(r);
    let force = k.int(r, 592);
    let self_damage = k.float(r, 560);
    let drop = k.int(r, 536);
    let disarm = k.int(r, 624);
    let mut bones = [(vec![], 0f32), (vec![], 0.0)];
    for (n, b) in bones.iter_mut().enumerate() {
        b.0 = s(r);
        b.1 = k.float(r, 912 + 4 * n as u16);
    }
    let on_hit = k.float(r, 552);
    let momentum = k.float(r, 600);
    let object_damage = k.float(r, 544);
    let mut branches = [(vec![], [0f32; 2]), (vec![], [0.0; 2]), (vec![], [0.0; 2])];
    for b in branches.iter_mut() {
        b.0 = s(r);
        b.1 = range(r);
    }
    let special = s(r);
    let sound = s(r);
    r.skip_line();
    let (mut g, mut a) = (-1, -1);
    if h(&group_name) != none() {
        g = group(&group_name);
        if h(&anim_name) != none() {
            a = anim(g, &anim_name);
        }
    }
    let homing_type = point(&homing);
    ResultDef {
        name,
        priority,
        anim_group: g,
        anim: a,
        sync_start_time: match sync {
            0 => 0,
            1 => 1,
            -1 => 2,
            _ => 0,
        },
        start_time,
        playback_rate,
        player_playback_mult,
        blend_in,
        blend_out,
        is_loop: is_loop != 0,
        is_an_idle: is_an_idle != 0,
        is_a_block: is_a_block != 0,
        player_lock_enables_melee_mode: lock != 0,
        homing: homing_type != 16,
        homing_target_type: homing_type,
        height_range: sorted(height),
        distance_range: sorted(distance),
        angle_range: [wrap(rad(angle[0])), wrap(rad(angle[1]))],
        stop_on_crit_frame: stop_on_crit != 0,
        stop_time,
        desired_height,
        desired_distance,
        desired_angle: [rad(desired_angle[0]), rad(desired_angle[1])],
        force_immediate_reaction: force != 0,
        self_damage,
        drop_weapon: drop != 0,
        disarm_opponent: disarm != 0,
        on_hit_damage: on_hit,
        applied_ped_move_momentum: momentum,
        object_damage_mult: object_damage,
        special_result: h(&special),
        sound: h(&sound),
        branches: branches.map(|(n, rg)| {
            let x = h(&n);
            (x != none(), if x != none() { x } else { 0 }, rg)
        }),
        strike_bones: bones.map(|(n, radius)| {
            let some = h(&n) != none();
            (some, if some { bone(&n) } else { 0 }, radius)
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turn_toward_steps_the_short_way_and_lands() {
        assert_eq!(turn_toward(0.0, 0.05, 0.1), 0.05);
        assert_eq!(turn_toward(0.0, 1.0, 0.1), 0.1);
        assert_eq!(turn_toward(0.0, -1.0, 0.1), -0.1);
        // 3 to -3 is 0.28 the positive way round: up past pi, wrapped.
        assert_eq!(turn_toward(3.0, -3.0, 0.2), wrap(3.0 + 0.2));
        assert!(turn_toward(3.0, -3.0, 0.2) < 0.0);
    }

    #[test]
    fn rows() {
        let file = b"// ACTIONS:,\nHit,5,1,HitReaction,all,1,1,1,0,0.0 : 500.0,0.0 : 10.0,0.0 : 10.0,0.0 : 100.0,unarmed,and,\
                     0,None,None,0.00 : 0.00,0,1,attack,pressed,0 : 1,0.5,all,None,pedRoot,!Lowblow_L_Res,0,0.0 : 0.0,any,0,\
                     -180.00 : 90.00,3.00 : 0.00,-2.00 : 2.00,None,None,None,DAM,0,\n// RESULTS:,\n\
                     Tough,1,unarmed_gang,L_Lowblow,-1,0,1,1,0,8,-4,0,0,1,pedRoot,-100.00 : 100.00,0.40 : 1.20,-2.00 : 2.00,1,\
                     100,0.00 : 900.00,1.00 : 20,0.00 : 0.00,0,40,0,0,L_HAND,0.06,L_FOREARM,0.06,7,0.5,0.3,Recoil,0.0 : 10.0,\
                     None,0.0 : 0.0,None,0.0 : 0.0,None,MELEE_COMBAT_LOW,\n";
        let t = read(file, |_| 7, |_, _| 0x1C1);
        assert!(t.completed);
        let a = &t.actions[0];
        assert_eq!((a.priority, a.impulse_op, a.opponent_pose, a.opponent_res_not), (5, 1, 1, true));
        assert_eq!(a.impulses[1], Impulse { check: true, impulse: 1, state: 5, range: [0.0, 1.0], interval: 0.5 });
        assert_eq!(a.distance_range, [0.0, 3.0]);
        assert_eq!(a.angle_range[0], -std::f32::consts::PI);
        let r = &t.results[0];
        assert_eq!((r.anim_group, r.anim, r.sync_start_time, r.homing_target_type), (7, 0x1C1, 2, 2));
        assert_eq!(r.strike_bones[0], (true, 0x4C3, 0.06));
        assert_eq!(r.self_damage, 40.0);
        assert_eq!(wrap(4.0), 4.0 - f32::from_bits(0x40C9_0FDB));
    }
}
