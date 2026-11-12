//! SA host boundary for the pinned IV animated-body solver and bump dispatcher.
use super::*;
use sa_combat::pedbodies as pb;

struct PreparedRecovery {made:delirium::bump::Made<i32>,health:f32,player:bool,look:Vec3}
#[derive(Default)]
pub(super) struct PedBump {
    prepared:Option<PreparedRecovery>,roll:Option<nm::RollUpAndRelax>,times:Option<[f32;2]>,task:Option<nm::Task>,elapsed:f32,health:f32,failed:bool,ready:bool,touched:bool,player:bool,agility:u8,
}
impl PedBump {
    // CatchFall replaces the completed reaction; its finish state belongs to
    // the NEW FallDown task. Keeping ready=true from the old Flinch/Balance
    // caused a handback/recontact loop on every host frame during recovery.
    pub(super) fn resume_fall(&mut self){
        if self.task.is_some(){
            self.task=Some(nm::Task::FallDown);self.elapsed=0.;self.roll=None;self.times=None;self.prepared=None;
            self.ready=false;self.failed=false;self.touched=false;
        }
    }
    pub(super) fn before_step(&mut self,a:&mut Agent,b:&Build,w:&Collision,dt:f32){
        let Some(task)=self.task.filter(|_|!self.ready)else{return};
        self.elapsed+=dt;
        if self.health<=0.{return} // corpse lifetime belongs to native death
        if let Some(roll)=&mut self.roll{roll.control(a,b,w,&mut RAND.lock().unwrap_or_else(|e|e.into_inner()),dt);}
        if std::mem::take(&mut self.touched)&&task==nm::Task::Balance{self.elapsed=f32::MIN_POSITIVE;}
        let failure=a.feedback.iter().find(|f|f.slot==8&&nm::task_fails(task,f.name)).map(|f|f.name);
        if let Some(name)=failure.filter(|_|!self.failed) {
            nm::task_failed(a,w,task);
            if task==nm::Task::Flinch {
                // Original NMFlinch failure callback releases its upper-body
                // guard. Let the actual fall/terrain drive the arms.
                let messages=delirium::meleetask::flinch_fails(&mut self.failed,name);
                let mut state=std::mem::take(&mut a.msgs);
                delirium::shottask::apply(a,b,w,&mut state,&messages);
                a.msgs=state;
            }
            self.failed=true;
        }
        let times=self.times.unwrap_or_else(||task.times());
        let next=if nm::task_ends(task,&a.feedback,self.elapsed>self.times.map_or_else(||task.min_time(),|t|t[0]),nm::balance_bar(self.player,self.agility,self.health),false){Some(nm::Next::Blend)}
            else{nm::task_finish_as(task,self.elapsed,times,a.doll.com().1.length(),a.doll.root().z,self.failed,0,self.health,self.player)};
        match next{
            Some(nm::Next::Blend)=>{self.ready=true;a.lean_in_direction(Vec3::ZERO,0.,0.,true);}
            Some(nm::Next::Relax)=>{nm::task_relax(a,-1.,-1.);self.task=Some(nm::Task::Relax);self.elapsed=0.;self.failed=false;}
            None=>{}
        }
    }
}

/// Rows: root xyz, yaw, velocity xyz (m/s), mass. Output: displacement xyz,
/// velocity xyz, strongest peer index (-1 absent), impulse Ns, normal xyz.
/// The host must replace native ped/ped rows for this same validated set only.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_ped_contacts(input:*const f32,count:i32,dt:f32,output:*mut f32)->i32 {
    if input.is_null()||output.is_null()||!(1..=32).contains(&count)||!dt.is_finite()||dt<=0.||dt>0.1{return fail(NM_E_ARG,"invalid ped contact frame")}
    let rows=unsafe{std::slice::from_raw_parts(input,count as usize*8)};
    if rows.iter().any(|v|!v.is_finite())||rows.chunks_exact(8).any(|r|r[7]<=0.){return fail(NM_E_ARG,"invalid ped body")}
    let mut bodies:Vec<_>=rows.chunks_exact(8).map(|r|{let mut b=pb::Body::ped([r[0],r[1],r[2]],r[3],[r[4],r[5],r[6]]);b.inv_mass=1./r[7];b}).collect();
    let out=unsafe{std::slice::from_raw_parts_mut(output,count as usize*11)};out.fill(0.);
    for r in out.chunks_exact_mut(11){r[6]=-1.;}
    let mut total=0;
    for _ in 0..pb::SUBSTEPS {
        let contacts=pb::contacts(&bodies,pb::PARTS.len());total+=contacts.len();
        let(moves,pushes)=pb::step(&mut bodies,&contacts,dt/pb::SUBSTEPS as f32);
        for (i,m) in moves.iter().enumerate(){for k in 0..2{out[i*11+k]+=m[k];bodies[i].root[k]+=m[k];}}
        for(c,impulse)in contacts.iter().zip(pushes){for(i,j,sign)in[(c.a,c.b,1.),(c.b,c.a,-1.)]{
            let r=&mut out[i*11..(i+1)*11];if r[6]<0.||impulse>r[7]{r[6]=j as f32;r[7]=impulse;r[8..11].copy_from_slice(&c.n.map(|n|n*sign));}
        }}
    }
    for(i,b)in bodies.iter().enumerate(){out[i*11+3..i*11+6].copy_from_slice(&b.vel);}
    total as i32
}

/// Original fragment filter geometry: the player's animated composite against
/// the victim's last drawn fragment capsules, translated to the current root.
/// Outer body contact alone only separates peds; it cannot start a ragdoll.
/// Capsule rows are endpoint xyz, endpoint xyz, radius (seven floats).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_ped_meets_fragment(root:*const f32,capsules:*const f32,count:i32)->i32 {
    use delirium::{curved,gjk};
    if root.is_null()||capsules.is_null()||!(1..=32).contains(&count){return fail(NM_E_ARG,"invalid fragment query")}
    let root=unsafe{v3(root)};let rows=unsafe{std::slice::from_raw_parts(capsules,count as usize*7)};
    if !root.is_finite()||rows.iter().any(|x|!x.is_finite())||rows.chunks_exact(7).any(|r|r[6]<=0.){return fail(NM_E_ARG,"invalid fragment geometry")}
    static PARTS:std::sync::OnceLock<Vec<(gjk::Shape,Mat3,Vec3)>>=std::sync::OnceLock::new();
    let parts=PARTS.get_or_init(||pb::PARTS.iter().enumerate().filter(|(_,p)|p.include&pb::FRAGMENT!=0).map(|(i,p)|{
        let(shape,rotation)=if i==0||i==3{(gjk::Shape::Curved(Box::leak(Box::new(curved::ped_part(p.core,2.*p.half,p.round))),p.round),Mat3::IDENTITY)}
            else{(gjk::Shape::Capsule(p.half,p.round),Mat3::from_rotation_x(std::f32::consts::FRAC_PI_2))};
        (shape,rotation,Vec3::Z*p.z)
    }).collect());
    for r in rows.chunks_exact(7){
        let(a,b)=(Vec3::new(r[0],r[1],r[2]),Vec3::new(r[3],r[4],r[5]));
        let shape=gjk::Shape::Capsule((b-a).length()*0.5,r[6]);
        let turn=Mat3::from_quat(Quat::from_rotation_arc(Vec3::Y,(b-a).normalize_or(Vec3::Y)));
        for(p,m,at)in parts{
            if gjk::closest(p,*m,root+at,&shape,turn,(a+b)*0.5,Vec3::X,f32::INFINITY,Some(gjk::Pen::Sphere)).is_some_and(|c|c.dist<pb::BREAK){return 1}
        }
    }0
}

