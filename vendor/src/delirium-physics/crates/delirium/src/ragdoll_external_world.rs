//! World constraints for the same finite native-owner mobility island.
//! No native gravity, suspension or wheel impulse is integrated here.
use super::*;
use crate::external_mobility::{ExternalOwner,ExternalPlacement,ExternalPlacementDiagnostic};
#[derive(Clone,Copy,Default)]
pub(super) struct WorldStats{queries:u32,triangles:u32,gjk:u32,casts:u32,iterations:u32,failures:u32,micros:u64}
impl WorldStats{
    fn write(self,d:&mut ExternalPlacementDiagnostic){d.query_count=self.queries;d.triangle_count=self.triangles;d.gjk_count=self.gjk;d.cast_count=self.casts;d.cast_failures=self.failures;d.solve_us=self.micros;}
    pub(super) fn admission_only(self,mut d:ExternalPlacementDiagnostic)->ExternalPlacementDiagnostic{
        d.query_count=d.query_count.saturating_sub(self.queries);d.triangle_count=d.triangle_count.saturating_sub(self.triangles);d.gjk_count=d.gjk_count.saturating_sub(self.gjk);
        d.cast_count=d.cast_count.saturating_sub(self.casts);d.cast_failures=d.cast_failures.saturating_sub(self.failures);d.solve_us=d.solve_us.saturating_sub(self.micros);d
    }
}
#[derive(Clone,Copy,Debug)]
struct CastFailure{code:u32,t:f32,gap:f32,iterations:u32}
#[cfg(test)]
thread_local!{static WORLD_QUERIES:std::cell::Cell<usize>=const{std::cell::Cell::new(0)};}

#[derive(Clone,Copy,PartialEq)]
pub(super) struct WorldContact {pub owner:u64,pub obstacle:i32,pub child:usize,pub instance:u32,pub point:Vec3,pub normal:Vec3,pub angular:Vec3,pub gap:f32,pub native_gap:f32,
    surface:Option<([Vec3;3],u8,[Vec3;4],u8,u32,crate::collision::Poly)>}

// These are nonlinear re-linearizations WITHIN one step, not manifold refresh
// across physical frames. Keep the multiplier of the same source inequality
// even when rotation moves its material witness beyond manifold's point-match
// radius. New surfaces fall back to hold / TU88296E1F8's nearest-old-point rule.
// Consume once: two new rows must never duplicate one old force.
pub(super) fn take_position_warm(c:&WorldContact,old:&mut[(WorldContact,Option<f64>)])->f64{
    if let Some(a)=&c.surface{
        if let Some((_,lambda))=old.iter_mut().find(|(p,lambda)|lambda.is_some()&&
            (p.owner,p.obstacle,p.child,p.instance)==(c.owner,c.obstacle,c.child,c.instance)&&
            p.surface.as_ref().is_some_and(|b|a.0==b.0&&a.4==b.4&&a.5.first==b.5.first)){
            return lambda.take().unwrap();
        }
    }
    let(mut found,mut near)=(None,delirium_data::kv!(568));
    for(i,(p,lambda))in old.iter().enumerate(){
        if lambda.is_none()||(p.owner,p.obstacle,p.child,p.instance)!=(c.owner,c.obstacle,c.child,c.instance){continue}
        let d=p.point.distance_squared(c.point);if d<near{near=d;found=Some(i);}
    }
    found.and_then(|i|old[i].1.take()).unwrap_or(0.)
}

fn parts(o:&Obstacle)->Vec<ObPart>{
    if !o.parts.is_empty(){return o.parts.to_vec();}
    vec![ObPart{shape:gjk::Shape::Box(o.half,0.),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:-o.half,hi:o.half,mesh:None,filter:Default::default()}]
}
fn jacobian(owner:&ExternalOwner,p:Vec3,n:Vec3)->[f64;6]{
    let t=(p-owner.com).cross(n);[n.x as f64,n.y as f64,n.z as f64,t.x as f64,t.y as f64,t.z as f64]
}
impl WorldContact {
    pub(super) fn row(&self,owner:&ExternalOwner,h:f32,position:bool)->Row {
        let j=[self.normal.x as f64,self.normal.y as f64,self.normal.z as f64,self.angular.x as f64,self.angular.y as f64,self.angular.z as f64];
        // The native engine already accepted its baseline and resolved tyre,
        // ground and body forces. Constrain ONLY the island's added motion;
        // an existing baseline overlap is not another depenetration impulse.
        let clearance=self.gap-self.native_gap.min(-ALLOW());
        let native=owner.velocity.dot(self.normal)+owner.spin.dot(self.angular);
        let target=if position{-clearance/h}else{native-clearance/h};
        Row::with(j.to_vec(),owner.response(j).to_vec(),target as f64,RowKind::Push)
    }
}

fn linearized_gap(end_gap:f32,angular:Vec3,normal:Vec3,translation:Vec3,rotation_vector:Vec3)->f32{
    end_gap-translation.dot(normal)-rotation_vector.dot(angular)
}

#[cfg(test)]mod position_continuation_tests{
 use super::*;
 fn owner()->ExternalOwner{ExternalOwner{id:7,epoch:1,com:Vec3::ZERO,velocity:Vec3::ZERO,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY*0.002,angular_inverse:Mat3::IDENTITY*0.006}}
 fn contact(point:Vec3,normal:Vec3)->WorldContact{WorldContact{owner:7,obstacle:4,child:0,instance:12,point,normal,angular:point.cross(normal),gap:0.,native_gap:0.,surface:None}}
 #[test]fn relinearized_contact_can_consume_only_its_actual_old_point_once(){
  super::super::moving_obstacle_regression::constants();
  let c=contact(Vec3::X,Vec3::Z);let mut old=[(c,Some(13.))];
  assert_eq!(take_position_warm(&WorldContact{instance:13,..c},&mut old),0.);
  assert_eq!(take_position_warm(&WorldContact{child:1,..c},&mut old),0.);
  assert_eq!(take_position_warm(&WorldContact{point:c.point+Vec3::X*0.1,..c},&mut old),0.);
  assert_eq!(take_position_warm(&WorldContact{point:c.point+Vec3::X*0.001,..c},&mut old),13.);
  assert_eq!(take_position_warm(&c,&mut old),0.,"one old force cannot be duplicated across new polygons");
 }
 #[test]fn restarting_contact_solution_is_not_convergence_of_a_finite_corner(){
  super::super::moving_obstacle_regression::constants();
  let owner=owner();let source=[contact(Vec3::X,Vec3::Z).row(&owner,1.,true),contact(-Vec3::Z,-Vec3::X).row(&owner,1.,true)];
  let initial=vec![0.4,0.,-0.4,0.,0.,0.];
  let run=|restart:bool|{let mut rows=source.clone();let mut result=initial.clone();
   for pass in 0..4{if restart&&pass>0{rows=source.clone();}result.clone_from(&initial);impulses(&mut rows,&mut[],&mut result);}
   rows.iter().map(|r|(r.target-r.j.iter().zip(&result).map(|(a,b)|a*b).sum::<f64>()).max(0.)).fold(0.,f64::max)
  };
  let restarted=run(true);let continued=run(false);
  assert!(restarted>0.001,"must reproduce an unresolved contact, not a trivial independent floor: {restarted}");
  assert!(continued<restarted*0.001,"same four passes/eight sweeps, preserving solved multipliers: {restarted} -> {continued}");
 }
 #[test]fn within_step_relinearization_keeps_same_surface_force_when_witness_rotates(){
  super::super::moving_obstacle_regression::constants();
  let tri=[Vec3::ZERO,Vec3::X,Vec3::Y];
  let poly=crate::collision::Poly{first:(0,17),n:Vec3::Z,edges:[None;4]};
  let c=WorldContact{surface:Some((tri,0,[Vec3::Z;4],0,12,poly)),..contact(Vec3::X,Vec3::Z)};
  let rotated=WorldContact{point:Quat::from_rotation_z(0.38)*c.point,..c};
  assert!(rotated.point.distance_squared(c.point)>delirium_data::kv!(568));
  let mut old=[(c,Some(13.))];
  assert_eq!(take_position_warm(&WorldContact{child:1,..rotated},&mut old),0.);
  assert_eq!(take_position_warm(&WorldContact{instance:13,..rotated},&mut old),0.);
  assert_eq!(take_position_warm(&rotated,&mut old),13.,"same nonlinear constraint, not a new physical frame/contact");
  assert_eq!(take_position_warm(&rotated,&mut old),0.,"surface can consume only one old force");
 }
}

