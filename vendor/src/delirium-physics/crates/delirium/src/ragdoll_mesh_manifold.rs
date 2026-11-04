//! Moving host mesh: all source polygons, one manifold per physical component.
//! TU8 829C3A40/829C2CF8 feed polygons to the same manifold; 8296E1F8 matches
//! old points, 8296E288 adds up to six, 8296DCF8 replaces by covered area.
//! The existing `hold` implements that policy. This fixes ownership of the
//! manifold; it does not simplify the mesh or change any solver setting.
use super::*;
#[cfg(test)]thread_local!{pub(super) static ENABLED:std::cell::Cell<bool>=const{std::cell::Cell::new(true)};}
#[cfg(test)]thread_local!{pub(super) static SAT_ENABLED:std::cell::Cell<bool>=const{std::cell::Cell::new(true)};}
fn sat_enabled()->bool{
    #[cfg(test)]{return SAT_ENABLED.with(|v|v.get())}
    #[cfg(not(test))]{!(env_on!("GTASA_PASSIVE_TEST_WINDOW")&&env_on!("GTASA_MESH_SAT_BASELINE"))}
}

pub(super) fn component(ob:&Obstacle)->bool {
    #[cfg(test)]if !ENABLED.with(|v|v.get()){return false}
    !ob.parts.is_empty()&&ob.parts.iter().all(|p|matches!(p.shape,gjk::Shape::Tri(..))||matches!(p.shape,gjk::Shape::Hull(v,_) if v.len()==3))
}

// The same four-axis triangle/box rejection already used by world meshes
// (TU8 829C25D8). Here the box follows the relative translation used by the
// EXISTING fixed-orientation convex cast, including both rounded margins and
// contact reach. No triangle/edge/contact normal is approximated or replaced.
pub(super) fn triangle_allows(ha:Vec3,fa:(Vec3,Mat3),sa:(Vec3,Mat3),c:gjk::Shape,fc:(Vec3,Mat3),sc:(Vec3,Mat3))->bool{
    let(t,margin)=match c{gjk::Shape::Tri(t,m)=>(t,m),gjk::Shape::Hull(v,m)if v.len()==3=>([v[0],v[1],v[2]],m),_=>return true};
    let moved=(fa.0-sa.0)-(fc.0-sc.0);
    let centre=fa.0-moved*0.5;
    let t=t.map(|v|fc.0+fc.1*v);
    let magnitude=t.iter().map(|v|v.abs().max_element()).fold(centre.abs().max_element().max(1.),f32::max);
    let half=ha+(fa.1.transpose()*moved).abs()*0.5+Vec3::splat(MARGIN()+margin+64.*f32::EPSILON*magnitude);
    if !centre.is_finite()||!half.is_finite()||half.min_element()<0.{return true}
    box_meets_tri(centre,fa.1,half,t)
}

pub(super) fn update(held:&mut Vec<Held>,(a,ha,fa,sa):(gjk::Shape,Vec3,(Vec3,Mat3),(Vec3,Mat3)),component:(Vec3,Mat3),
    children:impl IntoIterator<Item=(usize,gjk::Shape,Vec3,(Vec3,Mat3),(Vec3,Mat3))>)->usize {
    let(xa,qa)=fa;let(xc,qc)=component;let mut calls=0;
    for(child,c,hc,fc,sc)in children {
        if sat_enabled()&&!triangle_allows(ha,fa,sa,c,fc,sc){continue}
        calls+=1;
        let moved=(xa-sa.0)-(fc.0-sc.0);
        let fast=(qa.transpose()*moved).abs().cmpgt(ha).any()||(fc.1.transpose()*moved).abs().cmpgt(hc).any();
        // The original triangle callback only suppresses a cast for a polygon
        // that already has a contact, not when another polygon has one.
        let found=if fast&&!held.iter().any(|r|r.inst==child as u32){
            Ragdoll::cast_shapes((&a,&c),(sa.0,xa,qa),(sc.0,fc.0,fc.1)).map(|(pa,pc,n)|{
                let(pa,pc)=(xa+qa*pa,fc.0+fc.1*pc);(pa,pc,n,(pa-pc).dot(n))
            })
        }else{
            Ragdoll::shape_contact((&a,&c),fa,fc,MARGIN(),Vec3::Z).map(|q|(q.a,q.b,q.n,q.dist))
        };
        if let Some((pa,pc,n,gap))=found {
            hold(held,Held{local:qa.transpose()*(pa-xa),ground:qc.transpose()*(pc-xc),gap,age:0,m:0,fl:0,imp:Vec3::ZERO,pimp:Vec3::ZERO,n,inst:child as u32,off:false});
        }
    }
    // Refresh once per component. Ageing/culling once per triangle would make
    // persistence, warm starts and slip depend on render tessellation density.
    for r in held.iter_mut(){if r.age!=0{r.gap=(xa+qa*r.local-xc-qc*r.ground).dot(r.n);}r.age+=if r.age==1{2}else{1};}
    let(mut k,mut worst)=(0,(None,0.));
    while k<held.len(){
        if held[k].gap>=MARGIN(){held.swap_remove(k);continue}
        let r=&held[k];let d=xc+qc*r.ground-xa-qa*r.local;
        let slid=(d-r.n*d.dot(r.n)).length_squared();
        if r.age>delirium_data::kv!(1 int)&&slid>worst.1&&slid>delirium_data::kv!(568){worst=(Some(k),slid);}
        k+=1;
    }
    if let Some(k)=worst.0{held.swap_remove(k);}
    calls
}

