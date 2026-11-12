//! One scene clock, real target identities, authored strike windows and contacts.
//! SA owns navigation, damage accounting, guns, world collision and ragdolls.
use crate::{Data,hash,animation::{Bank,Pose,Rig,iv_bone,mover,mover_q},controller::{self as ct,Me,Melee,Pad,Foe,WeaponView}};
use bevy_math::{Vec2,Vec3,Quat};
use lsc4_combat::{melee::Held,melee_task,actiontable::wrap,rng::Rand};
use std::collections::BTreeMap;
pub const FRAME_FLOATS:usize=20;
pub const STATE_FLOATS:usize=16;
pub const HIT_FLOATS:usize=16;
pub const PLAYER:u32=1;pub const FIGHT:u32=2;pub const LOCK:u32=4;pub const PHYSICAL:u32=8;
pub const DEAD:u32=16;
// Physical ownership does not mean lying down. A balanced physical reactor
// can lease clip motor targets while its real body remains the hit geometry.
pub const DRIVEN:u32=32;pub const DOWN:u32=64;
// Native SA may decide to flee while a blow is still being resolved. Finish
// the original pending/given response, then return ownership; no timer/stun.
pub const EXIT_INTENT:u32=128;
// Explicit controlled fixtures only: retains AI reactions and stance but makes
// no autonomous attack choice. The live host never sets this outside a probe.
pub const PROBE_PASSIVE:u32=256;
fn club_weapon(club:bool)->WeaponView{WeaponView{fire:"MELEE",name:if club{"BASEBALLBAT"}else{"UNARMED"},damage_type:"MELEE",slot:if club{"MELEE"}else{"UNARMED"},held:Held{kind:club as u32,flags:if club{1<<9}else{0},between_shots:0}}}
// A held blade (SA knife / katana) is IV's KNIFE: type 3, weaponinfo MELEE_BLADE (+0x20 bit 10), the action table's `knife` class.
fn weapon(club:bool,blade:bool)->WeaponView{if club&&blade{WeaponView{fire:"MELEE",name:"KNIFE",damage_type:"MELEE",slot:"MELEE",held:Held{kind:3,flags:1<<10,between_shots:0}}}else{club_weapon(club)}}
pub struct Actor{
    pub rig:Rig,pub player:bool,pub me:Me,pub pad:Pad,pub melee:Melee,
    pub frame:[f32;FRAME_FLOATS],pub native:Vec<Pose>,pub pose:Vec<Pose>,pub state:[f32;STATE_FLOATS],
    flags:u32,target:u32,heading:f32,flags_240:u32,phase:f32,wait:Option<f32>,blend:f32,
    previous:Option<usize>,under:Vec<Pose>,last_seen:u32,pub key:String,waiting:bool,
    drift:Option<melee_task::Drift>,
    alternate_recoil_target:Option<u32>, alternate_hook_target:Option<u32>,
    gym_contact:Option<u32>,paired_gym:Option<(u32,usize)>,
    pub decision:crate::npc_decision::NpcDecision,decision_input:Option<(u32,crate::npc_decision::Input)>,bravery:u8,
    club:Option<(Vec3,Vec3,f32,f32)>,
    /// The held weapon is a blade (`feed_weapon`): IV KNIFE instead of BASEBALLBAT.
    blade:bool,
    /// This actor's DisarmOpponent has taken its foe's weapon and the host is swapping it into this actor's hands: the
    /// weapon change must not restart the disarm's own clip (`feed_club`). Lapses with the move.
    taking:bool,
}

