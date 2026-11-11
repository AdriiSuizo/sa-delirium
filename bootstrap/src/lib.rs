// SPDX-License-Identifier: GPL-3.0-only
use bevy_math::{Quat, Vec3};
use delirium::ragdoll::Rig;
use delirium_data::{
    archive_key::ArchiveKey, disc::Disc, img, kv, model, resource, rpf, skeleton,
};
use iso_data::xex;
#[path = "../compatibility/canonical_body.rs"]
mod canonical_body;
mod combat_resources;
mod sa_gym_resources;
mod xenia_xex_key;
mod visual_assets;
use delirium_physics as sdk;
use std::ffi::CString;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool,Ordering};
use std::sync::{Mutex,Condvar};
static RUNTIME_READY:AtomicBool=AtomicBool::new(false);
static RUNTIME_GATE:(Mutex<()>,Condvar)=(Mutex::new(()),Condvar::new());

fn publish_ready(value:bool)->Result<(),String>{
    let _guard=RUNTIME_GATE.0.lock().map_err(|_|"La sincronización de arranque se interrumpió")?;
    RUNTIME_READY.store(value,Ordering::Release);
    if value{RUNTIME_GATE.1.notify_all();}
    Ok(())
}

const PROGRAM: &str = "iv-program.bin";
const CHARACTER: &str = "fred.xrt";
const TASKS: &str = "taskparams.txt";
const BODY: &str = "fred-build.txt";
// Alpha.4's bind-body cache remains recoverable. The adapted runtime needs
// the canonical idle body; never silently accept the older body's 21 parts.
const CACHE: &str = "DeliriumSA-local-v3";
const WEAPON_INFO: &str = "weaponinfo.xml";
const PARTS: i32 = 21;
const MISSING_ISO: &str = "Selecciona tu ISO de GTA IV para Xbox 360 en el instalador para preparar Delirium";

fn disc_file(disc: &Disc, path: &str, limit: u64) -> Result<Vec<u8>, String> {
    match disc.len(path) {
        Some(size) if (1..=limit).contains(&size) => disc.read(path),
        _ => Err(format!("Falta {path} o su tamaño es incompatible")),
    }
}

fn archive_file(
    disc: &Disc,
    key: &ArchiveKey,
    archive: &str,
    path: &str,
) -> Result<Vec<u8>, String> {
    let data = disc_file(disc, archive, 128 << 20)?;
    let toc = rpf::read_toc(&data, key)?;
    let entry = toc
        .iter()
        .find(|entry| entry.path.eq_ignore_ascii_case(path))
        .ok_or_else(|| format!("Falta {path} en {archive}"))?;
    rpf::read(&data, entry)
}

fn character_build(disc: &Disc, key: &ArchiveKey) -> Result<String, String> {
    const ARCHIVE: &str = "xbox360/models/cdimages/componentpeds.img";
    let header = disc.read_at(ARCHIVE, 0, 20)?;
    let len = img::table_len(&header, Some(key))
        .filter(|size| *size <= 8 << 20)
        .ok_or("Índice de peatones incompatible")?;
    let entries = img::read_table(&disc.read_at(ARCHIVE, 0, len)?, Some(key))?;
    let entry = entries
        .iter()
        .find(|entry| entry.name.eq_ignore_ascii_case("m_y_pmanhat_01.xft"))
        .ok_or("No se encontró el peatón de referencia del SDK")?;
    let bytes = disc.read_at(ARCHIVE, entry.offset, usize::from(entry.used_blocks) * 2048)?;
    let fragment = resource::parse(&bytes)?;
    let bones = skeleton::read(&fragment, model::fragment_root(&fragment)?)?;
    let parts = delirium_data::ragdoll::read(&fragment)?;
    let parents: Vec<_> = bones.iter().map(|bone| bone.parent).collect();
    let bind: Vec<_> = bones
        .iter()
        .map(|bone| {
            (
                Vec3::from(bone.translation),
                Quat::from_array(bone.rotation).normalize(),
            )
        })
        .collect();
    let rig = Rig::new(&parts, &parents, &bind, &bones, delirium::nmrs::HOMER)
        .ok_or("No se pudo construir el cuerpo de referencia")?;
    if rig.build.parts.len() != PARTS as usize {
        return Err("Número de segmentos incompatible".into());
    }
    canonical_body::build(disc,key,rig).map_err(|error|error.to_string())
}

fn sdk_file(path: &Path) -> Result<CString, String> {
    CString::new(path.to_str().ok_or("Ruta no representable en UTF-8")?)
        .map_err(|error| error.to_string())
}

