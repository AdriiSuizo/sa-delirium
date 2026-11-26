"""Replace only the native adapter in verified extracted release resources.

Does not publish, download, install or change the base's component selections.
Use resources extracted from a checksum-verified official installer.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def rebuild(base, native, output, version):
    if not re.fullmatch(r'\d+\.\d+\.\d+-rc\.\d+', version):
        raise ValueError('Use an explicit local release-candidate version')
    spec = json.loads((base / 'portable-manifest.json').read_text(encoding='utf-8-sig'))
    if spec.get('lab_only'):
        raise ValueError('A laboratory payload cannot be a distribution base')
    old_bundle = base / 'portable-tools.zip'
    if sha(old_bundle.read_bytes()) != spec['tools_sha256']:
        raise ValueError('Release bundle checksum mismatch')
    receipt = json.loads((native / 'source-receipt.json').read_text(encoding='utf-8'))
    if receipt.get('fixtures') != 'authoring hooks and exports omitted':
        raise ValueError('The adapter must be built with --shipping')
    binary = (native / 'DeliriumSA.asi').read_bytes()
    if binary[:2] != b'MZ':
        raise ValueError('Adapter is not a PE binary')
    next_spec = copy.deepcopy(spec)
    matches = [f for f in next_spec['files'] if f['path'] == 'scripts/DeliriumSA.asi']
    if len(matches) != 1 or matches[0]['kind'] != 'payload':
        raise ValueError('Expected exactly one bundled native adapter')
    adapter = matches[0]
    member = 'payload/' + adapter['source']
    with zipfile.ZipFile(old_bundle) as old:
        if len(old.namelist()) != len(set(old.namelist())):
            raise ValueError('Duplicate archive members')
        previous = old.read(member)
        if sha(previous) != adapter['sha256'] or len(previous) != adapter['bytes']:
            raise ValueError('Original adapter does not match its manifest')
        output.mkdir(parents=True, exist_ok=False)
        with zipfile.ZipFile(output / 'portable-tools.zip', 'w', zipfile.ZIP_DEFLATED) as new:
            for info in old.infolist():
                new.writestr(info, binary if info.filename == member else old.read(info))
    adapter.update(bytes=len(binary), sha256=sha(binary))
    next_spec['version'] = version
    next_spec['tools_sha256'] = sha((output / 'portable-tools.zip').read_bytes())
    profile = next_spec['profile']
    # Keep journal/profile identity and user defaults compatible with restore.
    for f in profile['files']:
        if f['path'] == adapter['path']:
            f.update(bytes=adapter['bytes'], sha256=adapter['sha256'])
    profile['total_bytes'] = sum(f['bytes'] for f in profile['files'])
    for component in profile['components']:
        owned = [f for f in profile['files'] if component['id'] in f.get('components', [])]
        component.update(files=len(owned), bytes=sum(f['bytes'] for f in owned))
    (output / 'portable-manifest.json').write_text(json.dumps(next_spec, ensure_ascii=False), encoding='utf-8')
    (output / 'routing-report.json').write_text(json.dumps({
        'status': 'local_native_candidate', 'unresolved': [], 'base_version': spec['version'],
        'base_tools_sha256': spec['tools_sha256'], 'changed_payloads': [adapter['path']],
        'files': len(profile['files']), 'bytes': profile['total_bytes']}), encoding='utf-8')
    shutil.copyfile(base / 'SteamLauncher.exe', output / 'SteamLauncher.exe')
    # Verify every untouched archived byte, including tools, optional mods,
    # licenses and configuration; never silently rebuild those from local files.
    with zipfile.ZipFile(old_bundle) as old, zipfile.ZipFile(output / 'portable-tools.zip') as new:
        assert old.namelist() == new.namelist()
        for name in old.namelist():
            assert new.read(name) == (binary if name == member else old.read(name)), name
    print(json.dumps({'version': version, 'changed': adapter['path'], 'sha256': adapter['sha256'],
                      'all_other_archive_bytes_preserved': True}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--version', default='0.6.2-rc.1')
    args = parser.parse_args()
    rebuild(args.base, args.native, args.out, args.version)
