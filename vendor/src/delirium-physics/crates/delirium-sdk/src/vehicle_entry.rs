//! SA's real door-opening task supplies the entity, measured pose and probe.
//! Controller from LSC4 1dcd86650575: vehicle_entry, GrabHelper and player Grip.
//! No launch, pose snap, invented grip timeout, or velocity assignment here.
use super::*;
use delirium::{climbgrab, grabhelper::GrabHelper, meleetask, shottask};

#[derive(Default)]
pub(super) struct VehicleEntry {
    phase: u8, // inactive, balance/grab, relax
    elapsed: f64,
    relax_start: f32,
    helper: Option<GrabHelper>,
    lean: f32,
    lean_left: f32,
    direction: Vec3,
    set: String,
    pedalling: bool,
    health: f32,
    player: bool,
    failed: bool,
    ready: bool,
    release: bool,
    target_lost: bool,
    index: i32,
}
fn apply(a:&mut Agent,b:&Build,w:&Collision,m:&[shottask::Msg]) {
    // The task's GrabHelper writes pos1 for useLeft and pos2 for useRight.
    // NM's point-grab consumes pos2 for left and pos1 for right (nmgr::tick,
    // nmgr::decide). Translate that boundary explicitly; never alter the
    // original helper, enable an extra hand, or move its measured contact.
    // Line/surface grabs use ordered vertices, so their layout is unchanged.
    let m:Vec<_>=m.iter().map(|(name,params)|{
        let ordered=params.iter().any(|(k,v)|matches!(*k,"linegrab"|"surfaceGrab")&&matches!(v,Value::Bool(true)));
        (*name,params.iter().map(|(k,v)|{
            let k=if *name=="grab"&&!ordered{match *k{"pos1"=>"pos2","pos2"=>"pos1",x=>x}}else{*k};
            (k,v.clone())
        }).collect())
    }).collect();
    let mut state=std::mem::take(&mut a.msgs);
    shottask::apply(a,b,w,&mut state,&m);a.msgs=state;
}
impl VehicleEntry {
    pub(super) fn resume_fall(&mut self)->bool {
        if self.phase==0{return false}
        self.phase=3;self.elapsed=0.;self.ready=false;self.failed=false;self.helper=None;true
    }
    pub(super) fn before_step(&mut self,a:&mut Agent,b:&Build,w:&Collision,dt:f32) {
        if self.phase==0||self.ready{return}
        self.elapsed+=dt as f64;let age=self.elapsed as f32;
        if (self.target_lost||self.health<=0.0)&&self.phase==1 {
            // A recycled host pool slot must never become a new grip target.
            apply(a,b,w,&[("grab",vec![("start",Value::Bool(false))])]);
            nm::task_relax(a,-1.0,-1.0);self.phase=2;self.relax_start=age;self.helper=None;
        }
        self.failed|=a.feedback.iter().any(|f|f.slot==8&&f.name=="balance");
        if let Some(g)=self.helper.as_mut() {
            for f in &a.feedback {g.feedback(f.slot==12,f.name);}
            let mut msgs=Vec::new();msgs.extend(g.update(dt,self.release));
            // Refresh the host's world index without restarting the grab.
            if g.flags&0x100!=0 {msgs.extend(g.start());}
            let holding=g.grabbing();
            msgs.extend(meleetask::balance_lean(&mut self.lean,self.direction,&mut self.lean_left,holding,
                a.doll.frames().next().map_or(Vec3::Y,|f|f.rotation*Vec3::Y),dt));
            msgs.extend(with_tp(|tp|meleetask::grab_pedal(tp,&self.set,&mut self.pedalling,holding,a.doll.com().1)));
            apply(a,b,w,&msgs);
        }
        let holding=self.helper.as_ref().is_some_and(|g|g.grabbing());
        let next=if self.phase==3 {
            let task=nm::Task::FallDown;
            if nm::task_ends(task,&a.feedback,age>task.min_time(),nm::balance_bar(self.player,0,self.health),false){Some(nm::Next::Blend)}
            else{nm::task_finish_as(task,age,task.times(),a.doll.com().1.length(),a.doll.root().z,false,0,self.health,self.player)}
        }else if self.phase==2 {
            nm::task_finish_as(nm::Task::Relax,age-self.relax_start,nm::Task::Relax.times(),a.doll.com().1.length(),a.doll.root().z,false,0,self.health,self.player)
        // Both feedback callbacks (82619E48 / 82619EE8) compare now against
        // start + the creator's u16 minimum at +0x20. Entry passes 1000ms;
        // the 500ms bump-task minimum does not belong to this controller.
        }else if nm::task_ends(nm::Task::Balance,&a.feedback,age>climbgrab::BALANCE_MS[0] as f32/1000.0,nm::balance_bar(self.player,0,self.health),false){Some(nm::Next::Blend)}
        else {nm::task_finish_as(nm::Task::Balance,age,climbgrab::BALANCE_MS.map(|x|x as f32/1000.0),a.doll.com().1.length(),a.doll.root().z,self.failed,0,self.health,self.player)};
        match next {
            Some(_) if self.phase==1&&holding=>{nm::task_relax(a,-1.0,-1.0);self.phase=2;self.relax_start=age;self.helper=None;}
            Some(nm::Next::Relax)=>{nm::task_relax(a,-1.0,-1.0);self.phase=2;self.relax_start=age;self.helper=None;}
            Some(nm::Next::Blend)=>self.ready=true,
            None=>{}
        }
    }
}
/// 0x8241ED8C: original speed gate (NPC 3.5 m/s; player 5 m/s).
#[unsafe(no_mangle)]
pub extern "C" fn nm_vehicle_entry_grabs(speed2:f32,player:i32)->i32 {
    (speed2.is_finite()&&speed2>if player!=0{25.0}else{12.25}) as i32
}
/// local_hit/local_normal are a sphere probe on an actual collision part.
/// Null hit is the original no-contact lean-only branch. Flags player=1,gun=2.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_entry_start(a:*mut NmAgent,w:*const NmWorld,index:i32,
    local_hit:*const f32,local_normal:*const f32,direction:*const f32,health:f32,agility:f32,flags:u32)->i32 {
    let a=agent!(a);let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null entry world")};
    if index<0||direction.is_null()||!health.is_finite()||!agility.is_finite()||flags&!3!=0||local_hit.is_null()!=local_normal.is_null(){return fail(NM_E_ARG,"invalid entry arguments")}
    let dir=unsafe{v3(direction)};let hit=(!local_hit.is_null()).then(||unsafe{(v3(local_hit),v3(local_normal))});
    if !dir.is_finite()||hit.is_some_and(|(p,n)|!p.is_finite()||!n.is_finite()){return fail(NM_E_ARG,"nonfinite entry pose")}
    let player=flags&1!=0;let set=nm::balance_set(agility.clamp(0.0,255.0) as u8,health,player,flags&2!=0);
    let mut g=with_tp(|tp|climbgrab::door_grab(index,0,hit,&mut RAND.lock().unwrap_or_else(|e|e.into_inner()),player,tp));
    a.agent.stop_all_behaviours();a.road=Default::default();a.vehicle_exit=Default::default();a.ped_bump=Default::default();
    with_tp(|tp|nm::task_balance(&mut a.agent,&a.build,&w.0,tp,set,None,false));
    apply(&mut a.agent,&a.build,&w.0,g.start().as_slice());
    a.vehicle_entry=VehicleEntry{phase:1,helper:Some(g),lean:climbgrab::LEAN,lean_left:climbgrab::LEAN_TIME,
        direction:dir,set:set.to_string(),health,player,index,..Default::default()};1
}
/// Entity generation is validated by SA; update indexes before every step.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_entry_follow(a:*mut NmAgent,index:i32,speed2:f32,release:i32,health:f32)->i32 {
    let a=agent!(a);if !speed2.is_finite()||!health.is_finite(){return fail(NM_E_ARG,"nonfinite entry follow")}
    let e=&mut a.vehicle_entry;if e.phase==0{return 0}e.health=health;
    e.target_lost|=index<0;
    e.release|=release!=0||index<0||speed2<1.0||health<=0.0;
    if !e.target_lost {
        // The finite constraint may outlive Balance's helper in NMRelax.
        // Its binding must still follow this same host entity, never an array
        // slot recycled by another car or another door.
        a.agent.remap_grab_instance(e.index,index);e.index=index;
        if let Some(g)=e.helper.as_mut(){g.instance=Some((index,0));}
    }
    if e.target_lost||health<=0.0||(e.release&&e.helper.is_none()) {
        a.agent.grab(&a.build,None);
    }
    0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_entry_status(a:*const NmAgent,out:*mut f32)->i32 {
    let a=agent_ref!(a);if out.is_null(){return fail(NM_E_ARG,"null entry status")}
    let e=&a.vehicle_entry;unsafe{std::ptr::copy_nonoverlapping([e.phase as f32,e.elapsed as f32,e.helper.as_ref().is_some_and(|g|g.grabbing()) as u8 as f32,e.release as u8 as f32].as_ptr(),out,4)};
    (e.phase==0||e.ready) as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_entry_contact(a:*const NmAgent,out:*mut f32)->i32 {
    let a=agent_ref!(a);if out.is_null(){return fail(NM_E_ARG,"null grab observation")}
    unsafe{std::ptr::copy_nonoverlapping(a.agent.grab_observation().as_ptr(),out,10)};0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_entry_load(a:*const NmAgent,out:*mut f32)->i32 {
    let a=agent_ref!(a);if out.is_null(){return fail(NM_E_ARG,"null grip load")}
    unsafe{std::ptr::copy_nonoverlapping(a.agent.doll.grip_load.as_ptr(),out,4)};0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_grab_points(a:*const NmAgent,out:*mut f32)->i32 {
    let a=agent_ref!(a);if out.is_null(){return fail(NM_E_ARG,"null grip points")}
    let(mask,points)=a.agent.grab_points();unsafe{std::ptr::copy_nonoverlapping(points.as_ptr(),out,12)};mask as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_entry_cancel(a:*mut NmAgent)->i32 {
    let a=agent!(a);if a.vehicle_entry.phase!=0{a.agent.stop_all_behaviours();}a.vehicle_entry=Default::default();0
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    #[test]
    fn boarding_grip_preserves_motion_and_obeys_feedback_release_and_identity() {
        assert_eq!(nm_vehicle_entry_grabs(12.25,0),0);assert_eq!(nm_vehicle_entry_grabs(12.26,0),1);
        assert_eq!(nm_vehicle_entry_grabs(25.0,1),0);assert_eq!(nm_vehicle_entry_grabs(25.01,1),1);
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|s:&str|CString::new(root.join(s).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
            let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
            let a=nm_agent_create(b,[0.0,0.0,15.0].as_ptr(),0.0,0.4);assert!(!a.is_null());
            let w=NmWorld(Collision::default(),vec![],vec![]);
            let motion=(0..21).map(|k|(*a).agent.doll.motion(k)).collect::<Vec<_>>();
            let start=||nm_agent_vehicle_entry_start(a,&w,0,[1.0,0.0,0.5].as_ptr(),[1.0,0.0,0.0].as_ptr(),[1.0,0.0,0.0].as_ptr(),200.0,3.0,0);
            let step=|dt|{let a=&mut *a;a.vehicle_entry.before_step(&mut a.agent,&a.build,&w.0,dt);};
            assert_eq!(start(),1);
            assert_eq!((*a).agent.msgs.grab.pos[1],Vec3::new(1.,0.,0.5));
            assert_eq!((*a).agent.msgs.grab.pos[0],Vec3::new(1.,0.,0.5));
            assert!((*a).agent.msgs.grab.use_left&&(*a).agent.msgs.grab.use_right,
                "8241F174 duplicates the one door probe point into both hand slots");
            assert_eq!((0..21).map(|k|(*a).agent.doll.motion(k)).collect::<Vec<_>>(),motion);
            assert_eq!(nm_agent_vehicle_entry_follow(a,7,16.0,0,200.0),0);
            assert_eq!((*a).vehicle_entry.helper.as_ref().unwrap().instance,Some((7,0)));
            step(0.1);assert!(!(*a).vehicle_entry.ready);
            (*a).agent.feedback.push(delirium::nmshot::Feedback{slot:12,name:"LeftArmGrab",value:1.0});
            step(0.1);assert!((*a).vehicle_entry.helper.as_ref().unwrap().grabbing());
            assert_eq!((*a).vehicle_entry.lean,0.0,"holding ends original lean");
            (*a).agent.feedback.clear();
            nm_agent_vehicle_entry_follow(a,7,0.99,0,200.0);step(0.1);
            assert!(!(*a).vehicle_entry.helper.as_ref().unwrap().grabbing(),"car below 1m/s releases");
            assert_eq!(start(),1);
            nm_agent_vehicle_entry_follow(a,-1,16.0,0,200.0);step(0.1);
            assert!((*a).vehicle_entry.helper.is_none());assert_eq!((*a).vehicle_entry.phase,2);
            nm_agent_vehicle_entry_follow(a,0,16.0,0,200.0);step(0.1);
            assert!((*a).vehicle_entry.helper.is_none(),"reused collider cannot reclaim a lost entity");
            assert_eq!(start(),1);
            (*a).agent.feedback.push(delirium::nmshot::Feedback{slot:12,name:"LeftArmGrab",value:1.0});step(0.6);
            (*a).agent.feedback.push(delirium::nmshot::Feedback{slot:12,name:"balance",value:1.0});step(0.15);
            assert_eq!((*a).vehicle_entry.phase,1,"entry creator's 1000ms minimum applies to Balance feedback too (82619E7C)");
            step(0.25);
            assert_eq!((*a).vehicle_entry.phase,1,"82619E94 uses strict now > start + min, not >=");
            step(0.001);
            assert_eq!((*a).vehicle_entry.phase,2,"Balance finishing with a grip goes to NMRelax");assert!(!(*a).vehicle_entry.ready);
            (*a).vehicle_entry.ready=true;
            assert_eq!(nm_agent_task_catch_fall(a,c"*".as_ptr()),0);
            assert_eq!((*a).vehicle_entry.phase,3);assert!(!(*a).vehicle_entry.ready);
            (*a).agent.feedback.clear();
            let agent=&mut *a;
            agent.vehicle_entry.before_step(&mut agent.agent,&agent.build,&w.0,1./120.);
            assert!(!(*a).vehicle_entry.ready);
            nm_agent_vehicle_entry_cancel(a);assert_eq!((*a).vehicle_entry.phase,0);
            nm_agent_destroy(a);nm_build_destroy(b);
        }
    }

    #[test]
    fn grip_separation_is_corrected_without_injecting_launch_velocity() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|s:&str|CString::new(root.join(s).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
            // Isolate the grip constraint from the humanoid's authored motor
            // and joint-limit setup (a raw Drive::default is not that setup).
            let mut part=(&(*b).build.parts)[16].clone();part.parent=None;part.bone=0;
            let build=Build{parts:vec![part],nm:(*b).build.nm};
            for length in [0.0,0.02] {
                let initial=Vec3::new(0.,0.,15.);
                let frames=[Transform::from_translation(initial)];
                let mut free=Ragdoll::new(&build,&frames,Vec3::ZERO);
                let mut held=Ragdoll::new(&build,&frames,Vec3::ZERO);
                held.grips.push((0,Vec3::ZERO,initial+Vec3::Y*0.1,length,100.));
                let world=Collision::default();
                let drive=delirium::ragdoll::Drive{lift:1.,..Default::default()};
                for doll in [&mut free,&mut held] {
                    doll.step(&build,&world,1./120.,drive,&[],&[]);
                }
                assert!(held.snapped.is_empty(),"position error alone must not break the original finite grip");
                let moved=held.frames().next().unwrap().translation-initial;
                assert!(moved.y>0.01,"position solve must still correct the attachment: length={length} moved={moved:?}");
                let delta=held.motion(0).0-free.motion(0).0;
                assert!(delta.length()<0.05,"grip repair injected velocity: {delta:?}");
            }
            nm_build_destroy(b);
        }
    }

    #[test]
    fn original_entry_point_forms_a_left_solver_grip_and_follows_reindexing() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|s:&str|CString::new(root.join(s).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            nm_load_task_params(path("taskparams.txt").as_ptr());
            let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
            let a=nm_agent_create(b,[0.,0.,15.].as_ptr(),0.,0.4);assert!(!a.is_null());
            let hand=(*a).agent.doll.frames().nth(16).unwrap();
            let palm=hand.translation+hand.rotation*Vec3::X*0.02;
            let contact=palm+Vec3::Y*0.05;
            let centre=contact+Vec3::X*0.02;
            let local=contact-centre;
            let w=nm_world_create();
            nm_world_set_boxes(w,[centre.x,centre.y,centre.z,0.,0.,0.,1.,0.01,0.01,0.01,0.,0.,0.].as_ptr(),1);
            assert_eq!(nm_agent_vehicle_entry_start(a,w,0,local.to_array().as_ptr(),[-1.,0.,0.].as_ptr(),[0.,1.,0.].as_ptr(),200.,3.,0),1);
            let mut held=false;
            for _ in 0..12 {
                assert_eq!(nm_agent_step(a,w,1./60.),0);
                held|=(*a).agent.feedback.iter().any(|f|f.slot==12&&f.name=="LeftArmGrab");
                if held{break}
            }
            assert!(held,"measured left palm contact must produce NM feedback, not a synthetic helper flag");
            // SA's broadphase can reorder its dense obstacle array every
            // frame. Reassign the same physical door to another slot while a
            // decoy occupies the old one; retain this exact, finite grip.
            for (step,index) in [1,0,1,0,1,0].into_iter().enumerate() {
                if step==4 {
                    (*a).vehicle_entry.phase=2;(*a).vehicle_entry.helper=None;
                    nm::task_relax(&mut (*a).agent,-1.,-1.);
                }
                let shift=Vec3::Y*(step+1) as f32*0.003;
                let rotation=Quat::from_rotation_z((step+1) as f32*0.0003);
                let velocity=Vec3::Y*1.8;
                let spin=Vec3::Z*0.18;
                let far=centre+Vec3::X*100.;
                let mut boxes=Vec::new();
                for k in 0..2 {
                    let p=if k==index{centre+shift}else{far};
                    let q=if k==index{rotation}else{Quat::IDENTITY};
                    let v=if k==index{velocity}else{Vec3::splat(-40.)};
                    boxes.extend([p.x,p.y,p.z,q.x,q.y,q.z,q.w,0.01,0.01,0.01,v.x,v.y,v.z]);
                }
                assert_eq!(nm_world_set_boxes(w,boxes.as_ptr(),2),0);
                (&mut (*w).1)[index as usize].spin=spin;
                assert_eq!(nm_agent_vehicle_entry_follow(a,index,16.,0,200.),0);
                // One substep exposes stale collider placement in the solve;
                // later behaviour placement alone would hide it in telemetry.
                assert_eq!(nm_agent_step(a,w,1./600.),0);
                let observation=(*a).agent.grab_observation();
                assert_eq!(observation[1] as u32&1,1,"reindexing the same door must not release its hand");
                let target=Vec3::new(observation[4],observation[5],observation[6]);
                assert!(target.distance(centre+shift+rotation*local)<0.001,"old obstacle slot stole the grip: {target:?}");
                let solved=(*a).agent.doll.grip_solved_target.unwrap();
                let current=(&(*a).agent.doll.grips)[0].2;
                assert!(solved.distance(current)<1e-5,"solver used the previous door frame: {solved:?} vs {current:?}");
                let measured=(&(*a).agent.doll.grip_target_velocities)[0].1;
                let expected=velocity+spin.cross(current-centre-shift);
                assert!(measured.distance(expected)<1e-5,"moving grip lost point velocity on reindex/Relax: {measured:?} vs {expected:?}");
            }
            nm_agent_vehicle_entry_follow(a,-1,16.,0,200.);
            assert_eq!((*a).agent.grab_observation()[0],0.,"entity loss releases a Relax constraint too");
            nm_agent_vehicle_entry_follow(a,0,16.,0,200.);
            assert_eq!((*a).agent.grab_observation()[0],0.,"a recycled entity cannot reclaim the hand");
            nm_agent_destroy(a);nm_build_destroy(b);nm_world_destroy(w);
        }
    }

    #[test]
    fn entry_contact_near_right_hand_can_grab_without_left_hand_contact() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|s:&str|CString::new(root.join(s).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
            let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
            let a=nm_agent_create(b,[0.,0.,15.].as_ptr(),0.,0.4);assert!(!a.is_null());
            let hand=(*a).agent.doll.frames().nth(20).unwrap();
            let palm=hand.translation+hand.rotation*Vec3::X*0.02;
            let contact=palm+Vec3::Y*0.05;
            let centre=contact+Vec3::X*0.02;let local=contact-centre;
            let w=nm_world_create();
            assert_eq!(nm_world_set_boxes(w,[centre.x,centre.y,centre.z,0.,0.,0.,1.,0.01,0.01,0.01,0.,0.,0.].as_ptr(),1),0);
            assert_eq!(nm_agent_vehicle_entry_start(a,w,0,local.to_array().as_ptr(),[-1.,0.,0.].as_ptr(),[0.,1.,0.].as_ptr(),200.,3.,0),1);
            let mut held=false;
            for _ in 0..12 {
                assert_eq!(nm_agent_step(a,w,1./60.),0);
                held|=(*a).agent.feedback.iter().any(|f|f.slot==12&&f.name=="RightArmGrab");
                if held{break}
            }
            assert!(held,"original both-hand entry request must permit an actual right-arm grip");
            assert_eq!((*a).agent.grab_observation()[1] as u32&2,2);
            nm_agent_destroy(a);nm_build_destroy(b);nm_world_destroy(w);
        }
    }
}
