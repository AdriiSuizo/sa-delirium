//! Original SA local tracks on their own skeleton; IV owns the action clock.
use crate::animation::{Rig,Pose};
use bevy_math::{Quat,Vec3};
use delirium_data::anim::{Animation,Track,MOVER_TRANSLATION};
use serde::Deserialize;
#[derive(Deserialize,Clone)]pub struct Key{pub time:f32,pub q:[f32;4],pub p:Option<[f32;3]>}
#[derive(Deserialize,Clone)]pub struct Sequence{pub bone:u32,pub frames:Vec<Key>}
#[derive(Deserialize,Clone)]pub struct Clip{pub key:String,pub style:u32,pub step:usize,pub reach:f32,pub duration:f32,pub hit:f32,pub chain:f32,pub strike_start:f32,pub tracks:Vec<Sequence>,#[serde(default)]pub receiver:bool}
#[derive(Deserialize)]pub struct File{pub version:u32,pub clips:Vec<Clip>}
impl Sequence{
    fn sample(&self,t:f32)->Pose{
        let j=self.frames.partition_point(|k|k.time<t).min(self.frames.len()-1);let i=j.saturating_sub(1);
        let(a,b)=(&self.frames[i],&self.frames[j]);let w=if b.time>a.time{((t-a.time)/(b.time-a.time)).clamp(0.,1.)}else{0.};
        Pose{p:Vec3::from_array(a.p.unwrap_or([0.;3])).lerp(Vec3::from_array(b.p.unwrap_or([0.;3])),w),q:Quat::from_array(a.q).normalize().slerp(Quat::from_array(b.q).normalize(),w)}
    }
}
impl Clip{
    pub fn validate(&self)->bool{self.reach>0.&&self.reach.is_finite()&&self.duration>0.&&self.duration.is_finite()&&self.hit>0.&&self.hit<self.duration&&self.strike_start>=0.&&self.strike_start<self.hit&&
        self.tracks.iter().all(|t|!t.frames.is_empty()&&t.frames.windows(2).all(|w|w[0].time<=w[1].time)&&t.frames.iter().all(|k|k.time.is_finite()&&k.q.iter().all(|v|v.is_finite())&&Quat::from_array(k.q).length_squared()>0.5))}
    pub fn pose(&self,rig:&Rig,phase:f32,flags:u32)->Vec<Pose>{
        let mut out:Vec<Pose>=vec![];let t=phase.clamp(0.,1.)*self.duration;
        for(i,&id)in rig.ids.iter().enumerate(){let mut local=rig.local[i];
            if let Some(track)=self.tracks.iter().find(|s|s.bone==id){let frame=track.sample(t);local.q=frame.q;
                if rig.parents[i].is_none()&&track.frames[0].p.is_some(){local.p=frame.p;if flags&0x200!=0{local.p.x=0.}if flags&0x400!=0{local.p.y=0.}}
            }
            out.push(rig.parents[i].map_or(local,|p|out[p].mul(local)));
        }out
    }
    pub fn clock(&self)->Animation{
        let frames=(self.duration*60.).round() as usize+1;let mut values=vec![vec![];3];
        let root=self.tracks.iter().find(|t|t.bone==0);
        for i in 0..frames{let p=root.map_or(Vec3::ZERO,|t|t.sample(i as f32/60.).p);for j in 0..3{values[j].push(p[j]);}}
        let hz=delirium_data::kv!(80);let mut events=vec![0;(self.duration*hz).ceil() as usize+2];
        let on=(self.hit*hz).round() as usize;let off=(on+1).min(events.len()-1);
        if self.receiver{events[on]|=0x200}else{events[((self.strike_start*hz).floor()as usize).max(1).min(on)]|=0x200;events[off]|=0x200;}
        Animation{name:self.key.clone(),hash:crate::hash(&self.key),duration:self.duration,frames,tracks:vec![Track{kind:MOVER_TRANSLATION,bone:0,values}],mover_rotation:None,events}
    }
}
