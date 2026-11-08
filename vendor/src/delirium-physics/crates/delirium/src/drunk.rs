//! Drunk Niko (TU8 romandrink.sco and its four twins; LSC4 board SC4b, `gta4_data::drunk`). The drink scripts put the
//! player on a script-controlled ragdoll with this bodyBalance (0x4CD5) and give the stick to it
//! (GIVE_PLAYER_RAGDOLL_CONTROL, `playerctl`). The watch that gets him up after a fall is the host's (0x104CD).

/// The time on the drunk ragdoll by drunk level (0x4BA0): none sober.
pub fn ragdoll_ms(level: u32) -> u32 {
    match level {
        1 => 60_000,
        2 => 90_000,
        3 => 120_000,
        4 => 140_000,
        5 => 180_000,
        _ => 0,
    }
}

/// The drunk ragdoll's bodyBalance (0x4CD5, sent with start after SWITCH_PED_TO_RAGDOLL kind 1): the float params.
pub const BALANCE: [(&str, f32); 18] = [
    ("headLookAlwaysTimeout", 8.7),
    ("bodyTurnTimeout", 0.6),
    ("bodyStiffness", 8.4),
    ("damping", 0.7),
    ("armsOutOnPushMultiplier", 0.8),
    ("armsOutOnPushTimeout", 999.0),
    ("shoulder", 1.4),
    ("elbow", 1.95),
    ("returningToBalanceArmsOut", 1.0),
    ("turnAwayProb", 0.0),
    ("turnLeftProb", 0.1),
    ("turnRightProb", 0.1),
    ("turnOffProb", 0.0),
    ("turn2TargetProb", 0.6),
    ("turn2VelProb", 0.2),
    ("somersaultAngleThreshold", 0.1),
    ("sideSomersaultAngleThreshold", 0.1),
    ("bendElbowsGait", -0.3),
];
/// The rest of it: maxSteps (an int) and armsOutOnPush (a bool).
pub const MAX_STEPS: i32 = 0xFFFF;
pub const ARMS_OUT_ON_PUSH: bool = true;
