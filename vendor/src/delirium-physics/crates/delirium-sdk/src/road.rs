//! Host adaptation of bump::react -> NMBrace / RollUpAndRelax. No added impulse.
//! The host keeps damage authority and supplies health on IV's 0..200 scale.
use super::*;
use delirium::bump::{self, Kind as ThingKind, Thing};

#[derive(Default)]
pub(super) struct Road {
    state: i32, // 0 off, 1 awaiting solved contact, 2 brace, 3 roll/relax, 4 relax
    roll: Option<nm::RollUpAndRelax>,
    elapsed: f32,
    minimum: f32,
}

impl Road {
    pub(super) fn before_step(&mut self, agent: &mut Agent, build: &Build, world: &Collision, dt: f32) {
        if self.state != 0 { self.elapsed += dt; }
        // Obstacle indices belong to this host step only. A previous strongest
        // contact must not name a different car after the world is rebuilt.
        if matches!(self.state, 1 | 2) { agent.doll.contact = bump::ContactRecord::new(); }
        if let Some(roll) = &mut self.roll {
            roll.control(agent, build, world, &mut RAND.lock().unwrap_or_else(|e| e.into_inner()), dt);
        }
    }
}

fn stop_all(a: &mut NmAgent, w: &NmWorld) {
    let mut st = std::mem::take(&mut a.agent.msgs);
    msg::send(&mut a.agent, &a.build, &w.0, &mut st, "stopAllBehaviours", &[]).expect("known task message");
    a.agent.msgs = st;
}

/// Anticipatory NMBrace, using the original taskparams and random hold time.
/// Does not apply momentum. The host refreshes the target/vehicle before stepping.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_preview(a:*mut NmAgent,w:*const NmWorld,weak:i32)->i32 {
    let a=agent!(a);
    let Some(w)=(unsafe{w.as_ref()}) else{return fail(NM_E_ARG,"null world")};
    if weak!=0 && weak!=1 { return fail(NM_E_ARG,"invalid weak flag") }
    stop_all(a,w);
    with_tp(|tp|nm::task_brace(&mut a.agent,&a.build,&w.0,tp,if weak!=0{"WEAK"}else{"*"},&mut RAND.lock().unwrap_or_else(|e|e.into_inner()),false));
    a.road=Road{state:2,..Default::default()};
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_cancel(a: *mut NmAgent, w: *const NmWorld) -> i32 {
    let a = agent!(a);
    let Some(w) = (unsafe { w.as_ref() }) else { return fail(NM_E_ARG, "null world") };
    if a.road.state != 0 { stop_all(a, w); }
    a.road = Road::default();
    a.agent.doll.contact = bump::ContactRecord::new();
    a.agent.doll.road_watch = -1;
    a.agent.doll.road_watch_owner = None;
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_arm(a: *mut NmAgent, w: *const NmWorld) -> i32 {
    let a = agent!(a);
    let Some(w) = (unsafe { w.as_ref() }) else { return fail(NM_E_ARG, "null world") };
    stop_all(a, w);
    a.road = Road { state: 1, ..Default::default() };
    a.agent.contact_wake = true;
    a.agent.doll.contact = bump::ContactRecord::new();
    a.agent.doll.road_watch = -2;
    a.agent.doll.road_watch_owner = None;
    0
}

/// Select the vehicle slot from the obstacle snapshot used by the next solve.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_watch(a: *mut NmAgent, vehicle: i32, owner: u64) -> i32 {
    let a = agent!(a);
    if vehicle < -1 { return fail(NM_E_ARG, "invalid road vehicle"); }
    a.agent.doll.road_watch = vehicle;
    a.agent.doll.road_watch_owner = (vehicle>=0 && owner!=0).then_some(owner);
    0
}

