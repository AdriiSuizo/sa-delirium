//! Explicit source support ownership. Merely linking these exports never
//! changes native support: the host must release its own support atomically.
use super::*;
use delirium::external_mobility::{ExternalGroundBody,ExternalGroundClaim};

/// Stage original normal-ped support and gravity for this exact interval.
/// body_flags: bit0 boat, bit1 original excluded instance type15, bit2 fixed.
/// body_mass is actual positive finite archetype mass, never inferred mobility.
/// Metadata
/// must describe every current external owner whenever any claim is present.
/// claim_flags: bit0 initial grounded, bit1 opening damping, bit2 readd.
/// authored_xy contains two WORLD m/s floats per claim. No inferred flags,
/// duplicate IDs or missing body metadata are accepted. Rejection poisons
/// the interval; the host must not execute native and original support twice.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_set_external_ground(g:*mut NmGroup,epoch:u64,body_ids:*const u64,body_flags:*const u32,body_mass:*const f32,body_count:i32,claim_ids:*const u64,claim_flags:*const u32,authored_xy:*const f32,claim_count:i32)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null ground group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    g.external_invalid=true;
    if !(0..=256).contains(&body_count)||!(0..=256).contains(&claim_count)||
       (body_count>0&&(body_ids.is_null()||body_flags.is_null()||body_mass.is_null()))||
       (claim_count>0&&(claim_ids.is_null()||claim_flags.is_null()||authored_xy.is_null())) {
        return fail(NM_E_ARG,"invalid ground batch buffers");
    }
    let mut seen=std::collections::BTreeSet::new();
    let mut bodies=Vec::with_capacity(body_count as usize);
    for i in 0..body_count as usize {
        let(id,flags,mass)=unsafe{(*body_ids.add(i),*body_flags.add(i),*body_mass.add(i))};
        if !seen.insert(id)||!g.external_ids.contains(&id)||flags&!7!=0||!mass.is_finite()||mass<=0.{return fail(NM_E_ARG,"invalid ground body metadata")}
        bodies.push(ExternalGroundBody{id,boat:flags&1!=0,excluded_type15:flags&2!=0,fixed:flags&4!=0,mass});
    }
    if claim_count>0&&seen!=g.external_ids{return fail(NM_E_ARG,"ground metadata owner set differs")}
    seen.clear();let mut claims=Vec::with_capacity(claim_count as usize);
    for i in 0..claim_count as usize {
        let(id,flags,x,y)=unsafe{(*claim_ids.add(i),*claim_flags.add(i),*authored_xy.add(i*2),*authored_xy.add(i*2+1))};
        if !seen.insert(id)||!g.external_ids.contains(&id)||flags&!7!=0||!x.is_finite()||!y.is_finite(){return fail(NM_E_ARG,"invalid ground ownership claim")}
        claims.push(ExternalGroundClaim{id,authored_xy:Vec2::new(x,y),initial_grounded:flags&1!=0,opening_damping:flags&2!=0,readd:flags&4!=0});
    }
    let result=g.solver.stage_external_ground_bodies(epoch,&bodies).and_then(|_|g.solver.stage_external_ground(epoch,&claims));
    match result{Ok(())=>{g.external_invalid=false;0},Err(e)=>fail(NM_E_ARG,format!("ground staging: {e:?}"))}
}

