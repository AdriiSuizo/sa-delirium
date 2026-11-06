//! Original IV secondary shove clip, on tracked head/arm channels only.
use crate::animation::{Bank,Pose,Rig,iv_bone};
use lsc4_combat::{playerbump as pb,animblend};
pub struct Shove {pub rig:Rig,pub key:&'static str,pub clip:u32,pub player:animblend::Player}
pub fn select(r:&[f32],flags:u32)->Option<u32>{
    let b=pb::Bump{my_forward:[r[0],r[1],r[2]],other_forward:[r[3],r[4],r[5]],d:[r[6],r[7],r[8]],
        me:r[12] as i32,other:r[13] as i32,health:r[14],my_weakness:r[15],other_weakness:r[16],
        friend:flags&1!=0,shoved_last:flags&2!=0,in_formation:flags&4!=0};
    // Side is only consumed by Fall/HitResponse. This boundary accepts Shove;
    // the other answers remain with SA, never converted into a forced fall.
    if pb::answer(&b,||0)!=pb::Answer::Shove{return None}
    pb::shove_clip([r[9],r[10],r[11]],[-r[6],-r[7],-r[8]],b.me>=3,flags&8!=0)
}
impl Shove {
    pub fn new(bank:&Bank,rig:Rig,clip:u32)->Option<Self>{
        let key=pb::SHOVES.iter().find(|(c,_)|*c==clip)?.1;
        let mut player=animblend::Player::new(bank.clips.get(key)?.duration);
        player.weight=0.;player.active=true;player.playing=true;player.blend_out_at_end=true;
        player.remove_blended_out=true;player.priority=4;player.mask=8;player.set_blend(8.);
        Some(Self{rig,key,clip,player})
    }
    pub fn step(&mut self,dt:f32)->bool {self.player.update(dt);!self.player.blend_weight(dt)}
    pub fn pose(&self,bank:&Bank,under:&[Pose])->Option<Vec<Pose>>{
        if under.len()!=self.rig.ids.len(){return None}
        let over=bank.retarget(&self.rig,self.key,self.player.phase,pb::SHOVE_FLAGS)?;
        let clip=bank.clips.get(self.key)?;let mut out:Vec<Pose>=vec![];
        for (i,&sa) in self.rig.ids.iter().enumerate(){
            let tracked=iv_bone(sa).is_some_and(|id|clip.tracks.iter().any(|t|t.bone==id&&t.kind<=1));
            // IV mask 8 is HEAD_NECK_AND_ARMS: neck (4) included.
            let weight=if tracked&&(sa==4||sa==5||sa==8||(21..=36).contains(&sa)){self.player.weight}else{0.};
            let t=if let Some(p)=self.rig.parents[i]{
                let a=under[p].inverse().mul(under[i]);let b=over[p].inverse().mul(over[i]);
                let mut local=a.mix(b,weight);local.p=a.p;out[p].mul(local)
            }else{under[i]};out.push(t);
        }Some(out)
    }
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn original_side_and_last_peer_rules(){
  let mut r=[0.,1.,0.,0.,-1.,0.,0.,0.45,0.,1.,0.,0.,3.,1.,200.,1.,1.];
  assert_eq!(select(&r,0),Some(0x96));r[6]=0.3;assert_eq!(select(&r,0),Some(0x9a));
  r[6]=-0.3;assert_eq!(select(&r,0),Some(0x99));assert_eq!(select(&r,2),None);
  assert_eq!(select(&r,1),None);r[14]=99.;assert_eq!(select(&r,0),None);
 }
 #[test]fn original_running_shove_leaves_legs_root_and_untracked_arm_alone(){
  let bank=Bank::load(std::path::Path::new(&std::env::var("NM_TEST_COMBAT").expect("NM_TEST_COMBAT"))).unwrap();
  let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").expect("NM_TEST_RIG")).unwrap()).unwrap();
  let mut s=Shove::new(&bank,rig,0x99).unwrap();assert!(s.step(0.1));
  let under=s.rig.bind.clone();let out=s.pose(&bank,&under).unwrap();let mut arm_motion=0.0f32;
  for(i,&id)in s.rig.ids.iter().enumerate(){
   let local=|pose:&[Pose]|s.rig.parents[i].map_or(pose[i],|p|pose[p].inverse().mul(pose[i]));
   let a=local(&under);let b=local(&out);
   if id<=3||id>=41||(21..=26).contains(&id){assert!(a.p.distance(b.p)<1e-5);assert!(a.q.dot(b.q).abs()>0.99999,"untouched channel {id}");}
   if (31..=36).contains(&id){arm_motion=arm_motion.max(1.-a.q.dot(b.q).abs());}
  }
  assert!(arm_motion>0.001,"tracked left arm must actually move");
  let mut ended=false;for _ in 0..100{if !s.step(0.1){ended=true;break}}assert!(ended,"original one-shot must yield");
 }
}
