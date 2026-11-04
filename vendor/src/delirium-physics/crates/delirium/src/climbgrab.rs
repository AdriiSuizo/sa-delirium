//! Climbing onto something moving. The jump's control (824E1430) watches the climb (CTaskSimpleClimb, 254, found by
//! 826A5D50 down the sub-task chain; its state, +20, through 826A5CE0): hanging, shimmying or at the waist (states 2 to 5,
//! and past 8) on a vehicle, ped or object (+40 bits 22..25 of 2, 3, 4) that moves faster than 0.75 m/s, and the ped not in
//! a task 268, it makes the jump's sub-task 2109 (824DFFE8: NMBalance toward the target with a GrabHelper holding it,
//! `grabhelper`). A vehicle with +4836 of 2 must move at 10 m/s; of 3 (a train) the sub-task is 241 instead,
//! CTaskSimpleJumpInAir (ctor 0x824DDA98, made at 0x824E16F8 with 8.0, 9 and 0x7E).
//! The grab points (824DF7F8) are the probe's hit (a sphere round the hand bone, 1232 and 1219) on the target, point and
//! normal, in the target's frame.
use crate::prelude::*;
use crate::Fma;

/// 0x820132E0: 0.75 squared.
pub const MOVING: f32 = 0.5625;
/// 0x82000D68, a vehicle with +4836 of 2.
pub const MOVING_FAST: f32 = 100.0;
/// The hand bones 8260EA28's points are made for (824DFFE8 case 2109): its first and second.
pub const HANDS: [u16; 2] = [1232, 1219];
/// 82367988's probe sphere (824DF7F8's f1, 0x8201A6BC).
pub const PROBE_RADIUS: f32 = 0.2;
/// 824DFFE8 case 2109's NMBalance (82611900): 1000 to 10000 ms, r7 1, its lean +80 2.5 (0x8201E4D0) along `lean_dir`,
/// stopped after +228 = 1 s (0x82000D48), all under CTaskComplexNM 2000 / 20000 ms (826154F0).
pub const BALANCE_MS: [u32; 2] = [1000, 10000];
/// Its CTaskComplexNM's (826154F0): the NMRelax a Balance done holding on goes to (wish 0x838, 8261CC48) runs on them.
pub const COMPLEX_MS: [u32; 2] = [2000, 20000];
pub const LEAN: f32 = 2.5;
pub const LEAN_TIME: f32 = 1.0;

