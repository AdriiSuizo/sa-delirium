//! Original TU8 event queue behind an explicit semantic host boundary.
//!
//! Queue/add/handle are the checked original implementation, not SA priorities.
//! The host still owns source response construction and source task abort/give.
//! Missing mappings must fail PREFLIGHT before any response is constructed.
//! This bridge supports main/secondary responses, not handler's third response.
//! No SA accounting event is consumed here and no native hook is installed.
use sa_combat::original_event_queue as q;
use std::{cell::{Cell, UnsafeCell}, ffi::c_void};

pub const NM_EV_OK:i32=1;
pub const NM_EV_ARG:i32=-1;
pub const NM_EV_IDENTITY:i32=-2;
pub const NM_EV_BUSY:i32=-3;
pub const NM_EV_UNMAPPED:i32=-4;
pub const NM_EV_POISONED:i32=-5;
pub const NM_EV_REPLAY:i32=-6;
const VERSION:u32=1;

#[repr(C)]
#[derive(Clone,Copy,Debug,Default,PartialEq)]
pub struct NmOriginalEventInput {
    pub size:u32,pub kind:u32,
    pub receipt:u64,pub source_generation:u64,pub source_birth:u64,
    pub priority:i32,pub lifetime:i32,
    /// temporary1, stacks2, keep4, replace8. All values are ORIGINAL semantics.
    pub flags:u32,
    /// ped1, network-refused2, decision4, trials8/16, byteC32,
    /// applied-damage-bit38-7 64, blocking128, concerns256.
    pub admission:u32,pub decision_response:i32,
}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct NmOriginalEventAdded {pub stored:u32,pub group:u32,pub friends:u32,pub vehicle:u32,pub heard:u32}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct NmOriginalEventRequest {
    /// 0 preflight,1 ready,2 valid,3 concerns,4 overrides,5 respond,
    /// 6 abort,7 ragdoll-retry,8 give,9 clear. No native SA IDs.
    pub operation:u32,pub kind:u32,pub receipt:u64,
    pub generation:u64,pub birth:u64,
    pub source_generation:u64,pub source_birth:u64,
    pub age:i32,pub priority:i32,pub flags:u32,
    pub record_kind:u32,pub record_receipt:u64,
    pub record_source_generation:u64,pub record_source_birth:u64,
    pub record_priority:i32,pub task:u32,pub urgency:u32,
    /// Currently built response: main1,secondary2,slot2=4.
    pub built:u32,
}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct NmOriginalEventReply {
    pub answer:u32,
    /// ready/abort may mutate age, as original slots do. Initialize from request.
    pub age:i32,
    /// respond: main1,secondary2,slot2=4. abort: drop-main8.
    pub flags:u32,
}
pub type OriginalEventCall=unsafe extern "C" fn(*mut c_void,*const NmOriginalEventRequest,*mut NmOriginalEventReply)->i32;
#[repr(C)]
#[derive(Clone,Copy)]
pub struct NmOriginalEventHost {pub size:u32,pub version:u32,pub context:*mut c_void,pub call:Option<OriginalEventCall>}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct NmOriginalEventSnapshot {
    pub queued:u32,pub poisoned:u32,pub last_receipt:u64,
    /// nontemp,temp,pending,other. Keep these payloads alive in the host.
    pub records:[u64;4],pub note:u32,pub slot2:u32,
}
impl q::Record for NmOriginalEventInput {
    fn kind(&self)->u32{self.kind}
    fn stacks(&self)->bool{self.flags&2!=0}
}
struct State {
    queue:q::Queue<NmOriginalEventInput>,records:q::Records<NmOriginalEventInput>,
    slot2:bool,last_receipt:u64,poisoned:bool,
}
/// Thread-affine handle. All calls, including release, reject callback reentry.
/// No mutable Rust reference exists until this guard has been acquired.
pub struct NmOriginalEventQueue {generation:u64,birth:u64,busy:Cell<bool>,state:UnsafeCell<State>}
struct Guard<'a>(&'a NmOriginalEventQueue);
impl Drop for Guard<'_>{fn drop(&mut self){self.0.busy.set(false)}}
unsafe fn enter<'a>(p:*mut NmOriginalEventQueue,generation:u64,birth:u64)->Result<Guard<'a>,i32>{
    let Some(p)=(unsafe{p.as_ref()})else{return Err(NM_EV_ARG)};
    if p.generation!=generation||p.birth!=birth{return Err(NM_EV_IDENTITY)}
    if p.busy.replace(true){return Err(NM_EV_BUSY)}
    Ok(Guard(p))
}
impl Guard<'_>{fn state(&mut self)->&mut State{unsafe{&mut*self.0.state.get()}}}
fn valid_input(e:&NmOriginalEventInput)->bool{
    e.size==std::mem::size_of::<NmOriginalEventInput>()as u32&&e.kind!=0&&e.receipt!=0&&
    e.flags&!15==0&&e.admission&!511==0&&e.lifetime>=0&&
    (e.source_generation==0)==(e.source_birth==0)
}
fn check(s:&State,e:&NmOriginalEventInput)->i32{
    if s.poisoned{NM_EV_POISONED}else if !valid_input(e){NM_EV_ARG}else if e.receipt<=s.last_receipt{NM_EV_REPLAY}else{NM_EV_OK}
}

