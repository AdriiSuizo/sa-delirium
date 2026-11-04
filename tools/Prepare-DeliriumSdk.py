"""Prepare the pinned, locally patched Delirium data crate for a lab build."""
import argparse
import io
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = '93eed4d6a0aafa2e84526730ea1f54dab48ac7d4'
URL = 'https://github.com/PVCPC/delirium-physics.git'
PREFIX = 'crates/delirium-data/'
OUTPUT = ROOT / 'bootstrap' / '.sdk' / 'delirium-data'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'SDK source changed near {old[:52]}')
    return text.replace(old, new, 1)


def patch_layout(text):
    text = replace_once(text, 'const N: usize = 1093;', '''#[path = "sa_disc_europe_asia.rs"]
mod disc_europe_asia;

const N: usize = 1093;''')
    text = replace_once(text,
        '    let Some(map) = MAPS.into_iter().find(|m| m.len() == 44 + N * 13 && le(m, 4, 8) == prog.len() as u64 && m[12..44] == *blake3::hash(prog).as_bytes())',
        '''    let hash = blake3::hash(prog);
    let alternate = disc_europe_asia::matches(prog, hash);
    let Some(map) = MAPS.into_iter().find(|m| m.len() == 44 + N * 13 && le(m, 4, 8) == prog.len() as u64 && m[12..44] == *hash.as_bytes())
        .or_else(|| alternate.then_some(MAPS[1]))''')
    return replace_once(text,
        '    let rd = |at: usize, k: usize| if be { prog[at..at + k].iter().fold(0u64, |v, &x| v << 8 | x as u64) } else { le(prog, at, k) };',
        '''    if alternate && !disc_europe_asia::verify(prog, map) { return false; }
    let rd = |at: usize, k: usize| {
        let at = if alternate { disc_europe_asia::offset(at, k) } else { at };
        if be { prog[at..at + k].iter().fold(0u64, |v, &x| v << 8 | x as u64) } else { le(prog, at, k) }
    };''')


def data_files(checkout):
    resolved = subprocess.check_output(
        ['git', '-C', str(checkout), 'rev-parse', f'{REVISION}^{{commit}}'], text=True).strip()
    if resolved != REVISION:
        raise ValueError('Delirium source revision mismatch')
    archive = subprocess.check_output(
        ['git', '-C', str(checkout), 'archive', '--format=zip', REVISION, PREFIX])
    with zipfile.ZipFile(io.BytesIO(archive)) as contents:
        files = {
            Path(entry.filename.removeprefix(PREFIX)): contents.read(entry)
            for entry in contents.infolist() if entry.filename.startswith(PREFIX) and not entry.is_dir()
        }
    manifest = files[Path('Cargo.toml')].decode()
    for name, value in [('version', '"0.1.0"'), ('edition', '"2024"'),
                        ('license', '"GPL-3.0-only"'), ('publish', 'false')]:
        manifest = replace_once(manifest, f'{name}.workspace = true', f'{name} = {value}')
    files[Path('Cargo.toml')] = manifest.encode()
    source = Path('src/kv.rs')
    files[source] = patch_layout(files[source].decode()).encode()
    files[Path('src/sa_disc_europe_asia.rs')] = (
        ROOT / 'bootstrap' / 'sdk-patches' / 'sa_disc_europe_asia.rs').read_bytes()
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkout', type=Path, help='Existing Delirium Git checkout, read-only')
    options = parser.parse_args()
    checkout = options.checkout or ROOT / 'build' / 'upstream-delirium-v0.6.0'
    if not checkout.exists() and options.checkout is None:
        checkout.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--depth', '1', '--branch', 'v0.6.0',
                        URL, str(checkout)], check=True)
    files = data_files(checkout)
    if OUTPUT.exists():
        actual = {path.relative_to(OUTPUT) for path in OUTPUT.rglob('*') if path.is_file()}
        if actual != files.keys() or any((OUTPUT / name).read_bytes() != data for name, data in files.items()):
            raise ValueError(f'Generated SDK differs from source. Move aside {OUTPUT} and retry.')
    else:
        staging = OUTPUT.with_name(OUTPUT.name + '.preparing')
        staging.mkdir(parents=True)
        for name, data in files.items():
            dest = staging / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        staging.rename(OUTPUT)
    print(OUTPUT)


if __name__ == '__main__':
    main()