#[cfg(test)]mod health_order_regressions{
    use super::*;use crate::melee_health::{self as h,Boundary};use lsc4_combat::{ped,actiontable::{Action,ResultDef}};
    struct Host{health:ped::Health,factor:f32,hat:bool,proof:u32,accept:bool,revision:u64,receipts:Vec<h::Receipt>}
    impl Boundary for Host{
        fn prepare(&mut self,q:h::Request)->Option<h::Facts>{Some(h::Facts{generation:2,birth:3,revision:self.revision,source_generation:1,source_birth:4,
            health:self.health,victim:ped::Victim{factor:self.factor,hat:self.hat,flags:self.proof,player:q.victim_player,..Default::default()},source_group:Some(0),friend:false,same_group:false,floor:false,source_owned_by_victim:false,complete:true,decision:None})}
        fn commit(&mut self,r:&h::Receipt)->Result<bool,String>{
            assert!(r.selection.is_some(),"task storage cannot be reserved before the source selector");
            if !self.accept{return Ok(false)}self.health=r.after;self.revision+=1;self.receipts.push(*r);Ok(true)
        }
    }
    fn fixture(initial:f32,guard:bool)->(Runtime,Data,Host){
        static INIT:std::sync::Once=std::sync::Once::new();let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        INIT.call_once(||assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(root.join("game/DeliriumSA/fred.xrt")).unwrap()))));
        let dir=root.join("game/DeliriumSA/combat");let read=|n|std::fs::read(dir.join(n)).unwrap();
        let mut d=Data::load(&read("meleeanims.dat"),&read("action_table.csv"),&read("pedpersonality.dat")).unwrap();
        // Contacts are controlled leaves here. A table with no compatible
        // reaction proves the source Combat1900 + post-health branch itself.
        d.table.actions.clear();d.table.results=vec![ResultDef{name:1,anim_group:-1,anim:-1,..Default::default()},ResultDef{name:2,is_a_block:true,anim_group:-1,anim:-1,..Default::default()}];
        let bank=Bank::load(&dir).unwrap();let mut r=Runtime::new(bank);
        r.load_health_weapons(&std::fs::read_to_string(root.join("tools/mod-staging/original-health-20261101/weaponinfo.xml")).unwrap()).unwrap();
        let rig=Rig::from_json(&std::fs::read(root.join("research/combat-contact-20261031/inputs/f9200d93a5301c0db03dddf21256f0a959766e9c695a6caf25da4ec435e76010-combat-rig-7.json")).unwrap()).unwrap();
        r.register(&d,1,true,"player",rig.clone()).unwrap();r.register(&d,2,false,"ig_roman",rig).unwrap();r.now=100;r.rng=Rand::new(1);
        for (id,a) in &mut r.actors{a.flags=FIGHT|LOCK;a.frame[4]=if *id==2{initial}else{200.};a.target=if *id==2{1}else{2};a.last_seen=100;a.melee.on=true;
            ct::play_result(&mut a.melee.mv,&d,&Action{move_:if *id==2&&guard{2}else{1},..Default::default()},a.player,&|_|None);}
        let dm=r.bank.npc_decision.as_ref().unwrap();let input=crate::npc_decision::Input{flags:crate::npc_decision::SUPPORTED|crate::npc_decision::ENGAGEMENT|crate::npc_decision::POPULATION|crate::npc_decision::TARGET_PLAYER,target:Some(1),target_health:200.,distance:1.2,line:1,..Default::default()};
        r.actors.get_mut(&2).unwrap().decision.step(dm,input,initial,4,false,0.016,&mut Rand::new(8));
        let host=Host{health:ped::Health{health:initial,max:200.,armour:50.},factor:1.,hat:false,proof:0,accept:true,revision:1,receipts:vec![]};(r,d,host)
    }
    fn hit(r:&mut Runtime,d:&Data,h:Option<&mut dyn Boundary>,amount:f32,bone:u16){
        let pads=r.actors.iter().map(|(&id,a)|(id,a.pad.clone())).collect();
        r.resolve_contacts(d,&pads,vec![(1,2,bone,Vec3::Z,amount,0x4d0,Vec3::X,0.)],h).unwrap();
    }
    #[test]fn no_original_decision_cannot_start_a_host_melee_but_keeps_a_received_action(){
        for given in [false,true]{
            let(mut r,_,_)=fixture(200.,false);
            let dir=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../game/DeliriumSA/combat");let read=|n|std::fs::read(dir.join(n)).unwrap();
            let d=Data::load(&read("meleeanims.dat"),&read("action_table.csv"),&read("pedpersonality.dat")).unwrap();
            r.actors.get_mut(&1).unwrap().melee=Melee::default();
            let a=r.actors.get_mut(&2).unwrap();a.decision.clear();a.melee=Melee::default();
            if given{a.melee.on=true;ct::play_result(&mut a.melee.mv,&d,&Action{move_:crate::hash("Hit_Hook_L_Res"),..Default::default()},false,&|k|r.bank.clips.get(k).map(|c|c.duration));let m=a.melee.mv.as_mut().unwrap();assert!(m.key.is_some());m.given=true;}
            let input=crate::npc_decision::Input{flags:crate::npc_decision::SUPPORTED|crate::npc_decision::POPULATION|crate::npc_decision::TARGET_PLAYER,target:Some(1),target_health:200.,distance:1.2,line:1,..Default::default()};
            a.decision_input=Some((100,input));for a in r.actors.values_mut(){a.pad.feed(0,[127,127],[0,0],90);a.pad.feed(0,[127,127],[0,0],100);}r.advance(&d,100,0.016).unwrap();let a=&r.actors[&2];
            assert!(!a.decision.active());assert_eq!(a.state[15],0.);assert!(!a.me.go);
            if given{assert!(a.receiving());assert!(a.state[0]>0.);}else{assert_eq!(a.state[0],0.);assert!(!a.melee.on);}
        }
    }
    #[test]fn real_dispatch_uses_post_damage_health_and_current_guard(){
        let(mut old,d,mut h)=fixture(140.,false);hit(&mut old,&d,None,20.,0x36a1);assert_eq!(old.hits[0][7],0.);assert_eq!(old.actors[&2].frame[4],140.);
        let(mut fixed,d,_)=fixture(140.,false);hit(&mut fixed,&d,Some(&mut h),20.,0x36a1);
        assert_eq!(fixed.actors[&2].frame[4],120.);assert_eq!(fixed.hits[0][7],1.);assert_eq!(h.health.armour,50.);assert!(!h.receipts[0].selection.unwrap().pending);
        let(mut guarded,d,mut h)=fixture(140.,true);hit(&mut guarded,&d,Some(&mut h),20.,0x36a1);
        assert_eq!(guarded.actors[&2].frame[4],130.);assert_eq!(guarded.hits[0][7],0.);assert_eq!(guarded.hits[0][8],-2.);assert!(h.receipts[0].selection.unwrap().pending);
    }
    #[test]fn head_ko_rng_precedes_selector_but_rejected_owner_does_not_publish_it(){
        let(mut r,d,mut h)=fixture(200.,false);hit(&mut r,&d,Some(&mut h),20.,0x4b5);
        assert_eq!(r.actors[&2].frame[4],99.);assert_eq!(r.hits[0][7],1.);assert_eq!(h.receipts[0].outcome.flags,0xd4);
        let accepted_seed=r.rng.seed;assert_ne!(accepted_seed,1);
        let(mut rejected,d,mut h)=fixture(200.,false);h.accept=false;hit(&mut rejected,&d,Some(&mut h),20.,0x4b5);
        let(mut legacy,d,_)=fixture(200.,false);hit(&mut legacy,&d,None,20.,0x4b5);
        assert_eq!(rejected.rng.seed,legacy.rng.seed);assert_eq!(rejected.hits,legacy.hits);assert!(rejected.health_receipts.is_empty());assert_eq!(h.health.health,200.);
    }
    #[test]fn two_hits_same_frame_use_owner_revision_and_do_not_force_raw_damage_fatal(){
        let(mut r,d,mut h)=fixture(160.,false);hit(&mut r,&d,Some(&mut h),20.,0x36a1);hit(&mut r,&d,Some(&mut h),20.,0x36a1);
        assert_eq!(r.hits.iter().map(|h|h[7]).collect::<Vec<_>>(),vec![0.,1.]);assert_eq!(h.receipts.iter().map(|r|r.facts.revision).collect::<Vec<_>>(),vec![1,2]);assert_eq!(h.health.health,120.);
        let(mut r,d,mut h)=fixture(200.,true);h.factor=0.1;hit(&mut r,&d,Some(&mut h),250.,0x36a1);
        assert_eq!(h.health.health,187.5);assert_eq!(r.hits[0][7],0.,"raw authored damage cannot predict death before source guard/defence");assert!(r.actors[&2].melee.request.is_some());
    }
}
impl Actor {
    pub fn receiving(&self)->bool{self.melee.request.is_some()||self.melee.mv.as_ref().is_some_and(|m|m.given)}
    fn pos(&self)->Vec3{Vec3::from_slice(&self.frame)}
    fn at(&self,id:u16)->Option<Vec3>{let pose=if self.flags&PHYSICAL!=0{&self.native}else{&self.pose};self.rig.ids.iter().position(|&s|iv_bone(s)==Some(id)).map(|i|pose[i].p).or_else(||self.rig.parts.iter().find(|p|p.tag==id).map(|p|p.frame(pose).p))}
    fn lies(&self,bank:&Bank)->u8{
        let pose=if self.flags&PHYSICAL!=0{&self.native}else{&self.pose};
        let Some(q)=bank.original_axes(&self.rig,pose,2)else{return 4};
        let spine=self.rig.parts.iter().find(|p|p.tag==0x4b2).map(|p|p.frame(pose).p.z)
            .or_else(||self.at(0x1a1).map(|p|p.z));
        let Some(z)=spine else{return 4};let x=(q*Vec3::X).z;let y=(q*Vec3::Y).z;
        if self.flags&PHYSICAL!=0{if let (Some(l),Some(r))=(self.at(0x1a4),self.at(0x1a9)){return lsc4_combat::fall::lies_on_ragdoll(x,y,z,[l.z,r.z]);}}
        lsc4_combat::fall::lies(x,y,z,self.pos().z)
    }
    fn foe(&self,d:&Data,bank:&Bank)->Foe{
        let m=self.melee.mv.as_ref();
        Foe{player:self.player,feet:self.pos()-Vec3::Z,heading:self.heading,health:self.frame[4],lies:self.lies(bank),
            me:(!self.player).then_some(self.me),bones:ct::POINT_BONES.map(|id|self.at(id)),
            task:m.map(|m|(d.reaction_identity(d.table.results[m.result].name),Some((m.clip_t,m.step)))),
            block:m.is_some_and(|m|d.table.results[m.result].is_a_block),velocity:Vec3::from_slice(&self.frame[12..]),
            flags_240:self.flags_240,from_idle:self.melee.from_idle,on_stairs:false,weapon:Some(weapon(self.club.is_some(),self.blade).held),struck:m.is_some_and(|m|m.struck),
            frame_box:ct::frag_box(self.pos()-Vec3::Z,self.heading,self.capsules().iter().map(|&(a,b,r,_)|(a,b,r))),}
    }
    fn capsules(&self)->Vec<(Vec3,Vec3,f32,u16)>{
        let pose=if self.flags&PHYSICAL!=0{&self.native}else{&self.pose};
        self.rig.parts.iter().map(|p|p.capsule(pose)).collect()
    }
}
pub struct Runtime {pub bank:Bank,pub actors:BTreeMap<u32,Actor>,pub hits:Vec<[f32;HIT_FLOATS]>,pub contact_gaps:BTreeMap<(u32,u32),f32>,rng:Rand,pub now:u32,pending_contacts:bool,pub view_front:Option<Vec2>,
    health_weapon:Option<lsc4_combat::weapons::Weapon>,health_decisions:Option<lsc4_combat::decision::Store>,pub health_receipts:BTreeMap<usize,crate::melee_health::Receipt>}
