//! Geometry-only transport for the native animated mover. Uses the same
//! component bases, rounded shapes, GJK and conservative advance as NM.
use super::*;

// Contact admission and the feasible-direction projection MUST agree. GJK
// may return a planar normal with a few ulps of tangent component. Previously
// a t=0 contact used -1e-7 here but slide accepted it at -1e-6: slide returned
// the unchanged wish and the no-progress guard discarded the whole movement.
// Use the projection's existing feasibility rule in every admission path.
fn direction_allowed(v:Vec3,n:Vec3)->bool{n.dot(v)>=-1.0e-6}

#[cfg(test)]
thread_local!{static BROADPHASE_BASELINE:std::cell::Cell<bool>=const{std::cell::Cell::new(false)};}
pub(super) fn broadphase_enabled()->bool{
    #[cfg(test)] if BROADPHASE_BASELINE.with(|x|x.get()){return false;}
    !(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_WORLD_BROADPHASE_BASELINE"))
}

// Test-only A/B keeps the same conservative filter but runs it after metadata,
// as in the previous deployment. Never enabled by normal gameplay settings.
pub(super) fn early_bounds_enabled()->bool{
    broadphase_enabled()&&!(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_WORLD_QUERY_LATE_FILTER_BASELINE"))
}

// Projection onto the intersection n.dot(v)>=0. A 3-D closest feasible
// direction is the wish, a face, an edge, or the origin. This avoids sequential
// plane projection reintroducing motion into the first wall at a corner.
fn slide(wish: Vec3, normals: &[Vec3]) -> Vec3 {
    let valid=|v:Vec3|normals.iter().all(|&n|direction_allowed(v,n));
    let mut best=Vec3::ZERO;let mut error=wish.length_squared();
    let mut consider=|v:Vec3|{let e=v.distance_squared(wish);if e<error&&valid(v){best=v;error=e;}};
    consider(wish);
    for (i,&n) in normals.iter().enumerate(){
        consider(wish-n*n.dot(wish));
        for &other in &normals[..i]{let edge=n.cross(other);let l=edge.length_squared();if l>1.0e-12{consider(edge*(wish.dot(edge)/l));}}
    }
    best
}

impl Ragdoll {
    /// Sweep the entire supplied pose through a root translation, clipping and
    /// sliding against world triangles and current vehicle component bounds.
    /// Rotations stay fixed because this corrects translation AFTER native
    /// animation. It does not resolve a pre-existing static penetration, inject
    /// an impulse, alter the body, or substitute a ray for its volume.
    pub fn sweep_pose_translation(&self, build:&Build, pose:&[Transform], world:&Collision,
                                  obstacles:&[Obstacle], wish:Vec3)->(Vec3,usize){
        if pose.len()!=self.bodies.len()||pose.len()!=build.parts.len()||!wish.is_finite(){return (Vec3::ZERO,0)}
        let shapes:Vec<_>=self.bodies.iter().zip(pose).enumerate().filter_map(|(k,(b,p))|{
            if (self.contact_off|self.gone)>>k&1!=0{return None}
            let mut b=b.clone();b.set_q(p.rotation);
            Some((part_shape(&build.parts[k]),p.translation,turn(&b.bound()),bound_half(&build.parts[k])))
        }).collect();
        Self::sweep_shapes_translation(&shapes,world,obstacles,wish)
    }
    /// Shared conservative geometry transport. The shapes are explicit owner
    /// components, so animated-root queries do not invent articulated motion.
    pub fn sweep_shapes_translation(shapes:&[(gjk::Shape,Vec3,Mat3,Vec3)],world:&Collision,
                                    obstacles:&[Obstacle],wish:Vec3)->(Vec3,usize){
        if !wish.is_finite(){return (Vec3::ZERO,0)}
        static PROFILE:std::sync::OnceLock<Option<std::path::PathBuf>>=std::sync::OnceLock::new();
        let profile=PROFILE.get_or_init(||std::env::var_os("GTASA_PHYSICS_PROFILE").map(|p|std::path::PathBuf::from(p).with_extension("sweeps.jsonl")));
        let started=profile.as_ref().map(|_|std::time::Instant::now());
        let queries=std::cell::Cell::new(0usize);let candidates=std::cell::Cell::new(0usize);let iterations=std::cell::Cell::new(0usize);
        let mut moved=Vec3::ZERO;let mut remaining=wish;
        // Native support owns vertical motion for a horizontal root repair.
        let mut normals=if wish.z==0.{vec![Vec3::Z,-Vec3::Z]}else{vec![]};let mut hits=0;
        // Bound only the number of geometric feature changes, not force/time.
        // If exhausted, keep the already certified displacement.
        for _ in 0..16 {
            if profile.is_some(){iterations.set(iterations.get()+1);}
            if remaining.length_squared()<1.0e-14{break}
            let mut first:Option<(f32,Vec3)>=None;
            let mut take=|hit:Option<(f32,gjk::Closest)>|{
                if let Some((t,q))=hit{
                    if q.n.is_finite()&&!direction_allowed(remaining,q.n)&&t>=0.&&t<=1.&&first.is_none_or(|f|t<f.0){first=Some((t,q.n.normalize_or_zero()));}
                }
            };
            let cast=|query:&dyn Fn(f32)->Option<gjk::Closest>|{
                let q=query(0.)?;
                // Resting feet and separating overlap cannot trap tangential
                // movement. A penetrating surface only blocks further entry.
                if q.dist<=delirium_data::kv!(572)&&direction_allowed(remaining,q.n){return None}
                // The separating-contact check already evaluated t=0 with
                // this exact query. Reuse it; the original advancement math
                // and every later query stay identical.
                Self::advance_from(query,-remaining,q)
            };
            for &(shape,at,rot,half) in shapes{
                let from=at+moved;let to=from+remaining;let radius=Vec3::splat(half.length()+WORLD_MARGIN());
                let extent=Mat3::from_cols(rot.x_axis.abs(),rot.y_axis.abs(),rot.z_axis.abs())*half;
                let (low,high)=(from.min(to)-extent,from.max(to)+extent);
                let overlaps=|centre:Vec3,r:Mat3,h:Vec3|{
                    let e=Mat3::from_cols(r.x_axis.abs(),r.y_axis.abs(),r.z_axis.abs())*h;
                    low.cmple(centre+e).all()&&high.cmpge(centre-e).all()
                };
                let exact=shape.translation_bounds(rot,from,to,WORLD_MARGIN()+MARGIN());
                let frame=|at|crate::bvh::Frame{rows:[rot.x_axis,rot.y_axis,rot.z_axis],t:at};
                let triangles=if early_bounds_enabled(){
                    world.tris_in_part_bounded(from.min(to)-radius,from.max(to)+radius,&frame(from),&frame(to),-half,half,exact.0,exact.1)
                }else{world.tris_in_part(from.min(to)-radius,from.max(to)+radius,&frame(from),&frame(to),-half,half)};
                if profile.is_some(){candidates.set(candidates.get()+triangles.len());}
                for (t,_,dirs,_,_,poly) in triangles{
                    if broadphase_enabled()&&(!t[0].min(t[1]).min(t[2]).cmple(exact.1).all()||
                       !t[0].max(t[1]).max(t[2]).cmpge(exact.0).all()){continue;}
                    let tri=gjk::Shape::Tri(t.map(|p|p-t[0]),WORLD_MARGIN());
                    let query=|f|{if profile.is_some(){queries.set(queries.get()+1);}gjk::closest(&shape,rot,from+remaining*f-t[0],&tri,Mat3::IDENTITY,Vec3::ZERO,Vec3::Z,f32::MAX.sqrt(),Some(gjk::Pen::Tri(dirs))).map(|mut q|{
                        // Match the physical world's adjacency-normal cone;
                        // internal coplanar triangle edges are not obstacles.
                        // GJK triangles are two-sided. Apply the adjacency cone
                        // on the side actually met, preserving its orientation.
                        let side=if q.n.dot(poly.n)<0.{-1.}else{1.};
                        let mut normal=[(q.b+t[0],q.n*side)];poly.clamp(&mut normal,WORLD_MARGIN());q.n=normal[0].1*side;q
                    })};
                    take(cast(&query));
                }
                for ob in obstacles{
                    let body_rot=Mat3::from_quat(ob.rot);
                    if ob.parts.is_empty(){
                        if !overlaps(ob.centre,body_rot,ob.half){continue}
                        let shape_b=gjk::Shape::Box(ob.half,0.);
                        let query=|f|gjk::closest(&shape,rot,from+remaining*f-ob.centre,&shape_b,body_rot,Vec3::ZERO,Vec3::Z,f32::MAX.sqrt(),Some(gjk::Pen::Sphere));
                        take(cast(&query));
                    }else{for component in ob.parts.iter(){
                        if matches!(component.shape,gjk::Shape::Hull(v,_) if v.is_empty()){continue}
                        let origin=ob.at+body_rot*component.at;let r=body_rot*component.m;
                        if !overlaps(origin+r*((component.lo+component.hi)*0.5),r,(component.hi-component.lo)*0.5){continue}
                        let query=|f|gjk::closest(&shape,rot,from+remaining*f-origin,&component.shape,r,Vec3::ZERO,Vec3::Z,f32::MAX.sqrt(),Some(gjk::Pen::Sphere));
                        take(cast(&query));
                    }}
                }
            }
            let Some((fraction,normal))=first else{moved+=remaining;break};
            moved+=remaining*fraction;remaining*=1.-fraction;hits+=1;
            // Nearly parallel is not equivalent to the finite-precision
            // halfspace already retained. If this normal rejects the current
            // direction, discarding it here lets slide return that same
            // forbidden direction and the t0 no-progress guard stops tangents.
            if !normals.contains(&normal){normals.push(normal)}
            let next=slide(remaining,&normals);
            if next.distance_squared(remaining)<1.0e-14&&fraction==0.{break}
            remaining=next;
        }
        if let(Some(path),Some(started))=(profile,started){
            let ms=started.elapsed().as_secs_f64()*1000.;
            if ms>=4.{
                if let Ok(mut f)=std::fs::OpenOptions::new().create(true).append(true).open(path){
                    use std::io::Write;
                    let positions:Vec<_>=shapes.iter().map(|s|s.1.to_array()).collect();
                    let _=writeln!(f,"{{\"pid\":{},\"ms\":{},\"shapes\":{},\"candidates\":{},\"queries\":{},\"iterations\":{},\"hits\":{},\"wish\":{:?},\"moved\":{:?},\"positions\":{:?}}}",std::process::id(),ms,shapes.len(),candidates.get(),queries.get(),iterations.get(),hits,wish.to_array(),moved.to_array(),positions);
                }
            }
        }
        (moved,hits)
    }
    /// Exact live articulated components for recovery room occupancy. It is a
    /// query only: no fake bound, pose write, velocity change, or contact row.
    pub fn intersects_shape(&self,build:&Build,shape:&gjk::Shape,rotation:Mat3,centre:Vec3)->bool{
        for (k,b) in self.bodies.iter().enumerate(){
            if (self.contact_off|self.gone)>>k&1!=0{continue}
            let other=part_shape(&build.parts[k]);
            if gjk::closest(shape,rotation,centre,&other,turn(&b.bound()),b.x+self.o,Vec3::X,f32::INFINITY,Some(gjk::Pen::Sphere)).is_some_and(|q|q.dist<0.){return true}
        }false
    }

}

#[cfg(test)]mod broadphase_tests{
 use super::*;
 #[test]fn support_filter_preserves_translation_contacts_and_displacement(){
  super::super::moving_obstacle_regression::constants();
  let mut world=Collision::default();
  for x in -5..=5{for y in -5..=5{
   let(x,y)=(x as f32,y as f32);world.add(&[[x,y,0.],[x+1.,y,0.],[x+1.,y+1.,0.],[x,y+1.,0.]],&[[0,1,2],[0,2,3]],&[0,0]);
  }}
  world.add(&[[0.7,-3.,0.],[0.7,3.,0.],[0.7,3.,3.],[0.7,-3.,3.]],&[[0,1,2],[0,2,3]],&[0,0]);
  let shapes=[gjk::Shape::Capsule(0.4,0.15),gjk::Shape::Box(Vec3::new(0.2,0.3,0.4),0.02)];let mut cases=0;
  for shape in shapes{for angle in [0.,0.4,1.3]{for z in [0.1,0.4,0.56,0.8,1.4]{for scale in [0.000001,0.01,0.2,1.8]{for dir in [Vec3::X,Vec3::Y,Vec3::new(1.,1.,-0.1)]{
   let rig=[(shape,Vec3::new(0.,0.,z),Mat3::from_rotation_z(angle),Vec3::splat(0.7))];let wish=dir*scale;
   BROADPHASE_BASELINE.with(|x|x.set(true));let a=Ragdoll::sweep_shapes_translation(&rig,&world,&[],wish);
   BROADPHASE_BASELINE.with(|x|x.set(false));let b=Ragdoll::sweep_shapes_translation(&rig,&world,&[],wish);
   assert_eq!(a,b,"shape={shape:?} z={z} angle={angle} wish={wish:?}");cases+=1;
  }}}}}assert_eq!(cases,360);
 }
 #[test]fn rotation_bounds_contain_every_sampled_support_with_margin(){
  super::super::moving_obstacle_regression::constants();
  for shape in [gjk::Shape::Capsule(1.5,0.2),gjk::Shape::Box(Vec3::new(0.1,2.,0.3),0.05)]{
   for offset in [Vec3::ZERO,Vec3::new(2503.,-1650.,13.)]{
    let from=(offset,Quat::from_rotation_y(0.7));let to=(offset+Vec3::new(0.5,-0.3,0.2),Quat::from_rotation_z(2.8));
    let(lo,hi)=shape.rotating_bounds(from,to,0.02);
    for i in 0..=100{let t=i as f32/100.;let q=from.1.slerp(to.1,t);let at=from.0.lerp(to.0,t);
     for axis in [Vec3::X,Vec3::Y,Vec3::Z,-Vec3::X,-Vec3::Y,-Vec3::Z]{
      let p=at+q*shape.support(q.inverse()*axis)+axis*(match shape{gjk::Shape::Capsule(_,r)|gjk::Shape::Box(_,r)=>r,_=>0.}+0.02);
      assert!(p.cmpge(lo).all()&&p.cmple(hi).all());
     }
    }
   }
  }
 }
}
