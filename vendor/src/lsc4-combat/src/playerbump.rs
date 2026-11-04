//! The player's answer to bumping a ped (board PD4): CEventPlayerCollisionWithPed (4, vtable 0x8201B0FC), which the
//! collision scanner's ped case (a block at 0x824631A0 in 0x82462A08) posts to the player when his last contact (+0x16C
//! with +0x170 above 0) was a ped, and its handler 0x82471158: Niko shoves the ped aside (CTaskSimpleShovePed, 318, as a
//! secondary task), flinches (CTaskComplexHitResponse) or is knocked over (CTaskComplexFallAndGetUp). Xbox 360 exe, TU8.
//!
//! The event (0x82303638) keeps the scanning ped's move state at +0x40 (0x823C2800: 1 still, 2 walking, 3 running,
//! 4 sprinting) and the other's at +0x42 (its intelligence +0x2E8 >> 29). It concerns the player (0x82306FA0 then
//! 0x821B3448) only while he is alive, both are on foot and not attached, and he is at least walking (+0x40 2 or more).
//!
//! The handler, with `d` = the ped's place less the player's: the ped is in front when the player's forward row dotted
//! with `d` is 0 or more, and faces him unless its own forward row dotted with `d` is above 0. A ped following its
//! leader in formation (its first task 913) is left alone. Then, by the two move states:
//! - walking into a walking ped, running or sprinting into a still one, or into a moving one that faces away, him in
//!   front: a shove, unless it is the ped he shoved last (+0xD30), a friend (0x823C1328) or under 100 health;
//! - running or sprinting into a moving ped that faces him, it in front: knocked over toward the side it is on when he
//!   is the weaker (pedpersonality's DefendWeakness, model info +0xF8: his above 1.5, and above the ped's when that is
//!   above 1.5 too; never against a walking ped), else sprinting: a flinch from that side; running: nothing;
//! - still, a moving ped that faces him: a flinch from its side unless it walks (never, as the event needs him moving).
//!
//! The side (0 front, 1 left, 2 behind, 3 right) is the sector of his walk-round rectangle (0x8220E130 at the ped's
//! height, 0.25 m round him) the ped's place is in (0x82209E38: the lines through the centre and each corner, 0x82209DA8).
//!
//! Checked against the exe in `tools/xbox360-re/checks/playerbump.py` (the handler, the side and the shove's clip).

/// The move states (0x82411B50).
pub mod state {
    pub const STILL: i32 = 1;
    pub const WALK: i32 = 2;
    pub const RUN: i32 = 3;
    pub const SPRINT: i32 = 4;
}

/// CTaskComplexFollowLeaderInFormation: a ped running it as its first task is not answered.
pub const IN_FORMATION: u32 = 913;
/// The walk-round rectangle's margin round the player (0x82003C6C).
pub const MARGIN: f32 = 0.25;

/// What the handler gives the player.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Answer {
    Nothing,
    /// CTaskSimpleShovePed (0x82435F98) on his secondary slot (0x82419FC8, slot 4), the ped kept as the one he shoved
    /// last (0x822F27F0).
    Shove,
    /// CTaskComplexHitResponse (0x824C8508) from a side: HitFromFront / Left / Back / Right (404, 405, 403, 406),
    /// damage_react's 0xC1 / 0xC3 / 0xC2 / 0xC4 (dam_ad.xad), blend 4.
    HitResponse(u32),
    /// CTaskComplexFallAndGetUp (0x82658688) to a side, 0 ms.
    FallAndGetUp(u32),
}

/// What the handler reads. `me` / `other` the event's +0x40 / +0x42; `forward` the player's and the ped's forward rows
/// (matrix +0x10); `d` the ped's place less his.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Bump {
    pub me: i32,
    pub other: i32,
    pub my_forward: [f32; 3],
    pub other_forward: [f32; 3],
    pub d: [f32; 3],
    /// The ped's first task is [`IN_FORMATION`].
    pub in_formation: bool,
    /// The ped is the one he shoved last (+0xD30, 0x822F2860).
    pub shoved_last: bool,
    /// 0x823C1328 on his intelligence: a friend.
    pub friend: bool,
    /// The ped's health (+0x1E4).
    pub health: f32,
    /// DefendWeakness (model info +0xF8): his and the ped's.
    pub my_weakness: f32,
    pub other_weakness: f32,
}

