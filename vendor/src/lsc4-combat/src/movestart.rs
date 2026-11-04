//! The on-foot blend's start (board PD5): a ped that sets off from standing plays a start clip, picked by 0x82414638
//! (called from the on-foot step 0x82417438 at 0x8241791C) by how far it has to turn and whether it runs. While the
//! clip plays, the clip pick 0x82294E90 keeps the gait at idle (its state 1); when the clip ends it removes it at once
//! (0x822949C8) and sets the ratio to a walk or a run. Read in `docs/peds/movement.md` (Starts).

/// The start clips' ids (the move clip names at 0x82A97824, id = index + 0xE).
pub const WSTART: u32 = 0x12;
pub const WSTART_TURN_L90: u32 = 0x13;
pub const WSTART_TURN_R90: u32 = 0x14;
pub const WSTART_TURN_L180: u32 = 0x15;
pub const WSTART_TURN_R180: u32 = 0x16;
pub const RUNSTART_FWD: u32 = 0x17;
pub const RUNSTART_L_90: u32 = 0x18;
pub const RUNSTART_R_90: u32 = 0x19;
pub const RUNSTART_L_180: u32 = 0x1A;
pub const RUNSTART_R_180: u32 = 0x1B;

/// A start clip blends in at 4 a second (0x82AA305C, or 0x82000D6C when the blend's vf +0x58 says so; both 4).
pub const BLEND_IN: f32 = 4.0;

/// The gap is held to Â±178 degrees (0x82018A00); a 180 start turns that far.
const GAP_MAX: f32 = f32::from_bits(0x4046_D3D8);
/// Under 85 degrees (0x820189E8) the start is straight.
const STRAIGHT: f32 = f32::from_bits(0x3FBD_E44F);
/// Under 120 degrees (0x820189E4) it turns 90 (0x82012EA4), else 180.
const QUARTER: f32 = f32::from_bits(0x4006_0A92);

/// Whether the start is a run start (0x82414638 at 0x82414668-0x82414790): the wanted ratio (move blend +0x10) is 2 or
/// more (1.6, 0x82004E28, for the player: the blend's +0x50 bit 18), and the clip set has the run start flag (+0x378 bit
/// 29), and the blend's ceiling (+0x70) is 2 or more, and its +0x50 bit 12 is clear.
pub fn start_run(wanted: f32, player: bool, set_flag: bool, ceiling: f32, bit12: bool) -> bool {
    wanted >= if player { 1.6 } else { 2.0 } && set_flag && !bit12 && ceiling >= 2.0
}

/// The start clip and how far it turns by itself (0x82414A2C-0x82414AD8), from the gap between the ped's heading and
/// the wanted one (+0x9F4 âˆ’ +0x9F0, wrapped by 0x8245BC88). `turns` is off when the switches 0x82AA3039/3A/3B/58 (all 1
/// as shipped) or the set's flags (+0x378 bit 31 set and bit 30 clear) say no turning starts.
pub fn start_clip(gap: f32, run: bool, turns: bool) -> (u32, f32) {
    let gap = gap.clamp(-GAP_MAX, GAP_MAX);
    let r = if run { RUNSTART_FWD - WSTART } else { 0 };
    if gap.abs() < STRAIGHT || !turns {
        (WSTART + r, 0.0)
    } else if gap.abs() < QUARTER {
        if gap > 0.0 { (WSTART_TURN_L90 + r, std::f32::consts::FRAC_PI_2) } else { (WSTART_TURN_R90 + r, -std::f32::consts::FRAC_PI_2) }
    } else if gap > 0.0 {
        (WSTART_TURN_L180 + r, GAP_MAX)
    } else {
        (WSTART_TURN_R180 + r, -GAP_MAX)
    }
}

/// The turn the step adds while the start plays (0x82414B6C-0x82414BBC, into move blend +0x90, a rate): what the clip
/// does not turn, over its length, held to Â± the ped's turn speed (+0x9F8, degrees) Ã— Ï€/180 Ã— 50. 0 for a straight
/// start, whose turn rate stays the blend's own (+0x28).
pub fn start_rate(gap: f32, turn: f32, duration: f32, turn_speed: f32) -> f32 {
    let max = turn_speed * f32::from_bits(0x3C8E_FA35) * 50.0;
    ((gap.clamp(-GAP_MAX, GAP_MAX) - turn) / duration).clamp(-max, max)
}

/// The start is done (0x82295590-0x82295610): its time and this frame's step reach its length.
pub fn start_done(time: f32, step: f32, duration: f32) -> bool {
    time + step >= duration
}

/// The ratio when the start ends (0x82295620-0x82295728): a run (2) after a run start, else a walk (1).
pub fn start_ratio(id: u32) -> f32 {
    if (RUNSTART_FWD..=RUNSTART_R_180).contains(&id) { 2.0 } else { 1.0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_gap_picks_straight_quarter_or_half_turns() {
        assert_eq!(start_clip(0.5, false, true), (WSTART, 0.0));
        assert_eq!(start_clip(1.6, false, true).0, WSTART_TURN_L90);
        assert_eq!(start_clip(-1.6, true, true).0, RUNSTART_R_90);
        assert_eq!(start_clip(3.0, true, true), (RUNSTART_L_180, GAP_MAX));
        assert_eq!(start_clip(-3.0, false, true).0, WSTART_TURN_R180);
        // No turning starts: straight whatever the gap.
        assert_eq!(start_clip(3.0, true, false).0, RUNSTART_FWD);
    }

    #[test]
    fn run_starts_need_a_run_the_flag_and_the_ceiling() {
        assert!(start_run(2.0, false, true, 3.0, false));
        assert!(!start_run(1.8, false, true, 3.0, false));
        assert!(start_run(1.8, true, true, 3.0, false));
        assert!(!start_run(2.0, false, false, 3.0, false));
        assert!(!start_run(2.0, false, true, 1.0, false));
        assert!(!start_run(2.0, false, true, 3.0, true));
    }

    #[test]
    fn the_rest_of_the_turn_is_spread_over_the_clip() {
        // 100 degrees asked, a 90 clip, 1 s long: 10 degrees a second added.
        let r = start_rate(100f32.to_radians(), std::f32::consts::FRAC_PI_2, 1.0, 10.0);
        assert!((r - 10f32.to_radians()).abs() < 1e-5);
        // Held to the turn speed x 50 per second.
        assert!((start_rate(3.0, 0.0, 0.01, 1.0) - 50f32.to_radians()).abs() < 1e-5);
        assert!(start_done(0.9, 0.1, 1.0) && !start_done(0.8, 0.1, 1.0));
        assert_eq!((start_ratio(WSTART_TURN_R180), start_ratio(RUNSTART_FWD)), (1.0, 2.0));
    }
}
