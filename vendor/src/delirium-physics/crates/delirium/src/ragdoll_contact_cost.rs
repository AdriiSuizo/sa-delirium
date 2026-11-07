//! Explicit dense-contact CPU observation, no timers/writes in normal play.
use std::{path::PathBuf,sync::{OnceLock,Mutex},time::Instant};
static PATH:OnceLock<Option<PathBuf>>=OnceLock::new();
static WRITE:Mutex<()>=Mutex::new(());
// Per-frame sums for the physics breakdown: bodies, 5 phases, worst body, total, components, manifolds, hits, blocks.
static TOTALS:Mutex<[f64;12]>=Mutex::new([0.;12]);
pub(super)fn take()->[f64;12]{std::mem::take(&mut *TOTALS.lock().unwrap_or_else(|e|e.into_inner()))}
// GTASA_CONTACT_COST_MIN_MS lowers the 8 ms reporting floor (profiling ordinary steps).
fn min_ms()->f64{static MIN:OnceLock<f64>=OnceLock::new();*MIN.get_or_init(||std::env::var("GTASA_CONTACT_COST_MIN_MS").ok().and_then(|v|v.parse().ok()).unwrap_or(8.))}
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
  let Some(t)=self.start else{return};let ms=t.elapsed().as_secs_f64()*1000.;
  {let mut s=TOTALS.lock().unwrap_or_else(|e|e.into_inner());s[0]+=1.;for k in 0..5{s[1+k]+=self.ms[k];}s[6]=s[6].max(ms);s[7]+=ms;
   s[8]+=self.components as f64;s[9]+=self.manifolds as f64;s[10]+=hits as f64;s[11]+=blocks as f64;}
  if ms<min_ms(){return}
  let _lock=WRITE.lock().unwrap_or_else(|e|e.into_inner());
  if let Ok(mut f)=std::fs::OpenOptions::new().create(true).append(true).open(PATH.get().unwrap().as_ref().unwrap()){
   use std::io::Write;let _=writeln!(f,"{{\"pid\":{},\"ms\":{},\"phase_ms\":{:?},\"phase_cpu_ns\":{:?},\"components\":{},\"manifold_calls\":{},\"hits\":{},\"blocks\":{},\"held_slots\":{}}}",std::process::id(),ms,self.ms,self.cpu,self.components,self.manifolds,hits,blocks,slots);
  }
 }
}