/// The sum fmuls y, fmadds z, fmadds x of the handler (`a`'s lanes against `d`'s, in its order).
fn dot_yzx(a: [f32; 3], d: [f32; 3]) -> f32 {
    d[0].mul_add(a[0], a[2].mul_add(d[2], a[1] * d[1]))
}

/// 0x82471158: the answer, `side` worked out when one is needed.
pub fn answer(b: &Bump, side: impl FnOnce() -> u32) -> Answer {
    let front = dot_yzx(b.my_forward, b.d) >= 0.0;
    // fmuls y, fmadds z, fmadds x; kept when it is 0 or less (ble: a NaN faces away).
    let faces = b.other_forward[0].mul_add(b.d[0], b.other_forward[2].mul_add(b.d[2], b.other_forward[1] * b.d[1])) <= 0.0;
    if b.in_formation {
        return Answer::Nothing;
    }
    // The shove's own tests (+0x270's no-shove bit only counts in a build mode IV doesn't run, 0x82B39504 2).
    let shove = || if b.shoved_last || b.friend || b.health < 100.0 { Answer::Nothing } else { Answer::Shove };
    let moving = |s: i32| matches!(s, 2..=4);
    let still = |s: i32| matches!(s, 0 | 1);
    let (me, other) = (b.me, b.other);
    if me == state::WALK && other == state::WALK {
        return if front { shove() } else { Answer::Nothing };
    }
    if matches!(me, 3 | 4) && moving(other) {
        if !front {
            return Answer::Nothing;
        }
        if !faces {
            return shove();
        }
        let weaker = other != state::WALK && if b.my_weakness > 1.5 {
            if b.other_weakness > 1.5 { b.my_weakness > b.other_weakness } else { true }
        } else {
            false
        };
        return if weaker {
            Answer::FallAndGetUp(side())
        } else if me == state::SPRINT {
            Answer::HitResponse(side())
        } else {
            Answer::Nothing
        };
    }
    if still(me) && moving(other) && faces {
        let s = side();
        return if other == state::WALK { Answer::Nothing } else { Answer::HitResponse(s) };
    }
    if !matches!(me, 2..=4) || !still(other) || !front {
        return Answer::Nothing;
    }
    shove()
}

/// 0x82209E38 with 0x82209DA8: the sector of the rectangle (`centre` +0x00, `corners` +0x10..+0x40) that `p` lies in:
/// the first `i` (taking the lines in turn from the last) whose line (through the centre and corner `i - 1`) has `p` on
/// or ahead of it and whose next (corner `i`) has it behind; 0 when none does.
pub fn side(centre: [f32; 3], corners: [[f32; 3]; 4], p: [f32; 3]) -> u32 {
    // 0x82209DA8: each corner less the centre, turned a quarter (-y, x, 0), and its offset through the corner.
    let lines: [([f32; 3], f32); 4] = corners.map(|c| {
        let v = [c[0] - centre[0], c[1] - centre[1], c[2] - centre[2]];
        let n = [-v[1], v[0], 0.0f32];
        let off = -(n[1].mul_add(c[1], n[2].mul_add(c[2], n[0] * c[0])));
        (n, off)
    });
    // fmuls z, fmadds y, fadds the offset, fmadds x.
    let ahead = |(n, off): ([f32; 3], f32)| n[0].mul_add(p[0], n[1].mul_add(p[1], n[2] * p[2]) + off);
    for k in 4..8 {
        let (prev, cur) = ((k - 1) % 4, k % 4);
        if ahead(lines[prev]) < 0.0 {
            continue;
        }
        if ahead(lines[cur]) < 0.0 {
            return cur as u32;
        }
    }
    0
}