/// Read original support observations without applying their impulses again.
/// keys contains owner,epoch,support-owner,support-member per row (u64,
/// no float ID loss). Missing support-member is u64::MAX.
/// rows19: grounded,before,support-part, point3,normal3,
/// support-velocity3,support-impulse3,gravity-impulse3,slope-velocity.
/// Missing part is -1. These forces already belong to owner results.
/// Null buffers with capacity0 return count; undersized outputs are untouched.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_external_ground_results(g:*const NmGroup,keys:*mut u64,rows:*mut f32,capacity:i32)->i32 {
    let Some(g)=(unsafe{g.as_ref()})else{return fail(NM_E_ARG,"null ground group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    let results=g.solver.external_ground_results();
    if capacity==0&&keys.is_null()&&rows.is_null(){return results.len() as i32}
    if capacity<0||(capacity as usize)<results.len()||(!results.is_empty()&&(keys.is_null()||rows.is_null())){return fail(NM_E_ARG,"ground output buffer too small or null")}
    for (i,r) in results.iter().enumerate(){unsafe{
        let k=keys.add(i*4);*k=r.id;*k.add(1)=r.epoch;*k.add(2)=r.support_owner;
        *k.add(3)=if r.support_member==usize::MAX{u64::MAX}else{r.support_member as u64};
        let p=rows.add(i*19);*p=r.grounded as u8 as f32;*p.add(1)=r.before as u8 as f32;
        *p.add(2)=if r.support_part==usize::MAX{-1.}else{r.support_part as f32};
        for (offset,v) in [(3,r.point),(6,r.normal),(9,r.velocity),(12,r.support_impulse),(15,r.gravity_impulse)]{put3(p.add(offset),v);}
        *p.add(18)=r.slope_velocity;
    }}results.len() as i32
}

/// Forget persistent support only when the entity generation is destroyed,
/// not when an animated owner enters natural motion or temporarily unstages.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_forget_external_ground(g:*mut NmGroup,id:u64)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null ground group")};
    g.solver.forget_external_ground(id);0
}

