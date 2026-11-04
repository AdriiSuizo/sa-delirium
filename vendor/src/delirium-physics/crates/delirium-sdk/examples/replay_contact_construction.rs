use serde::Deserialize;
#[derive(Deserialize)]struct Replay{links:Vec<[f32;16]>,joints:Vec<[f32;10]>,order:Vec<usize>,jacobians:Vec<[Vec<f64>;3]>,expected:Vec<Vec<u64>>,#[serde(default)]jacobian_bits:Vec<[Vec<u64>;3]>}
fn main(){
 let a:Vec<_>=std::env::args_os().collect();assert_eq!(a.len(),3);let root=std::path::Path::new(&a[1]);
 let bytes=std::fs::read(root.join("fred.xrt")).unwrap();assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
 let mut r:Replay=serde_json::from_slice(&std::fs::read(&a[2]).unwrap()).unwrap();
 if !r.jacobian_bits.is_empty(){r.jacobians=r.jacobian_bits.iter().map(|j|j.clone().map(|v|v.into_iter().map(f64::from_bits).collect())).collect();}
 let mut keys=std::collections::HashSet::new();for j in &r.jacobians{keys.insert(j.iter().flatten().map(|x|(*x as f32).to_bits()).collect::<Vec<_>>());}
 println!("native_contact_count={} distinct_response_inputs={}",r.jacobians.len(),keys.len());
 for baseline in [true,false,true,false,true,false]{let start=std::time::Instant::now();let got=delirium::ragdoll::audit_contact_construction(&r.links,&r.joints,&r.order,&r.jacobians,baseline);let ms=start.elapsed().as_secs_f64()*1000.;if got!=r.expected{for (i,(a,b))in got.iter().zip(&r.expected).enumerate(){if a!=b{let first=a.iter().zip(b).position(|(a,b)|a!=b).unwrap();eprintln!("mismatch block={i} field={first} got={} expected={} got_f64={} expected_f64={}",a[first],b[first],f64::from_bits(a[first]),f64::from_bits(b[first]));break}}std::process::exit(1)}println!("baseline={baseline} ms={ms} bits_verified={}",got.iter().map(Vec::len).sum::<usize>());}
}
