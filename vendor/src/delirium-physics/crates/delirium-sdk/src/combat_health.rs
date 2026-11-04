//! One synchronous hit transaction, before the original reaction selector.
//! No native health arithmetic or implicit task capability crosses this ABI.
use super::*;
use sa_combat::melee_health::{self as health,Boundary};
use sa_combat::melee_health::ped;

#[repr(C)]#[derive(Clone,Copy,Default)]
pub struct NmCombatHealthRequest{pub version:u32,pub source:u32,pub victim:u32,pub now:u32,pub bone:u32,pub kind:u32,pub flags:u32,pub amount:f32}
#[repr(C)]#[derive(Clone,Copy,Default)]
pub struct NmCombatHealthFacts{
    pub generation:u64,pub birth:u64,pub revision:u64,pub source_generation:u64,pub source_birth:u64,
    pub health:f32,pub maximum:f32,pub armour:f32,pub factor:f32,
    pub proof_flags:u32,pub proof_group:u32,pub victim_flags:u32,pub source_group:u32,pub relationship_flags:u32,pub complete:u32,
    pub decision_index:i32,pub decision_flags:u32,
}
#[repr(C)]#[derive(Clone,Copy,Default)]
pub struct NmCombatHealthResult{
    pub health:f32,pub maximum:f32,pub armour:f32,pub lost:f32,pub absorbed:f32,
    pub outcome_flags:u32,pub rng_before:u32,pub rng_after:u32,pub selection_flags:u32,pub action:u32,pub result:u32,
    pub decision_response:i32,pub queue_flags:u32,
}
pub type NmCombatHealthCallback=unsafe extern "C" fn(*mut std::ffi::c_void,u32,*const NmCombatHealthRequest,*mut NmCombatHealthFacts,*const NmCombatHealthResult)->i32;
#[repr(C)]#[derive(Clone,Copy)]
pub struct NmCombatHealthHost{pub version:u32,pub context:*mut std::ffi::c_void,pub callback:Option<NmCombatHealthCallback>}
fn request(r:health::Request)->NmCombatHealthRequest{NmCombatHealthRequest{version:1,source:r.source,victim:r.victim,now:r.now,bone:r.bone as u32,kind:r.kind as u32,
    flags:r.source_player as u32|((r.victim_player as u32)<<1)|((r.guard as u32)<<2),amount:r.amount}}
fn unpack(f:NmCombatHealthFacts)->Option<health::Facts>{
    if f.complete!=1||f.proof_group>u16::MAX as u32||f.victim_flags&!0x1ffff!=0||f.relationship_flags&!15!=0||f.decision_flags&!7!=0{return None}
    let b=|n:u32|f.victim_flags&(1u32<<n)!=0;let r=|n:u32|f.relationship_flags&(1u32<<n)!=0;
    Some(health::Facts{generation:f.generation,birth:f.birth,revision:f.revision,source_generation:f.source_generation,source_birth:f.source_birth,
        health:ped::Health{health:f.health,max:f.maximum,armour:f.armour},
        victim:ped::Victim{player:b(0),factor:f.factor,in_vehicle:b(1),population:b(2),hat:b(3),guard:false,swat:b(4),flags:f.proof_flags,group:f.proof_group as u16,
            vest:b(5),ragdolling:b(6),shootable_in_vehicle:b(7),hurtable:b(8),fire_proof:b(9),mission:b(10),dies_when_injured:b(11),deep_water:b(12),no_critical:b(13),drowning_ok:b(14),weapon58_ok:b(15),blades_ok:b(16)},
        source_group:(f.source_group!=u32::MAX).then_some(f.source_group),friend:r(0),same_group:r(1),floor:r(2),source_owned_by_victim:r(3),complete:true,
        decision:(f.decision_flags&1!=0).then_some(health::DecisionFacts{index:f.decision_index,hostile:f.decision_flags&2!=0,network_refused:f.decision_flags&4!=0})})
}
fn result(r:&health::Receipt)->NmCombatHealthResult{
    let s=r.selection;
    NmCombatHealthResult{health:r.after.health,maximum:r.after.max,armour:r.after.armour,lost:r.outcome.lost,absorbed:r.outcome.absorbed,outcome_flags:r.outcome.flags as u32,
        rng_before:r.rng_before,rng_after:r.rng_after,selection_flags:s.map_or(0,|s|1|((s.ragdoll as u32)<<1)|((s.pending as u32)<<2)),
        action:s.and_then(|s|s.action).unwrap_or(0),result:s.and_then(|s|s.result).unwrap_or(0),decision_response:r.queue.map_or(200,|q|q.response),
        queue_flags:r.queue.map_or(0,|q|1|((q.trials[0]as u32)<<1)|((q.trials[1]as u32)<<2)|((q.trials[2]as u32)<<3)|((q.network_refused as u32)<<4))}
}
struct Host{host:NmCombatHealthHost,last:Option<(NmCombatHealthRequest,NmCombatHealthFacts)>}
impl Boundary for Host{
    fn prepare(&mut self,r:health::Request)->Option<health::Facts>{
        let q=request(r);let mut f=NmCombatHealthFacts::default();
        let ok=unsafe{self.host.callback.unwrap()(self.host.context,0,&q,&mut f,std::ptr::null())};
        if ok!=1{return None}
        self.last=Some((q,f));let facts=unpack(f);if facts.is_none(){self.cancel(r);}facts
    }
    fn cancel(&mut self,_:health::Request){if let Some((q,mut f))=self.last.take(){unsafe{self.host.callback.unwrap()(self.host.context,2,&q,&mut f,std::ptr::null());}}}
    fn commit(&mut self,r:&health::Receipt)->Result<bool,String>{
        let (q,mut f)=self.last.take().ok_or("missing original health reservation")?;
        if q.source!=r.request.source||q.victim!=r.request.victim||q.now!=r.request.now{return Err("original health reservation changed identity".into())}
        let out=result(r);let status=unsafe{self.host.callback.unwrap()(self.host.context,1,&q,&mut f,&out)};
        match status{1=>Ok(true),0=>Ok(false),_=>Err("original health commit failed after reservation".into())}
    }
}
impl Drop for Host{fn drop(&mut self){if let Some((q,mut f))=self.last.take(){unsafe{self.host.callback.unwrap()(self.host.context,2,&q,&mut f,std::ptr::null());}}}}

