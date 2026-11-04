//! Native transport; ownership/mobility lives in the shared group registry.
use super::*;
use super::original_animated_owner as original;
/// Immutable ORIGINAL four-component locomotion owner against an NM pose.
/// Same rows13 as pair contacts, with NM part A and original owner child B.
/// The21-part rendered skin is intentionally not substituted for this owner.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_animated_owner_pair_contacts(root:*const f32,heading:f32,velocity:*const f32,agent:*const NmAgent,states:*const f32,parts:i32,out:*mut f32,capacity:i32)->i32{
    let Some(a)=(unsafe{agent.as_ref()})else{return fail(NM_E_ARG,"null original owner peer")};
    if root.is_null()||velocity.is_null()||states.is_null()||out.is_null()||capacity<=0||parts<=0||parts as usize!=a.build.parts.len()||!heading.is_finite(){return fail(NM_E_ARG,"invalid original owner contact arrays")}
    let(at,vel)=(unsafe{v3(root)},unsafe{v3(velocity)});
    if !at.is_finite()||!vel.is_finite(){return fail(NM_E_ARG,"nonfinite original owner contact")}
    let Some((pose,motion))=recovery_contacts::read_states(unsafe{std::slice::from_raw_parts(states,parts as usize*13)},parts as usize)
        else{return fail(NM_E_ARG,"invalid original owner peer pose")};
    let obstacle=Obstacle{filter:delirium::ragdoll::CollisionFilter{kind:0x20,include:0x79FE},at,rot:Quat::from_rotation_z(heading),centre:at,com:at,vel,parts:original::parts(),..Default::default()};
    let contacts=a.agent.doll.query_obstacle_pose_contacts(&a.build,&pose,&obstacle);
    if contacts.len()>capacity as usize{return fail(NM_E_ARG,"original owner contact output too small")}
    for (n,c) in contacts.iter().enumerate(){
        let va=motion[c.a].0+motion[c.a].1.cross(c.point_a-pose[c.a].translation);
        let row=[c.a as f32,c.b as f32,c.point_a.x,c.point_a.y,c.point_a.z,c.point_b.x,c.point_b.y,c.point_b.z,c.normal.x,c.normal.y,c.normal.z,c.gap,-(va-vel).dot(c.normal)];
        unsafe{std::ptr::copy_nonoverlapping(row.as_ptr(),out.add(n*13),13)};
    }
    contacts.len() as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_world_append_animated_owner(w:*mut NmWorld,owner:u64,root:*const f32,heading:f32,velocity:*const f32)->i32{
    let Some(w)=(unsafe{w.as_mut()})else{return fail(NM_E_ARG,"null animated owner world")};
    if owner==0||root.is_null()||velocity.is_null()||!heading.is_finite(){return fail(NM_E_ARG,"invalid animated owner")}
    let(at,vel)=(unsafe{v3(root)},unsafe{v3(velocity)});
    if !at.is_finite()||!vel.is_finite(){return fail(NM_E_ARG,"nonfinite animated owner")}
    if w.1.iter().any(|o|o.owner==Some(owner)){return fail(NM_E_ARG,"duplicate animated owner bound")}
    let index=w.1.len();
    w.1.push(Obstacle{id:index as i32,owner:Some(owner),filter:delirium::ragdoll::CollisionFilter{kind:0x20,include:0x79FE},at,rot:Quat::from_rotation_z(heading),centre:at,com:at,
        half:Vec3::new(0.25,0.25,1.3),vel,spin:Vec3::ZERO,parts:original::parts(),last:None});
    index as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_world_recovery_room(w:*const NmWorld,root:*const f32,exclude_owner:u64)->i32{
    let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null recovery room world")};
    if root.is_null(){return fail(NM_E_ARG,"null recovery room root")}
    original::room_clear(&w.0,&w.1,unsafe{v3(root)},exclude_owner) as i32
}
/// 8261E688: while BlendFromNM waits for room, all preceding responses have
/// ended and NM is relaxed, still simulated. No pose reset or velocity change.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_recovery_wait(a:*mut NmAgent,w:*const NmWorld)->i32{
    if a.is_null()||w.is_null(){return fail(NM_E_ARG,"null recovery wait agent/world")}
    unsafe{nm_agent_road_cancel(a,w);nm_agent_vehicle_exit_cancel(a);nm_agent_vehicle_entry_cancel(a);ped_contacts::nm_agent_ped_bump_cancel(a);nm_agent_cancel_animation_pose(a);}
    let a=unsafe{&mut *a};a.agent.stop_all_behaviours();a.agent.body_relax_full(60.,None,None,false);0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_recovery_room_obstructed(a:*const NmAgent,root:*const f32)->i32{
    let Some(a)=(unsafe{a.as_ref()})else{return fail(NM_E_ARG,"null recovery peer")};
    if root.is_null(){return fail(NM_E_ARG,"null recovery root")}
    let p=unsafe{v3(root)};if !p.is_finite(){return fail(NM_E_ARG,"invalid recovery root")}
    a.agent.doll.intersects_shape(&a.build,&delirium::gjk::Shape::Capsule(0.485,0.25),
        delirium::prelude::Mat3::from_rotation_x(std::f32::consts::FRAC_PI_2),p+Vec3::Z*0.085) as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_world_sweep_animated_owner(w:*const NmWorld,owner:u64,root:*const f32,heading:f32,wish:*const f32,out:*mut f32)->i32{
    let Some(w)=(unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null animated sweep world")};
    if root.is_null()||wish.is_null()||out.is_null()||!heading.is_finite(){return fail(NM_E_ARG,"invalid animated sweep")}
    let p=unsafe{v3(root)};let by=unsafe{v3(wish)};if !p.is_finite()||!by.is_finite(){return fail(NM_E_ARG,"nonfinite animated sweep")}
    let(moved,n)=original::sweep(original::AnimatedOwner{root:p,heading,velocity:Vec3::ZERO},&w.0,&w.1,by,owner);
    unsafe{std::ptr::copy_nonoverlapping(moved.to_array().as_ptr(),out,3)};n as i32
}
