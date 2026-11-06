//! Original animated locomotion owner contract; registration/solve is owned by
//! the shared scene mobility registry. This module never creates per-agent
//! obstacle masses or commits a native movement by itself.
//! 822FA100 creates phInstGta +700 and phConstrainedCollider +6FC. The NM
//! owner is +704. 822FF540 restores +700 to current +38 AFTER the fit/room
//! tests and placement in BlendFromNM, not while waiting for a get-up.
use delirium::{gjk,prelude::{Vec3,Mat3,Quat},ragdoll::ObPart};
use sa_combat::pedbodies::{self,PARTS};
use delirium::{collision::Collision,ragdoll::Obstacle};

/// This body has one translational velocity, no turning mobility and no
/// per-bone motion. Render/targeting bones intentionally cannot enter it.
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct AnimatedOwner {pub root:Vec3,pub heading:f32,pub velocity:Vec3}
impl AnimatedOwner {
    pub fn inverse_mass(&self)->f32{1./pedbodies::MASS}
    pub fn point_velocity(&self,_point:Vec3)->Vec3{self.velocity}
    pub fn transform(&self)->Mat3{Mat3::from_quat(Quat::from_rotation_z(self.heading))}
    pub fn body(&self)->pedbodies::Body{pedbodies::Body::ped(self.root.to_array(),self.heading,self.velocity.to_array())}
}
#[derive(Clone,Copy,Debug,PartialEq)]pub enum Owner {Nm,Animated(AnimatedOwner)}
#[derive(Clone,Copy)]pub struct FittedPlacement {pub root:Vec3,pub heading:f32,pub ground_fitted:bool,pub room_clear:bool,pub placement_committed:bool}
impl Owner {
    /// Host facts about the original fit -> room -> placement order; this is
    /// not an alternate pose detector. Root is IV's placed root, ground+1m,
    /// explicitly adapted by SA, never the lying pelvis passed unchanged.
    pub fn handback(&mut self,fit:FittedPlacement,root_velocity:Vec3)->bool{
        if !matches!(self,Owner::Nm)||!fit.ground_fitted||!fit.room_clear||!fit.placement_committed||
           !fit.root.is_finite()||!fit.heading.is_finite()||!root_velocity.is_finite(){return false}
        *self=Owner::Animated(AnimatedOwner{root:fit.root,heading:fit.heading,velocity:root_velocity});true
    }
    /// Semantic NM event accepted by BlendFromNM. Seeding the retained NM
    /// body from the current rendered pose/motion is the caller's atomic work.
    pub fn accepted_nm_response(&mut self){*self=Owner::Nm;}
}

pub fn parts()->&'static [ObPart;4]{
    static SHAPES:std::sync::LazyLock<[ObPart;4]>=std::sync::LazyLock::new(||std::array::from_fn(|i|{
        let p=PARTS[i];
        let(shape,m,half)=if i==0||i==3{
            (gjk::Shape::Curved(Box::leak(Box::new(delirium::curved::ped_part(p.core,2.*p.half,p.round))),p.round),Mat3::IDENTITY,Vec3::new(p.core+p.round,p.core+p.round,p.half+p.round))
        }else{
            let a=std::f32::consts::FRAC_PI_2 as f64;
            let(c,s)=(delirium_data::mathrt::cos(a) as f32,delirium_data::mathrt::sin(a) as f32);
            (gjk::Shape::Capsule(p.half,p.round),Mat3::from_cols(Vec3::X,Vec3::new(0.,c,s),Vec3::new(0.,-s,c)),Vec3::new(p.round,p.half+p.round,p.round))
        };
        ObPart{shape,m,at:Vec3::Z*p.z,lo:-half,hi:half,mesh:None,filter:delirium::ragdoll::CollisionFilter{kind:p.types,include:p.include}}
    }));&SHAPES
}

