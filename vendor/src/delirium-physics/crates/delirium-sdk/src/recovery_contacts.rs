//! Pure contact query for handing an animated recovery back to its same body.
//! It uses the existing pair manifold/collider bases, including swept parts.
//! No task changes, impulses, pose writes, or behaviour thresholds occur here.
use super::*;

const STATE: usize = 13;
const CONTACT: usize = 13;

pub(super) fn read_states(raw: &[f32], parts: usize) -> Option<(Vec<Transform>, Vec<(Vec3, Vec3)>)> {
    if raw.len() != parts * STATE || !raw.iter().all(|x| x.is_finite()) { return None; }
    let mut pose = Vec::with_capacity(parts); let mut motion = Vec::with_capacity(parts);
    for s in raw.chunks_exact(STATE) {
        let q = Quat::from_xyzw(s[3], s[4], s[5], s[6]);
        // Reject corrupt palettes; normalise only the representation, not the
        // shape, offsets, inferred motion or elapsed contact time.
        if q.length_squared() < 0.5 || q.length_squared() > 1.5 { return None; }
        pose.push(Transform::from_translation(Vec3::from_slice(s)).with_rotation(q.normalize()));
        motion.push((Vec3::from_slice(&s[7..]), Vec3::from_slice(&s[10..])));
    }
    Some((pose, motion))
}

fn query(a: &Ragdoll, ab: &Build, ap: &[Transform], av: &[(Vec3, Vec3)], old_a: &[Transform],
         b: &Ragdoll, bb: &Build, bp: &[Transform], bv: &[(Vec3, Vec3)], old_b: &[Transform]) -> Vec<[f32; CONTACT]> {
    a.query_pose_contacts(ab, ap, old_a, b, bb, bp, old_b).into_iter().map(|c| {
        let va = av[c.a].0 + av[c.a].1.cross(c.point_a - ap[c.a].translation);
        let vb = bv[c.b].0 + bv[c.b].1.cross(c.point_b - bp[c.b].translation);
        [c.a as f32, c.b as f32, c.point_a.x, c.point_a.y, c.point_a.z,
         c.point_b.x, c.point_b.y, c.point_b.z, c.normal.x, c.normal.y, c.normal.z,
         c.gap, -(va-vb).dot(c.normal)]
    }).collect()
}

/// Each input has `parts*13` floats: position3, quaternion xyzw4, linear3,
/// angular3. Previous inputs use that same layout (their motion is ignored).
/// Each output row has 13 floats: partA,partB,pointA3,pointB3,normal3,gap,
/// closing m/s. Returns number written, negative on invalid input. Immutable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_pair_contacts(a: *const NmAgent, a_states: *const f32, a_previous: *const f32, a_parts: i32,
    b: *const NmAgent, b_states: *const f32, b_previous: *const f32, b_parts: i32, out: *mut f32, capacity: i32) -> i32 {
    if a.is_null() || b.is_null() || a == b || [a_states, a_previous, b_states, b_previous].iter().any(|p| p.is_null()) || out.is_null() || capacity <= 0 {
        return fail(NM_E_ARG, "invalid pair contact input");
    }
    let (a, b) = unsafe { (&*a, &*b) };
    if a_parts <= 0 || b_parts <= 0 || a_parts as usize != a.build.parts.len() || b_parts as usize != b.build.parts.len() {
        return fail(NM_E_ARG, "pair contact part counts do not match builds");
    }
    let read = |p: *const f32, n: i32| read_states(unsafe { std::slice::from_raw_parts(p, n as usize * STATE) }, n as usize);
    let (Some((ap,av)), Some((old_a,_)), Some((bp,bv)), Some((old_b,_))) =
        (read(a_states,a_parts),read(a_previous,a_parts),read(b_states,b_parts),read(b_previous,b_parts)) else {
        return fail(NM_E_ARG, "nonfinite or invalid contact palette");
    };
    let rows = query(&a.agent.doll,&a.build,&ap,&av,&old_a,&b.agent.doll,&b.build,&bp,&bv,&old_b);
    let count = rows.len().min(capacity as usize);
    for (i,row) in rows.iter().take(count).enumerate() { unsafe { std::ptr::copy_nonoverlapping(row.as_ptr(),out.add(i*CONTACT),CONTACT); } }
    count as i32
}