// Derivative of n.dot(Exp(w)*r) with respect to the ABSOLUTE rotation
// vector w used by the position solver. r cross n is only its derivative
// at w=0. At a nonzero proposal it is J_left(w)^T * ((Exp(w)*r) cross n).
// These are mathematical series coefficients, not collision tuning values.
fn rotation_gradient(rotated_arm:Vec3,normal:Vec3,spin:Vec3)->Vec3{
    let w=spin.as_dvec3();let a=rotated_arm.as_dvec3().cross(normal.as_dvec3());let t2=w.length_squared();
    if t2==0.{return a.as_vec3();}
    let (s,c)=if t2<1e-8{(0.5-t2/24.+t2*t2/720.,1./6.-t2/120.+t2*t2/5040.)}
        else{let t=t2.sqrt();((1.-t.cos())/t2,(1.-t.sin()/t)/t2)};
    (a-w.cross(a)*s+w.cross(w.cross(a))*c).as_vec3()
}

fn closest(part:&ObPart,at:Vec3,q:Quat,tri:[Vec3;3],dirs:[Vec3;4])->Option<gjk::Closest>{
    closest_traced(part,at,q,tri,dirs,&mut WorldStats::default())
}
fn closest_traced(part:&ObPart,at:Vec3,q:Quat,tri:[Vec3;3],dirs:[Vec3;4],stats:&mut WorldStats)->Option<gjk::Closest>{
    stats.gjk+=1;
    let shape=gjk::Shape::Tri(tri.map(|p|p-tri[0]),delirium_data::kv!(565));
    // Pen::Tri samples the supplied normal cone during overlap. Supply that
    // cone on the side occupied by this component, as well as preserving its
    // sign in the later adjacency clamp. Otherwise its fallback can report
    // the far face at first contact from the back of an authored triangle.
    let centre=at+q*((part.lo+part.hi)*0.5);
    let side=if dirs[0].dot(centre-tri[0])<0.{-1.}else{1.};let dirs=dirs.map(|n|n*side);
    let m=Mat3::from_quat(q);
    let margin=match part.shape{gjk::Shape::Box(_,m)|gjk::Shape::Capsule(_,m)|gjk::Shape::Tri(_,m)|gjk::Shape::Hull(_,m)|gjk::Shape::Curved(_,m)|gjk::Shape::Tapered(_,_,m)=>m};
    // Pen::Tri is an overlap resolver, not a signed-distance query. Its
    // degeneracy fallback can turn a separated thin pair's +gap into -gap.
    // A supporting plane proves separation including BOTH actual margins;
    // in that case keep GJK's distance result and never request penetration.
    let plane=dirs[0].dot(at-tri[0]+m*part.shape.support(m.transpose()*-dirs[0]))-margin-delirium_data::kv!(565);
    let penetration=(plane<=0.).then_some(gjk::Pen::Tri(dirs));
    let mut c=gjk::closest(&part.shape,m,at-tri[0],&shape,Mat3::IDENTITY,Vec3::ZERO,dirs[0],f32::INFINITY,penetration)?;
    c.a+=tri[0];c.b+=tri[0];Some(c)
}

/// Conservative advancement uses the actual quaternion trajectory. Endpoint
/// AABBs and a translational sweep at the FINAL rotation miss rotating doors.
fn cast_limited(part:&ObPart,from:(Vec3,Quat),to:(Vec3,Quat),tri:[Vec3;3],dirs:[Vec3;4],limit:usize)->Result<Option<(f32,gjk::Closest)>,CastFailure>{
    cast_traced(part,from,to,tri,dirs,limit,&mut WorldStats::default())
}
#[cfg(test)]thread_local!{static MAGNITUDE_CAST_BASELINE:std::cell::Cell<bool>=const{std::cell::Cell::new(false)};}
fn cast_traced(part:&ObPart,from:(Vec3,Quat),to:(Vec3,Quat),tri:[Vec3;3],dirs:[Vec3;4],limit:usize,stats:&mut WorldStats)->Result<Option<(f32,gjk::Closest)>,CastFailure>{
    stats.casts+=1;
    #[cfg(test)]let baseline=MAGNITUDE_CAST_BASELINE.with(|v|v.get());
    #[cfg(not(test))]let baseline=false;
    // GJK's spherical margin is isotropic. A capsule's rotating core is its
    // line segment, not that margin; a zero-half sphere has NO angular motion.
    let radius=if !baseline{match part.shape{gjk::Shape::Capsule(h,_)=>h.abs(),_=>part.lo.abs().max(part.hi.abs()).length()}}
        else{part.lo.abs().max(part.hi.abs()).length()};
    let angle=2.*from.1.dot(to.1).abs().clamp(0.,1.).acos();
    let travel=to.0-from.0;let angular=angle*radius;
    let fail=|stats:&mut WorldStats,code,t,gap,iterations|{stats.failures+=1;Err(CastFailure{code,t,gap,iterations})};
    if !travel.is_finite()||!angular.is_finite(){return fail(stats,2,0.,f32::NAN,0);}
    let(mut t,mut last_gap)=(0.,f32::NAN);
    for iteration in 0..limit {
        stats.iterations+=1;
        let Some(c)=closest_traced(part,from.0.lerp(to.0,t),from.1.slerp(to.1,t),tri,dirs,stats)else{return fail(stats,1,t,last_gap,iteration as u32+1)};
        last_gap=c.dist;
        if !c.dist.is_finite(){return fail(stats,2,t,c.dist,iteration as u32+1);}
        if c.dist<=1e-5{return Ok(Some((t,c)));}
        // Advance to the current separating PLANE, not by total sideways
        // travel. Its linear closing speed plus the angular support bound
        // cannot cross that plane earlier. Tangential velocity cannot close
        // it. Reference: Bullet btContinuousConvexCollision::calcTimeOfImpact,
        // blob623c58fc, projectedLinearVelocity + maxAngularProjectedVelocity.
        let bound=if baseline{travel.length()+angular}else{(-travel.dot(c.n)+angular).max(0.)};
        if bound<=1e-8{return Ok(None);}
        let next=t+c.dist/bound;
        if next>1.{return Ok(None);}
        if next<=t{return fail(stats,3,t,c.dist,iteration as u32+1);}
        t=next;
    }
    fail(stats,4,t,last_gap,limit as u32)
}
fn cast(part:&ObPart,from:(Vec3,Quat),to:(Vec3,Quat),tri:[Vec3;3],dirs:[Vec3;4])->Option<(f32,gjk::Closest)>{
    cast_limited(part,from,to,tri,dirs,128).ok().flatten()
}