impl Runtime {
    /// Host has accepted real damage from an on-foot ped, before a new SA flee
    /// can take ownership. Existing native panic/unknown roles are not admitted.
    pub fn civilian_harm(&mut self,id:u32,source:u32,health:f32,flags:u32)->Result<i32,String>{
        if !health.is_finite()||health<=0.||flags&!7!=0||id==source{return Err("invalid civilian harm facts".into())}
        let a=self.actors.get_mut(&id).ok_or("unregistered civilian")?;
        if a.player||a.decision.active(){return Ok(0)}
        let events=self.bank.civilian_events.as_ref().ok_or("missing original civilian event files")?;
        let column=if flags&2!=0{3}else if flags&4!=0{2}else if flags&1!=0{1}else{0};
        let code=events.harmed(health,column,&mut self.rng);
        // This boundary supports the actual ambient file's Combat1900. Other
        // event answers remain explicit, never replaced by a fight/flee guess.
        if code==1900{a.decision.damage_combat(source,&mut self.rng);}
        Ok(code)
    }
    pub fn set_view(&mut self,x:f32,y:f32)->Result<(),String>{
        let v=Vec2::new(x,y);
        if !v.is_finite()||v.length_squared()<1e-8{self.view_front=None;return Err("invalid combat view".into())}
        self.view_front=Some(v.normalize());Ok(())
    }
    /// Passive selector evidence for the explicit gym fixture. No state changes.
    pub fn gym_diagnostics(&self,d:&Data,id:u32)->Option<String>{
        let a=self.actors.get(&id)?;let m=a.melee.mv.as_ref()?;
        if !a.key.starts_with("sa_gym_"){return None}
        let other=self.actors.get(&a.target)?;let foe=other.foe(d,&self.bank);
        let branches:Vec<_>=d.table.results[m.result].branches.iter().filter(|b|b.0).filter_map(|b|{
            let action=d.table.actions.iter().find(|x|x.name==b.1)?;
            let point=Vec3::from(foe.point(action.rel_point,a.pos().into())?)-a.pos();
            Some(serde_json::json!({"action":action.name,"window":b.2,"relative_point":point.to_array(),"distance":point.truncate().length(),"height_range":action.height_range,"distance_range":action.distance_range,"angle":wrap((-point.x).atan2(point.y)-a.heading),"angle_range":action.angle_range,"impulse":lsc4_combat::melee::impulses_hold(action,Some(&a.pad.controls),0)}))
        }).collect();
        Some(serde_json::json!({"time":self.now,"clip":a.key,"phase":m.clip_t,"struck":m.struck,"target":a.target,"target_clip":other.key,"target_flags":other.flags,"target_lies":foe.lies,"root_distance":a.pos().distance(other.pos()),"accepted_knee":a.gym_contact,"paired":a.paired_gym,"branches":branches}).to_string())
    }
    pub fn new(bank:Bank)->Self{Self{bank,actors:BTreeMap::new(),hits:vec![],contact_gaps:BTreeMap::new(),rng:Rand::new(0x53414956),now:0,pending_contacts:false,view_front:None,health_weapon:None,health_decisions:None,health_receipts:BTreeMap::new()}}
    /// Explicit source weapon metadata, never inferred from SA weapon slots.
    pub fn load_health_weapons(&mut self,xml:&str)->Result<(),String>{
        let rows=lsc4_combat::weapons::read(xml);let found:Vec<_>=rows.into_iter().filter(|w|w.name=="UNARMED").collect();
        if found.len()!=1{return Err("source weaponinfo must contain one UNARMED row".into())}
        let w=found.into_iter().next().unwrap();
        if w.fire!="MELEE"||w.damage_type!="MELEE"||w.slot!="UNARMED"{return Err("source UNARMED metadata is incompatible".into())}
        self.health_weapon=Some(w);Ok(())
    }
    pub fn load_health_decisions(&mut self,path:&std::path::Path)->Result<(),String>{
        let store=lsc4_combat::decision::Store::load(|name|std::fs::read_to_string(path.join(name)).ok())?;
        self.health_decisions=Some(store);Ok(())
    }
    pub fn register(&mut self,d:&Data,id:u32,player:bool,profile:&str,rig:Rig)->Result<(),String>{
        let p=d.profile(if player{"player"}else{profile})?;
        let me=Me{attack:p.personality.attack,defend:p.personality.defend,martial:p.personality.melee_percent,groups:p.groups,go:false,
            moves:d.groups.iter().position(|g|g.name.eq_ignore_ascii_case(&p.personality.move_group)).map_or(-1,|i|i as i32)};
        let pose=rig.bind.clone();self.actors.insert(id,Actor{rig,player,me,pad:Pad::default(),melee:Melee::default(),frame:[0.;FRAME_FLOATS],
            native:pose.clone(),pose:pose.clone(),state:[0.;STATE_FLOATS],flags:0,target:u32::MAX,heading:0.,flags_240:0,phase:0.,wait:None,blend:0.,previous:None,under:pose,last_seen:0,key:String::new(),waiting:true,drift:None,alternate_recoil_target:None,alternate_hook_target:None,gym_contact:None,paired_gym:None,decision:Default::default(),decision_input:None,bravery:p.personality.bravery,club:None,blade:false,taking:false});Ok(())
    }
    pub fn feed_club(&mut self,id:u32,bounds:Option<&[f32]>)->Result<(),String>{self.feed_weapon(id,bounds,false)}
    pub fn feed_weapon(&mut self,id:u32,bounds:Option<&[f32]>,blade:bool)->Result<(),String>{
        let a=self.actors.get_mut(&id).ok_or("unregistered fighter")?;
        let club=match bounds {
            Some(b) if b.len()==8&&b.iter().all(|v|v.is_finite())&&b[6]>0.&&(0.0..=1.0).contains(&b[7])=>
                Some((Vec3::from_slice(b),Vec3::from_slice(&b[3..]),b[6],b[7])),
            None=>None,
            _=>return Err("invalid club bounds".into()),
        };
        if a.melee.mv.is_none(){a.taking=false;}
        let blade=blade&&club.is_some();
        if (a.club!=club||a.blade!=blade)&&!a.receiving()&&!a.taking{a.melee=Melee::default();a.previous=None;a.wait=None;}
        a.club=club;a.blade=blade;Ok(())
    }
    pub fn feed(&mut self,id:u32,f:&[f32],pose:&[f32],now:u32)->Result<(),String>{
        let a=self.actors.get_mut(&id).ok_or("unregistered fighter")?;
        if f.len()!=FRAME_FLOATS||f.iter().any(|x|!x.is_finite())||pose.len()!=a.rig.ids.len()*7||pose.iter().any(|x|!x.is_finite()) {return Err("invalid fighter frame".into())}
        a.frame.copy_from_slice(f);a.flags=f[5] as u32;a.target=if f[6]<0.{u32::MAX}else{f[6] as u32};a.heading=f[3];a.last_seen=now;
        // Native SA personality overrides the default IV archetype's scalar
        // filters. The host still applies victim weakness/armour exactly once.
        if !a.player&&f[18]==1.&&f[16]>0.&&f[17]>0.{a.me.attack=f[16];a.me.defend=f[17];}
        a.native=pose.chunks_exact(7).map(Pose::read).collect();
        if !a.melee.on||a.flags&PHYSICAL!=0 {a.pose=a.native.clone();}
        a.pad.feed(f[7] as u32,[f[8] as u8,f[9] as u8],[f[10] as u8,f[11] as u8],now);Ok(())
    }
    pub fn step(&mut self,d:&Data,now:u32,dt:f32)->Result<(),String>{
        self.advance(d,now,dt)?;self.finish(d)
    }
    pub fn feed_decision(&mut self,id:u32,now:u32,values:&[f32])->Result<(),String>{
        let input=crate::npc_decision::Input::read(values)?;
        let a=self.actors.get_mut(&id).ok_or("unregistered decision actor")?;
        if a.player{return Err("NPC decision facts supplied for player".into())}
        a.decision_input=Some((now,input));Ok(())
    }
    /// Host order: advance clips, resolve native locomotion/world collision,
    /// place each actor at its resolved root, then finish the strike queries.
    pub fn advance(&mut self,d:&Data,now:u32,dt:f32)->Result<(),String>{
        self.pending_contacts=false;
        if !dt.is_finite()||dt<=0.||dt>0.1{return Err("invalid combat timestep".into())}
        if self.actors.values().any(|a|a.last_seen==now&&a.rig.parts.len()!=21){return Err("fighter has no fitted contact geometry".into())}
        self.now=now;self.hits.clear();self.health_receipts.clear();self.contact_gaps.clear();
        // Paired gym actions need the same live identities throughout. A
        // retarget, death or physical interruption cannot leave SelfDamage
        // running on the old victim after the attacker has left the pair.
        let abandoned:Vec<_>=self.actors.iter().filter_map(|(&id,a)|{
            let (source,stage)=a.paired_gym?;
            let valid=self.actors.get(&source).is_some_and(|s|s.last_seen==now&&s.target==id&&s.flags&(DEAD|PHYSICAL|DOWN)==0&&
                s.melee.mv.as_ref().and_then(|m|m.key.as_deref()).is_some_and(|k|k==format!("sa_gym_7/attack{stage}")||stage==2&&k=="sa_gym_7/attack3"));
            (!valid).then_some(id)
        }).collect();
        for id in abandoned{let a=self.actors.get_mut(&id).unwrap();a.melee.mv=None;a.melee.request=None;a.paired_gym=None;}
        // Snapshot all foes before mutating controllers. Enumeration order cannot
        // give the later actor an extra frame of input or advance the first twice.
        let foes:BTreeMap<_,_>=self.actors.iter().filter(|(_,a)|a.last_seen==now).map(|(&id,a)|(id,a.foe(d,&self.bank))).collect();
        let targets:BTreeMap<_,_>=self.actors.iter().map(|(&id,a)|(id,a.target)).collect();
        let mut turn_state:BTreeMap<_,_>=self.actors.iter().map(|(&id,a)|(id,(a.waiting,a.frame[15]>0.))).collect();
        // Snapshot input histories for reciprocal dodge/counter tests. Retain
        // the live histories even if an asset error aborts this frame.
        let pads:BTreeMap<_,_>=self.actors.iter().map(|(&id,a)|(id,a.pad.clone())).collect();
        let tasks:BTreeMap<_,_>=self.actors.iter().map(|(&id,a)|(id,(a.melee.on,a.flags&LOCK!=0,a.melee.mv.as_ref().is_some_and(|m|m.given)))).collect();
        for (&id,a) in &mut self.actors {
            a.state=[0.;STATE_FLOATS];a.state[11]=-1.;a.state[13]=-1.;
            let driven=a.flags&(PHYSICAL|DRIVEN)==(PHYSICAL|DRIVEN)&&a.flags&DOWN==0;
            if !a.player {
                if let (Some(dm),Some((stamp,input)))=(&self.bank.npc_decision,a.decision_input){
                    if stamp==now&&a.last_seen==now {
                        let protected=a.receiving()||a.flags&PHYSICAL!=0;
                        let code=a.decision.step(dm,input,a.frame[4],a.bravery,protected,dt,&mut self.rng);
                        a.state[15]=code as f32;
                        if code==lsc4_combat::combat::CHARGE&&a.flags&FIGHT!=0 {a.flags&=!EXIT_INTENT;}
                        else if matches!(code,0|lsc4_combat::combat::RETREAT|lsc4_combat::combat::NONE) {a.flags|=EXIT_INTENT;}
                        else if code==lsc4_combat::combat::FIRE {a.flags&=!EXIT_INTENT;}
                    }else{a.decision.clear();}
                }
            }
            if a.last_seen!=now||a.flags&DEAD!=0||(a.flags&PHYSICAL!=0&&!driven)||(a.flags&FIGHT==0&&!a.receiving())||
                (a.flags&EXIT_INTENT!=0&&!a.receiving()) {a.melee=Melee::default();a.pose=a.native.clone();a.blend=0.;a.previous=None;a.drift=None;continue}
            let foe=foes.get(&a.target).copied();
            if !a.player&&foe.is_none()&&!a.receiving(){a.melee=Melee::default();continue}
            if let Some(t)=&mut a.wait{*t-=dt;}
            let distance=foe.map_or(f32::INFINITY,|f|(f.feet+Vec3::Z-a.pos()).length());
            let rivals:Vec<_>=targets.iter().filter(|(other,target)|**other!=id&&**target==a.target&&foes.get(other).is_some_and(|f|!f.player)).filter_map(|(&other,_)|Some((other,melee_task::Rival{d2:foes.get(&other)?.feet.distance_squared(foe?.feet),waiting:turn_state[&other].0,gang:turn_state[&other].1}))).collect();
            let turns=melee_task::take_turns(turn_state[&id].0,turn_state[&id].1,distance*distance,&rivals.iter().map(|r|r.1).collect::<Vec<_>>());
            let waiting=!a.player&&turns.waiting;a.waiting=waiting;turn_state.get_mut(&id).unwrap().0=waiting;
            if let Some(relieved)=turns.relieve{turn_state.get_mut(&rivals[relieved].0).unwrap().0=true;}
            let second=turns.second.and_then(|i|foes.get(&rivals[i].0)).map(|f|(f.feet+Vec3::Z).into());
            a.me.go=!driven&&!waiting&&a.decision.code!=lsc4_combat::combat::FIRE&&a.flags&(EXIT_INTENT|PROBE_PASSIVE)==0&&melee_task::wants_move(a.wait,distance,a.melee.mv.is_some(),self.rng.float_in_range(0.,1.),dt);
            // A blow requests a reaction on the receiver's NEXT task step. Read
            // the attacker's current result then, not the stale impact frame.
            // Stable pool identities prevent a removed/reused actor inheriting it.
            let request_by=a.melee.request.as_ref().and_then(|q|q.from);
            let blocking=a.melee.mv.as_ref().is_some_and(|m|d.table.results[m.result].is_a_block);
            if let Some(q)=a.melee.request.as_mut(){
                if let Some(by)=q.from.and_then(|id|foes.get(&id)){
                    q.blow.by=*by;q.locked=a.flags&LOCK!=0&&q.from==Some(a.target);
                    let momentum=d.result(q.blow.res).map_or(0.,|r|r.applied_ped_move_momentum);
                    if let Some(drift)=melee_task::Drift::start(q.way,true,blocking,momentum){a.drift=Some(drift);}
                }else{a.melee.request=None;}
            }
            let foe_task=foe.filter(|f|f.player).and_then(|f|{
                let &(on,lock,given)=tasks.get(&a.target)?;if !on{return None}
                let target=match targets.get(&a.target){Some(&t) if t==id=>ct::TaskTarget::Me,Some(t)=>foes.get(t).copied().map_or(ct::TaskTarget::Nobody,ct::TaskTarget::Ped),None=>ct::TaskTarget::Nobody};
                Some(ct::FoeTask{target,lock,given,block:f.block,struck:f.struck})
            });
            let w=weapon(a.club.is_some(),a.blade);let ctx=ct::Ctx{pad:&pads[&id],pos:a.pos(),health:a.frame[4],weapon:&w,foe,me:(!a.player).then_some(a.me),lock_on:a.flags&LOCK!=0,
                flags_240:a.flags_240,on_stairs:false,bones:ct::POINT_BONES.map(|b|a.at(b)),lies:a.lies(&self.bank),foe_pad:foe.filter(|f|f.player).and_then(|_|pads.get(&a.target)),foe_task,in_hand:true};
            a.melee.step(d,&ctx,&mut a.heading,dt,&|k|self.bank.clips.get(k),&mut||self.rng.next());
            a.state[12]=a.melee.took as u8 as f32;a.state[13]=request_by.map_or(-1.,|id|id as f32);
            if a.melee.took {a.wait=Some(melee_task::wait(&mut self.rng));if !a.player&&a.flags&LOCK!=0{if let Some(by)=request_by{a.target=by;a.state[11]=by as f32;}}}
            if a.player&&a.club.is_none() {
                let result=a.melee.mv.as_ref().map(|m|m.result);
                match result.map(|i|d.table.results[i].name) {
                    Some(name) if name==hash("cross_R_recoil_Res") => {
                        if result!=a.previous {a.alternate_recoil_target=Some(a.target);}
                        a.alternate_hook_target=None;
                    }
                    Some(name) if name==hash("hook_L_Res") => {
                        if let Some(target)=a.alternate_recoil_target.take() {
                            a.alternate_hook_target=(target==a.target).then_some(target);
                        }
                        if a.alternate_hook_target!=Some(a.target){a.alternate_hook_target=None;}
                    }
                    _ => {a.alternate_recoil_target=None;a.alternate_hook_target=None;}
                }
            }
            if !a.melee.on{a.pose=a.native.clone();continue}
            if a.melee.started{a.wait=Some(melee_task::wait(&mut self.rng));}
            if a.melee.mv.is_none(){if let Some(f)=foe{let v=f.feet+Vec3::Z-a.pos();let goal=(-v.x).atan2(v.y);a.heading+=wrap(goal-a.heading).clamp(-melee_task::TURN_SPEED*dt,melee_task::TURN_SPEED*dt);}}
            let yaw=Quat::from_rotation_z(a.heading);let world=Pose{p:a.pos(),q:yaw};
            let mut ratios=if a.player{((a.frame[8]-127.)/127.,-(a.frame[9]-127.)/127.)}
                else{foe.map_or((0.,0.),|f|melee_task::stance(a.pos().into(),(yaw*Vec3::X).into(),(yaw*Vec3::Y).into(),(f.feet+Vec3::Z).into(),None,waiting,second))};
            if a.player&&a.flags&LOCK!=0{
                let front=self.view_front.unwrap_or(Vec2::new(-a.heading.sin(),a.heading.cos()));
                let stick=crate::stance::locked_input(Vec2::new(ratios.0,ratios.1),front,a.heading);ratios=(stick.x,stick.y);
            }
            if let Some(drift)=&mut a.drift {if let Some(r)=drift.ratios((yaw*Vec3::X).into(),(yaw*Vec3::Y).into()){ratios=r;}drift.step(dt);}
            // 822C5100 preserves the distinction made at task construction:
            // a FromOnFoot one-shot is not the fighting movement blend.
            let standalone=a.player&&!a.melee.fighting&&!driven;
            let stance_frame=if standalone{crate::stance::Frame{pose:a.native.iter().map(|&p|world.inverse().mul(p)).collect(),velocity:Vec2::ZERO,key:String::new(),phase:a.phase,flags:0}}
                else{
                    let forward_gain=if a.player&&!driven&&a.melee.mv.is_none(){1.25}else{1.0};
                    crate::stance::step(&self.bank,&a.rig,Vec2::new(ratios.0,ratios.1),a.phase,dt,forward_gain)?
                };
            a.phase=stance_frame.phase;let stance=stance_frame.key;let flags=stance_frame.flags;let base=stance_frame.pose;
            let mut local=base.clone();let mut velocity=stance_frame.velocity;let mut rate=1.;let mut applied_flags=flags;
            let current=a.melee.mv.as_ref().map(|m|m.result);
            if current!=a.previous {a.under=a.pose.iter().map(|&p|world.inverse().mul(p)).collect();a.blend=0.;a.previous=current;
                if !a.melee.mv.as_ref().and_then(|m|m.key.as_deref()).is_some_and(|k|matches!(k,"sa_gym_7/attack2"|"sa_gym_7/attack3")){a.gym_contact=None;}
                if !a.melee.mv.as_ref().and_then(|m|m.key.as_deref()).is_some_and(|k|matches!(k,"sa_gym_7/hit2"|"sa_gym_7/hit3")){a.paired_gym=None;}
            }
            a.key=stance.clone();
            if let Some(m)=&a.melee.mv {let r=&d.table.results[m.result];
                if let Some((key,flags))=d.clip(r){if let Some(clip)=self.bank.clips.get(&key){
                    let target=self.bank.retarget(&a.rig,&key,m.phase(),flags).ok_or("result sampling failed")?;
                    // IV steps its blend once per 30 Hz frame, so an "instant" factor (the hit pool's 1000) shows
                    // the new clip one 33 ms frame later. Spread that same 33 ms at SA's frame rate rather than
                    // popping the body in one 8 ms frame; slower factors are below the cap and unchanged.
                    a.blend=(a.blend+r.blend_in.min(30.)*dt).clamp(0.,1.);
                    let posed=a.rig.blend(&base,&target,1.,flags);local=a.rig.blend(&a.under,&posed,a.blend,0);
                    let prev=((m.clip_t-m.step)/m.dur).clamp(0.,1.);
                    let delta=(mover(clip,m.phase())-mover(clip,prev))/dt;
                    if flags&0x200!=0{velocity.x=delta.x}if flags&0x400!=0{velocity.y=delta.y}
                    if flags&0x3000!=0{let q=mover_q(clip,m.phase())*mover_q(clip,prev).inverse();a.heading+=2.*q.z.atan2(q.w);}
                    rate=m.rate;applied_flags=flags;a.key=key;
                }}
            }else{a.blend=(a.blend+2.*dt).clamp(0.,1.);if a.under.len()==local.len(){local=a.rig.blend(&a.under,&local,a.blend,0);}}
            if let Some(home)=a.melee.home {
                let travel=a.melee.mv.as_ref().and_then(|m|m.key.as_ref().and_then(|k|self.bank.clips.get(k).map(|c|mover(c,m.phase()).y-mover(c,0.).y))).unwrap_or(0.);
                // Use the original 30 Hz approach budget for club swings, the
                // alternate follow-up and every AI strike. A shorter SA frame
                // should move the ped for less time, without also reducing its
                // approach speed: at 120 Hz an AI long hook closed a quarter of
                // the gap and stopped a hand short of the face.
                let club_swing = a.club.is_some() && a.melee.mv.as_ref().is_some_and(|m| {
                    let result = &d.table.results[m.result];
                    !m.given && result.strike_bones.iter().any(|&(active, tag, _)| active && tag == 0x4D0)
                });
                let ai_strike = !a.player && a.melee.mv.as_ref().is_some_and(|m| !m.given);
                let rate_step=if (a.alternate_hook_target.is_some()||club_swing||ai_strike)&&!home.push {dt.max(1./30.)} else {dt};
                if let Some(y)=home.speed(a.pos(),a.heading,velocity,travel,dt,rate_step){velocity.y=y}
            }
            let rendered_world=Pose{p:a.pos(),q:Quat::from_rotation_z(a.heading)};
            a.pose=local.into_iter().map(|p|rendered_world.mul(p)).collect();
            a.state[0]=if driven{2.}else if standalone{3.}else{1.};a.state[1]=a.heading;a.state[2]=velocity.x;a.state[3]=velocity.y;a.state[4]=current.map_or(-1.,|i|i as f32);
            a.state[5]=a.melee.mv.as_ref().map_or(0.,|m|m.clip_t);a.state[6]=rate;a.state[7]=a.melee.mv.as_ref().map_or(0.,|m|m.live[0] as u8 as f32+2.*m.live[1] as u8 as f32);
            a.state[8]=applied_flags as f32;a.state[9]=a.blend;a.state[10]=waiting as u8 as f32;
            a.state[14]=a.melee.mv.as_ref().is_some_and(|m|m.given) as u8 as f32;
        }
        for (id,(waiting,_)) in turn_state {self.actors.get_mut(&id).unwrap().waiting=waiting;}
        self.pending_contacts=true;Ok(())
    }
    /// Which axes the current authored result owns. Remaining axes belong to
    /// the ordinary on-foot movement; homing is also an authored forward owner.
    pub fn mover_axes(&self,id:u32)->u32{
        let Some(a)=self.actors.get(&id)else{return 0};
        let f=a.state[8]as u32;
        (if f&0x200!=0{1}else{0})|(if f&0x400!=0||a.melee.home.is_some(){2}else{0})|(if f&0x3000!=0{4}else{0})
    }
    /// Compose over the same frame's evaluated on-foot palette, without a second
    /// control/decision/clip step. Both drawing and subsequent strike queries use it.
    pub fn compose_standalone(&mut self,id:u32,root:&[f32],pose:&[f32])->Result<(),String>{
        if !self.pending_contacts||root.len()!=7||root.iter().chain(pose).any(|v|!v.is_finite()){return Err("invalid standalone composition phase".into())}
        let a=self.actors.get_mut(&id).ok_or("unregistered standalone actor")?;
        if a.state[0]!=3.||pose.len()!=a.rig.ids.len()*7{return Err("not a standalone palette owner".into())}
        let world=Pose::read(root);let inv=world.inverse();
        let base:Vec<_>=pose.chunks_exact(7).map(|p|inv.mul(Pose::read(p))).collect();
        let target=self.bank.retarget(&a.rig,&a.key,a.melee.mv.as_ref().map_or(0.,|m|m.phase()),a.state[8]as u32).ok_or("missing standalone clip")?;
        let posed=a.rig.blend(&base,&target,1.,a.state[8]as u32);
        let local=a.rig.blend(&a.under,&posed,a.blend,0);
        a.pose=local.into_iter().map(|p|world.mul(p)).collect();Ok(())
    }
    pub fn place(&mut self,id:u32,root:Vec3)->Result<(),String>{
        if !self.pending_contacts||!root.is_finite(){return Err("invalid combat placement phase".into())}
        let a=self.actors.get_mut(&id).ok_or("unregistered fighter")?;
        let delta=root-a.pos();a.frame[..3].copy_from_slice(&root.to_array());
        for pose in [&mut a.pose,&mut a.native]{for p in pose{p.p+=delta;}}
        Ok(())
    }
    /// Current native evaluation replaces only the passive under-pose. Does
    /// not run decisions, pad edges, animation clocks, or strike queries again.
    pub fn refresh_native_pose(&mut self,id:u32,pose:&[f32])->Result<(),String>{
        if !self.pending_contacts{return Err("native pose outside prepared combat frame".into())}
        let a=self.actors.get_mut(&id).ok_or("unregistered fighter")?;
        if pose.len()!=a.rig.ids.len()*7||pose.iter().any(|x|!x.is_finite()){return Err("invalid current native pose".into())}
        let current:Vec<_>=pose.chunks_exact(7).map(Pose::read).collect();
        a.native=current.clone();if a.state[0]==0.{a.pose=current;}Ok(())
    }
    pub fn finish(&mut self,d:&Data)->Result<(),String>{
        self.finish_with_health(d,None)
    }
    /// Original health precedes HitReaction. The callback may accept only a
    /// complete source task/accounting transaction. Rejected candidates leave
    /// RNG, health and the actor untouched and retain the explicit legacy path.
    pub fn finish_with_health(&mut self,d:&Data,health_host:Option<&mut dyn crate::melee_health::Boundary>)->Result<(),String>{
        if health_host.is_some()&&self.health_weapon.is_none(){return Err("original health weaponinfo is not loaded".into())}
        if !self.pending_contacts{return Err("combat contacts already resolved".into())}
        self.pending_contacts=false;let now=self.now;
        let pads:BTreeMap<_,_>=self.actors.iter().map(|(&id,a)|(id,a.pad.clone())).collect();
        // ForceImmediateReaction belongs to action construction, before the
        // authored strike window. It is not a second hit or a damage event.
        let forced:Vec<_>=self.actors.iter().filter_map(|(&id,a)|{
            let m=a.melee.mv.as_ref().filter(|m|m.forced)?;
            Some((id,a.target,ct::Blow{by:a.foe(d,&self.bank),player:a.player,res:d.table.results[m.result].name,clip:Some((m.clip_t,m.step))}))
        }).collect();
        for (id,target,blow) in forced {
            if let Some(m)=self.actors.get_mut(&id).and_then(|a|a.melee.mv.as_mut()){m.forced=false;}
            let gym=self.actors[&id].melee.mv.as_ref().and_then(|m|m.key.as_ref()).and_then(|k|self.bank.native.get(k)).filter(|c|c.style==7&&!c.receiver).map(|c|(c.step,c.reach));
            if let Some((stage,reach))=gym{
                let source=&self.actors[&id];
                // Selection already checked the initial grab window. Native
                // movement resolves before finish: reapplying that window here
                // rejects an accepted pair after a few mm of collision travel.
                // Retain the native technique's authored continuation envelope
                // and its actual contact/identity, never synthesize a new hit.
                let valid=self.actors.get(&target).is_some_and(|a|a.last_seen==now&&a.flags&(DEAD|PHYSICAL|DOWN)==0&&
                    source.pos().distance(a.pos())<=reach&&
                    if stage==2{source.gym_contact==Some(target)}else{a.paired_gym==Some((id,2))});
                if !valid{self.actors.get_mut(&id).unwrap().melee.mv=None;continue}
            }
            let Some(a)=self.actors.get_mut(&target).filter(|a|a.flags&(DOWN|DEAD)==0&&(a.flags&PHYSICAL==0||a.flags&DRIVEN!=0))else{continue};
            let result=ct::react(d,&a.foe(d,&self.bank),a.player,Some(&weapon(a.club.is_some(),a.blade)),a.melee.mv.as_ref(),&blow,false,[a.melee.on,false],ct::Pick::Forced{pad:a.player.then_some(&pads[&target].controls)},&mut||self.rng.next());
            if let Some(action)=result.action {
                ct::play_result(&mut a.melee.mv,d,action,a.player,&|k|self.bank.clips.get(k).map(|c|c.duration));
                if let Some(m)=a.melee.mv.as_mut(){m.given=true;m.swing=false;
                    if let Some((stage,_))=gym{m.clip_t=blow.clip.map_or(0.,|c|c.0);a.paired_gym=Some((id,stage));}
                }
                // Match the original receiver's react_with lifecycle. The
                // paired reaction cannot inherit an outgoing swing, idle
                // provenance, request or AI wait from the interrupted attack.
                a.melee.on=true;a.melee.from_idle=false;a.melee.request=None;
                a.wait=None;a.waiting=false;a.drift=None;a.target=id;a.state[11]=id as f32;
                // ForceImmediateReaction starts the paired reaction task, not
                // a ragdoll. Preserve its footwork/homing and authored event
                // clock; the original dispatch chooses physical falls later.
                if let Some(m)=a.melee.mv.as_ref(){
                    let mut h=[0.;HIT_FLOATS];h[0]=id as f32;h[1]=target as f32;h[2]=0x36a1 as f32;
                    h[4..7].copy_from_slice(&a.at(0x36a1).unwrap_or(a.pos()).to_array());
                    h[7]=0.;h[8]=m.result as f32;h[15]=2.;self.hits.push(h);
                }
            }
        }
        // A dodge must close the opponent's homing window, otherwise its fist
        // tracks the dodging head and turns a successful evade into a hit.
        let effects:Vec<_>=self.actors.iter().filter(|(_,a)|a.state[0]>0.).map(|(&id,a)|(id,a.target,a.melee.special_fx,a.melee.stop_foe_homing)).collect();
        for (id,target,fx,stop) in effects {
            if let Some(a)=self.actors.get_mut(&id){a.flags_240=lsc4_combat::melee::SpecialEffect::apply(fx.me,a.flags_240);}
            if let Some(a)=self.actors.get_mut(&target){a.flags_240=lsc4_combat::melee::SpecialEffect::apply(fx.opponent,a.flags_240);if stop{a.melee.stop_homing();}}
        }
        // All output palettes precede all sweeps, including NPC-vs-NPC.
        let bodies:Vec<_>=self.actors.iter().filter(|(_,a)|a.last_seen==now).map(|(&id,a)|{let pose=if a.flags&PHYSICAL!=0{&a.native}else{&a.pose};(id,a.rig.parts.iter().map(|p|p.bound(pose)).collect::<Vec<_>>())}).collect();
        let mut contacts=vec![];
        for (&id,a) in &mut self.actors {if a.state[0]!=1.&&a.state[0]!=3.{continue}
            let parts:BTreeMap<_,_>=a.rig.parts.iter().map(|p|(p.tag as u32,p.axis(&a.pose))).collect();
            let others:Vec<_>=bodies.iter().filter(|b|b.0!=id).map(|(i,c)|(*i as u64,c.as_slice())).collect();
            let held=a.club.and_then(|(start,end,radius,origin)|{
                let hand=a.rig.ids.iter().position(|&tag|tag==24)?;
                let wrist=a.pose[hand];
                Some((wrist.p+wrist.q*start,wrist.p+wrist.q*end,radius,origin))
            });
            let strikes=a.melee.strike(d,a.pos()-Vec3::Z,a.heading,&|tag|parts.get(&tag).copied(),held,&others);
            let strength=if a.player{d.player.attack}else{a.me.attack};
            for (victim,part,p,damage,striker,normal,gap) in strikes{contacts.push((id,victim as u32,part,p,damage*strength,striker,normal,gap));}
        }
        self.resolve_contacts(d,&pads,contacts,health_host)?;
        // Counter-victim results carry their own damage at the original 0x200
        // event. They often have no striking limb, so a contact-only adapter
        // silently loses this damage. Distinguish it from a physical sweep.
        for (&id,a) in &self.actors {
            let Some(damage)=a.melee.self_damage.filter(|_|a.state[0]>0.)else{continue};
            if !self.actors.contains_key(&a.target){continue}
            let mut h=[0.;HIT_FLOATS];h[0]=a.target as f32;h[1]=id as f32;h[2]=0x36a1 as f32;h[3]=damage;
            h[4..7].copy_from_slice(&a.at(0x36a1).unwrap_or(a.pos()).to_array());
            // Original SelfDamage updates health; a living paired recipient
            // keeps its reaction. Death or an already physical body retains NM.
            let release=a.key=="sa_gym_7/hit3"&&a.paired_gym==Some((a.target,3));
            h[7]=(release||a.flags&(DOWN|DEAD|PHYSICAL)!=0) as u8 as f32;
            // SA's final paired marker releases the throw into the existing
            // measured-pose/inertia handoff, without a new strike impulse.
            h[8]=if release{-1.}else{a.state[4]};h[15]=1.;self.hits.push(h);
        }
        // DisarmOpponent (meleetask.rs 0x822BF448: "his target's weapon is his and it goes from the ped"): at the disarm
        // clip's 0x200 event, locked on its target (`Melee::disarm_foe`), a club-holding foe's weapon changes hands.
        // Hit kind 3 (h[15]): h[0] the taker, h[1] the foe; no damage, no reaction (that is the forced Hit_disarmed_*).
        // Last in the list so the health receipts' indices above are untouched. The host owns the SA weapon swap.
        let takes:Vec<_>=self.actors.iter().filter(|(_,a)|a.state[0]>0.&&a.melee.disarm_foe&&a.last_seen==now)
            .filter_map(|(&id,a)|{let v=self.actors.get(&a.target)?;(a.target!=id&&v.club.is_some()&&v.flags&DEAD==0&&v.last_seen==now).then_some((id,a.target,v.at(0x36a1).unwrap_or(v.pos())))}).collect();
        for (id,target,at) in takes{
            if let Some(a)=self.actors.get_mut(&id){a.taking=true;}
            let mut h=[0.;HIT_FLOATS];h[0]=id as f32;h[1]=target as f32;h[2]=0x36a1 as f32;h[4..7].copy_from_slice(&at.to_array());h[8]=-1.;h[15]=3.;self.hits.push(h);
        }
        Ok(())
    }
    fn resolve_contacts(&mut self,d:&Data,pads:&BTreeMap<u32,Pad>,contacts:Vec<(u32,u32,u16,Vec3,f32,u32,Vec3,f32)>,mut health_host:Option<&mut dyn crate::melee_health::Boundary>)->Result<(),String>{
        let now=self.now;
        for (source,victim,part,point,damage,striker,normal,gap) in contacts{
            self.contact_gaps.insert((source,victim),gap);
            if let Some(a)=self.actors.get_mut(&source){if a.key=="sa_gym_7/attack1"{a.gym_contact=Some(victim);}}
            let Some(attacker)=self.actors.get(&source) else{continue};let by=attacker.foe(d,&self.bank);
            let Some(m)=attacker.melee.mv.as_ref()else{continue};let blow=ct::Blow{by,player:attacker.player,res:d.reaction_identity(d.table.results[m.result].name),clip:Some((m.clip_t,m.step))};let forced=m.forced;
            let before=self.actors[&victim].foe(d,&self.bank);
            let request=crate::melee_health::Request{source,victim,now,bone:part,amount:damage,source_player:blow.player,victim_player:self.actors[&victim].player,guard:before.block,kind:0};
            let mut candidate=if let (Some(host),Some(w),false)=(health_host.as_deref_mut(),self.health_weapon.as_ref(),attacker.club.is_some()){
                host.prepare(request).and_then(|facts|{let r=crate::melee_health::calculate(request,w,&self.rng,facts);if r.is_none(){host.cancel(request);}r})
            }else{None};
            let a=self.actors.get_mut(&victim).unwrap();
            let pick=if forced{ct::Pick::Forced{pad:a.player.then_some(&pads[&victim].controls)}}else{ct::Pick::Dispatcher};
            // 0x826174F0 / hitpath::priority, reasons BULLET/BLOW: the player's
            // NM priority is zero at health >=100, even outside a melee task.
            // The source health transaction includes guard/helmet/head KO. Raw
            // authored damage is NOT a prediction of death or a ragdoll trigger.
            let select=|health:f32,rng:&mut Rand|{let mut me=before;me.health=health;
                ct::react(d,&me,a.player,Some(&weapon(a.club.is_some(),a.blade)),a.melee.mv.as_ref(),&blow,!a.player||health<100.,a.decision.reaction_tasks(),pick,&mut||rng.next())};
            let physical=a.flags&(DOWN|DEAD)!=0||(a.flags&PHYSICAL!=0&&a.flags&DRIVEN==0);
            let reaction=if let Some(receipt)=candidate.as_mut(){
                let mut trial=Rand::new(receipt.rng_after);let reaction=select(receipt.after.health,&mut trial);
                receipt.rng_after=trial.seed;
                receipt.selection=Some(crate::melee_health::Selection{ragdoll:reaction.ragdoll||physical,pending:!reaction.ragdoll&&!physical&&!receipt.outcome.dies()&&!forced&&a.melee.on,
                    action:reaction.action.map(|a|a.name),result:reaction.result.map(|r|r.name)});
                let decision_ok=receipt.facts.decision.is_none()||self.health_decisions.as_ref().and_then(|store|crate::melee_health::decide_queue(receipt,store,&mut trial)).is_some();
                if !decision_ok {health_host.as_deref_mut().unwrap().cancel(request);}
                if decision_ok&&health_host.as_deref_mut().unwrap().commit(receipt)?{
                    self.rng=trial;a.frame[4]=receipt.after.health;reaction
                }else{candidate=None;select(before.health,&mut self.rng)}
            }else{select(before.health,&mut self.rng)};
            let terminal=candidate.as_ref().is_some_and(|r|r.outcome.dies());
            let ragdoll=reaction.ragdoll||physical;
            let mut chosen=-1.;
            if !ragdoll&&!terminal&&!forced&&a.melee.on {
                a.melee.request=Some(ct::Request{blow,from:Some(source),way:melee_task::request_way((blow.by.feet+Vec3::Z).into(),point.into()),locked:false});
                // -2 means a pending reaction, not a fabricated result or an
                // instruction to abandon the standing melee task.
                chosen=-2.;
            }else if !ragdoll&&!terminal {if let Some(act)=reaction.action {if let Some(r)=d.result_of(act){
                ct::play_result(&mut a.melee.mv,d,act,a.player,&|k|self.bank.clips.get(k).map(|c|c.duration));
                if let Some(m)=a.melee.mv.as_mut(){m.given=true;m.swing=false;}
                a.melee.on=true;a.melee.from_idle=false;a.melee.request=None;
                a.wait=None;a.waiting=false;a.drift=None;a.target=source;chosen=r as f32;
            }}}
            let direction=normal;
            // Adapt the original split: ordinary hits retain a directional
            // standing reaction; the dispatcher's NM choice, existing fall or
            // fatal damage owns full-body physics. Do not override it for each
            // light punch (including punches received by CJ).
            let mut h=[0.;HIT_FLOATS];h[0]=source as f32;h[1]=victim as f32;h[2]=part as f32;h[3]=damage;h[4..7].copy_from_slice(&point.to_array());h[7]=ragdoll as u8 as f32;h[8]=chosen;
            h[9..12].copy_from_slice(&direction.to_array());h[12]=striker as f32;h[13]=forced as u8 as f32;h[14]=before.block as u8 as f32;
            if let Some(receipt)=candidate{self.health_receipts.insert(self.hits.len(),receipt);}
            self.hits.push(h);
        }
        Ok(())
    }

}

