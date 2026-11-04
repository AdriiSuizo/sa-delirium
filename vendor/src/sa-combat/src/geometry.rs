//! Fitted articulated bounds, attached to the same bone poses that are rendered.
use crate::{animation::Pose,controller as ct};
use bevy_math::Vec3;
use serde::{Serialize,Deserialize};
#[derive(Clone,Serialize,Deserialize)]
pub struct Part {
    pub tag:u16, pub nodes:[usize;2], pub weight:f32, pub offset:[f32;7],
    /// Virtual subdivisions share their native rigid segment's orientation.
    /// None preserves older exported rigs which interpolated complete poses.
    #[serde(default)] pub rotation_node:Option<usize>,
    pub radius:f32, pub half:f32, pub boxed:Option<[f32;3]>,
}
impl Part {
    pub fn valid(&self,bones:usize)->bool {
        self.nodes.iter().all(|&i|i<bones)&&self.rotation_node.is_none_or(|i|i<bones)&&self.weight.is_finite()&&(0.0..=1.0).contains(&self.weight)
        &&self.offset.iter().all(|x|x.is_finite())&&(self.offset[3..].iter().map(|x|x*x).sum::<f32>()-1.).abs()<0.01
        &&self.radius.is_finite()&&self.radius>0.&&self.half.is_finite()&&self.half>=0.
        &&self.boxed.is_none_or(|b|b.iter().all(|x|x.is_finite()&&*x>0.))
    }
    pub fn joint(&self,pose:&[Pose])->Pose {
        let mut joint=pose[self.nodes[0]].mix(pose[self.nodes[1]],self.weight);
        if let Some(i)=self.rotation_node{joint.q=pose[i].q;}
        joint
    }
    pub fn frame(&self,pose:&[Pose])->Pose {self.joint(pose).mul(Pose::read(&self.offset))}
    pub fn axis(&self,pose:&[Pose])->(Vec3,Vec3,f32,f32){
        let t=self.frame(pose);let boxed=self.boxed.map(Vec3::from_array);
        let centroid=boxed.map_or(self.half+self.radius,|e|e.length());
        let(a,b,own)=ct::bound_axis(|v|t.p+t.q*v,self.radius,self.half,boxed,centroid);
        // IV's authored sphere radius is not a new volume around SA's hand.
        // Fit each sample inside the real cross-section of this fragment.
        let thickness=boxed.map_or(self.radius,|e|e.min_element());
        (a,b,own,thickness)
    }
    pub fn capsule(&self,pose:&[Pose])->(Vec3,Vec3,f32,u16){
        let t=self.frame(pose);let(a,b,r)=ct::frag_capsule(|v|t.p+t.q*v,self.radius,self.half,self.boxed.map(Vec3::from_array));(a,b,r,self.tag)
    }
    pub fn bound(&self,pose:&[Pose])->Bound{Bound{tag:self.tag,frame:self.frame(pose),radius:self.radius,half:self.half,boxed:self.boxed.map(Vec3::from_array)}}
}
#[derive(Clone,Copy)]
pub struct Bound{pub tag:u16,pub frame:Pose,pub radius:f32,pub half:f32,pub boxed:Option<Vec3>}
impl Bound{
    /// Exact swept sphere against a capsule or oriented box. No enclosing
    /// capsule for boxes: that admits empty space near hands/shoulders.
    pub fn contact(&self,p:Vec3,q:Vec3,r:f32)->Option<(f32,u16,Vec3,Vec3)>{
        self.sweep_contact(p,q,r).map(|(_,hit)|hit)
    }
    pub fn sweep_contact(&self,p:Vec3,q:Vec3,r:f32)->Option<(f32,(f32,u16,Vec3,Vec3))>{
        let inv=self.frame.inverse();let local=|p|inv.p+inv.q*p;
        let (p,q)=(local(p),local(q));let travel=q-p;
        let (x,y,body_radius)=if let Some(e)=self.boxed{let(x,y)=segment_box(p,q,e);(x,y,0.)}
        else{let(x,y)=segments(p,q,-Vec3::Y*self.half,Vec3::Y*self.half);(x,y,self.radius)};
        let distance=x.distance(y);let gap=distance-body_radius-r;
        if gap>0.{return None}
        // The closest point on the WHOLE sweep establishes overlap, but its
        // normal is tangential to travel (or already points out the far side).
        // Momentum needs the entering contact, not maximum penetration. The
        // distance to either convex bound is convex on this segment: bracket
        // its first root between the start and that known closest point.
        let nearest=|v:Vec3|if let Some(e)=self.boxed{v.clamp(-e,e)}
            else{Vec3::Y*v.y.clamp(-self.half,self.half)};
        let radius=body_radius+r;
        let(mut lo,mut hi)=(0.,if travel.length_squared()>1e-12{((x-p).dot(travel)/travel.length_squared()).clamp(0.,1.)}else{0.});
        if p.distance_squared(nearest(p))>radius*radius{
            for _ in 0..24{let mid=(lo+hi)*0.5;let at=p+travel*mid;
                if at.distance_squared(nearest(at))>radius*radius{lo=mid}else{hi=mid}}
        }else{hi=0.;}
        let x=p+travel*hi;let y=nearest(x);let distance=x.distance(y);
        let n=if distance>1e-6{(y-x)/distance}else{travel.normalize_or_zero()};
        // An already intersecting sample that is retreating is not a fresh
        // strike. Reject it before damage/reaction selection, not afterwards
        // by silently applying zero momentum to an accepted damaging hit.
        if travel.dot(n)<=0.{return None}
        let surface=y-n*body_radius;
        Some((hi,(gap,self.tag,self.frame.p+self.frame.q*surface,self.frame.q*n)))
    }
}
pub fn first_contact(bounds:&[Bound],p:Vec3,q:Vec3,r:f32)->Option<(f32,u16,Vec3,Vec3)>{
    bounds.iter().filter_map(|b|b.sweep_contact(p,q,r)).min_by(|a,b|a.0.total_cmp(&b.0)).map(|(_,hit)|hit)
}
fn segments(p:Vec3,q:Vec3,a:Vec3,b:Vec3)->(Vec3,Vec3){
    let(d,e,w)=(q-p,b-a,p-a);let(aa,ee,f)=(d.length_squared(),e.length_squared(),e.dot(w));
    let(s,t)=if aa<=1e-12&&ee<=1e-12{(0.,0.)}else if aa<=1e-12{(0.,(f/ee).clamp(0.,1.))}else{
        let c=d.dot(w);if ee<=1e-12{((-c/aa).clamp(0.,1.),0.)}else{let bb=d.dot(e);let den=aa*ee-bb*bb;let s=if den>1e-12{((bb*f-c*ee)/den).clamp(0.,1.)}else{0.};let t=(bb*s+f)/ee;
            if t<0.{((-c/aa).clamp(0.,1.),0.)}else if t>1.{(((bb-c)/aa).clamp(0.,1.),1.)}else{(s,t)}}};(p+d*s,a+e*t)
}
fn segment_box(p:Vec3,q:Vec3,e:Vec3)->(Vec3,Vec3){
    // Squared distance to an AABB is a convex piecewise quadratic along the
    // segment. Test interval extrema, including the rounded corners.
    let d=q-p;let mut cuts=vec![0.,1.];
    for i in 0..3{if d[i].abs()>1e-8{for edge in [-e[i],e[i]]{let t=(edge-p[i])/d[i];if t>0.&&t<1.{cuts.push(t)}}}}
    cuts.sort_by(f32::total_cmp);let mut best=(f32::INFINITY,p,p.clamp(-e,e));
    let mut test=|t:f32|{let x=p+d*t;let y=x.clamp(-e,e);let dist=x.distance_squared(y);if dist<best.0{best=(dist,x,y)}};
    for w in cuts.windows(2){test(w[0]);test(w[1]);let mid=p+d*((w[0]+w[1])*0.5);let(mut aa,mut bb)=(0.,0.);
        for i in 0..3{if mid[i] < -e[i]||mid[i]>e[i]{let edge=if mid[i]<0.{-e[i]}else{e[i]};aa+=d[i]*d[i];bb+=d[i]*(p[i]-edge)}}
        if aa>1e-12{test((-bb/aa).clamp(w[0],w[1]));}}
    (best.1,best.2)
}
#[cfg(test)]mod tests{
 use super::*;use bevy_math::Quat;
 #[test]fn rounded_box_does_not_fill_the_corner(){
  let b=Bound{tag:1,frame:Pose{p:Vec3::ZERO,q:Quat::IDENTITY},radius:0.1,half:0.2,boxed:Some(Vec3::ONE)};
  assert!(b.contact(Vec3::new(1.08,1.08,-2.),Vec3::new(1.08,1.08,2.),0.1).is_none());
  assert!(b.contact(Vec3::new(1.06,1.06,-2.),Vec3::new(1.06,1.06,2.),0.1).is_some());
 }
 #[test]fn rotated_bound_tracks_the_hand(){
  let b=Bound{tag:1,frame:Pose{p:Vec3::ZERO,q:Quat::from_rotation_z(std::f32::consts::FRAC_PI_2)},radius:0.03,half:0.12,boxed:None};
  assert!(b.contact(Vec3::new(0.11,-1.,0.),Vec3::new(0.11,1.,0.),0.02).is_some());
  assert!(b.contact(Vec3::new(-1.,0.11,0.),Vec3::new(1.,0.11,0.),0.02).is_none());
 }
 #[test]fn fast_sweep_reports_entry_normal_and_surface_not_deepest_overlap(){
  let b=Bound{tag:12,frame:Pose{p:Vec3::ZERO,q:Quat::IDENTITY},radius:0.1,half:0.05,boxed:None};
  let(_,tag,point,n)=b.contact(Vec3::new(-0.3,0.,0.),Vec3::new(0.3,0.,0.),0.03).unwrap();
  assert_eq!(tag,12);assert!(n.dot(Vec3::X)>0.999);
  assert!(point.distance(Vec3::new(-0.1,0.,0.))<1e-5);
  // A grazing strike still supplies an inward normal with finite closing speed.
  let(_,_,point,n)=b.contact(Vec3::new(-0.3,0.,0.10),Vec3::new(0.3,0.,0.10),0.03).unwrap();
  assert!(n.x>0.5&&n.z<0.);assert!((point.length()-0.1).abs()<1e-5);
 }
 #[test]fn box_sweep_uses_entry_face_and_preserves_separating_overlap(){
  let b=Bound{tag:10,frame:Pose{p:Vec3::ZERO,q:Quat::IDENTITY},radius:0.1,half:0.,boxed:Some(Vec3::splat(0.1))};
  let(_,_,point,n)=b.contact(Vec3::new(-0.3,0.,0.),Vec3::new(0.3,0.,0.),0.025).unwrap();
  assert!(point.distance(Vec3::new(-0.1,0.,0.))<1e-5);assert!(n.x>0.999);
  // Existing overlap retreating out of the volume is not a new inward impulse.
  assert!(b.contact(Vec3::new(-0.12,0.,0.),Vec3::new(-0.3,0.,0.),0.025).is_none());
 }
 #[test]fn first_surface_wins_over_a_deeper_body_part(){
  let make=|tag,x,r|Bound{tag,frame:Pose{p:Vec3::new(x,0.,0.),q:Quat::IDENTITY},radius:r,half:0.,boxed:None};
  let bounds=[make(12,0.15,0.15),make(13,-0.15,0.05)];
  let(_,tag,point,_)=first_contact(&bounds,Vec3::new(-0.5,0.,0.),Vec3::new(0.5,0.,0.),0.025).unwrap();
  assert_eq!(tag,13);assert!((point.x+0.2).abs()<1e-5);
 }
 #[test]fn small_hand_does_not_hit_through_the_authored_sphere_padding(){
  let pose=[Pose{p:Vec3::ZERO,q:Quat::IDENTITY}];
  let hand=Part{tag:0x4c3,nodes:[0,0],weight:0.,rotation_node:None,offset:[0.,0.,0.,0.,0.,0.,1.],radius:0.06,half:0.05,boxed:Some([0.04,0.09,0.025])};
  let(_,_,_,thickness)=hand.axis(&pose);
  let head=Bound{tag:0x4b5,frame:Pose{p:Vec3::new(0.,0.3,0.),q:Quat::IDENTITY},radius:0.1,half:0.,boxed:None};
  let(from,to)=(Vec3::new(-0.01,0.1,0.),Vec3::new(0.01,0.1,0.));
  assert!(head.contact(from,to,0.15).is_some()); // former false positive
  assert!(head.contact(from,to,0.15f32.min(thickness)).is_none());
  assert!(head.contact(from+Vec3::Y*0.1,to+Vec3::Y*0.1,thickness).is_some());
 }
}
