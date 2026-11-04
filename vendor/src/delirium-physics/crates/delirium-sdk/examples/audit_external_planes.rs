fn main(){
 let a:Vec<_>=std::env::args_os().collect();assert_eq!(a.len(),3,"assets capture.json");
 let bytes=std::fs::read(std::path::Path::new(&a[1]).join("fred.xrt")).unwrap();assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
 let result=delirium_physics::external_replay::audit_replay_planes(std::path::Path::new(&a[2])).unwrap();println!("{}",serde_json::to_string_pretty(&result).unwrap());
}
