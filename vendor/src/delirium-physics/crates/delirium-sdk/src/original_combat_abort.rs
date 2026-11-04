//! Original Combat1900 Damage9 arbitration. A child result is a SOURCE task's
//! result, not an SA MakeAbortable answer. No timers, force or motion writes.
use sa_combat::combatabort as ca;
#[repr(C)]#[derive(Clone,Copy,Default)]
pub struct NmOriginalCombatDamageAbort {
    pub size:u32,pub urgency:u32,pub ped:u32,pub ped_kind:i32,pub ped_flags:u32,pub locomotor:u32,
    pub target:u32,pub source:u32,pub combat_flags:u32,pub secondary:u32,pub main:u32,pub sub_kind:u32,pub child_accepted:u32,pub event_age:i32,
}
#[repr(C)]#[derive(Clone,Copy,Default)]
pub struct NmOriginalCombatDamageAbortResult {
    pub accepted:u32,pub asked_child:u32,pub dropped_main:u32,pub combat_flags:u32,pub ped_flags:u32,pub event_age:i32,
}
struct Calls{accepted:bool,asked:u32,dropped:u32,unsupported:bool}
impl ca::Calls for Calls{
    fn ask_sub(&mut self,_:i32)->bool{self.asked+=1;self.accepted}
    fn drop_main(&mut self){self.dropped+=1;}
    fn rand(&mut self)->f32{self.unsupported=true;0.}
    fn say(&mut self,_:u32,_:&'static str,_:u32){self.unsupported=true;}
    fn fire_type(&mut self,_:u32)->u32{self.unsupported=true;0}
    fn report(&mut self,_:u32,_:u32){self.unsupported=true;}
    fn raise(&mut self,_:u32){self.unsupported=true;}
    fn c823a1a60(&mut self,_:u32){self.unsupported=true;}
    fn c8254f768(&mut self,_:u32){self.unsupported=true;}
    fn ped_vf100(&mut self){self.unsupported=true;}
    fn blend(&mut self,_:u32,_:f32){self.unsupported=true;}
    fn release(&mut self,_:u32,_:u32){self.unsupported=true;}
    fn c82380aa8(&mut self,_:u32,_:u32,_:u32){self.unsupported=true;}
}
pub fn damage_abort(f:NmOriginalCombatDamageAbort)->Option<NmOriginalCombatDamageAbortResult>{
    if f.size!=std::mem::size_of::<NmOriginalCombatDamageAbort>()as u32||f.urgency>2||f.ped==0||f.child_accepted>1{return None}
    let mut t=ca::Combat{w60:f.combat_flags,target:(f.target!=0).then_some(ca::Target{id:f.target,..Default::default()}),sub:(f.sub_kind!=0).then_some(ca::Sub{kind:f.sub_kind,b4c:0})};
    let mut p=ca::Ped{id:f.ped,kind:f.ped_kind,w23c:f.ped_flags,wc9c:f.locomotor,secondary:f.secondary,main:(f.main!=u32::MAX).then_some(f.main),..Default::default()};
    let mut e=ca::Ev{kind:9,source:(f.source!=0).then_some(ca::Source{id:f.source,ped:true,player:false}),age:f.event_age,..Default::default()};
    let mut c=Calls{accepted:f.child_accepted!=0,asked:0,dropped:0,unsupported:false};
    let accepted=ca::combat(&mut t,&mut p,f.urgency as i32,Some(&mut e),&mut c);
    (!c.unsupported).then_some(NmOriginalCombatDamageAbortResult{accepted:accepted as u32,asked_child:c.asked,dropped_main:c.dropped,combat_flags:t.w60,ped_flags:p.w23c,event_age:e.age})
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_original_combat_damage_abort(input:*const NmOriginalCombatDamageAbort,out:*mut NmOriginalCombatDamageAbortResult)->i32{
    let Some(f)=(unsafe{input.as_ref()}).copied()else{return -1};if out.is_null(){return -1}
    let Some(r)=damage_abort(f)else{return -1};unsafe{*out=r};1
}
#[cfg(test)]mod tests{
    use super::*;
    fn facts()->NmOriginalCombatDamageAbort{NmOriginalCombatDamageAbort{size:56,urgency:1,ped:1,ped_kind:1,target:2,source:2,main:1900,sub_kind:1905,child_accepted:1,..Default::default()}}
    #[test]fn pending_melee_does_not_restart_outer_combat(){let f=facts();let r=damage_abort(f).unwrap();assert_eq!((r.accepted,r.asked_child,r.event_age,r.dropped_main),(0,0,1,0));}
    #[test]fn real_secondary_drops_redundant_combat_then_asks_actual_child(){let mut f=facts();f.secondary=431;let r=damage_abort(f).unwrap();assert_eq!((r.accepted,r.asked_child,r.event_age,r.dropped_main),(1,1,0,1));f.child_accepted=0;assert_eq!(damage_abort(f).unwrap().accepted,0);}
    #[test]fn immediate_still_requires_child_and_preserves_original_motor_flag(){let mut f=facts();f.urgency=2;f.ped_flags=0x80000;f.locomotor=1;let r=damage_abort(f).unwrap();assert_eq!((r.accepted,r.ped_flags,r.dropped_main),(1,0,0));}
}
