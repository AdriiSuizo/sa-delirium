//! Cheap conservative rejection BEFORE the original moving-child broadphase.
//! It must contain even that broadphase's false positives: warm manifold
//! allocation/clearing/order is unchanged, not merely the final visible pose.
use super::*;
fn abs(m:Mat3)->Mat3{Mat3::from_cols(m.x_axis.abs(),m.y_axis.abs(),m.z_axis.abs())}
// What the children of one frame share: a vehicle's triangles all do, and the matrix products
// were most of what preparing them cost (07/11). The arithmetic is swept()'s, in the same order.
pub(super)struct Frames{now:(Vec3,Mat3),last:(Vec3,Mat3),projection:Mat3,absolute:Mat3,turn:Mat3}
impl Frames{
 pub(super)fn new(now:(Vec3,Mat3),last:(Vec3,Mat3))->Self{
  let projection=now.1.transpose();
  Self{now,last,projection,absolute:abs(projection),turn:abs(projection*last.1)}
 }
 pub(super)fn child(&self,centre:Vec3,half:Vec3)->Rejection{
  let t=self.projection*((self.last.0+self.last.1*centre)-(self.now.0+self.now.1*centre));
  let h=self.turn*half;
  let(lo,hi)=((-half).min(t-h),half.max(t+h));
  Rejection{projection:self.projection,absolute:self.absolute,origin:self.now.0,centre:centre+(lo+hi)*0.5,half:(hi-lo)*0.5}
 }
}
#[derive(Clone,Copy)]
pub(super)struct Rejection{projection:Mat3,absolute:Mat3,origin:Vec3,centre:Vec3,half:Vec3}
impl Rejection{
 pub(super)fn new(now:(Vec3,Mat3),last:(Vec3,Mat3),centre:Vec3,half:Vec3)->Self{Frames::new(now,last).child(centre,half)}
 pub(super)fn body(now:(Vec3,Mat3),sweep:(Vec3,Vec3))->(Vec3,Vec3){(now.0+now.1*sweep.0,abs(now.1)*sweep.1)}
 pub(super)fn allows(&self,body:(Vec3,Vec3))->bool{
  // abs(P*M)*h <= abs(P)*(abs(M)*h) for nonnegative h. This avoids the
  // per-child matrix product and swept-child construction for distant shapes.
  // The original predicate still runs for every surviving candidate.
  let distance=(self.projection*(body.0-self.origin)-self.centre).abs();
  let reach=self.absolute*body.1+self.half+Vec3::splat(delirium_data::kv!(571));
  if !distance.is_finite()||!reach.is_finite()||body.1.min_element()<0.{return true;}
  // Outward roundoff allowance only, scaled to the operations' magnitudes;
  // never changes narrowphase margins, force, contact targets or constraints.
  let roundoff=distance.max(reach.abs()).max(Vec3::ONE)*(64.*f32::EPSILON);
  distance.cmple(reach+roundoff).all()
 }
 pub(super)fn meets(&self,body:(Vec3,Vec3),body_rotation:Mat3,body_half:Vec3)->bool{
  if !self.allows(body){return false;}
  // Original predicate, with only the child-only swept box reused. Keep the
  // same operation order and padding for its exact decision, including warm
  // contacts. Narrowphase still receives its original frames and shapes.
  let distance=(self.projection*(body.0-self.origin)-self.centre).abs();
  let relative=self.projection*body_rotation;
  let half=abs(relative)*body_half;
  distance.cmple(half+self.half+Vec3::splat(delirium_data::kv!(571))).all()
 }
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn cheap_rejection_contains_original_moving_child_acceptance(){
  super::super::moving_obstacle_regression::constants();
  let mut seed=18273u32;let mut draw=||{seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);(seed>>8)as f32/(1u32<<24)as f32};
  let(mut accepted,mut rejected)=(0,0);
  for i in 0..100000{
   let rotation=|v:f32|Mat3::from_quat(Quat::from_rotation_z(v)*Quat::from_rotation_y(v*0.7));
   let scale=if i%3==0{10000.}else{10.};
   let cx=Vec3::new((draw()-0.5)*scale,(draw()-0.5)*scale,(draw()-0.5)*scale);
   let cq=rotation(draw()*6.);let old=(cx+Vec3::new(draw()-0.5,draw()-0.5,0.),rotation(draw()*6.));
   let centre=Vec3::new((draw()-0.5)*20.,(draw()-0.5)*20.,(draw()-0.5)*20.);
   let half=Vec3::new(draw(),draw(),draw());
   let x=cx+cq*centre+Vec3::new((draw()-0.5)*5.,(draw()-0.5)*5.,(draw()-0.5)*5.);let tm=rotation(draw()*6.);
   let old_a=(x+Vec3::new(draw()-0.5,draw()-0.5,draw()-0.5),rotation(draw()*6.));
   let a=(x,tm);let sa=swept(a,old_a,Vec3::new(draw(),draw(),draw()));
   let exact=moving_child_meets(a,sa,(cx,cq),old,centre,half);
   let prepared=Rejection::new((cx,cq),old,centre,half);let body=Rejection::body(a,sa);
   let cheap=prepared.allows(body);
   assert_eq!(prepared.meets(body,tm,sa.1),exact,"cached exact predicate at sample{i}");
   if exact{accepted+=1;assert!(cheap,"false rejection at sample{i}");}if !cheap{rejected+=1;}
  }
  assert!(accepted>1000&&rejected>1000,"must exercise both decisions: {accepted} {rejected}");
 }
}