/// IV's per-impact damage input (82465360): the strongest solved contact of the
/// steps since the last call, ground included. out: summed impulse (N s),
/// component, other obstacle id (-1 world), inverse mass. Returns 1 when there
/// was a contact, 0 when none; the record is cleared either way.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_take_impact(a: *mut NmAgent, out: *mut f32) -> i32 {
    let a = agent!(a);
    if out.is_null() { return fail(NM_E_ARG, "null impact output"); }
    let r = std::mem::replace(&mut a.agent.doll.hurt, bump::ContactRecord::new());
    let hit = r.strongest > 0.0 && r.sum > 0.0;
    let values = [if hit { r.sum } else { 0.0 }, r.component as f32, r.other.map_or(-1.0, |o| o as f32), 1.0 / a.agent.doll.mass()];
    unsafe { std::ptr::copy_nonoverlapping(values.as_ptr(), out, 4) };
    hit as i32
}

/// Ground impact evidence for host audio, independent of the damage record.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_take_ground_impact(a:*mut NmAgent,out:*mut f32)->i32{
    let a=agent!(a);
    if out.is_null(){return fail(NM_E_ARG,"null ground impact output")}
    let [impulse,closing]=std::mem::take(&mut a.agent.doll.ground_impact);
    let values=[impulse/a.agent.doll.mass(),closing];
    unsafe{std::ptr::copy_nonoverlapping(values.as_ptr(),out,2)};
    i32::from(impulse>0.0)
}

/// Call immediately after a solver step, before replacing obstacle identities.
/// Returns 0 unchanged, 1 brace, 2 roll-up, 3 relax; negatives are SDK errors.
/// report: solved Ns, component, roll milliseconds, minimum task milliseconds.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_contact(a: *mut NmAgent, w: *const NmWorld, vehicle: i32,
    health_iv: f32, flags: u32, dt: f32, report: *mut f32) -> i32 {
    unsafe { nm_agent_road_contact_detail(a, w, vehicle, health_iv, flags, dt, report, std::ptr::null_mut()) }
}

