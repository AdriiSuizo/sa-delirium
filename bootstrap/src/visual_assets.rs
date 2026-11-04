// SPDX-License-Identifier: GPL-3.0-only
//! Compose the owner's IMG archives on disk, never inside the distribution.
//! No game addresses or replacement model/texture bytes are compiled here.
use serde_json::{json, Value};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::{self, File, OpenOptions},
    io::{Read, Seek, SeekFrom, Write},
    path::{Component, Path, PathBuf},
};

const SECTOR: u64 = 2048;
const MAX_IMG: u64 = 0xffff_f800; // >4 GiB needs a separately verified host I/O port.
type Result<T> = std::result::Result<T, String>;
fn io<T>(r: std::io::Result<T>) -> Result<T> {
    r.map_err(|e| e.to_string())
}
fn field<'a>(v: &'a Value, k: &str) -> Result<&'a str> {
    v[k].as_str()
        .ok_or_else(|| format!("Missing visual field {k}"))
}
fn number(v: &Value, k: &str) -> Result<u64> {
    v[k].as_u64()
        .ok_or_else(|| format!("Missing visual number {k}"))
}
fn relative(root: &Path, name: &str) -> Result<PathBuf> {
    // Reject drive-relative, UNC, alternate data streams and traversal on every platform.
    if name.is_empty()
        || name.contains(['\\', ':', '\t', '\r', '\n'])
        || name.starts_with('/')
        || name
            .split('/')
            .any(|c| c.is_empty() || c == "." || c == "..")
        || Path::new(name)
            .components()
            .any(|c| !matches!(c, Component::Normal(_)))
    {
        return Err("Unsafe visual path".into());
    }
    Ok(root.join(name))
}
fn existing(root: &Path, name: &str) -> Result<PathBuf> {
    let p = io(relative(root, name)?.canonicalize())?;
    if !p.starts_with(io(root.canonicalize())?) {
        return Err("Visual source escaped its root".into());
    }
    Ok(p)
}
fn fingerprint(path: &Path) -> Result<Value> {
    let m = io(fs::metadata(path))?;
    let t = io(m.modified())?
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| e.to_string())?;
    Ok(json!({"size":m.len(),"mtime_s":t.as_secs(),"mtime_ns":t.subsec_nanos()}))
}
fn hash(path: &Path) -> Result<String> {
    let mut f = io(File::open(path))?;
    let mut h = blake3::Hasher::new();
    let mut b = vec![0; 1 << 20];
    loop {
        let n = io(f.read(&mut b))?;
        if n == 0 {
            break;
        }
        h.update(&b[..n]);
    }
    Ok(h.finalize().to_hex().to_string())
}
fn verify(path: &Path, v: &Value) -> Result<()> {
    if io(fs::metadata(path))?.len() != number(v, "size")? || hash(path)? != field(v, "blake3")? {
        return Err(format!("Visual input integrity failed: {}", path.display()));
    }
    Ok(())
}
#[derive(Clone)]
struct Entry {
    name: String,
    offset: u64,
    size: u64,
    stream: u16,
    archive: u16,
}
fn directory(path: &Path) -> Result<Vec<Entry>> {
    let mut f = io(File::open(path))?;
    let total = io(f.metadata())?.len();
    let mut h = [0; 8];
    io(f.read_exact(&mut h))?;
    let count = u32::from_le_bytes(h[4..].try_into().unwrap()) as u64;
    if &h[..4] != b"VER2" || count == 0 || count > 1_000_000 || 8 + count * 32 > total {
        return Err("Invalid VER2 IMG directory".into());
    }
    let min = (8 + 32 * count).div_ceil(SECTOR) * SECTOR;
    let mut names = BTreeSet::new();
    let mut entries = Vec::new();
    for _ in 0..count {
        let mut b = [0; 32];
        io(f.read_exact(&mut b))?;
        let offset = u32::from_le_bytes(b[..4].try_into().unwrap()) as u64 * SECTOR;
        let stream = u16::from_le_bytes(b[4..6].try_into().unwrap());
        let archive = u16::from_le_bytes(b[6..8].try_into().unwrap());
        let end = b[8..]
            .iter()
            .position(|&x| x == 0)
            .ok_or("Unterminated IMG name")?;
        let name = std::str::from_utf8(&b[8..8 + end])
            .map_err(|e| e.to_string())?
            .to_ascii_lowercase();
        if name.is_empty() || name.contains(['/', '\\', ':']) || !names.insert(name.clone()) {
            return Err("Unsafe/duplicate IMG name".into());
        }
        let size = u64::from(if archive != 0 { archive } else { stream }) * SECTOR;
        if size == 0 || offset < min || offset.checked_add(size).is_none_or(|n| n > total) {
            return Err(format!("IMG entry out of bounds: {name}"));
        }
        entries.push(Entry {
            name,
            offset,
            size,
            stream,
            archive,
        });
    }
    Ok(entries)
}
fn copy_range(source: &mut File, dest: &mut File, offset: u64, size: u64) -> Result<()> {
    io(source.seek(SeekFrom::Start(offset)))?;
    let copied = io(std::io::copy(&mut source.take(size), dest))?;
    if copied != size {
        return Err("Short visual input read".into());
    }
    Ok(())
}
fn compose(base: &Path, pack: &Path, recipe: &Value, out: &Path) -> Result<Value> {
    verify(base, &recipe["base"])?;
    let mut entries = directory(base)?;
    let mut replacements = BTreeMap::new();
    for replacement in recipe["entries"]
        .as_array()
        .ok_or("Missing IMG replacements")?
    {
        let name = field(replacement, "name")?.to_ascii_lowercase();
        if name.is_empty()
            || name.len() > 23
            || !name.is_ascii()
            || name.contains(['/', '\\', ':', '\0'])
            || replacements.insert(name, replacement).is_some()
        {
            return Err("Invalid/duplicate IMG replacement".into());
        }
        verify(&existing(pack, field(replacement, "file")?)?, replacement)?;
    }
    let original_names: BTreeSet<_> = entries.iter().map(|e| e.name.clone()).collect();
    for (name, v) in &replacements {
        if !original_names.contains(name) {
            entries.push(Entry {
                name: name.clone(),
                offset: 0,
                size: number(v, "size")?,
                stream: 0,
                archive: 0,
            });
        }
    }
    let count = entries.len() as u64;
    let start = (8 + 32 * count).div_ceil(SECTOR) * SECTOR;
    let mut pos = start;
    let mut layout = Vec::new();
    for entry in &entries {
        let size = match replacements.get(&entry.name) {
            Some(v) => number(v, "size")?,
            None => entry.size,
        };
        let sectors = size.div_ceil(SECTOR);
        if sectors == 0
            || sectors > u16::MAX as u64
            || pos
                .checked_add(sectors * SECTOR)
                .is_none_or(|n| n > MAX_IMG)
        {
            return Err("Visual IMG exceeds the verified host sector/size limit".into());
        }
        layout.push((pos, size, sectors as u16));
        pos += sectors * SECTOR;
    }
    let mut f = io(OpenOptions::new().write(true).create_new(true).open(out))?;
    io(f.write_all(b"VER2"))?;
    io(f.write_all(&(count as u32).to_le_bytes()))?;
    for (entry, (offset, _, sectors)) in entries.iter().zip(&layout) {
        io(f.write_all(&((*offset / SECTOR) as u32).to_le_bytes()))?;
        // Uncompressed replacements use the native size; untouched entries retain both fields.
        let (stream, archive) = if replacements.contains_key(&entry.name) {
            (*sectors, 0)
        } else {
            (entry.stream, entry.archive)
        };
        io(f.write_all(&stream.to_le_bytes()))?;
        io(f.write_all(&archive.to_le_bytes()))?;
        let mut name = [0; 24];
        name[..entry.name.len()].copy_from_slice(entry.name.as_bytes());
        io(f.write_all(&name))?;
    }
    let mut original = io(File::open(base))?;
    for (entry, (offset, size, _)) in entries.iter().zip(&layout) {
        io(f.seek(SeekFrom::Start(*offset)))?;
        if let Some(v) = replacements.get(&entry.name) {
            let mut input = io(File::open(existing(pack, field(v, "file")?)?))?;
            copy_range(&mut input, &mut f, 0, *size)?;
        } else {
            copy_range(&mut original, &mut f, entry.offset, *size)?;
        }
    }
    io(f.set_len(pos))?;
    io(f.sync_all())?;
    drop(f);
    // Re-read the actual emitted directory and every entry bound before committing.
    if directory(out)?.len() != entries.len() {
        return Err("Generated IMG directory mismatch".into());
    }
    Ok(
        json!({"entries":entries.len(),"replacements":replacements.len(),"size":pos,"blake3":hash(out)?}),
    )
}

