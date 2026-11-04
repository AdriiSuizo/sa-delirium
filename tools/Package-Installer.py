"""Package own installer/core, notices and corresponding source. No game data."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    repo = Path(__file__).resolve().parents[1]
    project = repo.parent.parent
    args.out.mkdir(parents=True, exist_ok=True)
    exe = args.out / 'SA-Delirium-Setup.exe'
    assert not exe.exists(), 'Use a new output directory/version; never overwrite a published artifact'
    shutil.copy2(args.exe, exe)
    docs = {'LEEME.txt': repo / 'installer/LEEME.txt', 'LICENCIA.txt': repo / 'LICENSE.md',
            'GPL-3.0.txt': repo / 'bootstrap/COPYING', 'CREDITOS.md': repo / 'CREDITS.md',
            'NOTAS.txt': repo / 'installer/THIRD-PARTY-NOTES.txt'}
    for name, source in docs.items():
        shutil.copy2(source, args.out / name)
    source_zip = args.out / 'SA-Delirium-0.2.0-dev-import.1-corresponding-source.zip'
    allow = {'.rs', '.cpp', '.hpp', '.h', '.c', '.toml', '.lock', '.cmd', '.bat', '.ps1', '.py', '.md', '.cs', '.manifest'}
    entries = {}
    with zipfile.ZipFile(source_zip, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in ['sa-delirium', 'sa-combat', 'lsc4-combat', 'delirium-physics', 'plugin-sdk-sa']:
            root = project / 'src' / name
            for path in sorted(root.rglob('*')):
                relative = path.relative_to(root)
                if any(part in {'build', 'target', '.git', '.sdk', 'node_modules', '__pycache__'} for part in relative.parts):
                    continue
                if not path.is_file() or (path.suffix.lower() not in allow and path.name not in {'LICENSE', 'COPYING'}):
                    continue
                entry = 'src/' + name + '/' + relative.as_posix()
                entries[entry] = digest(path)
                archive.write(path, entry)
        # Local build scripts and their dependency references. Generated native
        # research/decompilations, binaries, original assets and secrets stay out.
        for name in ['Build-Deploy-Physics.ps1', 'Build-Deploy-Adapter.ps1', 'Deploy-CombatRuntime.ps1']:
            path = project / 'tools' / name
            entries['tools/' + name] = digest(path)
            archive.write(path, 'tools/' + name)
        archive.writestr('SOURCE-MANIFEST.json', json.dumps(entries, indent=2))
        archive.write(repo / 'LICENSE.md', 'LICENSE.md')
        archive.write(repo / 'bootstrap/COPYING', 'COPYING')
        archive.writestr('BUILD-NOTES.txt', 'Corresponding development sources for the bundled DeliriumSA.asi and delirium_physics.dll.\n'
                          'The installer and Steam launcher sources are in the tagged sa-delirium repository.\n'
                          'Use the included x86 MSVC/Rust build scripts and source-pinned dependencies.\n'
                          'Owner game resources are runtime inputs and are intentionally not included.\n'
                          'Build tools and tests have not been included as runtime files in the installer.\n')
    archive = args.out / 'SA-Delirium-0.2.0-dev-import.1.rar'
    listing = ['SA-Delirium-Setup.exe', *docs.keys()]
    rar = Path(r'C:\Program Files\WinRAR\Rar.exe')
    subprocess.run([str(rar), 'a', '-ma5', '-m5', '-idq', str(archive), *listing], cwd=args.out, check=True)
    subprocess.run([str(rar), 't', '-idq', str(archive)], check=True)
    packed = subprocess.check_output([str(rar), 'lb', str(archive)], text=True).splitlines()
    assert set(packed) == set(listing)
    hashes = {p.name: digest(p) for p in [exe, archive, source_zip]}
    (args.out / 'SHA256SUMS.txt').write_text(''.join(f'{h}  {n}\n' for n, h in hashes.items()), encoding='ascii')
    print(json.dumps({'artifacts': hashes, 'source_files': len(entries), 'rar_files': listing}, indent=2))


if __name__ == '__main__':
    main()
