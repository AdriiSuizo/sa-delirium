//! An immutable fitted collider for an animated pedestrian. It is not an
//! agent, owns no behaviour and cannot send impulses to the simulation.
use super::*;
pub struct NmPoseCollider {build:Build,doll:Ragdoll,pose:Vec<Transform>,motion:Vec<(Vec3,Vec3)>}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_pose_collider_create(build:*const NmBuild)->*mut NmPoseCollider{
    let Some(b)=(unsafe{build.as_ref()})else{fail(NM_E_ARG,"null pose collider build");return std::ptr::null_mut()};
    let doll=Ragdoll::new(&b.build,&b.frames,Vec3::ZERO);
    Box::into_raw(Box::new(NmPoseCollider{build:b.build.clone(),doll,pose:b.frames.clone(),motion:vec![(Vec3::ZERO,Vec3::ZERO);b.frames.len()]}))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_pose_collider_destroy(body:*mut NmPoseCollider){if !body.is_null(){unsafe{drop(Box::from_raw(body));}}}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_pose_collider_set_pose(body:*mut NmPoseCollider,states:*const f32,parts:i32)->i32{
    let Some(b)=(unsafe{body.as_mut()})else{return fail(NM_E_ARG,"null pose collider")};
    if states.is_null()||parts<=0||parts as usize!=b.build.parts.len(){return fail(NM_E_ARG,"pose collider part count")}
    let Some((pose,motion))=recovery_contacts::read_states(unsafe{std::slice::from_raw_parts(states,parts as usize*13)},parts as usize)
        else{return fail(NM_E_ARG,"invalid animated collider pose")};
    b.pose=pose;b.motion=motion;0
}
/// Same13-float contact rows as nm_agent_pair_contacts. Current geometry only:
/// the native mover already ran; this query solves its remaining overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_pose_collider_contacts(body:*const NmPoseCollider,agent:*const NmAgent,states:*const f32,parts:i32,out:*mut f32,capacity:i32)->i32{
    let (Some(a),Some(b))=(unsafe{body.as_ref()},unsafe{agent.as_ref()})else{return fail(NM_E_ARG,"null animated contact body")};
    if states.is_null()||parts<=0||parts as usize!=b.build.parts.len()||out.is_null()||capacity<=0{return fail(NM_E_ARG,"invalid animated contact arrays")}
    let Some((bp,bv))=recovery_contacts::read_states(unsafe{std::slice::from_raw_parts(states,parts as usize*13)},parts as usize)
        else{return fail(NM_E_ARG,"invalid peer contact state")};
    let rows=a.doll.query_pose_contacts(&a.build,&a.pose,&a.pose,&b.agent.doll,&b.build,&bp,&bp);
    let mut n=0;
    for c in rows.into_iter().take(capacity as usize){
        let va=a.motion[c.a].0+a.motion[c.a].1.cross(c.point_a-a.pose[c.a].translation);
        let vb=bv[c.b].0+bv[c.b].1.cross(c.point_b-bp[c.b].translation);
        let row=[c.a as f32,c.b as f32,c.point_a.x,c.point_a.y,c.point_a.z,c.point_b.x,c.point_b.y,c.point_b.z,
                 c.normal.x,c.normal.y,c.normal.z,c.gap,-(va-vb).dot(c.normal)];
        unsafe{std::ptr::copy_nonoverlapping(row.as_ptr(),out.add(n*13),13)};n+=1;
    }n as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_pose_collider_sweep(body:*const NmPoseCollider,world:*const NmWorld,translation:*const f32,out:*mut f32)->i32{
    let (Some(a),Some(w))=(unsafe{body.as_ref()},unsafe{world.as_ref()})else{return fail(NM_E_ARG,"null collider sweep body/world")};
    if translation.is_null()||out.is_null(){return fail(NM_E_ARG,"null collider sweep vector")}
    let wish=Vec3::from_slice(unsafe{std::slice::from_raw_parts(translation,3)});
    if !wish.is_finite(){return fail(NM_E_ARG,"nonfinite collider displacement")}
    let (by,hits)=a.doll.sweep_pose_translation(&a.build,&a.pose,&w.0,&w.1,wish);
    unsafe{std::ptr::copy_nonoverlapping(by.to_array().as_ptr(),out,3)};hits as i32
}

#[cfg(test)]mod tests{
 use super::*;
 fn fitted(boxed:Option<Vec3>)->(Build,Vec<Transform>){
  let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
  let asset=std::ffi::CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();unsafe{assert!(matches!(nm_init(asset.as_ptr()),0|1));}
  let (source,_)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
  let mut p=source.parts[16].clone();p.parent=None;p.bone=0;p.radius=0.2;p.half=0.;p.boxed=boxed;p.anchor=Vec3::ZERO;p.offset=Transform::IDENTITY;
  (Build{parts:vec![p],nm:source.nm},vec![Transform::IDENTITY])
 }
 fn wall(world:&mut Collision,axis:usize,at:f32){
  let mut p=[[-10.,-10.,-10.],[10.,-10.,-10.],[10.,10.,10.],[-10.,10.,10.]];
  if axis==0{p=[[at,-10.,-10.],[at,10.,-10.],[at,10.,10.],[at,-10.,10.]];}else if axis==1{p=[[-10.,at,-10.],[10.,at,-10.],[10.,at,10.],[-10.,at,10.]];}
  world.add(&p,&[[0,1,2],[0,2,3]],&[0,0]);
 }
 #[test]fn whole_body_sweep_stops_at_wall_and_keeps_tangent_motion(){
  let(b,p)=fitted(None);let a=Ragdoll::new(&b,&p,Vec3::ZERO);let mut world=Collision::default();wall(&mut world,0,1.);
  let(by,n)=a.sweep_pose_translation(&b,&p,&world,&[],Vec3::new(2.,1.,0.));
  assert!(n>0&&by.x>0.7&&by.x<0.81,"{by:?} {n}");assert!((by.y-1.).abs()<0.002,"wall lost tangent {by:?}");
  assert_eq!(a.frames().next().unwrap().translation,p[0].translation);
 }
 #[test]fn corner_stays_out_of_both_planes_and_repeated_corrections_are_safe(){
  let(b,mut p)=fitted(None);let a=Ragdoll::new(&b,&p,Vec3::ZERO);let mut world=Collision::default();wall(&mut world,0,1.);wall(&mut world,1,1.);
  let(by,n)=a.sweep_pose_translation(&b,&p,&world,&[],Vec3::new(2.,2.,0.));
  assert!(n>=2&&by.x>0.7&&by.y>0.7&&by.x<0.81&&by.y<0.81,"{by:?} {n}");
  p[0].translation+=by;
  for _ in 0..120{let(step,_)=a.sweep_pose_translation(&b,&p,&world,&[],Vec3::new(0.2,0.2,0.));p[0].translation+=step;}
  assert!(p[0].translation.x<0.81&&p[0].translation.y<0.81,"multiple stale corrections crossed corner {:?}",p[0]);
 }
 #[test]fn a_shoulder_blocks_when_root_ray_has_clearance_and_uses_rotated_box(){
  let(b,mut p)=fitted(Some(Vec3::new(0.5,0.1,0.1)));p[0].translation=Vec3::new(0.6,0.,0.);
  let a=Ragdoll::new(&b,&p,Vec3::ZERO);let mut world=Collision::default();wall(&mut world,0,1.3);
  let(by,_)=a.sweep_pose_translation(&b,&p,&world,&[],Vec3::new(0.4,0.,0.));
  assert!(by.x<0.25,"whole box crossed although root alone remained clear {by:?}");
  p[0].rotation=Quat::from_rotation_z(std::f32::consts::FRAC_PI_2);
  let(turned,_)=a.sweep_pose_translation(&b,&p,&world,&[],Vec3::new(0.4,0.,0.));
  assert!(turned.x>0.39,"rotated component axes were lost {turned:?}");
 }
 #[test]fn resting_floor_does_not_block_and_vehicle_component_does(){
  let(b,mut p)=fitted(None);p[0].translation.z=0.2;let a=Ragdoll::new(&b,&p,Vec3::ZERO);let mut world=Collision::default();world.add_floor(0.,0.,0.,10.,0);
  let(walk,_)=a.sweep_pose_translation(&b,&p,&world,&[],Vec3::X);assert!(walk.x>0.99,"floor trapped walker {walk:?}");
  let components=Box::leak(vec![delirium::ragdoll::ObPart{shape:delirium::gjk::Shape::Box(Vec3::new(0.1,2.,2.),0.),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::new(-0.1,-2.,-2.),hi:Vec3::new(0.1,2.,2.),mesh:None,filter:Default::default()}].into_boxed_slice());
  let ob=Obstacle{at:Vec3::X,rot:Quat::IDENTITY,parts:ObstacleParts::Static(components),..Default::default()};
  let(by,n)=a.sweep_pose_translation(&b,&p,&world,&[ob],Vec3::X*2.);assert!(n>0&&by.x>0.6&&by.x<0.71,"vehicle {by:?} {n}");
 }
 #[test]fn separating_existing_overlap_is_not_frozen(){
  let(b,mut p)=fitted(None);p[0].translation.x=0.9;let a=Ragdoll::new(&b,&p,Vec3::ZERO);let mut world=Collision::default();wall(&mut world,0,1.);
  let(by,_)=a.sweep_pose_translation(&b,&p,&world,&[],-Vec3::X);assert!(by.x< -0.99,"separating overlap frozen {by:?}");
 }
 #[test]fn collider_abi_refreshes_geometry_between_consecutive_peer_corrections(){
  let(build,frames)=fitted(None);let source=NmBuild{build,frames};
  let body=unsafe{nm_pose_collider_create(&source)};assert!(!body.is_null());
  let mut w=NmWorld(Collision::default(),vec![],vec![]);wall(&mut w.0,0,1.);
  let mut state=[0f32;13];state[6]=1.;let wish=[0.4,0.,0.];let mut by=[0f32;3];
  for _ in 0..4{
   assert_eq!(unsafe{nm_pose_collider_set_pose(body,state.as_ptr(),1)},0);
   assert!(unsafe{nm_pose_collider_sweep(body,&w,wish.as_ptr(),by.as_mut_ptr())}>=0);
   state[0]+=by[0];state[1]+=by[1];state[2]+=by[2];
  }
  assert!(state[0]>0.7&&state[0]<0.81&&state[2]==0.,"stale collider crossed wall {state:?}");
  assert_eq!(unsafe{nm_pose_collider_set_pose(body,state.as_ptr(),0)},NM_E_ARG);
  unsafe{nm_pose_collider_destroy(body)};
 }
}

#[cfg(test)]
#[path="animated_sweep_regressions.rs"]
mod animated_sweep_regressions;
