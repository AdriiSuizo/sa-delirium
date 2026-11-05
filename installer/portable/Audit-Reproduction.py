"""Find the original package/input for every frozen development runtime file.

Build-time only. Output contains relative package members and hashes, never
destination dependencies on this machine's development/staging directories.
Unresolved inputs remain explicit blockers rather than disappearing from scope.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import os


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    project = args.project.resolve()
    repo = Path(__file__).resolve().parents[2]
    index = json.loads((repo / 'installer/profile-index.json').read_text(encoding='utf-8'))
    stock = json.loads((project / 'release-work/evidence/native-dev5-clean-extract.json').read_text(encoding='utf-8'))['stock_sha256']
    stock_hash = {h: name for name, h in stock.items()}
    stock_lower = {name.casefold(): (name, h) for name, h in stock.items()}
    roots = {}
    extracted = project / 'tools/mod-staging/visual-20261028/extracted'
    for folder in extracted.iterdir():
        if folder.is_dir():
            roots['visual/' + folder.name] = folder
    for name in ['CLEO4', 'Essentials', 'ExeUnprotect', 'GInputSA', 'GrinchTrainerSA-1.0',
                 'GTASA.WidescreenFix', 'III.VC.SA.LimitAdjuster', 'modloader', 'PortableGTA',
                 'SilentPatchSA', 'SkyGfx-Extended', 'Ultimate-ASI-Loader-NoPDB', 'Urbanize',
                 'WindowedMode-3.0.1', 'desktop-20261028', 'niko-animations-v3',
                 'portable-rosa-original']:
        roots[name] = project / 'tools/mod-staging' / name
    names = {Path(f['path']).name.casefold() for f in index['files']}
    candidates = collections.defaultdict(list)
    for package, root in roots.items():
        if not root.is_dir():
            continue
        for folder, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d not in {'target', '.git', '__pycache__'}]
            for name in files:
                if name.casefold() not in names:
                    continue
                p = Path(folder) / name
                candidates[name.casefold()].append((package, root, p))
    hashed = {}
    rows = []
    for f in index['files']:
        name, expected = f['path'], f['sha256']
        row = {'path': name, 'reference_sha256': expected, 'bytes': f['bytes'], 'components': f['components']}
        if name.startswith('DeliriumSA/'):
            row['kind'] = 'iso_generated'
        elif name in {'scripts/DeliriumSA.asi', 'delirium_physics.dll', 'DeliriumSA.ini'}:
            row['kind'] = 'own_runtime_or_configuration'
        elif name == 'gta_sa.exe':
            row.update(kind='steam_compatibility_delta', source='gta-sa.exe')
        elif name.casefold() in stock_lower:
            source, original = stock_lower[name.casefold()]
            row.update(kind='steam_copy' if original == expected else 'steam_delta_review', source=source, source_sha256=original)
        elif expected in stock_hash:
            row.update(kind='steam_copy', source=stock_hash[expected])
        else:
            choices = candidates.get(Path(name).name.casefold(), [])
            matching = []
            for package, root, p in choices:
                if p.stat().st_size != f['bytes']:
                    continue
                if p not in hashed:
                    hashed[p] = sha(p)
                if hashed[p] == expected:
                    matching.append({'package': package, 'member': p.relative_to(root).as_posix(), 'sha256': expected})
            if matching:
                row.update(kind='original_package_member', sources=matching)
            else:
                row.update(kind='unresolved_or_authored_patch',
                           candidates=[{'package': package, 'member': p.relative_to(root).as_posix(), 'bytes': p.stat().st_size}
                                       for package, root, p in choices[:10]])
        rows.append(row)
    result = {'schema': 1, 'purpose': 'reproduce_without_development_on_destination',
              'counts': dict(collections.Counter(x['kind'] for x in rows)), 'files': rows,
              'status': 'audit_not_an_installable_or_redistribution_approval_manifest'}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    print(json.dumps({'counts': result['counts'], 'bytes_hashed': sum(p.stat().st_size for p in hashed), 'out': str(args.out)}))


if __name__ == '__main__':
    main()
