//! Replay geometry-query CPU work against a captured native SA COL mesh.
//! No actor state, collision tolerances, quality, or engine time is changed.
use delirium::{collision::Collision,prelude::*};
use std::{fs,path::Path,time::Instant,hint::black_box};
fn main()->Result<(),Box<dyn std::error::Error>>{
 let args:Vec<_>=std::env::args().skip(1).collect();let assets=Path::new(&args[0]);
 assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&fs::read(assets.join("fred.xrt"))?)));
 let bytes=fs::read(&args[1])?;let n=u32::from_le_bytes(bytes[..4].try_into()?)as usize;assert_eq!(bytes.len(),4+n*37);
 let verts:Vec<[f32;3]>=bytes[4..4+n*36].chunks_exact(12).map(|v|std::array::from_fn(|k|f32::from_le_bytes(v[k*4..k*4+4].try_into().unwrap()))).collect();
 let tris:Vec<[u32;3]>=(0..n as u32).map(|i|[i*3,i*3+1,i*3+2]).collect();
 let mut world=Collision::default();world.add_two_sided(&verts,&tris,&bytes[4+n*36..]);
 let mut qs=vec![];
 for step in 0..50 {for child in 0..28 {
  let at=Vec3::new(2406.+step as f32*0.3,-1658.+(child%4)as f32*0.4-0.6,12.2+(child/4)as f32*0.25);
  let end=at+Vec3::new(0.6,0.02,0.01);
  let e=Vec3::new(0.18,0.18,0.07)+Vec3::splat(0.02+0.05);let exact=(at.min(end)-e,at.max(end)+e);
  qs.push((at.min(end)-Vec3::splat(1.5),at.max(end)+Vec3::splat(1.5),at,exact));
 }}
 let kept=|t:&([Vec3;3],u8,[Vec3;4],u8,u32,delirium::collision::Poly),b:(Vec3,Vec3)|t.0[0].min(t.0[1]).min(t.0[2]).cmple(b.1).all()&&t.0[0].max(t.0[1]).max(t.0[2]).cmpge(b.0).all();
 if args.get(3).is_some_and(|s|s=="parallel"){
  let mut trials=vec![];
  for trial in 0..6{
   let mut world=Collision::default();let build=Instant::now();world.add_two_sided(&verts,&tris,&bytes[4+n*36..]);let build_ms=build.elapsed().as_secs_f64()*1000.;
   for cold in [true,false]{
    let timer=Instant::now();
    let outputs=std::thread::scope(|scope|{
     let handles:Vec<_>=qs.chunks(qs.len().div_ceil(6)).map(|part|{let world=&world;scope.spawn(move||part.iter().map(|&(lo,hi,at,b)|world.tris_near_from_bounded(lo,hi,at,b.0,b.1)).collect::<Vec<_>>())}).collect();
     handles.into_iter().flat_map(|h|h.join().unwrap()).collect::<Vec<_>>()
    });
    let ms=timer.elapsed().as_secs_f64()*1000.;let mut hash=0xcbf29ce484222325u64;let mut count=0;
    let mut word=|v:u32|{hash=(hash^v as u64).wrapping_mul(0x100000001b3);};
    for ts in outputs{word(ts.len() as u32);for(t,mat,dirs,mask,inst,poly) in ts{count+=1;
     for p in t.into_iter().chain(dirs).chain([poly.n]){for x in p.to_array(){word(x.to_bits());}}
     for e in poly.edges{word(u32::from(e.is_some()));if let Some((a,b,n))=e{for p in [a,b,n]{for x in p.to_array(){word(x.to_bits());}}}}
     for x in [mat as u32,mask as u32,inst,poly.first.0,poly.first.1]{word(x);}
    }}
    trials.push(serde_json::json!({"trial":trial,"cold":cold,"build_ms":build_ms,"ms":ms,"retained":count,"all_tuple_bits_hash":format!("{hash:016x}")}));
   }
  }
  let report=serde_json::json!({"queries":qs.len(),"workers":6,"trials":trials});fs::write(&args[2],serde_json::to_vec_pretty(&report)?)?;println!("{report}");return Ok(())
 }
 let mut broad=0;let mut retained=0;
 for &(lo,hi,at,b) in &qs{
  let wide=world.tris_near_from(lo,hi,at);broad+=wide.len();let old=wide.into_iter().filter(|t|kept(t,b)).collect::<Vec<_>>();
  let new=world.tris_near_from_bounded(lo,hi,at,b.0,b.1);assert_eq!(old,new);retained+=new.len();
 }
 assert!(retained>0&&broad>retained,"must cover accepted and rejected native surfaces");
 let mut rows=vec![];
 for run in 0..6{for early in if run%2==0{[false,true]}else{[true,false]}{
  let start=Instant::now();let mut count=0;
  for &(lo,hi,at,b) in &qs{
   if early{count+=black_box(world.tris_near_from_bounded(lo,hi,at,b.0,b.1)).len();}
   else{count+=black_box(world.tris_near_from(lo,hi,at).into_iter().filter(|t|kept(t,b)).collect::<Vec<_>>()).len();}
  }
  assert_eq!(count,retained);rows.push(serde_json::json!({"early":early,"run":run,"ms":start.elapsed().as_secs_f64()*1000.}));
 }}
 let report=serde_json::json!({"triangles":n,"queries":qs.len(),"broad_candidates":broad,"retained_candidates":retained,"complete_tuples_equal":true,"timings":rows});
 fs::write(&args[2],serde_json::to_vec_pretty(&report)?)?;println!("{report}");Ok(())
}
