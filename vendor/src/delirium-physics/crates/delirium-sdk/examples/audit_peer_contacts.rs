use delirium_physics::*;
use std::{ffi::CString,ptr};
// Same initialized body, same gravity, with and without a nearby peer capsule.
// Comparing their poses catches a disconnected SDK-to-solver peer list.
fn main(){unsafe{
    let root=std::path::PathBuf::from(std::env::args().nth(1).unwrap());
    let xrt=CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
    assert!(nm_init(xrt.as_ptr())>=0);
    let text=CString::new(std::fs::read(root.join("fred-build.txt")).unwrap()).unwrap();
    let build=nm_build_from_text(text.as_ptr());assert!(!build.is_null());
    let origin=[0.,0.,0.];let plain=nm_agent_create(build,origin.as_ptr(),0.,0.3);
    let touched=nm_agent_create(build,origin.as_ptr(),0.,0.3);
    let empty=nm_world_create();let peer=nm_world_create();
    let mut p=[0.;3];let mut q=[0.;4];
    assert_eq!(nm_agent_get_part_transform(touched,10,p.as_mut_ptr(),q.as_mut_ptr()),0);
    let row=[p[0]+0.13,p[1],p[2]-0.4,p[0]+0.13,p[1],p[2]+0.4,0.20,0.,0.,0.,0.];
    assert_eq!(nm_world_set_capsules(peer,row.as_ptr(),1),0);
    // Reject malformed arrays without erasing the valid contact.
    assert_eq!(nm_world_set_capsules(peer,ptr::null(),1),NM_E_ARG);
    assert_eq!(nm_world_set_capsules(peer,row.as_ptr(),513),NM_E_ARG);
    let mut bad=row;bad[6]=f32::NAN;
    assert_eq!(nm_world_set_capsules(peer,bad.as_ptr(),1),NM_E_ARG);
    for _ in 0..6{assert_eq!(nm_agent_step(plain,empty,1./60.),0);assert_eq!(nm_agent_step(touched,peer,1./60.),0);}
    let mut max_delta=0f32;
    for i in 0..21{
        let mut a=[0.;3];let mut b=[0.;3];
        nm_agent_get_part_transform(plain,i,a.as_mut_ptr(),q.as_mut_ptr());
        nm_agent_get_part_transform(touched,i,b.as_mut_ptr(),q.as_mut_ptr());
        assert!(a.iter().chain(b.iter()).all(|v|v.is_finite()));
        max_delta=max_delta.max(a.iter().zip(b).map(|(x,y)|(x-y).powi(2)).sum::<f32>().sqrt());
    }
    assert!(max_delta>0.01,"peer capsule did not affect the solver: {max_delta}");
    assert!(max_delta<1.0,"peer contact caused an explosive displacement: {max_delta}");
    assert_eq!(nm_world_set_capsules(peer,ptr::null(),0),0);
    println!("PEER_CONTACTS passed max_body_response_m={max_delta:.6} malformed_input_rejected=true");
    nm_agent_destroy(plain);nm_agent_destroy(touched);nm_world_destroy(empty);nm_world_destroy(peer);nm_build_destroy(build);
}}