pub(super) fn collect(group:&Group,world:&Collision,proposals:&std::collections::BTreeMap<u64,(Vec3,Vec3)>)->Vec<WorldContact>{
    collect_traced(group,world,proposals,&mut Default::default())
}
pub(super) fn collect_traced(group:&Group,world:&Collision,proposals:&std::collections::BTreeMap<u64,(Vec3,Vec3)>,audit:&mut std::collections::BTreeMap<u64,WorldStats>)->Vec<WorldContact>{
    collect_retained(group,world,proposals,audit,&[])
}
pub(super) fn collect_retained(group:&Group,world:&Collision,proposals:&std::collections::BTreeMap<u64,(Vec3,Vec3)>,audit:&mut std::collections::BTreeMap<u64,WorldStats>,retained:&[WorldContact])->Vec<WorldContact>{
    let mut found=vec![];
    for ob in group.external_world_geometry() {
        let id=ob.owner.unwrap();let owner=&group.external.owners[&id];
        let Some(&(extra,spin))=proposals.get(&id)else{continue};
        // The original animated constrained collider's floor support is not
        // yet traced. Its fourth-part sweep below admits locomotion without
        // converting the authored low foot capsule into a false upward force.
        if ob.filter.animated_ped(){continue;}
        let predictor=group.external_predictors.get(&id);
        let pending=predictor.is_some_and(|p|p.pending_position!=Vec3::ZERO||p.pending_rotation.dot(Quat::IDENTITY).abs()<1.);
        if extra==Vec3::ZERO&&spin==Vec3::ZERO&&!pending&&!retained.iter().any(|c|c.owner==id){continue;}
        let started=std::time::Instant::now();let stats=audit.entry(id).or_default();
        let extra_q=Quat::from_scaled_axis(spin);
        for (child,part) in parts(ob).iter().enumerate(){
            if matches!(part.shape,gjk::Shape::Hull(v,_)if v.is_empty()){continue;}
            let current=(ob.at+ob.rot*part.at,ob.rot*Quat::from_mat3(&part.m));
            let end=(owner.com+extra+extra_q*(current.0-owner.com),extra_q*current.1);
            let start=group.external_predictors.get(&id).map(|p|{
                let base=owner.com-p.pending_position;let inv=p.pending_rotation.inverse();
                (base+inv*(current.0-owner.com),inv*current.1)
            }).or_else(||ob.last.map(|(at,q)|(at+q*part.at,q*Quat::from_mat3(&part.m)))).unwrap_or(current);
            let radius=part.lo.abs().max(part.hi.abs()).length()+MARGIN();
            let r=Vec3::splat(radius);
            #[cfg(test)] WORLD_QUERIES.with(|n|n.set(n.get()+1));
            stats.queries+=1;
            let exact=part.shape.rotating_bounds(start,end,delirium_data::kv!(565)+MARGIN());
            let mut triangles=if super::animated_queries::early_bounds_enabled(){
                world.tris_near_from_bounded(start.0.min(end.0)-r,start.0.max(end.0)+r,start.0,exact.0,exact.1)
            }else{world.tris_near_from(start.0.min(end.0)-r,start.0.max(end.0)+r,start.0)};
            // A corrected trial pose is not an integrated frame. Do not throw
            // away its supporting inequalities merely because that trial is
            // now clear: restarting the island without them recreates the very
            // penetration they corrected. Re-evaluate the ACTUAL finite source
            // polygons throughout this step; unilateral rows become inactive
            // naturally when the unconstrained motion does not need support.
            let old:Vec<_>=retained.iter().filter(|c|c.owner==id&&c.obstacle==ob.id&&c.child==child).filter_map(|c|c.surface).collect();
            for surface in &old{if !triangles.iter().any(|q|q.4==surface.4&&q.5.first==surface.5.first){triangles.push(*surface);}}
            for source_surface in triangles{
                let(tri,_,dirs,_,instance,poly)=source_surface;
                let was=old.iter().any(|q|q.4==instance&&q.5.first==poly.first);
                if !was&&super::animated_queries::broadphase_enabled()&&(!tri[0].min(tri[1]).min(tri[2]).cmple(exact.1).all()||
                   !tri[0].max(tri[1]).max(tri[2]).cmpge(exact.0).all()){continue;}
                stats.triangles+=1;
                let Some(now)=closest_traced(part,end.0,end.1,tri,dirs,stats)else{continue};
                let crossed=cast_traced(part,start,end,tri,dirs,128,stats).ok().flatten();
                let candidate=if let Some((t,hit))=crossed.filter(|(t,_)|*t>1e-5){
                    if now.dist<0.&&now.n.dot(hit.n)>0.{
                        // Already overlapping on the SAME side: constrain the
                        // actual endpoint surface. The TOI material point can
                        // rotate away from that surface (a sphere's bottom,
                        // for example), understating real final penetration.
                        Some((now.a,now.b,now.n,now.dist))
                    }else{
                        // Retain the TOI barrier for clear-to-clear tunnelling
                        // and for a body whose nearest face switched sides.
                        let at=start.0.lerp(end.0,t);let q=start.1.slerp(end.1,t);
                        let p=end.0+end.1*(q.inverse()*(hit.a-at));
                        Some((p,hit.b,hit.n,(p-hit.b).dot(hit.n)))
                    }
                }else if now.dist<MARGIN()||was{Some((now.a,now.b,now.n,now.dist))}else{None};
                let Some((end_point,ground,n,end_gap))=candidate else{continue};
                let Some(native)=closest_traced(part,start.0,start.1,tri,dirs,stats)else{continue};
                let point=owner.com+extra_q.inverse()*(end_point-owner.com-extra);
                // Same two-sided polygon clamp as animated compound sweeps:
                // clamp in the face hemisphere, then restore the hit's side.
                let side=if n.dot(poly.n)<0.{-1.}else{1.};
                let mut surface=[(ground+n*delirium_data::kv!(565),n*side)];poly.clamp(&mut surface,delirium_data::kv!(565));
                let normal=surface[0].1*side;
                // 829C3A40 changes the normal cone but keeps the gap found by
                // narrowphase (reference ragdoll.rs:1440). Reprojecting the
                // old witnesses onto the new normal silently shrinks a deep
                // edge contact; admission still sees the real penetration.
                // Transport the measured endpoint gap to the row's current
                // linearization frame, and retain the actual native baseline.
                // The row uses translation + an angular Jacobian (rotation vector),
                // not the finite quaternion displacement of its point. Anchor
                // that SAME row to the measured gap at the proposed transform.
                // Mixing R*r-r in its offset with w cross r in the row changes
                // the constraint by O(angle^2), letting a rotating bike enter
                // the floor even when the linear rows report convergence.
                let angular=rotation_gradient(end_point-owner.com-extra,normal,spin);
                let gap=linearized_gap(end_gap,angular,normal,extra,spin);
                let native_gap=native.dist;
                if found.iter().any(|h:&WorldContact|h.owner==id&&h.child==child&&h.point.distance_squared(point)<1e-8&&h.normal.dot(normal)>0.9999){continue;}
                found.push(WorldContact{owner:id,obstacle:ob.id,child,instance,point,normal,angular,gap,native_gap,surface:Some(source_surface)});
            }
        }
        stats.micros+=started.elapsed().as_micros() as u64;
    }
    found
}

