//! Actual solver regression for native SA's two-sided zero-thickness COL.
use super::*;
fn run(side:f32,speed:f32,hz:u32,host:bool,reverse:bool,origin:Vec3)->f32{
 let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
 assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(root.join("fred.xrt")).unwrap())));
 let (source,_)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
 let mut part=source.parts[16].clone();part.parent=None;part.bone=0;part.mass=3.;part.boxed=None;part.radius=0.2;part.half=0.;part.offset=Transform::IDENTITY;part.anchor=Vec3::ZERO;part.inertia=Vec3::splat(0.048);
 let build=Build{parts:vec![part],nm:source.nm};
 let verts:Vec<_>=[Vec3::new(0.,-3.,-3.),Vec3::new(0.,3.,-3.),Vec3::new(0.,3.,3.),Vec3::new(0.,-3.,3.)].map(|p|(p+origin).to_array()).into();
 let triangles=if reverse{[[0,2,1],[0,3,2]]}else{[[0,1,2],[0,2,3]]};
 let mut world=Collision::default();if host{world.add_two_sided(&verts,&triangles,&[0,0]);}else{world.add(&verts,&triangles,&[0,0]);}
 let mut doll=Ragdoll::new(&build,&[Transform::from_translation(origin+Vec3::X*(side*0.6))],Vec3::X*(-side*speed));let mut least=0.6f32;
 for _ in 0..hz{doll.step(&build,&world,1./hz as f32,delirium::ragdoll::Drive{lift:1.,..Default::default()},&[],&[]);least=least.min((doll.frames().next().unwrap().translation.x-origin.x)*side);}
 least
}
#[test]fn native_col_blocks_both_directions_at_both_rates_and_map_coordinates(){
 for origin in [Vec3::ZERO,Vec3::new(2823.,-1783.,11.)]{for side in [-1.,1.]{for speed in [1.,4.,15.]{for hz in [30,120]{for reverse in [false,true]{
  let least=run(side,speed,hz,true,reverse,origin);assert!(least>0.15,"native COL crossed or penetrated: side{side} speed{speed} hz{hz} reversed{reverse} origin{origin:?} signed{least}");
 }}}}}
}
#[test]fn default_iv_import_keeps_its_original_one_sided_contract(){
 assert!(run(1.,4.,120,false,false,Vec3::ZERO)>0.15);
 assert!(run(-1.,4.,120,false,false,Vec3::ZERO)< -1.,"baseline must exercise the backside passage");
}
