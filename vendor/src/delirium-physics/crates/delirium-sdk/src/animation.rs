use super::*;

/// Original 82880F58/8288CEA8 animation-to-ragdoll motion. Seven floats per
/// input part: world xyz, quaternion xyzw; six output floats: velocity, spin.
/// Call once at handoff with consecutive evaluated poses and their elapsed
/// simulation time. Uses IV's incoming scale/limits [0.5, 3 m/s, 2 rad/s].
/// This is initialization, never a clamp on an already simulating body.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_animation_start_motion(now:*const f32,previous:*const f32,parts:i32,dt:f32,reference_velocity:*const f32,out:*mut f32)->i32 {
    if now.is_null()||previous.is_null()||reference_velocity.is_null()||out.is_null()||parts<=0||parts>256||!dt.is_finite()||dt<=0.0 {
        return fail(NM_E_ARG,"invalid animation motion input");
    }
    let v=unsafe{v3(reference_velocity)};
    if !v.is_finite(){return fail(NM_E_ARG,"invalid reference velocity")}
    let rows=|p:*const f32|unsafe{
        let pos=v3(p);let q=Quat::from_xyzw(*p.add(3),*p.add(4),*p.add(5),*p.add(6));
        if !pos.is_finite()||!q.is_finite()||q.length_squared()<1e-8{return None}
        let m=Mat3::from_quat(q.normalize());Some([m.x_axis,m.y_axis,m.z_axis,pos])
    };
    // Validate the whole batch before changing the caller's destination.
    let mut motion=Vec::with_capacity(parts as usize);
    for k in 0..parts as usize {
        let Some(n)=rows(unsafe{now.add(k*7)}) else{return fail(NM_E_ARG,"invalid current pose")};
        let Some(p)=rows(unsafe{previous.add(k*7)}) else{return fail(NM_E_ARG,"invalid previous pose")};
        let (_,linear,spin)=delirium::ragstart::start_part(n,Some(&p),Vec3::ZERO,dt,[0.5,3.0,2.0],v);
        if !linear.is_finite()||!spin.is_finite(){return fail(NM_E_ARG,"nonfinite start motion")}
        motion.push((linear,spin));
    }
    for (k,(linear,spin)) in motion.into_iter().enumerate(){unsafe{put3(out.add(k*6),linear);put3(out.add(k*6+3),spin)}}
    parts
}

