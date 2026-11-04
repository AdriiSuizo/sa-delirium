//! Real decoded assets and SA rigs, deterministic scenes, no game or desktop input.
use sa_combat::{Data,animation::{Bank,Rig,Pose},runtime::*};
use bevy_math::{Vec3,Quat};
use std::{fs,path::Path};
fn rig(path:&Path)->Rig {Rig::from_json(&fs::read(path).unwrap()).unwrap()}
fn main()->Result<(),Box<dyn std::error::Error>> {
 let args:Vec<_>=std::env::args().skip(1).collect();let root=Path::new(&args[0]);let dir=root.join("game/DeliriumSA/combat");
 let asset=delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?);assert!(delirium_data::kv::unlock(&asset));
 let mut data=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?,&fs::read(dir.join("action_table.csv"))?,&fs::read(dir.join("pedpersonality.dat"))?)?;
 if dir.join("sa-contact-ranges.json").exists(){data.adapt_reach(&fs::read(dir.join("sa-contact-ranges.json"))?)?;}
 let mut runtime=Runtime::new(Bank::load(&dir)?);let mut reports=vec![];
 for (scene,player,distance,button) in [("jab",true,1.15,0x20),("jab_fighter",true,1.15,0x20),("kick",true,1.15,0x80),("block",true,1.15,0x40),("miss",true,4.,0x20),("npc_pair",false,1.15,0x0),("defend",true,1.15,0x40),("dodge",true,1.15,0x40),("counter",true,1.15,0x40),("physical_yield",true,1.15,0x20),("physical_geometry",true,1.15,0x20),("physical_balanced",true,1.15,0x20),("physical_down",true,1.15,0x20),("player_unready",true,1.15,0),("player_low",true,1.15,0),("sa_stats",false,1.15,0),("jab_flee",false,1.15,0),("counter_flee",true,1.15,0x40),("counter_target_lost",true,1.15,0x40),("counter_death",true,1.15,0x40)] {
  let counter=scene.starts_with("counter");let exit_test=scene=="jab_flee"||counter&&scene!="counter";
  runtime.actors.clear();runtime.register(&data,1,player,"m_y_thief",rig(&root.join("game/DeliriumSA/combat-rig-0.json")))?;
  runtime.register(&data,2,false,"m_y_thief",rig(&root.join("game/DeliriumSA/combat-rig-7.json")))?;
  let mut p=[Vec3::Z,Vec3::new(0.,distance,1.)];let mut yaw=[0.,std::f32::consts::PI];let mut actions=vec![];let mut contacts=vec![];let mut poses=vec![];let mut requests=vec![];
  let mut previous=[String::new(),String::new()];let mut exit_at=None;let mut response_frames=0;let mut resumed_at=None;
  for n in 0..1200u32 {let now=1000+n*10;
   for i in 0..2 {let id=i as u32+1;let a=&runtime.actors[&id];let mut f=[0.;FRAME_FLOATS];f[..3].copy_from_slice(&p[i].to_array());f[3]=yaw[i];f[4]=200.;
    f[5]=((if i==0&&player{PLAYER}else{0})|FIGHT|LOCK)as f32;f[6]=(if i==0{2}else{1})as f32;f[8]=127.;f[9]=127.;f[10]=255.;
    if player&&i==0 {let pressed=if scene=="defend"{true}else if scene=="dodge"||counter{
      runtime.actors[&2].melee.mv.as_ref().is_some_and(|m|m.clip_t>0.35&&m.clip_t<0.42)
    }else{(100..135).contains(&n)};f[7]=(1|if pressed{button}else{0})as f32}
    if counter&&i==0&&a.key.contains("dodge_")&&a.state[5]>0.65&&a.state[5]<0.73{f[7]=33.;}
    // The target idles in the player contact/miss cases; AI pair owns both sides.
    if player&&i==1&&!counter&&!matches!(scene,"jab_fighter"|"jab_flee"|"defend"|"dodge"|"player_unready"|"player_low"){f[5]=0.;f[6]=-1.;}
    if i==1&&exit_at.is_some(){
     // Also reproduce loss of engagement/lock from pursuit or a weapon swap.
     f[5]=EXIT_INTENT as f32;
     if scene=="counter_target_lost"{f[6]=-1.;}
     if scene=="counter_death"&&n>exit_at.unwrap()+7{f[5]=(EXIT_INTENT|DEAD)as f32;f[4]=0.;}
    }
    if i==0&&matches!(scene,"player_unready"|"player_low"){f[5]=PLAYER as f32;f[7]=0.;f[4]=if scene=="player_low"{80.}else{200.};}
    if scene=="sa_stats"{f[16]=if i==0{0.7}else{1.1};f[17]=if i==0{1.7}else{0.9};f[18]=1.;}
    if scene=="physical_yield"&&i==0&&n>=110{f[5]=(PLAYER|FIGHT|LOCK|PHYSICAL)as f32;}
    if i==1&&matches!(scene,"physical_geometry"|"physical_balanced"|"physical_down"){f[5]=(FIGHT|LOCK|PHYSICAL|DRIVEN|if scene=="physical_down"{DOWN}else{0})as f32;f[6]=1.;}
    // Deliberately separate actual physical geometry from its motor target.
    // A sweep must miss the nearby target when the real body is elsewhere.
    let native_pos=p[i]+if i==1&&scene=="physical_geometry"{Vec3::X*10.}else{Vec3::ZERO};
    let world=Pose{p:native_pos,q:Quat::from_rotation_z(yaw[i])};let pose=a.rig.bind.iter().map(|&b|world.mul(b)).collect::<Vec<_>>();let mut buffer=vec![0.;pose.len()*7];for(k,t)in pose.iter().enumerate(){t.write(&mut buffer[k*7..k*7+7])}
    runtime.feed(id,&f,&buffer,now)?;
   }
   runtime.step(&data,now,0.01)?;
   if exit_at.is_some(){let receiver=&runtime.actors[&2];
    if receiver.receiving(){assert!(receiver.state[0]>0.,"native flee discarded a running response");response_frames+=1;}
    else if receiver.state[0]==0.&&resumed_at.is_none(){resumed_at=Some(n);}
    assert!(runtime.hits.iter().all(|h|h[0]!=2.||h[15]!=0.),"fleeing receiver started another attack");
   }
   if scene=="sa_stats"{assert_eq!(runtime.actors[&1].me.attack,0.7);assert_eq!(runtime.actors[&1].me.defend,1.7);assert_eq!(runtime.actors[&2].me.attack,1.1);assert_eq!(runtime.actors[&2].me.defend,0.9);}
   if scene=="physical_yield"&&n>=110{assert_eq!(runtime.actors[&1].state[0],0.,"combat stole NM ownership");assert!(runtime.hits.iter().all(|h|h[0]!=1.),"physical actor delivered animated strike");}
   if matches!(scene,"physical_geometry"|"physical_balanced"|"physical_down"){
    assert_eq!(runtime.actors[&2].state[0],if scene=="physical_down"{0.}else{2.},"physical reaction stole rendering or lost its motor lease");
    assert!(runtime.hits.iter().all(|h|h[0]!=2.||h[15]!=0.),"motor target delivered a phantom attack");
   }
   for i in 0..2{let a=&runtime.actors[&(i as u32+1)];assert!(a.pose.iter().all(|t|t.p.is_finite()&&t.q.is_finite()));
    if a.state[13]>=0.{requests.push(serde_json::json!({"frame":n,"receiver":i+1,"source":a.state[13],"taken":a.state[12],"result":a.state[4],"ownership":a.state[0]}));}
    for (k,parent) in a.rig.parents.iter().enumerate(){if let Some(parent)=parent {assert!((a.pose[k].p.distance(a.pose[*parent].p)-a.rig.local[k].p.length()).abs()<0.001)}}
    if previous[i]!=a.key {actions.push(serde_json::json!({"frame":n,"id":i+1,"clip":a.key,"result":a.state[4]}));previous[i]=a.key.clone()}
    if a.state[0]==1.{yaw[i]=a.state[1];p[i]+=Quat::from_rotation_z(yaw[i])*Vec3::new(a.state[2],a.state[3],0.)*0.01;}
    if scene=="jab"&&i==0&&(95..160).contains(&n)&&n%5==0{poses.push(serde_json::json!({"frame":n,"root":p[i].to_array(),"live":a.state[7],"ids":a.rig.ids,"points":a.pose.iter().map(|p|p.p.to_array()).collect::<Vec<_>>()}));}
   }
   for h in &runtime.hits {
    if exit_test&&exit_at.is_none()&&h[1]==2.&&h[15]==if counter{1.}else{0.}{
     assert!(runtime.actors[&2].receiving(),"damage receipt lost the pending/given response");exit_at=Some(n);
    }
    if h[15]==2.{let receiver=&runtime.actors[&(h[1] as u32)];let m=receiver.melee.mv.as_ref().expect("paired reaction missing");
     assert!(m.given&&!m.swing&&!receiver.melee.from_idle&&receiver.melee.request.is_none(),"paired reaction inherited outgoing attack state");}
    if h[8]==-2.{let receiver=&runtime.actors[&(h[1] as u32)];assert_eq!(receiver.melee.request.as_ref().and_then(|q|q.from),Some(h[0] as u32),"deferred blow lost its actual source");}
    contacts.push(serde_json::json!({"frame":n,"hit":h}));
   }
  }
  assert!(actions.iter().any(|a|a["clip"]=="move_melee/idle"),"stance missing {scene}");
  if scene=="miss" {assert!(contacts.is_empty(),"out-of-range contact");}
  if scene=="physical_geometry"{assert!(contacts.is_empty(),"hit the unphysical motor target instead of actual body");}
  if scene=="physical_balanced"{assert!(contacts.iter().any(|h|h["hit"][1]==2.),"balanced physical target became unhittable");}
  // Standing full-health fighters must not be forced out of their task merely
  // because contact was accepted. Existing physical/downed victims retain NM.
  if matches!(scene,"jab_fighter"|"defend"|"counter"|"npc_pair"){
   assert!(contacts.iter().any(|h|h["hit"][7]==0.),"healthy standing reactions all became ragdolls: {scene}");
  }
  if matches!(scene,"player_unready"|"player_low"){
   let received:Vec<_>=contacts.iter().filter(|h|h["hit"][1]==1.&&h["hit"][15]==0.).collect();
   assert!(!received.is_empty(),"player fixture never received a real strike: {scene}");
   if scene=="player_unready"{assert!(received.iter().all(|h|h["hit"][7]==0.),"healthy player forced into NM outside guard");}
   else{assert!(received.iter().any(|h|h["hit"][7]==1.),"low-health player NM gate never opened");}
  }
  if scene=="physical_down"{assert!(contacts.iter().all(|h|h["hit"][7]==1.),"downed target lost physical ownership");}
  assert!(contacts.iter().filter(|h|h["hit"][15]==2.).all(|h|h["hit"][7]==0.),"paired start forced a premature fall");
  assert!(contacts.iter().filter(|h|h["hit"][15]==2.).all(|h|h["hit"][3]==0.),"motor start applied counter damage early");
  if scene=="jab"||scene=="kick" {assert_eq!(contacts.len(),1,"single authored strike must make exactly one contact: {scene}");}
  if scene=="npc_pair" {assert!(contacts.len()>=2,"NPC fight produced no exchange");}
  if scene=="defend" {assert!(!requests.is_empty(),"running guard never received next-step requests");}
  for r in &requests {let n=r["frame"].as_u64().unwrap();assert!(contacts.iter().any(|h|h["frame"].as_u64()==n.checked_sub(1)&&h["hit"][1].as_f64()==r["receiver"].as_f64()&&h["hit"][8]==-2.),"request was consumed outside the next receiver step: {r}");}
  if scene=="defend" {assert!(contacts.iter().any(|h|h["hit"][14]==1.),"guard never intercepted an attack");assert!(actions.iter().any(|a|a["clip"].as_str().unwrap_or("").contains("dam_block")),"no directional block reaction");}
  if scene=="dodge" {assert!(actions.iter().any(|a|a["clip"].as_str().unwrap_or("").contains("dodge_")),"timed defence never dodged");assert!(contacts.len()<6,"timed dodges did not avoid any attacks");}
  if counter {
   let starts:Vec<_>=contacts.iter().filter(|h|h["hit"][15]==2.).collect();
   assert!(!starts.is_empty(),"no counter chain");
   let mut delivered=std::collections::BTreeSet::new();
   for h in contacts.iter().filter(|h|h["hit"][15]==1.) {
    let start=starts.iter().rev().find(|s|s["frame"].as_u64()<h["frame"].as_u64()&&s["hit"][1]==h["hit"][1]&&s["hit"][8]==h["hit"][8]).expect("counter damage without its paired reaction");
    assert!(delivered.insert((start["frame"].as_u64(),start["hit"][1].as_f64().map(|n|n as u32))),"counter damage repeated for one reaction");
   }
   assert!(!delivered.is_empty(),"counter chain never delivered authored damage");
  }
  if exit_test{
   assert!(exit_at.is_some(),"exit reproduction never received its hit");
   assert!(response_frames>1,"reaction yielded immediately to flee");
   assert!(resumed_at.is_some_and(|n|n>exit_at.unwrap()),"completed reaction never returned control to native AI");
   if scene=="counter_death"{assert!(resumed_at.unwrap()<=exit_at.unwrap()+8,"reaction lease blocked death");}
  }
  println!("{scene}: {} actions, {} contacts",actions.len(),contacts.len());
  reports.push(serde_json::json!({"scene":scene,"actions":actions,"contacts":contacts,"requests":requests,"poses":poses,"native_exit_at":exit_at,"response_frames_after_exit":response_frames,"native_resumed_at":resumed_at}));
 }
 fs::write(&args[1],serde_json::to_vec_pretty(&reports)?)?;Ok(())
}
