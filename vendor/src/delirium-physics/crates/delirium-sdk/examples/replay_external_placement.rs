//! Offline exact placement replay; never launches the game or changes saves.
fn main(){
    let args:Vec<_>=std::env::args_os().collect();
    if args.len()!=3{eprintln!("usage: replay_external_placement <DeliriumSA-assets> <captured-replay.json>");std::process::exit(2)}
    let assets=std::path::Path::new(&args[1]);
    let bytes=std::fs::read(assets.join("fred.xrt")).expect("read original solver constants");
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)),"unlock constants");
    let result=delirium_physics::external_replay::replay_file(std::path::Path::new(&args[2])).expect("replay captured exact scene");
    println!("{}",serde_json::to_string_pretty(&result).unwrap());
    let entries=result["replays"].as_array().expect("replay results");
    if entries.is_empty()||entries.iter().any(|r|r["exact_match"]!=true){std::process::exit(1)}
}