/// After restoring current part states, submit the preceding COMPLETED render
/// states, same13-float layout. The next solve consumes this trajectory once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_set_contact_history(a:*mut NmAgent,previous:*const f32,parts:i32)->i32 {
    let Some(a)=(unsafe{a.as_mut()}) else{return fail(NM_E_ARG,"null contact-history agent")};
    if previous.is_null()||parts<=0||parts as usize!=a.build.parts.len(){return fail(NM_E_ARG,"invalid contact-history palette")}
    let Some((pose,_))=read_states(unsafe{std::slice::from_raw_parts(previous,parts as usize*STATE)},parts as usize)
        else{return fail(NM_E_ARG,"invalid contact-history transform")};
    if !a.agent.doll.set_contact_history(&pose){return fail(NM_E_ARG,"contact-history frame count")}
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    fn build(boxed: Option<Vec3>) -> Build {
        let root = std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let asset=std::ffi::CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
        unsafe { assert!(matches!(nm_init(asset.as_ptr()),0|1)); }
        let (source,_)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
        let mut p=source.parts[16].clone();p.parent=None;p.bone=0;p.mass=4.;
        p.boxed=boxed;p.radius=0.2;p.half=0.;p.anchor=Vec3::ZERO;p.offset=Transform::IDENTITY;
        p.inertia=Vec3::splat(0.4*p.mass*0.2*0.2);
        Build{parts:vec![p],nm:source.nm}
    }
    fn contacts(distance: f32, velocity: Vec3) -> Vec<[f32; CONTACT]> {
        let b=build(None);let pa=[Transform::IDENTITY];let pb=[Transform::from_xyz(distance,0.,0.)];
        let a=Ragdoll::new(&b,&pa,Vec3::ZERO);let c=Ragdoll::new(&b,&pb,Vec3::ZERO);
        query(&a,&b,&pa,&[(velocity,Vec3::ZERO)],&pa,&c,&b,&pb,&[(Vec3::ZERO,Vec3::ZERO)],&pb)
    }
    #[test] fn separated_body_is_not_promoted_by_root_proximity() { assert!(contacts(0.5,Vec3::X).is_empty()); }
    #[test] fn actual_penetration_has_signed_gap_and_contact_closing() {
        let cs=contacts(0.35,Vec3::X);assert!(!cs.is_empty());
        assert!(cs.iter().all(|c| (c[11]+0.05).abs()<0.005 && (c[12]-1.).abs()<0.001));
        let cs=contacts(0.35,-Vec3::X);assert!(cs.iter().all(|c| c[12]<0.));
    }
    #[test] fn point_velocity_includes_rotation_without_invented_linear_kick() {
        let b=build(None);let pa=[Transform::IDENTITY];let pb=[Transform::from_xyz(0.35,0.,0.)];
        let a=Ragdoll::new(&b,&pa,Vec3::ZERO);let c=Ragdoll::new(&b,&pb,Vec3::ZERO);
        let cs=query(&a,&b,&pa,&[(Vec3::ZERO,Vec3::Z*4.)],&pa,&c,&b,&pb,&[(Vec3::ZERO,Vec3::ZERO)],&pb);
        // A sphere spinning tangentially at this contact has no normal closing.
        assert!(!cs.is_empty());assert!(cs.iter().all(|c| c[12].abs()<0.001));
        assert_eq!(a.motion(0),(Vec3::ZERO,Vec3::ZERO));
    }
    #[test] fn swept_crossing_uses_relative_motion_and_unchanged_bodies() {
        let b=build(None);let pa=[Transform::from_xyz(0.4,0.,0.)];let old=[Transform::from_xyz(-0.6,0.,0.)];let pb=[Transform::IDENTITY];
        let a=Ragdoll::new(&b,&pa,Vec3::ZERO);let c=Ragdoll::new(&b,&pb,Vec3::ZERO);
        let rows=query(&a,&b,&pa,&[(Vec3::X*10.,Vec3::ZERO)],&old,&c,&b,&pb,&[(Vec3::ZERO,Vec3::ZERO)],&pb);
        assert!(!rows.is_empty());assert!(rows.iter().any(|r|r[12]>0.));
        assert_eq!(a.frames().next().unwrap().translation,pa[0].translation);
    }
    #[test] fn exact_box_and_contact_mask_respected() {
        let b=build(Some(Vec3::new(0.3,0.1,0.1)));let pa=[Transform::IDENTITY];let pb=[Transform::from_xyz(0.5,0.,0.)];
        let mut a=Ragdoll::new(&b,&pa,Vec3::ZERO);let c=Ragdoll::new(&b,&pb,Vec3::ZERO);
        assert!(!a.query_pose_contacts(&b,&pa,&pa,&c,&b,&pb,&pb).is_empty());
        a.contact_off=1;assert!(a.query_pose_contacts(&b,&pa,&pa,&c,&b,&pb,&pb).is_empty());
    }
    #[test] fn invalid_palette_cannot_create_contact() {
        let mut state=[0f32;13];assert!(read_states(&state,1).is_none());state[6]=1.;
        assert!(read_states(&state,1).is_some());state[0]=f32::NAN;assert!(read_states(&state,1).is_none());
    }
    #[test]
    fn rendered_crossing_survives_promotion_and_is_solved_reciprocally() {
        let b=build(None);let old=[Transform::from_xyz(-0.6,0.,10.)];
        let pa=[Transform::from_xyz(0.6,0.,10.)];let pb=[Transform::from_xyz(0.,0.,10.)];
        let mut a=Ragdoll::new(&b,&old,Vec3::ZERO);let mut c=Ragdoll::new(&b,&pb,Vec3::ZERO);
        let rows=query(&a,&b,&pa,&[(Vec3::X*12.,Vec3::ZERO)],&old,&c,&b,&pb,&[(Vec3::ZERO,Vec3::ZERO)],&pb);
        assert!(rows.iter().any(|r|r[12]>0.),"rendered crossing was not detected");
        // Same operation as resumePhysicalContact: put the last rendered state
        // back on the kept body, without impulses or a new agent.
        a.set_body(0,pa[0].translation,pa[0].rotation,Vec3::X*12.,Vec3::ZERO);
        assert!(a.set_contact_history(&old));
        let world=Collision::default();let mut group=delirium::ragdoll::Group::default();
        let drive=delirium::ragdoll::Drive{lift:1.,..Default::default()};
        let mut members=[delirium::ragdoll::Member{doll:&mut a,id:1,build:&b,drive,obstacles:&[],others:&[]},
                         delirium::ragdoll::Member{doll:&mut c,id:2,build:&b,drive,obstacles:&[],others:&[]}];
        Ragdoll::step_group(&mut members,&world,1./60.,&mut group);
        assert!(group.contact_summary()[0]>0.,"query accepted crossing, but group lost its history during promotion");
        assert!(c.motion(0).0.x>0.,"no reciprocal impulse reached the peer");
        let mut free_a=Ragdoll::new(&b,&pa,Vec3::X*12.);let mut free_c=Ragdoll::new(&b,&pb,Vec3::ZERO);
        free_a.step(&b,&world,1./60.,drive,&[],&[]);free_c.step(&b,&world,1./60.,drive,&[],&[]);
        let total=a.motion(0).0.x*a.mass()+c.motion(0).0.x*c.mass();
        let expected=free_a.motion(0).0.x*free_a.mass()+free_c.motion(0).0.x*free_c.mass();
        assert!((total-expected).abs()<0.002,"promotion created unbalanced momentum: {total} vs {expected}");
        assert!(a.frames().next().unwrap().translation.x<c.frames().next().unwrap().translation.x-0.30,
            "swept contact did not restore the original side of the peer");
        // Continue for two seconds: history is consumed once; no replayed
        // collision, crossing, unbalanced momentum or phantom re-promotion.
        for _ in 0..120 {
            let mut members=[delirium::ragdoll::Member{doll:&mut a,id:1,build:&b,drive,obstacles:&[],others:&[]},
                             delirium::ragdoll::Member{doll:&mut c,id:2,build:&b,drive,obstacles:&[],others:&[]}];
            Ragdoll::step_group(&mut members,&world,1./60.,&mut group);
            free_a.step(&b,&world,1./60.,drive,&[],&[]);free_c.step(&b,&world,1./60.,drive,&[],&[]);
            let total=a.motion(0).0.x*a.mass()+c.motion(0).0.x*c.mass();
            let expected=free_a.motion(0).0.x*free_a.mass()+free_c.motion(0).0.x*free_c.mass();
            assert!((total-expected).abs()<0.002,"later step added unbalanced momentum");
            assert!(a.frames().next().unwrap().translation.x<c.frames().next().unwrap().translation.x-0.30,
                "body crossed its peer after the initial promotion");
        }
    }
}
