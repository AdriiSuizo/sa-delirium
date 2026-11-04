//! Read-only geometry prototype for the next external-owner/world constraint
//! stage. It does not change production contact generation or host placement.
use delirium::{prelude::*,collision::Collision,ragdoll::{Obstacle,ObPart},gjk};
use serde_json::json;

#[derive(Clone,Copy,Debug)]
struct Contact { point:Vec3,normal:Vec3,gap:f32,relative:Vec3 }
fn query(world:&Collision,ob:&Obstacle,owner_com:Vec3,owner_v:Vec3,owner_w:Vec3)->Vec<Contact>{
    let fallback=[ObPart{shape:gjk::Shape::Box(ob.half,0.),at:Vec3::ZERO,m:Mat3::IDENTITY,lo:-ob.half,hi:ob.half,mesh:None,filter:Default::default()}];
    let parts=if ob.parts.is_empty(){&fallback[..]}else{ob.parts};
    let mut contacts=vec![];let rot=Mat3::from_quat(ob.rot);
    let margin=delirium_data::kv!(565);
    for part in parts {
        let q=rot*part.m;let at=ob.at+rot*part.at;
        let centre=(part.lo+part.hi)*0.5;let half=(part.hi-part.lo)*0.5;
        let radius=half.length()+margin;let centre=at+q*centre;
        for(t,_,dirs,_,_,poly)in world.tris_near(centre-Vec3::splat(radius),centre+Vec3::splat(radius)){
            let tri=gjk::Shape::Tri(t.map(|p|p-t[0]),margin);
            let Some(c)=gjk::closest(&part.shape,q,at-t[0],&tri,Mat3::IDENTITY,Vec3::ZERO,dirs[0],f32::INFINITY,Some(gjk::Pen::Tri(dirs))) else{continue};
            if c.dist>=delirium_data::kv!(109){continue;}
            let mut material=[(c.b+t[0],c.n)];poly.clamp(&mut material,margin);
            let point=c.a+t[0];let normal=material[0].1;
            let relative=ob.vel_at(point)-(owner_v+owner_w.cross(point-owner_com));
            contacts.push(Contact{point,normal,gap:c.dist,relative});
        }
    }
    contacts
}
fn main(){
    let args:Vec<_>=std::env::args().collect();let assets=std::path::Path::new(&args[1]);
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
    let mut floor=Collision::default();floor.add_floor(0.,0.,0.,4.,0);
    let at=Vec3::new(0.,0.,0.63);let half=Vec3::new(0.8,1.8,0.65);
    let owner=Obstacle{id:1,at,centre:at,com:at,half,..Default::default()};
    let hits=query(&floor,&owner,at,Vec3::ZERO,Vec3::ZERO);
    assert!(!hits.is_empty());assert!(hits.iter().any(|h|h.normal.z>0.99&&h.gap<0.));
    let above=Obstacle{at:at+Vec3::Z,centre:at+Vec3::Z,..owner};
    assert!(query(&floor,&above,above.at,Vec3::ZERO,Vec3::ZERO).is_empty());
    // Articulation must remain a prescribed relative velocity when the
    // chassis is finite; the future row must not erase hinge motion.
    let rotating=Obstacle{spin:Vec3::Y*2.,..owner};
    let rotated=query(&floor,&rotating,at,Vec3::ZERO,Vec3::ZERO);
    assert!(rotated.iter().any(|h|h.relative.length()>0.1));
    let mut wall=Collision::default();
    wall.add(&[[0.79,-4.,-2.],[0.79,-4.,4.],[0.79,4.,4.],[0.79,4.,-2.]],&[[0,1,2],[0,2,3]],&[0,0]);
    let wall_hits=query(&wall,&owner,at,Vec3::ZERO,Vec3::ZERO);
    assert!(wall_hits.iter().any(|h|h.normal.x< -0.99&&h.gap<0.));
    let report=json!({"status":"isolated_geometry_prototype_not_active_in_solver","floor_contacts":hits.len(),"wall_contacts":wall_hits.len(),"floor":hits.iter().map(|h|json!({"point":h.point.to_array(),"normal":h.normal.to_array(),"gap":h.gap})).collect::<Vec<_>>(),"relative_component_velocity_preserved":true,"pending":["Shared island rows and reciprocal owner/world impulse","Contact manifold persistence and filtering matching native components","Swept candidate bounds for large proposed split displacements/rotations","Safe host placement and full native support constraints"]});
    std::fs::write(&args[2],serde_json::to_string_pretty(&report).unwrap()).unwrap();println!("External world geometry prototype passed; no production behavior changed");
}
