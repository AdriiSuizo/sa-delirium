use delirium_data::{disc::Disc,archive_key::ArchiveKey,rpf,resource,skeleton,model};
use std::{fs,path::PathBuf,ffi::CString,io::Write};
fn main()->Result<(),Box<dyn std::error::Error>> {
    let args:Vec<_>=std::env::args_os().skip(1).collect();
    if args.len()!=4 {return Err("export_combat_skeleton ISO EXE_MEMORY XRT NEW_OUTPUT_JSON".into())}
    let output=PathBuf::from(&args[3]);if output.exists(){return Err("output exists".into())}
    let xrt=CString::new(PathBuf::from(&args[2]).to_str().ok_or("path encoding")?)?;
    if unsafe{delirium_physics::nm_init(xrt.as_ptr())}!=0{return Err("asset constants unavailable".into())}
    let disc=Disc::open(&PathBuf::from(&args[0]))?;
    let common=disc.paths().into_iter().find(|p|p.ends_with("common.rpf")).ok_or("common.rpf")?;
    let (key,_)=ArchiveKey::find_by_trial(&fs::read(&args[1])?,&disc.read_at(common,0,0x810)?).ok_or("validated archive key unavailable")?;
    let platform=disc.paths().into_iter().find(|p|p.ends_with("xbox360.rpf")).ok_or("xbox360.rpf")?;
    let head=disc.read_at(platform,0,20)?;
    let size=u32::from_le_bytes(head[4..8].try_into()?) as usize;
    let toc=rpf::read_toc(&disc.read_at(platform,0,0x800+size)?,&key)?;
    let e=toc.iter().find(|e|e.path.eq_ignore_ascii_case("models/cdimages/playerped.rpf")).ok_or("playerped.rpf")?;
    if e.compressed || e.resource.is_some(){return Err("unexpected nested archive encoding".into())}
    let archive=disc.read_at(platform,e.offset,e.stored as usize)?;
    let inner=rpf::read_toc(&archive,&key)?;
    let e=inner.iter().find(|e|e.path.eq_ignore_ascii_case("player.xft")).ok_or("player.xft")?;
    let raw=rpf::read(&archive,e)?;let res=resource::parse(&raw)?;
    let bones=skeleton::read(&res,model::fragment_root(&res)?)?;
    let rows:Vec<_>=bones.iter().map(|b|serde_json::json!({"name":b.name,"id":b.id,"parent":b.parent,"translation":b.translation,"rotation":b.rotation})).collect();
    let report=serde_json::json!({"source_iso":args[0].to_string_lossy(),"source_resource":"xbox360.rpf/models/cdimages/playerped.rpf/player.xft","bones":rows});
    let mut f=fs::OpenOptions::new().write(true).create_new(true).open(output)?;
    f.write_all(&serde_json::to_vec_pretty(&report)?)?;
    println!("{} bind bones extracted read-only for retargeting",bones.len());
    Ok(())
}