/// Lease authored joint targets for the next step only. Seven floats per
/// body: world position xyz, quaternion xyzw. These are fitted NM body frames,
/// not raw SA bone matrices. No position/velocity is imposed on a body.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_submit_animation_pose(a:*mut NmAgent, transforms:*const f32, parts:i32,
    mask:*const c_char, weight:f32)->i32 {
    let a=agent!(a);
    a.agent.cancel_animation_pose();
    let Some(mask)=(unsafe{text(mask)}) else {return fail(NM_E_ARG,"animation mask missing")};
    if parts<=0 || parts as usize!=a.build.parts.len() || transforms.is_null() {
        return fail(NM_E_ARG,"need every animation transform");
    }
    let frames=(0..parts as usize).map(|i|unsafe{
        let row=transforms.add(i*7);
        Transform{translation:v3(row),rotation:Quat::from_xyzw(*row.add(3),*row.add(4),*row.add(5),*row.add(6)),..Default::default()}
    }).collect();
    match a.agent.submit_animation_pose(frames,mask,weight) {Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_cancel_animation_pose(a:*mut NmAgent)->i32 {
    agent!(a).agent.cancel_animation_pose();0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_animation_pose_status(a:*const NmAgent)->i32 {
    agent_ref!(a).agent.animation_pose_status()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    #[test]
    fn incoming_motion_uses_original_start_once_and_rejects_incomplete_input() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path.as_ptr()),0|1));
            for dt in [1.0/120.0,1.0/60.0,1.0/30.0] { for speed in [0.0,2.0,12.0] {
                let v=Vec3::new(0.0,speed,0.0);
                let prev=[0.0,0.0,1.0,0.0,0.0,0.0,1.0];
                let q=Quat::from_rotation_x(4.0*dt);
                let now=[0.0,speed*dt,1.0+dt,q.x,q.y,q.z,q.w];let mut out=[0.0;6];
                assert_eq!(nm_animation_start_motion(now.as_ptr(),prev.as_ptr(),1,dt,v.to_array().as_ptr(),out.as_mut_ptr()),1);
                let m=Mat3::from_quat(q);
                let (_,expected,spin)=delirium::ragstart::start_part([m.x_axis,m.y_axis,m.z_axis,Vec3::new(0.0,speed*dt,1.0+dt)],Some(&[Vec3::X,Vec3::Y,Vec3::Z,Vec3::Z]),Vec3::ZERO,dt,[0.5,3.0,2.0],v);
                assert!((v3(out.as_ptr())-expected).length()<1e-5);
                assert!((v3(out.as_ptr().add(3))-spin).length()<1e-5);
                assert!((v3(out.as_ptr())-v).length()<=3.0001);
                assert!(v3(out.as_ptr().add(3)).length()<=2.0001);
                assert!(out[2]>0.0,"the rising limb keeps its measured motion");
                let saved=out;
                assert_eq!(nm_animation_start_motion(now.as_ptr(),prev.as_ptr(),1,0.0,v.to_array().as_ptr(),out.as_mut_ptr()),NM_E_ARG);
                assert_eq!(out,saved);
            }}
            let now=[0.0,0.0,0.0,0.0,0.0,0.0,1.0,f32::NAN,0.0,0.0,0.0,0.0,0.0,1.0];
            let mut out=[123.0;12];
            assert_eq!(nm_animation_start_motion(now.as_ptr(),now.as_ptr(),2,0.01,[0.0;3].as_ptr(),out.as_mut_ptr()),NM_E_ARG);
            assert_eq!(out,[123.0;12]);
        }
    }
    #[test]
    fn full_reaction_drives_legs_without_teleporting_or_setting_root_velocity() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|n:&str|CString::new(root.join(n).to_str().unwrap()).unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
            let build=nm_build_load(path("fred-build.txt").as_ptr());assert!(!build.is_null());
            let world=nm_world_create();nm_world_add_floor(world,0.,0.,0.,20.);
            let full=nm_agent_create(build,[0.,0.,0.].as_ptr(),0.,0.4);
            let upper=nm_agent_create(build,[0.,0.,0.].as_ptr(),0.,0.4);
            let frames=|a|{let mut t=[0.;147];assert_eq!(nm_agent_get_part_transforms(a,t.as_mut_ptr(),21),21);t};
            for a in [full,upper] {
                assert_eq!(nm_agent_task_balance(a,world,c"DEFAULT".as_ptr(),[0.,2.,1.5].as_ptr()),0);
                assert_eq!(nm_agent_step(a,world,1./60.),0);
            }
            let mut pose=frames(full);
            // Bend a leg target while both agents otherwise have identical
            // input. The old upper-only bridge silently discarded it.
            for part in [2,3] {
                let j=part*7+3;let q=Quat::from_xyzw(pose[j],pose[j+1],pose[j+2],pose[j+3]);
                pose[j..j+4].copy_from_slice(&(Quat::from_rotation_x(0.45)*q).to_array());
            }
            for _ in 0..6 {
                for (a,mask) in [(full,c"fb"),(upper,c"ub")] {
                    let before=frames(a);let energy=nm_agent_get_kinetic_energy(a);
                    assert_eq!(nm_agent_submit_animation_pose(a,pose.as_ptr(),21,mask.as_ptr(),1.),0);
                    assert_eq!(frames(a),before,"submission must never pose a body");
                    assert_eq!(nm_agent_get_kinetic_energy(a),energy,"submission must not inject motion");
                    assert_eq!(nm_agent_step(a,world,1./60.),0);
                    assert_eq!(nm_agent_animation_pose_status(a),1);
                    assert!(frames(a).iter().all(|v|v.is_finite()));
                }
            }
            let f=frames(full);let u=frames(upper);
            let leg_delta:f32=f[7..28].iter().zip(&u[7..28]).map(|(a,b)|(a-b).abs()).sum();
            assert!(leg_delta>0.001,"full reaction lost the authored leg articulation");
            assert_ne!(f,pose,"joint targets cannot replace the solved physical pose");
            nm_agent_task_catch_fall(full,c"*".as_ptr());
            nm_agent_submit_animation_pose(full,pose.as_ptr(),21,c"fb".as_ptr(),1.);
            nm_agent_step(full,world,1./60.);
            assert_eq!(nm_agent_animation_pose_status(full),2,"fall must retain ownership");
            for a in [full,upper]{nm_agent_destroy(a)}nm_world_destroy(world);nm_build_destroy(build);
        }
    }
    #[test]
    fn animation_is_finite_motor_input_not_a_body_pin_or_stale_task() {
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=|n:&str|CString::new(root.join(n).to_str().unwrap()).unwrap();
        let mask=CString::new("ub").unwrap();let default=CString::new("DEFAULT").unwrap();let all=CString::new("*").unwrap();
        unsafe {
            assert!(matches!(nm_init(path("fred.xrt").as_ptr()),0|1));
            assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
            let build=nm_build_load(path("fred-build.txt").as_ptr());assert!(!build.is_null());
            let world=nm_world_create();nm_world_add_floor(world,0.,0.,0.,20.);
            let a=nm_agent_create(build,[0.,0.,0.].as_ptr(),0.,0.4);assert!(!a.is_null());
            assert_eq!(nm_agent_task_balance(a,world,default.as_ptr(),[0.,2.,1.5].as_ptr()),0);
            // First tick creates the solver's retained native drive.
            assert_eq!(nm_agent_step(a,world,1./60.),0);
            let frames=|a|{let mut t=[0.;147];assert_eq!(nm_agent_get_part_transforms(a,t.as_mut_ptr(),21),21);t};
            let before=frames(a);let mut authored=before;
            let q=Quat::from_xyzw(authored[12*7+3],authored[12*7+4],authored[12*7+5],authored[12*7+6]);
            authored[12*7+3..12*7+7].copy_from_slice(&(Quat::from_rotation_z(0.35)*q).to_array());
            let energy=nm_agent_get_kinetic_energy(a);
            assert_eq!(nm_agent_submit_animation_pose(a,authored.as_ptr(),21,mask.as_ptr(),1.),0);
            assert_eq!(frames(a),before);assert_eq!(nm_agent_get_kinetic_energy(a),energy);
            assert_eq!(nm_agent_step(a,world,1./60.),0);
            assert_eq!(nm_agent_animation_pose_status(a),1,"balanced pose must reach motor targets");
            assert_ne!(frames(a),authored,"physical body must not be set to the animation pose");
            assert_eq!(nm_agent_step(a,world,1./60.),0);
            assert_eq!(nm_agent_animation_pose_status(a),0,"unsupplied pose must expire");
            // An external impulse remains a solver input while the animation is
            // leased. Submitting targets cannot consume or overwrite it.
            let impulse=[12.,0.,0.];let head=&frames(a)[12*7..12*7+3];
            nm_agent_apply_impulse(a,12,head.as_ptr(),impulse.as_ptr());
            let point=Vec3::from_slice(head);
            let predicted=(*a).agent.doll.predicted_contact_velocity(&(*a).build,&[],12,point);
            assert!((predicted-(*a).agent.doll.vel(12).0).length()>0.01,"fixture must contain pending momentum");
            nm_agent_submit_animation_pose(a,authored.as_ptr(),21,mask.as_ptr(),1.);
            assert_eq!((*a).agent.doll.predicted_contact_velocity(&(*a).build,&[],12,point),predicted);
            nm_agent_task_catch_fall(a,all.as_ptr());
            nm_agent_submit_animation_pose(a,authored.as_ptr(),21,mask.as_ptr(),1.);
            nm_agent_step(a,world,1./60.);
            assert_eq!(nm_agent_animation_pose_status(a),2,"fall must take over from authored pose");
            let old=frames(a);
            let invalid=CString::new("root_pin").unwrap();
            assert_eq!(nm_agent_submit_animation_pose(a,authored.as_ptr(),21,invalid.as_ptr(),1.),NM_E_ARG);
            assert_eq!(nm_agent_submit_animation_pose(a,authored.as_ptr(),20,mask.as_ptr(),1.),NM_E_ARG);
            assert_eq!(nm_agent_submit_animation_pose(a,authored.as_ptr(),21,mask.as_ptr(),f32::NAN),NM_E_ARG);
            authored[3]=f32::INFINITY;
            assert_eq!(nm_agent_submit_animation_pose(a,authored.as_ptr(),21,mask.as_ptr(),1.),NM_E_ARG);
            assert_eq!(frames(a),old);
            nm_agent_step(a,world,1./60.);assert_eq!(nm_agent_animation_pose_status(a),0);
            nm_agent_destroy(a);nm_world_destroy(world);nm_build_destroy(build);
        }
    }
}
