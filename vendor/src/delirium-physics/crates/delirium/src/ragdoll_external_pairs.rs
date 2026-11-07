//! Animated/native owner pairs enter the SAME mobility island as NM contacts.
//! Original composite masks and 82976BC8 target/move policy are reused from
//! the owned lsc4-combat port; no render-bone velocity or second mass bridge.
use super::*;
use crate::external_mobility::ExternalOwner;
use lsc4_combat::pedbodies;

pub(super) struct OwnerPairRow {
    pub a:u64,pub b:u64,pub child_a:usize,pub child_b:usize,
    pub point_a:Vec3,pub point_b:Vec3,pub normal:Vec3,
    pub row:Row,pub move_a:Vec3,pub move_b:Vec3,
}
fn parts(o:&Obstacle)->Vec<ObPart>{
    if !o.parts.is_empty(){return o.parts.to_vec();}
    vec![ObPart{shape:gjk::Shape::Box(o.half,0.),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:-o.half,hi:o.half,mesh:None,filter:Default::default()}]
}
// Only the cheap rejection path differs. The exact manifold below remains the
// authority, including its masks, contact reach, normal and response.
#[cfg(test)]thread_local!{static CHILD_BOUNDS:std::cell::Cell<u8>=const{std::cell::Cell::new(0)};}
#[cfg(test)]thread_local!{static MANIFOLD_CALLS:std::cell::Cell<usize>=const{std::cell::Cell::new(0)};}
fn child_bounds_mode()->u8{
    #[cfg(test)]{return CHILD_BOUNDS.with(|v|v.get());}
    #[cfg(not(test))]{
        if env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_OWNER_PAIR_NO_CHILD_BOUNDS"){2}
        else if env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_OWNER_PAIR_LEGACY_BOUNDS"){1}else{0}
    }
}
struct PreparedPart{part:ObPart,now:(Vec3,Mat3),old:(Vec3,Mat3),half:Vec3,bounds:(Vec3,Vec3)}
#[cfg(test)]thread_local!{static PREPARED_CHILDREN:std::cell::Cell<usize>=const{std::cell::Cell::new(0)};}
#[cfg(test)]thread_local!{static EAGER_PREPARE:std::cell::Cell<bool>=const{std::cell::Cell::new(false)};}
fn prepare(ob:&Obstacle)->Vec<PreparedPart>{
    #[cfg(test)]PREPARED_CHILDREN.with(|n|n.set(n.get()+ob.parts.len().max(1)));
    let rotation=Mat3::from_quat(ob.rot);
    parts(ob).into_iter().map(|part|{
        let now=(ob.at+rotation*part.at,rotation*part.m);
        let old=ob.last.map_or(now,|(at,q)|{let r=Mat3::from_quat(q);(at+r*part.at,r*part.m)});
        let half=(part.hi-part.lo)*0.5;
        // The old sphere was centred on the component origin and lost the
        // actual local centre (lo+hi)/2. A remote mesh triangle could be missed
        // where it really was, or sent to GJK as though it were at the origin.
        // Existing support/arc bounds retain the complete shape, margin and
        // both moving frames. Cache once per call, never across native frames.
        let bounds=part.shape.rotating_bounds((old.0,Quat::from_mat3(&old.1)),(now.0,Quat::from_mat3(&now.1)),pedbodies::BREAK);
        PreparedPart{part,now,old,half,bounds}
    }).collect()
}
fn side(ob:&Obstacle,owner:&ExternalOwner,point:Vec3,n:Vec3)->pedbodies::Side {
    let rotation=Mat3::from_quat(ob.rot);
    pedbodies::Side{rows:rotation.to_cols_array_2d(),at:(rotation.transpose()*(point-ob.at)).to_array(),n:n.to_array(),
        margin:[pedbodies::MARGIN;3],inv_mass:n.dot(owner.linear_inverse*n),state:if owner.linear_inverse==Mat3::ZERO{1}else{2}}
}
fn point_j(owner:&ExternalOwner,p:Vec3,n:Vec3)->[f64;6]{
    let t=(p-owner.com).cross(n);[n.x as f64,n.y as f64,n.z as f64,t.x as f64,t.y as f64,t.z as f64]
}
// The original target is depth/dt, sized for ped overlaps of centimetres. A ped
// found a metre inside a car asked for 60+ m/s against the car's finite mass:
// impulses of 100000 launched a parked car and the one CJ had just left (07/11).
const MAX_PAIR_SPEED:f32=6.;
const MAX_PAIR_MOVE:f32=0.1;
pub(super) fn collect(group:&mut Group,h:f32)->Vec<OwnerPairRow>{
    let profile=group.profile_enabled;let(mut prepare_ms,mut prepared_children,mut narrow_calls,mut paired_shapes)=(0.,0,0,0);
    let mut measured_prepare=|ob:&Obstacle|{
        let started=profile.then(std::time::Instant::now);let parts=prepare(ob);
        if let Some(t)=started{prepare_ms+=t.elapsed().as_secs_f64()*1000.;prepared_children+=parts.len();}parts
    };
    let mut geometry=group.external_geometry.clone();geometry.sort_by_key(|o|(o.owner,o.id));
    let bounds:Vec<_>=geometry.iter().map(|o|{
        let q=Mat3::from_quat(o.rot);let local=q.transpose()*(o.centre-o.at);
        let side=|at:Vec3,r:Mat3|{let centre=at+r*local;let half=Mat3::from_cols(r.x_axis.abs(),r.y_axis.abs(),r.z_axis.abs())*o.half+Vec3::splat(pedbodies::BREAK);(centre-half,centre+half)};
        let now=side(o.at,q);let old=o.last.map_or(now,|(p,r)|side(p,Mat3::from_quat(r)));
        if let Some((p,r))=o.last {if r.dot(o.rot).abs()<1.-1e-7{
            // Endpoint AABBs alone miss a long component rotating through a
            // third orientation; this encloses its full angular sweep.
            let radius=Vec3::splat(local.length()+o.half.length()+pedbodies::BREAK);
            return(p.min(o.at)-radius,p.max(o.at)+radius);
        }}
        (now.0.min(old.0),now.1.max(old.1))
    }).collect();
    // Prepare child frames only after the unchanged owner-pair filters accept.
    // Traffic outside every candidate pair must not pay per-triangle support
    // and rotational-arc construction on every physics substep.
    let prepared:Vec<std::cell::OnceCell<Vec<PreparedPart>>>=
        geometry.iter().map(|_|std::cell::OnceCell::new()).collect();
    #[cfg(test)]if EAGER_PREPARE.with(|v|v.get()){
        for (slot,ob) in prepared.iter().zip(&geometry){slot.get_or_init(||measured_prepare(ob));}
    }
    let bounds_mode=child_bounds_mode();
    let mut pairs=vec![];
    for i in 0..geometry.len(){for k in i+1..geometry.len(){
        let(a,b)=(&geometry[i],&geometry[k]);let(ia,ib)=(a.owner.unwrap(),b.owner.unwrap());
        if ia==ib||!a.filter.meets(b.filter){continue;}
        if !bounds[i].0.cmple(bounds[k].1).all()||!bounds[k].0.cmple(bounds[i].1).all(){continue;}
        // Car/car remains owned by SA's vehicle solver. A registered animated
        // ped participates here against peds or other staged finite owners.
        if !a.filter.animated_ped()&&!b.filter.animated_ped(){continue;}
        if profile{paired_shapes+=1;}
        let(oa,ob)=(group.external.owners[&ia],group.external.owners[&ib]);
        let parts_a=prepared[i].get_or_init(||measured_prepare(a));
        let parts_b=prepared[k].get_or_init(||measured_prepare(b));
        for (ca,prepared_a) in parts_a.iter().enumerate(){for(cb,prepared_b)in parts_b.iter().enumerate(){
            let(pa,pb)=(&prepared_a.part,&prepared_b.part);
            if !pa.filter.meets(pb.filter)||matches!(pa.shape,gjk::Shape::Hull(v,_)if v.is_empty())||matches!(pb.shape,gjk::Shape::Hull(v,_)if v.is_empty()){continue;}
            let(ha,hb)=(prepared_a.half,prepared_b.half);
            let(fa,fb)=(prepared_a.now,prepared_b.now);let(sa,sb)=(prepared_a.old,prepared_b.old);
            if bounds_mode==0{
                let(ba,bb)=(prepared_a.bounds,prepared_b.bounds);
                if !ba.0.cmple(bb.1).all()||!bb.0.cmple(ba.1).all(){continue;}
            }else if bounds_mode==1{
                let(ba,bb)=(swept(fa,sa,ha),swept(fb,sb,hb));
                if (fa.0+fa.1*ba.0).distance(fb.0+fb.1*bb.0)>ba.1.length()+bb.1.length()+pedbodies::BREAK{continue;}
            }
            // Same exact four-axis mesh rejection used for NM/mesh contacts.
            // Ordinary animated owners still query these pairs after recovery;
            // a ragdoll-only optimization leaves that real CPU spike intact.
            // Restrict the boxed side to origin-centred analytic shapes; a
            // generic off-centre hull's half extents are not centred at `fa`.
            if bounds_mode==0&&owner_mesh_sat(){
                let centred=|s|matches!(s,gjk::Shape::Box(..)|gjk::Shape::Capsule(..));
                if centred(pa.shape)&&!mesh_manifold::triangle_allows(ha,fa,sa,pb.shape,fb,sb){continue;}
                if centred(pb.shape)&&!mesh_manifold::triangle_allows(hb,fb,sb,pa.shape,fa,sa){continue;}
            }
            #[cfg(test)]MANIFOLD_CALLS.with(|n|n.set(n.get()+1));
            if profile{narrow_calls+=1;}
            let mut held=vec![];Ragdoll::manifold(&mut held,(pa.shape,ha,fa,sa),(pb.shape,hb,fb,sb));
            for c in held {
                if c.gap>pedbodies::BREAK{continue;}
                let(point_a,point_b)=(fa.0+fa.1*c.local,fb.0+fb.1*c.ground);let n=c.n;
                let solve_a=external_ground::pair(group,a,b,ca,point_b,n);
                let solve_b=external_ground::pair(group,b,a,cb,point_a,-n);
                if !solve_a||!solve_b{continue;}
                let target=pedbodies::target(-c.gap,1./h,&side(a,&oa,point_a,n),&side(b,&ob,point_b,-n));
                let(ja,jb)=(point_j(&oa,point_a,n),point_j(&ob,point_b,-n));
                let j=ja.into_iter().chain(jb).collect();let mj=oa.response(ja).into_iter().chain(ob.response(jb)).collect();
                let relative=(a.vel_at(point_a)-oa.point_velocity(point_a))-(b.vel_at(point_b)-ob.point_velocity(point_b));
                let row=Row{relaxation:pedbodies::SOR as f64,..Row::with(j,mj,(target.speed.min(MAX_PAIR_SPEED)-relative.dot(n)) as f64,RowKind::Push)};
                let(ma,mb)=target.moves.map_or((Vec3::ZERO,Vec3::ZERO),|(a,b)|(Vec3::from(a).clamp_length_max(MAX_PAIR_MOVE),Vec3::from(b).clamp_length_max(MAX_PAIR_MOVE)));
                pairs.push(OwnerPairRow{a:ia,b:ib,child_a:ca,child_b:cb,point_a,point_b,normal:n,row,move_a:ma,move_b:mb});
            }
        }}
    }}
    if profile{group.profile_pair_prepare_ms=prepare_ms;group.profile_pair_counts=[prepared_children,narrow_calls,pairs.len(),paired_shapes];}
    pairs
}

