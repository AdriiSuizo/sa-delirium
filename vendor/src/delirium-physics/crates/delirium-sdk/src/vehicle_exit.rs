//! NewExitVehicle's separated-body handoff. Uses the original NM tasks and
//! finish predicates; SA retains the lateral exit, entity and get-up tasks.
use super::*;

#[derive(Default)]
pub(super) struct VehicleExit {
    phase: i32, // 0 inactive, 1 balance, 2 catch, 3 roll, 4 relax
    elapsed: f64,
    velocity: Vec3,
    health: f32,
    player: bool,
    failed: bool,
    ready: bool,
    relax_start: f32,
    high_fall: bool,
    /// NMJumpRollFromRoadVehicle's finish window (82613AA8, the player's
    /// `bail_times`): 3..6 s for the bail out of a car, 2..30 s thrown off a bike.
    times: [f32; 2],
}

impl VehicleExit {
    pub(super) fn resume_fall(&mut self)->bool {
        if self.phase==0{return false}
        self.phase=5;self.elapsed=0.;self.ready=false;self.failed=false;true
    }
    pub(super) fn before_step(&mut self, a: &mut Agent, b: &Build, w: &Collision, dt: f32) {
        if self.phase == 0 || self.ready { return; }
        self.elapsed += dt as f64;
        let age = self.elapsed as f32;
        let next = if self.phase == 5 {
            let task=nm::Task::FallDown;
            if nm::task_ends(task,&a.feedback,age>task.min_time(),nm::balance_bar(self.player,0,self.health),false){Some(nm::Next::Blend)}
            else{nm::task_finish_as(task,age,task.times(),a.doll.com().1.length(),a.doll.root().z,false,0,self.health,self.player)}
        } else if self.phase == 1 {
            self.failed |= a.feedback.iter().any(|f| f.slot == 8 && f.name == "balance");
            if nm::task_ends(nm::Task::Balance, &a.feedback, age > 0.5,
                nm::balance_bar(self.player, 0, self.health), false) { Some(nm::Next::Blend) }
            else { nm::task_finish_as(nm::Task::Balance, age, [0.5,20.0], a.doll.com().1.length(), a.doll.root().z,
                self.failed, 0, self.health, self.player) }
        } else if self.phase == 4 {
            nm::task_finish_as(nm::Task::Relax, age-self.relax_start, nm::Task::Relax.times(),
                a.doll.com().1.length(), a.doll.root().z, false, 0, self.health, self.player)
        } else {
            // highFall keeps the body until it lands: its own arms-up and catchFall come before the roll.
            if self.phase == 2 && age > with_tp(nm::jump_roll_up_time) && (!self.high_fall||a.doll.touch.iter().any(|&t|t as i32>0)) {
                if self.high_fall { a.stop_all_behaviours(); }
                with_tp(|tp| nm::task_jump_roll_up(a,b,w,tp,self.velocity));
                self.phase = 3;
            }
            // A force lease for this SDK update and all its solver substeps.
            a.doll.roll_push = Some((with_tp(delirium::jumproll::Set::read),self.velocity,self.phase==3));
            nm::task_finish_as(nm::Task::Flinch, age, self.times, a.doll.com().1.length(), a.doll.root().z,
                false, 0, self.health, self.player)
        };
        match next {
            Some(nm::Next::Relax) => {
                a.doll.roll_push=None; nm::task_relax(a,-1.0,-1.0);
                self.phase=4;self.relax_start=age;
            }
            Some(nm::Next::Blend) => { self.ready=true;a.doll.roll_push=None; }
            None => {}
        }
    }
}