/// 826A5CE0: the climb's state counts as climbing (states 2, 3, 4, 5 and past 8; 0, 1, 6, 7, 8 do not).
pub fn climbing(state: u32) -> bool {
    matches!(state, 2..=5) || state > 8
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Next {
    /// 2109: NMBalance with the grab.
    Grab,
    /// 241: CTaskSimpleJumpInAir (a train, +4836 of 3).
    JumpInAir,
}

/// 824E1430's climb branch: the sub-task the jump makes (`kind` the target's +40 bits 22..25, `status` its +4836, `state`
/// the climb's, `vel` the target's collider velocity (vf 232)), none when it does not.
pub fn moving_climb(kind: u32, status: i32, state: u32, vel: Vec3) -> Option<Next> {
    if !(2..5).contains(&kind) || !climbing(state) {
        return None;
    }
    let fast = kind == 2 && status == 2;
    if vel.z.fma(vel.z, vel.x.fma(vel.x, vel.y * vel.y)) <= if fast { MOVING_FAST } else { MOVING } {
        return None;
    }
    Some(if kind == 2 && status == 3 { Next::JumpInAir } else { Next::Grab })
}

/// 824DF7F8's last step: a world point and normal in the target's frame, `axes` its matrix's (the console takes the dots
/// with the matrix's columns, vmsum3fp on its transposed rows), `at` its place.
pub fn in_frame(axes: [Vec3; 3], at: Vec3, p: Vec3, n: Vec3) -> (Vec3, Vec3) {
    let d = p - at;
    (Vec3::new(axes[0].dot(d), axes[1].dot(d), axes[2].dot(d)), Vec3::new(axes[0].dot(n), axes[1].dot(n), axes[2].dot(n)))
}

/// 824DFFE8's lean direction (its +48): the right hand's bone (1232's global matrix, 8217DBE0) from the ped's place
/// (+32's +48), over its length by the console's estimate refined once (vrsqrtefp; an infinite estimate gives 0; then
/// e + (1 - x e²) 0.5 e, fused).
pub fn lean_dir(hand: Vec3, at: Vec3) -> Vec3 {
    use crate::Vd;
    let d = hand - at;
    let x = d.vd(d);
    let e = crate::estimate::vrsqrtefp(x);
    let e = if e.is_infinite() { 0.0 } else { e };
    d * (-x).mul_add(e * e, 1.0).mul_add(0.5 * e, e)
}

/// 8260EA28 for the points 824DFFE8 made (each hand's, the ones that hit, packed: the first is pos1 with normalL, the
/// second pos2 with normalR) on the entity `instance`, its part the first hit's (821C3040: +108), then case 2109's own fields: grabStrength
/// (+112) rand(80, 110) (82158E78), the time to grab (+116) 1 s, and for the player (ped vf 276) the time to hold (+120)
/// 5 s (0x82053E68); a ped's stays 0 (held until let go).
pub fn jump_grab(instance: i32, part: i32, points: &[(Vec3, Vec3)], rand: &mut delirium_data::rng::Rand, player: bool) -> crate::grabhelper::GrabHelper {
    let mut g = crate::grabhelper::GrabHelper::new();
    let p = |k: usize| points.get(k).map(|x| x.0);
    let n = |k: usize| points.get(k).map(|x| x.1);
    g.set_points(Some((instance, part)), [p(0), n(0), p(1), n(1), None, None]);
    g.strength = rand.float_in_range(80.0, 110.0);
    g.find_left = 1.0;
    if player {
        g.hold_left = 5.0;
    }
    g
}

/// 0x8241DFA8 gate-16 yes, item 4: the NMBalance's direction (+48) is the horizontal unit vector from the ped to the door
/// probe's hit point, or with no hit the car's forward row.
pub fn door_dir(at: Vec3, hit: Option<Vec3>, forward: Vec3) -> Vec3 {
    hit.map_or(forward, |h| Vec3::new(h.x - at.x, h.y - at.y, 0.0).normalize_or_zero())
}

/// 0x8241DFA8 item 7 supplies its ONE probe hit to BOTH hands. The original
/// 0x8241F130..0x8241F174 sets r5 == r7 (point) and r6 == r8 (normal) before
/// 8260EA28. This differs from the climbing caller's independently probed hands.
/// No hit leaves both unset (lean only). Strength/hold time remain the original.
pub fn door_grab(instance: i32, part: i32, hit: Option<(Vec3, Vec3)>, rand: &mut delirium_data::rng::Rand, player: bool,
                 tp: &delirium_data::taskparams::TaskParams) -> crate::grabhelper::GrabHelper {
    let both = hit.map(|p| [p, p]);
    let mut g = jump_grab(instance, part, both.as_ref().map_or(&[], |p| &p[..]), rand, player);
    g.block = crate::grabhelper::read_block(tp);
    g
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn entry_single_hit_is_given_to_both_hands_as_8241f174() {
        use crate::msg::Value;
        let tp = delirium_data::taskparams::TaskParams::default();
        let hit = (Vec3::new(0.3, -0.7, 0.2), Vec3::Y);
        let mut r = delirium_data::rng::Rand::new(417);
        let mut expected_rng = r.clone();
        let expected = jump_grab(9, 11, &[hit, hit], &mut expected_rng, false);
        let mut actual = door_grab(9, 11, Some(hit), &mut r, false, &tp);
        assert_eq!((actual.pos1, actual.normal_l, actual.pos2, actual.normal_r), (hit.0, hit.1, hit.0, hit.1));
        assert_eq!(actual.instance, Some((9, 11)));
        assert_eq!(actual.strength, expected.strength);
        assert_eq!((actual.find_left, actual.hold_left), (1.0, 0.0));
        assert_eq!(r.float_in_range(0.0, 1.0), expected_rng.float_in_range(0.0, 1.0), "same RNG consumption");
        let msg = actual.start().unwrap();
        assert!(msg.1.iter().any(|(k,v)| *k == "useLeft" && matches!(v, Value::Bool(true))));
        assert!(msg.1.iter().any(|(k,v)| *k == "useRight" && matches!(v, Value::Bool(true))));
        let mut miss = door_grab(9, 11, None, &mut r, false, &tp);
        assert!(miss.start().is_none(), "no contact must not synthesize an anchor");
        let mut climb = jump_grab(9, 11, &[hit], &mut r, false);
        assert_eq!(climb.pos2, Vec3::ZERO, "climbing's independently probed hand remains absent");
        assert!(climb.start().unwrap().1.iter().any(|(k,v)| *k == "useRight" && matches!(v, Value::Bool(false))));
    }

    #[test]
    fn the_door_leans_to_the_hit_or_forward() {
        let at = Vec3::new(1.0, 1.0, 1.0);
        assert!((door_dir(at, Some(Vec3::new(1.0, 4.0, 9.0)), Vec3::X) - Vec3::Y).length() < 1e-6);
        assert_eq!(door_dir(at, None, Vec3::X), Vec3::X);
    }

    #[test]
    fn the_frame_takes_dots_with_the_axes() {
        let a = [Vec3::Y, -Vec3::X, Vec3::Z];
        let (p, n) = in_frame(a, Vec3::new(1.0, 2.0, 3.0), Vec3::new(1.0, 5.0, 3.5), Vec3::X);
        assert_eq!((p, n), (Vec3::new(3.0, 0.0, 0.5), Vec3::new(0.0, -1.0, 0.0)));
    }

    #[test]
    fn the_grab_goes_to_the_car() {
        let mut r = delirium_data::rng::Rand::new(1);
        assert!(jump_grab(3, 0, &[], &mut r, true).start().is_none());
        let mut g = jump_grab(3, 0, &[(Vec3::X, Vec3::Y), (Vec3::Z, Vec3::Y)], &mut r, true);
        assert!((80.0..=110.0).contains(&g.strength) && g.find_left == 1.0 && g.hold_left == 5.0);
        assert_eq!(jump_grab(3, 0, &[(Vec3::X, Vec3::Y)], &mut r, false).hold_left, 0.0);
        let m = g.start().unwrap();
        assert!(m.1.iter().any(|x| x.0 == "instanceIndex"));
        assert!(m.1.iter().any(|x| x.0 == "pos2"));
    }

    #[test]
    fn the_lean_is_toward_the_hand() {
        let d = lean_dir(Vec3::new(3.0, 4.0, 1.0), Vec3::new(0.0, 0.0, 1.0));
        assert!((d - Vec3::new(0.6, 0.8, 0.0)).length() < 1e-5);
        assert_eq!(lean_dir(Vec3::ONE, Vec3::ONE), Vec3::ZERO);
    }

    /// NM_MOVCLIMB: 824E1430 run by the game (nm-workbench oracle/movclimb_all.sh) with a fake jump task over a climb in
    /// each state on a target of each kind, status and velocity: the sub-task it hands back (none, 2109 with the jump's +60
    /// set to 0x1001, or another), bit for bit.
    #[test]
    fn moving_climb_as_the_game() {
        let Ok(path) = std::env::var("NM_MOVCLIMB") else { return };
        let log = std::fs::read_to_string(path).unwrap();
        let h = |x: &str| u32::from_str_radix(x, 16).unwrap();
        let mut n = 0;
        let mut seen = [0; 3];
        for l in log.lines() {
            let w: Vec<&str> = l.split_whitespace().collect();
            // case <ent> <kind> <status> <state> vx vy vz ret R|same w0 W t60 T
            let (kind, status, state): (u32, i32, u32) = (w[2].parse().unwrap(), w[3].parse().unwrap(), w[4].parse().unwrap());
            let vel = Vec3::new(f32::from_bits(h(w[5])), f32::from_bits(h(w[6])), f32::from_bits(h(w[7])));
            // `ret same`: the jump's sub-task handed back unchanged.
            let (none, t60) = (w[9] == "same", h(w[13]) >> 16);
            let got = moving_climb(kind, status, state, vel);
            let want = if none { None } else if t60 == 0x1001 { Some(Next::Grab) } else { Some(Next::JumpInAir) };
            assert_eq!(got, want, "{l}");
            seen[got.map_or(0, |g| if g == Next::Grab { 1 } else { 2 })] += 1;
            n += 1;
        }
        eprintln!("moving_climb: {n} cases (none {}, grab {}, other {})", seen[0], seen[1], seen[2]);
        assert!(n > 100 && seen[1] > 0);
    }
}
