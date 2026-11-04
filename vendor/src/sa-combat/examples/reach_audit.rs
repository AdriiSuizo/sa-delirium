//! Range sweep using decoded clips and real exported SA rigs. No desktop input.
use sa_combat::{Data,animation::{Bank,Rig,Pose,iv_bone},runtime::*};
use bevy_math::{Vec3,Quat,Mat3};
use std::{fs,path::Path};
fn rig(path:&Path)->Rig {Rig::from_json(&fs::read(path).unwrap()).unwrap()}
fn main()->Result<(),Box<dyn std::error::Error>> {
 let args:Vec<_>=std::env::args().skip(1).collect();let root=Path::new(&args[0]);let dir=root.join("game/DeliriumSA/combat");
 let asset=delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?);assert!(delirium_data::kv::unlock(&asset));
 let mut data=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?,&fs::read(dir.join("action_table.csv"))?,&fs::read(dir.join("pedpersonality.dat"))?)?;
 if dir.join("sa-contact-ranges.json").exists(){data.adapt_reach(&fs::read(dir.join("sa-contact-ranges.json"))?)?;}
 let mut rt=Runtime::new(Bank::load(&dir)?);let mut reports=vec![];
 let host_order=args.iter().any(|a|a=="--host-order");
 let skeleton:serde_json::Value=serde_json::from_slice(&fs::read(dir.join("skeleton.json"))?)?;
 let bones=skeleton["bones"].as_array().unwrap();let mut source_bind:Vec<Pose>=vec![];
 for b in bones{let p: [f32;3]=serde_json::from_value(b["translation"].clone())?;let q:[f32;4]=serde_json::from_value(b["rotation"].clone())?;let t=Pose{p:Vec3::from_array(p),q:Quat::from_array(q)};source_bind.push(b["parent"].as_u64().map_or(t,|i|source_bind[i as usize].mul(t)));}
 let index=|tag:u32|bones.iter().position(|b|b["id"]==tag).unwrap();let up=(source_bind[index(0x4b4)].p-source_bind[index(0x1a1)].p).normalize();let r=source_bind[index(0x1a7)].p-source_bind[index(0x1a2)].p;let right=(r-up*r.dot(up)).normalize();let align=Quat::from_mat3(&Mat3::from_cols(right,up.cross(right),up)).inverse();
 for button in [0x20,0x10] { for distance in [0.55,0.7,0.85,1.,1.05,1.1,1.15,1.175,1.2,1.25,1.3,1.35,1.4,1.45,1.6,1.8,2.,2.4,4.] {
  rt.actors.clear();rt.register(&data,1,true,"player",rig(&root.join("game/DeliriumSA/combat-rig-0.json")))?;
  rt.register(&data,2,false,"m_y_thief",rig(&root.join("game/DeliriumSA/combat-rig-7.json")))?;
  let mut pos=[Vec3::Z,Vec3::new(0.,distance,1.)];let mut yaw=[0.,std::f32::consts::PI];let mut frames=vec![];
  let mut elapsed=0.0f32;
  for n in 0..350u32 {let dt=if host_order{[0.008f32,0.016,0.06,0.011][n as usize%4]}else{0.01};let now=1000+(elapsed*1000.).round() as u32;
   for i in 0..2 {let a=&rt.actors[&(i as u32+1)];let mut f=[0.;FRAME_FLOATS];f[..3].copy_from_slice(&pos[i].to_array());f[3]=yaw[i];f[4]=200.;f[5]=if i==0{(PLAYER|FIGHT|LOCK) as f32}else{0.};f[6]=if i==0{2.}else{-1.};f[7]=if i==0{(1|if (1.0..1.35).contains(&elapsed){button}else{0}) as f32}else{0.};f[8]=127.;f[9]=127.;f[10]=255.;
    let world=Pose{p:pos[i],q:Quat::from_rotation_z(yaw[i])};let mut buffer=vec![0.;a.pose.len()*7];for(k,&t)in a.rig.bind.iter().enumerate(){world.mul(t).write(&mut buffer[k*7..k*7+7]);}rt.feed(i as u32+1,&f,&buffer,now)?;
   }
   if host_order{
    rt.advance(&data,now,dt)?;
    for i in 0..2{let id=i as u32+1;let a=&rt.actors[&id];if a.state[0]==1.{yaw[i]=a.state[1];pos[i]+=Quat::from_rotation_z(yaw[i])*Vec3::new(a.state[2],a.state[3],0.)*dt;}rt.place(id,pos[i])?;}
    rt.finish(&data)?;
    assert!(rt.finish(&data).is_err(),"duplicate completion must not repeat damage");
   }else{rt.step(&data,now,dt)?;}
   let a=&rt.actors[&1];let victim=&rt.actors[&2];
   if let Some(m)=&a.melee.mv {let r=&data.table.results[m.result];let mut limbs=vec![];
    for &(enabled,tag,radius) in &r.strike_bones {if !enabled{continue}if let Some(k)=a.rig.ids.iter().position(|&s|iv_bone(s)==Some(tag as u16)){
     let head=victim.pose[victim.rig.ids.iter().position(|&s|s==8).unwrap()].p;
     let flags=data.clip(r).unwrap().1;let source=rt.bank.pose(&a.key,m.phase(),flags).unwrap();let source_local=align*(source[index(tag)].p-source_bind[index(0)].p);let sa_local=Quat::from_rotation_z(-yaw[0])*(a.pose[k].p-pos[0]);
     limbs.push(serde_json::json!({"tag":tag,"radius":radius,"pos":a.pose[k].p.to_array(),"source_local":source_local.to_array(),"sa_local":sa_local.to_array(),"head_distance":a.pose[k].p.distance(head)}));
    }}
    frames.push(serde_json::json!({"n":n,"clip":a.key,"result":m.result,"phase":m.clip_t,"live":m.live,"root":pos[0].to_array(),"target_root":pos[1].to_array(),"velocity":a.state[3],"homing":a.melee.home.map(|h|[h.dist,h.rate]),"limbs":limbs,"hits":rt.hits}));
   }
   if !host_order{for i in 0..2{let a=&rt.actors[&(i as u32+1)];if a.state[0]==1.{yaw[i]=a.state[1];pos[i]+=Quat::from_rotation_z(yaw[i])*Vec3::new(a.state[2],a.state[3],0.)*dt;}}}
   elapsed+=dt;
  }
  let hits=frames.iter().flat_map(|f|f["hits"].as_array().unwrap()).filter(|h|h[0]==1.).count();
  if distance<=1.45 {assert_eq!(hits,1,"reachable initial attack failed: button={button} distance={distance}");}
  if distance>=1.8 {assert_eq!(hits,0,"out-of-reach target took damage");}
  reports.push(serde_json::json!({"button":button,"distance":distance,"frames":frames}));
 }
 }
 fs::write(&args[1],serde_json::to_vec_pretty(&reports)?)?;Ok(())
}
