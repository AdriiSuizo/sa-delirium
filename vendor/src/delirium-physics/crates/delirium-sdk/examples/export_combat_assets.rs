//! Read user-owned IV assets; output only into a NEW directory in this project.
//! The source disc/check-out is never opened for writing; no key is persisted.
use delirium_data::{disc::Disc,archive_key::ArchiveKey,img,rpf,resource,anim};
use std::{fs,path::PathBuf,collections::HashSet,ffi::CString};
fn main()->Result<(),Box<dyn std::error::Error>> {
    let args:Vec<_>=std::env::args_os().skip(1).collect();
    if !(4..=5).contains(&args.len()) {return Err("export_combat_assets ISO EXE_MEMORY XRT NEW_OUTPUT_DIR [EXTRA_DICTIONARIES_CSV]".into())}
    let output=PathBuf::from(&args[3]);
    if output.exists() {return Err("output already exists; refusing to merge asset revisions".into())}
    let xrt=CString::new(PathBuf::from(&args[2]).to_str().ok_or("path encoding")?)?;
    if unsafe{delirium_physics::nm_init(xrt.as_ptr())}!=0 {return Err("cannot initialize asset constants".into())}
    let disc=Disc::open(&PathBuf::from(&args[0]))?;
    let common=disc.paths().into_iter().find(|p|p.ends_with("common.rpf")).ok_or("common.rpf not found")?;
    let archive=disc.read(common)?;
    let image=fs::read(&args[1])?;
    let (key,_)=ArchiveKey::find_by_trial(&image,&archive).ok_or("no validated archive key in owned executable image")?;
    let entries=rpf::read_toc(&archive,&key)?;
    let names=["action_table.csv","meleeanims.dat","pedpersonality.dat"];
    let mut files=vec![];
    for name in names {
        let e=entries.iter().find(|e|e.path.to_ascii_lowercase().ends_with(name)).ok_or(format!("missing {name}"))?;
        files.push((name.to_string(),rpf::read(&archive,e)?));
    }
    let groups=delirium_data::meleeanims::read(&files[1].1);
    let mut dictionaries:HashSet<_>=groups.iter().map(|g|String::from_utf8_lossy(&g.dictionary).to_ascii_lowercase()).collect();
    dictionaries.insert("move_melee".into());
    dictionaries.insert("ped".into()); // original one-hand walking/running shoves
    if let Some(extra)=args.get(4){
        for name in extra.to_str().ok_or("dictionary encoding")?.split(','){
            if name.is_empty()||!name.bytes().all(|b|b.is_ascii_lowercase()||b==b'_'){return Err("invalid extra dictionary".into())}
            dictionaries.insert(name.into());
        }
    }
    let img_path=disc.paths().into_iter().find(|p|p.ends_with("anim.img")).ok_or("anim.img not found")?;
    let head=disc.read_at(img_path,0,20)?;
    let len=img::table_len(&head,Some(&key)).ok_or("invalid animation archive header")?;
    let table=img::read_table(&disc.read_at(img_path,0,len)?,Some(&key))?;
    let mut catalog=vec![];
    for entry in table.iter().filter(|e| dictionaries.contains(e.name.rsplit_once('.').map_or(e.name.as_str(),|p|p.0).to_ascii_lowercase().as_str())) {
        // Archive names are untrusted file data, not paths to write verbatim.
        if entry.name.contains(['/', '\\', ':']) || entry.name==".." {return Err("invalid dictionary basename".into())}
        let bytes=disc.read_at(img_path,entry.offset,entry.size() as usize)?;
        let resource=resource::parse(&bytes)?;
        let clips=anim::read_dictionary(&resource)?;
        for c in clips {
            if c.frames==0 || !c.duration.is_finite() || c.duration<=0. {return Err(format!("invalid clip {}",c.name).into())}
            if c.tracks.iter().flat_map(|t|t.values.iter().flatten()).any(|v|!v.is_finite()) {return Err(format!("non-finite clip {}",c.name).into())}
            catalog.push(serde_json::json!({"dictionary":entry.name,"name":c.name,"hash":c.hash,"frames":c.frames,
                "duration":c.duration,"tracks":c.tracks.iter().map(|t|serde_json::json!({"kind":t.kind,"bone":t.bone})).collect::<Vec<_>>(),
                "events":c.events,"has_mover_rotation":c.mover_rotation.is_some()}));
        }
        files.push((entry.name.clone(),bytes));
    }
    if catalog.is_empty() {return Err("no combat clips decoded".into())}
    fs::create_dir_all(&output)?;
    for (name,bytes) in &files {fs::write(output.join(name),bytes)?;}
    let report=serde_json::json!({"source_iso":args[0].to_string_lossy(),"source_image":args[1].to_string_lossy(),
        "source_img":img_path,"assets":files.iter().map(|(n,b)|serde_json::json!({"file":n,"bytes":b.len()})).collect::<Vec<_>>(),
        "clips":catalog,"retargeted_to_sa":false,"key_persisted":false});
    fs::write(output.join("catalog.json"),serde_json::to_vec_pretty(&report)?)?;
    println!("Decoded {} combat clips from {} dictionaries; preserved source assets; SA retargeting still required",catalog.len(),files.len()-3);
    Ok(())
}