/// Optional geometry output: world point, normal toward the ped, normal
/// closing speed (m/s), strongest impulse (N s), obstacle slot.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_contact_detail(a: *mut NmAgent, w: *const NmWorld, vehicle: i32,
    health_iv: f32, flags: u32, dt: f32, report: *mut f32, geometry: *mut f32) -> i32 {
    let a = agent!(a);
    let Some(w) = (unsafe { w.as_ref() }) else { return fail(NM_E_ARG, "null world") };
    if !health_iv.is_finite() || !(dt > 0.0 && dt <= 0.25) || flags & !7 != 0 || report.is_null() {
        return fail(NM_E_ARG, "invalid road contact arguments");
    }
    if !w.1.iter().any(|o| o.id == vehicle) { return fail(NM_E_ARG, "road vehicle absent from this step"); }
    if !matches!(a.road.state, 1 | 2) { return 0; }
    let c = &mut a.agent.doll.contact;
    if c.other != Some(vehicle) || !(c.strongest > 0.0 && c.sum > 0.0) { return 0; }
    if !geometry.is_null() {
        let [px, py, pz, _] = c.point;
        let [nx, ny, nz, _] = c.normal;
        let values = [px, py, pz, nx, ny, nz, f32::from_bits(c.extra[0]), c.strongest, c.extra[1] as i32 as f32];
        unsafe { std::ptr::copy_nonoverlapping(values.as_ptr(), geometry, values.len()); }
    }
    let car = Thing { id: vehicle, kind: ThingKind::Vehicle { player_driver: flags & 1 != 0 } };
    let imp = c.consume(4, dt, |_| car).expect("positive solved contact");
    let ped = bump::Ped { id: -2, health: health_iv, inv_mass: 1.0 / a.agent.doll.mass(), woke: true,
        drop: None, vz: a.agent.doll.vel(0).0.z, tough: false, player_cut: false, player: flags & 2 != 0,
        mission: flags & 4 != 0, quick_relax: false, agility: 3, gun: false,
        place: a.agent.doll.com().0, vel: a.agent.doll.com().1, standing_on: None };
    // SA keeps its native damage (never applied twice) and unknown SA agility is neutral (3). `health_iv` is the ped's
    // health BEFORE this hit: SA's lethal run-over damage (100) had zeroed it, so every fast hit took IV's dying branch
    // (roll-up/relax) where IV's own does not: 82465360's damage comes from this solved impulse (`bump::damage`,
    // viewer peds.rs:3903..3925) and only a ped it kills dies; the rest brace (<= 400 N s) or roll up.
    let around = bump::Around { touching: vec![car], player: None, blend: None, sit: None, die: None, accepts: true };
    let mut rand = RAND.lock().unwrap_or_else(|e| e.into_inner());
    let amount = bump::damage(&ped, &imp, &mut rand).map_or(0.0, |(amount, _)| amount);
    let health = health_iv - amount;
    let Some(made) = bump::react(&ped, &imp, &around, amount, !(health > 0.0), health, &mut rand, || None) else { return 0 };
    let mut roll_ms = 0;
    let result = match made.task {
        bump::Task::Brace { weak, .. } => {
            if a.road.state == 2 { return 0; } // ongoing contact is not a task restart
            stop_all(a, w);
            with_tp(|tp| nm::task_brace(&mut a.agent, &a.build, &w.0, tp, if weak { "WEAK" } else { "*" }, &mut rand, false));
            a.road.state = 2;
            1
        }
        bump::Task::RollUp { roll } => {
            roll_ms = roll.max(0) as u32;
            a.road.roll = Some(with_tp(|tp| nm::RollUpAndRelax::start(&mut a.agent, &a.build, &w.0, tp, &mut rand, -1.0, 1.0, roll_ms)));
            a.road.state = 3;
            a.agent.doll.road_watch = -1;
            a.agent.doll.road_watch_owner = None;
            2
        }
        bump::Task::Relax { a: stiffness, b: damping } => {
            stop_all(a, w);
            nm::task_relax(&mut a.agent, stiffness, damping);
            a.road.state = 4;
            a.agent.doll.road_watch = -1;
            a.agent.doll.road_watch_owner = None;
            3
        }
        _ => return 0,
    };
    a.road.elapsed = 0.0;
    a.road.minimum = f32::max(made.min as f32, roll_ms as f32) * 0.001;
    unsafe { std::ptr::copy_nonoverlapping([imp.impulse, imp.component as f32, roll_ms as f32, made.min as f32].as_ptr(), report, 4); }
    result
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_recovery_ready(a: *const NmAgent) -> i32 {
    let a = agent_ref!(a);
    (a.road.state == 0 || a.road.elapsed >= a.road.minimum) as i32
}

/// All coordinates are world-space. The original task performs the local conversion.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_road_brace_control(a: *mut NmAgent, w: *const NmWorld, vehicle: i32,
    hit: *const f32, place: *const f32, look: *const f32) -> i32 {
    let a = agent!(a);
    let Some(w) = (unsafe { w.as_ref() }) else { return fail(NM_E_ARG, "null world") };
    let Some(o) = w.1.iter().find(|o| o.id == vehicle) else { return fail(NM_E_ARG, "brace vehicle absent") };
    if hit.is_null() || place.is_null() || look.is_null() { return fail(NM_E_ARG, "null brace point") }
    let (hit, place, look) = unsafe { (v3(hit), v3(place), v3(look)) };
    if !hit.is_finite() || !place.is_finite() || !look.is_finite() { return fail(NM_E_ARG, "invalid brace point") }
    if a.road.state != 2 { return 0; }
    nm::task_brace_control(&mut a.agent, &a.build, &w.0,
        [o.rot * Vec3::X, o.rot * Vec3::Y, o.rot * Vec3::Z, o.at], Some(hit), place, look, vehicle);
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    #[test]
    fn original_selector_and_timed_controller_preserve_momentum() {
        let root = std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path = |name: &str| CString::new(root.join(name).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()), 0 | 1)); // already initialized by another SDK test is success
            assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()), 0);
            let b = nm_build_load(path("fred-build.txt").as_ptr()); assert!(!b.is_null());
            let a = nm_agent_create(b, [0.,0.,5.].as_ptr(), 0., 0.4); assert!(!a.is_null());
            let mut world = NmWorld(Collision::default(), vec![Obstacle { id: 7, ..Default::default() }], vec![]);
            let w = &mut world as *mut NmWorld;
            // Seed actual part motion. Changing tasks must not overwrite it.
            for k in 0..21 {
                let f = (*a).agent.doll.frames().nth(k).unwrap();
                nm_agent_set_part_state(a, k as i32, f.translation.to_array().as_ptr(), f.rotation.to_array().as_ptr(), [2.,1.,-0.5].as_ptr(), [0.2,0.1,0.].as_ptr());
            }
            let velocities = |a: *const NmAgent| (0..21).map(|k| (*a).agent.doll.vel(k)).collect::<Vec<_>>();
            let original = velocities(a);
            let seed_contact = move |impulse: f32, id: i32| {
                (*a).agent.doll.contact = bump::ContactRecord::new();
                (*a).agent.doll.contact.other = Some(id);
                (*a).agent.doll.contact.strongest = impulse;
                (*a).agent.doll.contact.sum = impulse;
                (*a).agent.doll.contact.component = 2;
            };
            let mut report = [0.;4];
            assert_eq!(nm_agent_road_arm(a,w),0);
            assert_eq!(nm_agent_road_contact(a,w,7,200.,0,1./60.,report.as_mut_ptr()),0,"no proximity launch");
            seed_contact(100.,8);
            assert_eq!(nm_agent_road_contact(a,w,7,200.,0,1./60.,report.as_mut_ptr()),0,"unrelated obstacle");
            for impulse in [0.1,100.,400.] {
                nm_agent_road_arm(a,w);seed_contact(impulse,7);
                assert_eq!(nm_agent_road_contact(a,w,7,200.,0,1./60.,report.as_mut_ptr()),1);
                assert_eq!(velocities(a),original,"brace must not add a launch");
            }
            seed_contact(400.1,7);
            assert_eq!(nm_agent_road_contact(a,w,7,200.,0,1./60.,report.as_mut_ptr()),2,"strong real contact replaces brace");
            assert!((1000.0..=2000.0).contains(&report[2]));
            assert_eq!(report[3],2000.);
            assert_eq!(velocities(a),original,"roll start preserves inertia");
            assert_eq!(nm_agent_road_recovery_ready(a),0);
            {
                let a_ref=&mut *a;
                for _ in 0..121 { a_ref.road.before_step(&mut a_ref.agent,&a_ref.build,&world.0,1./60.); }
            }
            assert_eq!(nm_agent_road_recovery_ready(a),1);
            assert_eq!(nm_agent_is_relaxing(a),1,"original controller must progress to relax");
            assert_eq!(velocities(a),original,"controller messages must not directly rewrite motion");
            nm_agent_road_cancel(a,w);
            assert!((*a).road.roll.is_none());
            assert_eq!((*a).road.state,0);
            assert_eq!(velocities(a),original);
            // A new unrelated task must not be replaced by a delayed roll timer.
            assert_eq!(nm_agent_road_recovery_ready(a),1);
            nm_agent_road_arm(a,w);seed_contact(100.,7);
            assert_eq!(nm_agent_road_contact(a,w,7,0.,0,1./60.,report.as_mut_ptr()),3,"dead unoccupied-car victim relaxes");
            nm_agent_road_arm(a,w);seed_contact(100.,7);
            assert_eq!(nm_agent_road_contact(a,w,7,0.,1,1./60.,report.as_mut_ptr()),2,"original player-car dying branch");
            assert!((1000.0..=3000.0).contains(&report[2]));
            assert_eq!(nm_agent_road_contact(a,w,77,200.,0,1./60.,report.as_mut_ptr()),NM_E_ARG);
            assert_eq!(nm_agent_road_contact(a,w,7,f32::NAN,0,1./60.,report.as_mut_ptr()),NM_E_ARG);
            assert_eq!(nm_agent_road_brace_control(a,w,77,[0.;3].as_ptr(),[0.;3].as_ptr(),[0.;3].as_ptr()),NM_E_ARG);
            nm_agent_destroy(a);nm_build_destroy(b);
        }
    }
}
