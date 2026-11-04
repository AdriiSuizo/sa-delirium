use delirium_physics::*;
use std::ffi::CString;
unsafe extern "C" {fn nm_agent_melee_task(a:*mut NmAgent,w:*const NmWorld,threat:*const f32,health:f32,agility:i32,flags:u32)->i32;}
fn main(){unsafe{
 let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
 let path=|s:&str|CString::new(root.join(s).to_str().unwrap()).unwrap();
 assert!(nm_init(path("fred.xrt").as_ptr())>=0);assert_eq!(nm_load_task_params(path("taskparams.txt").as_ptr()),0);
 let b=nm_build_load(path("fred-build.txt").as_ptr());assert!(!b.is_null());
 let w=nm_world_create();assert_eq!(nm_world_add_floor(w,0.,0.,0.,20.),0);
 for hz in [60.,120.]{for part in [0,12,15]{for impulse in [7.6,112.5]{
  let dt=1./hz;let a=nm_agent_create(b,[0.,0.,0.].as_ptr(),0.,0.4);
  assert_eq!(nm_agent_melee_task(a,w,[0.,-1.,1.7].as_ptr(),200.,3,0),0);
  let mut start=[0.;3];let mut vel=[0.;3];nm_agent_get_com(a,start.as_mut_ptr(),vel.as_mut_ptr());
  let(mut peak,mut low,mut delivered,mut first_up)=(0.0f32,100.0f32,0.0f32,0.0f32);
  for k in 0..(hz*3.) as usize{
   if (k as f32)*dt<0.15-1e-6{
    let h=dt.min(0.15-(k as f32)*dt);let mut at=[0.;3];nm_agent_get_part_transform(a,part,at.as_mut_ptr(),std::ptr::null_mut());
    let j=impulse*h/0.15;assert_eq!(nm_agent_apply_impulse(a,part,at.as_ptr(),[0.,j,0.].as_ptr()),0);delivered+=j;
   }
   assert_eq!(nm_agent_step(a,w,dt),0);let mut at=[0.;3];nm_agent_get_com(a,at.as_mut_ptr(),vel.as_mut_ptr());
   if k==(hz as usize){let(mut pelvis,mut chest)=([0.;3],[0.;3]);nm_agent_get_part_transform(a,0,pelvis.as_mut_ptr(),std::ptr::null_mut());nm_agent_get_part_transform(a,10,chest.as_mut_ptr(),std::ptr::null_mut());let d:[f32;3]=std::array::from_fn(|i|chest[i]-pelvis[i]);first_up=d[2]/(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]).sqrt();}
   peak=peak.max((vel[0]*vel[0]+vel[1]*vel[1]).sqrt());low=low.min(at[2]);
  }
  let mut at=[0.;3];nm_agent_get_com(a,at.as_mut_ptr(),vel.as_mut_ptr());
  assert!((delivered-impulse).abs()<0.001);
  println!("{{\"hz\":{hz},\"part\":{part},\"impulse_ns\":{impulse},\"fallen\":{},\"torso_up_at_1s\":{first_up},\"lowest_com_z\":{low},\"peak_xy_mps\":{peak},\"travel_y\":{}}}",nm_agent_has_fallen(a),at[1]-start[1]);
  nm_agent_destroy(a);
 }}}
 nm_world_destroy(w);nm_build_destroy(b);
}}
