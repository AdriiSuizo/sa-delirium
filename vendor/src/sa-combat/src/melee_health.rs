//! Synchronous original health before the hit selector. No SA damage units.
//! The host reserves/commits its one lifetime/revision owner; source RNG advances
//! only if that commit succeeds. Unmapped tasks/facts never get guessed defaults.
pub use lsc4_combat::ped;
use lsc4_combat::{rng::Rand, weapons::Weapon};

#[derive(Clone,Copy,Debug)]
pub struct Request {
    pub source:u32,pub victim:u32,pub now:u32,pub bone:u16,pub amount:f32,
    pub source_player:bool,pub victim_player:bool,pub guard:bool,pub kind:u8,
}
#[derive(Clone,Copy,Debug)]
pub struct Facts {
    pub generation:u64,pub birth:u64,pub revision:u64,pub source_generation:u64,pub source_birth:u64,
    pub health:ped::Health,pub victim:ped::Victim,
    pub source_group:Option<u32>,pub friend:bool,pub same_group:bool,pub floor:bool,
    pub source_owned_by_victim:bool,
    /// Source facts/task/accounting capabilities are complete for this outcome.
    /// PREPARE may reserve storage; COMMIT still decides terminal branch support.
    pub complete:bool,
    /// Selected original PED/group decision index, never an SA decision id.
    pub decision:Option<DecisionFacts>,
}
#[derive(Clone,Copy,Debug)]
pub struct DecisionFacts {pub index:i32,pub hostile:bool,pub network_refused:bool}
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct QueueDecision {pub response:i32,pub trials:[bool;3],pub network_refused:bool}
#[derive(Clone,Copy,Debug)]
pub struct Receipt {
    pub request:Request,pub facts:Facts,pub after:ped::Health,pub outcome:ped::Outcome,
    pub rng_before:u32,pub rng_after:u32,
    pub selection:Option<Selection>,
    pub queue:Option<QueueDecision>,
}
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct Selection {pub ragdoll:bool,pub pending:bool,pub action:Option<u32>,pub result:Option<u32>}
pub trait Boundary {
    fn prepare(&mut self,request:Request)->Option<Facts>;
    /// True only after the original owner/mirror, response storage and accounting
    /// have committed. False must cancel its reservations without any mutation.
    fn commit(&mut self,receipt:&Receipt)->Result<bool,String>;
    /// PREPARE is allowed to reserve storage. Invalid facts must release it.
    fn cancel(&mut self,_request:Request){}
}
/// Original arithmetic only, on a copied RNG. The dispatcher must select its
/// response with `after.health` before the host can reserve the matching task.
pub fn calculate(request:Request,weapon:&Weapon,rng:&Rand,mut facts:Facts)->Option<Receipt>{
    if request.source==request.victim||request.kind!=0||!request.amount.is_finite()||request.amount<0.||
        weapon.name!="UNARMED"||weapon.fire!="MELEE"||weapon.damage_type!="MELEE"||weapon.slot!="UNARMED"{
        return None
    }
    if !facts.complete||facts.generation==0||facts.birth==0||facts.revision==0||facts.source_generation==0||facts.source_birth==0||
        !facts.health.health.is_finite()||!facts.health.max.is_finite()||facts.health.max<=0.||
        !facts.health.armour.is_finite()||facts.health.armour<0.||!facts.victim.factor.is_finite()||facts.victim.factor<0.||
        facts.victim.player!=request.victim_player{return None}
    // IsABlock comes from the ACTUAL current source result, not an SA blocking
    // key or native reaction animation. Head protection remains a host fact.
    facts.victim.guard=request.guard;
    let hit=ped::Hit{amount:request.amount,weapon,bone:request.bone,
        attacker:if request.source_player{ped::Attacker::Player{sure:false}}else{ped::Attacker::Ped},
        friend:facts.friend,floor:facts.floor,group:facts.source_group,same_group:facts.same_group,number:Some(0)};
    let allowed=facts.health.health<100.||(!facts.source_owned_by_victim&&ped::allowed(&hit,&facts.victim));
    let mut after=facts.health;let mut trial=*rng;
    let mut outcome=if allowed{ped::damage(&mut after,&hit,&facts.victim,&mut trial)}else{ped::Outcome::default()};
    // The pure damage arithmetic returns only 04/08/10/20;824DC670 owns APPLIED
    // and LANDED. Preserve protected physical hits as calculated/not-landed.
    outcome.flags|=0x80|if allowed{0x40}else{0};
    Some(Receipt{request,facts,after,outcome,rng_before:rng.seed,rng_after:trial.seed,selection:None,queue:None})
}
/// 824C25A8 -> 8230B258 trials1200/1700/300, then8230B0D0. This
/// runs after the dispatcher selector, before publishing its shared RNG.
/// The original queue may refuse admission later; its decision draws still
/// occurred. Only an upstream network refusal skips these source draws.
pub fn decide_queue(r:&mut Receipt,store:&lsc4_combat::decision::Store,rng:&mut Rand)->Option<()> {
    let f=r.facts.decision?;
    // Combat decision makers need weapon/situation facts not carried here.
    // Script indices need their script store, and cannot fall through to a file.
    if f.index>=0&&(!matches!(store.kind(f.index),Some(lsc4_combat::decision::Kind::Ped|lsc4_combat::decision::Kind::Group))) {return None}
    if store.get(f.index,r.facts.victim.mission).is_none(){return None}
    if f.network_refused {r.queue=Some(QueueDecision{response:200,trials:[false;3],network_refused:true});return Some(())}
    let column=if f.hostile{3}else if r.facts.friend{2}else if r.request.source_player{1}else{0};
    let pick=|allowed:&[i32],weights:&[(i32,f32)],rng:&mut Rand|store.pick(f.index,r.facts.victim.mission,r.after.health,9,column,r.facts.victim.in_vehicle,0,weights,allowed,rng);
    let trials=[1200,1700,300].map(|task|pick(&[task],&[],rng)==task);
    let weights=[(1700,0.),(1200,0.),(300,0.),(2000,0.),(2001,0.),(2002,0.),(2003,0.),(2004,0.)];
    let response=pick(&[],&weights,rng);r.queue=Some(QueueDecision{response,trials,network_refused:false});r.rng_after=rng.seed;Some(())
}
pub fn transact(request:Request,weapon:&Weapon,rng:&mut Rand,host:&mut dyn Boundary)->Result<Option<Receipt>,String>{
    let Some(facts)=host.prepare(request)else{return Ok(None)};
    let Some(receipt)=calculate(request,weapon,rng,facts)else{host.cancel(request);return Ok(None)};
    if !host.commit(&receipt)?{return Ok(None)}
    rng.seed=receipt.rng_after;Ok(Some(receipt))
}

