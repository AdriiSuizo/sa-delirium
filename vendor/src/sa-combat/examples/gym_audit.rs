//! Owned source clips, real SA rigs and the production selector/contact loop.
use sa_combat::{Data,animation::{Bank,Rig,Pose},runtime::*};
use bevy_math::{Vec3,Quat};
use std::{fs,path::Path};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let args:Vec<_>=std::env::args().skip(1).collect();let root=Path::new(&args[0]);let dir=root.join("game/DeliriumSA/combat");
 assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?)));
 let mut data=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?,&fs::read(dir.join("action_table.csv"))?,&fs::read(dir.join("pedpersonality.dat"))?)?;
 let mut bank=Bank::load(&root.join("tools/mod-staging/niko-original-20261101"))?;
 let native:sa_combat::native_animation::File=serde_json::from_slice(&fs::read(root.join("tools/mod-staging/sa-gym-original-20261101/sa-gym-clips.json"))?)?;
 for c in native.clips{assert!(c.validate(),"invalid {}",c.key);bank.clips.insert(c.key.clone(),c.clock());bank.native.insert(c.key.clone(),c);}
 data.install_gym(&bank)?;let mut rt=Runtime::new(bank);let mut reports=vec![];
 let mut scenes=vec![];for style in [4,5,6,7]{for dist in [0.7,0.9,1.1,3.0]{scenes.push((style,dist,"repeat"));}}
 for style in [5,6,7]{scenes.push((style,0.9,"held"));scenes.push((style,0.9,"punch"));}
 scenes.push((7,0.9,"lost"));scenes.push((7,0.9,"death"));scenes.push((7,0.9,"post_selection_move"));
 for (style,dist,input) in scenes{
  data.gym_style(style);rt.actors.clear();
  // A second held-control fighter isolates the chain from random AI attacks.
  // Native world collision and NPC decisions are tested in the game separately.
  for (id,model) in [(1,0),(2,7)]{let rig=Rig::from_json(&fs::read(root.join(format!("game/DeliriumSA/combat-rig-{model}.json")))?)?;rt.register(&data,id,true,"m_y_thief",rig)?;}
  let mut roots=[Vec3::Z,Vec3::new(0.,dist,1.)];let mut yaws=[0.,std::f32::consts::PI];let mut prev=[String::new(),String::new()];let mut clips=vec![];let mut hits=vec![];let mut samples=vec![];
  let mut interrupted=None;
  for n in 0..700{let now=1000+n*10;
   if matches!(input,"lost"|"death")&&interrupted.is_none()&&rt.actors[&2].key=="sa_gym_7/hit2"&&rt.actors[&2].state[5]>0.08{interrupted=Some(n);}
   for i in 0..2{let id=i as u32+1;let a=&rt.actors[&id];let mut f=[0.;20];f[..3].copy_from_slice(&roots[i].to_array());f[3]=yaws[i];f[4]=200.;f[5]=(PLAYER|FIGHT|LOCK)as f32;f[6]=if i==0{2.}else{1.};f[8]=127.;f[9]=127.;f[10]=255.;f[7]=1.;
    if i==0{f[7]=(1|if (50..480).contains(&n)&&(input=="held"||n%18<3){if input=="punch"{0x20}else{0x80}}else{0})as f32;
     if interrupted.is_some(){if input=="death"{f[5]=(PLAYER|DEAD)as f32;f[4]=0.;}else{f[6]=-1.;}}
    }
    let world=Pose{p:roots[i],q:Quat::from_rotation_z(yaws[i])};let mut pose=vec![0.;a.rig.ids.len()*7];for(k,b)in a.rig.bind.iter().enumerate(){world.mul(*b).write(&mut pose[k*7..k*7+7]);}rt.feed(id,&f,&pose,now)?;
   }
   rt.advance(&data,now,0.01)?;
   // A valid grab can move just outside its entry range while the native
   // world resolves this frame. It remains inside the authored continuation
   // envelope, with the same real preceding contact and actor identities.
   if input=="post_selection_move"&&rt.actors[&1].key=="sa_gym_7/attack2"{roots[1].y=1.115;}
   // Fixed collision-resolved roots exercise the selector/contacts without
   // pretending this offline harness includes the native world solver.
   for i in 0..2{let id=i as u32+1;let a=&rt.actors[&id];if a.state[0]==1.{yaws[i]=a.state[1];}rt.place(id,roots[i])?;}
   rt.finish(&data)?;
   if n%5==0&&n<210 {let a=&rt.actors[&1];samples.push(serde_json::json!({"frame":n,"state":a.state,"roots":roots.map(|v|v.to_array()),"ids":a.rig.ids,"points":a.pose.iter().map(|p|p.p.to_array()).collect::<Vec<_>>()}));}
   for i in 0..2{let a=&rt.actors[&(i as u32+1)];if prev[i]!=a.key{clips.push(serde_json::json!({"frame":n,"id":i+1,"clip":a.key}));prev[i]=a.key.clone();}assert!(a.pose.iter().all(|p|p.p.is_finite()&&p.q.is_finite()));
    for(k,p)in a.rig.parents.iter().enumerate(){if let Some(p)=p{assert!((a.pose[k].p.distance(a.pose[*p].p)-a.rig.local[k].p.length()).abs()<0.001,"bone length changed");}}
   }
   for h in &rt.hits{hits.push(serde_json::json!({"frame":n,"hit":h}));}
  }
  reports.push(serde_json::json!({"style":style,"distance":dist,"input":input,"interrupted":interrupted,"clips":clips,"hits":hits,"samples":samples}));
 }
 fs::write(root.join(&args[1]),serde_json::to_vec_pretty(&reports)?)?;
 for s in &reports{let style=s["style"].as_u64().unwrap();let distance=s["distance"].as_f64().unwrap();let clips=s["clips"].as_array().unwrap();let hits=s["hits"].as_array().unwrap();
  if style==4{assert!(clips.iter().all(|c|!c["clip"].as_str().unwrap().starts_with("sa_gym_")),"unlearned style changed Niko");}
  if distance>2.{assert!(hits.is_empty(),"far attack caused damage");}
  if s["input"]=="repeat"&&style>=5&&distance<1.{for step in 1..=3{assert!(clips.iter().any(|c|c["id"]==1&&c["clip"]==format!("sa_gym_{style}/attack{step}")),"missing chained step {style}/{step}");}}
  if (s["input"]=="repeat"||s["input"]=="post_selection_move")&&style==7&&distance<1.{
   assert!(hits.iter().any(|h|h["hit"][15]==2.),"missing paired start");
   assert!(hits.iter().any(|h|h["hit"][15]==1.&&h["hit"][7]==1.&&h["hit"][8]==-1.),"missing final physical release");
  }
  if s["input"]=="held"{assert!(!clips.iter().any(|c|c["clip"]==format!("sa_gym_{style}/attack2")),"holding one press auto-chained");}
  if s["input"]=="punch"{assert!(clips.iter().all(|c|!c["clip"].as_str().unwrap().starts_with("sa_gym_")),"gym replaced Niko's punch button");}
  if s["input"]=="lost"||s["input"]=="death"{let n=s["interrupted"].as_u64().expect("pair interruption fixture did not start");assert!(hits.iter().all(|h|h["frame"].as_u64().unwrap()<n||h["hit"][15]!=1.),"orphaned pair kept damaging victim");}
 }
 println!("{} fixed-root selector/contact scenes passed; native collision is a separate live test",reports.len());Ok(())
}
