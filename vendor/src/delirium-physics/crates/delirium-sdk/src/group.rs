//! Host transport for the existing articulated, reciprocal island solver.
//! Stage immutable per-agent collider snapshots, then advance all bodies once.
use super::*;

const GROUP_CAPACITY:usize=32;
/// Actual transport admission capacity; callers must reserve all live owners.
#[unsafe(no_mangle)]
pub extern "C" fn nm_group_capacity()->i32{GROUP_CAPACITY as i32}

struct Staged { agent:*mut NmAgent, id:usize, obstacles:Vec<Obstacle>, others:Vec<Capsule> }
#[derive(Default)]
pub struct NmGroup { pub(super) solver:delirium::ragdoll::Group, staged:Vec<Staged>, pub(super) external_ids:std::collections::BTreeSet<u64>, pub(super) external_invalid:bool }

#[unsafe(no_mangle)]
pub extern "C" fn nm_group_create()->*mut NmGroup {Box::into_raw(Box::new(NmGroup::default()))}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_destroy(g:*mut NmGroup){if !g.is_null(){drop(unsafe{Box::from_raw(g)});}}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_reset(g:*mut NmGroup)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};*g=Default::default();0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_begin(g:*mut NmGroup)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};g.staged.clear();g.external_ids.clear();g.external_invalid=false;
    if g.solver.stage_external(0,&[]).is_err(){return fail(NM_E_STATE,"could not reset external owners")}0
}
/// The agent must remain alive until step/begin/reset. Identity includes the
/// host pool generation; duplicate identities and pointers are rejected.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_add(g:*mut NmGroup,a:*mut NmAgent,id:u32,w:*const NmWorld)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};
    let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null world")};
    if a.is_null()||g.staged.iter().any(|s|s.agent==a||s.id==id as usize){return fail(NM_E_ARG,"duplicate/null group member")}
    if unsafe{(*a).mode}!=NM_MODE_LEAD{return fail(NM_E_FOLLOW,"group requires simulated LEAD bodies")}
    if g.staged.len()>=GROUP_CAPACITY{return fail(NM_E_ARG,"too many group members")}
    g.staged.push(Staged{agent:a,id:id as usize,obstacles:w.1.clone(),others:w.2.clone()});0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_step(g:*mut NmGroup,w:*const NmWorld,dt:f32)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};
    let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null world")};
    if !(dt>0.&&dt<=0.25){return fail(NM_E_ARG,"invalid group dt")}
    if g.external_invalid||g.staged.iter().flat_map(|s|&s.obstacles).any(|o|o.owner.is_some_and(|id|!g.external_ids.contains(&id))){
        g.staged.clear();return fail(NM_E_STATE,"unstaged or rejected external owner; island not advanced")
    }
    if let Err(error)=g.solver.stage_external_geometry(&w.1){
        g.staged.clear();return fail(NM_E_STATE,format!("external geometry rejected: {error:?}"))
    }
    let staged=std::mem::take(&mut g.staged);
    // Unique pointers/identities were validated before creating any &mut.
    let mut members:Vec<_>=staged.iter().map(|s|{
        let a=unsafe{&mut *s.agent};
        before_step(a,&w.0,dt);
        (&mut a.agent,s.id,&a.build,s.obstacles.as_slice(),s.others.as_slice())
    }).collect();
    // Opt-in observation only: records the existing per-owner narrowphase
    // counters on slow steps, without changing any solver work or decisions.
    static PROFILE:std::sync::OnceLock<Option<std::path::PathBuf>>=std::sync::OnceLock::new();
    let profile=PROFILE.get_or_init(||std::env::var_os("GTASA_PHYSICS_PROFILE").map(std::path::PathBuf::from));
    g.solver.profile_enabled=profile.is_some();
    let started=profile.as_ref().map(|_|std::time::Instant::now());
    Agent::step_group(&mut members,&w.0,dt,&mut g.solver);
    if let(Some(path),Some(started))=(profile,started){
        let elapsed=started.elapsed().as_secs_f64()*1000.;
        static LIMIT:std::sync::OnceLock<f64>=std::sync::OnceLock::new();
        let limit=*LIMIT.get_or_init(||std::env::var("GTASA_PHYSICS_PROFILE_MS").ok().and_then(|v|v.parse().ok()).unwrap_or(8.));
        if elapsed>=limit {
            let report=serde_json::json!({"pid":std::process::id(),"dt":dt,"members":staged.len(),"elapsed_ms":elapsed,"phase_ms":g.solver.profile_ms,"impulses_stage_ms":g.solver.profile_impulses.0,"impulses_counts":g.solver.profile_impulses.1,"solve_stage_ms":g.solver.profile_solve_ms,"solve_stage_cycles":g.solver.profile_solve_cycles,
                "owner_pair_prepare_ms":g.solver.profile_pair_prepare_ms,"owner_pair_counts":g.solver.profile_pair_counts,
                "owners":g.solver.external_placement_diagnostics().iter().map(crate::external_replay::diagnostic).collect::<Vec<_>>()});
            // Format in memory first. Value's Display issued614 tiny Write calls
            // for one recorded row (audit_profile_io); disk observation must
            // not amplify a slow step through hundreds of synchronous writes.
            if let Ok(mut f)=std::fs::OpenOptions::new().create(true).append(true).open(path){use std::io::Write;let line=report.to_string();let _=writeln!(f,"{line}");}
        }
    }
    drop(members);
    if let Some(error)=g.solver.external_error(){return fail(NM_E_STATE,format!("external island rejected: {error:?}"))}
    for s in staged {let a=unsafe{&mut *s.agent};a.agent.doll.roll_push=None;a.set.fill(false);crate::end_no_collision_frame(a);}
    0
}
/// Physical peer pairs, active manifold points, maximum pre-solve penetration
/// and total solved normal impulse. No helper flag counts as a contact.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_contacts(g:*const NmGroup,out:*mut f32)->i32 {
    let Some(g)=(unsafe{g.as_ref()})else{return fail(NM_E_ARG,"null group")};
    if out.is_null(){return fail(NM_E_ARG,"null group observation")}
    unsafe{std::ptr::copy_nonoverlapping(g.solver.contact_summary().as_ptr(),out,4)};0
}

