//! Host boundary for the original non-cop Combat task, not a new aggression roll.
//! Original task/control/decision algorithms live unmodified in lsc4_combat.
//! SA still supplies the initiating event, native navigation and unmapped roles.
use lsc4_combat::{combat::{self,Combat,Facts,Sub},decision::{DecisionMaker,situation},rng::Rand};

pub const FACT_FLOATS:usize=8;
pub const SUPPORTED:u32=1;pub const ENGAGEMENT:u32=2;pub const HARD_EXIT:u32=4;
pub const SAME_FLEE:u32=8;pub const TARGET_PLAYER:u32=16;pub const TARGET_COP:u32=32;
pub const TARGET_VEHICLE:u32=64;pub const GANG:u32=128;pub const POPULATION:u32=256;
pub const TARGET_MISSION:u32=512;pub const TARGET_FRIEND:u32=1024;pub const HATES:u32=2048;
pub const WANTED_COP:u32=4096;pub const PLAYER_INSIDE:u32=8192;pub const HIT_RESPONSE:u32=16384;
pub const FLAGS:u32=(HIT_RESPONSE<<1)-1;
const UNWEIGHED:[(i32,f32);8]=[(1700,0.),(1200,0.),(300,0.),(2000,0.),(2001,0.),(2002,0.),(2003,0.),(2004,0.)];

#[derive(Clone,Copy,Debug,Default)]
pub struct Input{pub flags:u32,pub target:Option<u32>,pub target_health:f32,pub target_armed:u8,pub distance:f32,pub line:u8,pub fights:u8}
impl Input{
 pub fn read(v:&[f32])->Result<Self,String>{
  if v.len()!=FACT_FLOATS||v.iter().any(|x|!x.is_finite())||v[0]<0.||v[0].fract()!=0.||v[0] as u32&!FLAGS!=0||v[1]< -1.||v[1].fract()!=0.||v[2]<0.||!(0. ..=2.).contains(&v[3])||v[3].fract()!=0.||v[4]<0.||!(0. ..=3.).contains(&v[5])||v[5].fract()!=0.||!(0. ..=16.).contains(&v[6])||v[6].fract()!=0.||v[7]!=0.{return Err("invalid NPC decision facts".into())}
  Ok(Self{flags:v[0] as u32,target:(v[1]>=0.).then_some(v[1] as u32),target_health:v[2],target_armed:v[3] as u8,distance:v[4],line:v[5] as u8,fights:v[6] as u8})
 }
 fn facts(self,dt:f32,health:f32,bravery:u8)->Facts{
  let has=|f|self.flags&f!=0;
  Facts{dt,health,bravery,target:self.target.is_some()&&self.target_health>0.,target_armed:self.target_armed,
   target_cop:has(TARGET_COP),wanted_cop_near:has(WANTED_COP),gang:has(GANG),hates_target:has(HATES),
   target_in_vehicle:has(TARGET_VEHICLE),target_player:has(TARGET_PLAYER),target_friend:has(TARGET_FRIEND),target_mission:has(TARGET_MISSION),
   fights:self.fights,target_hurt:self.target_health<100.,population:has(POPULATION),player_inside:has(PLAYER_INSIDE),
   no_cover:true,target_dist:self.distance,line_state:self.line,..Default::default()}
 }
}

/// Explicit ambient on-foot index -1: owned R_Norm and the original injured EMPTY file.
/// No guessed decision rows when either dependency is absent.
pub struct CivilianEvents {normal:DecisionMaker,empty:DecisionMaker}
impl CivilianEvents {
 pub fn load(dir:&std::path::Path)->Result<Self,String>{
  let read=|name:&str|DecisionMaker::parse(&std::fs::read_to_string(dir.join(name)).map_err(|e|e.to_string())?);
  Ok(Self{normal:read("R_Norm.ped")?,empty:read("m_empty.ped")?})
 }
 /// 82573808's under-100 EMPTY, then824C25A8's trials and final event9 pick.
 pub fn harmed(&self,health:f32,column:usize,rng:&mut Rand)->i32{
  let dm=if health<100.{&self.empty}else{&self.normal};
  for allowed in [1200,1700,300]{dm.pick(9,column,false,None,&[],&[allowed],rng);}
  dm.pick(9,column,false,None,&UNWEIGHED,&[],rng)
 }
}

#[derive(Default)]
pub struct NpcDecision{task:Option<Combat>,target:Option<u32>,exhausted:Option<u32>,pub code:i32,pub refused:bool,pub unsupported:Option<i32>}
impl NpcDecision{
 pub fn active(&self)->bool{self.task.is_some()}
 /// 8218C790..8218C7C0: accepted Damage response1900 constructs Combat at
 /// its actual source and sets bit17. Do not restart an already owning task.
 pub fn damage_combat(&mut self,target:u32,rng:&mut Rand)->bool{
  if self.active(){return false}
  let mut task=Combat::new(0.,rng);task.fighting();
  self.task=Some(task);self.target=Some(target);self.exhausted=None;self.code=0;true
 }

