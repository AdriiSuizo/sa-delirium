//! Isolate world-mesh sidedness/CCD with an actual captured COL surface.
//! No gameplay constants or world geometry are changed by this diagnostic.
use delirium::prelude::*;
use delirium::{ragdoll::{Build,Ragdoll,Drive},collision::Collision};
use std::{fs,path::Path};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let args:Vec<_>=std::env::args().skip(1).collect();let assets=Path::new(&args[0]);
 assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&fs::read(assets.join("fred.xrt"))?)));
 let bytes=fs::read(&args[1])?;let count=u32::from_le_bytes(bytes[..4].try_into()?)as usize;
 assert_eq!(bytes.len(),4+count*37);
 let vertices:Vec<[f32;3]>=bytes[4..4+count*36].chunks_exact(12).map(|v|std::array::from_fn(|k|f32::from_le_bytes(v[k*4..k*4+4].try_into().unwrap()))).collect();
 let triangles:Vec<[u32;3]>=(0..count as u32).map(|i|[i*3,i*3+1,i*3+2]).collect();
 let (source,_)=Build::undump(&fs::read_to_string(assets.join("fred-build.txt"))?);
 let mut part=source.parts[16].clone();part.parent=None;part.bone=0;part.mass=10.;part.boxed=None;part.radius=0.2;part.half=0.;part.offset=Transform::IDENTITY;part.anchor=Vec3::ZERO;part.inertia=Vec3::splat(0.4*part.mass*part.radius*part.radius);
 let build=Build{parts:vec![part],nm:source.nm};
 let input:serde_json::Value=serde_json::from_slice(&fs::read(&args[2])?)?;let mut rows=vec![];
 let vec=|s:&str|{let a:Vec<f32>=s.split(',').map(|x|x.parse().unwrap()).collect();Vec3::new(a[0],a[1],a[2])};
 for two_sided in [false,true]{for reversed in [false,true]{
  let mut world=Collision::default();let tri=if reversed{triangles.iter().map(|t|[t[0],t[2],t[1]]).collect::<Vec<_>>()}else{triangles.clone()};if two_sided{world.add_two_sided(&vertices,&tri,&bytes[4+count*36..]);}else{world.add(&vertices,&tri,&bytes[4+count*36..]);}
  for (ray,r)in input["rays"].as_array().unwrap().iter().enumerate(){
   let normal=vec(r["normal"].as_str().unwrap()).normalize();if normal.z.abs()>0.01{continue}
   let point=vec(r["point"].as_str().unwrap());
   for side in [-1.,1.]{for speed in [1.,4.,15.]{for hz in [30,120]{
    let n=normal*side;let mut doll=Ragdoll::new(&build,&[Transform::from_translation(point+n*0.4)],-n*speed);
    let mut least=0.4f32;let mut peak=0f32;
    // Lift1 only isolates horizontal collision; no feet, gravity or controllers
    // may make an apparent stop hide a missed triangle in this diagnostic.
    for _ in 0..hz{doll.step(&build,&world,1./hz as f32,Drive{lift:1.,..Default::default()},&[],&[]);let p=doll.frames().next().unwrap().translation;least=least.min((p-point).dot(n));peak=peak.max(doll.vel(0).0.length());}
    rows.push(serde_json::json!({"ray":ray,"model":r["model"],"two_sided":two_sided,"reversed":reversed,"side":side,"speed":speed,"hz":hz,"minimum_signed_center_m":least,"peak_mps":peak,"crossed_plane":least< -0.2}));
   }}}
  }
 }
 }
 fs::write(&args[3],serde_json::to_vec_pretty(&rows)?)?;
 println!("{} cases, {} crossed their tested plane",rows.len(),rows.iter().filter(|r|r["crossed_plane"]==true).count());Ok(())
}
