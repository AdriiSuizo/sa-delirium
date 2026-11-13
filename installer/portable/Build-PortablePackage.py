"""Build a source-checked Steam/ISO installer; fail on unaccounted files.

Development and original-author archives are BUILD inputs only. The resulting
manifest contains HTTPS sources and hashes, never a destination development path.
Embedded presets include selected original plugins and their notices; Urbanize
remains an automatic original-author download.
Game resources remain local Steam inputs or owner-ISO generated resources.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import shutil
import subprocess
import tempfile
import zipfile
import sys


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def classic_alternatives(recipes, classic, game, patches, label='PC 1.0 US'):
    """Accept each game file as PC 1.0 US ships it, besides the Steam one.

    classic holds the 1.0 US versions of the files that differ from Steam, at
    their Steam paths (gtasa-open-downgrader output layout). Files that are
    the same in both editions need nothing. A 1.0 file equal to the recipe
    output is copied; otherwise one verified delta reaches that output. The
    executable is accepted under both names: gta_sa.exe (PC) and gta-sa.exe
    (a Steam folder already downgraded), plus the exact 1.0 output itself.
    """
    added = 0
    target_exe = game/'gta_sa.exe'
    for f in recipes:
        if f['kind'] not in {'steam', 'steam_delta'}:
            continue
        exe = f['path'] == 'gta_sa.exe'
        steam_sha = f['source_sha256'] if f['kind'] == 'steam_delta' else f['sha256']
        inputs = []
        candidate = classic / f['source']
        if candidate.is_file():
            inputs.append(candidate)
        if exe:
            inputs.append(target_exe)
        alternatives = f.setdefault('alternatives', [])
        for path in inputs:
            digest = sha(path)
            if digest == steam_sha:
                continue
            entry = dict(source_sha256=digest, source_bytes=path.stat().st_size)
            if digest != f['sha256'] and f['kind'] == 'steam_delta':
                entry['patch'] = hashlib.sha256(('classic|'+f['path']+'|'+digest).encode()).hexdigest()[:24]+'.pa30'
                patches.append((entry['patch'], path, game/f['path']))
            if not exe:
                entry['name'] = label
            for name in (['gta_sa.exe', 'gta-sa.exe'] if exe else [f['source']]):
                if any(a.get('source', f['source']).casefold() == name.casefold() and a['source_sha256'] == digest for a in alternatives):
                    continue
                alternatives.append(dict(entry, source=name)); added += 1
        if not alternatives:
            del f['alternatives']
    return added


def patch_names(recipes):
    # Every patch, including the alternatives of a recipe, maps to its output.
    names = {}
    for f in recipes:
        if f.get('patch'):names[f['patch']] = f['sha256']
        for a in f.get('alternatives', []):
            if a.get('patch'):names[a['patch']] = f['sha256']
    return names


# Each audited input has a delta to the executable used by the existing profile.
VERIFIED_CLASSIC_EXECUTABLES = {
    '611db14a6d0eb35911f7671c6c97ad3fbc8ff720ec7c4fad956ce86547baa10c':
        'PC 1.0 US · SA DirectX Optimiser',
    # Publicly identified original retail-disc image; an actual owner-supplied
    # input is required before the build can produce its source-checked delta.
    # Merely recognizing this hash never makes it installable by itself.
    '8c609f108ad737deffbd0d17c702f5974d290c4379de742277b809f80350da1c':
        'PC 1.0 US · original retail-disc executable',
}
CLASSIC_EXECUTABLE_SIZES = {
    '8c609f108ad737deffbd0d17c702f5974d290c4379de742277b809f80350da1c': 14405632,
}


def classic_executable_delta(recipes, source, target):
    if not source.is_file() or source.is_symlink():
        raise ValueError('Use --classic-executable with an audited local executable')
    digest = sha(source)
    if digest not in VERIFIED_CLASSIC_EXECUTABLES:
        raise ValueError(f'Classic executable has no verified compatibility recipe: {digest}')
    pinned_size = CLASSIC_EXECUTABLE_SIZES.get(digest)
    if pinned_size is not None and source.stat().st_size != pinned_size:
        raise ValueError('Retail executable size differs from the audited reference')
    recipe = next(f for f in recipes if f['path'] == 'gta_sa.exe' and f['kind'] == 'steam_delta')
    if sha(target) != recipe['sha256'] or target.stat().st_size != recipe['bytes']:
        raise ValueError('The target differs from the verified profile executable')
    alternatives = recipe.setdefault('alternatives', [])
    if digest == recipe['source_sha256'] or any(a['source_sha256'] == digest for a in alternatives):
        raise ValueError('Executable variant is already covered by the package')
    patch = hashlib.sha256(('classic-executable|' + digest).encode()).hexdigest()[:24] + '.pa30'
    alternatives.append(dict(name=VERIFIED_CLASSIC_EXECUTABLES[digest], source='gta_sa.exe',
                             source_sha256=digest, source_bytes=source.stat().st_size, patch=patch))
    return patch, source, target


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--izzy-source', type=Path, required=True)
    ap.add_argument('--steam-source',type=Path,help='Verified owned stock Steam files used only to build compatibility deltas')
    ap.add_argument('--plan', action='store_true', help='Write routing report only; no distributable ZIP')
    ap.add_argument('--update', action='store_true', help='Refresh this unpublished production package in place')
    ap.add_argument('--preset',choices=['development','classic','full'],default='full')
    ap.add_argument('--version',default='0.3.11')
    ap.add_argument('--embed-mods',action='store_true',help='Embed selected mod members except Urbanize, acquired automatically from its author')
    ap.add_argument('--classic-source',type=Path,
                    help='Folder with the PC 1.0 US versions of the files that differ from Steam (for example '
                         'the gtasa-open-downgrader output). Lets the installer accept a PC 1.0 US copy of the game.')
    ap.add_argument('--steam-edition',action='append',default=[],metavar='NAME=PATH',
                    help='Accept another owned Steam gta-sa.exe build (for example German R2 or a future Steam update). '
                         'Only its executable delta is added; every other Steam file must still match the stock reference.')
    ap.add_argument('--classic-executable', type=Path, action='append', default=[],
                    help='Add a delta from an already audited local PC executable, selected by its SHA-256.')
    args = ap.parse_args()
    project = args.project.resolve()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    if args.update:
        assert out.is_dir() and read(out/'portable-manifest.json').get('lab_only') is False
    else:
        out.mkdir(parents=True, exist_ok=False)
    frozen = read(repo / 'installer/profile-index.json')
    audit = read(project / 'release-work/evidence/portable-installer/archive-audit.json')
    catalog = read(Path(__file__).with_name('source-catalog.json'))['packages']
    packages = {p['id']: p for p in catalog}
    archives = {p['id']: p for p in audit['packages']}
    matched = {f['path']: f for f in audit['matched']}
    stock = read(project / 'release-work/evidence/native-dev5-clean-extract.json')['stock_sha256']
    stock_by_name = {n.casefold(): (n, h) for n, h in stock.items()}
    stock_by_hash = {h: n for n, h in stock.items()}
    original = args.steam_source.resolve() if args.steam_source else project/'release-work/reference/steam-stock'
    editions = []
    for value in args.steam_edition:
        label, sep, location = value.partition('=')
        location = Path(location).resolve()
        assert sep and label.strip() and location.is_file() and not location.is_symlink(), 'Use --steam-edition "Name=C:/path/gta-sa.exe"'
        assert location.name.casefold() == 'gta-sa.exe', 'An alternative Steam edition is identified by its gta-sa.exe'
        digest = sha(location)
        assert digest != sha(original/'gta-sa.exe') and digest not in {e['source_sha256'] for e in editions}, 'Edition repeated or identical to stock'
        editions.append(dict(name=label.strip(), source_sha256=digest, source_bytes=location.stat().st_size,
                             patch=hashlib.sha256(('gta_sa.exe|'+digest).encode()).hexdigest()[:24]+'.pa30', path=location))
    sdk = repo / 'build/bootstrap-portable/i686-pc-windows-msvc/release'
    # The same engine source built for AVX2/FMA/BMI CPUs; the ASI picks it at
    # run time (PhysicsEngineVariant.hpp) and keeps the baseline otherwise.
    sdk_avx2 = repo / 'build/bootstrap-portable-avx2/i686-pc-windows-msvc/release'
    seven = Path(r'C:\Program Files\7-Zip\7z.exe')
    delta = repo / 'build/portable-tools/DeltaTool.exe'
    own_runtime = repo / 'build/portable-production-runtime21/DeliriumSA.asi'
    fxs_archive=project/'tools/downloads/security-review-20261105/GTA SA - FxsFuncs.7z'
    assert sha(fxs_archive)=='5c53fae0effa2c16ce927450aa8deb92065a1849122212dce6a84df094b5110b'
    fxs11=subprocess.run([str(seven),'x','-so','-spd','-p-',str(fxs_archive),'FxsFuncs/FxsFuncs.asi'],capture_output=True,check=True).stdout
    assert hashlib.sha256(fxs11).hexdigest()=='cd9ccf96e6a20a721825608636e88dc15a11bc988616dc8f8c8c9a8d6cb0eaf4'
    fxs_readme=subprocess.run([str(seven),'x','-so','-spd','-p-',str(fxs_archive),'Leiame.txt'],capture_output=True,check=True).stdout
    # The author's own settings from the same archive. The development profile's ini was written for NextGen Effects,
    # which the installer no longer ships: its HeliDustWaterPrims=4 enabled a heli_dust prim the stock effect lacks and
    # crashed low flights over water (gta_sa+0xA9F54 from FxsFuncs, 08/11).
    fxs_ini=subprocess.run([str(seven),'x','-so','-spd','-p-',str(fxs_archive),'FxsFuncs/FxsFuncs.ini'],capture_output=True,check=True).stdout
    assert b'HeliDustWaterPrims = -1' in fxs_ini
    receipts = []
    recipes = []
    payload = {}
    patches = []
    unresolved = []
    used_packages = set()
    classic_components={'asi-loader','cleo','cleo-plus','modloader','window','silentpatch','ginput','widescreen','limit-adjuster','frameratevigilante','portablegta','skygfx','ps2-time-cycle','delirium','render-backend','izzy-s-lively-vehicle-physics','fxsfuncs','nextgen-effects','enhance-particletxd','project2dfx'}
    weapon_components={'gta-v-weapons','nextgen-remaster-weapons'}

    # Small configuration files authored for this distribution. Preserve the
    # selected gameplay settings while dropping machine resolution/menu state.
    own = {
        'global.ini': '[GlobalSets]\nDebugLog=1\nCrashDumpZip=0\nCrashDumpMaxReports=10\nLoadExtraPlugins=scripts\\ExeUnprotect.asi|scripts\\modloader.asi\n',
        'GrinchTrainerSA.toml': '[Features]\nAimSkinChanger = false\n',
        'GTASA.WidescreenFixLegacy.ini': '; Use the official legacy widescreen implementation.\n',
        'scripts/GTASA.WidescreenFixLegacy.ini': '; Use the official legacy widescreen implementation.\n',
        'scripts/III.VC.SA.WindowedMode.cfg': '[MAIN]\nWindowMode=2\nConfineCursor=0\nPauseOnFocusLoss=0\nResumeOnFocus=0\n[WINDOW]\nWidth=0\nHeight=0\nMaximized=0\nPositionX=-1\nPositionY=-1\n',
        'dxvk.conf': '# Framerate Vigilante retains activity limits.\nd3d9.presentInterval = 0\n',
        'scripts/RendererProfile.txt': 'dxvk-d9\n',
        'modloader/.data/plugins/plugins.ini': '[Priority]\n',
        'modloader/.data/config.ini': '[Config]\nEnableMenu=true\nEnablePlugins=true\nEnableLog=true\nImmediateFlushLog=true\nMaxLogSize=5242880\nAutoRefresh=true\n',
        'modloader/Project2DFX/salodlights.cfg': '[DISTANCE]\nViewDistance=0\nDrawDistance=1\nDetailDistance=1\nShadowDistance=1\n[LIGHTS]\nDistantLights=1\nDistantLightShadows=0\nDistantTraffic=0\nSearchlights=1\nDistantLightSize=1\n',
    }
    widescreen = (project / 'game/scripts/GTASA.WidescreenFix.ini').read_text(encoding='utf-8-sig')
    import re
    widescreen, nx = re.subn(r'(?m)^ResX\s*=.*$', 'ResX = 0', widescreen)
    widescreen, ny = re.subn(r'(?m)^ResY\s*=.*$', 'ResY = 0', widescreen)
    assert nx == ny == 1
    # This compact file is the authored override, not a copy of the upstream INI.
    own['scripts/GTASA.WidescreenFix.ini'] = widescreen
    priorities = {
        'GTA V Weapons': 76, 'GTA IV Textures and HQ Roads': 30, 'SA Brightened Project': 54,
        'Jefferson Retextured': 62, 'Parallax Interiors': 72, 'LOD Vegetation': 63,
        'LV Gas Station Neon': 74, 'Project2DFX': 65, 'Visual Compatibility': 95,
        'BSOR_Vegetation': 60, 'NextGen Grass': 70, 'NextGen Effects': 65, 'FxsFuncs': 65,
        'Enhance ParticleTXD': 65, 'Rubbish GTA 4': 65, 'Improved Lamps': 75,
        'Advanced Aiming Mod': 65, 'NeXtGen Remaster Weapons': 65, 'HD Skin Pack 299': 65,
        'Delirium Texture Fixes': 99,
    }
    if args.preset in {'full','classic'}:
        priorities.pop('GTA V Weapons',None)
    priorities.pop('GTA IV Textures and HQ Roads',None)
    # Visual mods with no recorded permission stay out of the public package (Adrii, 07/11); only what changes
    # gameplay is kept. Visual Compatibility merges maps for them and places their objects, so it goes too.
    for folder in ('SA Brightened Project','Jefferson Retextured','Parallax Interiors','LOD Vegetation','LV Gas Station Neon','Visual Compatibility',
                   'BSOR_Vegetation','NextGen Grass','NextGen Effects','Enhance ParticleTXD','Rubbish GTA 4','Improved Lamps','Delirium Texture Fixes'):
        priorities.pop(folder,None)
    own['modloader/modloader.ini'] = '[Folder.Config]\nProfile=Default\nPriorityLimit=100\n[Profiles.Default.Config]\nIgnoreAllMods=false\nExcludeAllMods=false\n[Profiles.Default.Priority]\n' + ''.join(f'{k}={v}\n' for k,v in priorities.items()) + '[Profiles.Default.IgnoreMods]\n_ignore\n'
    licensed = {
        'd3d9.dll': ('DXVK-zlib.txt', 'https://github.com/doitsujin/dxvk/releases/tag/v3.1.1'),
        'scripts/ExeUnprotect.asi': ('Ultimate-ASI-Loader-MIT.txt', 'https://github.com/ThirteenAG/Ultimate-ASI-Loader'),
        'modloader/PortableGTA/portablegta.asi': ('PortableGTA-MIT.txt', 'https://github.com/GTAmodding/miscmods'),
    }
    for f in frozen['files']:
        if f['path'].startswith('modloader/FramerateVigilante/'):
            licensed[f['path']] = ('FramerateVigilante-MIT.txt', 'https://github.com/GTAmodding/FramerateVigilante')

    patched = {
        'vorbisFile.dll': ('ual-c8cb367641', 'dinput8.dll'),
        'cleo/Urbanize/Urbanize - Root.cs': ('urbanize-21217a20e7', 'CLEO/Urbanize/Urbanize - Root.cs'),
        'cleo/Urbanize/Urbanize.dat': ('urbanize-21217a20e7', 'CLEO/Urbanize/Urbanize.dat'),
        'cleo/Urbanize/Urbanize.ini': ('urbanize-21217a20e7', 'CLEO/Urbanize/Urbanize.ini'),
        'GInputSA.ini': ('ginput-b6d03e3322', 'GInputSA.ini'),
        'SilentPatchSA.ini': ('silentpatch-0837bbdc05', 'SilentPatchSA.ini'),
        'modloader/FxsFuncs/FxsFuncs.asi': ('FxsFuncs-5ccb842a3f', 'FxsFuncs/FxsFuncs.asi'),
        'modloader/FxsFuncs/FxsFuncs.ini': ('FxsFuncs-5ccb842a3f', 'FxsFuncs/FxsFuncs.ini'),
        'modloader/Jefferson Retextured/hah.IDE': ('jefferson-e5fd966b60', 'modloader/JeffersonRetextured/hah.IDE'),
        'modloader/Jefferson Retextured/hah.IPL': ('jefferson-e5fd966b60', 'modloader/JeffersonRetextured/hah.IPL'),
        'modloader/Project2DFX/SALodLights.dat': ('project2dfx-3defd60348', 'SALodLights.dat'),
        'modloader/Project2DFX/SALodLights.asi': ('project2dfx-3defd60348', 'SALodLights.asi'),
        'III.VC.SA.LimitAdjuster.asi': ('limit-adjuster', 'III.VC.SA.LimitAdjuster.asi'),
        'III.VC.SA.LimitAdjuster.ini': ('limit-adjuster', 'III.VC.SA.LimitAdjuster.ini'),
    }
    # Text map merges are source checked against the owner's installed SA.
    steam_maps = {Path(n).name.casefold(): n for n in stock if n.lower().endswith(('.ide', '.ipl', 'object.dat'))}

    def add_payload(name, data, components, permission):
        if isinstance(data, Path):
            size, h = data.stat().st_size, sha(data)
        else:
            data = data.encode('utf-8') if isinstance(data, str) else data
            size, h = len(data), hashlib.sha256(data).hexdigest()
        recipes.append(dict(path=name, kind='payload', source=name, bytes=size, sha256=h, components=components))
        payload[name] = data
        receipts.append(dict(path=name, permission=permission))

    def archive_path(pid):
        if pid=='project2dfx-3defd60348':
            return project/'release-work/portable-fetch/originals/project2dfx-current.zip'
        if pid in archives and pid!='project2dfx-3defd60348':
            return Path(archives[pid]['local_build_path'])
        if pid=='skygfx-extended':
            return project/'tools/downloads/SA_-_SkyGfx_Extended.zip'
        return project / 'release-work/portable-fetch/originals' / packages[pid]['filename']

    def patch_package(f, pid, member):
        package = packages[pid]
        if package.get('security_blocked'):
            unresolved.append(dict(path=f['path'],reason='Security review required; do not process or distribute',package=pid));return
        archive = archive_path(pid)
        if not archive.exists():
            unresolved.append(dict(path=f['path'], reason='Original author archive not acquired', package=pid));return
        if sha(archive) != package['sha256']:
            raise ValueError('Build archive changed: ' + pid)
        password = package.get('password', '-')
        run = subprocess.run([str(seven), 'x', '-so', '-spd', '-p'+password, str(archive), member], capture_output=True)
        if run.returncode:
            unresolved.append(dict(path=f['path'], reason='Original member unavailable', package=pid, member=member));return
        data = run.stdout
        source_hash = hashlib.sha256(data).hexdigest()
        recipe = dict(f, kind='package', package=pid, member=member)
        used_packages.add(pid)
        if source_hash != f['sha256']:
            patch_name = hashlib.sha256(f['path'].encode()).hexdigest()[:24]+'.pa30'
            recipe.update(kind='package_delta', source_sha256=source_hash, source_bytes=len(data), patch=patch_name)
            patches.append((patch_name, data, project / 'game' / f['path']))
        recipes.append(recipe)

    visual_removed={'jefferson-retextured','sa-brightened-project','parallax-interiors','bsor-vegetation','improved-lamps','nextgen-effects',
                    'lv-gas-station-neon','lod-vegetation','nextgen-grass','rubbish-gta-4','enhance-particletxd','visual-compatibility'}
    for f in frozen['files']:
        name = f['path']
        components = f['components']
        # Steam consumes this from its original library, not from the prepared SA directory.
        if name.casefold() == 'installscript.vdf':
            continue
        if args.preset=='full' and set(components)&weapon_components:continue
        if set(components)&visual_removed:continue
        if args.preset=='classic' and (name.endswith('.ini.0') or (components and not set(components)&classic_components)):continue
        if name.startswith('DeliriumSA/') or set(components) & {'rosa-project-evolved', 'proper-player-retex', 'gta-iv-textures-and-hq-roads', 'izzy-s-lively-vehicle-physics'} or name in {'gta-sa.exe', 'delirium_physics.dll', 'cleo/cleo_saves/cs1.sav', 'GrinchTrainerSA/update.json'}:
            continue
        f = {k:f[k] for k in ['path','bytes','sha256','components']}
        target = project / 'game' / name
        base = stock_by_name.get(('gta-sa.exe' if name == 'gta_sa.exe' else name).casefold())
        if name == 'vorbisFile.dll':
            base = None
        if base:
            source, expected = base
            assert sha(original / source) == expected
            if name in {'gta_sa.exe', 'anim/anim.img', 'data/script/main.scm', 'data/script/script.img'}:
                assert sha(target) == f['sha256']
                patch_name = hashlib.sha256(name.encode()).hexdigest()[:24]+'.pa30'
                recipes.append(dict(f, kind='steam_delta', source=source, source_sha256=expected, source_bytes=(original/source).stat().st_size, patch=patch_name))
                patches.append((patch_name, original/source, target))
                if name == 'gta_sa.exe' and editions:
                    # Same verified 1.0 output, reached from another Steam build.
                    recipes[-1]['alternatives'] = [{k:e[k] for k in ['name','source_sha256','source_bytes','patch']} for e in editions]
                    patches.extend((e['patch'], e['path'], target) for e in editions)
            else:
                recipes.append(dict(f, kind='steam', source=source, sha256=expected, bytes=(original/source).stat().st_size))
        elif f['sha256'] in stock_by_hash:
            recipes.append(dict(f, kind='steam', source=stock_by_hash[f['sha256']]))
        elif name == 'scripts/DeliriumSA.asi':
            add_payload(name, own_runtime, components, 'Own authored SA adapter, shipping hooks only')
        elif name == 'DeliriumSA.ini':
            assert sha(target) == f['sha256']
            add_payload(name, target, components, 'Own authored Delirium configuration')
        elif name=='modloader/FxsFuncs/FxsFuncs.asi':
            add_payload(name,fxs11,components,'Explicit original author compilation permission; original supplied1.1 ASI unchanged')
        elif name=='modloader/FxsFuncs/FxsFuncs.ini':
            add_payload(name,fxs_ini,components,'FxsFuncs author permits compilation; original supplied settings unchanged')
        elif name in own:
            add_payload(name, own[name], components, 'Authored distribution configuration')
        elif name in licensed:
            license_name, source_url = licensed[name]
            assert sha(target) == f['sha256']
            assert (Path(__file__).with_name('licenses') / license_name).is_file()
            add_payload(name, target, components, license_name+'; '+source_url)
        elif name.startswith('modloader/Visual Compatibility/'):
            source = steam_maps.get(Path(name).name.casefold())
            if not source:
                unresolved.append(dict(path=name, reason='No verified Steam map input'));continue
            patch_name = hashlib.sha256(name.encode()).hexdigest()[:24]+'.pa30'
            recipes.append(dict(f, kind='steam_delta', source=source, source_sha256=stock[source], source_bytes=(original/source).stat().st_size, patch=patch_name))
            patches.append((patch_name, original/source, target))
        elif name in patched:
            patch_package(f, *patched[name])
        elif name.startswith('modloader/SkyGfx/'):
            patch_package(f,'skygfx-extended',name.removeprefix('modloader/'))
        elif name == 'modloader/Rubbish GTA 4/cleo/RoadObjectsGTA4.ini':
            # Runtime object handles are not portable. Use the original author's
            # default configuration, not the author's accumulated live state.
            pid, member = 'rubbish-c4e0a2ce6a', 'Rubbish GTA 4/cleo/RoadObjectsGTA4.ini'
            data = subprocess.run([str(seven),'x','-so','-spd','-p-',str(archive_path(pid)),member],capture_output=True,check=True).stdout
            recipes.append(dict(f,kind='package',package=pid,member=member,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()));used_packages.add(pid)
        else:
            sources = [s for s in matched.get(name, {}).get('sources', []) if s['package'] in packages]
            if not sources:
                unresolved.append(dict(path=name, reason='No accounted original-author source'));continue
            choice = sources[0]
            recipes.append(dict(f,kind='package',package=choice['package'],member=choice['member']))
            used_packages.add(choice['package'])

    if args.classic_source:
        classic = args.classic_source.resolve()
        assert classic.is_dir() and not classic.is_symlink(), 'Use --classic-source with the PC 1.0 US file folder'
        added = classic_alternatives(recipes, classic, project/'game', patches)
        assert added, 'No PC 1.0 US alternative was found in --classic-source'
    for executable in args.classic_executable:
        patches.append(classic_executable_delta(recipes, executable.absolute(), project/'game/gta_sa.exe'))
    add_payload('scripts/sa_delirium_bootstrap.dll', sdk/'sa_delirium_bootstrap.dll', ['delirium'], 'Own clean ISO/SDK runtime; bundled third-party Rust notices required')
    add_payload('scripts/sa_delirium_bootstrap_avx2.dll', sdk_avx2/'sa_delirium_bootstrap.dll', ['delirium'], 'Own clean ISO/SDK runtime, same source for AVX2 CPUs; bundled third-party Rust notices required')
    add_payload('Documentation/Original/FxsFuncs-1.1/Leiame.txt',fxs_readme,[],'Original FxsFuncs supplied readme and compilation permission')
    izzy = args.izzy_source.resolve()
    izzy_receipt = read(project/'release-work/evidence/portable-installer/izzy-1.6-owner-input.json')
    for f in izzy_receipt['files']:
        path = izzy/f['path']
        assert sha(path)==f['sha256'] and path.stat().st_size==f['bytes']
        name = 'modloader/'+f['path'] if '/' in f['path'] else "modloader/Izzy's Lively Vehicle Physics/"+f['path']
        add_payload(name,path,['izzy-s-lively-vehicle-physics'],'Explicit Izzy author permission supplied2026-11-05; original ASI/config/readme unchanged')
    for path in sorted(Path(__file__).with_name('licenses').glob('*.txt')):
        add_payload('Documentation/Licenses/'+path.name,path,[],'Required original license notice')
    for path in sorted((repo/'licenses').glob('*.txt')):
        add_payload('Documentation/Licenses/Project/'+path.name,path,[],'Required retained third-party notice')
    add_payload('Documentation/README.md',repo/'README.md',[],'Distribution install and restore instructions')
    notices=repo/'build/portable-rust-notices'
    assert (notices/'dependency-licenses.json').is_file(),'Generate the locked Rust runtime notices first'
    for path in sorted(notices.rglob('*')):
        if path.is_file():add_payload('Documentation/Licenses/Rust/'+path.relative_to(notices).as_posix(),path,[],'Original dependency license notice')
    add_payload('Documentation/Licenses/plugin-sdk-Zlib.txt',Path(__file__).with_name('include')/'plugin-sdk-sa/LICENSE',[],'Unmodified public ABI interface header license')
    add_payload('Documentation/CREDITS.md',repo/'CREDITS.md',[],'Project credits and original author links')
    add_payload('Documentation/Corresponding-Source.txt','SA Delirium integration and shared runtime: GNU GPL version3.\nCorresponding source and build instructions: https://github.com/AdriiSuizo/sa-delirium\nUse the source archive for the same release as this installer.\nOriginal game executables and resources are not distributed.\nRead the included GPL text, additional permissions and dependency notices.\n',[],'Authored source availability notice')

    # Include parents only when they are actually needed by a selected package.
    queue = list(used_packages)
    while queue:
        parent = packages[queue.pop()].get('parent')
        if parent and parent not in used_packages:
            used_packages.add(parent);queue.append(parent)
    # Preserve the authors' small documentation through original-download
    # recipes as well. Place it outside Mod Loader's readme parser.
    for pid in sorted(used_packages):
        archive = archive_path(pid)
        if packages[pid].get('format')=='raw' or not archive.exists():continue
        if pid in archives:
            members = archives[pid].get('members', [])
        elif pid=='skygfx-extended':
            members = read(project/'release-work/evidence/portable-installer/skygfx-extended-owner-archive.json')['members']
        else:
            listing=subprocess.run([str(seven),'l','-slt','-sccUTF-8','-p-',str(archive)],capture_output=True,text=True,encoding='utf-8',check=True).stdout
            members=[]
            for block in listing.split('----------',1)[-1].split('\n\n'):
                fields=dict(line.split(' = ',1) for line in block.splitlines() if ' = ' in line)
                if fields.get('Path') and fields.get('Size') and fields.get('CRC'):
                    members.append(dict(member=fields['Path'].replace('\\','/'),bytes=int(fields['Size'])))
        for member in members:
            name=member['member']
            if Path(name).suffix.lower() not in {'','.txt','.md'} or member['bytes']>1024*1024 or not re.search(r'readme|leiame|license|licence|credits|copyright',Path(name).name,re.I):continue
            assert not Path(name).is_absolute() and '..' not in Path(name).parts
            password=packages[pid].get('password','-')
            data=subprocess.run([str(seven),'x','-so','-spd','-p'+password,str(archive),name],capture_output=True,check=True).stdout
            recipes.append(dict(path='Documentation/Original/'+pid+'/'+name,kind='package',package=pid,member=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),components=[]))
    if args.embed_mods:
        # Urbanize's author forbids repackaging. Keep that original download
        # automatic; embed the other selected components and their notices.
        # V@DO$ forbids using the HD skins on other sites: never embedded, an installer choice fetched from its original link.
        original_downloads={'urbanize-21217a20e7','skins-2aa371fddf'}
        extraction=tempfile.TemporaryDirectory(prefix='author-input-',dir=out)
        extracted={}
        groups=collections.defaultdict(list)
        for recipe in recipes:
            if recipe['kind'] in {'package','package_delta'} and recipe['package'] not in original_downloads:
                groups[recipe['package']].append(recipe)
        for pid,group in groups.items():
            package=packages[pid];archive=archive_path(pid)
            assert not package.get('security_blocked'),'Blocked source is not a build input'
            assert archive.is_file() and sha(archive)==package['sha256'],'Original author archive changed: '+pid
            if package.get('format')=='raw':extracted[pid]=archive;continue
            directory=Path(extraction.name)/hashlib.sha256(pid.encode()).hexdigest()[:16]
            directory.mkdir()
            names=sorted({f['member'] for f in group})
            for name in names:
                assert not Path(name).is_absolute() and '..' not in Path(name).parts and not any(c in name for c in '\r\n\0')
            listing=directory.with_suffix('.members.txt');listing.write_text('\n'.join(names)+'\n',encoding='utf-8')
            subprocess.run([str(seven),'x','-y','-bd','-bb0','-spd','-scsUTF-8','-p'+package.get('password','-'),'-o'+str(directory),str(archive),'@'+str(listing)],capture_output=True,check=True)
            extracted[pid]=directory
        # Resolve only build-time author inputs. No full game resource can take
        # this route: game originals have steam/steam_delta recipes above.
        converted=[]
        for recipe in recipes:
            if recipe['kind'] not in {'package','package_delta'} or recipe['package'] in original_downloads:
                converted.append(recipe);continue
            pid=recipe['package'];package=packages[pid];archive=archive_path(pid)
            assert not package.get('security_blocked'),'Blocked source is not a build input'
            data=extracted[pid] if package.get('format')=='raw' else extracted[pid]/recipe['member']
            assert data.is_file() and not data.is_symlink() and not data.is_junction()
            if recipe['kind']=='package_delta':
                assert sha(data)==recipe['source_sha256'] and data.stat().st_size==recipe['source_bytes']
                data=project/'game'/recipe['path']
                assert sha(data)==recipe['sha256'] and data.stat().st_size==recipe['bytes']
            else:
                assert data.stat().st_size==recipe['bytes'] and sha(data)==recipe['sha256'],'Original member changed: '+recipe['path']
            converted.append(dict(path=recipe['path'],kind='payload',source=recipe['path'],bytes=recipe['bytes'],sha256=recipe['sha256'],components=recipe['components']))
            payload[recipe['path']]=data
            receipts.append(dict(path=recipe['path'],original_package=pid,original_url=package.get('url'),original_member=recipe['member'],permission='Original third-party component; preserve author credits, original notices and separate terms'))
        recipes=converted
        patches=[p for p in patches if p[0] in patch_names(recipes)]
        used_packages={f['package'] for f in recipes if f['kind'] in {'package','package_delta'}}
    browser_package = None
    if any(packages[p].get('resolver')=='browser' for p in used_packages):
        browser_package = read(project/'release-work/portable-fetch/originals/webview2-runtime-source.json')
        browser_package = {k:browser_package[k] for k in ['id','filename','sha256','bytes','url']}
        browser_package['resolver']='bundled'
        packages[browser_package['id']]=browser_package
        used_packages.add(browser_package['id'])
    plan = dict(status='routing_incomplete' if unresolved else 'routing_complete_not_gameplay_acceptance',
                files=len(recipes), kinds=dict(collections.Counter(f['kind'] for f in recipes)),
                bytes=sum(f['bytes'] for f in recipes), packages=sorted(used_packages),
                unresolved=unresolved, embedded_permissions=receipts)
    (out/'routing-report.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:plan[k] for k in ['status','files','kinds','bytes','unresolved']},ensure_ascii=False),flush=True)
    if args.plan:
        return
    if unresolved:
        raise SystemExit('Unaccounted files; no distributable package was created.')
    helpers = out/'helpers'
    (helpers/'payload').mkdir(parents=True,exist_ok=args.update)
    (helpers/'patches').mkdir(exist_ok=args.update)
    allowed=set(payload)
    for path in (helpers/'payload').rglob('*'):
        if path.is_file():assert path.relative_to(helpers/'payload').as_posix() in allowed,'Stale embedded file; review before repackaging'
    for name, data in payload.items():
        path = helpers/'payload'/name;path.parent.mkdir(parents=True,exist_ok=True)
        if isinstance(data,Path):shutil.copy2(data,path)
        else:path.write_bytes(data)
    with tempfile.TemporaryDirectory(prefix='patch-input-',dir=out) as temp:
        for name, source, target in patches:
            assert sha(target)==patch_names(recipes)[name]
            if not isinstance(source,Path):
                path=Path(temp)/'original';path.write_bytes(source);source=path
            patch=helpers/'patches'/name
            if patch.exists():
                check=Path(temp)/'roundtrip'
                subprocess.run([str(delta),'apply',str(source),str(patch),str(check)],check=True,stdout=subprocess.DEVNULL)
                assert sha(check)==sha(target),'A retained patch no longer reproduces its source-checked output'
                check.unlink()
            else:
                subprocess.run([str(delta),'make',str(source),str(target),str(patch)],check=True,stdout=subprocess.DEVNULL)
                # The patch generator returning 0 is not sufficient: replay the
                # exact source->target conversion and compare the whole output.
                check=Path(temp)/'roundtrip'
                subprocess.run([str(delta),'apply',str(source),str(patch),str(check)],check=True,stdout=subprocess.DEVNULL)
                assert check.stat().st_size == target.stat().st_size and sha(check)==sha(target), \
                    'Generated patch does not reproduce the verified executable'
                check.unlink()
    for source,name in [(delta,'DeltaTool.exe'),(sdk/'sa-delirium-bootstrap.exe','sa-delirium-bootstrap.exe'),(seven,'7z.exe'),(seven.with_name('7z.dll'),'7z.dll'),(seven.with_name('License.txt'),'7-Zip-LICENSE.txt')]:shutil.copy2(source,helpers/name)
    shutil.copy2(repo/'build/portable-tools/RendererProbe.exe',helpers/'RendererProbe.exe')
    if browser_package:
        source=project/'release-work/portable-fetch/originals'/browser_package['filename']
        assert sha(source)==browser_package['sha256']
        target=helpers/'packages'/browser_package['sha256']/browser_package['filename']
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    if browser_package:
        browser=repo/'build/portable-browser-download8'
        for path in browser.iterdir():
            if path.is_file() and path.suffix.lower() in {'.exe','.dll','.txt'}:shutil.copy2(path,helpers/path.name)
    subprocess.run([sys.executable,str(Path(__file__).with_name('Prepare-PlatformSupport.py')),'--project',str(project),'--out',str(helpers/'directx')],check=True,stdout=subprocess.DEVNULL)
    profile=dict(frozen)
    if editions:profile['accepted_steam_executable_sha256']=[e['source_sha256'] for e in editions]
    profile.update(profile='steam-owned-iso-'+args.preset+'-'+args.version,source_kind='owned_steam_plus_owned_xbox360_iso',files=[{k:f[k] for k in ['path','bytes','sha256','components']} for f in recipes],total_bytes=plan['bytes'],notes=['Steam originals are read locally; IV resources are generated from the owner ISO.','Mod plugins are embedded except automatic original Urbanize download.' if args.embed_mods else 'Original author distributions are downloaded and source-checked before local preparation.'])
    for c in profile['components']:
        c['requested']=bool(c['requested'] and c['id'] not in ['rosa-project-evolved','proper-player-retex','gta-iv-textures-and-hq-roads'] and (args.preset!='classic' or c['id'] in classic_components))
        if args.preset=='full' and c['id'] in weapon_components:c['requested']=False
        if c['id'] in visual_removed:c['requested']=False
        c['files']=sum(c['id'] in f['components'] for f in recipes)
        c['bytes']=sum(f['bytes'] for f in recipes if c['id'] in f['components'])
        if c['id']=='izzy-s-lively-vehicle-physics':c['version']='1.6';c['distribution']='Explicit author permission; original files unchanged'
    for p in used_packages:
        if p.startswith('urbanize') or p=='skins-2aa371fddf':packages[p]['optional']=True
        if p=='skins-2aa371fddf':packages[p].update(opt_in=True,title='HD Skin Pack 299',url=packages[p]['url'].replace(' ','%20'))
    manifest=dict(schema=1,version=args.version,lab_only=False,choose_compatible_renderer=True,prepare_legacy_directx=True,profile=profile,files=recipes,packages=[packages[p] for p in sorted(used_packages)],omissions=['GTA V HUD / minimap / weapon wheel','RoSA / bundled Proper Player Retex','GTA IV Textures and HQ Roads','Added weapon packs','Visual mods without recorded permission','Author saves and generated state'])
    if browser_package:manifest['webview_bootstrapper_package']=browser_package['id']
    bundle=out/'portable-tools.zip.new'
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED,compresslevel=7,strict_timestamps=False) as zip:
        for path in sorted(helpers.rglob('*')):
            if path.is_file():zip.write(path,path.relative_to(helpers).as_posix())
    bundle.replace(out/'portable-tools.zip')
    manifest['tools_sha256']=sha(out/'portable-tools.zip')
    (out/'portable-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False),encoding='utf-8')
    if args.embed_mods:extraction.cleanup()


if __name__=='__main__':
    main()
