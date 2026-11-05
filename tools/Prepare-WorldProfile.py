"""Connect reviewed visual-map folders to the native, data-only recipe.

Consumes the existing development compatibility merges, never installs anything.
New definitions, dictionaries and collision archives are explicit dependencies;
missing assets or conflicting model IDs stop preparation rather than hiding them.
The downloaded content remains personal/local-only.
"""
from pathlib import Path
import argparse, collections, importlib.util, json, re

def sections(path):
    result = {}; section = None
    for line in path.read_text(encoding='latin1').splitlines():
        line = line.split('#', 1)[0].strip()
        if not line or line.startswith(';'): continue
        if line.lower() == 'end': section = None
        elif section is None:
            section = line.lower(); result.setdefault(section, [])
        else: result[section].append([x.strip() for x in line.split(',')])
    return result

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', required=True, type=Path)
    ap.add_argument('--development', required=True, type=Path)
    ap.add_argument('--catalog', required=True, type=Path)
    ap.add_argument('--base-profile', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    a = ap.parse_args()
    a.game = a.game.resolve(); a.development = a.development.resolve(); a.output = a.output.resolve()
    assert not a.output.exists(), 'Preserve preceding profiles and evidence'
    spec = importlib.util.spec_from_file_location('visual_payload', Path(__file__).with_name('Prepare-VisualPayload.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    profile = json.loads(a.base_profile.read_text(encoding='utf-8-sig'))
    profile['archives'] = ['models/gta3.img', 'models/gta_int.img']
    catalog = {x.get('folder'): x for x in json.loads(a.catalog.read_text(encoding='utf-8-sig'))['content']}
    chosen = ['SA Brightened Project', 'BSOR_Vegetation', 'Jefferson Retextured',
              'LOD Vegetation', 'NextGen Grass', 'Parallax Interiors', 'LV Gas Station Neon', 'GTA V Weapons']
    mods = a.development / 'modloader'
    stock = {p.name.lower(): p for p in (a.game / 'data').rglob('*') if p.is_file()}
    img = module.img(a.game / 'models/gta3.img')
    report = {'groups': chosen, 'deferred': ['CLEO scripts', 'wind shader pipeline', 'FxsFuncs/effects',
                 'audio', 'RoSA/Proper Player redistribution'], 'new_definitions': [], 'new_models': [],
              'texture_parents': [], 'conflicts': [], 'missing': []}
    files = {}; asset_sources = {}; new_ide = []; new_ipl = []; new_col = []
    owner_definitions = {}
    ordered_ide = []
    for dat in ['data/default.dat', 'data/gta.dat']:
        for line in (a.game / dat).read_text(encoding='latin1').splitlines():
            match = re.match(r'^IDE\s+(.+?)\s*$', line, re.I)
            if match: ordered_ide.append(a.game / match[1].replace('\\', '/'))
    for path in ordered_ide:
        if path.is_file():
            for section, rows in sections(path).items():
                if section in ('objs', 'tobj', 'anim'):
                    for row in rows:
                        if row[0].isdigit(): owner_definitions[int(row[0])] = row
    active_definitions = dict(owner_definitions)
    def meta(folder):
        c = catalog.get(folder, {})
        return {'id': c.get('id', folder.lower().replace(' ', '-')), 'rights': 'local-only',
                'url': c.get('source', 'Owner-supplied local archive'), 'priority': c.get('priority', 50)}
    def loose(path, target, folder):
        files[target.lower()] = {**meta(folder), 'source': str(path), 'target': target,
                                'mode': 'splice' if (a.game / target).is_file() else 'copy'}
    a.output.mkdir(parents=True)
    # The existing seven compatibility files were already semantically merged;
    # reuse them at priority95 rather than redoing map-editor rounding/remaps.
    ordered = sorted(chosen, key=lambda n: (meta(n)['priority'], n))
    for folder in ordered:
        root = mods / folder; assert root.is_dir(), root
        profile['mods'].append({**meta(folder), 'path': str(root), 'extensions': ['.dff', '.txd', '.col', '.ipl', '.ifp']})
        for path in sorted(root.rglob('*')):
            if not path.is_file(): continue
            name = path.name.lower(); ext = path.suffix.lower()
            if ext in ('.dff', '.txd', '.col'): asset_sources[name] = (path, folder)
            if ext not in ('.ide', '.ipl', '.col'): continue
            if 'optional' in str(path.relative_to(root)).lower(): continue
            if name in img: continue  # IMG member: composer routes the replacement.
            if name in stock and ext in ('.ide', '.ipl'):
                loose(path, stock[name].relative_to(a.game).as_posix(), folder)
            elif ext == '.ide': new_ide.append((path, folder))
            elif ext == '.ipl': new_ipl.append((path, folder))
            elif ext == '.col':
                # An existing loose collision archive is overridden at its real
                # path; genuinely new ones are registered through COLFILE.
                matches = [p for p in (a.game / 'models').rglob('*') if p.is_file() and p.name.lower() == name]
                assert len(matches) <= 1, (name, matches)
                if matches: loose(path, matches[0].relative_to(a.game).as_posix(), folder)
                else: new_col.append((path, folder))
    compatibility = mods / 'Visual Compatibility'
    for path in sorted(compatibility.iterdir()):
        if path.suffix.lower() in ('.ide', '.ipl'):
            assert path.name.lower() in stock
            loose(path, stock[path.name.lower()].relative_to(a.game).as_posix(), 'Visual Compatibility')
    # Reconstruct the effective IDE set, including native override precedence.
    effective = [Path(v['source']) for v in files.values() if v['target'].endswith('.ide')]
    effective += [p for p, _ in new_ide]
    txds = set(); required_dff = set(); new_ids = {}
    for path in effective:
        for section, rows in sections(path).items():
            if section == 'txdp':
                for row in rows:
                    if len(row) >= 2:
                        # Native LoadTXDParent creates an empty child slot if
                        # absent; only its parent requires a dictionary file.
                        txds.add(row[1].lower()+'.txd'); report['texture_parents'].append(row[:2])
            elif section in ('objs', 'tobj', 'anim'):
                for row in rows:
                    assert len(row) >= 3 and row[0].isdigit(), (path, row)
                    model = int(row[0]); assert 0 <= model < 20000, (path, model)
                    if model in new_ids and new_ids[model][1:3] != row[1:3]:
                        report['conflicts'].append({'id': model, 'before': new_ids[model], 'after': row})
                    new_ids[model] = row; active_definitions[model] = row
                    required_dff.add(row[1].lower()+'.dff'); txds.add(row[2].lower()+'.txd')
    # Texture-parent-only definitions (BSOR) are new IDE files too.
    bsor = mods / 'BSOR_Vegetation/thetxdp.ide'
    if all(p != bsor for p, _ in new_ide):
        new_ide.append((bsor, 'BSOR_Vegetation'))
        for row in sections(bsor).get('txdp', []):
            txds.add(row[1].lower()+'.txd'); report['texture_parents'].append(row[:2])
    # Every genuinely new IMG entry is justified by a definition or a parent
    # dictionary. Unreferenced downloaded models aren't silently shipped.
    already = {x['name'] for x in profile.get('new_assets', [])}
    for name in sorted(required_dff | txds):
        if name in img or name in already: continue
        if name not in asset_sources:
            report['missing'].append({'asset': name}); continue
        source, folder = asset_sources[name]
        profile.setdefault('new_assets', []).append({**meta(folder), 'source': str(source),
                'name': name, 'archive': 'models/gta3.img'})
        report['new_models'].append(name)
    # Verify every text IPL instance refers to the exact effective model name.
    map_paths = [Path(v['source']) for v in files.values() if v['target'].endswith('.ipl')]
    map_paths += [p for p, _ in new_ipl]
    for path in map_paths:
        if path.read_bytes()[:4] == b'bnry': continue
        for row in sections(path).get('inst', []):
            model = int(row[0]); definition = active_definitions.get(model)
            # Native LoadObjectInstance selects by numeric ID. Dummy/hash names
            # emitted by the original tools are informational, not substitutes
            # for a missing model. Preserve them, and require the actual ID.
            if definition is None:
                report['missing'].append({'map': path.name, 'model': model, 'name': row[1],
                                          'definition': definition[:3] if definition else None})
    # Register additional IDE/COL before native instance creation. Existing
    # gta.dat bytes stay on the recipient; the pack contains only inserted lines.
    before = []; after = []
    for kind, rows in [('IDE', new_ide), ('COLFILE 0', new_col), ('IPL', new_ipl)]:
        seen = set()
        for path, folder in rows:
            name = path.name.lower()
            assert name not in seen, ('Duplicate new map filename', name)
            seen.add(name); target = 'data/SAStandalone/maps/' + name
            loose(path, target, folder)
            line = kind + ' ' + target.replace('/', '\\') + '\r\n'
            (after if kind == 'IPL' else before).append(line)
            report['new_definitions'].append({'kind': kind, 'name': name})
    gta = (a.game / 'data/gta.dat').read_bytes().decode('latin1')
    index = re.search(r'(?m)^IPL ', gta); assert index
    text = gta[:index.start()] + ''.join(before) + gta[index.start():] + '\r\n' + ''.join(after)
    gta_path = a.output / 'gta.dat'; gta_path.write_bytes(text.encode('latin1'))
    loose(gta_path, 'data/gta.dat', 'Visual Compatibility')
    # Procedural plants use the established active priority (grass above BSOR).
    for folder, basename in [('BSOR_Vegetation', 'procobj.dat'), ('NextGen Grass', 'plants.dat')]:
        loose(mods / folder / 'data' / basename, 'data/' + basename, folder)
    # Grass is loaded by its native loose path, not by an IMG or model ID.
    for path in sorted((mods / 'NextGen Grass/models/grass').iterdir()):
        if path.is_file() and path.suffix.lower() in ('.dff', '.txd'):
            loose(path, 'models/grass/' + path.name.lower(), 'NextGen Grass')
    # Preserve phase7 lamp rows; other independent maps are added transactionally.
    previous = {x['target'].lower(): x for x in profile.get('loose_files', [])}
    for key, value in files.items():
        assert key not in previous, ('Unreviewed table overlap with base profile', key)
        previous[key] = value
    profile['loose_files'] = list(previous.values())
    (a.output / 'review.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if report['conflicts'] or report['missing']:
        raise SystemExit('Dependency review has unresolved entries; see review.json. No install performed.')
    (a.output / 'profile.json').write_text(json.dumps(profile, indent=2), encoding='utf-8')
    print(json.dumps({'status': 'prepared_for_payload_and_runtime_validation', 'new_entries': len(report['new_models']),
                      'new_definitions': len(report['new_definitions']), 'loose_files': len(profile['loose_files'])}))

if __name__ == '__main__': main()
