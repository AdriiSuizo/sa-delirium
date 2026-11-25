// SPDX-License-Identifier: GPL-3.0-only
use super::{CACHE, archive_file, supported_disc};
use std::fs;
use std::path::Path;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

const MELEE_BANK: &str = "231498DE/3B8F078A"; // RESIDENT/MELEE
const MELEE_SAMPLES: [(&str, u32); 5] = [
    ("bat.wav", 0x92e28aa7),   // WOODEN_BAT_PED
    ("fist2.wav", 0xbe8971d2), // FIST_2
    ("fist3.wav", 0x90329525), // FIST_3
    ("fist5.wav", 0x666a4191), // FIST_5
    ("face1.wav", 0x3c9c3eea), // FACE_1
];

const COLLISIONS_BANK: &str = "231498DE/9644F744"; // RESIDENT/COLLISIONS
const CJ_GROUND_SAMPLES: [(&str, u32); 12] = [
    ("body01.wav", 0x292d6225), // BODY_1
    ("body02.wav", 0x3b7906c0), // BODY_2
    ("body03.wav", 0x05349a34), // BODY_3
    ("body05.wav", 0x203e506f), // BODY_5
    ("body06.wav", 0x3277f4e2), // BODY_6
    ("body07.wav", 0xfd8f8b12), // BODY_7
    ("body08.wav", 0x0fd62f9f), // BODY_8
    ("body09.wav", 0xd6f13dd6), // BODY_9
    ("body10.wav", 0x256a71a0), // BODY_10
    ("body11.wav", 0x5d4f6169), // BODY_11
    ("body12.wav", 0xf01806fc), // BODY_12
    ("body13.wav", 0x49e4ba94), // BODY_13
];

fn sample_index(bank: &[u8], hash: u32) -> Option<usize> {
    let offset = usize::try_from(u64::from_be_bytes(bank.get(..8)?.try_into().ok()?)).ok()?;
    let count = u32::from_be_bytes(bank.get(16..20)?.try_into().ok()?) as usize;
    if offset != 28 || !(1..=1024).contains(&count) {
        return None;
    }
    let mut matching = bank
        .get(offset..offset + count * 16)?
        .chunks_exact(16)
        .enumerate()
        .filter(|(_, row)| row[8..12] == hash.to_be_bytes());
    let (index, _) = matching.next()?;
    matching.next().is_none().then_some(index + 1)
}

fn valid_wave(path: &Path) -> bool {
    let Ok(meta) = fs::metadata(path) else {
        return false;
    };
    if !(46..=256 * 1024).contains(&meta.len()) {
        return false;
    }
    let Ok(bytes) = fs::read(path) else {
        return false;
    };
    if bytes.len() < 46 || &bytes[..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
        return false;
    }
    let mut chunks = &bytes[12..];
    let mut format_valid = false;
    while chunks.len() >= 8 {
        let size = u32::from_le_bytes(chunks[4..8].try_into().unwrap()) as usize;
        let Some(body) = chunks.get(8..).and_then(|body| body.get(..size)) else {
            return false;
        };
        match &chunks[..4] {
            b"fmt " if size >= 16 => {
                format_valid = body[..4] == [1, 0, 1, 0] && body[14..16] == [16, 0];
            }
            b"data" => return format_valid && size > 0 && size % 2 == 0,
            _ => {}
        }
        let Some(next) = chunks
            .get(8..)
            .and_then(|body| body.get(size.saturating_add(size % 2)..))
        else {
            return false;
        };
        chunks = next;
    }
    false
}

fn import_bank_samples(
    cache: &Path,
    folder: &str,
    bank_rpf_path: &str,
    bank_file: &str,
    samples: &[(&str, u32)],
    disable_loop: bool,
    disc: &delirium_data::disc::Disc,
    key: &delirium_data::archive_key::ArchiveKey,
    decoder: &Path,
) -> Result<(), String> {
    let destination = cache.join(folder);
    if destination.exists() {
        return if samples
            .iter()
            .all(|(name, _)| valid_wave(&destination.join(name)))
        {
            Ok(())
        } else {
            Err(format!(
                "La carpeta {folder} ya existe y contiene muestras inválidas"
            ))
        };
    }
    let bank = archive_file(disc, key, "xbox360/audio/sfx/resident.rpf", bank_rpf_path)?;
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    let staging = cache.join(format!(".{folder}-{}-{stamp}", std::process::id()));
    fs::create_dir(&staging).map_err(|e| e.to_string())?;
    let result = (|| {
        let bank_path = staging.join(bank_file);
        fs::write(&bank_path, &bank).map_err(|e| e.to_string())?;
        for &(name, hash) in samples {
            let index = sample_index(&bank, hash)
                .ok_or_else(|| format!("Falta la muestra de GTA IV {name}"))?;
            let path = staging.join(name);
            let mut command = Command::new(decoder);
            command.current_dir(&staging);
            if disable_loop {
                command.arg("-i");
            }
            command
                .arg("-s")
                .arg(index.to_string())
                .arg("-o")
                .arg(name)
                .arg(bank_file);
            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                command.creation_flags(0x0800_0000);
            }
            let output = command
                .output()
                .map_err(|e| format!("vgmstream-cli: {e}"))?;
            if !output.status.success() || !valid_wave(&path) {
                return Err(format!(
                    "No se pudo decodificar {name}: {}",
                    String::from_utf8_lossy(&output.stderr).trim()
                ));
            }
        }
        fs::remove_file(&bank_path).map_err(|e| e.to_string())?;
        fs::rename(&staging, &destination).map_err(|e| e.to_string())
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(&staging);
    }
    result
}

/// Import short melee and CJ ground-impact effects locally from the owner's disc using vgmstream-cli.
pub fn import_melee_audio(root: &Path, iso: &Path, decoder: &Path) -> Result<(), String> {
    let cache =
        fs::canonicalize(root.join(CACHE)).map_err(|e| format!("Caché de Delirium: {e}"))?;
    let melee_ready = cache.join("melee-audio").is_dir()
        && MELEE_SAMPLES
            .iter()
            .all(|(name, _)| valid_wave(&cache.join("melee-audio").join(name)));
    let ground_ready = cache.join("cj-ground-audio").is_dir()
        && CJ_GROUND_SAMPLES
            .iter()
            .all(|(name, _)| valid_wave(&cache.join("cj-ground-audio").join(name)));
    if melee_ready && ground_ready {
        return Ok(());
    }
    let decoder = fs::canonicalize(decoder).map_err(|e| format!("vgmstream-cli: {e}"))?;
    let (disc, _, key) = supported_disc(iso)?;
    if !melee_ready {
        import_bank_samples(
            &cache,
            "melee-audio",
            MELEE_BANK,
            "melee.ivaud",
            &MELEE_SAMPLES,
            false,
            &disc,
            &key,
            &decoder,
        )?;
    }
    if !ground_ready {
        import_bank_samples(
            &cache,
            "cj-ground-audio",
            COLLISIONS_BANK,
            "collisions.ivaud",
            &CJ_GROUND_SAMPLES,
            true,
            &disc,
            &key,
            &decoder,
        )?;
    }
    Ok(())
}