fn sdk_check(code: i32, operation: &str) -> Result<(), String> {
    if code >= 0 {
        Ok(())
    } else {
        Err(format!("{operation} falló con código {code}"))
    }
}

fn activate(cache: &Path) -> Result<(), String> {
    publish_ready(false)?;
    // Clip event decoding reads the original program's constants. On a fresh
    // cache-only process they are not initialized by the importer beforehand.
    if !kv::unlock_program(&fs::read(cache.join(PROGRAM)).map_err(|e|e.to_string())?) {
        return Err("El motor adaptado rechazó el programa de la caché".into());
    }
    combat_resources::check(&cache.join("combat"))?;
    let character = sdk_file(&cache.join(CHARACTER))?;
    let tasks = sdk_file(&cache.join(TASKS))?;
    let body = sdk_file(&cache.join(BODY))?;
    unsafe {
        sdk_check(sdk::nm_init(character.as_ptr()), "nm_init")?;
        sdk_check(
            sdk::nm_load_task_params(tasks.as_ptr()),
            "nm_load_task_params",
        )?;
        let build = sdk::nm_build_load(body.as_ptr());
        if build.is_null() {
            return Err("El SDK rechazó el cuerpo guardado".into());
        }
        let parts = sdk::nm_build_part_count(build);
        sdk::nm_build_destroy(build);
        if parts != PARTS {
            return Err("El SDK rechazó el número de segmentos".into());
        }
    }
    Ok(())
}

/// Native adapters may consume SDK exports only after the first-run worker
/// has completed character/task initialization in this same DLL instance.
#[unsafe(no_mangle)]
pub extern "C" fn sa_delirium_runtime_ready()->i32{
    i32::from(RUNTIME_READY.load(Ordering::Acquire))
}

/// The ASI's worker can sleep until the first-run import is committed. A prompt
/// to copy a multi-GB ISO must not exhaust a one-shot startup deadline and leave
/// the gameplay module permanently inactive after the player clicks Retry.
#[unsafe(no_mangle)]
pub extern "C" fn sa_delirium_wait_ready(timeout_ms:u32)->i32{
    let Ok(guard)=RUNTIME_GATE.0.lock()else{return -1};
    let result=RUNTIME_GATE.1.wait_timeout_while(guard,std::time::Duration::from_millis(u64::from(timeout_ms)),
        |_|!RUNTIME_READY.load(Ordering::Acquire));
    match result{Ok(_)=>i32::from(RUNTIME_READY.load(Ordering::Acquire)),Err(_)=>-1}
}

fn iso_path(root: &Path) -> Result<PathBuf, String> {
    let mut images = Vec::new();
    for file in fs::read_dir(root).map_err(|error| error.to_string())? {
        let file = file.map_err(|error| error.to_string())?;
        if file
            .path()
            .extension()
            .is_some_and(|ext| ext.eq_ignore_ascii_case("iso"))
            && file.path().is_file()
        {
            images.push(file.path());
        }
    }
    match images.as_slice() {
        [image] => Ok(image.clone()),
        [] => Err(MISSING_ISO.into()),
        _ => Err("Hay varias ISO; selecciona la de GTA IV para Xbox 360 en el instalador".into()),
    }
}

pub fn check_iso(iso: &Path) -> Result<(), String> {
    supported_disc(iso).map(|_| ())
}

fn supported_disc(iso: &Path) -> Result<(Disc, Vec<u8>, ArchiveKey), String> {
    let disc = Disc::open(iso).map_err(|error| {
        format!("No se pudo leer la ISO como disco de GTA IV para Xbox 360. Comprueba que es una imagen completa. {error}")
    })?;
    let xex_bytes = disc_file(&disc, "default.xex", 32 << 20)
        .map_err(|error| format!("La ISO no contiene un default.xex de Xbox 360 legible. {error}"))?;
    // Xenia's BSD-licensed format constant is bundled with its notice. The
    // owner's program still comes exclusively from the supplied retail ISO.
    // First installation must not depend on curl, Internet or a shared cache.
    let program = xex::XexImage::parse_with_key(&xex_bytes, &xenia_xex_key::RETAIL_KEY)
        .map_err(|error| format!("No se pudo leer el ejecutable de la ISO. {error}"))?
        .image;
    if !kv::unlock_program(&program) {
        return Err(format!(
            "Este ejecutable de GTA IV no coincide con las compilaciones verificadas de Xbox 360. Huella de default.xex descifrado (BLAKE3): {}",
            blake3::hash(&program)
        ));
    }
    let header = disc.read_at("xbox360.rpf", 0, 0x810)
        .map_err(|error| format!("No se pudo leer xbox360.rpf de la ISO. {error}"))?;
    let (key, _) =
        ArchiveKey::find_by_trial(&program, &header).ok_or("Archivo de Xbox 360 incompatible")?;
    for path in ["common.rpf", "xbox360/anim/anim.img", "xbox360/models/cdimages/componentpeds.img"] {
        if disc.len(path).is_none_or(|size| size == 0) {
            return Err(format!("Falta {path} en la ISO de GTA IV."));
        }
    }
    Ok((disc, program, key))
}

