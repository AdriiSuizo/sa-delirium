use delirium_physics::*;
use delirium::prelude::*;
use delirium::ragdoll::{Build, Ragdoll, Limit, Obstacle, Drive};
use std::ffi::CString;
fn main() {
    let root=std::path::PathBuf::from(std::env::args().nth(1).expect("asset directory"));
    let xrt=CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
    let path=root.join("fred-build.txt");
    unsafe { assert_eq!(nm_init(xrt.as_ptr()),0); }
    let (model,_)=Build::undump(&std::fs::read_to_string(&path).unwrap());
    // Analytic free rigid body: 1/m + (r x n)^T I^-1 (r x n).
    let mut part=model.parts[0].clone();
    part.parent=None;part.mass=4.;part.inertia=Vec3::splat(2.);part.offset=Transform::IDENTITY;
    part.bind=Quat::IDENTITY;part.rest=Quat::IDENTITY;part.anchor=Vec3::ZERO;part.anchor_parent=Vec3::ZERO;
    let build=Build{parts:vec![part.clone()],nm:model.nm};
    // Actual solver, not an implementation-mirroring target test: two bodies
    // translating together must not generate a normal impact. Also compare
    // the same closing speed in the stationary and translated frames.
    let run_contact=|common:f32,relative:f32| {
        let mut sphere=build.clone();sphere.parts[0].radius=0.1;sphere.parts[0].half=0.;
        let at=Vec3::new(0.,0.,3.);
        let mut doll=Ragdoll::new(&sphere,&[Transform::from_translation(at)],Vec3::X*(common+relative));
        // Obstacles are supplied at the end of the host's interval; begin()
        // predicts the ragdoll to that same instant before contact generation.
        let center=at-Vec3::X*0.59+Vec3::X*(common/60.);
        let car=Obstacle{id:0,at:center,rot:Quat::IDENTITY,centre:center,half:Vec3::new(0.5,2.,2.),vel:Vec3::X*common,com:center,..Default::default()};
        let drive=Drive{muscles:&[],lift:0.,accel:Vec3::ZERO,rigid:&[],torque:&[],force:&[]};
        doll.step(&sphere,&delirium::collision::Collision::default(),1./60.,drive,&[car],&[]);
        doll.com().1.x-common
    };
    let stationary=run_contact(0.,0.);let moving=run_contact(4.,0.);
    println!("co-moving normal relative speed stationary={stationary} moving={moving}");
    assert!((moving-stationary).abs()<0.03,"Co-moving contact creates a kick");
    let s=run_contact(0.,-2.);let m=run_contact(4.,-2.);
    println!("approach normal relative speed stationary={s} moving={m}");
    assert!((s-m).abs()<0.03,"Moving contact violates frame invariance");
    for origin in [Vec3::ZERO,Vec3::new(2430.,-1658.,13.)] {
        let mut doll=Ragdoll::new(&build,&[Transform::from_translation(origin)],Vec3::ZERO);
        assert!((doll.contact_inverse_mass(&build,&[],0,origin,Vec3::X)-0.25).abs()<1e-5);
        assert!((doll.contact_inverse_mass(&build,&[],0,origin+Vec3::Y,Vec3::X)-0.75).abs()<1e-5);
        doll.impulse(0, origin+Vec3::Y, Vec3::X*2.);
        let predicted=doll.predicted_contact_velocity(&build,&[],0,origin+Vec3::Y);
        assert!((predicted.x-1.5).abs()<1e-5,"off-centre pending velocity {predicted:?}");
        assert_eq!(doll.predicted_contact_velocity(&build,&[],0,origin+Vec3::Y),predicted);
        assert_eq!(doll.vel(0),(Vec3::ZERO,Vec3::ZERO));
    }
    // Two joined masses, force through the joint axis: the hand is not free.
    let mut child=part.clone();child.parent=Some(0);child.anchor_parent=Vec3::Y;child.limit=Limit::Locked;
    let linked=Build{parts:vec![part,child],nm:model.nm};
    let doll=Ragdoll::new(&linked,&[Transform::IDENTITY,Transform::from_translation(Vec3::Y)],Vec3::ZERO);
    let inverse=doll.contact_inverse_mass(&linked,&[],1,Vec3::Y,Vec3::Y);
    assert!((inverse-0.125).abs()<1e-4,"linked inverse mass {inverse}");
    unsafe {
        let path=CString::new(path.to_str().unwrap()).unwrap();let build=nm_build_load(path.as_ptr());
        let zero=[0.,0.,0.];let agent=nm_agent_create(build,zero.as_ptr(),0.,0.4);assert!(!agent.is_null());
        let mut before=[0.;147];nm_agent_get_part_transforms(agent,before.as_mut_ptr(),21);
        let energy=nm_agent_get_kinetic_energy(agent);
        for k in 0..21 {
            let p=&before[k as usize*7..];
            for normal in [[1.,0.,0.],[0.,1.,0.],[0.,0.,1.]] {
                let mut inverse=0.;assert_eq!(nm_agent_get_contact_inverse_mass(agent,k,p.as_ptr(),normal.as_ptr(),&mut inverse),0);
                assert!(inverse.is_finite()&&inverse>0.);
            }
        }
        let mut after=[0.;147];nm_agent_get_part_transforms(agent,after.as_mut_ptr(),21);
        assert_eq!(after,before);assert_eq!(nm_agent_get_kinetic_energy(agent),energy);
        let mut value=123.;
        assert_eq!(nm_agent_get_contact_inverse_mass(agent,0,zero.as_ptr(),zero.as_ptr(),&mut value),NM_E_ARG);
        assert_eq!(value,123.);
        assert_eq!(nm_agent_get_contact_inverse_mass(agent,-1,zero.as_ptr(),zero.as_ptr(),&mut value),NM_E_ARG);
        let mut velocity=[0.;3];
        assert_eq!(nm_agent_get_predicted_contact_velocity(agent,-1,zero.as_ptr(),velocity.as_mut_ptr()),NM_E_ARG);
        assert_eq!(nm_agent_get_predicted_contact_velocity(agent,0,[f32::NAN,0.,0.].as_ptr(),velocity.as_mut_ptr()),NM_E_ARG);
        assert_eq!(nm_agent_take_obstacle_impulses(agent,std::ptr::null_mut(),std::ptr::null_mut(),0),0);
        assert_eq!(nm_agent_take_obstacle_impulses(agent,std::ptr::null_mut(),std::ptr::null_mut(),1),NM_E_ARG);
        nm_agent_destroy(agent);nm_build_destroy(build);
    }
    println!("PASS: free/off-centre analytic mass, joined mass, translation invariance, finite SDK queries and no pose/energy mutation");
}
