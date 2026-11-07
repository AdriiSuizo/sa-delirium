//! Independent player movement lifetime; combat targets may disappear freely.
use super::*;
pub struct NmLocomotion(sa_combat::locomotion::Locomotion);
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_create(c:*const NmCombat,ids:*const u32,parents:*const u32,bind:*const f32,count:i32,heading:f32)->*mut NmLocomotion{
    let result=(||->Result<_,String>{
        if c.is_null()||ids.is_null()||parents.is_null()||bind.is_null()||!(20..=128).contains(&count)||!heading.is_finite(){return Err("invalid movement rig".into())}
        let rig=sa_combat::animation::Rig::new(unsafe{std::slice::from_raw_parts(ids,count as usize)},unsafe{std::slice::from_raw_parts(parents,count as usize)},unsafe{std::slice::from_raw_parts(bind,count as usize*7)})?;
        Ok(sa_combat::locomotion::Locomotion::new(rig,heading))
    })();
    match result{Ok(p)=>Box::into_raw(Box::new(NmLocomotion(p))),Err(e)=>{fail(NM_E_ARG,e);std::ptr::null_mut()}}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_destroy(p:*mut NmLocomotion){if !p.is_null(){drop(unsafe{Box::from_raw(p)})}}
/// Original run pressure and stamina from the player's own pad, once per frame.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_control(p:*mut NmLocomotion,push:f32,tapped:i32,held:i32,now:u32,dt:f32)->f32{
    let Some(p)=(unsafe{p.as_mut()})else{return -1.};
    match p.0.control(push,tapped!=0,held!=0,now,dt){Ok(wanted)=>wanted,Err(e)=>{fail(NM_E_ARG,e);-1.}}
}
/// Input wanted gait 0..3, target/current yaw, crouch and terrain slope.
/// Negative dt cancels ownership, without leaving an old mover for re-entry.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_step(c:*const NmCombat,p:*mut NmLocomotion,wanted:f32,goal:f32,heading:f32,crouch:i32,slope:i32,now:u32,dt:f32)->i32{
    let Some(p)=(unsafe{p.as_mut()})else{return fail(NM_E_ARG,"null movement")};
    if dt<0.{p.0.cancel();return 0}
    let Some(rt)=(unsafe{c.as_ref()}).and_then(|c|c.1.as_ref())else{return fail(NM_E_ARG,"movement bank unavailable")};
    match p.0.step(&rt.bank,wanted,goal,heading,crouch!=0,slope.clamp(-1,1) as i16,now,dt){Ok(())=>1,Err(e)=>fail(NM_E_ARG,e)}
}
/// Canonical rig palette plus heading, local velocity xy, ratio, phase, state,
/// elapsed seconds and movement set. Inactive returns zero.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_pose(p:*const NmLocomotion,pose:*mut f32,count:i32,state:*mut f32)->i32{
    let Some(p)=(unsafe{p.as_ref()})else{return fail(NM_E_ARG,"null movement")};
    if !p.0.active{return 0}
    if pose.is_null()||state.is_null()||count as usize!=p.0.pose.len(){return fail(NM_E_ARG,"invalid movement output")}
    for(t,r)in p.0.pose.iter().zip(unsafe{std::slice::from_raw_parts_mut(pose,count as usize*7)}.chunks_exact_mut(7)){t.write(r)}
    unsafe{std::ptr::copy_nonoverlapping(p.0.state().as_ptr(),state,8)};1
}
/// One bank clip on this rig: pose rows, then duration and mover speed (m/s) in info.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_clip_pose(c:*const NmCombat,p:*const NmLocomotion,key:*const c_char,phase:f32,pose:*mut f32,count:i32,info:*mut f32)->i32{
    let Some(p)=(unsafe{p.as_ref()})else{return fail(NM_E_ARG,"null movement")};
    let Some(rt)=(unsafe{c.as_ref()}).and_then(|c|c.1.as_ref())else{return fail(NM_E_ARG,"movement bank unavailable")};
    if key.is_null()||pose.is_null()||info.is_null()||count as usize!=p.0.rig.ids.len(){return fail(NM_E_ARG,"invalid clip pose request")}
    let Ok(key)=(unsafe{std::ffi::CStr::from_ptr(key)}).to_str()else{return fail(NM_E_ARG,"invalid clip key")};
    let Some(clip)=rt.bank.clips.get(key)else{return 0};
    let Some(rows)=rt.bank.retarget(&p.0.rig,key,phase.rem_euclid(1.),0)else{return 0};
    for(t,r)in rows.iter().zip(unsafe{std::slice::from_raw_parts_mut(pose,count as usize*7)}.chunks_exact_mut(7)){t.write(r)}
    let moved=(sa_combat::animation::mover(clip,1.)-sa_combat::animation::mover(clip,0.)).length();
    unsafe{*info=clip.duration;*info.add(1)=moved/clip.duration;}1
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_locomotion_clip(p:*const NmLocomotion,out:*mut c_char,len:i32)->i32{
    let Some(p)=(unsafe{p.as_ref()})else{return fail(NM_E_ARG,"null movement")};unsafe{put(&p.0.key,out,len)}
}
