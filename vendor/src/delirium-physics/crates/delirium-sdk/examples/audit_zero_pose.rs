// Check the host API's critical contract: changing motor rest targets must not
// teleport, pin, alter velocity, or inject energy into the physical body.
use delirium_physics::*;
use std::ffi::CString;
fn main() {
    let root=std::path::PathBuf::from(std::env::args().nth(1).expect("asset directory"));
    let xrt=CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
    let build_path=CString::new(root.join("fred-build.txt").to_str().unwrap()).unwrap();
    unsafe {
        assert_eq!(nm_init(xrt.as_ptr()),0);
        let build=nm_build_load(build_path.as_ptr());assert!(!build.is_null());
        let at=[0.,0.,0.];let agent=nm_agent_create(build,at.as_ptr(),0.,0.4);assert!(!agent.is_null());
        let n=nm_agent_part_count(agent);assert_eq!(n,21);
        let mut before=vec![0.;n as usize*7];assert_eq!(nm_agent_get_part_transforms(agent,before.as_mut_ptr(),n),n);
        let mut neutral=before.clone();
        // Different wrist rest rotations are valid without moving its body.
        for k in [16,20] {neutral[k*7+3..k*7+7].copy_from_slice(&[0.,0.,0.,1.]);}
        let dv=[0.3,-0.7,0.2];assert_eq!(nm_agent_add_velocity(agent,dv.as_ptr()),0);
        let energy=nm_agent_get_kinetic_energy(agent);
        assert_eq!(nm_agent_set_zero_transforms(agent,neutral.as_ptr(),n),0);
        let mut after=before.clone();assert_eq!(nm_agent_get_part_transforms(agent,after.as_mut_ptr(),n),n);
        assert_eq!(after,before,"rest pose moved body");
        assert_eq!(nm_agent_get_kinetic_energy(agent),energy,"rest pose changed energy");
        assert_eq!(nm_agent_set_zero_transforms(agent,std::ptr::null(),n),NM_E_ARG);
        assert_eq!(nm_agent_set_zero_transforms(agent,neutral.as_ptr(),n-1),NM_E_ARG);
        neutral[3]=f32::NAN;assert_eq!(nm_agent_set_zero_transforms(agent,neutral.as_ptr(),n),NM_E_ARG);
        assert_eq!(nm_agent_get_part_transforms(agent,after.as_mut_ptr(),n),n);assert_eq!(after,before);
        nm_agent_destroy(agent);nm_build_destroy(build);
    }
    println!("PASS: animated rest targets preserve body transforms and kinetic energy; malformed inputs rejected");
}