fn import(root: &Path, cache: &Path, iso: &Path) -> Result<(), String> {
    let (disc, program, key) = supported_disc(iso)?;
    let character = archive_file(&disc, &key, "xbox360.rpf", "data/naturalmotion/fred.xrt")?;
    let tasks = archive_file(&disc, &key, "common.rpf", "data/taskparams.txt")?;
    sdk_check(
        unsafe {
            sdk::nm_init_memory(
                character.as_ptr(),
                character
                    .len()
                    .try_into()
                    .map_err(|_| "XRT demasiado grande")?,
            )
        },
        "nm_init_memory",
    )?;
    let build = character_build(&disc, &key)?;

    let staging = cache.with_extension(format!("tmp-{}", std::process::id()));
    fs::create_dir(&staging).map_err(|error| format!("No se pudo preparar la caché: {error}"))?;
    let result = (|| {
        for (name, bytes) in [
            (PROGRAM, program.as_slice()),
            (CHARACTER, &character),
            (TASKS, &tasks),
            (BODY, build.as_bytes()),
        ] {
            fs::write(staging.join(name), bytes).map_err(|error| format!("{name}: {error}"))?;
        }
        combat_resources::import(&disc,&key,&staging.join("combat"))?;
        sa_gym_resources::import(root,&staging.join("combat"))?;
        combat_resources::seal(&staging.join("combat"))?;
        activate(&staging)?;
        fs::rename(&staging, cache)
            .map_err(|error| format!("No se pudo finalizar la caché: {error}"))
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

/// IV's weapon table, from the owner's disc. Optional: outside the sealed combat bank, added to
/// an existing cache when the ISO is at hand, and the game falls back without it.
fn import_weapon_info(cache: &Path, iso: &Path) -> Result<(), String> {
    let disc = Disc::open(iso)?;
    let xex_bytes = disc_file(&disc, "default.xex", 32 << 20)?;
    let program = xex::XexImage::parse_with_key(&xex_bytes, &xenia_xex_key::RETAIL_KEY)?.image;
    let header = disc.read_at("xbox360.rpf", 0, 0x810)?;
    let (key, _) = ArchiveKey::find_by_trial(&program, &header).ok_or("Archivo de Xbox 360 incompatible")?;
    let bytes = archive_file(&disc, &key, "common.rpf", "data/weaponinfo.xml")?;
    fs::write(cache.join(WEAPON_INFO), bytes).map_err(|error| error.to_string())?;
    const ANIM: &str = "xbox360/anim/anim.img";
    let size = delirium_data::img::table_len(&disc.read_at(ANIM, 0, 20)?, Some(&key))
        .filter(|n| *n < 8 << 20).ok_or("Índice de animaciones incompatible")?;
    let entries = delirium_data::img::read_table(&disc.read_at(ANIM, 0, size)?, Some(&key))?;
    let dir = cache.join("injured");
    fs::create_dir_all(&dir).map_err(|error| error.to_string())?;
    for name in ["move_injured_lower.xad", "move_injured_upper.xad"] {
        let entry = entries.iter().find(|e| e.name.eq_ignore_ascii_case(name)).ok_or_else(|| format!("Falta {name}"))?;
        fs::write(dir.join(name), disc.read_at(ANIM, entry.offset, entry.size() as usize)?).map_err(|error| error.to_string())?;
    }
    Ok(())
}

pub fn prepare(root: &Path) -> Result<&'static str, String> {
    prepare_with_iso(root, None)
}

/// Installer entry point: read the owner's ISO in place. Never copy a multi-GB
/// image into the game directory or require the author's development folder.
/// An existing complete cache can be reopened after the image is disconnected.
pub fn prepare_with_iso(root: &Path, supplied_iso: Option<&Path>) -> Result<&'static str, String> {
    publish_ready(false)?;
    if !root.join("gta-sa.exe").is_file() && !root.join("gta_sa.exe").is_file() {
        return Err("La carpeta no contiene el ejecutable de GTA San Andreas".into());
    }
    let cache = root.join(CACHE);
    match cache.try_exists() {
        Ok(true) => {
            activate(&cache)?;
            if !cache.join(WEAPON_INFO).is_file() || !cache.join("injured").is_dir() {
                let located = supplied_iso.map(Path::to_path_buf).or_else(|| iso_path(root).ok());
                if let Some(iso) = located {
                    if let Err(error) = import_weapon_info(&cache, &iso) { eprintln!("weaponinfo: {error}"); }
                }
            }
            publish_ready(true)?;
            Ok("cache_ready")
        }
        Ok(false) => {
            let located;
            let iso = match supplied_iso {
                Some(path) => path,
                None => { located = iso_path(root)?; &located },
            };
            import(root, &cache, iso)?;
            if let Err(error) = import_weapon_info(&cache, iso) { eprintln!("weaponinfo: {error}"); }
            // Publish only after the complete staging tree has been renamed.
            publish_ready(true)?;
            Ok("imported_iso")
        }
        Err(error) => Err(error.to_string()),
    }
}

/// # Safety
/// The caller supplies a UTF-16 path of `length` code units and a writable
/// buffer of `capacity` bytes. Calls must be serialized by the host.
#[cfg(windows)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_delirium_prepare(
    path: *const u16,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    use std::os::windows::ffi::OsStringExt;
    if path.is_null() || length == 0 || output.is_null() || capacity == 0 {
        return -1;
    }
    let path = unsafe { std::slice::from_raw_parts(path, length) };
    let root = PathBuf::from(std::ffi::OsString::from_wide(path));
    let result = std::panic::catch_unwind(|| prepare(&root))
        .unwrap_or_else(|_| Err("El lector de Delirium interrumpió la preparación".into()));
    let (code, message) = match result {
        Ok(status) => (0, status.to_owned()),
        Err(error) if error == MISSING_ISO => (-2, error),
        Err(error) => (-1, error),
    };
    let len = message.len().min(capacity - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(message.as_ptr(), output, len);
        output.add(len).write(0);
    }
    code
}