struct Pass {host:NmOriginalEventHost,generation:u64,birth:u64,built:u32,failed:bool}
impl Pass {
    fn request(&self,op:u32,e:Option<&q::Ev<NmOriginalEventInput>>)->NmOriginalEventRequest{
        let mut r=NmOriginalEventRequest{operation:op,generation:self.generation,birth:self.birth,built:self.built,..Default::default()};
        if let Some(e)=e{r.kind=e.kind;r.receipt=e.data.receipt;r.source_generation=e.data.source_generation;
            r.source_birth=e.data.source_birth;r.age=e.age;r.priority=e.priority;r.flags=e.data.flags;}
        r
    }
    fn call(&mut self,r:NmOriginalEventRequest)->NmOriginalEventReply{
        let mut out=NmOriginalEventReply{age:r.age,..Default::default()};
        if self.failed{return out}
        let status=unsafe{(self.host.call.unwrap())(self.host.context,&r,&mut out)};
        let allowed=if r.operation==5{7}else if r.operation==6{8}else{0};
        if status!=NM_EV_OK||out.answer>1||out.flags&!allowed!=0{self.failed=true;return NmOriginalEventReply{age:r.age,..Default::default()}}
        out
    }
}
impl q::Asks<NmOriginalEventInput> for Pass {
    fn ready(&mut self,e:&mut q::Ev<NmOriginalEventInput>)->bool{let o=self.call(self.request(1,Some(e)));e.age=o.age;o.answer!=0}
    fn valid(&mut self,e:&q::Ev<NmOriginalEventInput>)->bool{self.call(self.request(2,Some(e))).answer!=0}
    fn concerns(&mut self,e:&q::Ev<NmOriginalEventInput>)->bool{self.call(self.request(3,Some(e))).answer!=0}
}
impl q::Handler<NmOriginalEventInput,NmOriginalEventInput> for Pass {
    fn overrides(&mut self,e:&q::Ev<NmOriginalEventInput>,rec:&NmOriginalEventInput)->bool{
        let mut r=self.request(4,Some(e));r.record_kind=rec.kind;r.record_receipt=rec.receipt;
        r.record_source_generation=rec.source_generation;r.record_source_birth=rec.source_birth;r.record_priority=rec.priority;
        self.call(r).answer!=0
    }
    fn temporary(&mut self,e:&q::Ev<NmOriginalEventInput>)->bool{e.data.flags&1!=0}
    fn respond(&mut self,e:&q::Ev<NmOriginalEventInput>,slot2:&mut bool)->q::Reply{
        let o=self.call(self.request(5,Some(e)));self.built=o.flags;*slot2=o.flags&4!=0;
        q::Reply{main:o.flags&1!=0,secondary:o.flags&2!=0}
    }
    fn other(&mut self)->bool{false}
    fn other_tree(&mut self)->u32{0}
    fn ask(&mut self,task:u32,urgency:u32,e:&mut q::Ev<NmOriginalEventInput>)->bool{
        let mut r=self.request(6,Some(e));r.task=task;r.urgency=urgency;
        let o=self.call(r);e.age=o.age;if o.flags&8!=0{self.built&=!1}o.answer!=0
    }
    fn ragdoll(&mut self,e:&q::Ev<NmOriginalEventInput>)->bool{self.call(self.request(7,Some(e))).answer!=0}
    fn other_stop(&mut self){}
    // Original8246DFD8 as wired by eventpass::note. In particular, an abort
    // callback may delete main; the stale Reply.main must not make a record.
    fn note(&mut self,recs:&mut q::Records<NmOriginalEventInput>,e:&q::Ev<NmOriginalEventInput>,slot2:bool){
        if self.built&1==0||e.kind==q::SCRIPT_COMMAND{return}
        if e.data.flags&1!=0&&!slot2{
            if let Some(n)=recs.nontemp.take(){recs.pending=Some(n)}recs.temp=Some(e.data);
        }else{recs.nontemp=Some(e.data);recs.pending=None;recs.temp=None}
    }
    fn note_other(&mut self,_:&mut q::Records<NmOriginalEventInput>,_:&q::Ev<NmOriginalEventInput>){}
    fn give(&mut self,e:&q::Ev<NmOriginalEventInput>){self.call(self.request(8,Some(e)));}
    fn clear(&mut self){self.call(self.request(9,None));self.built=0}
}