#[cfg(test)]thread_local!{static OWNER_MESH_SAT:std::cell::Cell<bool>=const{std::cell::Cell::new(true)};}
fn owner_mesh_sat()->bool{
    #[cfg(test)]{return OWNER_MESH_SAT.with(|v|v.get())}
    #[cfg(not(test))]{!(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_OWNER_MESH_SAT_BASELINE"))}
}

#[cfg(test)]mod bounds_regressions{
 use super::*;
 fn scene(offset:f32,body_offset:f32,angle:f32,previous:bool,count:usize)->Group{
  super::super::moving_obstacle_regression::constants();
  let root=Vec3::Z*15.;let rot=Quat::from_rotation_z(angle);
  let body=root+rot*Vec3::X*body_offset;
  let owners=[ExternalOwner{id:1,epoch:1,com:body,linear_inverse:Mat3::IDENTITY/25.,angular_inverse:Mat3::ZERO,velocity:Vec3::ZERO,spin:Vec3::ZERO},ExternalOwner{id:2,epoch:1,com:root,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::ZERO,velocity:Vec3::ZERO,spin:Vec3::ZERO}];
  let sphere=ObPart{shape:gjk::Shape::Capsule(0.,0.2),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::splat(-0.2),hi:Vec3::splat(0.2),mesh:None,filter:Default::default()};
  let t=[Vec3::new(offset,-0.5,-0.5),Vec3::new(offset,0.5,-0.5),Vec3::new(offset,0.,0.5)];
  let triangle=ObPart{shape:gjk::Shape::Tri(t,0.002),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:t[0].min(t[1]).min(t[2]),hi:t[0].max(t[1]).max(t[2]),mesh:None,filter:Default::default()};
  let mut g=Group::default();g.stage_external(1,&owners).unwrap();
  let a=Obstacle{id:11,owner:Some(1),filter:CollisionFilter{kind:0x20,include:0x79FE},at:body,centre:body,com:body,rot,half:Vec3::splat(0.25),parts:Box::leak(vec![sphere].into_boxed_slice()),..Default::default()};
  let mut b=Obstacle{id:22,owner:Some(2),at:root,centre:root,com:root,rot,half:Vec3::splat(offset.abs()+2.),parts:Box::leak(vec![triangle;count].into_boxed_slice()),..Default::default()};
  if previous{b.last=Some((root+Vec3::new(-0.02,0.01,0.),Quat::from_rotation_z(angle-0.15)));}
  g.stage_external_geometry(&[a,b]).unwrap();g
 }
 fn run(g:&mut Group,mode:u8)->(Vec<u64>,usize){
  CHILD_BOUNDS.with(|v|v.set(mode));MANIFOLD_CALLS.with(|v|v.set(0));
  let rows=collect(g,1./60.);let count=MANIFOLD_CALLS.with(|v|v.get());CHILD_BOUNDS.with(|v|v.set(0));
  let mut bits=vec![rows.len()as u64];
  for p in rows{
   bits.extend([p.a,p.b,p.child_a as u64,p.child_b as u64]);
   for v in [p.point_a,p.point_b,p.normal,p.move_a,p.move_b]{bits.extend(v.to_array().map(|f|f.to_bits()as u64));}
   // Full constraint data, not just the presence/count of a collision.
   bits.extend(p.row.j.iter().chain(p.row.mj.iter()).map(|f|f.to_bits()));
   bits.extend([p.row.target.to_bits(),p.row.eff.to_bits(),p.row.lambda.to_bits(),p.row.relaxation.to_bits()]);
  }(bits,count)
 }
 #[test]fn lazy_preparation_preserves_rows_and_skips_unpaired_dense_geometry(){
  for separated in [false,true]{
   let mut g=scene(0.,0.1,0.7,true,2048);
   if separated{let mut obs=g.external_geometry.clone();obs[1].at+=Vec3::X*100.;obs[1].centre+=Vec3::X*100.;obs[1].last=None;g.stage_external_geometry(&obs).unwrap();}
   EAGER_PREPARE.with(|v|v.set(true));PREPARED_CHILDREN.with(|v|v.set(0));
   let old=run(&mut g,0);let eager=PREPARED_CHILDREN.with(|v|v.get());
   EAGER_PREPARE.with(|v|v.set(false));PREPARED_CHILDREN.with(|v|v.set(0));
   let new=run(&mut g,0);let lazy=PREPARED_CHILDREN.with(|v|v.get());
   assert_eq!(old,new,"all ordered witnesses and constraint bits must match");
   assert_eq!(eager,2049);assert_eq!(lazy,if separated{0}else{2049});
  }
 }
 #[test]fn offcentre_triangle_contact_was_rejected_at_component_origin(){
  for angle in [0.,0.7,2.4]{let mut g=scene(10.,10.1,angle,false,1);
   let oracle=run(&mut g,2);let old=run(&mut g,1);let corrected=run(&mut g,0);
   assert!(oracle.0[0]>0,"actual remote triangle must contact the sphere");
   assert_eq!(old.0[0],0,"must reproduce the lost-centre rejection");assert_eq!(corrected.0,oracle.0);
  }
 }
 #[test]fn corrected_bounds_match_unculled_contact_witnesses_through_motion(){
  let mut contacts=0;
  for offset in [-10.,-0.7,0.,0.7,10.]{for distance in [-0.3,-0.1,0.,0.1,0.3]{for angle in [0.,0.7,2.4]{for moving in [false,true]{
   let mut g=scene(offset,offset+distance,angle,moving,1);let oracle=run(&mut g,2);let actual=run(&mut g,0);
   contacts+=oracle.0[0];assert_eq!(actual.0,oracle.0,"offset={offset} distance={distance} angle={angle} moving={moving}");
  }}}}assert!(contacts>20);
 }
 #[test]fn remote_dense_children_do_not_enter_narrowphase_at_mesh_origin(){
  let mut g=scene(10.,0.,0.,false,2048);let old=run(&mut g,1);let corrected=run(&mut g,0);
  assert_eq!(old.0,corrected.0);assert_eq!(old.0[0],0);assert_eq!(old.1,2048);assert_eq!(corrected.1,0);
 }
 #[test]fn owner_mesh_sat_rejects_separated_planes_inside_overlapping_aabbs(){
  let mut g=scene(0.,0.,0.,false,1);let mut obs=g.external_geometry.clone();
  // Outside the actual expanded box's diagonal plane, while its AABB still
  // overlaps. Every vertex must be on x+y=d: the earlier (d,d,1) third vertex
  // tilted that plane toward the box, invalidating the rejection assertion.
  let d=2.*(0.2+MARGIN()+0.002)+0.1;
  let t=[Vec3::new(0.,d,0.),Vec3::new(d,0.,0.),Vec3::new(d,0.,1.)];
  assert!(t.iter().all(|p|p.x+p.y==d));
  let part=ObPart{shape:gjk::Shape::Tri(t,0.002),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::ZERO,hi:Vec3::new(d,d,1.),mesh:None,filter:Default::default()};
  obs[1].parts=Box::leak(vec![part;2048].into_boxed_slice());g.stage_external_geometry(&obs).unwrap();
  OWNER_MESH_SAT.with(|v|v.set(false));let old=run(&mut g,0);
  OWNER_MESH_SAT.with(|v|v.set(true));let new=run(&mut g,0);
  assert_eq!(old.0,new.0,"must preserve every contact/witness/constraint bit");assert_eq!(old.0[0],0);
  assert_eq!(old.1,2048);assert_eq!(new.1,0,"triangle plane must reject before GJK");
 }
}