fn compose_patch(root: &Path, pack: &Path, item: &Value, out: &Path) -> Result<()> {
    let base = existing(root, field(item, "target")?)?;
    verify(&base, &item["base"])?;
    let size = number(&item["base"], "size")?;
    let mut cursor = 0;
    let mut original = io(File::open(base))?;
    let mut output = io(OpenOptions::new().create_new(true).write(true).open(out))?;
    for splice in item["splices"].as_array().ok_or("Missing file splices")? {
        let offset = number(splice, "offset")?;
        let removed = number(splice, "delete")?;
        if offset < cursor || offset > size || removed > size - offset {
            return Err("Overlapping/out-of-bounds visual patch".into());
        }
        copy_range(&mut original, &mut output, cursor, offset - cursor)?;
        let source = existing(pack, field(splice, "file")?)?;
        verify(&source, splice)?;
        let mut inserted = io(File::open(source))?;
        copy_range(&mut inserted, &mut output, 0, number(splice, "size")?)?;
        cursor = offset + removed;
    }
    copy_range(&mut original, &mut output, cursor, size - cursor)?;
    io(output.sync_all())?;
    drop(output);
    if io(fs::metadata(out))?.len() != number(item, "result_size")?
        || hash(out)? != field(item, "result_blake3")?
    {
        return Err("Composed visual file failed its result hash".into());
    }
    Ok(())
}