/// Components admitted against an NM fragment. Keep the component index2
/// present but empty so contact component identities stay original.
pub fn fragment_parts()->&'static [ObPart;4]{
    static SHAPES:std::sync::LazyLock<[ObPart;4]>=std::sync::LazyLock::new(||{
        let mut p=*parts();p[2].shape=gjk::Shape::Hull(&[],0.);p
    });&SHAPES
}
/// Original standing-room probe82368F78, used only after pose fitting.
/// This is NOT the locomotion world's support filter and does not certify a
/// route from the old root or a full animation trajectory.
pub fn room_clear(world:&Collision,obstacles:&[Obstacle],at:Vec3,exclude:u64)->bool{room_clear_ignoring(world,obstacles,at,exclude,None)}
/// `ignored`: the vehicle the body still ignores (SA m_pEntityIgnoredCollision) does not take its room.
pub fn room_clear_ignoring(world:&Collision,obstacles:&[Obstacle],at:Vec3,exclude:u64,ignored:Option<u64>)->bool{
    if !at.is_finite(){return false}
    let radius=0.25;let down=0.4;let up=0.57;
    let shape=gjk::Shape::Capsule((up+down)*0.5,radius);
    let stand=Mat3::from_rotation_x(std::f32::consts::FRAC_PI_2);
    let centre=at+Vec3::Z*((up-down)*0.5);
    let reach=Vec3::splat(radius);
    for(t,_,dirs,..)in world.tris_near(at-Vec3::Z*down-reach,at+Vec3::Z*up+reach){
        let tri=gjk::Shape::Tri(t.map(|p|p-t[0]),0.005);
        if gjk::closest(&shape,stand,centre-t[0],&tri,Mat3::IDENTITY,Vec3::ZERO,Vec3::X,f32::INFINITY,Some(gjk::Pen::Tri(dirs))).is_some_and(|q|q.dist<0.){return false}
    }
    for ob in obstacles{
        if ob.owner==Some(exclude)||(ignored.is_some()&&ob.owner==ignored){continue}
        let r=Mat3::from_quat(ob.rot);
        let meets=|s:&gjk::Shape,m:Mat3,p:Vec3|gjk::closest(&shape,stand,centre,s,m,p,Vec3::X,f32::INFINITY,Some(gjk::Pen::Sphere)).is_some_and(|q|q.dist<0.);
        if ob.parts.is_empty(){if meets(&gjk::Shape::Box(ob.half,0.),r,ob.centre){return false}}
        else{let shapes=if ob.owner.is_some_and(|id|id>>32==2){parts().as_slice()}else{ob.parts};for p in shapes{
            if matches!(p.shape,gjk::Shape::Hull(v,_)if v.is_empty()){continue}
            if meets(&p.shape,r*p.m,ob.at+r*p.at){return false}
        }}
    }true
}

/// Root correction uses the original compound and the same conservative GJK
/// sweep as the physical bodies. Existing penetrations allow separation and
/// tangential movement; this does not claim to replace native ground support.
pub fn sweep(owner:AnimatedOwner,world:&Collision,obstacles:&[Obstacle],wish:Vec3,exclude:u64)->(Vec3,usize){
    let rot=owner.transform();let shapes:Vec<_>=parts().iter().map(|p|{
        (p.shape,owner.root+rot*p.at,rot*p.m,(p.hi-p.lo)*0.5)
    }).collect();
    let others:Vec<_>=obstacles.iter().filter(|o|o.owner!=Some(exclude)).cloned().collect();
    delirium::ragdoll::Ragdoll::sweep_shapes_translation(&shapes,world,&others,wish)
}

#[derive(Clone,Copy,Debug)]pub struct Contact {pub child:usize,pub a:Vec3,pub b:Vec3,pub normal:Vec3,pub gap:f32,pub closing:f32}
/// Exact shared GJK against one physical fragment component. Body translation
/// gives the velocity on A; the physical component's supplied point velocity
/// gives B. Full solver integration will use its articulated Jacobian, not
/// pretend the fragment is a second root mass.
pub fn fragment_contacts(body:AnimatedOwner,shape:gjk::Shape,rotation:Mat3,centre:Vec3,
                         velocity:Vec3,spin:Vec3)->Vec<Contact>{
    let rot=body.transform();let mut rows=vec![];
    for(i,p)in parts().iter().enumerate(){
        if PARTS[i].include&pedbodies::FRAGMENT==0{continue}
        let at=body.root+rot*p.at;
        if let Some(q)=gjk::closest(&p.shape,rot*p.m,at,&shape,rotation,centre,Vec3::X,f32::INFINITY,Some(gjk::Pen::Sphere)){
            if q.dist<=pedbodies::BREAK{
                let vb=velocity+spin.cross(q.b-centre);
                rows.push(Contact{child:i,a:q.a,b:q.b,normal:q.n,gap:q.dist,closing:-(body.velocity-vb).dot(q.n)});
            }
        }
    }rows
}