/// Confirmed on-foot body contact, already solved once by the host. Chooses
/// the original bump response without damage, a launch, or another impulse.
/// Other contains peer root xyz, peer head xyz, victim root xyz.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_ped_bump(a:*mut NmAgent,w:*const NmWorld,other:*const f32,health:f32,impulse:f32,dt:f32,flags:u32)->i32 {
    let a=agent!(a);let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null world")};
    if other.is_null()||!health.is_finite()||health<=0.||!impulse.is_finite()||impulse<=0.||!dt.is_finite()||dt<=0.||dt>0.1||flags&!3!=0{return fail(NM_E_ARG,"invalid bump")}
    let other=unsafe{std::slice::from_raw_parts(other,9)};if other.iter().any(|v|!v.is_finite()){return fail(NM_E_ARG,"invalid peer position")}
    let at=Vec3::new(other[0],other[1],other[2]);
    let look=Vec3::new(other[3],other[4],other[5]);let own=Vec3::new(other[6],other[7],other[8]);
    use delirium::bump::{self,Thing,Kind};
    let peer=Thing{id:1,kind:Kind::Ped{player:flags&1!=0,ragdoll:false}};
    let(place,vel)=a.agent.doll.com();
    let ped=bump::Ped{id:0,health,inv_mass:1./a.agent.doll.mass(),woke:true,drop:None,vz:vel.z,tough:false,player_cut:false,
        player:flags&2!=0,mission:false,quick_relax:false,agility:3,gun:false,place,vel,standing_on:None};
    let impact=bump::Impact{impulse,dt,component:0,other:Some(peer)};
    let around=bump::Around{touching:vec![peer],player:(flags&1!=0).then_some(bump::Player{thing:peer,place:at,in_car:false}),blend:None,sit:None,die:None,accepts:true};
    let mut rand=RAND.lock().unwrap_or_else(|e|e.into_inner());
    let Some(made)=bump::react(&ped,&impact,&around,0.,false,health,&mut rand,||None)else{return 0};
    match made.task {
        bump::Task::Balance{set,..}=>{
            a.agent.contact_wake=true;
            with_tp(|tp|nm::task_balance(&mut a.agent,&a.build,&w.0,tp,set,Some(look),false));
            a.ped_bump=PedBump{task:Some(nm::Task::Balance),health,player:flags&2!=0,agility:3,..Default::default()};
            a.agent.lean_in_direction(own-at,0.4,1.0,true);1
        }
        _=>0,
    }
}

/// Refresh the original bump's one-second directional force lease, without
/// restarting the balance task or replacing its joint pose or velocity.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_ped_bump_follow(a:*mut NmAgent,direction:*const f32)->i32 {
    let a=agent!(a);if direction.is_null(){return fail(NM_E_ARG,"null bump direction")}
    let direction=unsafe{v3(direction)};if !direction.is_finite(){return fail(NM_E_ARG,"invalid bump direction")}
    if a.ped_bump.task==Some(nm::Task::Balance)&&!a.ped_bump.ready{a.agent.lean_in_direction(direction,0.4,1.,true);}0
}

/// Actual player/body contact refreshes Balance's clock (8261A530).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_ped_bump_touch(a:*mut NmAgent)->i32 {let a=agent!(a);a.ped_bump.touched=true;0}

/// Cancel the timer/force lease on replacement, without rewriting inertia.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_ped_bump_cancel(a:*mut NmAgent)->i32 {
    let a=agent!(a);if a.ped_bump.task.is_some(){a.agent.lean_in_direction(Vec3::ZERO,0.,0.,true);}
    a.ped_bump=Default::default();0
}

/// Returns readiness; output is task (0 inactive,1 balance,2 relax), seconds,
/// ready flag and failure flag. No arbitrary host minimum time substitutes it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_ped_bump_status(a:*const NmAgent,out:*mut f32)->i32 {
    let a=agent_ref!(a);if out.is_null(){return fail(NM_E_ARG,"null bump status")}
    let b=&a.ped_bump;let ready=b.ready||b.task.is_none();
    let phase=match b.task{Some(nm::Task::Balance)=>1.,Some(nm::Task::Flinch)=>3.,Some(_)=>2.,None=>0.};
    unsafe{std::ptr::copy_nonoverlapping([phase,b.elapsed,ready as u8 as f32,b.failed as u8 as f32].as_ptr(),out,4)};ready as i32
}

/// Start the original accepted melee NM task and its feedback/finish clock.
/// Flags: flinch=1, player=2. The host supplies mapped IV health and agility.
/// Hit momentum remains a separate measured contact, never added here.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_melee_task(a:*mut NmAgent,w:*const NmWorld,threat:*const f32,health:f32,agility:i32,flags:u32)->i32 {
    let a=agent!(a);let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null world")};
    if threat.is_null()||!health.is_finite()||!(0..=255).contains(&agility)||flags&!3!=0{return fail(NM_E_ARG,"invalid melee task")}
    let threat=unsafe{v3(threat)};if !threat.is_finite(){return fail(NM_E_ARG,"invalid melee threat")}
    let task=if flags&1!=0{nm::Task::Flinch}else{nm::Task::Balance};
    let mut rand=RAND.lock().unwrap_or_else(|e|e.into_inner());
    with_tp(|tp|if task==nm::Task::Flinch{nm::task_flinch(&mut a.agent,&a.build,&w.0,tp,&mut rand,if agility<3{"MELEE_PASSIVE"}else{"MELEE"},threat)}
        else{nm::task_balance(&mut a.agent,&a.build,&w.0,tp,"*",Some(threat),false)});
    a.ped_bump=PedBump{task:Some(task),health,agility:agility as u8,player:flags&2!=0,..Default::default()};0
}
/// Refresh live health without restarting the reaction clock.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_contact_health(a:*mut NmAgent,health:f32)->i32 {
    let a=agent!(a);if !health.is_finite(){return fail(NM_E_ARG,"invalid live health")};a.ped_bump.health=health;0
}

