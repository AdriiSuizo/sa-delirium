//! Transport for finite host bodies participating in the articulated island.
//! One owner can have many compound shapes, but its mobility is staged once.
use super::*;
use delirium::external_mobility::{ExternalDrive,ExternalOwner,ExternalPredictor};

/// Stage generation-qualified host owners for this solver substep. rows27:
/// COM3, velocity3, spin3, linear inverse mobility9, world inverse inertia9.
/// Matrices are column-major. Zero mobility means a constrained direction.
/// Duplicate/conflicting owners, invalid epochs and nonfinite data are errors.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_set_external(g:*mut NmGroup, epoch:u64, ids:*const u64, rows:*const f32, count:i32)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};
    g.external_ids.clear();g.external_invalid=true;
    let _=g.solver.stage_external(0,&[]);
    if count<0||count>256||(count>0&&(ids.is_null()||rows.is_null())){return fail(NM_E_ARG,"invalid external owner rows")}
    let mut owners=Vec::with_capacity(count as usize);
    for n in 0..count as usize {
        let id=unsafe{*ids.add(n)};if id==0{return fail(NM_E_ARG,"owner zero is reserved for kinematic geometry")}
        let p=unsafe{rows.add(n*27)};
        let matrix=|offset|unsafe{Mat3::from_cols(v3(p.add(offset)),v3(p.add(offset+3)),v3(p.add(offset+6)))};
        owners.push(ExternalOwner{id,epoch,com:unsafe{v3(p)},velocity:unsafe{v3(p.add(3))},spin:unsafe{v3(p.add(6))},
            linear_inverse:matrix(9),angular_inverse:matrix(18)});
    }
    match g.solver.stage_external(epoch,&owners){Ok(())=>{g.external_ids.extend(owners.iter().map(|o|o.id));g.external_invalid=false;0},Err(e)=>fail(NM_E_ARG,format!("external owner staging: {e:?}"))}
}

/// Stage the carried contact-motion predictor for exactly the registered
/// owner set and epoch. rows7: pending COM displacement3, WORLD rotation xyzw4.
/// The owner snapshot is already at this predicted endpoint. Undoing these
/// transforms gives the accepted base from which the whole correction is swept.
/// A rejected stage poisons this interval; a previous predictor cannot be reused.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_set_external_predictors(g:*mut NmGroup,epoch:u64,ids:*const u64,rows:*const f32,count:i32)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    g.external_invalid=true;
    if count<0||count>256||(count>0&&(ids.is_null()||rows.is_null())){return fail(NM_E_ARG,"invalid external predictor rows")}
    let mut seen=std::collections::BTreeSet::new();
    let mut predictors=Vec::with_capacity(count as usize);
    for n in 0..count as usize {
        let id=unsafe{*ids.add(n)};let p=unsafe{rows.add(n*7)};
        let position=unsafe{v3(p)};
        let rotation=unsafe{Quat::from_xyzw(*p.add(3),*p.add(4),*p.add(5),*p.add(6))};
        if !seen.insert(id)||!g.external_ids.contains(&id)||!position.is_finite()||!rotation.is_finite()||
           (rotation.length_squared()-1.).abs()>0.001{return fail(NM_E_ARG,"invalid or duplicate external predictor")}
        predictors.push(ExternalPredictor{id,pending_position:position,pending_rotation:rotation});
    }
    if seen!=g.external_ids{return fail(NM_E_ARG,"external predictor owner set differs")}
    match g.solver.stage_external_predictors(epoch,&predictors){
        Ok(())=>{g.external_invalid=false;0},
        Err(e)=>fail(NM_E_ARG,format!("external predictor staging: {e:?}"))
    }
}

