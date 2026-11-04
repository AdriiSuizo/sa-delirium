//! Actual six-character, 21-part simulation; emits every output float's bits.
//! Emits bit-exact trajectories for compiler/scheduler comparisons in separate processes.
use delirium_physics::*;
use std::{ffi::CString,path::Path,time::Instant};
fn main(){unsafe{
 let args:Vec<_>=std::env::args_os().collect();assert_eq!(args.len(),3);
 let root=Path::new(&args[1]);let c=|s:&str|CString::new(s).unwrap();
 assert!(nm_init(c(root.join("fred.xrt").to_str().unwrap()).as_ptr())>=0);
 let build=nm_build_load(c(root.join("fred-build.txt").to_str().unwrap()).as_ptr());assert!(!build.is_null());
 let world=nm_world_create();assert!(nm_world_add_floor(world,0.,0.,0.,50.)>=0);
 let group=nm_group_create();let mut agents=vec![];
 for i in 0..6{
  let at=[(i%3)as f32*0.55,(i/3)as f32*0.55,0.];
  let a=nm_agent_create(build,at.as_ptr(),0.,0.3);assert!(!a.is_null());
  assert_eq!(nm_msg_begin(a,c("bodyBalance").as_ptr()),0);
  assert_eq!(nm_msg_set_bool(a,c("start").as_ptr(),1),0);assert!(nm_msg_send(a,world)>=0);agents.push(a);
 }
 let mut times=vec![];let mut frames=vec![];let mut max_pairs=0f32;
 for frame in 0..200{
  if frame==30{let mut p=[0.;3];nm_agent_get_part_transform(agents[0],10,p.as_mut_ptr(),std::ptr::null_mut());
   assert_eq!(nm_agent_apply_impulse(agents[0],10,p.as_ptr(),[112.5,0.,0.].as_ptr()),0);}
  assert_eq!(nm_group_begin(group),0);
  for(i,&a)in agents.iter().enumerate(){assert_eq!(nm_group_add(group,a,(i as u32+1)*256+1,world),0);}
  let start=Instant::now();assert_eq!(nm_group_step(group,world,1./120.),0);
  if frame>=10{times.push(start.elapsed().as_secs_f64()*1000.);}
  let mut contacts=[0.;4];assert_eq!(nm_group_contacts(group,contacts.as_mut_ptr()),0);max_pairs=max_pairs.max(contacts[0]);
  let mut bits=vec![];
  for &a in &agents{
   let n=nm_agent_part_count(a);assert_eq!(n,21);let mut poses=vec![0.;n as usize*7];
   assert!(nm_agent_get_part_transforms(a,poses.as_mut_ptr(),n)>=0);bits.extend(poses.into_iter().map(f32::to_bits));
   for p in 0..n{let(mut v,mut w)=([0.;3],[0.;3]);assert_eq!(nm_agent_get_part_velocity(a,p,v.as_mut_ptr(),w.as_mut_ptr()),0);
    bits.extend(v.into_iter().chain(w).map(f32::to_bits));}
  }frames.push(bits);
 }
 assert!(max_pairs>0.,"must test real physical peer contacts");times.sort_by(f64::total_cmp);
 let report=serde_json::json!({"cpu_avx2":std::is_x86_feature_detected!("avx2"),"cpu_fma":std::is_x86_feature_detected!("fma"),"bits":frames,"max_peer_pairs":max_pairs,"timing_ms":{"median":times[times.len()/2],"p95":times[times.len()*95/100],"max":times[times.len()-1]}});
 std::fs::write(&args[2],serde_json::to_vec(&report).unwrap()).unwrap();
 println!("{}",report["timing_ms"]);
 nm_group_destroy(group);for a in agents{nm_agent_destroy(a);}nm_world_destroy(world);nm_build_destroy(build);
}}
