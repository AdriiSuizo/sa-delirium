//! Read the actual source schedule after loading the user's own NM assets.
use delirium_physics::*;
fn main(){unsafe{
 let path=std::env::args().nth(1).expect("fred.xrt path");
 let path=std::ffi::CString::new(path).unwrap();assert!(nm_init(path.as_ptr())>=0);
 let rate:f32=delirium_data::kv!(80);let cap:f32=delirium_data::kv!(61);
 println!("rate={rate} multiplier={} cap={cap}",delirium::ragdoll::SUBSTEPS);
 for dt in [1./120.,0.012073901,1./60.,1./30.]{
  let n=(dt*rate*delirium::ragdoll::SUBSTEPS as f32).round().clamp(1.,cap) as usize;
  println!("dt={dt} internal_steps={n}");
 }
}}