/// Stage pre-contact drive impulses for this exact owner interval. rows6:
/// linear impulse3 (Ns), angular impulse3 (Nms), in WORLD axes. This replaces
/// the staged drive set before step; it never applies or accumulates a force
/// while staging. IDs must be unique members of the current owner set.
/// The common solve includes these impulses in results18 and placements;
/// contact receipts contain contact impulses only. Do not commit drives again.
/// Wrong epochs, invalid input or staging after consumption reject the interval.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_set_external_drives(g:*mut NmGroup,epoch:u64,ids:*const u64,rows:*const f32,count:i32)->i32 {
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    g.external_invalid=true;
    if count<0||count>256||(count>0&&(ids.is_null()||rows.is_null())){return fail(NM_E_ARG,"invalid external drive rows")}
    let mut seen=std::collections::BTreeSet::new();
    let mut drives=Vec::with_capacity(count as usize);
    for n in 0..count as usize {
        let id=unsafe{*ids.add(n)};let p=unsafe{rows.add(n*6)};
        let impulse=unsafe{v3(p)};let torque=unsafe{v3(p.add(3))};
        if !seen.insert(id)||!g.external_ids.contains(&id)||!impulse.is_finite()||!torque.is_finite(){
            return fail(NM_E_ARG,"invalid, missing or duplicate external drive owner")
        }
        drives.push(ExternalDrive{id,impulse,torque});
    }
    match g.solver.stage_external_drives(epoch,&drives){
        Ok(())=>{g.external_invalid=false;0},
        Err(e)=>fail(NM_E_ARG,format!("external drive staging: {e:?}"))
    }
}

/// Read world-validated TOTAL corrections from accepted base. rows12:
/// admitted translation3, WORLD rotation xyzw4, valid, world contact count,
/// requested translation3: vehicle pre-world preview (diagnostic only), or
/// animated owner's FINAL coupled translation before its admission sweep.
/// Vehicle admitted translation includes those rows and matches the final
/// predictor + split. The newly solved delta velocity belongs to the NEXT
/// integration (TU8 collider82972440/82972650 before solve,829726A0 after).
/// Ped admission may additionally slide
/// that translation against the world while native ground support remains.
/// Never integrate newly solved velocity into this already elapsed interval.
/// Split-only results18 remain a separate, unchanged diagnostic contract.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_external_placements(g:*const NmGroup,ids:*mut u64,rows:*mut f32,capacity:i32)->i32 {
    let Some(g)=(unsafe{g.as_ref()})else{return fail(NM_E_ARG,"null group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    let placements=g.solver.external_placements();
    if capacity==0&&ids.is_null()&&rows.is_null(){return placements.len() as i32}
    if capacity<0||(capacity as usize)<placements.len()||(!placements.is_empty()&&(ids.is_null()||rows.is_null())){
        return fail(NM_E_ARG,"external placement buffer too small or null")
    }
    for(n,r)in placements.iter().enumerate(){unsafe{
        *ids.add(n)=r.id;let p=rows.add(n*12);
        put3(p,r.admitted_position);
        std::ptr::copy_nonoverlapping(r.admitted_rotation.to_array().as_ptr(),p.add(3),4);
        *p.add(7)=r.valid as u8 as f32;*p.add(8)=r.world_contacts as f32;
        put3(p.add(9),r.requested_position);
    }}placements.len() as i32
}

/// Bind an obstacle component to one finite owner staged in the same group.
/// Zero unbinds it. This does not create a second mass for another component.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_world_set_obstacle_owner(w:*mut NmWorld,index:i32,owner:u64)->i32 {
    let Some(w)=(unsafe{w.as_mut()})else{return fail(NM_E_ARG,"null world")};
    let Some(ob)=usize::try_from(index).ok().and_then(|i|w.1.get_mut(i))else{return fail(NM_E_ARG,"invalid obstacle index")};
    ob.owner=(owner!=0).then_some(owner);0
}

/// Read the stable owner of a contact obstacle; zero means kinematic or absent.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_world_obstacle_owner(w:*const NmWorld,index:i32)->u64 {
    let Some(w)=(unsafe{w.as_ref()})else{return 0};
    usize::try_from(index).ok().and_then(|i|w.1.get(i)).and_then(|ob|ob.owner).unwrap_or(0)
}