#[cfg(test)]mod tests{
    use super::*;
    fn grid(n:usize,hole:bool)->Vec<(usize,gjk::Shape,Vec3,(Vec3,Mat3),(Vec3,Mat3))>{
        let mut out=vec![];
        for y in 0..n{for x in 0..n{
            let l=Vec3::new(-1.+2.*x as f32/n as f32,-1.+2.*y as f32/n as f32,0.);
            let d=2./n as f32;
            if hole&&l.x>=-0.5&&l.x<0.5&&l.y>=-0.5&&l.y<0.5{continue}
            for t in [[l,l+Vec3::X*d,l+(Vec3::X+Vec3::Y)*d],[l,l+(Vec3::X+Vec3::Y)*d,l+Vec3::Y*d]]{
                out.push((out.len(),gjk::Shape::Tri(t,0.),Vec3::splat(2.),(Vec3::ZERO,Mat3::IDENTITY),(Vec3::ZERO,Mat3::IDENTITY)));
            }
        }}out
    }
    #[test]fn tessellation_is_not_a_separate_physical_body_per_triangle(){
        moving_obstacle_regression::constants();
        for n in [1,4,8,16]{
            let children=grid(n,false);let a=(gjk::Shape::Capsule(0.,0.2),Vec3::splat(0.2),(Vec3::Z*0.195,Mat3::IDENTITY),(Vec3::Z*0.195,Mat3::IDENTITY));let mut held=vec![];
            for _ in 0..8{
                assert!(update(&mut held,a,(Vec3::ZERO,Mat3::IDENTITY),children.clone())>0);
                assert!(!held.is_empty()&&held.len()<=6,"n={n}, contacts={}",held.len());
                assert!(held.iter().any(|r|r.n.z>0.99&&r.gap<0.));
                assert!(held.iter().all(|r|r.age<20),"age must advance per physical step, not per polygon");
            }
        }
    }
    #[test]fn actual_mesh_hole_remains_clear_not_a_convex_hull(){
        moving_obstacle_regression::constants();let mut held=vec![];
        let a=(gjk::Shape::Capsule(0.,0.1),Vec3::splat(0.1),(Vec3::ZERO,Mat3::IDENTITY),(Vec3::ZERO,Mat3::IDENTITY));
        update(&mut held,a,(Vec3::ZERO,Mat3::IDENTITY),grid(8,true));assert!(held.is_empty());
    }
    #[test]fn component_frame_tracks_world_witnesses_and_preserves_warm_start(){
        moving_obstacle_regression::constants();
        let mut held=vec![];let a=(gjk::Shape::Capsule(0.,0.2),Vec3::splat(0.2),(Vec3::Z*0.195,Mat3::IDENTITY),(Vec3::Z*0.195,Mat3::IDENTITY));
        update(&mut held,a,(Vec3::ZERO,Mat3::IDENTITY),grid(1,false));assert!(!held.is_empty());
        for r in &mut held{r.imp=Vec3::new(2.,3.,4.);r.pimp=Vec3::new(0.1,0.2,0.3);}
        let old=held[0];let q=Mat3::from_quat(Quat::from_rotation_y(0.4));let x=Vec3::new(3.,-2.,1.);
        let children=grid(1,false).into_iter().map(|(i,s,h,_,_)|(i,s,h,(x,q),(x,q)));
        let shifted=(a.0,a.1,(x+q*a.2.0,q),(x+q*a.3.0,q));
        update(&mut held,shifted,(x,q),children);
        assert!(held.iter().any(|r|r.imp==old.imp&&r.pimp==old.pimp),"matching old point must retain impulse");
        assert!(held.iter().any(|r|((shifted.2.0+q*r.local)-(x+q*r.ground)).dot(r.n)<0.));
        assert!(held.iter().any(|r|r.n.dot(q*Vec3::Z)>0.99));
    }
    #[test]fn fast_crossing_still_casts_all_polygons_and_keeps_the_barrier(){
        moving_obstacle_regression::constants();let mut held=vec![];
        let q=Mat3::from_quat(Quat::from_rotation_y(std::f32::consts::FRAC_PI_2));
        let children=grid(8,false).into_iter().map(|(i,s,h,_,_)|(i,s,h,(Vec3::ZERO,q),(Vec3::ZERO,q)));
        let a=(gjk::Shape::Capsule(0.,0.2),Vec3::splat(0.2),(Vec3::X*0.5,Mat3::IDENTITY),(-Vec3::X*0.5,Mat3::IDENTITY));
        assert!(update(&mut held,a,(Vec3::ZERO,q),children)>0);
        assert!(!held.is_empty()&&held.len()<=6);
        assert!(held.iter().any(|r|r.n.x< -0.99&&r.gap< -0.4),"clear-to-clear sweep must retain entry-side barrier");
    }
    #[test]fn live_body_stays_supported_on_tessellated_mesh_with_one_manifold(){
        moving_obstacle_regression::constants();
        for n in [1,8]{
            let build=moving_obstacle_regression::finite_probe_build(80.);
            let parts=grid(n,false).into_iter().map(|(_,shape,_,_,_)|{
                let gjk::Shape::Tri(t,_)=shape else{unreachable!()};
                ObPart{shape,m:Mat3::IDENTITY,at:Vec3::ZERO,lo:t.into_iter().fold(Vec3::INFINITY,Vec3::min),hi:t.into_iter().fold(Vec3::NEG_INFINITY,Vec3::max),mesh:None,filter:Default::default()}
            }).collect::<Vec<_>>();
            let obstacle=Obstacle{id:43,at:Vec3::Z*15.,centre:Vec3::Z*15.,parts:Box::leak(parts.into_boxed_slice()),..Default::default()};
            let mut doll=Ragdoll::new(&build,&[Transform::from_translation(Vec3::Z*15.195)],-Vec3::Z);
            for _ in 0..60{doll.step(&build,&Collision::default(),1./120.,Drive{lift:1.,..Default::default()},std::slice::from_ref(&obstacle),&[]);}
            assert!(doll.bodies[0].x.z+doll.o.z>15.18,"mesh support failed: {:?}",doll.bodies[0].x+doll.o);
            assert!(doll.held_car.len()<=1,"triangle count leaked into physical pair identity");
            assert!(doll.held_car.iter().any(|m|!m.3.is_empty()));
        }
    }
    #[test]fn triangle_rejection_contains_narrowphase_contacts_and_original_casts(){
        moving_obstacle_regression::constants();
        let mut seed=345981u32;let mut draw=||{seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);(seed>>8)as f32/(1u32<<24)as f32};
        let(mut hits,mut skipped,mut casts)=(0,0,0);
        for i in 0..20000{
            let q=Mat3::from_quat(Quat::from_euler(EulerRot::XYZ,draw()*6.,draw()*6.,draw()*6.));
            let cq=Mat3::from_quat(Quat::from_euler(EulerRot::XYZ,draw()*6.,draw()*6.,draw()*6.));
            let offset=Vec3::new(draw()-0.5,draw()-0.5,draw()-0.5)*if i%3==0{10000.}else{5.};
            let x=offset+Vec3::new(draw()-0.5,draw()-0.5,draw()-0.5);
            let t=std::array::from_fn(|_|Vec3::new(draw()-0.5,draw()-0.5,draw()-0.5));
            let half=draw()*0.5;let radius=0.1+draw()*0.2;let ha=Vec3::new(radius,half+radius,radius);
            let a=gjk::Shape::Capsule(half,radius);let c=gjk::Shape::Tri(t,0.005);
            let fa=(x,q);let fc=(offset,cq);
            let sa=(x+Vec3::new(draw()-0.5,draw()-0.5,draw()-0.5),q);let sc=(offset+Vec3::new(draw()-0.5,draw()-0.5,draw()-0.5),cq);
            let allows=triangle_allows(ha,fa,sa,c,fc,sc);
            skipped+=usize::from(!allows);
            if Ragdoll::shape_contact((&a,&c),fa,fc,MARGIN(),Vec3::Z).is_some(){hits+=1;assert!(allows,"lost native closest-point contact at{i}");}
            if i<2000&&Ragdoll::cast_shapes((&a,&c),(sa.0,fa.0,q),(sc.0,fc.0,cq)).is_some(){casts+=1;assert!(allows,"lost native cast at{i}");}
        }
        assert!(hits>500&&skipped>500&&casts>100,"insufficient real hit/miss/sweep coverage: {hits}/{skipped}/{casts}");
    }
}
