use delirium_physics::*;
use std::{ptr,ffi::CString};
fn main(){unsafe{
    let root=std::path::PathBuf::from(std::env::args().nth(1).unwrap());
    let xrt=CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();assert!(nm_init(xrt.as_ptr())>=0);
    // Two horizontal bonnet triangles and a lower bumper box, not a roof-high slab.
    let geometry=[0.,-1.,-2.,0.8,1.,-2.,0.8,1.,2.,0.8,
                  0.,-1.,-2.,0.8,1.,2.,0.8,-1.,2.,0.8,
                  1.,0.,0.,0.4,1.,2.,0.2,0.,0.,0.];
    let id=nm_vehicle_shape_register(geometry.as_ptr(),3);assert!(id>=0);
    for _ in 0..1000{assert_eq!(nm_vehicle_shape_register(geometry.as_ptr(),3),id);}
    assert_eq!(nm_vehicle_shape_register(ptr::null(),3),NM_E_ARG);
    let mut bad=geometry;bad[5]=f32::NAN;assert_eq!(nm_vehicle_shape_register(bad.as_ptr(),3),NM_E_ARG);
    let text=CString::new(std::fs::read(root.join("fred-build.txt")).unwrap()).unwrap();let build=nm_build_from_text(text.as_ptr());
    let w=nm_world_create();let row=[0.,0.,0.,0.,0.,0.,1.,1.,2.,1.5,0.,0.,0.];
    assert_eq!(nm_world_set_boxes(w,row.as_ptr(),1),0);
    assert_eq!(nm_world_set_vehicle_shape(w,0,id,[0.,0.,0.].as_ptr()),0);
    assert_eq!(nm_world_set_vehicle_shape(w,1,id,[0.,0.,0.].as_ptr()),NM_E_ARG);
    let a=nm_agent_create(build,[0.,0.,0.85].as_ptr(),0.,0.3);assert!(!a.is_null());
    for _ in 0..60{assert_eq!(nm_agent_step(a,w,1./60.),0);}
    let mut p=[0.;3];let mut v=[0.;3];assert_eq!(nm_agent_get_com(a,p.as_mut_ptr(),v.as_mut_ptr()),0);
    assert!(p.iter().chain(v.iter()).all(|v|v.is_finite()));assert!(p[2]<2.5,"false roof or launch {p:?}");
    nm_agent_destroy(a);nm_world_destroy(w);nm_build_destroy(build);
    println!("VEHICLE_SHAPES passed deduplicated=1000 invalid_input_rejected=true com={p:?} velocity={v:?}");
}}