 /// Hit dispatcher tests the outer Combat(1900) and its Retreat(1904), not
 /// the separate Melee(431) animation task. A running reaction is not combat.
 pub fn reaction_tasks(&self)->[bool;2]{[self.task.is_some(),self.task.as_ref().is_some_and(|c|c.sub.as_ref().is_some_and(|s|s.code()==combat::RETREAT))]}
 pub fn clear(&mut self){*self=Self::default();}
 /// Physical and received-action owners can refuse a primary change. The
 /// original control's refused branch restores its own state, rather than us
 /// inventing a stun timer or suppressing fear. Death/missions remain host exits.
 pub fn step(&mut self,dm:&DecisionMaker,input:Input,health:f32,bravery:u8,protected:bool,dt:f32,rng:&mut Rand)->i32{
  self.refused=false;self.unsupported=None;
  if input.flags&SUPPORTED==0||input.flags&HARD_EXIT!=0||health<=0.{self.clear();return 0}
  if self.active()&&self.target!=input.target{
   if protected {self.refused=true;return self.code}
   self.clear();
  }
  if self.task.is_none(){
   if input.target.is_some()&&self.exhausted==input.target{self.code=combat::NONE;return self.code}
   // No ambient aggression or reversal of an already fleeing passive NPC.
   if input.flags&ENGAGEMENT==0||input.flags&SAME_FLEE!=0||input.target.is_none(){self.code=0;return self.code}
   let mut c=Combat::new(0.,rng);if input.flags&HIT_RESPONSE!=0{c.fighting();}
   self.task=Some(c);self.target=input.target;
  }
  let mut f=input.facts(dt,health,bravery);
  let attributes=dm.attributes;
  f.caution=attributes.caution;f.navigation_style=attributes.navigation_style;f.standing_style=attributes.standing_style;
  f.injured_reaction=attributes.target_injured_reaction;
  let mask=situation::mask(health<f32::from(attributes.low_health),situation::UNARMED,input.target_armed,f.target_in_vehicle);
  let mut unsupported=None;
  let mut ask=|s,rng:&mut Rand|{
   let result=dm.pick(s,0,false,Some(mask),&UNWEIGHED,&[],rng);
   if !matches!(result,combat::NONE|combat::FIRE|combat::RETREAT|combat::CHARGE){unsupported=Some(result);}
   result
  };
  let c=self.task.as_mut().unwrap();let mut go=c.step(&f,&mut ask,rng);
  if c.abort().is_some()&&protected{go=c.refused(&f,&mut ask,rng);self.refused=true;}
  c.remade=false;
  if unsupported.is_some(){self.clear();self.unsupported=unsupported;return 0}
  if !go{self.clear();self.exhausted=input.target;self.code=combat::NONE;return self.code}
  self.code=c.sub.map_or(0,Sub::code);self.code
 }
}

