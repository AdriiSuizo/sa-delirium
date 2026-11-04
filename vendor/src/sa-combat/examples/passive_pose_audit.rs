//! Regression for passive target geometry produced after native animation.
use sa_combat::{Data,animation::{Bank,Rig,Pose},runtime::*};
use bevy_math::{Vec3,Quat};use std::{fs,path::Path};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let root=std::env::args().nth(1).expect("owned project root");let root=Path::new(&root);let dir=root.join("game/DeliriumSA/combat");
 let asset=delirium_data::nm_asset::read(&fs::read(root.join("game/DeliriumSA/fred.xrt"))?);assert!(delirium_data::kv::unlock(&asset));
 let mut data=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?,&fs::read(dir.join("action_table.csv"))?,&fs::read(dir.join("pedpersonality.dat"))?)?;
 if dir.join("sa-contact-ranges.json").exists(){data.adapt_reach(&fs::read(dir.join("sa-contact-ranges.json"))?)?;}
 let mut totals=[0;2];
 for scene in 0..2{
  let mut rt=Runtime::new(Bank::load(&dir)?);
  for(id,model,player)in[(1,0,true),(2,7,false)]{rt.register(&data,id,player,"m_y_thief",Rig::from_json(&fs::read(root.join(format!("game/DeliriumSA/combat-rig-{model}.json")))?)?)?;}
  let mut positions=[Vec3::Z,Vec3::new(0.,1.15,1.)];let mut yaw=[0.,std::f32::consts::PI];
  for tick in 0..350u32{
   let now=1000+10*tick;
   for id in [1,2]{let a=&rt.actors[&id];let root=positions[id as usize-1];let q=Quat::from_rotation_z(yaw[id as usize-1]);
    let mut f=[0.;FRAME_FLOATS];f[..3].copy_from_slice(&root.to_array());f[3]=yaw[id as usize-1];f[4]=200.;f[5]=if id==1{(PLAYER|FIGHT|LOCK)as f32}else{0.};f[6]=if id==1{2.}else{-1.};f[8]=127.;f[9]=127.;f[10]=255.;f[7]=if id==1&&(100..135).contains(&tick){33.}else{1.};
    let mut raw=vec![0.;a.rig.ids.len()*7];for(k,b)in a.rig.bind.iter().enumerate(){Pose{p:root,q}.mul(*b).write(&mut raw[k*7..k*7+7]);}rt.feed(id,&f,&raw,now)?;
   }
   rt.advance(&data,now,0.01)?;
   for id in [1,2]{let i=id as usize-1;let a=&rt.actors[&id];if a.state[0]==1.{yaw[i]=a.state[1];positions[i]+=Quat::from_rotation_z(yaw[i])*Vec3::new(a.state[2],a.state[3],0.)*0.01;}rt.place(id,positions[i])?;}
   if scene==1{
    let source_state=rt.actors[&1].state;let source_key=rt.actors[&1].key.clone();let source_pose=rt.actors[&1].pose.clone();
    let current=rt.actors[&2].native.clone();let mut raw=vec![0.;current.len()*7];for(k,p)in current.iter().enumerate(){Pose{p:p.p+Vec3::X*5.,q:p.q}.write(&mut raw[k*7..k*7+7]);}
    rt.refresh_native_pose(2,&raw)?;
    assert_eq!(rt.actors[&1].state,source_state);assert_eq!(rt.actors[&1].key,source_key);
    assert!(rt.actors[&1].pose.iter().zip(source_pose).all(|(a,b)|a.p==b.p&&a.q==b.q));
    assert!(rt.actors[&2].pose.iter().all(|p|p.p.x>4.));
   }
   rt.finish(&data)?;if scene==0&&(95..180).contains(&tick)&&tick%5==0{eprintln!("tick {tick} root={:?} clip={} state={:?} hits={:?}",positions[0],rt.actors[&1].key,rt.actors[&1].state,rt.hits);}
   totals[scene]+=rt.hits.iter().filter(|h|h[0]==1.&&h[1]==2.&&h[15]==0.).count();
   assert!(rt.refresh_native_pose(2,&[]).is_err(),"native update after contact finish must fail");
  }
 }
 assert!(totals[0]>0,"control scene did not exercise a real strike");assert_eq!(totals[1],0,"stale passive palette still received phantom damage");
 println!("Real-clip contact baseline={} refreshed_current_target={} unchanged attacker clock/input/pose",totals[0],totals[1]);Ok(())
}