#[cfg(test)]mod tests {
 use super::*;
 struct Host{facts:Facts,accept:bool,commits:Vec<Receipt>}
 impl Boundary for Host{fn prepare(&mut self,_:Request)->Option<Facts>{Some(self.facts)}fn commit(&mut self,r:&Receipt)->Result<bool,String>{if self.accept{self.commits.push(*r);}Ok(self.accept)}}
 fn fixtures()->(Request,Weapon,Host){
  let r=Request{source:1,victim:2,now:100,bone:0x36a1,amount:20.,source_player:true,victim_player:false,guard:false,kind:0};
  let w=Weapon{name:"UNARMED".into(),fire:"MELEE".into(),damage_type:"MELEE".into(),slot:"UNARMED".into(),flags:vec!["ARMOUR_PENETRATING".into()],..Default::default()};
  let h=Host{facts:Facts{generation:2,birth:3,revision:1,source_generation:1,source_birth:4,health:ped::Health{health:200.,max:200.,armour:50.},victim:ped::Victim::default(),source_group:Some(0),friend:false,same_group:false,floor:false,source_owned_by_victim:false,complete:true,decision:None},accept:true,commits:vec![]};(r,w,h)
 }
 #[test]fn actual_unarmed_penetrates_armour_and_guard_precedes_selector(){let(mut r,w,mut h)=fixtures();r.guard=true;let mut rng=Rand::new(1);
  let p=transact(r,&w,&mut rng,&mut h).unwrap().unwrap();assert_eq!((p.after.health,p.after.armour,p.outcome.lost,p.outcome.flags),(190.,50.,10.,0xc0));assert_eq!(rng.seed,1);}
 #[test]fn head_ko_precedes_guard_and_updates_rng_once(){let(mut r,w,mut h)=fixtures();r.bone=0x4b5;r.guard=true;let mut rng=Rand::new(1);
  let p=transact(r,&w,&mut rng,&mut h).unwrap().unwrap();assert_eq!(p.after.health,99.);assert_eq!(p.outcome.flags,0xd4);assert_eq!(rng.seed,1u32.wrapping_mul(0x343fd).wrapping_add(0x269ec3));}
 #[test]fn helmet_changes_roll_eligibility_not_only_visual_reaction(){let(mut r,w,mut h)=fixtures();r.bone=0x4b5;r.amount=7.;h.facts.victim.hat=true;let mut rng=Rand::new(1);
  let p=transact(r,&w,&mut rng,&mut h).unwrap().unwrap();assert_eq!(p.after.health,194.75);assert_eq!(rng.seed,1);}
 #[test]fn rejected_terminal_branch_does_not_consume_shared_rng_or_health(){let(mut r,w,mut h)=fixtures();r.bone=0x4b5;h.accept=false;let mut rng=Rand::new(1);
  assert!(transact(r,&w,&mut rng,&mut h).unwrap().is_none());assert_eq!(rng.seed,1);assert_eq!(h.facts.health.health,200.);assert!(h.commits.is_empty());}
 #[test]fn protected_hit_keeps_calculated_without_landed_and_zero_rng(){let(mut r,w,mut h)=fixtures();r.bone=0x4b5;h.facts.victim.flags=ped::flags::MELEE_PROOF;let mut rng=Rand::new(1);
  let p=transact(r,&w,&mut rng,&mut h).unwrap().unwrap();assert_eq!(p.outcome.flags,0x80);assert_eq!(p.after.health,200.);assert_eq!(rng.seed,1);}
 #[test]fn source_owned_refusal_and_injured_proof_bypass_are_distinct(){let(r,w,mut h)=fixtures();h.facts.source_owned_by_victim=true;let mut rng=Rand::new(1);
  assert_eq!(transact(r,&w,&mut rng,&mut h).unwrap().unwrap().outcome.flags,0x80);
  h.facts.health.health=90.;assert_eq!(transact(r,&w,&mut rng,&mut h).unwrap().unwrap().after.health,70.);}
 #[test]fn same_frame_second_hit_uses_committed_owner_not_frame_start_health(){let(r,w,mut h)=fixtures();let mut rng=Rand::new(1);
  let first=transact(r,&w,&mut rng,&mut h).unwrap().unwrap();h.facts.health=first.after;h.facts.revision+=1;
  let second=transact(r,&w,&mut rng,&mut h).unwrap().unwrap();assert_eq!((first.after.health,second.after.health),(180.,160.));assert_eq!(second.facts.revision,2);}
}