pub fn prepare(root: &Path) -> Result<String> {
    let pack = root.join("SAStandalone-assets");
    let manifest = pack.join("manifest.json");
    if !manifest.is_file() {
        return Ok("visual_pack_absent".into());
    }
    let bytes = io(fs::read(&manifest))?;
    if bytes.len() > 16 << 20 {
        return Err("Visual manifest too large".into());
    }
    let recipe: Value = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
    if recipe["schema"] != 1 {
        return Err("Unknown visual manifest schema".into());
    }
    let imgs = recipe["imgs"].as_array().ok_or("Missing IMG recipes")?;
    let files = recipe["files"]
        .as_array()
        .ok_or("Missing loose file recipes")?;
    let mut inputs = serde_json::Map::new();
    let mut targets = BTreeSet::new();
    for img in imgs {
        let target = field(img, "target")?;
        validate_target(target)?;
        if !target.ends_with(".img") || !targets.insert(target.to_ascii_lowercase()) {
            return Err("Duplicate/invalid IMG target".into());
        }
        let base = existing(root, target)?;
        inputs.insert(format!("base:{target}"), fingerprint(&base)?);
        for e in img["entries"].as_array().ok_or("Missing entries")? {
            let source = field(e, "file")?;
            inputs.insert(
                format!("pack:{source}"),
                fingerprint(&existing(&pack, source)?)?,
            );
        }
    }
    for item in files {
        let target = field(item, "target")?;
        validate_target(target)?;
        if !targets.insert(target.to_ascii_lowercase()) {
            return Err("Duplicate file target".into());
        }
        if item["mode"] == "splice" {
            inputs.insert(
                format!("base:{target}"),
                fingerprint(&existing(root, target)?)?,
            );
            for splice in item["splices"].as_array().ok_or("Missing file splices")? {
                let source = field(splice, "file")?;
                inputs.insert(
                    format!("pack:{source}"),
                    fingerprint(&existing(&pack, source)?)?,
                );
            }
        } else if item["mode"].is_null() || item["mode"] == "copy" {
            let source = field(item, "file")?;
            inputs.insert(
                format!("pack:{source}"),
                fingerprint(&existing(&pack, source)?)?,
            );
        } else {
            return Err("Unknown visual file recipe".into());
        }
    }
    let cache_root = root.join("SAStandalone-asset-cache");
    io(fs::create_dir_all(&cache_root))?;
    let id = blake3::hash(&bytes).to_hex().to_string();
    let cache = cache_root.join(&id);
    let stamp = json!({"schema":1,"inputs":inputs});
    let receipt = cache.join("receipt.json");
    let cached = fs::read(&receipt)
        .ok()
        .and_then(|b| serde_json::from_slice::<Value>(&b).ok());
    let ready = if let Some(c) = cached.as_ref() {
        c["stamp"] == stamp
            && c["outputs"].as_object().is_some_and(|outputs| {
                outputs.len() == imgs.len() + files.len()
                    && outputs.iter().all(|(name, v)| {
                        relative(&cache, name)
                            .ok()
                            .and_then(|p| fingerprint(&p).ok())
                            .as_ref()
                            == Some(v)
                    })
            })
    } else {
        false
    };
    if !ready {
        // Never mutate an existing generation while it may still be mapped by another process.
        if cache.exists() {
            return Err(
                "Visual cache changed: move that cache generation aside before rebuilding".into(),
            );
        }
        let lock = cache_root.join(format!("{id}.lock"));
        let lock_file = io(OpenOptions::new().write(true).create_new(true).open(&lock))?;
        let temp = cache_root.join(format!("{id}.tmp-{}", std::process::id()));
        let result: Result<()> = (|| {
            io(fs::create_dir(&temp))?;
            let mut outputs = serde_json::Map::new();
            let mut evidence = Vec::new();
            for (index, img) in imgs.iter().enumerate() {
                let name = format!("archive-{index}.img");
                let path = temp.join(&name);
                evidence.push(compose(
                    &existing(root, field(img, "target")?)?,
                    &pack,
                    img,
                    &path,
                )?);
                outputs.insert(name, fingerprint(&path)?);
            }
            for (index, item) in files.iter().enumerate() {
                let name = format!("file-{index}.bin");
                let path = temp.join(&name);
                if item["mode"] == "splice" {
                    compose_patch(root, &pack, item, &path)?;
                } else {
                    let source = existing(&pack, field(item, "file")?)?;
                    verify(&source, item)?;
                    io(fs::copy(&source, &path))?;
                }
                outputs.insert(name, fingerprint(&path)?);
            }
            let r = json!({"stamp":stamp,"outputs":outputs,"archives":evidence});
            let mut receipt = io(File::create(temp.join("receipt.json")))?;
            io(receipt.write_all(&serde_json::to_vec_pretty(&r).map_err(|e| e.to_string())?))?;
            io(receipt.sync_all())?;
            drop(receipt);
            io(fs::rename(&temp, &cache))?;
            Ok(())
        })();
        drop(lock_file);
        let _ = fs::remove_file(lock);
        if result.is_err() {
            let _ = fs::remove_dir_all(&temp);
        }
        result?;
    }
    let mut mappings = String::new();
    for (index, img) in imgs.iter().enumerate() {
        mappings.push_str(&format!(
            "{}\tSAStandalone-asset-cache/{id}/archive-{index}.img\n",
            field(img, "target")?
        ));
    }
    for (index, item) in files.iter().enumerate() {
        mappings.push_str(&format!(
            "{}\tSAStandalone-asset-cache/{id}/file-{index}.bin\n",
            field(item, "target")?
        ));
    }
    let active = root.join("SAStandalone-assets-active.tsv");
    let temp = root.join("SAStandalone-assets-active.tmp");
    io(fs::write(&temp, mappings))?;
    // Windows rename doesn't replace an existing destination. This file is only
    // startup registration metadata, never the source/cache being used by the host.
    if active.exists() {
        io(fs::remove_file(&active))?;
    }
    io(fs::rename(temp, active))?;
    Ok(format!(
        "visual_pack_{} imgs={} files={} generation={id}",
        if ready { "cached" } else { "prepared" },
        imgs.len(),
        files.len()
    ))
}
fn validate_target(target: &str) -> Result<()> {
    relative(Path::new("."), target)?;
    if !(target.starts_with("models/")
        || target.starts_with("data/")
        || target.starts_with("anim/")
        || target.starts_with("audio/"))
        || matches!(
            Path::new(target).extension().and_then(|s| s.to_str()),
            Some("exe" | "asi" | "dll" | "cs" | "cleo" | "scm")
        )
    {
        return Err("Visual recipe cannot redirect executable/script paths".into());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn root() -> PathBuf {
        let p = std::env::temp_dir().join(format!(
            "sa-img-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&p).unwrap();
        p
    }
    fn image(p: &Path) {
        let mut f = File::create(p).unwrap();
        f.write_all(b"VER2\x02\0\0\0").unwrap();
        for (i, name) in ["a.dff", "b.txd"].iter().enumerate() {
            f.write_all(&((i + 1) as u32).to_le_bytes()).unwrap();
            f.write_all(&[1, 0, 0, 0]).unwrap();
            let mut n = [0; 24];
            n[..name.len()].copy_from_slice(name.as_bytes());
            f.write_all(&n).unwrap();
        }
        f.set_len(6144).unwrap();
        f.seek(SeekFrom::Start(2048)).unwrap();
        f.write_all(&[11; 2048]).unwrap();
        f.write_all(&[22; 2048]).unwrap();
    }
    fn spec(p: &Path) -> Value {
        json!({"size":fs::metadata(p).unwrap().len(),"blake3":hash(p).unwrap()})
    }
    #[test]
    fn merge_preserves_owner_bytes_replaces_and_adds_without_memory_img() {
        let r = root();
        fs::create_dir(r.join("models")).unwrap();
        let base = r.join("models/gta3.img");
        image(&base);
        let before = hash(&base).unwrap();
        let pack = r.join("SAStandalone-assets");
        fs::create_dir(&pack).unwrap();
        fs::write(pack.join("one"), [33; 5000]).unwrap();
        fs::write(pack.join("two"), [44; 23]).unwrap();
        let mut a = spec(&pack.join("one"));
        a["name"] = json!("a.dff");
        a["file"] = json!("one");
        let mut b = spec(&pack.join("two"));
        b["name"] = json!("new.txd");
        b["file"] = json!("two");
        let recipe = json!({"schema":1,"imgs":[{"target":"models/gta3.img","base":spec(&base),"entries":[a,b]}],"files":[]});
        fs::write(
            pack.join("manifest.json"),
            serde_json::to_vec(&recipe).unwrap(),
        )
        .unwrap();
        assert!(prepare(&r).unwrap().contains("prepared"));
        let mapping = fs::read_to_string(r.join("SAStandalone-assets-active.tsv")).unwrap();
        let output = r.join(mapping.trim().split('\t').nth(1).unwrap());
        let dir = directory(&output).unwrap();
        assert_eq!(dir.len(), 3);
        let mut f = File::open(&output).unwrap();
        for (e, value, size) in [(&dir[0], 33, 5000), (&dir[1], 22, 2048), (&dir[2], 44, 23)] {
            f.seek(SeekFrom::Start(e.offset)).unwrap();
            let mut b = vec![0; size];
            f.read_exact(&mut b).unwrap();
            assert!(b.iter().all(|&v| v == value));
        }
        drop(f);
        assert_eq!(hash(&base).unwrap(), before);
        assert!(prepare(&r).unwrap().contains("cached"));
        fs::write(pack.join("one"), [3; 7]).unwrap();
        assert!(prepare(&r).is_err());
        fs::remove_dir_all(r).unwrap();
    }
    #[test]
    fn traversal_and_executables_are_rejected() {
        for p in [
            "../a",
            "/a",
            "C:/a",
            "a\\b",
            "a:b",
            "data//x",
            "data/./x",
            "data/x\tx",
        ] {
            assert!(relative(Path::new("."), p).is_err(), "{p}");
        }
        assert!(validate_target("data/script/main.scm").is_err());
        assert!(validate_target("scripts/x.asi").is_err());
    }
    #[test]
    fn sparse_table_patch_retains_owner_content_and_checks_result() {
        let r = root();
        fs::create_dir(r.join("data")).unwrap();
        let base = r.join("data/test.ide");
        let original = b"objs\n10, old, stock\n20, retained, stock\nend\n";
        fs::write(&base, original).unwrap();
        let pack = r.join("SAStandalone-assets");
        fs::create_dir(&pack).unwrap();
        let inserted = b"10, new, own\n";
        fs::write(pack.join("change"), inserted).unwrap();
        let mut change = spec(&pack.join("change"));
        change["file"] = json!("change");
        change["offset"] = json!(5);
        change["delete"] = json!(15);
        let expected = b"objs\n10, new, own\n20, retained, stock\nend\n";
        let item = json!({"target":"data/test.ide","mode":"splice","base":spec(&base),"splices":[change],
            "result_size":expected.len(),"result_blake3":blake3::hash(expected).to_hex().to_string()});
        fs::write(
            pack.join("manifest.json"),
            serde_json::to_vec(&json!({"schema":1,"imgs":[],"files":[item]})).unwrap(),
        )
        .unwrap();
        prepare(&r).unwrap();
        let map = fs::read_to_string(r.join("SAStandalone-assets-active.tsv")).unwrap();
        let out = r.join(map.trim().split('\t').nth(1).unwrap());
        assert_eq!(fs::read(out).unwrap(), expected);
        assert_eq!(fs::read(base).unwrap(), original);
        assert!(prepare(&r).unwrap().contains("cached"));
        fs::remove_dir_all(r).unwrap();
    }
    #[test]
    fn corrupted_input_never_commits() {
        let r = root();
        image(&r.join("owner.img"));
        let mut recipe = json!({"base":spec(&r.join("owner.img")),"entries":[{"name":"a.dff","file":"missing","size":1,"blake3":"bad"}]});
        assert!(compose(&r.join("owner.img"), &r, &recipe, &r.join("bad.img")).is_err());
        assert!(!r.join("bad.img").exists());
        recipe["base"]["blake3"] = json!("bad");
        assert!(compose(&r.join("owner.img"), &r, &recipe, &r.join("bad.img")).is_err());
        fs::remove_dir_all(r).unwrap();
    }
    #[test]
    #[ignore = "requires an isolated owner-backed visual recipe"]
    fn prepares_owned_visual_pack() {
        let root = PathBuf::from(std::env::var_os("SA_DELIRIUM_VISUAL_TEST_ROOT").unwrap());
        println!("{}", prepare(&root).unwrap());
    }
}
