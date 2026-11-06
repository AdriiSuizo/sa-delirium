//! Owned IV clip sampling and anatomical retargeting. No changes to source assets.
use bevy_math::{Mat3,Quat,Vec3};
use delirium_data::{anim::{self,Animation,Track},resource};
use serde::Deserialize;
use std::{collections::HashMap,path::Path};

#[derive(Clone,Copy,Debug)]
pub struct Pose { pub p:Vec3, pub q:Quat }
impl Pose {
    pub fn mul(self,b:Self)->Self{Self{p:self.p+self.q*b.p,q:(self.q*b.q).normalize()}}
    pub fn inverse(self)->Self{let q=self.q.inverse();Self{p:q*-self.p,q}}
    pub fn mix(self,b:Self,w:f32)->Self{Self{p:self.p.lerp(b.p,w),q:self.q.slerp(b.q,w)}}
    pub fn read(a:&[f32])->Self{Self{p:Vec3::from_slice(a),q:Quat::from_xyzw(a[3],a[4],a[5],a[6]).normalize()}}
    pub fn write(self,a:&mut[f32]){a[..3].copy_from_slice(&self.p.to_array());a[3..7].copy_from_slice(&self.q.to_array());}
}
#[derive(Deserialize)]
struct Bone {id:u16,parent:Option<usize>,translation:[f32;3],rotation:[f32;4]}
#[derive(Deserialize)]
struct Skeleton{bones:Vec<Bone>}
pub struct Bank {pub civilian_events:Option<crate::npc_decision::CivilianEvents>,pub clips:HashMap<String,Animation>,pub native:HashMap<String,crate::native_animation::Clip>,pub npc_decision:Option<lsc4_combat::decision::DecisionMaker>,bones:Vec<Bone>,bind:Vec<Pose>,world:Vec<Pose>,align:Quat}
pub fn sample_vec(t:&Track,phase:f32,frames:usize)->Vec3 {
    let x=phase.clamp(0.,1.)*(frames-1) as f32;let i=x.floor() as usize;
    Vec3::from_slice(&t.sample(i)).lerp(Vec3::from_slice(&t.sample(i+1)),x.fract())
}
pub fn sample_quat(t:&Track,phase:f32,frames:usize)->Quat {
    let x=phase.clamp(0.,1.)*(frames-1) as f32;let i=x.floor() as usize;
    let q=|i|{let a=t.sample(i);Quat::from_xyzw(a[0],a[1],a[2],a[3]).normalize()};q(i).slerp(q(i+1),x.fract())
}
/// Bone channels between their 30 Hz keys: a Catmull-Rom curve through the same keys. Each key is reproduced
/// exactly; at a higher frame rate the speed no longer steps at every key (linear segments read as 30 Hz).
/// The mover stays linear: travel, homing and the contact calibration read it.
fn key(t:&Track,i:usize,c:usize)->f32{let v=&t.values[c];v[i.min(v.len()-1)]}
fn cubic(a:f32,b:f32,c:f32,d:f32,u:f32)->f32{
    let (u2,u3)=(u*u,u*u*u);
    0.5*(2.*b+(c-a)*u+(2.*a-5.*b+4.*c-d)*u2+(3.*b-a-3.*c+d)*u3)
}
pub fn sample_vec_smooth(t:&Track,phase:f32,frames:usize)->Vec3 {
    let x=phase.clamp(0.,1.)*(frames-1) as f32;let i=x.floor() as usize;let u=x.fract();
    if t.values.len()<3||frames<3{return sample_vec(t,phase,frames)}
    let (h,j,k)=(i.saturating_sub(1),i+1,(i+2).min(frames-1));
    Vec3::new(cubic(key(t,h,0),key(t,i,0),key(t,j,0),key(t,k,0),u),cubic(key(t,h,1),key(t,i,1),key(t,j,1),key(t,k,1),u),cubic(key(t,h,2),key(t,i,2),key(t,j,2),key(t,k,2),u))
}
pub fn sample_quat_smooth(t:&Track,phase:f32,frames:usize)->Quat {
    let x=phase.clamp(0.,1.)*(frames-1) as f32;let i=x.floor() as usize;let u=x.fract();
    if t.values.len()<4||frames<3{return sample_quat(t,phase,frames)}
    let q=|n:usize|glam_quat(key(t,n,0),key(t,n,1),key(t,n,2),key(t,n,3));
    let b=q(i);let side=|o:Quat|if o.dot(b)<0.{-o}else{o};
    let (a,c,d)=(side(q(i.saturating_sub(1))),side(q(i+1)),side(q((i+2).min(frames-1))));
    let m=|f:fn(Quat)->f32|cubic(f(a),f(b),f(c),f(d),u);
    let out=Quat::from_xyzw(m(|q|q.x),m(|q|q.y),m(|q|q.z),m(|q|q.w));
    if out.length_squared()>1e-8{out.normalize()}else{b.slerp(c,u)}
}
fn glam_quat(x:f32,y:f32,z:f32,w:f32)->Quat{Quat::from_xyzw(x,y,z,w).normalize()}
pub fn mover(a:&Animation,phase:f32)->Vec3 {a.tracks.iter().find(|t|t.kind==anim::MOVER_TRANSLATION).map_or(Vec3::ZERO,|t|sample_vec(t,phase,a.frames))}
pub fn mover_q(a:&Animation,phase:f32)->Quat {a.mover_rotation.as_ref().map_or(Quat::IDENTITY,|t|sample_quat(t,phase,a.frames))}
pub fn movement_flags(key:&str)->Option<u32>{let name=key.rsplit('/').next()?;lsc4_combat::meleeanims::MOVES.iter().find(|m|m.0.eq_ignore_ascii_case(name)).map(|m|m.1[1])}
impl Bank {
    pub fn load(dir:&Path)->Result<Self,String>{
        let skeleton:Skeleton=serde_json::from_slice(&std::fs::read(dir.join("skeleton.json")).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
        let bones=skeleton.bones;
        if bones.is_empty()||bones.iter().enumerate().any(|(i,b)|b.parent.is_some_and(|p|p>=i)){return Err("invalid source skeleton".into())}
        let bind:Vec<_>=bones.iter().map(|b|Pose{p:Vec3::from_array(b.translation),q:Quat::from_array(b.rotation).normalize()}).collect();
        let world=Self::globals(&bones,&bind);
        let at=|id|bones.iter().position(|b|b.id==id).map(|i|world[i].p).ok_or(format!("missing IV bone {id}"));
        let up=(at(0x4b4)?-at(0x1a1)?).normalize();let r=at(0x1a7)?-at(0x1a2)?;let right=(r-up*r.dot(up)).normalize();
        let align=Quat::from_mat3(&Mat3::from_cols(right,up.cross(right),up)).inverse();
        let mut clips=HashMap::new();
        for e in std::fs::read_dir(dir).map_err(|e|e.to_string())? {
            let path=e.map_err(|e|e.to_string())?.path();if path.extension().and_then(|s|s.to_str())!=Some("xad"){continue}
            let dictionary=path.file_stem().unwrap().to_string_lossy().to_ascii_lowercase();
            let bytes=std::fs::read(&path).map_err(|e|e.to_string())?;
            for a in anim::read_dictionary(&resource::parse(&bytes)?)? {
                if a.frames==0||a.duration<=0.||!a.duration.is_finite(){return Err("invalid clip timing".into())}
                let key=format!("{dictionary}/{}",a.name).to_ascii_lowercase();clips.insert(key,a);
            }
        }
        if !clips.contains_key("move_melee/idle"){return Err("missing melee stance".into())}
        let mut native=HashMap::new();let path=dir.join("sa-gym-clips.json");
        if path.exists(){let file:crate::native_animation::File=serde_json::from_slice(&std::fs::read(path).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            if file.version!=1{return Err("unsupported native clip schema".into())}
            for c in file.clips{if !c.validate(){return Err(format!("invalid native clip {}",c.key))}clips.insert(c.key.clone(),c.clock());native.insert(c.key.clone(),c);}
        }
        let path=dir.join("decision/M_NO_NO.cmb");
        let npc_decision=Some(lsc4_combat::decision::DecisionMaker::parse(&std::fs::read_to_string(path).map_err(|e|format!("missing original combat decision: {e}"))?)?);
        let civilian_events=Some(crate::npc_decision::CivilianEvents::load(&dir.join("decision"))?);
        Ok(Self{clips,native,npc_decision,civilian_events,bones,bind,world,align})
    }
    fn globals(bones:&[Bone],local:&[Pose])->Vec<Pose>{let mut out:Vec<Pose>=vec![];for (i,b) in bones.iter().enumerate(){out.push(b.parent.map_or(local[i],|p|out[p].mul(local[i])))}out}
    pub fn pose(&self,key:&str,phase:f32,flags:u32)->Option<Vec<Pose>>{
        let a=self.clips.get(key)?;let mut local=self.bind.clone();
        for t in &a.tracks {let Some(i)=self.bones.iter().position(|b|b.id==t.bone) else{continue};
            match t.kind {anim::BONE_TRANSLATION=>local[i].p=sample_vec_smooth(t,phase,a.frames),anim::BONE_ROTATION=>local[i].q=sample_quat_smooth(t,phase,a.frames),_=>{}}
        }
        // Original player::with_mover: carried axes move the entity, the other
        // axes and the mover's lean belong in the pose. Root tracks replace bind.
        let root=self.bones.iter().position(|b|b.id==0)?;
        let has_p=a.tracks.iter().any(|t|t.bone==0&&t.kind==0);let has_q=a.tracks.iter().any(|t|t.bone==0&&t.kind==1);
        let m=mover(a,phase);let mut q=mover_q(a,phase);
        if flags&0x3000!=0 {let twist=Quat::from_xyzw(0.,0.,q.z,q.w);if twist.length_squared()>1e-8{q*=twist.normalize().inverse()}}
        if has_p {local[root].p+=Vec3::new(if flags&0x200==0{m.x}else{0.},if flags&0x400==0{m.y}else{0.},if flags&0x800==0{m.z}else{0.});}
        if has_q {local[root].q=q*local[root].q;}
        Some(Self::globals(&self.bones,&local))
    }
    pub fn retarget(&self,rig:&Rig,key:&str,phase:f32,flags:u32)->Option<Vec<Pose>>{
        if let Some(c)=self.native.get(key){return Some(c.pose(rig,phase,flags))}
        let source=self.pose(key,phase,flags)?;self.retarget_source(rig,&source)
    }
    /// Original BlendN on source local channels before transferring to SA.
    pub fn retarget_mix(&self,rig:&Rig,samples:&[(&str,f32,f32)])->Option<Vec<Pose>>{
        use lsc4_combat::animblend::{Dof,blend_n};
        let dofs=|local:&[Pose]|local.iter().enumerate().flat_map(|(i,p)|[
            Dof{track:0,id:self.bones[i].id,kind:0,invalid:false,value:[p.p.x,p.p.y,p.p.z,0.]},
            Dof{track:1,id:self.bones[i].id,kind:1,invalid:false,value:p.q.to_array()}]).collect::<Vec<_>>();
        let mut dest=dofs(&self.bind);let mut frames=vec![];let mut weights=vec![];
        for &(key,phase,weight) in samples {if weight<=0.{continue}
            let global=self.pose(key,phase,movement_flags(key)?)?;
            let local:Vec<_>=self.bones.iter().enumerate().map(|(i,b)|b.parent.map_or(global[i],|p|global[p].inverse().mul(global[i]))).collect();
            frames.push(dofs(&local));weights.push(weight);
        }
        if frames.is_empty(){return None}
        blend_n(&mut dest,&frames.iter().map(Vec::as_slice).collect::<Vec<_>>(),&weights,None);
        let local:Vec<_>=dest.chunks_exact(2).map(|d|Pose{p:Vec3::from_slice(&d[0].value),q:Quat::from_array(d[1].value).normalize()}).collect();
        self.retarget_source(rig,&Self::globals(&self.bones,&local))
    }
    fn correction(&self,rig:&Rig,i:usize,s:usize)->Quat{
        rig.ids.iter().enumerate().find_map(|(child,&id)|{
            if rig.parents[child]!=Some(i){return None}
            let next=iv_bone(id).and_then(|id|self.bones.iter().position(|b|b.id==id))?;
            let from=rig.bind[child].p-rig.bind[i].p;let to=self.align*(self.world[next].p-self.world[s].p);
            (from.length_squared()>0.0001&&to.length_squared()>0.0001).then(||Quat::from_rotation_arc(from.normalize(),to.normalize()))
        }).unwrap_or(Quat::IDENTITY)
    }
    /// Undo the rest-pose correction before reading original IV bone axes.
    pub fn original_axes(&self,rig:&Rig,pose:&[Pose],sa:u32)->Option<Quat>{
        let i=rig.ids.iter().position(|&id|id==sa)?;
        let s=self.bones.iter().position(|b|Some(b.id)==iv_bone(sa))?;
        Some((pose[i].q*rig.bind[i].q.inverse()*self.correction(rig,i,s).inverse()*self.align*self.world[s].q).normalize())
    }
    fn retarget_source(&self,rig:&Rig,source:&[Pose])->Option<Vec<Pose>>{
        let mut out:Vec<Pose>=vec![];
        let root=self.bones.iter().position(|b|b.id==0)?;
        let root_delta=self.align*(source[root].p-self.world[root].p);
        for i in 0..rig.ids.len(){
            let src=iv_bone(rig.ids[i]).and_then(|id|self.bones.iter().position(|b|b.id==id));
            // The skins do NOT share a rest pose (SA's upper arms slope down).
            // Match each anatomical segment's rest direction before transferring
            // IV's rotation; a raw world delta leaves jabs bent into the chest.
            let correction=src.map(|s|self.correction(rig,i,s)).unwrap_or(Quat::IDENTITY);
            let q=src.map(|s|self.align*source[s].q*self.world[s].q.inverse()*self.align.inverse()*correction*rig.bind[i].q)
                .unwrap_or_else(||rig.parents[i].map_or(rig.bind[i].q,|p|out[p].q*rig.local[i].q)).normalize();
            let p=rig.parents[i].map_or(rig.bind[i].p+root_delta,|p|out[p].p+out[p].q*rig.local[i].p);
            out.push(Pose{p,q});
        }
        Some(out)
    }
}
// Actual SA HAnim tags (eBoneTag), not legacy SDK's mislabeled ePedBones.
// 6/7/8 are brows/jaw: inherit their local facial pose from head 5. They must
// never consume the IV head/neck rotation as an independent body segment.
pub fn iv_bone(sa:u32)->Option<u16>{Some(match sa{0=>0,1=>0x1a1,2=>0x4b2,3=>0x36a1,4=>0x4b4,5=>0x4b5,41=>0x1a2,42=>0x1a3,43=>0x1a4,44=>0x1a5,51=>0x1a7,52=>0x1a8,53=>0x1a9,54=>0x4b0,31=>0x4c0,32=>0x4c1,33=>0x4c2,34=>0x4c3,35=>0x35d6,36=>0x35d7,21=>0x4c7,22=>0x4c8,23=>0x4c9,24=>0x4d0,25=>0x35b6,26=>0x35b7,_=>return None})}
#[cfg(test)]mod anatomy_tests{
    use super::*;
    #[test]fn cj_facial_children_keep_their_local_pose_under_a_turning_head(){
        let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
        let bank=Bank::load(Path::new(&std::env::var("NM_TEST_COMBAT").unwrap())).unwrap();
        let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
        let head=rig.ids.iter().position(|&id|id==5).unwrap();
        let pelvis=rig.ids.iter().position(|&id|id==1).unwrap();
        for thigh in [41,51]{let i=rig.ids.iter().position(|&id|id==thigh).unwrap();assert_eq!(rig.parents[i],Some(pelvis));}
        for key in ["move_melee/idle","move_player/run"]{for phase in [0.,0.25,0.5,0.75]{
            let pose=bank.retarget(&rig,key,phase,movement_flags(key).unwrap()).unwrap();
            for id in [6,7,8]{
                let i=rig.ids.iter().position(|&x|x==id).unwrap();assert_eq!(rig.parents[i],Some(head));assert_eq!(iv_bone(id),None);
                let local=pose[head].inverse().mul(pose[i]);
                assert!(local.p.distance(rig.local[i].p)<1e-5);assert!(local.q.dot(rig.local[i].q).abs()>0.99999);
            }
        }}
        assert_eq!(iv_bone(5),Some(0x4b5));assert_eq!(iv_bone(4),Some(0x4b4));
    }
}
#[derive(Clone)]
pub struct Rig {pub ids:Vec<u32>,pub parents:Vec<Option<usize>>,pub bind:Vec<Pose>,pub local:Vec<Pose>,pub parts:Vec<crate::geometry::Part>}
impl Rig {
    pub fn from_json(bytes:&[u8])->Result<Self,String>{
        #[derive(Deserialize)]struct Export{ids:Vec<u32>,parents:Vec<u32>,bind:Vec<f32>,parts:Vec<crate::geometry::Part>}
        let x:Export=serde_json::from_slice(bytes).map_err(|e|e.to_string())?;
        let mut rig=Self::new(&x.ids,&x.parents,&x.bind)?;rig.set_parts(x.parts)?;Ok(rig)
    }
    pub fn new(ids:&[u32],parents:&[u32],bind:&[f32])->Result<Self,String>{
        if ids.len()<20||ids.len()>128||parents.len()!=ids.len()||bind.len()!=ids.len()*7||bind.iter().any(|x|!x.is_finite()){return Err("invalid SA rig".into())}
        let parents:Vec<_>=parents.iter().enumerate().map(|(i,&p)|if p==u32::MAX{Ok(None)}else if (p as usize)<i{Ok(Some(p as usize))}else{Err("non-topological SA rig")}).collect::<Result<_,_>>()?;
        if bind.chunks_exact(7).any(|b|(b[3..7].iter().map(|x|x*x).sum::<f32>()-1.).abs()>0.01){return Err("invalid SA quaternion".into())}
        let bind:Vec<_>=bind.chunks_exact(7).map(Pose::read).collect();
        let local=bind.iter().enumerate().map(|(i,&b)|parents[i].map_or(b,|p|bind[p].inverse().mul(b))).collect();
        Ok(Self{ids:ids.to_vec(),parents,bind,local,parts:vec![]})
    }
    pub fn set_parts(&mut self,parts:Vec<crate::geometry::Part>)->Result<(),String>{
        if parts.len()!=21||parts.iter().any(|p|!p.valid(self.ids.len())){return Err("invalid fitted combat bounds".into())}
        self.parts=parts;Ok(())
    }
    pub fn blend(&self,a:&[Pose],b:&[Pose],weight:f32,flags:u32)->Vec<Pose>{
        let mut out:Vec<Pose>=vec![];
        for i in 0..self.ids.len(){let sa=self.ids[i];let selected=if flags==0x208001{sa>=2&&sa<9||sa>=21&&sa<=36}else if flags==0x8008{sa>=4&&sa<=8||sa>=21&&sa<=36}else{true};let w=if selected{weight}else{0.};
            let t=if let Some(p)=self.parents[i]{let aa=a[p].inverse().mul(a[i]);let bb=b[p].inverse().mul(b[i]);let mut local=aa.mix(bb,w);local.p=self.local[i].p;out[p].mul(local)}else{a[i].mix(b[i],w)};out.push(t);
        }out
    }
}