#[cfg(test)]mod tests{
    use super::*;
    #[test]fn physical_owner_diagnostic_matches_original_components_not_display_limbs(){
        init();
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        let (source,_)=delirium::ragdoll::Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
        let mut part=source.parts[16].clone();part.parent=None;part.bone=0;part.radius=0.1;part.half=0.;part.boxed=None;part.anchor=Vec3::ZERO;part.offset=delirium::prelude::Transform::IDENTITY;
        let build=delirium::ragdoll::Build{parts:vec![part],nm:source.nm};
        for heading in [0.,0.4,1.57]{
            let owner=AnimatedOwner{root:Vec3::Z,heading,velocity:Vec3::ZERO};
            let position=owner.root+owner.transform()*Vec3::new(0.28,0.,0.085);
            let pose=[delirium::prelude::Transform::from_translation(position)];
            let mut doll=delirium::ragdoll::Ragdoll::new(&build,&pose,Vec3::ZERO);
            let obstacle=Obstacle{filter:delirium::ragdoll::CollisionFilter{kind:0x20,include:0x79FE},at:owner.root,rot:Quat::from_rotation_z(heading),parts:parts(),..Default::default()};
            let rows=doll.query_obstacle_pose_contacts(&build,&pose,&obstacle);
            assert!(!rows.is_empty());
            for row in &rows{
                assert_eq!(row.b,0,"sphere at torso height must not hit the map-only component or feet");
                // Independent radial geometry: original torso radius .25,
                // sphere radius .10, centres .28 apart. Reversing a GJK/EPA
                // query is not an exact oracle (its initial axis differs).
                assert!((row.gap+0.07).abs()<0.0001,"heading={heading} child={} gap={}",row.b,row.gap);
                assert!((row.normal-owner.transform()*Vec3::X).length()<0.002);
            }
            assert_eq!(doll.frames().next().unwrap(),pose[0]);
            doll.contact_off=1;assert!(doll.query_obstacle_pose_contacts(&build,&pose,&obstacle).is_empty());
        }
    }
    fn init(){let p=std::env::var("NM_TEST_ASSET").unwrap_or_else(|_|format!("{}/fred.xrt",std::env::var("NM_TEST_ASSETS").unwrap()));let a=delirium_data::nm_asset::read(&std::fs::read(p).unwrap());delirium_data::kv::unlock(&a);}
    #[test]fn waiting_for_room_keeps_nm_until_placement_committed(){
        let mut owner=Owner::Nm;
        let mut fit=FittedPlacement{root:Vec3::new(3.,4.,1.),heading:0.3,ground_fitted:true,room_clear:false,placement_committed:false};
        assert!(!owner.handback(fit,Vec3::X));assert_eq!(owner,Owner::Nm);
        fit.room_clear=true;assert!(!owner.handback(fit,Vec3::X));
        fit.placement_committed=true;assert!(owner.handback(fit,Vec3::X));
        let Owner::Animated(body)=owner else{panic!()};assert_eq!(body.root,fit.root);assert_eq!(body.velocity,Vec3::X);
        assert_eq!(body.inverse_mass(),0.04);assert_eq!(body.body().inv_inertia,[[0.;3];3]);
        // No second handback from an old fit after a body is already animated.
        assert!(!owner.handback(fit,Vec3::ZERO));owner.accepted_nm_response();assert_eq!(owner,Owner::Nm);
    }
    #[test]fn getup_hand_motion_cannot_produce_locomotor_impulse(){
        init();let body=AnimatedOwner{root:Vec3::new(0.,0.,1.),heading:0.,velocity:Vec3::ZERO};
        let shape=gjk::Shape::Capsule(0.18,0.1);let centre=Vec3::new(0.34,0.,0.5);
        let original=fragment_contacts(body,shape,Mat3::IDENTITY,centre,Vec3::ZERO,Vec3::ZERO);assert!(!original.is_empty());
        // A moving 21-bone display/targeting palette remains independent of
        // locomotion ownership, even during the prone start of the get-up.
        let mut bones=[Vec3::ZERO;21];
        for frame in 0..240{
            bones[13]=Vec3::new(frame as f32*0.01,0.5,(frame as f32*0.2).sin());
            let current=fragment_contacts(body,shape,Mat3::IDENTITY,centre,Vec3::ZERO,Vec3::ZERO);
            assert_eq!(current.len(),original.len());
            for(a,b)in current.iter().zip(&original){assert_eq!(a.child,b.child);assert_eq!(a.a,b.a);assert_eq!(a.normal,b.normal);assert_eq!(a.gap,b.gap);assert_eq!(a.closing,0.);}
        }
        // A real moving root still produces a closing contact; no collision was
        // removed to achieve the absence of animation-generated energy.
        let moving=fragment_contacts(AnimatedOwner{velocity:Vec3::X,..body},shape,Mat3::IDENTITY,centre,Vec3::ZERO,Vec3::ZERO);
        assert!(moving.iter().any(|c|c.closing>0.5));
        assert!(original.iter().all(|c|c.child!=2),"map-only capsule must not collide with fragment0x40");
    }
    #[test]fn body_geometry_is_original_composite_not_a_standing_generic_capsule(){
        init();let p=parts();assert_eq!(p.len(),4);
        assert!(matches!(p[0].shape,gjk::Shape::Curved(..)));assert!(matches!(p[1].shape,gjk::Shape::Capsule(..)));
        assert_eq!(p[0].at.z,0.085);assert_eq!(p[1].at.z,-0.875);assert_eq!(p[3].at.z,-0.55);
        let b=pedbodies::Body::ped([0.,0.,1.],0.,[0.;3]);let mut v=vec![b,pedbodies::Body::ped([0.4,0.,1.],0.,[0.;3])];
        let cs=pedbodies::contacts(&v,4);assert!(!cs.is_empty());
        let(moves,j)=pedbodies::step(&mut v,&cs,1./60.);assert!(moves.iter().flatten().any(|x|*x!=0.)||j.iter().any(|x|*x!=0.));
        assert!(v[0].vel[0]<=0.&&v[1].vel[0]>=0.);
    }
    #[test]fn root_compound_sweep_preserves_floor_tangent_stops_wall_and_corner(){
        init();let owner=AnimatedOwner{root:Vec3::Z,heading:0.37,velocity:Vec3::ZERO};
        let mut w=Collision::default();w.add_floor(0.,0.,0.,10.,0);
        let(m,n)=sweep(owner,&w,&[],Vec3::new(1.,0.4,0.),1);assert!(m.distance(Vec3::new(1.,0.4,0.))<0.001,"floor trapped compound {m:?} {n}");
        w.add(&[[1.,-5.,-2.],[1.,5.,-2.],[1.,5.,4.],[1.,-5.,4.]],&[[0,1,2],[0,2,3]],&[0,0]);
        let(m,n)=sweep(owner,&w,&[],Vec3::new(2.,0.4,0.),1);assert!(n>0&&m.x>0.6&&m.x<0.76&&m.y>0.39,"wall {m:?} {n}");
        w.add(&[[-5.,1.,-2.],[5.,1.,-2.],[5.,1.,4.],[-5.,1.,4.]],&[[0,1,2],[0,2,3]],&[0,0]);
        let(mut at,mut count)=(owner.root,0);for _ in 0..180{let(m,n)=sweep(AnimatedOwner{root:at,..owner},&w,&[],Vec3::new(0.03,0.03,0.),1);at+=m;count+=n;}
        assert!(count>0&&at.x<0.76&&at.y<0.76&&at.x>0.6&&at.y>0.6,"corner {at:?}");
    }
    #[test]fn original_room_probe_blocks_wall_vehicle_and_peer_not_floor_or_self(){
        init();let mut world=Collision::default();world.add_floor(0.,0.,0.,10.,0);
        let root=Vec3::Z;assert!(room_clear(&world,&[],root,1));
        let car=Obstacle{id:0,at:Vec3::new(0.3,0.,1.),centre:Vec3::new(0.3,0.,1.),half:Vec3::new(0.1,1.,1.),rot:Quat::IDENTITY,owner:Some(3),..Default::default()};
        assert!(!room_clear(&world,&[car],root,1));assert!(room_clear(&world,&[car],root,3));
        let peer=Obstacle{owner:Some(4),at:Vec3::new(0.4,0.,1.),rot:Quat::IDENTITY,parts:parts(),..Default::default()};
        assert!(!room_clear(&world,&[peer],root,1));
        world.add(&[[0.2,-2.,0.],[0.2,2.,0.],[0.2,2.,3.],[0.2,-2.,3.]],&[[0,1,2],[0,2,3]],&[0,0]);
        assert!(!room_clear(&world,&[],root,1));
    }
}