/// Prepare only authored visual replacements against the owner's original files.
/// The caller registers the emitted physical paths with Ultimate ASI Loader.
#[cfg(windows)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_delirium_prepare_visuals(path:*const u16,length:usize,output:*mut u8,capacity:usize)->i32{
    use std::os::windows::ffi::OsStringExt;
    if path.is_null()||length==0||output.is_null()||capacity==0{return -1;}
    let root=PathBuf::from(std::ffi::OsString::from_wide(unsafe{std::slice::from_raw_parts(path,length)}));
    let result=std::panic::catch_unwind(||visual_assets::prepare(&root)).unwrap_or_else(|_|Err("Visual preparation interrupted".into()));
    let (code,message)=match result{Ok(s)=>(0,s),Err(e)=>(-1,e)};
    let len=message.len().min(capacity-1);
    unsafe{std::ptr::copy_nonoverlapping(message.as_ptr(),output,len);output.add(len).write(0);}code
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn runtime_ready_gate_can_retry_after_timeout_and_wake_a_waiter(){
        publish_ready(false).unwrap();assert_eq!(sa_delirium_wait_ready(1),0);
        let (start,started)=std::sync::mpsc::channel();
        let waiter=std::thread::spawn(move||{start.send(()).unwrap();sa_delirium_wait_ready(5000)});
        started.recv().unwrap();publish_ready(true).unwrap();assert_eq!(waiter.join().unwrap(),1);
        assert_eq!(sa_delirium_wait_ready(0),1);
        publish_ready(false).unwrap();assert_eq!(sa_delirium_wait_ready(0),0);
    }

    #[test]
    #[ignore = "requires an owned ISO hardlink in an isolated test root"]
    fn imports_owned_iso_then_runs_without_it() {
        let root=PathBuf::from(std::env::var_os("SA_DELIRIUM_TEST_ROOT").unwrap());
        assert!(!root.join(CACHE).exists());
        assert_eq!(prepare(&root).unwrap(),"imported_iso");
        fs::rename(root.join("owned.iso"),root.join("owned.iso.parked")).unwrap();
        assert_eq!(prepare(&root).unwrap(),"cache_ready");
        runs_an_initialized_delirium_agent();
    }

    #[test]
    #[ignore = "requires a cache generated locally from a supported owned disc"]
    fn runs_an_initialized_delirium_agent() {
        use std::hash::{DefaultHasher, Hasher};

        let root = PathBuf::from(std::env::var_os("SA_DELIRIUM_TEST_ROOT").unwrap());
        assert_eq!(prepare(&root).unwrap(), "cache_ready");
        let body = sdk_file(&root.join(CACHE).join(BODY)).unwrap();
        unsafe {
            let build = sdk::nm_build_load(body.as_ptr());
            assert!(!build.is_null());
            let agent = sdk::nm_agent_create(build, [0.0, 0.0, 2.0].as_ptr(), 0.0, 0.5);
            assert!(!agent.is_null());
            assert_eq!(sdk::nm_agent_part_count(agent), PARTS);
            let world = sdk::nm_world_create();
            assert!(!world.is_null());
            assert!(sdk::nm_world_add_floor(world, 0.0, 0.0, 0.0, 50.0) >= 0);
            let mut before = [0.0f32; 3];
            let mut rotation = [0.0f32; 4];
            assert!(
                sdk::nm_agent_get_part_transform(
                    agent,
                    0,
                    before.as_mut_ptr(),
                    rotation.as_mut_ptr()
                ) >= 0
            );
            // Exercise the existing adapted SDK's real task API. The newer
            // standalone SDK's task_fall_down/check exports are not its ABI.
            assert_eq!(sdk::nm_agent_task_catch_fall(agent,std::ptr::null()),0);
            assert_eq!(sdk::nm_agent_is_catching_fall(agent),1);
            let mut trajectory = DefaultHasher::new();
            let mut pose = [0.0f32; PARTS as usize * 7];
            for _ in 0..120 {
                assert!(sdk::nm_agent_step(agent, world, 1.0 / 120.0) >= 0);
                assert!(sdk::nm_agent_get_part_transforms(agent, pose.as_mut_ptr(), PARTS) >= 0);
                for value in pose {
                    assert!(value.is_finite());
                    trajectory.write_u32(value.to_bits());
                }
            }
            println!("fall_trajectory={:016x}", trajectory.finish());
            let mut after = [0.0f32; 3];
            assert!(
                sdk::nm_agent_get_part_transform(
                    agent,
                    0,
                    after.as_mut_ptr(),
                    rotation.as_mut_ptr()
                ) >= 0
            );
            assert!(after.iter().chain(&rotation).all(|value| value.is_finite()));
            assert!(before[2] - after[2] > 0.5);
            sdk::nm_agent_destroy(agent);
            sdk::nm_world_destroy(world);
            sdk::nm_build_destroy(build);
        }
    }

    #[test]
    #[ignore = "requires the owner's original development bank and a fresh isolated import"]
    fn imported_combat_bank_matches_development() {
        let root=PathBuf::from(std::env::var_os("SA_DELIRIUM_TEST_ROOT").unwrap());
        let reference=PathBuf::from(std::env::var_os("SA_DELIRIUM_COMBAT_REFERENCE").unwrap());
        assert_eq!(prepare(&root).unwrap(),"cache_ready");
        let imported=root.join(CACHE).join("combat");
        assert_eq!(combat_resources::validate(&imported).unwrap(),(418,12));
        for name in combat_resources::DICTIONARIES {
            let name=format!("{name}.xad");
            assert_eq!(fs::read(imported.join(&name)).unwrap(),fs::read(reference.join(&name)).unwrap(),"{name}");
        }
        for name in ["action_table.csv","MeleeAnims.dat","pedpersonality.dat","sa-contact-ranges.json"] {
            assert_eq!(fs::read(imported.join(name)).unwrap(),fs::read(reference.join(name)).unwrap(),"{name}");
        }
        // PC and console use different line endings; all decision tokens must
        // still match. Bank::load above additionally runs the real decision parser.
        for name in ["M_NO_NO.cmb","R_Norm.ped","m_empty.ped"] {
            let a=fs::read_to_string(imported.join("decision").join(name)).unwrap();
            let b=fs::read_to_string(reference.join("decision").join(name)).unwrap();
            assert_eq!(a.split_whitespace().collect::<Vec<_>>(),b.split_whitespace().collect::<Vec<_>>(),"{name}");
        }
        for (name,key) in [("skeleton.json","bones"),("sa-gym-clips.json","clips")] {
            let a:serde_json::Value=serde_json::from_slice(&fs::read(imported.join(name)).unwrap()).unwrap();
            let b:serde_json::Value=serde_json::from_slice(&fs::read(reference.join(name)).unwrap()).unwrap();
            assert_eq!(a[key],b[key],"{name}");
        }
        // The two C ABI entry points used by the real host must accept the bank.
        let path=sdk_file(&imported).unwrap();
        unsafe {
            let c=sdk::nm_combat_load(path.as_ptr());assert!(!c.is_null());
            assert_eq!(sdk::nm_combat_enable(c,path.as_ptr()),0);
            sdk::nm_combat_destroy(c);
        }
    }
}
