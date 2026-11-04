//! Actual FromOnFoot selectors, including a cold host registration versus a live pad.
use sa_combat::{Data,animation::{Bank,Rig,Pose},runtime::*};
use bevy_math::{Vec3,Quat};
use std::{fs,path::Path};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let args:Vec<_>=std::env::args().skip(1).collect();let root=Path::new(&args[0]);let dir=root.join("game/DeliriumSA/combat");
 assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?)));
 let mut d=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?,&fs::read(dir.join("action_table.csv"))?,&fs::read(dir.join("pedpersonality.dat"))?)?;
 d.adapt_reach(&fs::read(dir.join("sa-contact-ranges.json"))?)?;
 let mut rows=vec![];
 for warm in [false,true]{for distance in [0.7,1.,1.15,1.4]{
  let mut rt=Runtime::new(Bank::load(&dir)?);
  for(id,model,player)in[(1,0,true),(2,7,false)]{rt.register(&d,id,player,"m_y_thief",Rig::from_json(&fs::read(root.join(format!("game/DeliriumSA/combat-rig-{model}.json")))?)?)?;}
  let mut p=[Vec3::Z,Vec3::new(0.,distance,1.)];let mut yaw=[0.,std::f32::consts::PI];let mut actions=vec![];
  for tick in (if warm{0}else{100})..400u32{
   let now=1000+tick*10;
   for id in [1,2]{let i=id as usize-1;let a=&rt.actors[&id];let mut f=[0.;FRAME_FLOATS];f[..3].copy_from_slice(&p[i].to_array());f[3]=yaw[i];f[4]=200.;f[5]=if id==1{(PLAYER|if tick>=100{FIGHT}else{0})as f32}else{0.};f[6]=if id==1{2.}else{-1.};f[8]=127.;f[9]=127.;f[7]=if id==1&&(100..135).contains(&tick){32.}else{0.};
    let mut raw=vec![0.;a.rig.ids.len()*7];for(k,b)in a.rig.bind.iter().enumerate(){Pose{p:p[i],q:Quat::from_rotation_z(yaw[i])}.mul(*b).write(&mut raw[k*7..k*7+7]);}rt.feed(id,&f,&raw,now)?;
   }
   rt.advance(&d,now,0.01)?;
   let a=&rt.actors[&1];if a.state[0]>0.{if actions.last().is_none_or(|v:&String|v!=&a.key){actions.push(a.key.clone());}yaw[0]=a.state[1];p[0]+=Quat::from_rotation_z(yaw[0])*Vec3::new(a.state[2],a.state[3],0.)*0.01;}
   for id in [1,2]{rt.place(id,p[id as usize-1])?;}
   if rt.actors[&1].state[0]==3.{
    let a=&rt.actors[&1];assert!(!a.melee.fighting);assert!(!a.key.starts_with("move_melee/"));
    let state=a.state;let root_pose=Pose{p:p[0],q:Quat::from_rotation_z(yaw[0])};let mut root=[0.;7];root_pose.write(&mut root);
    let mut under=vec![0.;a.rig.ids.len()*7];for(k,b)in a.rig.bind.iter().enumerate(){root_pose.mul(*b).write(&mut under[k*7..k*7+7]);}
    rt.compose_standalone(1,&root,&under)?;assert_eq!(rt.actors[&1].state,state,"composition must not advance any task/input/clip clock");
   }
   rt.finish(&d)?;
  }
  if warm{assert!(!actions.is_empty(),"warm real pad must dispatch first unlocked attack");assert!(actions.iter().all(|s|!s.starts_with("move_melee/")));}else{assert!(actions.is_empty(),"cold pad reproduction changed");}
  rows.push(serde_json::json!({"warm":warm,"distance":distance,"actions":actions}));
 }}
 fs::write(&args[1],serde_json::to_vec_pretty(&rows)?)?;Ok(())
}