fn rejected(d:&mut ExternalPlacementDiagnostic,reason:u32,obstacle:i32,child:usize,tri:(u32,u32),base:f32,end:f32,cast:Option<CastFailure>){
    if d.reason_flags==0{
        d.obstacle=obstacle;d.child=child as u32;d.triangle_chunk=tri.0;d.triangle_index=tri.1;d.base_gap=base;d.end_gap=end;
        if let Some(c)=cast{d.cast_error=c.code;d.cast_t=c.t;d.cast_gap=c.gap;d.cast_iterations=c.iterations;}
    }
    d.reason_flags|=reason;
}
fn diagnosed(p:ExternalPlacement,mut d:ExternalPlacementDiagnostic,mut stats:WorldStats,started:std::time::Instant)->(ExternalPlacement,ExternalPlacementDiagnostic){
    stats.micros+=started.elapsed().as_micros() as u64;stats.write(&mut d);(p,d)
}
pub(super) fn placement(group:&Group,world:&Collision,id:u64,delta:Vec3,turn:Vec3,requested:Vec3,contacts:u32,mut stats:WorldStats)->(ExternalPlacement,ExternalPlacementDiagnostic){
    placement_scene(group,Some(world),None,None,id,delta,turn,requested,contacts,stats)
}
pub(super) fn placement_scene(group:&Group,world:Option<&Collision>,replay:Option<&[crate::external_mobility::ExternalReplayQuery]>,mut capture:Option<&mut Vec<crate::external_mobility::ExternalReplayQuery>>,id:u64,delta:Vec3,turn:Vec3,requested:Vec3,contacts:u32,mut stats:WorldStats)->(ExternalPlacement,ExternalPlacementDiagnostic){
    let started=std::time::Instant::now();let mut diagnostic=ExternalPlacementDiagnostic{id,..Default::default()};
    let owner=&group.external.owners[&id];
    let Some(p)=group.external_predictors.get(&id) else{diagnostic.reason_flags=1;return diagnosed(ExternalPlacement{id,admitted_position:Vec3::ZERO,admitted_rotation:Quat::IDENTITY,valid:false,world_contacts:contacts,requested_position:requested},diagnostic,stats,started);};
    let rotation=(Quat::from_scaled_axis(turn)*p.pending_rotation).normalize();
    let movement=p.pending_position+delta;
    let base=owner.com-p.pending_position;
    let geometry:Vec<_>=group.external_world_geometry().filter(|o|o.owner==Some(id)).collect();
    let animated=!geometry.is_empty()&&geometry.iter().all(|o|o.filter.animated_ped());
    // Animated admission is a separate sweep of the FINAL coupled solution.
    // Its receipt must describe that sweep's actual input, not the earlier
    // position preview: world rows and continued coupled iterations can have
    // changed it. Vehicles retain the pre-world preview for diagnostics.
    let requested=if animated{movement}else{requested};
    if movement==Vec3::ZERO&&rotation.dot(Quat::IDENTITY).abs()>=1.{
        // No motion is being added to the native accepted baseline. It is not
        // our job to re-solve every car's already completed environment step.
        if geometry.is_empty(){diagnostic.reason_flags=2;}
        return diagnosed(ExternalPlacement{id,admitted_position:movement,admitted_rotation:rotation,valid:!geometry.is_empty(),world_contacts:0,requested_position:requested},diagnostic,stats,started);
    }
    if animated{
        // Preserve native ground support, and reuse the tested compound
        // translational admission (including t=0 floor tangent/edge logic).
        // Original axisymmetric upright parts are invariant under native yaw.
        let mut shapes=vec![];
        for ob in &geometry {for (child,part) in parts(ob).into_iter().enumerate(){
            // Claimed ordinary owner: source callback consumes child1 as
            // support and disables child3 against fixed world. Neither may
            // become a second rigid floor through placement admission.
            if group.ground_claimed(id)&&(child==1||child==3){continue;}
            let at=ob.at+ob.rot*part.at;let q=ob.rot*Quat::from_mat3(&part.m);
            let inv=p.pending_rotation.inverse();let from=base+inv*(at-owner.com);
            shapes.push((part.shape,from,Mat3::from_quat(inv*q),(part.hi-part.lo)*0.5));
        }}
        let (admitted,hits)=Ragdoll::sweep_shapes_translation(&shapes,world.expect("animated replay explicitly rejected"),&[],movement);
        let upright=(rotation*Vec3::Z).distance_squared(Vec3::Z)<1e-8;
        if !upright{diagnostic.reason_flags|=128;}if shapes.is_empty(){diagnostic.reason_flags|=2;}
        // Existing compound sweep does not expose its internal query counts;
        // mark them UNKNOWN instead of reporting a fabricated zero.
        stats.queries=u32::MAX;stats.triangles=u32::MAX;stats.gjk=u32::MAX;
        return diagnosed(ExternalPlacement{id,admitted_position:admitted,admitted_rotation:rotation,valid:upright&&!shapes.is_empty(),world_contacts:hits as u32,requested_position:requested},diagnostic,stats,started);
    }
    let mut valid=movement.is_finite()&&rotation.is_finite();let mut shapes=0;
    if !valid{diagnostic.reason_flags|=4;}
    for ob in group.external_world_geometry().filter(|o|o.owner==Some(id)){
        for(child,part)in parts(ob).into_iter().enumerate(){
            if matches!(part.shape,gjk::Shape::Hull(v,_)if v.is_empty()){continue;}shapes+=1;
            let current=(ob.at+ob.rot*part.at,ob.rot*Quat::from_mat3(&part.m));
            let inv=p.pending_rotation.inverse();let start=(base+inv*(current.0-owner.com),inv*current.1);
            let end=(base+movement+rotation*(start.0-base),rotation*start.1);
            let radius=part.lo.abs().max(part.hi.abs()).length()+MARGIN();let r=Vec3::splat(radius);
            #[cfg(test)] WORLD_QUERIES.with(|n|n.set(n.get()+1));
            stats.queries+=1;
            let lo=start.0.min(end.0)-r;let hi=start.0.max(end.0)+r;
            let exact=part.shape.rotating_bounds(start,end,delirium_data::kv!(565)+MARGIN());
            let triangles=if let Some(world)=world{
                if super::animated_queries::early_bounds_enabled(){world.tris_near_from_bounded(lo,hi,start.0,exact.0,exact.1)}else{world.tris_near_from(lo,hi,start.0)}
            }else{
                let Some(query)=replay.and_then(|qs|qs.iter().find(|q|q.obstacle==ob.id&&q.child==child&&q.lo==lo&&q.hi==hi))else{
                    valid=false;rejected(&mut diagnostic,256,ob.id,child,(0,0),f32::NAN,f32::NAN,None);continue
                };query.triangles.clone()
            };
            for &(tri,_,dirs,_,_,poly) in &triangles{
                if super::animated_queries::broadphase_enabled()&&(!tri[0].min(tri[1]).min(tri[2]).cmple(exact.1).all()||
                   !tri[0].max(tri[1]).max(tri[2]).cmpge(exact.0).all()){continue;}
                stats.triangles+=1;
                let(a,b)=(closest_traced(&part,start.0,start.1,tri,dirs,&mut stats),closest_traced(&part,end.0,end.1,tri,dirs,&mut stats));
                let(Some(a),Some(b))=(a,b)else{valid=false;rejected(&mut diagnostic,8,ob.id,child,poly.first,f32::NAN,f32::NAN,None);continue};
                // Existing contact may separate or move tangentially. Never
                // bless a deeper endpoint, or a clear-to-clear tunnel.
                if b.dist<(-ALLOW()).min(a.dist)-2e-4{valid=false;rejected(&mut diagnostic,16,ob.id,child,poly.first,a.dist,b.dist,None);}
                if a.dist>1e-5{match cast_traced(&part,start,end,tri,dirs,128,&mut stats){
                    Err(e)=>{valid=false;rejected(&mut diagnostic,64,ob.id,child,poly.first,a.dist,b.dist,Some(e));},
                    Ok(Some((t,c))) if b.dist>ALLOW()=>{valid=false;rejected(&mut diagnostic,32,ob.id,child,poly.first,a.dist,b.dist,Some(CastFailure{code:0,t,gap:c.dist,iterations:0}));},
                    _=>{}
                }}
            }
            if let Some(out)=capture.as_deref_mut(){out.push(crate::external_mobility::ExternalReplayQuery{obstacle:ob.id,child,lo,hi,triangles});}
        }
    }
    if shapes==0{diagnostic.reason_flags|=2;}
    diagnosed(ExternalPlacement{id,admitted_position:movement,admitted_rotation:rotation,valid:valid&&shapes>0,world_contacts:contacts,requested_position:requested},diagnostic,stats,started)
}