#[cfg(test)]mod tests{
 use super::*;
 fn dm()->DecisionMaker{DecisionMaker::parse(&std::fs::read_to_string(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tools/mod-staging/npc-decisions-20261101/decision/M_NO_NO.cmb")).unwrap()).unwrap()}
 fn input()->Input{Input{flags:SUPPORTED|ENGAGEMENT|POPULATION|TARGET_PLAYER,target:Some(42),target_health:200.,distance:1.2,line:1,..Default::default()}}
 #[test]fn original_ambient_damage_event_and_preexisting_flee_are_distinct(){
  let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tools/mod-staging/original-health-20261101/decision");
  let events=CivilianEvents::load(&root).unwrap();let mut r=Rand::new(1);
  assert_eq!(events.harmed(200.,1,&mut r),1900);assert_eq!(events.harmed(200.,0,&mut r),1900);
  assert_eq!(events.harmed(200.,2,&mut r),200);assert_eq!(events.harmed(99.,1,&mut r),200);
  let mut c=NpcDecision::default();let mut f=input();f.flags|=SAME_FLEE;
  assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),0);
  assert!(c.damage_combat(42,&mut r));let seed=r.seed;assert!(!c.damage_combat(42,&mut r));assert_eq!(r.seed,seed);
  assert_eq!(c.step(&dm(),f,200.,4,true,0.016,&mut r),combat::CHARGE);
  f.target_armed=2;for _ in 0..350{c.step(&dm(),f,200.,4,false,0.016,&mut r);}
  assert_eq!(c.code,combat::RETREAT,"source courage may still choose a real flee");
 }
 #[test]fn passive_and_unknown_roles_do_not_gain_an_attack(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();f.flags&=!ENGAGEMENT;assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),0);f.flags=ENGAGEMENT;assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),0);assert!(!c.active());}
 #[test]fn exact_default_file_fights_fists_and_retreats_from_a_gun(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::CHARGE);f.target_armed=2;assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::RETREAT);}
 #[test]fn a_same_target_native_flee_proposal_cannot_overrule_original_courage(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::CHARGE);f.flags|=SAME_FLEE;assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::CHARGE);}
 #[test]fn an_existing_passive_flee_is_not_suppressed(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();f.flags|=SAME_FLEE;assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),0);}
 #[test]fn a_physical_response_refuses_weapon_escalation_until_original_rethink(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();c.step(&dm(),f,200.,4,false,0.016,&mut r);f.target_armed=2;assert_eq!(c.step(&dm(),f,200.,4,true,0.016,&mut r),combat::CHARGE);assert!(c.refused);assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::CHARGE);for _ in 0..350{c.step(&dm(),f,200.,4,false,0.016,&mut r);}assert_eq!(c.code,combat::RETREAT);}
 #[test]fn unrelated_panic_and_death_remain_hard_boundaries(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();c.step(&dm(),f,200.,4,false,0.016,&mut r);f.flags|=HARD_EXIT;assert_eq!(c.step(&dm(),f,200.,4,true,0.016,&mut r),0);assert!(!c.active());f.flags&=!HARD_EXIT;c.step(&dm(),f,200.,4,false,0.016,&mut r);assert_eq!(c.step(&dm(),f,0.,4,true,0.016,&mut r),0);}
 #[test]fn an_injured_target_ends_combat_after_the_current_response(){let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();c.step(&dm(),f,200.,4,false,0.016,&mut r);f.target_health=90.;assert_eq!(c.step(&dm(),f,200.,4,true,0.016,&mut r),combat::CHARGE);assert!(c.refused);assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::NONE);assert_eq!(c.step(&dm(),f,200.,4,false,0.016,&mut r),combat::NONE);}
 #[test]fn completed_task_releases_passive_state_and_accepts_a_new_opponent(){
  let mut c=NpcDecision::default();let mut r=Rand::new(1);let mut f=input();let d=dm();
  assert_eq!(c.step(&d,f,200.,4,false,0.016,&mut r),combat::CHARGE);
  f.target_health=90.;assert_eq!(c.step(&d,f,200.,4,false,0.016,&mut r),combat::NONE);
  let ended=f;f.flags&=!ENGAGEMENT;f.target=None;
  assert_eq!(c.step(&d,f,200.,4,false,0.016,&mut r),0);assert_eq!(c.code,0);
  assert_eq!(c.step(&d,ended,200.,4,false,0.016,&mut r),combat::NONE);assert_eq!(c.code,combat::NONE);
  f=input();f.target=Some(43);
  assert_eq!(c.step(&d,f,200.,4,false,0.016,&mut r),combat::CHARGE);assert_eq!(c.code,combat::CHARGE);
 }
 #[test]fn hit_dispatch_uses_combat_and_retreat_not_a_running_melee_clip(){
  use lsc4_combat::{melee::{Ask,Fighter,Special,hit_reaction},actiontable::ActionTable};
  let table=ActionTable::default();let mut ask=Ask{fighter:Fighter{health:200.,..Default::default()},groups:[-1;7],loaded:&|_|true,
   receiving_hit:false,blocking:false,just_struck:false,target:None,lock_on:false,impulses:false,controls:None,setting:0,
   kind:None,idle:None,pos:[0.;3],heading:0.,opponent_res:0,special:Special::default()};
  // No matching action is an explicit original branch, not an assumed miss.
  let mut c=NpcDecision::default();let mut rng=Rand::new(1);let mut f=input();let d=dm();
  assert!(hit_reaction(&table,&ask,true,c.reaction_tasks(),||0).ragdoll,"passive reaction is not Combat1900");
  c.step(&d,f,200.,4,false,0.016,&mut rng);
  assert!(!hit_reaction(&table,&ask,true,c.reaction_tasks(),||0).ragdoll,"healthy standing combat must remain authored");
  ask.fighter.health=124.;assert!(hit_reaction(&table,&ask,true,c.reaction_tasks(),||0).ragdoll);
  ask.fighter.health=200.;f.target_armed=2;c.step(&d,f,200.,4,false,0.016,&mut rng);
  assert!(hit_reaction(&table,&ask,true,c.reaction_tasks(),||0).ragdoll,"Retreat1904 changes the original dispatcher branch");
  assert!(!hit_reaction(&table,&ask,false,c.reaction_tasks(),||0).ragdoll,"task mapping cannot override the original NM gate");
 }
}