/// Create a source event queue for ONE generation-bearing ped lifetime. No hooks.
#[unsafe(no_mangle)]
pub extern "C" fn nm_original_event_create(generation:u64,birth:u64)->*mut NmOriginalEventQueue{
    if generation==0||birth==0{return std::ptr::null_mut()}
    Box::into_raw(Box::new(NmOriginalEventQueue{generation,birth,busy:Cell::new(false),state:UnsafeCell::new(State{
        queue:q::Queue{evs:Vec::with_capacity(q::MAX)},records:Default::default(),slot2:false,last_receipt:0,poisoned:false})}))
}
/// Release only outside callbacks and for the exact lifetime. Failure retains it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_event_destroy(p:*mut NmOriginalEventQueue,generation:u64,birth:u64)->i32{
    let g=match unsafe{enter(p,generation,birth)}{Ok(x)=>x,Err(e)=>return e};drop(g);unsafe{drop(Box::from_raw(p))};NM_EV_OK
}
/// Validate before committing source health. This does not reserve a native queue slot.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_event_prepare(p:*mut NmOriginalEventQueue,generation:u64,birth:u64,event:*const NmOriginalEventInput)->i32{
    let mut g=match unsafe{enter(p,generation,birth)}{Ok(x)=>x,Err(e)=>return e};
    let Some(e)=(unsafe{event.as_ref()})else{return NM_EV_ARG};check(g.state(),e)
}
/// Original add/replace/reject runs AFTER synchronous source health. Added reports
/// social side effects once even when the original queue rejects a full-slot hit.
/// Monotonic receipt serials forbid retry/replaying those effects after removal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_event_add(p:*mut NmOriginalEventQueue,generation:u64,birth:u64,event:*const NmOriginalEventInput,added:*mut NmOriginalEventAdded)->i32{
    let mut g=match unsafe{enter(p,generation,birth)}{Ok(x)=>x,Err(e)=>return e};
    let (Some(e),Some(out))=(unsafe{event.as_ref()},unsafe{added.as_mut()})else{return NM_EV_ARG};
    let s=g.state();let status=check(s,e);if status!=NM_EV_OK{return status}
    let a=e.admission;let facts=q::Admit{ped:a&1!=0,refused:a&2!=0,decision:a&4!=0,trials:[a&8!=0,a&16!=0],
        response:e.decision_response,byte_c:a&32!=0,bit38:a&64!=0,blocking:a&128!=0,temporary:e.flags&1!=0,concerns:a&256!=0};
    let event=q::Ev{kind:e.kind,priority:e.priority,lifetime:e.lifetime,age:0,keep:false,data:*e};
    let o=s.queue.add(event,e.flags&4!=0,e.flags&8!=0,&facts);s.last_receipt=e.receipt;
    *out=NmOriginalEventAdded{stored:o.stored as u32,group:o.group as u32,friends:o.friends as u32,vehicle:o.vehicle as u32,heard:o.heard as u32};NM_EV_OK
}
/// Handle once per original intelligence pass. roots6 are opaque host task tokens:
/// five ORIGINAL primary slots then secondary0, never untranslated SA slot numbers.
/// PREFLIGHT must prove every event/task mapping supported. Callback failure after
/// preflight poisons the handle; no partially executed task transition is retried.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_event_handle(p:*mut NmOriginalEventQueue,generation:u64,birth:u64,roots6:*const u32,host:*const NmOriginalEventHost)->i32{
    let mut g=match unsafe{enter(p,generation,birth)}{Ok(x)=>x,Err(e)=>return e};
    let Some(host)=(unsafe{host.as_ref()})else{return NM_EV_ARG};
    if roots6.is_null()||host.size!=std::mem::size_of::<NmOriginalEventHost>()as u32||host.version!=VERSION||host.call.is_none(){return NM_EV_ARG}
    let s=g.state();if s.poisoned{return NM_EV_POISONED}
    let roots=unsafe{std::slice::from_raw_parts(roots6,6)};
    let tree=q::Tree{slots:roots[..5].try_into().unwrap(),secondary:roots[5]};
    let mut pass=Pass{host:*host,generation,birth,built:0,failed:false};
    let pre=pass.call(pass.request(0,None));if pass.failed||pre.answer==0{return NM_EV_UNMAPPED}
    s.queue.handle(&mut s.records,&tree,&mut s.slot2,&mut pass);
    if pass.failed{s.poisoned=true;NM_EV_POISONED}else{NM_EV_OK}
}
/// Inspect queue and source response records. No native task/health mutation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_event_snapshot(p:*mut NmOriginalEventQueue,generation:u64,birth:u64,out:*mut NmOriginalEventSnapshot)->i32{
    let mut g=match unsafe{enter(p,generation,birth)}{Ok(x)=>x,Err(e)=>return e};let Some(out)=(unsafe{out.as_mut()})else{return NM_EV_ARG};
    let s=g.state();*out=NmOriginalEventSnapshot{queued:s.queue.evs.len()as u32,poisoned:s.poisoned as u32,last_receipt:s.last_receipt,
        records:[s.records.nontemp,s.records.temp,s.records.pending,s.records.other].map(|x|x.map_or(0,|v|v.receipt)),note:s.records.note,slot2:s.slot2 as u32};NM_EV_OK
}
/// Receipts still referenced by queue/records; host keeps their exact payloads alive.
/// Count-only: null output/capacity0. Duplicates can occur and are intentional.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_event_references(p:*mut NmOriginalEventQueue,generation:u64,birth:u64,receipts:*mut u64,capacity:i32)->i32{
    let mut g=match unsafe{enter(p,generation,birth)}{Ok(x)=>x,Err(e)=>return e};if capacity<0||receipts.is_null()&&capacity!=0{return NM_EV_ARG}
    let s=g.state();let refs=s.queue.evs.iter().map(|e|e.data.receipt).chain([s.records.nontemp,s.records.temp,s.records.pending,s.records.other].into_iter().flatten().map(|e|e.receipt));
    let mut n=0;for r in refs{if n<capacity{unsafe{receipts.add(n as usize).write(r)}}n+=1}n
}
