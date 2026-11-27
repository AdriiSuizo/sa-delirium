"""Replace the native adapter and optional matching runtimes in a verified release.

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


def rebuild(base, native, output, version, bootstrap=None, avx2=None):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-rc\.\d+)?', version):
        raise ValueError('Use an explicit version or release-candidate version')
    spec = json.loads((base / 'portable-manifest.json').read_text(encoding='utf-8-sig'))
    if spec.get('lab_only'):
        raise ValueError('A laboratory payload cannot be a distribution base')
    old_bundle = base / 'portable-tools.zip'
    if sha(old_bundle.read_bytes()) != spec['tools_sha256']:
        raise ValueError('Release bundle checksum mismatch')
    receipt = json.loads((native / 'source-receipt.json').read_text(encoding='utf-8'))
    if receipt.get('fixtures') != 'authoring hooks and exports omitted':
        raise ValueError('The adapter must be built with --shipping')
    if (bootstrap is None) != (avx2 is None):
        raise ValueError('Replace the baseline and AVX2 runtimes as a pair')
    upgrades = {'scripts/DeliriumSA.asi': native / 'DeliriumSA.asi'}
    if bootstrap is not None:
        upgrades.update({
            'scripts/sa_delirium_bootstrap.dll': bootstrap,
            'scripts/sa_delirium_bootstrap_avx2.dll': avx2,
        })
    next_spec = copy.deepcopy(spec)
    replacements = {}
    payloads = {}
    for path, source in upgrades.items():
        binary = source.read_bytes()
        pe_head = int.from_bytes(binary[0x3c:0x40], 'little')
        if binary[:2] != b'MZ' or binary[pe_head:pe_head + 6] != b'PE\x00\x00\x4c\x01':
            raise ValueError(f'Expected a Win32 PE binary: {path}')
        matches = [file for file in next_spec['files'] if file['path'] == path]
        if len(matches) != 1 or matches[0]['kind'] != 'payload':
            raise ValueError(f'Expected exactly one bundled binary: {path}')
        entry = matches[0]
        member = 'payload/' + entry['source']
        replacements[member] = (binary, entry['sha256'], entry['bytes'])
        entry.update(bytes=len(binary), sha256=sha(binary))
        payloads[path] = entry
    with zipfile.ZipFile(old_bundle) as old:
        if len(old.namelist()) != len(set(old.namelist())):
            raise ValueError('Duplicate archive members')
        for member, (_, expected, size) in replacements.items():
            previous = old.read(member)
            if sha(previous) != expected or len(previous) != size:
                raise ValueError(f'Original payload does not match its manifest: {member}')
        output.mkdir(parents=True, exist_ok=False)
        with zipfile.ZipFile(output / 'portable-tools.zip', 'w', zipfile.ZIP_DEFLATED) as new:
            for info in old.infolist():
                entry = replacements.get(info.filename)
                new.writestr(info, entry[0] if entry else old.read(info))
    next_spec['version'] = version
    next_spec['tools_sha256'] = sha((output / 'portable-tools.zip').read_bytes())
    profile = next_spec['profile']
    # Keep journal/profile identity and user defaults compatible with restore.
    for f in profile['files']:
        if f['path'] in payloads:
            entry = payloads[f['path']]
            f.update(bytes=entry['bytes'], sha256=entry['sha256'])
    profile['total_bytes'] = sum(f['bytes'] for f in profile['files'])
    for component in profile['components']:
        owned = [f for f in profile['files'] if component['id'] in f.get('components', [])]
        component.update(files=len(owned), bytes=sum(f['bytes'] for f in owned))
    (output / 'portable-manifest.json').write_text(json.dumps(next_spec, ensure_ascii=False), encoding='utf-8')
    (output / 'routing-report.json').write_text(json.dumps({
        'status': 'local_native_candidate', 'unresolved': [], 'base_version': spec['version'],
        'base_tools_sha256': spec['tools_sha256'], 'changed_payloads': sorted(payloads),
        'files': len(profile['files']), 'bytes': profile['total_bytes']}), encoding='utf-8')
    shutil.copyfile(base / 'SteamLauncher.exe', output / 'SteamLauncher.exe')
    # Verify every untouched archived byte, including tools, optional mods,
    # licenses and configuration; never silently rebuild those from local files.
    with zipfile.ZipFile(old_bundle) as old, zipfile.ZipFile(output / 'portable-tools.zip') as new:
        assert old.namelist() == new.namelist()
        for name in old.namelist():
            entry = replacements.get(name)
            assert new.read(name) == (entry[0] if entry else old.read(name)), name
    print(json.dumps({'version': version, 'changed': {k: v['sha256'] for k, v in payloads.items()},
                      'all_other_archive_bytes_preserved': True}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--bootstrap', type=Path)
    parser.add_argument('--avx2', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--version', default='0.6.2-rc.1')
    args = parser.parse_args()
    rebuild(args.base, args.native, args.out, args.version, args.bootstrap, args.avx2)