/// Load original weaponinfo.xml explicitly; the enabled combat runtime is unchanged on error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_health_weapons(c:*mut NmCombat,path:*const c_char)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null or busy combat")};
    let Some(rt)=c.1.as_mut()else{return fail(NM_E_ARG,"combat disabled")};
    let Some(path)=(unsafe{text(path)})else{return fail(NM_E_ARG,"missing source weaponinfo")};
    match std::fs::read_to_string(path).map_err(|e|e.to_string()).and_then(|xml|rt.load_health_weapons(&xml)){Ok(())=>0,Err(e)=>fail(NM_E_PARSE,e)}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_health_decisions(c:*mut NmCombat,path:*const c_char)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null or busy combat")};
    let Some(rt)=c.1.as_mut()else{return fail(NM_E_ARG,"combat disabled")};
    let Some(path)=(unsafe{text(path)})else{return fail(NM_E_ARG,"missing source decision directory")};
    match rt.load_health_decisions(std::path::Path::new(path)){Ok(())=>0,Err(e)=>fail(NM_E_PARSE,e)}
}
/// Phases: 0 prepare explicit facts/storage;1 commit health+source task/accounting;
/// 2 cancel. Return1 accepts,0 refuses without mutation,negative is a fatal
/// publication invariant failure. Callbacks cannot reenter this combat object.
/// Unmapped contacts retain the explicit legacy path and have no receipt.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_finish_health(c:*mut NmCombat,host:*const NmCombatHealthHost)->i32{
    let Some(h)=(unsafe{host.as_ref()}).copied().filter(|h|h.version==1&&h.callback.is_some())else{return fail(NM_E_ARG,"invalid source health host")};
    let raw=c;let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null or busy combat")};
    let Some(rt)=c.1.as_mut()else{return fail(NM_E_ARG,"combat disabled")};
    let Some(_guard)=CombatCallbackGuard::enter(raw)else{return fail(NM_E_ARG,"recursive combat callback")};
    let mut bridge=Host{host:h,last:None};
    match rt.finish_with_health(&c.0,Some(&mut bridge)){Ok(())=>rt.hits.len() as i32,Err(e)=>fail(NM_E_ARG,e)}
}
/// Accepted transaction for a hit of the just-finished frame. A receipt means
/// the host MUST NOT call the SA damage calculator for that hit again.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_health_receipt(c:*const NmCombat,index:i32,out:*mut NmCombatHealthResult)->i32{
    let Some(rt)=(unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref())else{return fail(NM_E_ARG,"null or busy combat")};
    if index<0||out.is_null(){return fail(NM_E_ARG,"invalid health receipt output")}
    let Some(r)=rt.health_receipts.get(&(index as usize))else{return 0};unsafe{*out=result(r)};1
}

#[cfg(test)]mod tests{
    use super::*;
    #[test]fn layout_is_stable(){assert_eq!(std::mem::size_of::<NmCombatHealthRequest>(),32);assert_eq!(std::mem::size_of::<NmCombatHealthFacts>(),88);assert_eq!(std::mem::size_of::<NmCombatHealthResult>(),52);}
    #[test]fn unknown_flags_and_incomplete_source_facts_are_rejected(){let mut f=NmCombatHealthFacts::default();assert!(unpack(f).is_none());f.complete=1;assert!(unpack(f).is_some());f.victim_flags=1<<17;assert!(unpack(f).is_none());f.victim_flags=0;f.relationship_flags=16;assert!(unpack(f).is_none());}
}
