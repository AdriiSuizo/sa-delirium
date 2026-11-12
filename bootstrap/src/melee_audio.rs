// SPDX-License-Identifier: GPL-3.0-only
use super::{CACHE, archive_file, supported_disc};
use std::fs;
use std::path::Path;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

const BANK: &str = "231498DE/3B8F078A"; // RESIDENT/MELEE
const SAMPLES: [(&str, u32); 5] = [
    ("bat.wav", 0x92e28aa7),   // WOODEN_BAT_PED
    ("fist2.wav", 0xbe8971d2), // FIST_2
    ("fist3.wav", 0x90329525), // FIST_3
    ("fist5.wav", 0x666a4191), // FIST_5
    ("face1.wav", 0x3c9c3eea), // FACE_1
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

/// Import short melee effects locally from the owner's disc using vgmstream-cli.
pub fn import_melee_audio(root: &Path, iso: &Path, decoder: &Path) -> Result<(), String> {
    let cache =
        fs::canonicalize(root.join(CACHE)).map_err(|e| format!("CachÃ© de Delirium: {e}"))?;
    let destination = cache.join("melee-audio");
    if destination.exists() {
        return if SAMPLES
            .iter()
            .all(|(name, _)| valid_wave(&destination.join(name)))
        {
            Ok(())
        } else {
            Err("La carpeta melee-audio ya existe y contiene muestras invÃ¡lidas".into())
        };
    }
    let decoder = fs::canonicalize(decoder).map_err(|e| format!("vgmstream-cli: {e}"))?;
    let (disc, _, key) = supported_disc(iso)?;
    let bank = archive_file(&disc, &key, "xbox360/audio/sfx/resident.rpf", BANK)?;
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    let staging = cache.join(format!(".melee-audio-{}-{stamp}", std::process::id()));
    fs::create_dir(&staging).map_err(|e| e.to_string())?;
    let result = (|| {
        let bank_path = staging.join("melee.ivaud");
        fs::write(&bank_path, &bank).map_err(|e| e.to_string())?;
        for (name, hash) in SAMPLES {
            let index = sample_index(&bank, hash)
                .ok_or_else(|| format!("Falta la muestra de GTA IV {name}"))?;
            let path = staging.join(name);
            let mut command = Command::new(&decoder);
            command
                .current_dir(&staging)
                .arg("-s")
                .arg(index.to_string())
                .arg("-o")
                .arg(name)
                .arg("melee.ivaud");
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