/// The concerns test of the collision events (CEventPedCollisionWithPlayer and CEventPlayerCollisionWithPed: 0x82306FA0
/// then 0x821B3448) for the ped `r` the event is given, about the other `o`. Each field is what the exe reads.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Concerns {
    /// `r`'s health (+0x1E4) at 0 or below with its death state (+0x9C4) 1 or 2.
    pub dead: bool,
    /// `r` attached to something (+0x1E2's low nibble 2 or more).
    pub attached: bool,
    /// `r` runs a melee move (task 433, 0x823C0E98).
    pub melee_move: bool,
    /// `r` or `o` in a vehicle (+0x23C bit 29).
    pub in_vehicle: bool,
    pub other_in_vehicle: bool,
    /// `o`'s static count (intelligence +0x264).
    pub other_static: i32,
    /// `r` hates `o`, dislike counted (0x823C1458 with 1).
    pub hates: bool,
    /// The event's +0x40.
    pub me: i32,
    /// `r`'s forward row and the event's normal (+0x20).
    pub forward: [f32; 3],
    pub normal: [f32; 3],
    /// `r` already walking round `o` (its task 517 with +0x70 `o`).
    pub walking_round: bool,
    /// Both run task 1202, or both run 1203 (0x82159078 on each one's tree, 0x821B3448).
    pub both_1202: bool,
    pub both_1203: bool,
}

/// The static count past which the other's bump is no event (0x82014B6C: `route::MAX_BUMPS`, 30).
pub const MOST_STATIC: i32 = 30;

/// 0x82306FA0 then 0x821B3448: whether the event concerns the ped.
pub fn concerns(c: &Concerns) -> bool {
    // fmuls y, fmadds z, fmadds x; past 1 refused.
    let d = c.forward[0].mul_add(c.normal[0], c.forward[2].mul_add(c.normal[2], c.forward[1] * c.normal[1]));
    !(c.dead || c.attached || c.melee_move || c.in_vehicle || c.other_in_vehicle || c.other_static > MOST_STATIC || c.hates || c.me < 2
      || d > 1.0 || c.walking_round || c.both_1202 || c.both_1203)
}

/// 0x821896B0: a count (a byte) that a call adds to at most once a window: 3000 ms from the add before (+4 its start,
/// +8 its length, +0xC set once started, +0xD to restart it from the next call).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RateCount {
    pub count: u8,
    pub start: u32,
    pub length: u32,
    pub started: bool,
    pub restart: bool,
}

impl RateCount {
    /// The window's length (0xBB8).
    pub const WINDOW: u32 = 3000;

    /// A count whose first add counts (started, its window over).
    pub fn new() -> RateCount {
        RateCount { started: true, ..Default::default() }
    }

    /// 0x821896B0(`n`) at `now` (ms, 0x82C6C2A4): added when the window is over, which starts a new one.
    pub fn add(&mut self, n: u8, now: u32) {
        let over = if self.started {
            if self.restart {
                (self.start, self.restart) = (now, false);
            }
            (self.length as i32).wrapping_add(self.start as i32) <= now as i32
        } else {
            false
        };
        if !over {
            return;
        }
        (self.start, self.length, self.started) = (now, Self::WINDOW, true);
        self.count = self.count.wrapping_add(n);
    }
}

/// 0x824360D8: the shove's clip in the door group (11, ped.xad), by where the ped is across him: `right` his right row,
/// `back` his place less the ped's, `running` his +0xABC 3 or 4. With a weapon whose model is the one in his hand
/// (+0x294 and vf 0x118) only a shove from the left (0x99), none from the right. None of 0x96..0x9A: no shove.
pub fn shove_clip(right: [f32; 3], back: [f32; 3], running: bool, armed: bool) -> Option<u32> {
    let x = right[0].mul_add(back[0], right[1].mul_add(back[1], right[2] * back[2]));
    if armed {
        return if x < 0.0 { None } else { Some(0x99) };
    }
    // The two limits are doubles (lfd 0x8201A2A0, 0x82003C50): the single compared with them as it is; a NaN goes the
    // first way.
    let (x, run) = (x as f64, if running { 2 } else { 0 });
    Some(if !(x >= -0.2) { 0x98 + run } else if x < 0.2 { 0x96 } else { 0x97 + run })
}

/// The door group's rows' clip player flags (0x82A960C8): bone mask 8 (HEAD_NECK_AND_ARMS), 0x4000 dropped once
/// blended out, 0x8000 blending out at its end.
pub const SHOVE_FLAGS: u32 = 0xC008;

/// The door group's anims (0x82A960A0 names, 0x82A960C8 rows: flags [`SHOVE_FLAGS`], layer 4) that a shove plays.
pub const SHOVES: [(u32, &str); 5] = [
    (0x96, "ped/run_open_door_shove"),
    (0x97, "ped/open_door"),
    (0x98, "ped/open_door_r"),
    (0x99, "ped/run_open_door"),
    (0x9A, "ped/run_open_door_r"),
];

