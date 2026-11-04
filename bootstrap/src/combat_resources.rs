// SPDX-License-Identifier: GPL-3.0-only
//! Build the existing combat bank from the owner's disc, never from packaged
//! game assets. A bank is usable only after the real parser accepts every input.
use delirium_data::{archive_key::ArchiveKey, disc::Disc, img, model, resource, rpf, skeleton};
use serde_json::json;
use std::{collections::BTreeMap, fs, path::Path};

pub const DICTIONARIES: &[&str] = &[
    "melee_baseball_core", "melee_baseball_extra", "melee_counters",
    "melee_gang_unarmed", "melee_gun", "melee_hits_common", "melee_knife_core",
    "melee_knife_extra", "melee_ped_unarmed", "melee_player_ground",
    "melee_player_unarmed", "melee_unarmed_base", "move_combat_strafe",
    "move_crouch", "move_melee", "move_player", "ped",
];

fn put(dir: &Path, name: &str, bytes: &[u8]) -> Result<(), String> {
    let path=dir.join(name);
    fs::create_dir_all(path.parent().ok_or("invalid resource path")?).map_err(|e|e.to_string())?;
    fs::write(path,bytes).map_err(|e|format!("{name}: {e}"))
}

pub fn import(disc: &Disc, key: &ArchiveKey, dir: &Path) -> Result<(), String> {
    fs::create_dir(dir).map_err(|e|e.to_string())?;
    let common=super::disc_file(disc,"common.rpf",128<<20)?;
    let toc=rpf::read_toc(&common,key)?;
    for (source,dest) in [
        ("data/action_table.csv","action_table.csv"),
        ("data/meleeanims.dat","MeleeAnims.dat"),
        ("data/pedpersonality.dat","pedpersonality.dat"),
        ("data/decision/combat/M_NO_NO.cmb","decision/M_NO_NO.cmb"),
        ("data/decision/R_Norm.ped","decision/R_Norm.ped"),
        ("data/decision/m_empty.ped","decision/m_empty.ped"),
    ] {
        let e=toc.iter().find(|e|e.path.eq_ignore_ascii_case(source))
            .ok_or_else(||format!("Falta {source} en common.rpf; candidatos: {:?}",
                toc.iter().filter(|e|e.path.to_ascii_lowercase().ends_with(&source.rsplit('/').next().unwrap().to_ascii_lowercase())).map(|e|&e.path).collect::<Vec<_>>()))?;
        put(dir,dest,&rpf::read(&common,e)?)?;
    }
    const ANIM: &str="xbox360/anim/anim.img";
    let size=img::table_len(&disc.read_at(ANIM,0,20)?,Some(key))
        .filter(|n|*n<8<<20).ok_or("Índice de animaciones incompatible")?;
    let entries=img::read_table(&disc.read_at(ANIM,0,size)?,Some(key))?;
    for name in DICTIONARIES {
        let name=format!("{name}.xad");
        let entry=entries.iter().find(|e|e.name.eq_ignore_ascii_case(&name))
            .ok_or_else(||format!("Falta la animación {name}"))?;
        // IMG's used length excludes the final sector padding. This reproduces
        // the original development bank's resources byte for byte.
        put(dir,&name,&disc.read_at(ANIM,entry.offset,entry.size() as usize)?)?;
    }
    let header=disc.read_at("xbox360.rpf",0,20)?;
    let size=u32::from_le_bytes(header[4..8].try_into().unwrap()) as usize;
    if size>8<<20 {return Err("Índice de modelos incompatible".into())}
    let toc=rpf::read_toc(&disc.read_at("xbox360.rpf",0,0x800+size)?,key)?;
    let e=toc.iter().find(|e|e.path.eq_ignore_ascii_case("models/cdimages/playerped.rpf"))
        .ok_or("Falta el archivo del jugador")?;
    let archive=disc.read_at("xbox360.rpf",e.offset,e.stored as usize)?;
    let toc=rpf::read_toc(&archive,key)?;
    let e=toc.iter().find(|e|e.path.eq_ignore_ascii_case("player.xft"))
        .ok_or("Falta el esqueleto del jugador")?;
    let fragment=resource::parse(&rpf::read(&archive,e)?)?;
    let bones=skeleton::read(&fragment,model::fragment_root(&fragment)?)?;
    let bones:Vec<_>=bones.iter().map(|b|json!({"id":b.id,"name":b.name,"parent":b.parent,
        "translation":b.translation,"rotation":b.rotation})).collect();
    put(dir,"skeleton.json",&serde_json::to_vec(&json!({"bones":bones})).map_err(|e|e.to_string())?)?;
    put(dir,"sa-contact-ranges.json",include_bytes!("../config/sa-contact-ranges.json"))?;
    Ok(())
}

pub fn validate(dir:&Path)->Result<(usize,usize),String>{
    let read=|name:&str|fs::read(dir.join(name)).map_err(|e|format!("{name}: {e}"));
    let mut data=sa_combat::Data::load(&read("MeleeAnims.dat")?,&read("action_table.csv")?,&read("pedpersonality.dat")?)?;
    data.adapt_reach(&read("sa-contact-ranges.json")?)?;
    // Gym techniques are required here, although the generic SDK allows banks
    // without them. Missing local dependencies must not silently remove a feature.
    let gym:sa_combat::native_animation::File=serde_json::from_slice(&read("sa-gym-clips.json")?).map_err(|e|e.to_string())?;
    if gym.version!=1||gym.clips.len()!=12||gym.clips.iter().any(|c|!c.validate()){
        return Err("Faltan técnicas de gimnasio compatibles".into());
    }
    let bank=sa_combat::animation::Bank::load(dir)?;
    for name in DICTIONARIES {
        if !bank.clips.keys().any(|k|k.starts_with(&format!("{name}/"))){
            return Err(format!("Banco incompleto: {name}"));
        }
    }
    data.install_gym(&bank)?;
    Ok((bank.clips.len(),bank.native.len()))
}

fn manifest_rows(dir:&Path)->Result<BTreeMap<String,String>,String>{
    let mut rows=BTreeMap::new();
    let mut files=vec!["action_table.csv".to_owned(),"MeleeAnims.dat".into(),"pedpersonality.dat".into(),
        "decision/M_NO_NO.cmb".into(),"decision/R_Norm.ped".into(),"decision/m_empty.ped".into(),
        "skeleton.json".into(),"sa-contact-ranges.json".into(),"sa-gym-clips.json".into()];
    files.extend(DICTIONARIES.iter().map(|n|format!("{n}.xad")));
    for name in files {
        let bytes=fs::read(dir.join(&name)).map_err(|e|format!("{name}: {e}"))?;
        rows.insert(name,blake3::hash(&bytes).to_hex().to_string());
    }
    Ok(rows)
}
pub fn seal(dir:&Path)->Result<(),String>{
    let (clips,gym_clips)=validate(dir)?;
    let receipt=json!({"version":1,"files":manifest_rows(dir)?,"clips":clips,"gym_clips":gym_clips});
    put(dir,"manifest.json",&serde_json::to_vec_pretty(&receipt).map_err(|e|e.to_string())?)
}
pub fn check(dir:&Path)->Result<(),String>{
    let receipt:serde_json::Value=serde_json::from_slice(&fs::read(dir.join("manifest.json")).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
    let expected=serde_json::to_value(manifest_rows(dir)?).map_err(|e|e.to_string())?;
    if receipt["version"]!=1||receipt["files"]!=expected{return Err("La caché del combate está incompleta o ha cambiado".into())}
    validate(dir)?;Ok(())
}
