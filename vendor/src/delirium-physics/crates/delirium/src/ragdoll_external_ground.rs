//! Ordinary support in the shared finite island. Explicit host ownership only.
//! Source callbacks 822F7118/822F74F0/822F6678; original four-part compound.
use super::*;
use crate::external_mobility::*;
use crate::original_ground::{self as source,OtherBody,Support,Contact};
use std::collections::BTreeMap;
#[derive(Clone,Default)]struct MapManifold {held:Vec<Held>,seen:Option<(Vec3,Mat3)>,generations:BTreeMap<u32,u64>}
impl MapManifold {
 fn update(&mut self,world:&Collision,shape:gjk::Shape,half:Vec3,tm:Mat3,x:Vec3){
 self.held.retain(|r|self.generations.get(&r.inst).is_some_and(|&key|world.slot_key(r.inst)==Some(key)));
 let o=Vec3::ZERO;let held=&mut self.held;
            let last = self.seen.as_ref().map_or(x, |s| s.0);
            let fast = (tm.transpose() * (x - last)).abs().cmpgt(half).any();
            let last_m = self.seen.as_ref().map_or(tm, |s| s.1);
            let (sc, sh) = swept((x, tm), (last, last_m), half);
            let (sm, sh) = (tm, sh + Vec3::splat(WORLD_MARGIN()));
            let sc = x + o + sm * sc;
            let found = {
                let reach = Vec3::splat(half.length() + delirium_data::kv!(73));
                let new = |q: gjk::Closest, at: Vec3| {
                    let local = tm.transpose() * (q.a - at);
                    let ground = q.b - q.n * WORLD_MARGIN() + o;
                    Held { local, ground, gap: q.dist, age: 0, m: 0, fl:0, imp: Vec3::ZERO, pimp: Vec3::ZERO, n: q.n, inst: 0, off: false }
                };
                let frame = |m: Mat3, at: Vec3| crate::bvh::Frame { rows: [m.x_axis, m.y_axis, m.z_axis], t: at + o };
                let mut found: Vec<(Held, crate::collision::Poly)> = world.tris_in_part(x.min(last) + o - reach, x.max(last) + o + reach,
                    &frame(tm, x), &frame(last_m, last), -half, half).into_iter().filter_map(|(t, m, dirs, _mask, inst, poly)| {
                    if !box_meets_tri(sc, sm, sh, t) {
                        return None;
                    }
                    let (tri, back) = (gjk::Shape::Tri(t.map(|v| v - t[0]), WORLD_MARGIN()), t[0] - o);
                    let query = |at: Vec3| gjk::closest(&shape, tm, at + o - t[0], &tri, Mat3::IDENTITY, Vec3::ZERO, Vec3::X, f32::INFINITY, Some(gjk::Pen::Tri(dirs)))
                        .map(|q| gjk::Closest { a: q.a + back, b: q.b + back, ..q });
                    let h = if fast {
                        let (t, q) = Ragdoll::advance(|t| query(last + (x - last) * t), last - x)?;
                        let mut h=new(q,last+(x-last)*t);
                        // The cast witness follows the part to its current
                        // endpoint (829C2CF8), not just the hit time.
                        h.gap=(x+tm*h.local-(h.ground+h.n*WORLD_MARGIN())).dot(h.n);
                        h
                    } else {
                        let q = query(x).filter(|q| q.dist < delirium_data::kv!(109))?;
                        new(q, x)
                    };
                    // Ordinary phConstrainedCollider pairs have +0x118 clear:
                    // fragment-only leaving/edge rejection is not applied.
                    Some((Held { m, fl:world.poly_flags(poly.first), inst, ..h }, poly))
                }).collect();
                for k in 0..found.len() {
                    let poly = found[k].1;
                    if found[..k].iter().any(|f| f.1.first == poly.first) {
                        continue;
                    }
                    let mut hits: Vec<(Vec3, Vec3)> = found[k..].iter().filter(|f| f.1.first == poly.first).map(|f| (f.0.ground + f.0.n * WORLD_MARGIN(), f.0.n)).collect();
                    poly.clamp(&mut hits, WORLD_MARGIN());
                    for (f, (at, n)) in found[k..].iter_mut().filter(|f| f.1.first == poly.first).zip(hits) {
                        (f.0.ground, f.0.n) = (at - n * WORLD_MARGIN(), n);
                    }
                }
                found.into_iter().map(|f| f.0).collect::<Vec<Held>>()
            };
            #[cfg(feature = "portal")]
            let found: Vec<Held> = found.into_iter().filter(|h| !world.in_hole(h.ground)).collect();
            #[cfg(feature = "portal")]
            held.retain(|h| !world.in_hole(h.ground));
            let mut groups: Vec<(u32, Vec<Held>)> = vec![];
            for r in held.drain(..) {
                match groups.iter_mut().find(|g| g.0 == r.inst) {
                    Some(g) => g.1.push(r),
                    None => groups.push((r.inst, vec![r])),
                }
            }
            for new in found {
                let g = match groups.iter().position(|g| g.0 == new.inst) {
                    Some(g) => g,
                    None => { groups.push((new.inst, vec![])); groups.len() - 1 }
                };
                hold(&mut groups[g].1, new);
            }
            for (_, held) in groups.iter_mut() {
                for r in held.iter_mut() {
                    if r.age != 0 {
                        r.gap = (x + o + tm * r.local - r.ground).dot(r.n) - WORLD_MARGIN();
                    }
                    r.age += if r.age == 1 { 2 } else { 1 };
                }
                let slid = |r: &Held| { let d = x + o + tm * r.local - r.ground; (d - r.n * d.dot(r.n)).length_squared() };
                let (mut k, mut worst) = (0, (None, 0.0));
                while k < held.len() {
                    if held[k].gap >= MARGIN() {
                        held.swap_remove(k);
                        continue;
                    }
                    let d = slid(&held[k]);
                    if held[k].age > delirium_data::kv!(1 int) && d > worst.1 && d > delirium_data::kv!(568) {
                        worst = (Some(k), d);
                    }
                    k += 1;
                }
                if let Some(k) = worst.0 {
                    held.swap_remove(k);
                }
            }
            *held = groups.into_iter().flat_map(|g| g.1).collect();

 self.generations=held.iter().filter_map(|r|world.slot_key(r.inst).map(|key|(r.inst,key))).collect();
 self.seen=Some((x,tm));
 }
}