#[cfg(test)]mod tests{
 use super::*;
 fn assets(){
  let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
  let asset=std::ffi::CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
  unsafe{assert!(matches!(nm_init(asset.as_ptr()),0|1));}
 }
 #[test]fn original_recovery_selection_is_prepared_then_started_without_motion_reset(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();let b=nm_build_load(file.as_ptr());let w=nm_world_create();
  for(clip,phase,want)in[(0,0.,1),(1,0.1,1),(1,0.3,2),(1,0.7,3),(2,0.1,1),(2,0.3,2),(2,0.7,3)]{
   let a=nm_agent_create(b,[0.,0.,1.].as_ptr(),0.,0.4);let n=&mut *a;
   let poses:Vec<_>=n.agent.doll.frames().collect();let velocity:Vec<_>=(0..21).map(|k|n.agent.doll.vel(k)).collect();
   assert_eq!(nm_agent_prepare_recovery_contact(a,[0.,1.,1.,0.,1.,1.7,0.,0.,1.].as_ptr(),180.,12.,1./60.,clip,phase,17),want);
   assert!(n.ped_bump.task.is_none(),"prepare changed physical response before handoff");
   assert_eq!(nm_agent_start_recovery_contact(a,w),want);assert_eq!(nm_agent_start_recovery_contact(a,w),0,"replayed the accepted event");
   assert_eq!(n.ped_bump.times,Some([2.,60.]));assert!(!n.ped_bump.ready);
   assert_eq!((0..21).map(|k|n.agent.doll.vel(k)).collect::<Vec<_>>(),velocity);
   for(k,f)in n.agent.doll.frames().enumerate(){assert_eq!(f.translation,poses[k].translation);assert_eq!(f.rotation,poses[k].rotation);}
   if want==2{assert!(n.ped_bump.roll.is_some());n.ped_bump.resume_fall();assert!(n.ped_bump.roll.is_none());assert!(n.ped_bump.times.is_none());}
   nm_agent_destroy(a);
  }nm_world_destroy(w);nm_build_destroy(b);
 }}
 #[test]fn interrupted_recovery_cannot_reuse_completed_melee_finish(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
  let b=nm_build_load(file.as_ptr());let a=nm_agent_create(b,[0.,0.,1.].as_ptr(),0.,0.4);
  let n=&mut *a;n.ped_bump=PedBump{task:Some(nm::Task::Flinch),ready:true,elapsed:4.,health:200.,..Default::default()};
  let velocity=n.agent.doll.vel(0);
  assert_eq!(nm_agent_task_catch_fall(a,c"*".as_ptr()),0);
  assert_eq!(n.ped_bump.task,Some(nm::Task::FallDown));assert!(!n.ped_bump.ready);
  n.ped_bump.before_step(&mut n.agent,&n.build,&Collision::default(),1./120.);
  assert!(!n.ped_bump.ready);assert_eq!(n.agent.doll.vel(0),velocity);
  nm_agent_destroy(a);nm_build_destroy(b);
 }}
 #[test]fn contact_wake_single_agent_matches_original_group_first_step(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
  let b=nm_build_load(file.as_ptr());assert!(!b.is_null());
  let a=nm_agent_create(b,[0.,0.,2.].as_ptr(),0.,0.4);let g=nm_agent_create(b,[0.,0.,2.].as_ptr(),0.,0.4);
  let world=Collision::default();let a=&mut *a;let g=&mut *g;
  a.agent.contact_wake=true;g.agent.contact_wake=true;
  let initial=a.agent.doll.root();
  a.agent.step(&a.build,&world,1./60.,&[],&[]);
  let mut group=delirium::ragdoll::Group::default();
  Agent::step_group(&mut[(&mut g.agent,0,&g.build,&[],&[])],&world,1./60.,&mut group);
  assert!(!a.agent.contact_wake);assert!(!g.agent.contact_wake);
  let single:Vec<_>=a.agent.doll.frames().collect();let grouped:Vec<_>=g.agent.doll.frames().collect();
  for (i,(s,g)) in single.iter().zip(&grouped).enumerate(){assert!(s.translation.distance(g.translation)<1e-5,"part {i}: single {:?}, group {:?}",s.translation,g.translation);}
  assert!(a.agent.doll.root().distance(initial)>1e-6,"first contact step must actually advance");
  nm_agent_destroy(a);nm_agent_destroy(g);nm_build_destroy(b);
 }}
 #[test]fn balance_ends_on_original_feedback_and_contact_refreshes_its_clock(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
  let b=nm_build_load(file.as_ptr());assert!(!b.is_null());let a=nm_agent_create(b,[0.,0.,1.].as_ptr(),0.,0.4);assert!(!a.is_null());
  let w=Collision::default();let n=&mut *a;
  n.ped_bump=PedBump{task:Some(nm::Task::Balance),health:200.,..Default::default()};
  n.agent.feedback=vec![delirium::nmshot::Feedback{slot:12,name:"balance",value:0.}];
  let velocity=n.agent.doll.vel(0);
  n.ped_bump.before_step(&mut n.agent,&n.build,&w,0.9);assert!(!n.ped_bump.ready);
  n.ped_bump.touched=true;n.ped_bump.before_step(&mut n.agent,&n.build,&w,0.2);assert!(!n.ped_bump.ready);
  n.ped_bump.before_step(&mut n.agent,&n.build,&w,1.1);assert!(n.ped_bump.ready);
  assert_eq!(n.agent.doll.vel(0),velocity,"finishing must not rewrite inertia");
  assert_eq!(nm_agent_ped_bump_cancel(a),0);assert!(n.ped_bump.task.is_none());
  nm_agent_destroy(a);nm_build_destroy(b);
 }}
 /// peds.rs/nm.rs balances_and_steps: run into by Niko (NMBalance, lean 0.4
 /// as a force for ONE second) it stumbles on and stays up. Measures the SA
 /// host path (nm_agent_ped_bump) with and without a per-frame lean re-arm.
 fn bump_run(follow:bool,dt:f32)->(f32,bool,f32){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let path=|n:&str|std::ffi::CString::new(root.join(n).to_str().unwrap()).unwrap();
  assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
  let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
  let w=nm_world_create();nm_world_add_floor(w,0.,0.,0.,20.);
  let a=nm_agent_create(b,[0.,0.,0.].as_ptr(),0.,0.4);
  let start=(*a).agent.doll.com().0;
  let other=[start.x,start.y-0.5,start.z, start.x,start.y-0.5,start.z+0.7, start.x,start.y,start.z];
  assert_eq!(nm_agent_ped_bump(a,w,other.as_ptr(),200.,4.,dt,1),1);
  let mut low=f32::MAX;let away=Vec3::Y;
  for _ in 0..(3.7/dt) as usize{
   if follow{nm_agent_ped_bump_follow(a,away.to_array().as_ptr());}
   assert_eq!(nm_agent_step(a,w,dt),0);
   low=low.min((*a).agent.doll.com().0.z);
  }
  let moved=((*a).agent.doll.com().0-start).y;let failed=(*a).ped_bump.failed;
  nm_agent_destroy(a);nm_world_destroy(w);nm_build_destroy(b);(low,failed,moved)
 }}
 /// As the game: the player's contact restarts Balance's clock for a while
 /// (8261A530); the lean keeps its own one-second end unless re-armed.
 fn bump_touched(follow:bool,touch:f32)->(f32,bool,f32){bump_from(follow,touch,Vec3::NEG_Y)}
 /// `from`: where the player stands relative to the victim (its facing is
 /// the build's), so sides and back can be compared with the same rig.
 fn bump_from(follow:bool,touch:f32,from:Vec3)->(f32,bool,f32){bump_from_moving(follow,touch,from,0.)}
 /// `v0`: the victim already moving away at the bump (m/s), as the shared
 /// solve leaves the 25 kg animated owner in the contact frame in game.
 fn bump_from_moving(follow:bool,touch:f32,from:Vec3,v0:f32)->(f32,bool,f32){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let path=|n:&str|std::ffi::CString::new(root.join(n).to_str().unwrap()).unwrap();
  assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
  let b=nm_build_load(path("fred-build.txt").as_ptr());let w=nm_world_create();nm_world_add_floor(w,0.,0.,0.,20.);
  let a=nm_agent_create(b,[0.,0.,0.].as_ptr(),0.,0.4);let dt=1./120.;
  let start=(*a).agent.doll.com().0;
  if v0>0.{for i in 0..21{let(mut pos,mut rot,mut lin,mut ang)=([0f32;3],[0f32;4],[0f32;3],[0f32;3]);
   nm_agent_get_part_transform(a,i,pos.as_mut_ptr(),rot.as_mut_ptr());nm_agent_get_part_velocity(a,i,lin.as_mut_ptr(),ang.as_mut_ptr());
   let l=Vec3::from(lin)-from*v0;assert_eq!(nm_agent_set_part_state(a,i,pos.as_ptr(),rot.as_ptr(),l.to_array().as_ptr(),ang.as_ptr()),0);}}
  let p=start+from*0.5;
  let other=[p.x,p.y,start.z, p.x,p.y,start.z+0.7, start.x,start.y,start.z];
  assert_eq!(nm_agent_ped_bump(a,w,other.as_ptr(),200.,4.,dt,1),1);
  let mut low=f32::MAX;let mut peak=0f32;
  for k in 0..(3.7/dt) as usize{
   if (k as f32)*dt<touch{nm_agent_ped_bump_touch(a);}
   if follow{nm_agent_ped_bump_follow(a,(-from).to_array().as_ptr());}
   assert_eq!(nm_agent_step(a,w,dt),0);low=low.min((*a).agent.doll.com().0.z);
   let v=(*a).agent.doll.com().1;peak=peak.max(v.truncate().length());
  }
  eprintln!("PEAK from={from:?} touched_for={touch} peak_horizontal_mps={peak:.2}");
  let moved=((*a).agent.doll.com().0-start).dot(-from);let failed=(*a).ped_bump.failed;
  nm_agent_destroy(a);nm_world_destroy(w);nm_build_destroy(b);(low,failed,moved)
 }}
 #[test]fn player_bump_balance_stays_up_like_the_original(){
  for dt in [1./60.,1./120.]{for follow in [false,true]{
   let (low,failed,moved)=bump_run(follow,dt);
   eprintln!("BUMP dt={dt:.4} rearm_each_frame={follow} lowest_com_z={low:.3} failed={failed} moved_y={moved:.2}");
  }}
  for follow in [false,true]{
   let (low,failed,moved)=bump_touched(follow,0.6);
   eprintln!("BUMP touched_for=0.6s rearm_each_frame={follow} lowest_com_z={low:.3} failed={failed} moved_y={moved:.2}");
  }
  for (name,from) in [("behind",Vec3::NEG_Y),("front",Vec3::Y),("left",Vec3::NEG_X),("right",Vec3::X)]{for touch in [0.,0.6]{
   let (low,failed,moved)=bump_from(false,touch,from);
   eprintln!("SIDE player={name} touched_for={touch} lowest_com_z={low:.3} failed={failed} moved_away={moved:.2}");
  }}
  for v0 in [0.4,0.8,1.2]{for (name,from) in [("behind",Vec3::NEG_Y),("left",Vec3::NEG_X)]{
   let (low,failed,moved)=bump_from_moving(false,0.,from,v0);
   eprintln!("MOVING player={name} start_speed={v0} lowest_com_z={low:.3} failed={failed} moved_away={moved:.2}");
  }}
  let (low,failed,_)=bump_touched(false,0.6);
  assert!(low>0.6&&!failed,"player contact keeps Balance's clock, not the one-second lean: low {low} failed {failed}");
  let (low,failed,moved)=bump_run(false,1./120.);
  assert!(low>0.6&&!failed&&moved>0.3,"original one-second lean: low {low} failed {failed} moved {moved}");
 }
 /// The in-game Grove Street case: ground falling away along the push (about
 /// 13 degrees under the victim) and the player's walk still pressing for
 /// 0.4 s (~20 N s measured). Diagnostic only: prints which one fells it.
 fn bump_variant(slope_deg:f32,push_ns:f32,look_msg:bool)->(f32,f32){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let path=|n:&str|std::ffi::CString::new(root.join(n).to_str().unwrap()).unwrap();
  assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
  let b=nm_build_load(path("fred-build.txt").as_ptr());let w=nm_world_create();
  let t=slope_deg.to_radians().tan();
  let (y0,y1)=(-0.3f32,20.0f32);let pos=[-20.,y0,0., 20.,y0,0., 20.,y1,-(y1-y0)*t, -20.,y1,-(y1-y0)*t, -20.,-20.,0., 20.,-20.,0.];
  let tri:[u32;12]=[0,1,2,0,2,3,4,5,1,4,1,0];let mat=[0u8;4];
  assert!(nm_world_add_mesh(w,pos.as_ptr(),6,tri.as_ptr(),4,mat.as_ptr())>=0);
  let a=nm_agent_create(b,[0.,0.,0.].as_ptr(),0.,0.4);let dt=1./120.;
  let start=(*a).agent.doll.com().0;
  let other=[start.x,start.y-0.5,start.z, start.x,start.y-0.5,start.z+0.7, start.x,start.y,start.z];
  assert_eq!(nm_agent_ped_bump(a,w,other.as_ptr(),200.,4.,dt,1),1);
  let mut low=f32::MAX;
  for k in 0..(3.7/dt) as usize{
   if (k as f32)*dt<0.4&&push_ns>0.{let at=(*a).agent.doll.com().0;let j=[0.,push_ns*dt/0.4,0.];nm_agent_apply_impulse(a,0,at.to_array().as_ptr(),j.as_ptr());}
   if look_msg{let h=[other[3],other[4]+(k as f32)*dt*1.2,other[5]];
    assert_eq!(nm_msg_begin(a,c"bodyBalance".as_ptr()),0);assert_eq!(nm_msg_set_vec3(a,c"headLookPos".as_ptr(),h.as_ptr()),0);assert!(nm_msg_send(a,w)>=0);}
   assert_eq!(nm_agent_step(a,w,dt),0);
   let c=(*a).agent.doll.com().0;low=low.min(c.z-(-(c.y-y0).max(0.)*t));
  }
  let moved=((*a).agent.doll.com().0-start).y;
  nm_agent_destroy(a);nm_world_destroy(w);nm_build_destroy(b);(low,moved)
 }}
 #[test]fn player_bump_balance_grove_street_factors(){
  for look in [false,true]{for slope in [0.,13.]{for push in [0.,20.]{
   let (low,moved)=bump_variant(slope,push,look);
   eprintln!("GROVE look_msg_each_frame={look} slope_deg={slope} extra_push_ns={push} lowest_com_above_ground={low:.3} moved_y={moved:.2} fell={}",low<0.6);
  }}}
 }
 /// BUMP_STATE: a game log of rig_fit_points + bump_start_state lines. Replays
 /// the exact start (fitted rig, pose, velocities) on the logged ground plane
 /// and on flat ground. Diagnostic: skipped without the file.
 #[test]fn player_bump_replay_game_start(){unsafe{
  let Ok(text)=std::env::var("BUMP_STATE").and_then(|p|std::fs::read_to_string(p).map_err(|_|std::env::VarError::NotPresent))else{eprintln!("BUMP_STATE not set: skipped");return};
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let path=|n:&str|std::ffi::CString::new(root.join(n).to_str().unwrap()).unwrap();
  assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
  let src=nm_build_load(path("fred-build.txt").as_ptr());
  let nums=|s:&str|s.split_whitespace().filter_map(|x|x.parse::<f32>().ok()).collect::<Vec<f32>>();
  let fit=text.lines().find_map(|l|l.strip_prefix("rig_fit_points ")).map(|l|nums(l)[2..].to_vec()).expect("fit");assert_eq!(fit.len(),63);
  let line=text.lines().find_map(|l|l.strip_prefix("bump_start_state ")).expect("state");
  let below:f32=line.split("ground_below_root=").nth(1).unwrap().split_whitespace().next().unwrap().parse().unwrap();
  let n:Vec<f32>=line.split("ground_n=").nth(1).unwrap().split_whitespace().next().unwrap().split(',').map(|x|x.parse().unwrap()).collect();
  let pts=nums(line.split("points=").nth(1).unwrap().split("parts=").next().unwrap());
  let parts=nums(line.split("parts=").nth(1).unwrap());assert_eq!(parts.len(),21*13);
  let own=Vec3::new(pts[6],pts[7],pts[8]);
  {let f=&*nm_build_fit(src,fit.as_ptr());let o=&*src;
   for(i,(a,b))in o.build.parts.iter().zip(&f.build.parts).enumerate(){
    eprintln!("FIT {i:2} {:16} size {:.3} half {:.3}->{:.3} radius {:.3}->{:.3} mass {:.2}->{:.2} pivot_z {:.3}->{:.3}",a.name,b.size/a.size.max(1e-6),a.half,b.half,a.radius,b.radius,a.mass,b.mass,
     o.frames[i].translation.z-o.frames[0].translation.z,f.frames[i].translation.z-f.frames[0].translation.z);}}
  // 0 nm_build_fit as shipped (IV part masses since 2026-11-01), 1 + legs
  // (grown = thigh size), 2/3 total-mass variants, 4/5 divide by size^3 a
  // second time (historical comparison only), 9 unfitted fred.
  for variant in [0,1,2,3,4,5,9]{for sloped in [true,false]{let fitted=variant!=9;
   let b=if fitted{nm_build_fit(src,fit.as_ptr())}else{src};
   if fitted{let bb=&mut (*b).build;let legs=bb.parts[1].size;
    let w:f32=bb.parts.iter().map(|p|p.mass).sum();let v=bb.parts.iter().map(|p|p.mass*p.size.powi(3)).sum::<f32>()/w;
    for p in &mut bb.parts{
     if matches!(variant,1|3|5){p.grown*=legs;}
     if matches!(variant,2|3){p.heft*=1./v;}
     if matches!(variant,4|5){p.heft/=p.size.powi(3);}
    }}
   let w=nm_world_create();
   let normal=if sloped{Vec3::new(n[0],n[1],n[2]).normalize()}else{Vec3::Z};let o=Vec3::new(0.,0.,-below);
   let (u,v)=(normal.any_orthonormal_pair());let c=|a:f32,b:f32|(o+u*a+v*b).to_array();
   let q=[c(-20.,-20.),c(20.,-20.),c(20.,20.),c(-20.,20.)].concat();let tri:[u32;6]=if (u.cross(v)).dot(normal)>0.{[0,1,2,0,2,3]}else{[0,2,1,0,3,2]};
   assert!(nm_world_add_mesh(w,q.as_ptr(),4,tri.as_ptr(),2,[0u8;2].as_ptr())>=0);
   let a=nm_agent_create(b,[0.,0.,-below].as_ptr(),0.,0.4);
   for i in 0..21{let r=&parts[i*13..i*13+13];
    assert_eq!(nm_agent_set_part_state(a,i as i32,r[0..3].as_ptr(),r[3..7].as_ptr(),r[7..10].as_ptr(),r[10..13].as_ptr()),0);}
   let rel:Vec<f32>=(0..3).flat_map(|k|{let p=Vec3::new(pts[k*3],pts[k*3+1],pts[k*3+2])-own;p.to_array()}).collect();
   let dt=1./120.;assert_eq!(nm_agent_ped_bump(a,w,rel.as_ptr(),200.,4.,dt,1),1);
   let mut low=f32::MAX;let start=(*a).agent.doll.com().0;
   for _ in 0..(3.0/dt) as usize{assert_eq!(nm_agent_step(a,w,dt),0);let c=(*a).agent.doll.com().0;low=low.min((c-o).dot(normal));}
   let moved=(*a).agent.doll.com().0-start;
   eprintln!("REPLAY variant={variant} sloped={sloped} lowest_com_above_ground={low:.3} failed={} moved=({:.2},{:.2})",(*a).ped_bump.failed,moved.x,moved.y);
   nm_agent_destroy(a);nm_world_destroy(w);if fitted{nm_build_destroy(b);}
  }}
  nm_build_destroy(src);
 }}
 // Same in-game start, many small perturbations (walk speed, walk heading,
 // slope steepness): counts how often each balancer leg setting stays up.
 // legs 0 = IV constants, 1 = x thigh size (Build::scaled convention),
 // 2 = x actual hip-ankle length and hip width; 9 = unfitted IV rig.
 #[test]fn player_bump_slope_sweep(){unsafe{
  let Ok(text)=std::env::var("BUMP_STATE").and_then(|p|std::fs::read_to_string(p).map_err(|_|std::env::VarError::NotPresent))else{eprintln!("BUMP_STATE not set: skipped");return};
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let path=|n:&str|std::ffi::CString::new(root.join(n).to_str().unwrap()).unwrap();
  assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
  let src=nm_build_load(path("fred-build.txt").as_ptr());
  let nums=|s:&str|s.split_whitespace().filter_map(|x|x.parse::<f32>().ok()).collect::<Vec<f32>>();
  let fit=text.lines().find_map(|l|l.strip_prefix("rig_fit_points ")).map(|l|nums(l)[2..].to_vec()).expect("fit");
  let line=text.lines().find_map(|l|l.strip_prefix("bump_start_state ")).expect("state");
  let below:f32=line.split("ground_below_root=").nth(1).unwrap().split_whitespace().next().unwrap().parse().unwrap();
  let n:Vec<f32>=line.split("ground_n=").nth(1).unwrap().split_whitespace().next().unwrap().split(',').map(|x|x.parse().unwrap()).collect();
  let pts=nums(line.split("points=").nth(1).unwrap().split("parts=").next().unwrap());
  let parts=nums(line.split("parts=").nth(1).unwrap());
  let own=Vec3::new(pts[6],pts[7],pts[8]);
  let ratios={let f=&*nm_build_fit(src,fit.as_ptr());let o=&*src;
   let d=|fr:&[Transform],a:usize,b:usize|(fr[a].translation-fr[b].translation).length();
   let r=((d(&f.frames,1,3)+d(&f.frames,4,6))/(d(&o.frames,1,3)+d(&o.frames,4,6)),d(&f.frames,1,4)/d(&o.frames,1,4),f.build.parts[1].size);
   nm_build_destroy(f as *const _ as *mut _);r};
  eprintln!("RATIOS hip_ankle={:.3} hip_width={:.3} thigh_size={:.3} iv_legs={:?}",ratios.0,ratios.1,ratios.2,delirium::nmdbm::CHAR_LEGS());
  let slope0=Vec3::new(n[0],n[1],n[2]).normalize();
  // walker: the player keeps walking through at its in-game 1.74 m/s for
  // 1.2 s as an animated owner (kinematic here; 25 kg + motor in game).
  let walker_v=std::env::var("BUMP_WALKER").ok().and_then(|v|{let v:Vec<f32>=v.split(',').filter_map(|x|x.parse().ok()).collect();(v.len()==3).then(||Vec3::new(v[0],v[1],v[2]))});
  for walker in [false,true]{if walker&&walker_v.is_none(){continue}
  for variant in [0,1,2,9]{let(mut up,mut total,mut worst)=(0,0,f32::MAX);
   for speed in [0.85f32,1.0,1.15]{for yaw in [-0.07f32,0.0,0.07]{for steep in [0.8f32,1.0,1.2]{
    let b=if variant!=9{nm_build_fit(src,fit.as_ptr())}else{src};
    let w=nm_world_create();
    let tilt=Vec3::new(slope0.x*steep,slope0.y*steep,0.);let normal=(tilt+Vec3::Z*slope0.z).normalize();let o=Vec3::new(0.,0.,-below);
    let (u,v)=normal.any_orthonormal_pair();let c=|a:f32,b:f32|(o+u*a+v*b).to_array();
    let q=[c(-20.,-20.),c(20.,-20.),c(20.,20.),c(-20.,20.)].concat();let tri:[u32;6]=if (u.cross(v)).dot(normal)>0.{[0,1,2,0,2,3]}else{[0,2,1,0,3,2]};
    assert!(nm_world_add_mesh(w,q.as_ptr(),4,tri.as_ptr(),2,[0u8;2].as_ptr())>=0);
    let a=nm_agent_create(b,[0.,0.,-below].as_ptr(),0.,0.4);
    let iv=delirium::nmdbm::CHAR_LEGS();
    match variant{1=>(*a).agent.legs=(iv.0*ratios.2,iv.1*ratios.2),2=>(*a).agent.legs=(iv.0*ratios.0,iv.1*ratios.1),_=>{}}
    let turn=Quat::from_rotation_z(yaw);
    for i in 0..21{let r=&parts[i*13..i*13+13];
     let vel=(turn*Vec3::new(r[7],r[8],r[9])*speed).to_array();let ang=(turn*Vec3::new(r[10],r[11],r[12])*speed).to_array();
     assert_eq!(nm_agent_set_part_state(a,i as i32,r[0..3].as_ptr(),r[3..7].as_ptr(),vel.as_ptr(),ang.as_ptr()),0);}
    let rel:Vec<f32>=(0..3).flat_map(|k|{let p=Vec3::new(pts[k*3],pts[k*3+1],pts[k*3+2])-own;p.to_array()}).collect();
    let dt=1./120.;assert_eq!(nm_agent_ped_bump(a,w,rel.as_ptr(),200.,4.,dt,1),1);
    let mut low=f32::MAX;let peer=Vec3::new(rel[0],rel[1],rel[2]);
    for k in 0..(3.0/dt) as usize{let t=k as f32*dt;
     if walker{assert_eq!(nm_world_set_boxes(w,std::ptr::null(),0),0);
      if t<1.2{let v=walker_v.unwrap();let at=(peer+v*t).to_array();
       assert!(crate::original_animated_owner_api::nm_world_append_animated_owner(w,1,at.as_ptr(),v.y.atan2(v.x)-std::f32::consts::FRAC_PI_2,v.to_array().as_ptr())>=0);
       nm_agent_ped_bump_touch(a);}}
     assert_eq!(nm_agent_step(a,w,dt),0);let c=(*a).agent.doll.com().0;low=low.min((c-o).dot(normal));}
    total+=1;if low>0.6&&!(*a).ped_bump.failed{up+=1;}worst=worst.min(low);
    if speed==1.0&&yaw==0.0&&steep==1.0{eprintln!("EXACT walker={walker} legs={variant} lowest_com={low:.3} failed={}",(*a).ped_bump.failed);}
    nm_agent_destroy(a);nm_world_destroy(w);if variant!=9{nm_build_destroy(b);}
   }}}
   eprintln!("SWEEP walker={walker} legs={variant} standing={up}/{total} worst_com={worst:.3}");
  }}
  nm_build_destroy(src);
 }}
 #[test]fn gap_audit_reports_world_overlap_from_solver_rows(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let b=nm_build_load(std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap().as_ptr());
  let audit=|z:f32|{let w=nm_world_create();nm_world_add_floor(w,0.,0.,z,20.);
   let a=nm_agent_create(b,[0.,0.,0.].as_ptr(),0.,0.4);
   let start=(*a).agent.doll.com().0;let other=[start.x,start.y-0.5,start.z,start.x,start.y-0.5,start.z+0.7,start.x,start.y,start.z];
   assert_eq!(nm_agent_ped_bump(a,w,other.as_ptr(),200.,4.,1./120.,1),1);
   for _ in 0..3{assert_eq!(nm_agent_step(a,w,1./120.),0);}
   let mut rows=[0f32;40];assert_eq!(nm_agent_gap_audit(a,rows.as_mut_ptr(),5),5);
   nm_agent_destroy(a);nm_world_destroy(w);rows};
  // Resting on the floor the rows sit near the solver's allowed slop; a floor
  // 5 cm above the feet's resting height is reported as real overlap.
  let free=audit(-2.0);assert_eq!(free[5],0.,"no world rows far from the floor");
  let sunk=audit(0.05);
  assert!(sunk[5]>0.&&sunk[1]< -0.01,"world gap {} rows {}",sunk[1],sunk[5]);
  assert_eq!(sunk[8],1.);assert_eq!(sunk[13],0.,"no vehicle rows");
  nm_build_destroy(b);
 }}
 #[test]fn fitted_rig_keeps_original_part_masses(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let src=nm_build_load(std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap().as_ptr());
  // Joints stretched unevenly: legs longer, torso shorter (as SA model 7).
  let a=nm_agent_create(src,[0.,0.,0.].as_ptr(),0.,0.4);let mut j=[0f32;63];
  for i in 0..21{let(mut q,mut par,mut lim)=([0f32;4],0,[0f32;3]);let mut p=[0f32;3];
   if i==0{nm_agent_get_part_transform(a,0,p.as_mut_ptr(),q.as_mut_ptr());}else{nm_agent_get_joint(a,i,&mut par,p.as_mut_ptr(),q.as_mut_ptr(),lim.as_mut_ptr(),std::ptr::null_mut());}
   j[i as usize*3..i as usize*3+3].copy_from_slice(&p);}
  nm_agent_destroy(a);let pelvis=Vec3::new(j[0],j[1],j[2]);
  for i in 1..21usize{let k=if (1..7).contains(&i){1.2}else{0.8};for c in 0..3{j[i*3+c]=pelvis[c]+(j[i*3+c]-pelvis[c])*k;}}
  let f=nm_build_fit(src,j.as_ptr());assert!(!f.is_null());
  for i in 0..21{let(mut m0,mut m1)=(0f32,0f32);
   nm_build_part_info(src,i,&mut m0,std::ptr::null_mut(),std::ptr::null_mut(),std::ptr::null_mut());
   nm_build_part_info(f,i,&mut m1,std::ptr::null_mut(),std::ptr::null_mut(),std::ptr::null_mut());
   assert!((m0-m1).abs()<=m0*1e-4,"part {i}: mass {m0} became {m1}");}
  assert!((&*f).build.parts[2].half>(&*src).build.parts[2].half*1.1,"shapes must still follow the target bones");
  nm_build_destroy(f);nm_build_destroy(src);
 }}
 #[test]fn reshaped_skin_parts_keep_joints_masses_and_rest(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let src=nm_build_load(std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap().as_ptr());
  let a=nm_agent_create(src,[0.,0.,0.].as_ptr(),0.,0.4);let mut j=[0f32;63];
  for i in 0..21{let(mut q,mut par,mut lim)=([0f32;4],0,[0f32;3]);let mut p=[0f32;3];
   if i==0{nm_agent_get_part_transform(a,0,p.as_mut_ptr(),q.as_mut_ptr());}else{nm_agent_get_joint(a,i,&mut par,p.as_mut_ptr(),q.as_mut_ptr(),lim.as_mut_ptr(),std::ptr::null_mut());}
   j[i as usize*3..i as usize*3+3].copy_from_slice(&p);}
  nm_agent_destroy(a);
  let f=nm_build_fit(src,j.as_ptr());assert!(!f.is_null());
  let joints=|b:*const NmBuild|{let a=nm_agent_create(b,[0.,0.,1.].as_ptr(),0.,0.4);let mut out=vec![];
   for i in 1..21{let(mut q,mut par,mut lim,mut p)=([0f32;4],0,[0f32;3],[0f32;3]);nm_agent_get_joint(a,i,&mut par,p.as_mut_ptr(),q.as_mut_ptr(),lim.as_mut_ptr(),std::ptr::null_mut());out.push(Vec3::from(p));}
   nm_agent_destroy(a);out};
  let before=joints(f);let mut masses=[0f32;21];
  for i in 0..21{nm_build_part_info(f,i,&mut masses[i as usize],std::ptr::null_mut(),std::ptr::null_mut(),std::ptr::null_mut());}
  let mut e=[0f32;3];assert_eq!(nm_build_part_box(f,12,e.as_mut_ptr()),0);assert_eq!(nm_build_part_box(f,16,e.as_mut_ptr()),1);
  // Wrong kind / implausible sizes are refused without mutation.
  assert!(nm_build_reshape_part(f,12,[0.,0.,0.].as_ptr(),0.1,0.03,[0.1,0.1,0.1].as_ptr())<0);
  assert!(nm_build_reshape_part(f,12,[0.,0.,0.].as_ptr(),0.9,0.03,std::ptr::null())<0);
  let (mut r0,mut f0)=(0f32,[0f32;3]);let mut q0=[0f32;4];
  nm_build_part_info(f,12,std::ptr::null_mut(),&mut r0,std::ptr::null_mut(),std::ptr::null_mut());nm_build_part_frame(f,12,f0.as_mut_ptr(),q0.as_mut_ptr());
  let delta=Vec3::new(0.0,0.06,0.03);
  assert_eq!(nm_build_reshape_part(f,12,delta.to_array().as_ptr(),r0*1.4,0.04,std::ptr::null()),0);
  assert_eq!(nm_build_reshape_part(f,16,[0.,0.01,0.].as_ptr(),0.,0.,[0.05,0.1,0.06].as_ptr()),0);
  assert_eq!(nm_build_reshape_part(f,15,[0.005,0.,0.].as_ptr(),0.07,0.12,std::ptr::null()),0);
  let mut f1=[0f32;3];nm_build_part_frame(f,12,f1.as_mut_ptr(),q0.as_mut_ptr());
  let moved=Vec3::from(f1)-Vec3::from(f0);
  assert!((moved.length()-delta.length()).abs()<1e-5,"body moves to the shape centre: {moved}");
  let after=joints(f);
  for (i,(x,y)) in before.iter().zip(&after).enumerate(){assert!(x.distance(*y)<1e-4,"joint {} moved {}",i+1,x.distance(*y));}
  for i in 0..21{let mut m=0f32;nm_build_part_info(f,i,&mut m,std::ptr::null_mut(),std::ptr::null_mut(),std::ptr::null_mut());assert!((m-masses[i as usize]).abs()<1e-5,"part {i} mass changed");}
  // At rest on flat ground the reshaped body does not self-explode.
  let w=nm_world_create();nm_world_add_floor(w,0.,0.,0.,20.);
  let a=nm_agent_create(f,[0.,0.,0.].as_ptr(),0.,0.4);
  let start=(*a).agent.doll.com().0;let other=[start.x,start.y-0.5,start.z,start.x,start.y-0.5,start.z+0.7,start.x,start.y,start.z];
  assert_eq!(nm_agent_ped_bump(a,w,other.as_ptr(),200.,4.,1./120.,1),1);
  for _ in 0..240{assert_eq!(nm_agent_step(a,w,1./120.),0);}
  let mut v=[0f32;3];let mut q=[0f32;4];let mut p=[0f32;3];
  for i in 0..21{nm_agent_get_part_transform(a,i,p.as_mut_ptr(),q.as_mut_ptr());assert!(Vec3::from(p).is_finite());}
  nm_agent_get_part_velocity(a,0,v.as_mut_ptr(),std::ptr::null_mut());assert!(Vec3::from(v).length()<20.,"pelvis speed {}",Vec3::from(v).length());
  nm_agent_destroy(a);nm_world_destroy(w);nm_build_destroy(f);nm_build_destroy(src);
 }}
 #[test]fn outer_body_touch_is_not_fragment_contact(){unsafe{
  assets();
  let root=[0.,0.,1.];
  let mut fragment=[0.,0.50,0.8,0.,0.50,1.5,0.17];
  assert_eq!(nm_ped_meets_fragment(root.as_ptr(),fragment.as_ptr(),1),0);
  fragment[1]=0.35;fragment[4]=0.35;
  assert_eq!(nm_ped_meets_fragment(root.as_ptr(),fragment.as_ptr(),1),1);
  fragment[6]=f32::NAN;assert_eq!(nm_ped_meets_fragment(root.as_ptr(),fragment.as_ptr(),1),NM_E_ARG);
 }}
 #[test]fn melee_flinch_finishes_on_original_feedback_and_death_does_not_get_up(){unsafe{
  assets();let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
  let b=nm_build_load(file.as_ptr());let a=nm_agent_create(b,[0.,0.,1.].as_ptr(),0.,0.4);let w=nm_world_create();
  assert_eq!(nm_agent_melee_task(a,w,[0.,1.,1.7].as_ptr(),200.,3,1),0);
  let n=&mut *a;assert_eq!(n.ped_bump.task,Some(nm::Task::Flinch));
  n.agent.feedback=vec![delirium::nmshot::Feedback{slot:12,name:"balance",value:0.}];
  n.ped_bump.before_step(&mut n.agent,&n.build,&(*w).0,nm::Task::Flinch.min_time()+0.01);assert!(n.ped_bump.ready);
  assert_eq!(nm_agent_melee_task(a,w,[0.,1.,1.7].as_ptr(),200.,3,1),0);
  assert_eq!(nm_agent_contact_health(a,0.),0);let n=&mut *a;
  n.ped_bump.before_step(&mut n.agent,&n.build,&(*w).0,20.);assert!(!n.ped_bump.ready);
  nm_agent_destroy(a);nm_build_destroy(b);nm_world_destroy(w);
 }}
 #[test]fn contact_is_reciprocal_and_a_miss_has_no_push(){unsafe{
  let mut input=[0.,0.,1.,0.,0.,1.5,0.,70., 0.,0.48,1.,0.,0.,0.,0.,70.];let mut out=[0.;22];
  assert!(nm_ped_contacts(input.as_ptr(),2,1./120.,out.as_mut_ptr())>0);
  assert_eq!((out[6],out[17]),(1.,0.));assert!(out[7]>0.);
  assert!((out[4]+out[15]-1.5).abs()<1e-5);assert!(out[2].abs()+out[13].abs()<1e-6);
  input[9]=0.9;assert_eq!(nm_ped_contacts(input.as_ptr(),2,1./120.,out.as_mut_ptr()),0);assert_eq!((out[6],out[17]),(-1.,-1.));
 }}
}

