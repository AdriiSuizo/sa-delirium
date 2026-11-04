//! Dump source and retargeted transforms for reproducible visual rig review.
use sa_combat::animation::{Bank,Rig};
use std::{fs,path::Path};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let args:Vec<_>=std::env::args().skip(1).collect();let root=Path::new(&args[0]);
 assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?)));
 let bank=Bank::load(&root.join("game/DeliriumSA/combat"))?;
 let rig=Rig::from_json(&fs::read(root.join("game/DeliriumSA/combat-rig-0.json"))?)?;let mut rows=vec![];
 for key in ["move_player/idle","move_player/walk","move_player/run","move_melee/idle"]{
  for phase in [0.,0.25,0.5,0.75]{let source=bank.pose(key,phase,sa_combat::animation::movement_flags(key).unwrap()).ok_or("source pose")?;let sa=bank.retarget(&rig,key,phase,sa_combat::animation::movement_flags(key).unwrap()).ok_or("SA pose")?;
   let flatten=|poses:Vec<sa_combat::animation::Pose>|poses.iter().map(|p|{let mut a=[0.;7];p.write(&mut a);a}).collect::<Vec<_>>();
   rows.push(serde_json::json!({"key":key,"phase":phase,"source":flatten(source),"sa":flatten(sa)}));
  }
 }
 fs::write(root.join(&args[1]),serde_json::to_vec_pretty(&rows)?)?;Ok(())
}