/// Read aggregated finite-owner results, once per owner, without consuming.
/// rows18: impulse3, angular impulse3, delta velocity3, delta spin3,
/// split position correction3, split orientation correction3.
/// A short buffer is an error, never a silently truncated force transfer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_external_results(g:*const NmGroup,ids:*mut u64,rows:*mut f32,capacity:i32)->i32 {
    let Some(g)=(unsafe{g.as_ref()})else{return fail(NM_E_ARG,"null group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    let results=g.solver.external_results();
    if capacity<0||(capacity as usize)<results.len()||(!results.is_empty()&&(ids.is_null()||rows.is_null())){
        return fail(NM_E_ARG,"external result buffer too small or null")
    }
    for(n,r)in results.iter().enumerate(){unsafe{
        *ids.add(n)=r.id;
        for(k,v)in [r.impulse,r.angular_impulse,r.delta_velocity,r.delta_spin,r.position_correction,r.orientation_correction].iter().enumerate(){
            put3(rows.add(n*18+k*3),*v);
        }
    }}
    results.len() as i32
}

/// Actual solved external pairs. ids contain A,B per row. rows17 contain
/// impulse-on-A3, point3, normal3, pre-solve point velocities A3,B3, children2.
/// With null outputs and capacity0, returns the required row count. No fake
/// contact is emitted for a zero impulse, and reading never reapplies a force.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_external_contacts(g:*const NmGroup,ids:*mut u64,rows:*mut f32,capacity:i32)->i32 {
    let Some(g)=(unsafe{g.as_ref()})else{return fail(NM_E_ARG,"null group")};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    let contacts=g.solver.external_contacts();
    if capacity==0&&ids.is_null()&&rows.is_null(){return contacts.len() as i32}
    if capacity<0||(capacity as usize)<contacts.len()||(!contacts.is_empty()&&(ids.is_null()||rows.is_null())){
        return fail(NM_E_ARG,"external contact buffer too small or null")
    }
    for(n,c)in contacts.iter().enumerate(){unsafe{
        *ids.add(n*2)=c.a;*ids.add(n*2+1)=c.b;
        for(k,v)in [c.impulse,c.point,c.normal,c.velocity_a,c.velocity_b].iter().enumerate(){put3(rows.add(n*17+k*3),*v);}
        *rows.add(n*17+15)=c.child_a as f32;*rows.add(n*17+16)=c.child_b as f32;
    }}contacts.len() as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    fn row()->[f32;27]{let mut row=[0.;27];row[9]=0.04;row[13]=0.04;row[17]=0.04;row}
    #[test]
    fn drive_transport_rejects_stale_missing_duplicate_and_nonfinite_without_reusing_old_drive(){unsafe{
        let g=nm_group_create();let id=(2u64<<32)|257;let owner=row();let drive=[12.5,0.,0.,0.,0.,0.];
        assert_eq!(nm_group_set_external(g,1,&id,owner.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_drives(g,1,&id,drive.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_drives(g,2,&id,drive.as_ptr(),1),NM_E_ARG);
        assert_eq!(nm_group_external_placements(g,std::ptr::null_mut(),std::ptr::null_mut(),0),NM_E_STATE);
        assert_eq!(nm_group_set_external(g,2,&id,owner.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_drives(g,2,&(id+1),drive.as_ptr(),1),NM_E_ARG);
        assert_eq!(nm_group_set_external(g,3,&id,owner.as_ptr(),1),0);
        let duplicate=[id,id];let mut repeated=[0.;12];repeated[..6].copy_from_slice(&drive);repeated[6..].copy_from_slice(&drive);
        assert_eq!(nm_group_set_external_drives(g,3,duplicate.as_ptr(),repeated.as_ptr(),2),NM_E_ARG);
        assert_eq!(nm_group_set_external(g,4,&id,owner.as_ptr(),1),0);
        let mut bad=drive;bad[4]=f32::NAN;
        assert_eq!(nm_group_set_external_drives(g,4,&id,bad.as_ptr(),1),NM_E_ARG);
        assert_eq!(nm_group_set_external(g,5,&id,owner.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_drives(g,5,std::ptr::null(),std::ptr::null(),0),0);
        nm_group_destroy(g);
    }}
    #[test]
    fn predictor_contract_rejects_partial_generations_stale_epoch_and_invalid_rotation(){unsafe{
        let g=nm_group_create();let id=(2u64<<32)|0xFFFFFF01;let rows=row();
        let good=[0.01,0.,0.,0.,0.,0.,1.];
        assert_eq!(nm_group_set_external(g,42,&id,rows.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_predictors(g,42,&id,good.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_predictors(g,42,std::ptr::null(),std::ptr::null(),0),NM_E_ARG);
        assert_eq!(nm_group_external_placements(g,std::ptr::null_mut(),std::ptr::null_mut(),0),NM_E_STATE);
        assert_eq!(nm_group_set_external(g,43,&id,rows.as_ptr(),1),0);
        assert_eq!(nm_group_set_external_predictors(g,42,&id,good.as_ptr(),1),NM_E_ARG);
        assert_eq!(nm_group_set_external(g,44,&id,rows.as_ptr(),1),0);
        let replacement=id+1;
        assert_eq!(nm_group_set_external_predictors(g,44,&replacement,good.as_ptr(),1),NM_E_ARG);
        assert_eq!(nm_group_set_external(g,45,&id,rows.as_ptr(),1),0);
        let mut invalid=good;invalid[6]=2.;
        assert_eq!(nm_group_set_external_predictors(g,45,&id,invalid.as_ptr(),1),NM_E_ARG);
        assert!((*g).external_invalid);
        nm_group_destroy(g);
    }}
    #[test]
    fn staging_is_atomic_and_failure_cannot_reuse_an_old_finite_owner(){unsafe{
        let g=nm_group_create();let ids=[(2u64<<32)|257];let mut data=row();
        assert_eq!(nm_group_begin(g),0);
        assert_eq!(nm_group_set_external(g,13,ids.as_ptr(),data.as_ptr(),1),0);
        assert!((*g).external_ids.contains(&ids[0]));
        data[13]=-1.;
        assert_eq!(nm_group_set_external(g,14,ids.as_ptr(),data.as_ptr(),1),NM_E_ARG);
        assert!((*g).external_ids.is_empty());assert!((*g).external_invalid);
        let w=nm_world_create();assert_eq!(nm_group_step(g,w,1./120.),NM_E_STATE);
        assert_eq!(nm_group_begin(g),0);assert!(!(*g).external_invalid);
        assert_eq!(nm_group_set_external(g,15,std::ptr::null(),std::ptr::null(),0),0);
        assert_eq!(nm_group_step(g,w,1./120.),0);
        nm_world_destroy(w);nm_group_destroy(g);
    }}
    #[test]
    fn generation_ids_roundtrip_without_float_conversion_and_duplicate_snapshots_must_agree(){unsafe{
        let g=nm_group_create();let id=(2u64<<32)|0xFFFFFF01;let ids=[id,id];let one=row();
        let mut rows=[0.;54];rows[..27].copy_from_slice(&one);rows[27..].copy_from_slice(&one);
        assert_eq!(nm_group_set_external(g,u64::MAX,ids.as_ptr(),rows.as_ptr(),2),0);
        assert_eq!((*g).external_ids.len(),1);assert!((*g).external_ids.contains(&id));
        rows[30]=1.;assert_eq!(nm_group_set_external(g,99,ids.as_ptr(),rows.as_ptr(),2),NM_E_ARG);
        assert!((*g).external_invalid);
        let w=nm_world_create();let box_row=[0.,0.,0.,0.,0.,0.,1.,1.,1.,1.,0.,0.,0.];
        assert_eq!(nm_world_set_boxes(w,box_row.as_ptr(),1),0);
        assert_eq!(nm_world_set_obstacle_owner(w,0,id),0);assert_eq!((&(*w).1)[0].owner,Some(id));
        assert_eq!(nm_world_set_obstacle_owner(w,0,0),0);assert_eq!((&(*w).1)[0].owner,None);
        assert_eq!(nm_world_set_obstacle_owner(w,-1,id),NM_E_ARG);
        nm_world_destroy(w);nm_group_destroy(g);
    }}
}
