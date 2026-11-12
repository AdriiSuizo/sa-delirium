//! Inspect the physical parameters of an owned IV automobile.
use delirium_data::{
    archive_key::ArchiveKey, bounds, disc::Disc, img, kv, model, resource, rpf, vehicles,
};
use serde_json::json;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args_os().skip(1);
    let iso = PathBuf::from(
        args.next()
            .ok_or("usage: vehicle_iv_reference ISO IMPORTED_PROGRAM MODEL [PROFILE_PATH]")?,
    );
    let program = std::fs::read(args.next().ok_or("missing imported program")?)?;
    let name = args
        .next()
        .ok_or("missing vehicle model")?
        .into_string()
        .map_err(|_| "invalid model name")?;
    if !kv::unlock_program(&program) {
        return Err("unsupported owned IV program".into());
    }
    let disc = Disc::open(&iso)?;
    let header = disc.read_at("xbox360.rpf", 0, 0x810)?;
    let (key, _) = ArchiveKey::find_by_trial(&program, &header).ok_or("archive key not found")?;
    let common = disc.read("common.rpf")?;
    let files = rpf::read_toc(&common, &key)?;
    let handling_entry = files
        .iter()
        .find(|entry| entry.path.eq_ignore_ascii_case("data/handling.dat"))
        .ok_or("handling.dat missing")?;
    let bytes = rpf::read(&common, handling_entry)?;
    let handling = vehicles::read_handling(&String::from_utf8_lossy(&bytes));
    if name == "--handling-list" {
        let mut names: Vec<_> = handling.keys().collect();
        names.sort_unstable();
        for name in names {
            println!("{name}");
        }
        return Ok(());
    }
    const ARCHIVE: &str = "xbox360/models/cdimages/vehicles.img";
    let header = disc.read_at(ARCHIVE, 0, 20)?;
    let len = img::table_len(&header, Some(&key))
        .filter(|len| *len <= 8 << 20)
        .ok_or("unsupported vehicle archive")?;
    let entries = img::read_table(&disc.read_at(ARCHIVE, 0, len)?, Some(&key))?;
    let fragment_name = format!("{name}.xft");
    let entry = entries
        .iter()
        .find(|entry| entry.name.eq_ignore_ascii_case(&fragment_name))
        .ok_or("model missing from owned archive")?;
    let data = disc.read_at(ARCHIVE, entry.offset, usize::from(entry.used_blocks) * 2048)?;
    let fragment = resource::parse(&data)?;
    let inertia = model::fragment_inertia(&fragment)?;
    let damping = model::fragment_damping(&fragment)?;
    let (geometry, skipped) = bounds::fragment(&fragment, false)?;
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for vertex in geometry.iter().flat_map(|mesh| &mesh.positions) {
        for axis in 0..3 {
            min[axis] = min[axis].min(vertex[axis]);
            max[axis] = max[axis].max(vertex[axis]);
        }
    }
    let dimensions = std::array::from_fn::<_, 3, _>(|i| max[i] - min[i]);
    let tuning = handling
        .get(&name.to_ascii_uppercase())
        .ok_or("model has no same-name handling")?;
    let profile = serde_json::to_string_pretty(&json!({
            "model": name, "mass_kg": tuning.mass, "inertia_per_kg": inertia, "fragment_damping": damping,
            "collision_dimensions_m": dimensions, "collision_parts_skipped": skipped,
            "centre_of_mass_m": tuning.centre_of_mass, "spring_force": tuning.suspension_force,
            "compression_damping": tuning.suspension_comp_damp, "rebound_damping": tuning.suspension_rebound_damp,
            "upper_stop_m": tuning.suspension_upper, "lower_stop_m": tuning.suspension_lower,
            "spring_axle_bias": tuning.suspension_bias
    }))?;
    if let Some(path) = args.next() {
        use std::io::Write;
        std::fs::File::create_new(path)?.write_all(profile.as_bytes())?;
    } else {
        println!("{profile}");
    }
    Ok(())
}