/// Begin only after the ped is physically clear of the vehicle. Velocity is
/// inherited world m/s, health uses IV's 0..200 scale, flags: player=1, gun=2.
/// Returns 1 balance (<=7.5 m/s), 2 catch->roll; never applies a launch impulse.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_exit_start(a:*mut NmAgent,w:*const NmWorld,velocity:*const f32,health:f32,agility:f32,flags:u32)->i32 {
    let a=agent!(a);
    let Some(w)=(unsafe{w.as_ref()}) else{return fail(NM_E_ARG,"null world")};
    if velocity.is_null()||!health.is_finite()||!agility.is_finite()||flags & !3 != 0{return fail(NM_E_ARG,"invalid exit arguments")}
    let v=unsafe{v3(velocity)};if !v.is_finite(){return fail(NM_E_ARG,"invalid exit velocity")}
    a.agent.stop_all_behaviours();a.road=Default::default();
    let phase=if v.length_squared()<=56.25 {1}else{2};
    if phase==1 {
        with_tp(|tp|nm::task_balance(&mut a.agent,&a.build,&w.0,tp,nm::balance_set(agility.clamp(0.0,255.0) as u8,health,flags&1!=0,flags&2!=0),None,false));
    }else{with_tp(|tp|nm::task_jump_roll(&mut a.agent,&a.build,&w.0,tp));}
    a.vehicle_exit=VehicleExit{phase,velocity:v,health,player:flags&1!=0,times:[3.0,6.0],..Default::default()};
    phase
}

/// Thrown off a bike by a crash (0x824662D0 past the NM gate, reason 14): the
/// rider comes off on the ragdoll with the bike's velocity and always takes
/// the jump roll (no NMBalance, whatever the speed), 2..30 s (the viewer's
/// `thrown_off_bike`: bail, not bail_balance, bail_times [2, 30]). Velocity is
/// world m/s, health IV's 0..200, flags: player=1, high fall in the air=2
/// (requested for CJ's dive and the windscreen, not IV's jump roll: NMHFall's flail and arms up, then
/// its catchFall, before the same roll). Returns 2; adds no launch.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_bike_throw_start(a:*mut NmAgent,w:*const NmWorld,velocity:*const f32,health:f32,flags:u32)->i32 {
    let a=agent!(a);
    let Some(w)=(unsafe{w.as_ref()}) else{return fail(NM_E_ARG,"null world")};
    if velocity.is_null()||!health.is_finite()||flags & !3 != 0{return fail(NM_E_ARG,"invalid bike throw arguments")}
    let v=unsafe{v3(velocity)};if !v.is_finite(){return fail(NM_E_ARG,"invalid bike throw velocity")}
    a.agent.stop_all_behaviours();a.road=Default::default();
    let high_fall=flags&2!=0;
    with_tp(|tp|if high_fall{nm::task_high_fall(&mut a.agent,&a.build,&w.0,tp,"*",flags&1!=0)}else{nm::task_jump_roll(&mut a.agent,&a.build,&w.0,tp)});
    a.vehicle_exit=VehicleExit{phase:2,velocity:v,health,player:flags&1!=0,times:[2.0,30.0],high_fall,..Default::default()};
    2
}

/// Cancel timers and the pending force lease when another reaction takes over.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_exit_cancel(a:*mut NmAgent)->i32 {
    let a=agent!(a);
    if a.vehicle_exit.phase!=0 { a.agent.stop_all_behaviours(); }
    a.vehicle_exit=Default::default();a.agent.doll.roll_push=None;0
}