// A mesh exposes many consecutive children in the same frame. Reject a batch
// only when the UNION of their original predicate boxes cannot meet the body.
// This is not a new geometric/contact test; survivors use Rejection::meets.
pub(super)const BATCH:usize=32;
pub(super)struct Prepared{pub children:Vec<Rejection>,batches:Vec<Option<(Mat3,Vec3,Vec3,Vec3)>>}
impl Prepared{
 pub(super)fn new(children:Vec<Rejection>)->Self{
  let batches=children.chunks(BATCH).map(|chunk|{
   let first=chunk.first()?;
   if !chunk.iter().all(|c|c.projection==first.projection&&c.origin==first.origin){return None;}
   let mut lo=Vec3::INFINITY;let mut hi=Vec3::NEG_INFINITY;
   for c in chunk{lo=lo.min(c.centre-c.half);hi=hi.max(c.centre+c.half);}
   Some((first.projection,first.origin,lo,hi))
  }).collect();Self{children,batches}
 }
 pub(super)fn candidates(&self,body:(Vec3,Vec3),rotation:Mat3,half:Vec3)->Vec<bool>{
  self.batches.iter().map(|batch|{
   let Some((p,origin,lo,hi))=*batch else{return true;};
   let centre=p*(body.0-origin);let reach=abs(p*rotation)*half+Vec3::splat(delirium_data::kv!(571));
   if !centre.is_finite()||!reach.is_finite()||!lo.is_finite()||!hi.is_finite(){return true;}
   let epsilon=centre.abs().max(lo.abs()).max(hi.abs()).max(reach.abs()).max(Vec3::ONE)*(64.*f32::EPSILON);
   centre.cmpge(lo-reach-epsilon).all()&&centre.cmple(hi+reach+epsilon).all()
  }).collect()
 }
}
#[cfg(test)]mod batch_tests{
 use super::*;
 #[test]fn batch_rejection_contains_every_original_child_decision(){
  super::super::moving_obstacle_regression::constants();
  let mut seed=15731u32;let mut draw=||{seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);(seed>>8)as f32/(1u32<<24)as f32};
  let mut skipped=0;
  for case in 0..2000{
   let rot=Mat3::from_quat(Quat::from_rotation_z(draw()*6.));let at=Vec3::new(draw()*1000.,draw()*1000.,draw()*10.);
   let children=(0..97).map(|k|Rejection::new((at,rot),(at+Vec3::X*draw(),rot),Vec3::new(k as f32*0.5,draw(),draw()),Vec3::splat(draw()*0.2))).collect();
   let prep=Prepared::new(children);
   let br=Mat3::from_quat(Quat::from_rotation_y(draw()*6.));let half=Vec3::splat(draw());
   let body=(at+rot*Vec3::new(draw()*70.-10.,draw()*4.,draw()*4.),Vec3::ONE);
   let batches=prep.candidates(body,br,half);
   for (i,c) in prep.children.iter().enumerate(){
    let exact=c.meets(body,br,half);assert!(!exact||batches[i/BATCH],"false rejection case={case} child={i}");
    skipped+=usize::from(!batches[i/BATCH]);
   }
  }
  assert!(skipped>10000,"must exercise actual rejections: {skipped}");
 }
}