#[cfg(test)]mod disarm_regressions{
    use super::*;use lsc4_combat::actiontable::Action;
    // The whole host-side chain of the player's weapon take, on the real table, clips and rig: the disarm result's forced
    // reaction puts the club foe on Hit_disarmed_bat_Res; at the clip's 0x200 event, locked on, one kind-3 record names
    // taker and foe; the weapon swap (host) must not restart the taker's clip.
    fn world(lock:bool,club:bool)->(Runtime,Data){
        static INIT:std::sync::Once=std::sync::Once::new();let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        INIT.call_once(||assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(root.join("game/DeliriumSA/fred.xrt")).unwrap()))));
        let dir=root.join("game/DeliriumSA/combat");let read=|n:&str|std::fs::read(dir.join(n)).unwrap();
        let d=Data::load(&read("meleeanims.dat"),&read("action_table.csv"),&read("pedpersonality.dat")).unwrap();
        let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
        let mut r=Runtime::new(Bank::load(&dir).unwrap());r.register(&d,1,true,"player",rig.clone()).unwrap();r.register(&d,2,false,"ig_roman",rig).unwrap();
        if club{r.feed_club(2,Some(&[0.,0.,0.,0.,0.,0.9,0.04,0.1])).unwrap();}
        let bank=&r.bank;let a=r.actors.get_mut(&1).unwrap();a.melee.on=true;
        ct::play_result(&mut a.melee.mv,&d,&Action{move_:hash("Disarm_Bat_Res"),..Default::default()},true,&|k|bank.clips.get(k).map(|c|c.duration));
        a.flags=FIGHT|PLAYER|if lock{LOCK}else{0};
        (r,d)
    }
    fn frame(r:&mut Runtime,d:&Data,f:u32,lock:bool)->Vec<[f32;HIT_FLOATS]>{
        let now=100+f*17;
        for (&id,a) in &mut r.actors{
            a.frame[..3].copy_from_slice(&if id==1{[0.,0.,1.]}else{[0.,1.,1.]});a.frame[3]=if id==1{0.}else{std::f32::consts::PI};a.frame[4]=200.;
            a.flags=if id==1{FIGHT|PLAYER|if lock{LOCK}else{0}}else{FIGHT|PROBE_PASSIVE|(a.flags&PHYSICAL)};
            a.heading=if id==1{a.heading}else{std::f32::consts::PI};a.target=if id==1{2}else{1};a.last_seen=now;
            a.pad.feed(0,[127,127],[0,0],now.saturating_sub(17));a.pad.feed(0,[127,127],[0,0],now);
        }
        r.advance(d,now,0.017).unwrap();r.finish(d).unwrap();r.hits.iter().copied().filter(|h|h[15]==3.).collect()
    }
    #[test]
    fn the_disarm_result_hands_the_club_over_once_and_survives_the_swap(){
        let (mut r,d)=world(true,true);let (mut takes,mut seen_reaction,mut at)=(vec![],false,0);
        for f in 0..140{
            takes.extend(frame(&mut r,&d,f,true).into_iter().map(|h|(f,h)));
            if takes.len()==1&&at==0{
                at=f;
                // The host now swaps weapons: the new club in the taker's hands, the foe's gone.
                r.feed_club(1,Some(&[0.,0.,0.,0.,0.,0.9,0.04,0.1])).unwrap();r.feed_club(2,None).unwrap();
                assert!(r.actors[&1].melee.mv.is_some(),"the weapon swap restarted the disarm clip");
            }
            seen_reaction|=r.actors[&2].melee.mv.as_ref().is_some_and(|m|d.table.results[m.result].name==hash("Hit_disarmed_bat_Res"));
        }
        assert!(seen_reaction,"the club foe never played Hit_disarmed_bat_Res");
        assert_eq!(takes.len(),1,"one 0x200 event, one take: {takes:?}");
        let h=takes[0].1;assert_eq!((h[0],h[1],h[15]),(1.,2.,3.));assert!((50..65).contains(&takes[0].0),"0x200 (frame 29 of 55 at 30 Hz, 0.97 s) is about frame 57 at 17 ms, got {}",takes[0].0);
        // The taker finishes the clip and goes on armed: the move ends by itself.
        assert!(r.actors[&1].melee.mv.is_none()||r.actors[&1].melee.mv.as_ref().is_some_and(|m|m.clip_t<m.dur));
    }
    #[test]
    fn nothing_is_taken_unlocked_or_from_a_bare_handed_foe(){
        for (lock,club) in [(false,true),(true,false)]{
            let (mut r,d)=world(lock,club);
            for f in 0..140{assert!(frame(&mut r,&d,f,lock).is_empty(),"lock {lock} club {club}: took a weapon");}
        }
    }
    #[test]
    fn without_the_guard_a_weapon_change_restarts_the_clip(){
        // The control for the guard: an unrelated weapon change (no take) still resets the task, as before.
        let (mut r,d)=world(true,true);for f in 0..10{frame(&mut r,&d,f,true);}
        assert!(r.actors[&1].melee.mv.is_some());r.feed_club(1,Some(&[0.,0.,0.,0.,0.,0.9,0.04,0.1])).unwrap();
        assert!(r.actors[&1].melee.mv.is_none());
    }
}