/// Pure query: phase, simulated seconds, ready-to-blend, inherited speed (4 floats).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_exit_status(a:*const NmAgent,out:*mut f32)->i32 {
    let a=agent_ref!(a);if out.is_null(){return fail(NM_E_ARG,"null exit status")}
    let e=&a.vehicle_exit;
    unsafe{std::ptr::copy_nonoverlapping([e.phase as f32,e.elapsed as f32,(e.ready||e.phase==0) as u8 as f32,e.velocity.length()].as_ptr(),out,4)};
    e.phase
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    #[test]
    fn exit_chain_preserves_motion_and_yields_only_when_original_task_finishes() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|n:&str|CString::new(root.join(n).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
            let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
            let a=nm_agent_create(b,[0.0,0.0,15.0].as_ptr(),0.0,0.4);assert!(!a.is_null());
            let w=NmWorld(Collision::default(),vec![],vec![]);
            let seed=|speed:f32|for k in 0..21 {
                let f=(*a).agent.doll.frames().nth(k).unwrap();
                assert_eq!(nm_agent_set_part_state(a,k as i32,f.translation.to_array().as_ptr(),f.rotation.to_array().as_ptr(),[speed,0.0,0.0].as_ptr(),[0.0;3].as_ptr()),0);
            };
            let advance=|dt:f32|{let a=&mut *a;a.vehicle_exit.before_step(&mut a.agent,&a.build,&w.0,dt);};
            seed(7.5);
            assert_eq!(nm_agent_vehicle_exit_start(a,&w,[7.5,0.0,0.0].as_ptr(),200.0,3.0,1),1);
            assert_eq!((*a).agent.doll.motion(0).0,Vec3::new(7.5,0.0,0.0));
            seed(0.0);advance(0.25);advance(0.25);assert!(!(*a).vehicle_exit.ready);
            (*a).agent.feedback.push(delirium::nmshot::Feedback{slot:12,name:"balance",value:1.0});
            advance(0.01);assert!((*a).vehicle_exit.ready,"successful balance can return upright after the original minimum");
            (*a).agent.feedback.clear();
            seed(9.0);
            assert_eq!(nm_agent_vehicle_exit_start(a,&w,[9.0,0.0,0.0].as_ptr(),200.0,3.0,1),2);
            let before=(0..21).map(|k|(*a).agent.doll.motion(k)).collect::<Vec<_>>();
            for _ in 0..4 {advance(0.25);}assert_eq!((*a).vehicle_exit.phase,2);
            advance(0.01);assert_eq!((*a).vehicle_exit.phase,3);
            assert_eq!((0..21).map(|k|(*a).agent.doll.motion(k)).collect::<Vec<_>>(),before,"task messages do not overwrite velocity");
            for _ in 0..24 {advance(0.25);}assert!(!(*a).vehicle_exit.ready,"fast body cannot be recovered by a fixed six-second timeout");
            seed(0.0);advance(0.01);assert!((*a).vehicle_exit.ready);assert!((*a).agent.doll.roll_push.is_none());
            assert_eq!(nm_agent_task_catch_fall(a,c"*".as_ptr()),0);
            assert_eq!((*a).vehicle_exit.phase,5);assert!(!(*a).vehicle_exit.ready);
            (*a).agent.feedback.clear();advance(1./120.);assert!(!(*a).vehicle_exit.ready);
            nm_agent_vehicle_exit_start(a,&w,[9.0,0.0,0.0].as_ptr(),200.0,3.0,1);advance(0.2);
            assert!((*a).agent.doll.roll_push.is_some());
            assert_eq!(nm_agent_vehicle_exit_cancel(a),0);advance(2.0);
            assert_eq!((*a).vehicle_exit.phase,0);assert!((*a).agent.doll.roll_push.is_none(),"old roll timer cannot overwrite a new reaction");
            // Integrator check: original acceleration becomes mass-scaled forces,
            // is dt-dependent. Use initialized NM articulation, as in the host;
            // a raw fred-build skeleton is not a configured solver fixture.
            let frames=(*a).agent.doll.frames().collect::<Vec<_>>();
            let make=||Agent::new(&(*a).build,Ragdoll::new(&(*a).build,&frames,Vec3::new(9.0,0.0,0.0)),Quat::IDENTITY,0.4);
            let mut base=make();let mut push=make();
            for agent in [&mut base,&mut push]{with_tp(|tp|nm::task_jump_roll(agent,&(*a).build,&w.0,tp));}
            let set=with_tp(delirium::jumproll::Set::read);
            let acc=delirium::jumproll::push(&set,Vec3::new(9.0,0.0,0.0),Vec3::new(9.0,0.0,0.0),false).unwrap();
            push.doll.roll_push=Some((set,Vec3::new(9.0,0.0,0.0),false));
            for agent in [&mut base,&mut push]{agent.step_follow(&(*a).build,&w.0,1.0/60.0,&[],&[]);}
            let delta=push.doll.com().1-base.doll.com().1;
            assert!((delta-acc/60.0).length()<0.002,"mass force integration: {delta:?} vs {acc:?}");
            assert!(push.doll.stage_vel.iter().all(|(v,w)|v.is_finite()&&w.is_finite()));
            seed(9.0);nm_agent_vehicle_exit_start(a,&w,[9.0,0.0,0.0].as_ptr(),200.0,3.0,1);
            assert_eq!(nm_agent_step(a,&w,1.0/30.0),0);
            assert!( (*a).agent.doll.roll_push.is_none(),"force lease cleared after the SDK update");
            assert_eq!(nm_agent_vehicle_exit_start(a,&w,[f32::NAN,0.0,0.0].as_ptr(),200.0,3.0,1),NM_E_ARG);
            nm_agent_destroy(a);nm_build_destroy(b);
        }
    }
}