/// Select an original bump::react response with semantic BlendFromNM context.
/// It only caches the accepted task: no pose/velocity/controller mutation yet.
/// points9: peer root3, peer look3, subject root3. clip0=no evaluated clip,
/// clip1=SA front get-up equivalent (original group5), clip2=back (group6).
/// flags: peer player1, subject player2, mission4, armed8, animated-owner16.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_prepare_recovery_contact(a:*mut NmAgent,points:*const f32,health:f32,impulse:f32,dt:f32,clip:i32,phase:f32,flags:u32)->i32{
    let a=agent!(a);a.ped_bump.prepared=None;
    if points.is_null()||!health.is_finite()||health<=0.||!impulse.is_finite()||impulse<=0.||!dt.is_finite()||dt<=0.||dt>0.1||!(0..=2).contains(&clip)||flags&!31!=0||
       (clip!=0&&(!phase.is_finite()||!(0.0..=1.0).contains(&phase))){return fail(NM_E_ARG,"invalid recovery contact")}
    let points=unsafe{std::slice::from_raw_parts(points,9)};if points.iter().any(|x|!x.is_finite()){return fail(NM_E_ARG,"invalid recovery contact points")}
    use delirium::bump::{self,Thing,Kind};
    let peer=Thing{id:1,kind:Kind::Ped{player:flags&1!=0,ragdoll:false}};
    let(_,vel)=a.agent.doll.com();let place=Vec3::from_slice(&points[6..]);
    let mass=if flags&16!=0{pb::MASS}else{a.agent.doll.mass()};
    let ped=bump::Ped{id:0,health,inv_mass:1./mass,woke:true,drop:None,vz:vel.z,tough:false,player_cut:false,
        player:flags&2!=0,mission:flags&4!=0,quick_relax:false,agility:3,gun:flags&8!=0,place,vel,standing_on:None};
    let blend=bump::Blend{state:2,id:if clip==0{u32::MAX}else{102},group:match clip{1=>5,2=>6,_=>u32::MAX},phase:(clip!=0).then_some(phase)};
    let around=bump::Around{touching:vec![peer],player:(flags&1!=0).then_some(bump::Player{thing:peer,place:Vec3::from_slice(points),in_car:false}),blend:Some(blend),sit:None,die:None,accepts:true};
    let impact=bump::Impact{impulse,dt,component:0,other:Some(peer)};
    let mut rand=RAND.lock().unwrap_or_else(|e|e.into_inner());
    let Some(made)=bump::react(&ped,&impact,&around,0.,false,health,&mut rand,||None)else{return 0};
    if !matches!(made.wrap,Some(bump::Wrap::ComplexNm(_))){return 0}
    let kind=match made.task{bump::Task::Relax{..}=>1,bump::Task::RollUp{..}=>2,bump::Task::Balance{..}=>3,_=>return 0};
    a.ped_bump.prepared=Some(PreparedRecovery{made,health,player:flags&2!=0,look:Vec3::from_slice(&points[3..])});kind
}
/// Host has accepted ComplexNM and seeded the retained body from the CURRENT
/// pose/motion. Start exactly the prepared response; never add another impulse.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_start_recovery_contact(a:*mut NmAgent,w:*const NmWorld)->i32{
    let a=agent!(a);let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null recovery response world")};
    let Some(p)=a.ped_bump.prepared.take()else{return 0};
    use delirium::bump;let mut rand=RAND.lock().unwrap_or_else(|e|e.into_inner());a.agent.stop_all_behaviours();
    let(task,roll,kind)=match p.made.task{
        bump::Task::Relax{a:x,b:y}=>{nm::task_relax(&mut a.agent,x,y);(nm::Task::Relax,None,1)},
        bump::Task::RollUp{roll}=>{let r=with_tp(|tp|nm::RollUpAndRelax::start(&mut a.agent,&a.build,&w.0,tp,&mut rand,-1.,1.,roll as u32));(nm::Task::Relax,Some(r),2)},
        bump::Task::Balance{set,..}=>{with_tp(|tp|nm::task_balance(&mut a.agent,&a.build,&w.0,tp,set,Some(p.look),false));(nm::Task::Balance,None,3)},
        _=>return 0,
    };
    a.agent.contact_wake=true;
    a.ped_bump=PedBump{task:Some(task),roll,times:Some([p.made.min as f32/1000.,p.made.max as f32/1000.]),health:p.health,player:p.player,agility:3,..Default::default()};kind
}