#[derive(Clone,Copy,Debug)]enum Target {Map{slot:u32,key:u64},Owner{ id:u64,component:i32,local:Vec3 },Member{ id:usize,part:usize,local:Vec3 }}
#[derive(Clone,Default)]struct Ped {state:source::State,maps:[MapManifold;4],target:Option<Target>,had:bool,plane:f32}
#[derive(Default)]pub(super) struct Registry {
    claims:BTreeMap<u64,ExternalGroundClaim>,states:BTreeMap<u64,Ped>,
    drives:BTreeMap<u64,ExternalDrive>,results:Vec<ExternalGroundResult>,staged:bool,bodies:BTreeMap<u64,ExternalGroundBody>,
}
impl Registry {pub(super) fn clear_stage(&mut self){self.claims.clear();self.drives.clear();self.results.clear();self.staged=false;self.bodies.clear();}}
impl Group {
    pub fn stage_external_ground_bodies(&mut self,epoch:u64,bodies:&[ExternalGroundBody])->Result<(),ExternalError>{
        if epoch!=self.external.epoch{return Err(ExternalError::WrongEpoch(epoch));}
        if self.external_drive_consumed_epoch==Some(epoch){return Err(ExternalError::ConsumedDriveEpoch(epoch));}
        let mut next=BTreeMap::new();for &b in bodies{
            if !self.external.owners.contains_key(&b.id){return Err(ExternalError::MissingSnapshot(b.id));}
            if !b.mass.is_finite()||b.mass<=0.{return Err(ExternalError::InvalidGround(b.id));}
            if next.insert(b.id,b).is_some_and(|old|old!=b){return Err(ExternalError::DivergentGround(b.id));}
        }
        self.ground.bodies=next;Ok(())
    }
    pub fn stage_external_ground(&mut self,epoch:u64,claims:&[ExternalGroundClaim])->Result<(),ExternalError>{
        if epoch!=self.external.epoch{return Err(ExternalError::WrongEpoch(epoch));}
        if self.external_drive_consumed_epoch==Some(epoch){return Err(ExternalError::ConsumedDriveEpoch(epoch));}
        let mut next=BTreeMap::new();
        for &c in claims {
            let Some(o)=self.external.owners.get(&c.id)else{return Err(ExternalError::MissingSnapshot(c.id))};
            if !c.authored_xy.is_finite()||o.angular_inverse!=Mat3::ZERO||(o.linear_inverse-Mat3::IDENTITY/25.).to_cols_array().iter().any(|x|x.abs()>1e-7){return Err(ExternalError::InvalidGround(c.id));}
            if next.insert(c.id,c).is_some_and(|old|old!=c){return Err(ExternalError::DivergentGround(c.id));}
        }
        if self.ground.staged&&next!=self.ground.claims{return Err(ExternalError::DivergentGround(0));}
        self.ground.claims=next;self.ground.staged=true;Ok(())
    }
    pub fn external_ground_results(&self)->&[ExternalGroundResult]{&self.ground.results}
    /// Entity-generation destruction only, not temporary normal->NM handoff.
    /// A re-added normal collider preserves the source ped's support history.
    pub fn forget_external_ground(&mut self,id:u64){self.ground.states.remove(&id);self.ground.claims.remove(&id);self.ground.drives.remove(&id);}
    pub(super) fn ground_claimed(&self,id:u64)->bool{self.ground.claims.contains_key(&id)}
}
pub(super) fn drive(group:&Group,id:u64)->ExternalDrive{group.ground.drives.get(&id).copied().unwrap_or(ExternalDrive{id,..Default::default()})}
fn put(group:&mut Group,id:u64,root:Vec3,child:usize,other:OtherBody,support:Support,target:Target)->bool{
    let Some(p)=group.ground.states.get_mut(&id).filter(|_|group.ground.claims.contains_key(&id))else{return true};
    let disposition=p.state.contact(root,Contact{child,other,support},true);
    if disposition.selected_support{p.target=Some(target);}
    disposition.solve
}
fn actual_other(metadata:ExternalGroundBody)->OtherBody {
    if metadata.fixed{OtherBody::Fixed{excluded_type15:metadata.excluded_type15}}
    else {OtherBody::Dynamic{mass:metadata.mass,excluded_type15:metadata.excluded_type15}}
}
pub(super) fn pair(group:&mut Group,a:&Obstacle,b:&Obstacle,child:usize,point:Vec3,n:Vec3)->bool{
    let id=a.owner.unwrap();if !group.ground_claimed(id){return true;}
    let other=group.external.owners[&b.owner.unwrap()];
    let metadata=group.ground.bodies[&other.id];
    let support=Support{owner:other.id,point,normal:n,velocity:if metadata.boat{b.vel_at(a.at)}else{Vec3::ZERO},stairs:false};
    put(group,id,a.at,child,actual_other(metadata),support,Target::Owner{id:other.id,component:b.id,local:b.rot.inverse()*(point-b.at)})
}
pub(super) fn begin(group:&mut Group,ms:&mut [Member],world:&Collision,dt:f32)->Result<(),ExternalError>{
    for m in ms.iter_mut(){m.doll.normal_support_owners=group.ground.claims.keys().copied().collect();}
    if group.external_drive_consumed_epoch==Some(group.external.epoch){return Ok(());}
    let claims:Vec<_>=group.ground.claims.values().copied().collect();
    if !claims.is_empty(){for &id in group.external.owners.keys(){
        if !group.ground.bodies.contains_key(&id){return Err(ExternalError::InvalidGround(id));}
    }}
    // Validate every claim before modifying any physical body or persistent state.
    for c in &claims{
        let gs:Vec<_>=group.external_geometry.iter().filter(|o|o.owner==Some(c.id)).collect();
        if gs.len()!=1||!gs[0].filter.animated_ped()||gs[0].parts.len()!=4{return Err(ExternalError::InvalidGround(c.id));}
    }
    for claim in claims {
        let owner=group.external.owners[&claim.id];
        let ob=*group.external_geometry.iter().find(|o|o.owner==Some(claim.id)).unwrap();
        let mut p=group.ground.states.remove(&claim.id).unwrap_or_else(||Ped{state:source::State{grounded:claim.initial_grounded,before:claim.initial_grounded,..Default::default()},..Default::default()});
        if claim.readd{p.maps=Default::default();}
        // Refresh the previous material point from the SAME staged epoch; a
        // removed/recycled generation cannot act as a phantom platform.
        if let Some(s)=p.state.ground.as_mut(){
            let refreshed=match p.target {
                Some(Target::Map{slot,key})=>world.slot_key(slot)==Some(key),
                Some(Target::Owner{id,component,local})=>group.external_geometry.iter().find(|o|o.owner==Some(id)&&o.id==component).is_some_and(|o|{s.point=o.at+o.rot*local;s.velocity=if group.ground.bodies[&id].boat{o.vel_at(ob.at)}else{Vec3::ZERO};true}),
                Some(Target::Member{id,part,local})=>ms.iter().find(|m|m.id==id).and_then(|m|m.doll.bodies.get(part).map(|b|(m.doll.o,b))).is_some_and(|(origin,b)|{s.point=origin+b.x+turn(&b.bound())*local;s.velocity=Vec3::ZERO;true}),
                None=>false,
            };
            if !refreshed{p.state.ground=None;p.target=None;}
        }
        let f=p.state.begin(source::Input{root:ob.at,velocity:owner.velocity-Vec3::Z*p.plane,authored_xy:claim.authored_xy,normal_instance:true,simulator_enabled:true,opening_damping:claim.opening_damping});p.had=f.had_ground;p.target=None;
        let support=f.support_acceleration*(25.*dt);let gravity=f.gravity_acceleration*(25.*dt);
        // The move blender's plane velocity is an authored velocity channel,
        // separate from falling speed. Replace its previous contribution once;
        // never accumulate a fresh uphill velocity at every physics substep.
        let plane_impulse=Vec3::Z*(25.*(f.slope_velocity-p.plane));p.plane=f.slope_velocity;
        // 822F74F0 calls ped AddForce only; do not invent an opposite spring
        // force on the support. Normal contacts remain reciprocal in Group.
        group.ground.drives.insert(claim.id,ExternalDrive{id:claim.id,impulse:support+gravity+plane_impulse,torque:Vec3::ZERO});
        group.ground.results.push(ExternalGroundResult{id:claim.id,epoch:group.external.epoch,support_member:usize::MAX,support_part:usize::MAX,support_impulse:support,gravity_impulse:gravity,slope_velocity:f.slope_velocity,..Default::default()});
        for (child,part) in ob.parts.iter().enumerate(){
            let tm=Mat3::from_quat(ob.rot)*part.m;let centre=ob.at+ob.rot*part.at;
            p.maps[child].update(world,part.shape,(part.hi-part.lo)*0.5,tm,centre);
            let contacts=p.maps[child].held.clone();
            for r in contacts {
                let s=Support{owner:0,point:r.ground,normal:r.n,velocity:Vec3::ZERO,stairs:r.fl&1!=0};
                let d=p.state.contact(ob.at,Contact{child,other:OtherBody::Fixed{excluded_type15:false},support:s},true);
                if d.selected_support{p.target=world.slot_key(r.inst).map(|key|Target::Map{slot:r.inst,key});}
            }
            if child==1{p.maps[child].held.clear();}else if child==3{for r in &mut p.maps[child].held{r.off=true;}}
        }
        group.ground.states.insert(claim.id,p);
        // Source callback sees the same current articulated collision shapes
        // before child1 rows are removed; no render/animation-bone proxy.
        for m in ms.iter(){
            let poses:Vec<_>=m.doll.bodies.iter().map(|b|Transform::from_translation(b.x+m.doll.o).with_rotation(b.q)).collect();
            for c in m.doll.query_obstacle_pose_contacts(m.build,&poses,&ob){
                if c.gap>lsc4_combat::pedbodies::BREAK{continue;}
                let body=&m.doll.bodies[c.a];let normal=-c.normal;
                let s=Support{owner:0,point:c.point_a,normal,velocity:Vec3::ZERO,stairs:false};
                put(group,claim.id,ob.at,c.b,OtherBody::Dynamic{mass:m.doll.mass(),excluded_type15:false},s,Target::Member{id:m.id,part:c.a,local:turn(&body.bound()).transpose()*(c.point_a-m.doll.o-body.x)});
            }
        }
    }
    Ok(())
}
pub(super) struct GroundRow {pub id:u64,pub child:usize,pub slot:usize,pub point:Vec3,pub normal:Vec3,pub row:Row}
pub(super) fn rows(group:&Group,h:f32)->Vec<GroundRow>{
    use lsc4_combat::pedbodies;let mut out=vec![];
    for (&id,p) in &group.ground.states{
        if !group.ground_claimed(id){continue;}let owner=&group.external.owners[&id];let ob=group.external_geometry.iter().find(|o|o.owner==Some(id)).unwrap();
        for child in [0,2]{let part=&ob.parts[child];let tm=Mat3::from_quat(ob.rot)*part.m;let centre=ob.at+ob.rot*part.at;
            for (slot,c) in p.maps[child].held.iter().enumerate().filter(|(_,r)|!r.off){
                let point=centre+tm*c.local;let n=c.n;let rot=Mat3::from_quat(ob.rot);
                let a=pedbodies::Side{rows:rot.to_cols_array_2d(),at:(rot.transpose()*(point-ob.at)).to_array(),n:n.to_array(),margin:[pedbodies::MARGIN;3],inv_mass:1./25.,state:2};
                let b=pedbodies::Side{rows:Mat3::IDENTITY.to_cols_array_2d(),at:c.ground.to_array(),n:(-n).to_array(),margin:[0.;3],inv_mass:0.,state:1};
                let target=pedbodies::target(-c.gap,1./h,&a,&b);let torque=(point-owner.com).cross(n);
                let j=[n.x as f64,n.y as f64,n.z as f64,torque.x as f64,torque.y as f64,torque.z as f64];
                let row=Row{relaxation:pedbodies::SOR as f64,lambda:c.imp.x as f64,..Row::with(j.to_vec(),owner.response(j).to_vec(),target.speed as f64,RowKind::Push)};
                out.push(GroundRow{id,child,slot,point,normal:n,row});
            }
        }
    }out
}
pub(super) fn set_push(group:&mut Group,id:u64,child:usize,slot:usize,lambda:f32){if let Some(c)=group.ground.states.get_mut(&id).and_then(|p|p.maps[child].held.get_mut(slot)){c.imp=Vec3::X*lambda;}}
pub(super) fn finish(group:&mut Group){
    for r in &mut group.ground.results{
        let Some(p)=group.ground.states.get_mut(&r.id)else{continue};let root=group.external.owners[&r.id].com;
        let delta=group.external_placements.iter().find(|x|x.id==r.id&&x.valid).map_or(Vec3::ZERO,|x|x.admitted_position-group.external_predictors[&r.id].pending_position);
        p.state.finish(root+delta,p.had);r.grounded=p.state.grounded;r.before=p.state.before;
        if let Some(s)=p.state.ground{r.point=s.point;r.normal=s.normal;r.velocity=s.velocity;r.support_owner=s.owner;}
        if let Some(Target::Member{id,part,..})=p.target{r.support_member=id;r.support_part=part;}
    }
}