#[cfg(test)]
mod smooth_tests {
    use super::*;
    #[test]
    fn smooth_sampling_keeps_every_key_and_the_speed_continuous() {
        let xs:Vec<f32>=(0..8).map(|i|(i as f32*0.7).sin()).collect();
        let t=Track{kind:anim::BONE_TRANSLATION,bone:0,values:vec![xs.clone(),xs.clone(),xs.clone()]};
        for i in 0..8 {let p=sample_vec_smooth(&t,i as f32/7.,8);assert!((p.x-xs[i]).abs()<1e-5);}
        // Speed either side of key 3: linear steps, the curve does not.
        let v=|a:f32,b:f32|(sample_vec_smooth(&t,b/7.,8).x-sample_vec_smooth(&t,a/7.,8).x)/(b-a);
        assert!((v(2.99,3.0)-v(3.0,3.01)).abs()<0.01);
        let q=|a:f32|Quat::from_rotation_z(a);
        let qs:Vec<Quat>=(0..6).map(|i|q(i as f32*0.3)).collect();
        let r=Track{kind:anim::BONE_ROTATION,bone:0,values:(0..4).map(|c|qs.iter().map(|q|q.to_array()[c]).collect()).collect()};
        for i in 0..6 {assert!(sample_quat_smooth(&r,i as f32/5.,6).angle_between(qs[i])<1e-4);}
    }
}
