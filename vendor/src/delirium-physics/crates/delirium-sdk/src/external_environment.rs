//! Separately identified native vehicle/environment collision geometry.
//! Peer/rider compound bounds in NmWorld are never replaced by this staging.
use super::*;

/// Atomic per-interval override of environment geometry for the listed owners.
/// ids are generation-bearing owners; components are unique obstacle IDs;
/// shapes are registered native COL shapes. rows19: centre3, quaternion4,
/// broadphase half3, centre velocity3, spin3, parent COM3. Empty means no
/// overrides. Original native support is preserved; no wheel model is invented.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_set_external_environment(g:*mut NmGroup,epoch:u64,ids:*const u64,components:*const i32,shapes:*const i32,rows:*const f32,count:i32)->i32{
    let Some(g)=(unsafe{g.as_mut()})else{return fail(NM_E_ARG,"null environment group")};
    let result=(||->Result<Vec<Obstacle>,String>{
        if !(0..=256).contains(&count)||(count>0&&(ids.is_null()||components.is_null()||shapes.is_null()||rows.is_null())){return Err("invalid environment batch".into())}
        let mut staged=Vec::with_capacity(count as usize);
        let cache=VEHICLE_SHAPES.lock().unwrap_or_else(|e|e.into_inner());
        for n in 0..count as usize{
            let(id,component,shape)=(unsafe{*ids.add(n)},unsafe{*components.add(n)},unsafe{*shapes.add(n)});
            let r=unsafe{std::slice::from_raw_parts(rows.add(n*19),19)};
            if id==0||component<0||shape<0||r.iter().any(|v|!v.is_finite()){return Err("invalid environment identity or geometry".into())}
            let q=Quat::from_xyzw(r[3],r[4],r[5],r[6]);
            if (q.length_squared()-1.).abs()>0.001||r[7..10].iter().any(|v|*v<=0.){return Err("invalid environment transform or bounds".into())}
            let Some((_,parts))=cache.get(shape as usize)else{return Err("unknown native environment shape".into())};
            let at=Vec3::from_slice(r);
            staged.push(Obstacle{id:component,owner:Some(id),at,rot:q,centre:at,half:Vec3::from_slice(&r[7..]),vel:Vec3::from_slice(&r[10..]),spin:Vec3::from_slice(&r[13..]),com:Vec3::from_slice(&r[16..]),parts,..Default::default()});
        }
        Ok(staged)
    })();
    let staged=match result{Ok(v)=>v,Err(e)=>{g.external_invalid=true;return fail(NM_E_ARG,e)}};
    if g.external_invalid{return fail(NM_E_STATE,"rejected external owner interval")}
    match g.solver.stage_external_environment_geometry(epoch,&staged){
        Ok(())=>0,Err(e)=>{g.external_invalid=true;fail(NM_E_ARG,format!("external environment: {e:?}"))}
    }
}

#[cfg(test)]mod tests{
 use super::*;
 #[test]fn environment_transport_rejects_wrong_epoch_and_bad_shapes_without_touching_peer_scene(){unsafe{
    let g=nm_group_create();let id=(1u64<<32)|257;let mut owner=[0f32;27];owner[9]=1.;owner[13]=1.;owner[17]=1.;
    let primitive=[1.,0.,0.,0.,1.,1.,1.,0.,0.,0.];let shape=nm_vehicle_shape_register(primitive.as_ptr(),1);assert!(shape>=0);
    let row=[0.,0.,0.,0.,0.,0.,1.,1.,1.,1.,0.,0.,0.,0.,0.,0.,0.,0.,0.];let component=7;
    assert_eq!(external_mobility::nm_group_set_external(g,1,&id,owner.as_ptr(),1),0);
    assert_eq!(nm_group_set_external_environment(g,2,&id,&component,&shape,row.as_ptr(),1),NM_E_ARG);
    assert_eq!(external_mobility::nm_group_set_external(g,2,&id,owner.as_ptr(),1),0);
    assert_eq!(nm_group_set_external_environment(g,2,&id,&component,&shape,row.as_ptr(),1),0);
    let bad=10000;assert_eq!(nm_group_set_external_environment(g,2,&id,&component,&bad,row.as_ptr(),1),NM_E_ARG);
    assert_eq!(external_mobility::nm_group_external_results(g,std::ptr::null_mut(),std::ptr::null_mut(),0),NM_E_STATE);
    assert_eq!(external_mobility::nm_group_set_external(g,3,&id,owner.as_ptr(),1),0);
    assert_eq!(nm_group_set_external_environment(g,3,std::ptr::null(),std::ptr::null(),std::ptr::null(),std::ptr::null(),0),0);
    nm_group_destroy(g);
 }}
}