#[cfg(test)]mod tests{
    use super::*;
    use super::super::moving_obstacle_regression::{constants,original_ped_obstacle,original_ped_owner};
    fn floor()->Collision{let mut w=Collision::default();w.add_flagged(&[[-20.,-20.,0.],[20.,-20.,0.],[20.,20.,0.],[-20.,20.,0.]],&[[0,1,2],[0,2,3]],&[0,0],&[0,0]);w}
    #[test]fn fast_cast_transports_witness_and_depth_to_current_part(){
        constants();let mut world=Collision::default();world.add_flagged(&[[0.,-20.,-20.],[0.,-20.,20.],[0.,20.,20.],[0.,20.,-20.]],&[[0,1,2],[0,2,3]],&[0,0],&[0,0]);
        let mut map=MapManifold::default();let shape=gjk::Shape::Capsule(0.,0.1);let half=Vec3::splat(0.1);
        map.update(&world,shape,half,Mat3::IDENTITY,-Vec3::X);assert!(map.held.is_empty());
        map.update(&world,shape,half,Mat3::IDENTITY,Vec3::X);assert!(!map.held.is_empty());
        assert!(map.held.iter().any(|h|h.n.x< -0.99&&h.gap< -1.0),"cast must encode a crossed wall, not a zero-depth touching point");
    }
    fn stage(g:&mut Group,epoch:u64,root:Vec3,velocity:Vec3,claimed:bool){
        g.stage_external(epoch,&[original_ped_owner(1,epoch,root,velocity)]).unwrap();
        g.stage_external_geometry(&[original_ped_obstacle(1,root,velocity)]).unwrap();
        g.stage_external_predictors(epoch,&[ExternalPredictor{id:1,pending_position:Vec3::ZERO,pending_rotation:Quat::IDENTITY}]).unwrap();
        if claimed{
            g.stage_external_ground_bodies(epoch,&[ExternalGroundBody{id:1,boat:false,excluded_type15:false,fixed:false,mass:25.}]).unwrap();
            g.stage_external_ground(epoch,&[ExternalGroundClaim{id:1,authored_xy:velocity.truncate(),initial_grounded:true,opening_damping:true,readd:false}]).unwrap();
        }
    }
    #[test]fn ordinary_ground_capsule_is_support_not_rigid_floor_and_state_persists(){
        constants();let world=floor();let mut g=Group::default();let (mut root,mut velocity)=(Vec3::Z,Vec3::ZERO);let h=1./120.;let mut max_abs=0f32;
        for frame in 1..241{
            // Host predictor already integrates incoming velocity once.
            root+=velocity*h;stage(&mut g,frame,root,velocity,true);
            Ragdoll::step_group(&mut[],&world,h,&mut g);assert!(g.external_error().is_none(),"{:?}",g.external_error());
            let p=g.external_placements()[0];assert!(p.valid,"{p:?}");root+=p.admitted_position;velocity+=g.external_results()[0].delta_velocity;
            assert!(g.ground.states[&1].maps[1].held.is_empty());assert!(g.ground.states[&1].maps[3].held.iter().all(|p|p.off));
            max_abs=max_abs.max((root.z-1.).abs());
        }
        assert!(g.external_ground_results()[0].grounded,"{:?}",g.external_ground_results());
        assert!(max_abs<0.02,"support must not push the 0.3m-low capsule out: {root:?} vmax={velocity:?} max={max_abs}");
        assert!(velocity.z.abs()<0.02);assert!((root.z-1.).abs()<0.02);
    }
    #[test]fn external_vertical_impulse_survives_the_next_support_step(){
        constants();let world=floor();let mut g=Group::default();stage(&mut g,1,Vec3::Z,Vec3::ZERO,true);Ragdoll::step_group(&mut[],&world,1./120.,&mut g);
        let predicted=Vec3::Z*(1.+1./120.);
        stage(&mut g,2,predicted,Vec3::Z,true);Ragdoll::step_group(&mut[],&world,1./120.,&mut g);
        let v=Vec3::Z+g.external_results()[0].delta_velocity;assert!(v.z>0.8&&v.z<1.,"no SA reset or capsule push: {v:?}");
        assert_eq!(g.external_placements()[0].admitted_position,Vec3::ZERO,"native prediction already integrated incoming upward velocity; gravity changes next velocity");
    }
    #[test]fn claim_is_opt_in_epoch_checked_and_unknown_metadata_fails_before_mutation(){
        constants();let mut g=Group::default();stage(&mut g,1,Vec3::Z,Vec3::ZERO,false);Ragdoll::step_group(&mut[],&floor(),1./60.,&mut g);assert!(g.external_ground_results().is_empty());assert_eq!(g.external_results()[0].impulse,Vec3::ZERO);
        stage(&mut g,2,Vec3::Z,Vec3::ZERO,false);
        let c=ExternalGroundClaim{id:1,authored_xy:Vec2::ZERO,initial_grounded:true,opening_damping:true,readd:false};
        assert!(g.stage_external_ground(1,&[c]).is_err());g.stage_external_ground(2,&[c]).unwrap();Ragdoll::step_group(&mut[],&floor(),1./60.,&mut g);
        assert_eq!(g.external_error(),Some(ExternalError::InvalidGround(1)));assert!(g.ground.states.is_empty());assert!(g.external_results().is_empty());
    }
    #[test]fn claimed_peer_capsule_is_removed_after_support_callback_and_mass_rule_is_real(){
        constants();let mut g=Group::default();let root=Vec3::Z;let a=original_ped_owner(1,7,root,Vec3::ZERO);let b=original_ped_owner(2,7,Vec3::ZERO,Vec3::ZERO);
        g.stage_external(7,&[a,b]).unwrap();let oa=original_ped_obstacle(1,root,Vec3::ZERO);let ob=original_ped_obstacle(2,Vec3::ZERO,Vec3::ZERO);g.stage_external_geometry(&[oa,ob]).unwrap();
        g.stage_external_ground_bodies(7,&[ExternalGroundBody{id:1,boat:false,excluded_type15:false,fixed:false,mass:25.},ExternalGroundBody{id:2,boat:false,excluded_type15:false,fixed:false,mass:25.}]).unwrap();g.stage_external_ground(7,&[ExternalGroundClaim{id:1,authored_xy:Vec2::ZERO,initial_grounded:true,opening_damping:true,readd:false}]).unwrap();
        begin(&mut g,&mut[],&Collision::default(),1./60.).unwrap();
        assert!(!pair(&mut g,&oa,&ob,1,Vec3::Z*0.4,Vec3::Z));assert_eq!(g.ground.states[&1].state.ground.unwrap().point.z,0.4);
        assert!(pair(&mut g,&oa,&ob,3,Vec3::Z*0.4,Vec3::Z));
        g.ground.bodies.get_mut(&2).unwrap().mass=201.;assert!(!pair(&mut g,&oa,&ob,3,Vec3::Z*0.4,Vec3::Z));
        let rows=external_pairs::collect(&mut g,1./60.);assert!(rows.iter().all(|r|r.child_a!=1));
    }
    #[test]fn step_down_keeps_force_continuity_and_does_not_snap_or_enter_ragdoll(){
        constants();let mut world=Collision::default();
        world.add_flagged(&[[-20.,-20.,0.],[0.,-20.,0.],[0.,20.,0.],[-20.,20.,0.],[0.,-20.,-0.15],[20.,-20.,-0.15],[20.,20.,-0.15],[0.,20.,-0.15]],&[[0,1,2],[0,2,3],[4,5,6],[4,6,7]],&[0;4],&[0;4]);
        let mut g=Group::default();let(mut root,mut velocity)=(Vec3::new(-1.,0.,1.),Vec3::new(1.,0.,0.));let h=1./120.;let mut max_fall=0f32;
        for frame in 1..481{
            root+=velocity*h;stage(&mut g,frame,root,velocity,true);Ragdoll::step_group(&mut[],&world,h,&mut g);
            assert!(g.external_error().is_none());assert!(g.external_placements()[0].valid);
            root+=g.external_placements()[0].admitted_position;velocity+=g.external_results()[0].delta_velocity;
            max_fall=max_fall.max(velocity.z.abs());
            if frame>2{assert!(g.external_ground_results()[0].grounded,"ordinary curb loses source support at frame {frame}, {root:?}");}
        }
        assert!((root.z-0.85).abs()<0.02,"{root:?}");assert!(max_fall<1.0,"{max_fall}");assert!((velocity.x-1.).abs()<0.001);
    }
    #[test]fn boat_damping_uses_current_root_point_velocity_and_does_not_invent_reciprocal_spring(){
        constants();let mut g=Group::default();let root=Vec3::Z;let epoch=1;let ped=original_ped_owner(1,epoch,root,Vec3::ZERO);
        let boat=ExternalOwner{id:2,epoch,com:Vec3::ZERO,velocity:Vec3::Z,spin:Vec3::Y,linear_inverse:Mat3::IDENTITY/500.,angular_inverse:Mat3::IDENTITY/600.};
        g.stage_external(epoch,&[ped,boat]).unwrap();let a=original_ped_obstacle(1,root,Vec3::ZERO);let b=Obstacle{id:2,owner:Some(2),at:Vec3::ZERO,rot:Quat::IDENTITY,centre:Vec3::ZERO,com:Vec3::ZERO,half:Vec3::new(3.,3.,0.1),vel:boat.velocity,spin:boat.spin,..Default::default()};g.stage_external_geometry(&[a,b]).unwrap();
        g.stage_external_ground_bodies(epoch,&[ExternalGroundBody{id:1,boat:false,excluded_type15:false,fixed:false,mass:25.},ExternalGroundBody{id:2,boat:true,excluded_type15:false,fixed:false,mass:500.}]).unwrap();g.stage_external_ground(epoch,&[ExternalGroundClaim{id:1,authored_xy:Vec2::ZERO,initial_grounded:true,opening_damping:true,readd:false}]).unwrap();
        begin(&mut g,&mut[],&Collision::default(),1./120.).unwrap();pair(&mut g,&a,&b,1,Vec3::ZERO,Vec3::Z);
        assert_eq!(g.ground.states[&1].state.ground.unwrap().velocity,Vec3::X+Vec3::Z,"root lever, not contact lever, and current spin");
        assert_eq!(drive(&g,2).impulse,Vec3::ZERO,"822F74F0 has no opposite support force call");
        g.ground.bodies.get_mut(&2).unwrap().boat=false;g.ground.states.get_mut(&1).unwrap().state.ground=None;pair(&mut g,&a,&b,1,Vec3::ZERO,Vec3::Z);
        assert_eq!(g.ground.states[&1].state.ground.unwrap().velocity,Vec3::ZERO,"a car is not silently classified as boat");
    }
}
