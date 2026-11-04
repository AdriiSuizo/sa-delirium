"""Freeze the playable development tree as hashes, never as redistributed game data."""
from __future__ import annotations
import argparse
import collections
import datetime
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def exclusion(name: str) -> str | None:
    p = name.lower()
    parts = p.split('/')
    if p == 'scripts/v_hud_by_dk22pac.asi' or p.startswith('scripts/gtavhud_by_dk22pac/'):
        return 'requested_gta_v_hud_removal'
    if parts[0] in ('crashdumps', 'userfiles', 'logs', 'readme'):
        return 'personal_profile_or_diagnostics'
    if parts[0] == 'deliriumsa':
        if p.startswith('deliriumsa/combat/') or parts[-1] in ('fred.xrt', 'fred-build.txt', 'taskparams.txt'):
            return None
        return 'generated_physics_diagnostics'
    if p.startswith('modloader/.data/cache/'):
        return 'generated_modloader_cache'
    if parts[-1] in ('gametestbridge.asi', 'steamruntimeprobe.asi', 'probecrash.asi'):
        return 'test_instrumentation'
    if parts[-1].endswith(('.log', '.dmp', '.jsonl', '.bmp', '.tmp', '.bak')):
        return 'generated_diagnostics'
    if parts[-1] == '1' or parts[-1].endswith(('.iso', '.rar', '.zip')):
        return 'not_runtime_content'
    return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--catalog', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    game = args.game.resolve(strict=True)
    catalog = json.loads(args.catalog.read_text(encoding='utf-8-sig'))['content']
    files, ignored = [], collections.Counter()
    for path in sorted(game.rglob('*')):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f'Reparse point is not an importable source: {path}')
        if not path.is_file():
            continue
        name = path.relative_to(game).as_posix()
        reason = exclusion(name)
        if reason:
            ignored[reason] += 1
            continue
        owners = [c['id'] for c in catalog for root in c.get('paths', [])
                  if root.startswith('game/') and
                  (name.casefold() == root[5:].casefold() or name.casefold().startswith(root[5:].casefold() + '/'))]
        files.append({'path': name, 'bytes': path.stat().st_size, 'sha256': digest(path),
                      'components': sorted(set(owners))})
    index = {r['path'].casefold(): r for r in files}
    required = ['gta_sa.exe', 'vorbisFile.dll', 'global.ini', 'scripts/DeliriumSA.asi',
                'scripts/modloader.asi', 'delirium_physics.dll', 'DeliriumSA.ini',
                'DeliriumSA/fred.xrt', 'DeliriumSA/taskparams.txt',
                'DeliriumSA/combat/action_table.csv', 'models/gta3.img', 'data/script/main.scm']
    for name in required:
        if name.casefold() not in index:
            raise ValueError(f'Missing required runtime resource: {name}')
    components = []
    for component in catalog:
        owned = [f for f in files if component['id'] in f['components']]
        components.append({'id': component['id'], 'name': component['name'],
                           'requested': component['id'] != 'v-hud',
                           'files': len(owned), 'bytes': sum(f['bytes'] for f in owned),
                           'development_status': component.get('status', '')})
    result = {'schema': 1, 'profile': 'development-steam-import-20261104',
              'source_kind': 'owner_local_files_only', 'steam_app_id': '12120', 'native_save_path_limit_bytes': 104,
              'source_executable': 'gta_sa.exe', 'source_executable_sha256': index['gta_sa.exe']['sha256'],
              'stock_steam_executable_sha256': '15e3cfedba9a841df67d8194e7249afb493b0e10d6138fb8ebab2c136e543efb',
              'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'files': files, 'total_bytes': sum(f['bytes'] for f in files),
              'components': components, 'excluded_counts': dict(ignored),
              'runtime_exclusions': ['scripts/V_HUD_by_DK22Pac.asi', 'scripts/GTAVHud_by_DK22Pac'],
              'notes': ['This index contains hashes, not game or mod assets.',
                        'All gameplay components found in development are retained except the requested GTA V HUD.',
                        'A matching local source is required; this is not a fresh-machine full modpack downloader.']}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    print(json.dumps({'files': len(files), 'bytes': result['total_bytes'], 'components': len(components),
                      'excluded': dict(ignored), 'out': str(args.out)}))


if __name__ == '__main__':
    main()
