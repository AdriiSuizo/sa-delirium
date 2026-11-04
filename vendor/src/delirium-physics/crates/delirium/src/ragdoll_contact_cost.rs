//! Explicit dense-contact CPU observation, no timers/writes in normal play.
use std::{path::PathBuf,sync::{OnceLock,Mutex},time::Instant};
static PATH:OnceLock<Option<PathBuf>>=OnceLock::new();
static WRITE:Mutex<()>=Mutex::new(());
#[cfg(windows)]
fn cpu_ns()->u64{
 #[link(name="kernel32")] unsafe extern "system"{fn GetThreadTimes(h:*mut std::ffi::c_void,c:*mut u64,e:*mut u64,k:*mut u64,u:*mut u64)->i32;}
 let(mut c,mut e,mut k,mut u)=(0,0,0,0);unsafe{GetThreadTimes((-2isize)as *mut _,&mut c,&mut e,&mut k,&mut u)};(k+u)*100
}
#[cfg(not(windows))]fn cpu_ns()->u64{0}
pub(super)struct Cost{start:Option<Instant>,ms:[f64;5],cpu:[u64;5],components:usize,manifolds:usize}
impl Cost{
 pub fn new(components:usize)->Self{Self{start:PATH.get_or_init(||std::env::var_os("GTASA_CONTACT_COST_PROFILE").map(PathBuf::from)).as_ref().map(|_|Instant::now()),ms:[0.;5],cpu:[0;5],components,manifolds:0}}
 pub fn timer(&self)->Option<(Instant,u64)>{self.start.map(|_|(Instant::now(),cpu_ns()))}
 pub fn add(&mut self,k:usize,t:Option<(Instant,u64)>){if let Some((t,cpu))=t{self.ms[k]+=t.elapsed().as_secs_f64()*1000.;self.cpu[k]+=cpu_ns().saturating_sub(cpu);}}
 pub fn called_manifold(&mut self){if self.start.is_some(){self.manifolds+=1;}}
 pub fn finish(self,hits:usize,blocks:usize,slots:usize){
  let Some(t)=self.start else{return};let ms=t.elapsed().as_secs_f64()*1000.;if ms<8.{return}
  let _lock=WRITE.lock().unwrap_or_else(|e|e.into_inner());
  if let Ok(mut f)=std::fs::OpenOptions::new().create(true).append(true).open(PATH.get().unwrap().as_ref().unwrap()){
   use std::io::Write;let _=writeln!(f,"{{\"pid\":{},\"ms\":{},\"phase_ms\":{:?},\"phase_cpu_ns\":{:?},\"components\":{},\"manifold_calls\":{},\"hits\":{},\"blocks\":{},\"held_slots\":{}}}",std::process::id(),ms,self.ms,self.cpu,self.components,self.manifolds,hits,blocks,slots);
  }
 }
}