#[cfg(test)]
mod tests{
    use super::*;
    use crate::external_mobility::ExternalPredictor;
    fn setup(at:Vec3,pending:Vec3,velocity:Vec3,half:Vec3)->Group{
        super::super::moving_obstacle_regression::constants();
        let owner=ExternalOwner{id:9,epoch:42,com:at,velocity,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::ZERO};
        let mut group=Group::default();group.stage_external(42,&[owner]).unwrap();
        group.stage_external_predictors(42,&[ExternalPredictor{id:9,pending_position:pending,pending_rotation:Quat::IDENTITY}]).unwrap();
        group.stage_external_geometry(&[Obstacle{id:1,owner:Some(9),at,centre:at,com:at,half,vel:velocity,..Default::default()}]).unwrap();group
    }
    fn wall(x:f32)->Collision{
        let mut world=Collision::default();
        world.add(&[[x,-4.,-4.],[x,-4.,4.],[x,4.,4.],[x,4.,-4.]],&[[0,1,2],[0,2,3]],&[0,0]);world
    }
    #[test]fn animated_admission_receipt_describes_final_solver_input_not_earlier_preview(){
        let mut group=setup(Vec3::ZERO,Vec3::ZERO,Vec3::ZERO,Vec3::splat(0.3));
        group.external_geometry[0].filter=super::super::CollisionFilter{kind:0x20,include:0x79FE};
        // Position iterations changed the shared island after its initial
        // preview. The native host must verify the exact input to this sweep,
        // which can differ both from that preview and the clipped output.
        let world=wall(0.35);let solved=Vec3::X*0.1;let preview=Vec3::X*0.2;
        let(p,_)=placement_scene(&group,Some(&world),None,None,9,solved,Vec3::ZERO,preview,0,Default::default());
        assert!(p.valid);assert!(p.admitted_position.x<solved.x,"wall must clip final input");
        assert_eq!(p.requested_position,solved,"receipt must match results18 before the sweep");
        let(zero,_)=placement_scene(&group,Some(&world),None,None,9,Vec3::ZERO,Vec3::ZERO,preview,0,Default::default());
        assert_eq!(zero.requested_position,Vec3::ZERO,"zero final input also supersedes stale preview");
    }
    #[test]fn position_preview_reuse_preserves_finite_owner_and_contact_admission_bits(){
        fn run(reuse:bool,with_wall:bool)->(Vec<u32>,u32){
            super::super::POSITION_PREVIEW_REUSE.with(|v|v.set(reuse));
            let h=1./60.;let pending=Vec3::X*0.05;
            let mut group=setup(pending,pending,Vec3::ZERO,Vec3::splat(0.3));
            let world=if with_wall{wall(0.32)}else{Collision::default()};
            group.stage_external_drives(42,&[crate::external_mobility::ExternalDrive{id:9,impulse:Vec3::X*(0.05*300./h),torque:Vec3::ZERO}]).unwrap();
            Ragdoll::step_group(&mut[],&world,h,&mut group);assert!(group.external_error().is_none());
            let result=group.external_results()[0];let placement=group.external_placements()[0];assert!(placement.valid);
            let mut bits=vec![];
            for v in [result.impulse,result.angular_impulse,result.delta_velocity,result.delta_spin,result.position_correction,result.orientation_correction,placement.admitted_position,placement.requested_position]{bits.extend(v.to_array().map(f32::to_bits));}
            bits.extend(placement.admitted_rotation.to_array().map(f32::to_bits));
            (bits,placement.world_contacts)
        }
        for with_wall in [false,true]{
            let old=run(false,with_wall);let new=run(true,with_wall);assert_eq!(old,new,"wall={with_wall}");
            assert_eq!(new.1>0,with_wall,"must exercise both empty and contact-bearing world queries");
        }
        super::super::POSITION_PREVIEW_REUSE.with(|v|v.set(true));
    }
    #[test]fn authored_edge_cone_keeps_geometric_penetration_depth(){
        super::super::moving_obstacle_regression::constants();
        // Live130024: native COL sphere25 touches the edge of world triangle
        // 5800. Its geometric normal is far from the adjacent face cone.
        // Rotating the reaction normal must not halve its measured depth.
        let at=Vec3::new(2415.8318,-1663.3046,13.450975);
        let tri=[[2415.9766,-1664.0078,13.8232155],[2416.0837,-1664.0078,13.801901],[2415.9766,-1663.9006,13.801901]].map(Vec3::from);
        let mut world=Collision::default();world.add(&tri.map(|v|v.to_array()),&[[0,1,2]],&[53]);
        let part=ObPart{shape:gjk::Shape::Capsule(0.,0.72),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::splat(-0.72),hi:Vec3::splat(0.72),mesh:None,filter:Default::default()};
        let mut group=setup(at,Vec3::ZERO,Vec3::ZERO,Vec3::splat(0.72));
        group.external_geometry[0].parts=Box::leak(vec![part].into_boxed_slice());
        let extra=Vec3::Z*0.001;
        let contacts=collect(&group,&world,&[(9,(extra,Vec3::ZERO))].into_iter().collect());
        assert_eq!(contacts.len(),1);
        let c=contacts[0];
        // Closest point is the last vertex; this distance oracle is independent
        // of the row generator and the GJK penetration fallback.
        let baseline=at.distance(tri[2])-0.72-delirium_data::kv!(565);
        assert!(baseline < -0.017);
        assert!((c.native_gap-baseline).abs()<2e-5,"normal clamp must retain baseline depth: actual={} geometric={baseline}",c.native_gap);
        let endpoint=(at+extra).distance(tri[2])-0.72-delirium_data::kv!(565);
        let transported=c.gap+extra.dot(c.normal);
        assert!((transported-endpoint).abs()<2e-5,"linearization keeps the exact candidate depth: actual={transported} geometric={endpoint}");
        let h=1./60.;
        // This test exercises an ACTUAL pending placement. A newly solved
        // impulse is future momentum, not a way to move a post-step snapshot.
        group=setup(at+extra,extra,Vec3::ZERO,Vec3::splat(0.72));
        group.external_geometry[0].parts=Box::leak(vec![part].into_boxed_slice());
        Ragdoll::step_group(&mut[],&world,h,&mut group);
        let accepted=group.external_placements()[0];
        assert!(accepted.valid&&accepted.world_contacts>0,"the shared solve must satisfy its unchanged geometric admission: {accepted:?} {:?}",group.external_placement_diagnostics());
        let final_gap=(at+accepted.admitted_position).distance(tri[2])-0.72-delirium_data::kv!(565);
        assert!(final_gap>=baseline-2e-4,"actual source sphere must not sink further into the vertex");
    }
    #[test]fn captured_separated_wheel_face_cannot_become_a_penetration(){
        super::super::moving_obstacle_regression::constants();
        // Exact f32 witness: live123436, owner4294972161, child89 / road9261.
        let v=[[0.000019358662,-0.28329659,-0.16347742],[-0.053633075,-0.29421568,-0.07878322],[-0.053623967,-0.26576903,-0.1533571]].map(Vec3::from);
        let part=ObPart{shape:gjk::Shape::Tri(v,0.002),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::new(-0.053633075,-0.29421568,-0.16347742),hi:Vec3::new(0.000019358662,-0.26576903,-0.07878322),mesh:None,filter:Default::default()};
        let at=Vec3::new(2439.2278,-1635.1477,12.706768);let q=Quat::from_xyzw(-0.35848522,0.38284203,0.5910135,-0.6128812);
        let tri=[[2431.7734,-1631.6172,12.3984],[2439.711,-1648.9062,12.499963],[2439.711,-1633.8438,12.406213]].map(Vec3::from);
        let dirs=[[0.0007616572,0.006223944,0.99998033],[0.0007616572,0.006223944,0.99998033],[0.0007616572,0.006223944,0.99998033],[0.00087245845,0.006618932,0.9999777]].map(Vec3::from);
        let m=Mat3::from_quat(q);let side=if dirs[0].dot(at+q*((part.lo+part.hi)*0.5)-tri[0])<0.{-1.}else{1.};let n=dirs[0]*side;
        let separated=n.dot(at-tri[0]+m*part.shape.support(m.transpose()*-n))-0.002-delirium_data::kv!(565);
        assert!(separated>0.001,"independent separating plane proves disjoint margins: {separated}");
        let hit=closest(&part,at,q,tri,dirs).unwrap();
        assert!(hit.dist>0.001,"a separated wheel face must not become a negative-gap force: {hit:?}");
        assert!((hit.dist-separated).abs()<1e-5);
    }
    #[test]fn native_environment_override_keeps_peer_geometry_and_native_baseline_separate(){
        let mut group=setup(Vec3::Z*0.1,-Vec3::Z*0.2,Vec3::ZERO,Vec3::splat(2.));
        let environment=Obstacle{id:91,owner:Some(9),at:Vec3::Z*0.1,centre:Vec3::Z*0.1,com:Vec3::Z*0.1,half:Vec3::splat(0.25),..Default::default()};
        group.stage_external_environment_geometry(42,&[environment]).unwrap();
        assert_eq!(group.external_geometry[0].half,Vec3::splat(2.),"peer bound must not become environment COL");
        let mut world=Collision::default();world.add_floor(0.,0.,0.,4.,0);
        Ragdoll::step_group(&mut[],&world,1./60.,&mut group);
        let p=group.external_placements()[0];assert!(p.valid&&p.world_contacts>0,"{p:?}");
        assert!(p.admitted_position.z< -0.025&&p.admitted_position.z> -0.075,"environment floor support uses the .25 native envelope: {p:?}");
        let replay=group.external_placement_replays(&world);assert!(replay[0].environment_override);assert_eq!(replay[0].geometry[0].id,91);
        assert_eq!(group.external_geometry[0].half,Vec3::splat(2.));
        let owner=group.external.owners[&9];group.stage_external(43,&[ExternalOwner{epoch:43,..owner}]).unwrap();
        assert!(group.external_environment_geometry.is_empty());
    }
    #[test]fn native_environment_stage_rejects_stale_missing_duplicate_and_nonfinite_atomically(){
        use crate::external_mobility::ExternalError as E;
        let mut group=setup(Vec3::Z*2.,Vec3::ZERO,Vec3::ZERO,Vec3::splat(0.25));let ob=group.external_geometry[0];
        group.stage_external_environment_geometry(42,&[ob]).unwrap();
        assert_eq!(group.stage_external_environment_geometry(41,&[ob]),Err(E::WrongEpoch(41)));assert!(group.external_environment_geometry.is_empty());
        assert_eq!(group.stage_external_environment_geometry(42,&[ob,ob]),Err(E::DuplicateEnvironment(ob.id)));
        assert_eq!(group.stage_external_environment_geometry(42,&[Obstacle{owner:Some(123),..ob}]),Err(E::MissingSnapshot(123)));
        assert_eq!(group.stage_external_environment_geometry(42,&[Obstacle{at:Vec3::NAN,..ob}]),Err(E::InvalidEnvironment(9)));
        assert_eq!(group.external_geometry[0].at,ob.at);
        Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut group);
        assert_eq!(group.external_error(),Some(E::InvalidEnvironment(9)));assert!(group.external_results().is_empty(),"invalid environment cannot silently fall back to peer/render bounds");
        group.stage_external_environment_geometry(42,&[]).unwrap();Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut group);assert!(group.external_error().is_none());
    }
    #[test]fn environment_override_does_not_shrink_actual_owner_peer_contacts(){
        use super::super::moving_obstacle_regression::{original_ped_obstacle,original_ped_owner};
        super::super::moving_obstacle_regression::constants();
        let car=ExternalOwner{id:1,epoch:7,com:Vec3::Z,velocity:Vec3::X,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::ZERO};
        let ped=original_ped_owner(2,7,Vec3::new(0.9,0.,1.),Vec3::ZERO);
        let peer=Obstacle{id:1,owner:Some(1),at:car.com,centre:car.com,com:car.com,vel:car.velocity,half:Vec3::splat(0.75),..Default::default()};
        let mut g=Group::default();g.stage_external(7,&[car,ped]).unwrap();g.stage_external_geometry(&[peer,original_ped_obstacle(2,ped.com,ped.velocity)]).unwrap();
        g.stage_external_environment_geometry(7,&[Obstacle{id:88,half:Vec3::splat(0.1),..peer}]).unwrap();
        assert_eq!(g.external_world_geometry().count(),2,"unoverridden ped retains its source geometry");
        Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut g);
        assert!(!g.external_contacts().is_empty(),"DFF/peer envelope still generates real contacts");
        assert!(g.external_results().iter().any(|r|r.id==2&&r.impulse.length()>0.));
    }
    #[test]fn diagnostic_replay_preserves_actual_geometry_and_rejection_witness(){
        super::super::moving_obstacle_regression::constants();
        let world=wall(1.);let mut group=setup(Vec3::ZERO,Vec3::ZERO,Vec3::X*4.,Vec3::splat(0.25));
        let owner=group.external.owners[&9];
        for (delta,expected) in [(Vec3::X*0.9,16),(Vec3::X*2.,32)]{
            let(p,d)=placement(&group,&world,9,delta,Vec3::ZERO,delta,0,Default::default());
            assert!(!p.valid&&d.reason_flags&expected!=0,"{p:?} {d:?}");
            assert!(d.triangle_count>0&&d.gjk_count>0&&d.query_count==1);
            group.external_replay_steps=vec![(owner,1./60.,delta,Vec3::ZERO,delta,0)];
            group.external.owners.get_mut(&9).unwrap().velocity=Vec3::Y*27.;
            let captured=group.external_placement_replays(&world);let mut replay=captured[0].clone();
            assert_eq!(replay.owner.velocity,owner.velocity,"preserve pre-commit owner, not mutated registry");
            assert_eq!(replay.queries[0].triangles.len(),2,"exact source triangles, not replacement hull/floor");
            let(a,b)=Group::replay_external_placement(&replay).unwrap();
            assert_eq!(a.valid,p.valid);assert_eq!(b.reason_flags,d.reason_flags);assert_eq!(b.obstacle,d.obstacle);
            assert_eq!(b.base_gap.to_bits(),d.base_gap.to_bits());assert_eq!(b.end_gap.to_bits(),d.end_gap.to_bits());
            assert_eq!(b.gjk_count,d.gjk_count);assert_eq!(b.cast_count,d.cast_count);
            replay.queries.clear();let(_,missing)=Group::replay_external_placement(&replay).unwrap();
            assert_eq!(missing.reason_flags,256,"missing scene is not an empty world approval");
        }
        group.stage_external(43,&[ExternalOwner{epoch:43,..owner}]).unwrap();
        assert!(group.external_placement_replays(&world).is_empty(),"new interval cannot reuse old replay");
    }
    #[test]fn vehicle_world_pending_sweep_stops_clear_to_clear_tunnel(){
        let mut group=setup(Vec3::X*2.,Vec3::X*2.,Vec3::X*4.,Vec3::splat(0.25));
        Ragdoll::step_group(&mut[],&wall(1.),1./60.,&mut group);
        let p=group.external_placements()[0];let r=group.external_results()[0];
        assert!(p.valid,"{p:?}");assert!(p.world_contacts>0);
        assert!(p.admitted_position.x<0.76&&p.admitted_position.x>0.70,"{p:?}");
        assert!((p.requested_position.x-2.).abs()<1e-5,"request is pending trajectory; current collision impulse must not rewrite its history: {p:?} {r:?}");
        assert!((p.admitted_position.x-(2.+r.position_correction.x)).abs()<1e-5,"pending plus actual split must match admitted trajectory");
        assert!(r.delta_velocity.x< -3.9&&r.delta_velocity.z.abs()<1e-4,"{r:?}");
    }
    #[test]fn world_constraint_does_not_repeat_native_support_or_free_space_damping(){
        super::super::moving_obstacle_regression::constants();
        let mut world=Collision::default();world.add_floor(0.,0.,0.,8.,0);
        let mut clear=setup(Vec3::Z*2.,Vec3::ZERO,Vec3::X*3.,Vec3::splat(0.25));
        Ragdoll::step_group(&mut[],&world,1./90.,&mut clear);
        let r=clear.external_results()[0];let p=clear.external_placements()[0];
        assert_eq!(r.impulse,Vec3::ZERO);assert_eq!(r.delta_spin,Vec3::ZERO);
        assert_eq!(p.admitted_position,Vec3::ZERO);assert!(p.valid);assert_eq!(p.world_contacts,0);
        // The native engine owns this existing overlap/downward velocity.
        // With no shared impulse or pending correction we must not solve it
        // again, lift the car, or manufacture another suspension response.
        let mut sinking=setup(Vec3::Z*0.23,Vec3::ZERO,-Vec3::Z*3.,Vec3::splat(0.25));
        Ragdoll::step_group(&mut[],&world,1./90.,&mut sinking);
        let r=sinking.external_results()[0];let p=sinking.external_placements()[0];
        assert_eq!(r.impulse,Vec3::ZERO,"do not duplicate native accepted support");
        assert!(p.valid&&p.world_contacts==0,"{p:?}");
        assert_eq!(p.admitted_position,Vec3::ZERO);assert_eq!(r.position_correction,Vec3::ZERO);
    }
    #[test]fn corrected_clear_trial_retains_source_floor_until_the_island_is_committed(){
        let mut group=setup(Vec3::Z*0.4,Vec3::ZERO,Vec3::ZERO,Vec3::splat(0.25));
        let mut geometry=group.external_geometry.clone();
        geometry[0].parts=Box::leak(vec![ObPart{shape:gjk::Shape::Capsule(0.,0.25),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::splat(-0.25),hi:Vec3::splat(0.25),mesh:None,filter:Default::default()}].into_boxed_slice());
        group.stage_external_geometry(&geometry).unwrap();
        let mut world=Collision::default();world.add_floor(0.,0.,0.,8.,0);
        let id=geometry[0].owner.unwrap();
        let proposals=|delta|std::collections::BTreeMap::from([(id,(delta,Vec3::ZERO))]);
        let found=collect(&group,&world,&proposals(-Vec3::Z*0.3));assert!(!found.is_empty());
        let clear=proposals(Vec3::Z);assert!(collect(&group,&world,&clear).is_empty(),"must reproduce contact disappearing after correction");
        let retained=collect_retained(&group,&world,&clear,&mut Default::default(),&found);assert!(!retained.is_empty());
        let owner=&group.external.owners[&id];let mut rows:Vec<_>=retained.iter().map(|c|c.row(owner,1.,true)).collect();
        let mut qp=vec![0.,0.,-0.3,0.,0.,0.];impulses(&mut rows,&mut[],&mut qp);
        let delta=Vec3::new(qp[0]as f32,qp[1]as f32,qp[2]as f32);
        let(p,d)=placement(&group,&world,id,delta,Vec3::ZERO,-Vec3::Z*0.3,rows.len()as u32,Default::default());
        assert!(p.valid,"retained finite source surface must prevent re-penetration without a looser guard: {d:?}");
        assert!(delta.z> -0.16&&delta.z< -0.15);
    }
    #[test]fn predictor_epoch_missing_owner_and_zero_shape_never_reuse_admission(){
        let mut group=setup(Vec3::Z*2.,Vec3::ZERO,Vec3::ZERO,Vec3::splat(0.25));
        assert!(group.stage_external_predictors(41,&[]).is_err());assert!(group.external_predictors.is_empty());
        assert!(group.stage_external_predictors(42,&[]).is_err());assert!(group.external_placements().is_empty());
        group.stage_external_predictors(42,&[ExternalPredictor{id:9,pending_position:Vec3::ZERO,pending_rotation:Quat::IDENTITY}]).unwrap();
        group.stage_external_geometry(&[]).unwrap();Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut group);
        assert!(!group.external_placements()[0].valid);
    }
    #[test]fn rotating_component_sweep_catches_intermediate_obstruction(){
        super::super::moving_obstacle_regression::constants();
        let part=ObPart{shape:gjk::Shape::Box(Vec3::new(1.,0.05,0.05),0.),at:Vec3::ZERO,m:Mat3::IDENTITY,lo:Vec3::new(-1.,-0.05,-0.05),hi:Vec3::new(1.,0.05,0.05),mesh:None,filter:Default::default()};
        let tri=[Vec3::new(0.7,0.7,-1.),Vec3::new(0.7,0.7,1.),Vec3::new(0.7,1.7,1.)];
        let n=(tri[1]-tri[0]).cross(tri[2]-tri[0]).normalize();let dirs=[n;4];
        let a=(Vec3::ZERO,Quat::IDENTITY);let b=(Vec3::ZERO,Quat::from_rotation_z(std::f32::consts::FRAC_PI_2));
        assert!(closest(&part,a.0,a.1,tri,dirs).unwrap().dist>0.);
        assert!(closest(&part,b.0,b.1,tri,dirs).unwrap().dist>0.);
        assert!(cast(&part,a,b,tri,dirs).is_some(),"rotation between clear endpoints must be swept");
        assert!(cast_limited(&part,a,b,tri,dirs,1).is_err(),"an exhausted geometric budget is indeterminate, never certified clear");
    }
    #[test]fn animated_owner_keeps_native_support_and_sweeps_total_pending_motion(){
        use super::super::moving_obstacle_regression::{constants,original_ped_obstacle,original_ped_owner};
        constants();let at=Vec3::new(0.30,0.,1.);let mut world=wall(0.4);world.add_floor(0.,0.,0.,6.,0);
        let owner=original_ped_owner(7,42,at,Vec3::X);let mut group=Group::default();
        group.stage_external(42,&[owner]).unwrap();group.stage_external_geometry(&[original_ped_obstacle(7,at,Vec3::X)]).unwrap();
        group.stage_external_predictors(42,&[ExternalPredictor{id:7,pending_position:Vec3::X*0.3,pending_rotation:Quat::IDENTITY}]).unwrap();
        Ragdoll::step_group(&mut[],&world,1./120.,&mut group);
        let p=group.external_placements()[0];let r=group.external_results()[0];
        assert!(p.valid&&p.world_contacts>0,"{p:?}");
        assert!(p.admitted_position.x>0.1&&p.admitted_position.x<0.16,"{p:?}");
        assert_eq!(p.admitted_position.z,0.);assert_eq!(r.impulse,Vec3::ZERO,"native foot support is not another upward rigid-body impulse");
    }
    #[test]fn nm_impact_on_car_at_wall_reacts_through_same_island(){
        use super::super::moving_obstacle_regression::finite_probe_build;
        let build=finite_probe_build(80.);let at=Vec3::Z*2.;let h=1./60.;
        let mut group=setup(at,Vec3::ZERO,Vec3::ZERO,Vec3::new(0.5,0.5,0.5));
        let world=wall(0.5);
        let proposal=std::collections::BTreeMap::from([(9,(Vec3::X*0.1,Vec3::ZERO))]);
        let allowed=collect(&group,&world,&proposal).iter().filter(|c|c.normal.x< -0.99)
            .map(|c|-c.row(&group.external.owners[&9],h,false).target as f32).fold(f32::INFINITY,f32::min);
        let obstacles=group.external_geometry.clone();
        let mut doll=Ragdoll::new(&build,&[Transform::from_translation(at-Vec3::X*0.65)],Vec3::X*4.5);
        Ragdoll::step_group(&mut[Member{doll:&mut doll,id:100,build:&build,drive:Drive{lift:1.,..Default::default()},obstacles:&obstacles,others:&[]}],&world,h,&mut group);
        let p=group.external_placements()[0];let r=group.external_results()[0];
        assert!(p.valid&&p.world_contacts>0,"{p:?}");assert!(p.admitted_position.x<=0.002+1e-6,"{p:?}");
        assert!(allowed.is_finite()&&r.delta_velocity.x<=allowed+1e-5,"world normal constraint {allowed} must hold AFTER actual impulses: {r:?}");
        assert!(doll.motion(0).0.x<1.,"reaction must reach original impacting body: {:?}",doll.motion(0));
    }
    #[test]fn vehicle_world_response_is_independent_of_triangle_winding(){
        for reversed in [false,true]{for direction in [1f32,-1.]{
            let mut group=setup(Vec3::X*(direction*2.),Vec3::X*(direction*2.),Vec3::X*(direction*4.),Vec3::splat(0.25));
            let mut world=Collision::default();let x=direction;
            let tris=if reversed{[[0,2,1],[0,3,2]]}else{[[0,1,2],[0,2,3]]};
            world.add(&[[x,-4.,-4.],[x,-4.,4.],[x,4.,4.],[x,4.,-4.]],&tris,&[0,0]);
            Ragdoll::step_group(&mut[],&world,1./60.,&mut group);
            let p=group.external_placements()[0];let r=group.external_results()[0];
            assert!(p.valid&&p.world_contacts>0,"reversed={reversed} direction={direction}: {p:?}");
            assert!(p.admitted_position.x.abs()<0.76&&p.admitted_position.x.abs()>0.70,"{p:?}");
            assert!(direction*r.impulse.x<0.,"reciprocal reaction missing from back face: {r:?}");
        }}
    }
    #[test]fn unrelated_native_vehicle_fleet_has_no_world_resolve_or_scan(){
        super::super::moving_obstacle_regression::constants();
        let mut world=Collision::default();world.add_floor(0.,0.,0.,40.,0);
        let owners:Vec<_>=(1..=48).map(|id|ExternalOwner{id,epoch:7,com:Vec3::new(id as f32*1.5-36.,0.,0.23),
            velocity:Vec3::new(4.,0.,-3.),spin:Vec3::Y*0.4,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::IDENTITY/500.}).collect();
        let geometry:Vec<_>=owners.iter().map(|o|Obstacle{id:o.id as i32,owner:Some(o.id),at:o.com,centre:o.com,com:o.com,half:Vec3::splat(0.25),vel:o.velocity,spin:o.spin,..Default::default()}).collect();
        let predictors:Vec<_>=owners.iter().map(|o|ExternalPredictor{id:o.id,pending_position:Vec3::ZERO,pending_rotation:Quat::IDENTITY}).collect();
        let mut group=Group::default();group.stage_external(7,&owners).unwrap();group.stage_external_geometry(&geometry).unwrap();group.stage_external_predictors(7,&predictors).unwrap();
        WORLD_QUERIES.with(|n|n.set(0));Ragdoll::step_group(&mut[],&world,1./120.,&mut group);
        assert_eq!(WORLD_QUERIES.with(|n|n.get()),0,"native-only vehicles must not each rescan the environment for every other island");
        assert_eq!(group.external_placements().len(),48);
        assert!(group.external_placements().iter().all(|p|p.valid&&p.admitted_position==Vec3::ZERO&&p.world_contacts==0));
        assert!(group.external_results().iter().all(|r|r.impulse==Vec3::ZERO&&r.angular_impulse==Vec3::ZERO&&r.position_correction==Vec3::ZERO));
    }
}