#[cfg(test)]mod tests {
    use super::*;
    #[test]fn public_ground_transaction_preserves_floor_state_and_exact_identity(){unsafe{
        let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=std::ffi::CString::new(assets.join("fred.xrt").to_str().unwrap()).unwrap();
        assert!(matches!(nm_init(path.as_ptr()),0|1));
        let g=nm_group_create();let mut world=NmWorld(Collision::default(),vec![],vec![]);world.0.add_floor(0.,0.,0.,20.,0);
        let id=(2u64<<32)|0x7fff_ff01;let flags=0u32;let mass=25.;let cflags=3u32;let xy=[0.,0.];
        let mut root=Vec3::Z;let mut velocity=Vec3::ZERO;
        for frame in 1..=180u64{
            assert_eq!(nm_group_begin(g),0);let mut owner=[0f32;27];put3(owner.as_mut_ptr(),root);put3(owner.as_mut_ptr().add(3),velocity);
            for k in [9,13,17]{owner[k]=1./25.;}
            assert_eq!(external_mobility::nm_group_set_external(g,frame,&id,owner.as_ptr(),1),0);
            let predictor=[0.,0.,0.,0.,0.,0.,1.];
            assert_eq!(external_mobility::nm_group_set_external_predictors(g,frame,&id,predictor.as_ptr(),1),0);
            assert_eq!(nm_group_set_external_ground(g,frame,&id,&flags,&mass,1,&id,&cflags,xy.as_ptr(),1),0);
            world.1.clear();assert_eq!(original_animated_owner_api::nm_world_append_animated_owner(&mut world,id,&root.x,0.,&velocity.x),0);
            assert_eq!(nm_group_step(g,&world,1./60.),0);
            assert_eq!(nm_group_external_ground_results(g,std::ptr::null_mut(),std::ptr::null_mut(),0),1);
            let mut keys=[42u64;6];let mut support=[42f32;21];
            assert_eq!(nm_group_external_ground_results(g,keys.as_mut_ptr().add(1),support.as_mut_ptr().add(1),0),NM_E_ARG);
            assert_eq!(keys,[42u64;6]);assert_eq!(support,[42f32;21]);
            assert_eq!(nm_group_external_ground_results(g,keys.as_mut_ptr().add(1),support.as_mut_ptr().add(1),1),1);
            assert_eq!(keys,[42,id,frame,0,u64::MAX,42]);assert_eq!(support[0],42.);assert_eq!(support[20],42.);
            assert_eq!(support[1],1.,"floor support cannot flicker");
            let mut out_id=0;let mut result=[0f32;18];let mut placement=[0f32;12];
            assert_eq!(external_mobility::nm_group_external_results(g,&mut out_id,result.as_mut_ptr(),1),1);assert_eq!(out_id,id);
            assert_eq!(external_mobility::nm_group_external_placements(g,&mut out_id,placement.as_mut_ptr(),1),1);assert_eq!(placement[7],1.);
            let solved=v3(result.as_ptr().add(12));
            assert!((solved-v3(placement.as_ptr().add(9))).length()<1e-6,"host receipt must match final result, before world sweep");
            velocity+=v3(result.as_ptr().add(6));root+=v3(placement.as_ptr());
            assert!((root.z-1.).abs()<0.025,"support transport must not introduce root snap: {root:?}");
        }
        assert_eq!(nm_group_forget_external_ground(g,id),0);nm_group_destroy(g);
    }}
    #[test]fn ownership_claim_transport_rejects_partial_unknown_duplicate_and_stale_batches(){unsafe{
        let g=nm_group_create();let ids=[(2u64<<32)|129,(1u64<<32)|258];let mut owners=[0f32;54];
        for k in [9,13,17]{owners[k]=1./25.;owners[27+k]=1./1200.;}
        let stage=|epoch|external_mobility::nm_group_set_external(g,epoch,ids.as_ptr(),owners.as_ptr(),2);
        let flags=[0u32,0];let masses=[25f32,1200.];let claim_flags=3;let xy=[0f32,0.];
        assert_eq!(stage(1),0);
        assert_eq!(nm_group_set_external_ground(g,1,ids.as_ptr(),flags.as_ptr(),masses.as_ptr(),1,ids.as_ptr(),&claim_flags,xy.as_ptr(),1),NM_E_ARG);
        assert_eq!(nm_group_external_ground_results(g,std::ptr::null_mut(),std::ptr::null_mut(),0),NM_E_STATE);
        assert_eq!(stage(2),0);
        assert_eq!(nm_group_set_external_ground(g,2,ids.as_ptr(),flags.as_ptr(),masses.as_ptr(),2,ids.as_ptr(),&claim_flags,xy.as_ptr(),1),0);
        assert_eq!(nm_group_external_ground_results(g,std::ptr::null_mut(),std::ptr::null_mut(),0),0);
        assert_eq!(stage(3),0);
        assert_eq!(nm_group_set_external_ground(g,2,ids.as_ptr(),flags.as_ptr(),masses.as_ptr(),2,ids.as_ptr(),&claim_flags,xy.as_ptr(),1),NM_E_ARG);
        assert_eq!(stage(4),0);let duplicates=[ids[0],ids[0]];
        assert_eq!(nm_group_set_external_ground(g,4,duplicates.as_ptr(),flags.as_ptr(),masses.as_ptr(),2,ids.as_ptr(),&claim_flags,xy.as_ptr(),1),NM_E_ARG);
        assert_eq!(stage(5),0);let bad=[0u32,8];
        assert_eq!(nm_group_set_external_ground(g,5,ids.as_ptr(),bad.as_ptr(),masses.as_ptr(),2,ids.as_ptr(),&claim_flags,xy.as_ptr(),1),NM_E_ARG);
        assert_eq!(stage(6),0);let nan=[f32::NAN,0.];
        assert_eq!(nm_group_set_external_ground(g,6,ids.as_ptr(),flags.as_ptr(),masses.as_ptr(),2,ids.as_ptr(),&claim_flags,nan.as_ptr(),1),NM_E_ARG);
        assert_eq!(stage(7),0);
        assert_eq!(nm_group_set_external_ground(g,7,ids.as_ptr(),flags.as_ptr(),masses.as_ptr(),2,ids.as_ptr().add(1),&claim_flags,xy.as_ptr(),1),NM_E_ARG,"car is not an original25kg rotation-locked ped");
        nm_group_destroy(g);
    }}
}
