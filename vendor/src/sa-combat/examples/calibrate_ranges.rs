//! Measure initial punch reach; keep original clips, windows, speeds and homing.
use sa_combat::{Data,animation::{Bank,Rig,Pose},runtime::*,controller};
use bevy_math::{Vec3,Quat};
use std::{fs,path::Path};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let args:Vec<_>=std::env::args().skip(1).collect();let root=Path::new(&args[0]);let dir=root.join("game/DeliriumSA/combat");
 let asset=delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?);assert!(delirium_data::kv::unlock(&asset));
 let data=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?,&fs::read(dir.join("action_table.csv"))?,&fs::read(dir.join("pedpersonality.dat"))?)?;
 let mut rt=Runtime::new(Bank::load(&dir)?);let rigs=[fs::read(root.join("game/DeliriumSA/combat-rig-0.json"))?,fs::read(root.join("game/DeliriumSA/combat-rig-7.json"))?];let mut reports=vec![];
 for name in ["Lowblow_L_Act","Jab_Act","hook_R_long_Act","Lowblow_R_Act","cross_R_Act","hook_L_long_Act"]{
  let action=data.table.actions.iter().find(|a|a.name==sa_combat::hash(name)).unwrap();let mut samples=vec![];
  for tick in 16..=88 {let distance=tick as f32*0.025;
   rt.actors.clear();rt.register(&data,1,true,"player",Rig::from_json(&rigs[0])?)?;rt.register(&data,2,false,"m_y_thief",Rig::from_json(&rigs[1])?)?;
   let mut pos=[Vec3::Z,Vec3::new(0.,distance,1.)];let mut yaw=[0.,std::f32::consts::PI];let mut hit=None;
   for n in 0..250u32{let now=1000+n*10;
    for i in 0..2{let a=&rt.actors[&(i as u32+1)];let mut f=[0.;FRAME_FLOATS];f[..3].copy_from_slice(&pos[i].to_array());f[3]=yaw[i];f[4]=200.;f[5]=if i==0{(PLAYER|FIGHT|LOCK)as f32}else{0.};f[6]=if i==0{2.}else{-1.};f[7]=if i==0{1.}else{0.};f[8]=127.;f[9]=127.;f[10]=255.;let world=Pose{p:pos[i],q:Quat::from_rotation_z(yaw[i])};let mut buffer=vec![0.;a.pose.len()*7];for(k,&t)in a.rig.bind.iter().enumerate(){world.mul(t).write(&mut buffer[7*k..7*k+7]);}rt.feed(i as u32+1,&f,&buffer,now)?;}
    if n==100{let a=rt.actors.get_mut(&1).unwrap();controller::play_result(&mut a.melee.mv,&data,action,true,&|k|rt.bank.clips.get(k).map(|c|c.duration));}
    rt.step(&data,now,0.01)?;
    if let Some(h)=rt.hits.iter().find(|h|h[0]==1.&&h[1]==2.&&h[15]==0.){hit=Some(serde_json::json!({"phase_ms":(n-100)*10,"part":h[2]}));break}
    for i in 0..2{let a=&rt.actors[&(i as u32+1)];if a.state[0]==1.{yaw[i]=a.state[1];pos[i]+=Quat::from_rotation_z(yaw[i])*Vec3::new(a.state[2],a.state[3],0.)*0.01;}}
   }
   samples.push(serde_json::json!({"distance":distance,"contact":hit}));
  }
  let max=samples.iter().filter(|s|!s["contact"].is_null()).map(|s|s["distance"].as_f64().unwrap()).fold(0.,f64::max);
  println!("{name}: {max:.3} m");reports.push(serde_json::json!({"action":name,"result":action.move_,"original_range":action.distance_range,"max_contact_distance":max,"samples":samples}));
 }
 fs::write(&args[1],serde_json::to_vec_pretty(&reports)?)?;Ok(())
}