#[cfg(test)]
mod linearization_tests {
 use super::*;
 #[test]fn rotating_sphere_world_row_uses_actual_endpoint_surface_not_advected_impact_material(){
  super::super::moving_obstacle_regression::constants();
  let com=Vec3::new(0.,0.,0.39);let radius=0.35;let angle=0.27f32;
  let owner=ExternalOwner{id:7,epoch:1,com,velocity:Vec3::ZERO,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/500.,angular_inverse:Mat3::IDENTITY/160.};
  let part=ObPart{shape:gjk::Shape::Capsule(0.,radius),m:Mat3::IDENTITY,at:Vec3::Y,
   lo:Vec3::splat(-radius),hi:Vec3::splat(radius),mesh:None,filter:Default::default()};
  let mut group=Group::default();group.stage_external(1,&[owner]).unwrap();
  group.stage_external_geometry(&[Obstacle{id:2,owner:Some(7),at:com,centre:com,com,half:Vec3::splat(2.),parts:Box::leak(vec![part].into_boxed_slice()),..Default::default()}]).unwrap();
  let mut world=Collision::default();world.add_floor(0.,0.,0.,10.,0);
  let spin=Vec3::X*-angle;let measured=-0.0069;
  let extra=Vec3::Z*(radius+delirium_data::kv!(565)+measured-com.z+angle.sin());
  let contacts=collect(&group,&world,&[(7,(extra,spin))].into_iter().collect());assert!(!contacts.is_empty());
  let exact=(com+extra+Quat::from_scaled_axis(spin)*Vec3::Y).z-radius-delirium_data::kv!(565);
  let mut deepest=f32::INFINITY;
  for c in contacts{
   let transported=c.gap+c.normal.dot(extra)+c.angular.dot(spin);
   println!("rotating sphere: exact endpoint gap={exact}, row gap={transported}");
   // The adjoining triangle may touch a sphere at its EDGE rather than the
   // plane's interior. It is shallower, never deeper; the floor union's
   // deepest contact must still equal the independent infinite-plane oracle.
   assert!(transported>=exact-3e-5);deepest=deepest.min(transported);
  }
  assert!((deepest-exact).abs()<3e-5,"the impact's material point rotates UP the sphere; its bottom must still constrain the real endpoint");
 }
 #[test]fn tangential_sphere_cast_reaches_analytic_plane_without_rotation_or_sideways_iteration_cost(){
  super::super::moving_obstacle_regression::constants();
  let part=ObPart{shape:gjk::Shape::Capsule(0.,0.35),m:Mat3::IDENTITY,at:Vec3::ZERO,
   lo:Vec3::splat(-0.35),hi:Vec3::splat(0.35),mesh:None,filter:Default::default()};
  let tri=[Vec3::new(0.,-40.,-40.),Vec3::new(0.,40.,-40.),Vec3::new(0.,0.,40.)];let dirs=[Vec3::X;4];
  let mut old_failed=0;
  for k in 0..24{
   let from=(Vec3::new(0.36,-1.,0.),Quat::IDENTITY);
   let to=(Vec3::new(0.34,1.+k as f32*0.2,0.),Quat::from_rotation_y(0.28));
   MAGNITUDE_CAST_BASELINE.with(|v|v.set(true));
   old_failed+=usize::from(cast_limited(&part,from,to,tri,dirs,128).is_err());
   MAGNITUDE_CAST_BASELINE.with(|v|v.set(false));
   let mut stats=WorldStats::default();let (t,hit)=cast_traced(&part,from,to,tri,dirs,128,&mut stats).unwrap().expect("actual plane crossing");
   let analytic=(from.0.x-0.35-delirium_data::kv!(565))/(from.0.x-to.0.x);
   assert!((t-analytic).abs()<0.002,"case{k}: t={t}, analytic={analytic}");
   assert!(hit.dist<=1e-5&&stats.iterations<12,"sphere rotation and sideways travel must not exhaust the sweep: {} {}",hit.dist,stats.iterations);
   let rotated_from=(from.0,Quat::from_rotation_x(-0.9));
   let still=(to.0,rotated_from.1);
   let (same,_)=cast_limited(&part,rotated_from,still,tri,dirs,128).unwrap().unwrap();
   assert!((same-t).abs()<1e-4,"a sphere's sweep is independent of orientation");
  }
  assert!(old_failed>12,"must reproduce exhausted original advances, got {old_failed}");
  let away=(Vec3::new(0.37,8.,0.),Quat::from_rotation_y(0.8));
  assert!(cast_limited(&part,(Vec3::new(0.36,0.,0.),Quat::IDENTITY),away,tri,dirs,128).unwrap().is_none());
 }
 #[test]fn rotation_vector_contact_derivative_matches_independent_finite_differences(){
  use bevy_math::{DQuat,DVec3};
  super::super::moving_obstacle_regression::constants();
  println!("world ALLOW={} margin={} sweeps={}",ALLOW(),MARGIN(),SWEEPS());
  let mut incorrect_zero_origin=0;
  for k in 0..2000{
   let t=k as f64*0.017;let r=DVec3::new(0.865,t.sin()*0.28,-0.637);
   let n=DVec3::new(t.sin()*0.01,0.006225597,1.).normalize();
   let w=if k==0{DVec3::ZERO}else{DVec3::new(t.sin()*0.4,-0.019,0.043)};
   let arm=DQuat::from_scaled_axis(w)*r;
   let actual=rotation_gradient(arm.as_vec3(),n.as_vec3(),w.as_vec3()).as_dvec3();
   let h=1e-5;let numerical=DVec3::from_array(std::array::from_fn(|i|{
    let mut before=w;let mut after=w;before[i]-=h;after[i]+=h;
    ((DQuat::from_scaled_axis(after)*r).dot(n)-(DQuat::from_scaled_axis(before)*r).dot(n))/(2.*h)
   }));
   assert!((actual-numerical).length()<2e-6,"case{k} derivative={actual:?} independent={numerical:?}");
   incorrect_zero_origin+=usize::from((r.cross(n)-numerical).length()>0.01);
  }
  assert!(incorrect_zero_origin>1000,"must reproduce the incorrect nonzero-proposal gradient");
 }
 #[test]
 fn rotating_contact_row_reconstructs_measured_gap_at_its_linearization_point(){
  super::super::moving_obstacle_regression::constants();
  let owner=ExternalOwner{id:7,com:Vec3::new(10.,-8.,2.),epoch:1,velocity:Vec3::ZERO,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::IDENTITY};
  for n in 0..2000 {
   let a=n as f32*0.007;let arm=Vec3::new(0.7,0.8+a.sin(),-0.6);
   let normal=Vec3::new(a.sin(),0.4,a.cos()).normalize();
   let translation=Vec3::new(0.02,-0.03,0.07);let spin=Vec3::new(a.sin()*0.4,0.2,a.cos()*0.3);
   let angular=rotation_gradient(Quat::from_scaled_axis(spin)*arm,normal,spin);
   let measured=-0.0074;let row=WorldContact{owner:7,obstacle:0,child:12,instance:0,point:owner.com+arm,normal,angular,surface:None,
     gap:linearized_gap(measured,angular,normal,translation,spin),native_gap:0.06};
   let j=[normal.x as f64,normal.y as f64,normal.z as f64,angular.x as f64,angular.y as f64,angular.z as f64];
   let x=[translation.x,translation.y,translation.z,spin.x,spin.y,spin.z];
   let residual=row.gap as f64+j.iter().zip(x).map(|(j,x)|j*x as f64).sum::<f64>();
   assert!((residual-measured as f64).abs()<1.0e-6,"{n}: {residual}");
  }
 }
}