#[cfg(test)]
mod tests {
    use super::*;
    fn build(mass:f32)->Build {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let asset=std::ffi::CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
        unsafe{assert!(matches!(nm_init(asset.as_ptr()),0|1));}
        let (source,_)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
        let mut p=source.parts[16].clone();p.parent=None;p.bone=0;p.mass=mass;
        p.boxed=None;p.radius=0.2;p.half=0.;p.anchor=Vec3::ZERO;p.offset=Transform::IDENTITY;
        p.inertia=Vec3::splat(0.4*mass*0.2*0.2);
        Build{parts:vec![p],nm:source.nm}
    }
    fn chain(speed:f32,axis:Vec3,reverse:bool)->(Vec<Vec3>,f32) {
        let builds=[build(1.),build(2.),build(3.)];
        let mut dolls:Vec<_>=builds.iter().enumerate().map(|(i,b)|{
            let f=Transform::from_translation(Vec3::Z*15.+axis*(i as f32*0.45));
            Ragdoll::new(b,&[f],if i==0{axis*speed}else{Vec3::ZERO})
        }).collect();
        let mut free:Vec<_>=builds.iter().enumerate().map(|(i,b)|{
            let f=Transform::from_translation(Vec3::Z*15.+axis*(i as f32*0.45));
            Ragdoll::new(b,&[f],if i==0{axis*speed}else{Vec3::ZERO})
        }).collect();
        let world=Collision::default();let mut group=delirium::ragdoll::Group::default();
        let drive=delirium::ragdoll::Drive{lift:1.,..Default::default()};
        let mut max_error=0f32;let mut touched=false;
        for _ in 0..240 {
            let mut ms:Vec<_>=dolls.iter_mut().zip(&builds).enumerate().map(|(i,(doll,b))|
                delirium::ragdoll::Member{doll,id:100+i*256,build:b,drive,obstacles:&[],others:&[]}).collect();
            if reverse{ms.reverse();}
            Ragdoll::step_group(&mut ms,&world,1./120.,&mut group);drop(ms);
            for (d,b) in free.iter_mut().zip(&builds){d.step(b,&world,1./120.,drive,&[],&[]);}
            let momentum=|ds:&[Ragdoll]|ds.iter().map(|d|d.vel(0).0*d.mass()).sum::<Vec3>();
            max_error=max_error.max(momentum(&dolls).distance(momentum(&free)));
            touched|=group.contact_summary()[0]>0.;
            let pos:Vec<_>=dolls.iter().map(|d|d.frames().next().unwrap().translation).collect();
            assert!(pos.iter().all(|p|p.is_finite()));
            for pair in pos.windows(2){assert!((pair[1]-pair[0]).dot(axis)>0.30,"bodies crossed or deeply interpenetrated: {pos:?}");}
        }
        assert!(touched,"fixture must actually collide");
        assert!(dolls[2].vel(0).0.dot(axis)>0.,"force must reach the third finite body");
        assert!(max_error<0.002,"unbalanced physical-peer impulse: {max_error}");
        (dolls.iter().map(|d|d.frames().next().unwrap().translation).collect(),max_error)
    }
    #[test]
    fn unequal_mass_contact_chain_transmits_momentum_in_every_direction_without_order_bias(){
        for speed in [0.5,3.,8.] {for axis in [Vec3::X,Vec3::Y,Vec3::new(-1.,1.,0.).normalize()] {
            let(a,_)=chain(speed,axis,false);let(b,_)=chain(speed,axis,true);
            for(x,y)in a.iter().zip(&b){assert!(x.distance(*y)<1e-4,"pool order changed the solution");}
        }}
    }
    #[test]
    fn exported_capacity_matches_real_staging_and_never_evicts_live_bodies(){unsafe{
        build(1.);let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
        let b=nm_build_load(file.as_ptr());let w=nm_world_create();let g=nm_group_create();nm_group_begin(g);
        let mut agents=vec![];
        for i in 0..=nm_group_capacity(){
            let a=nm_agent_create(b,[i as f32*3.,0.,2.].as_ptr(),0.,0.4);assert!(!a.is_null());agents.push(a);
            assert_eq!(nm_group_add(g,a,i as u32,w),if i<nm_group_capacity(){0}else{NM_E_ARG});
        }
        assert_eq!((*g).staged.len(),nm_group_capacity()as usize);
        for(i,s)in (*g).staged.iter().enumerate(){assert_eq!(s.agent,agents[i]);}
        nm_group_destroy(g);for a in agents{nm_agent_destroy(a);}nm_build_destroy(b);nm_world_destroy(w);
    }}
    #[test]
    fn staging_rejects_aliases_and_snapshots_each_members_colliders(){unsafe{
        build(1.);let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        let file=std::ffi::CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
        let b=nm_build_load(file.as_ptr());let a=nm_agent_create(b,[0.,0.,2.].as_ptr(),0.,0.4);
        let other=nm_agent_create(b,[2.,0.,2.].as_ptr(),0.,0.4);
        let w=nm_world_create();let g=nm_group_create();nm_group_begin(g);
        nm_world_set_boxes(w,[1.,2.,3.,0.,0.,0.,1.,0.1,0.1,0.1,0.,0.,0.].as_ptr(),1);
        assert_eq!(nm_group_add(g,a,1,w),0);
        assert_eq!(nm_group_add(g,a,2,w),NM_E_ARG);assert_eq!(nm_group_add(g,other,1,w),NM_E_ARG);
        nm_world_set_boxes(w,std::ptr::null(),0);assert_eq!(nm_group_add(g,other,2,w),0);
        assert_eq!((&(*g).staged)[0].obstacles.len(),1);assert!((&(*g).staged)[1].obstacles.is_empty());
        assert_eq!(nm_group_step(g,w,1./60.),0);assert!((*g).staged.is_empty());
        nm_group_reset(g);nm_group_destroy(g);nm_agent_destroy(a);nm_agent_destroy(other);nm_build_destroy(b);nm_world_destroy(w);
    }}
}
