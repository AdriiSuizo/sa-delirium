"""Build-source overlay for SA 1.0 and the clean ISO-backed shared runtime.

Uses retained authored sources. No Steam address remapping, game binaries,
prebuilt development DLL or generated owner resources enter the overlay.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
from runtime_sources import strip_fixtures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--include-test-diagnostics', action='store_true', help='Private validation only; retain authoring fixture exports/hooks')
    args = ap.parse_args()
    repo = Path(__file__).resolve().parents[2]
    subprocess.run([sys.executable, str(repo / 'tools/Verify-Vendor.py')], check=True)
    assert not args.out.exists(), 'Use a fresh isolated build directory'
    args.out.mkdir(parents=True)
    source = repo / 'vendor/src/sa-delirium'
    records = {}
    texts = {path.name: path.read_text(encoding='utf-8-sig') for path in source.iterdir() if path.suffix in {'.hpp', '.cpp'}}
    omitted = [] if args.include_test_diagnostics else strip_fixtures(texts)
    for path in source.iterdir():
        if path.suffix not in {'.hpp', '.cpp'}:
            continue
        data = path.read_bytes()
        text = texts[path.name]
        records[path.name] = hashlib.sha256(data).hexdigest()
        text = text.replace('#include "../plugin-sdk-sa/plugin_sa/game_sa/rw/', '#include "')
        text = text.replace('#include "../delirium-physics/crates/delirium-sdk/include/', '#include "')
        text = text.replace('#include "../plugin-sdk-sa/', '#include "plugin-sdk-sa/')
        if path.name == 'SessionLog.hpp':
            text = text.replace('"delirium_physics.dll"', '"scripts/sa_delirium_bootstrap.dll"')
            text = text.replace('"DeliriumSA/combat/', '"DeliriumSA-local-v3/combat/')
        if path.name == 'Adapter.cpp':
            old = 'folder=fs::path(exe).parent_path()/L"DeliriumSA";'
            assert text.count(old) == 1
            text = text.replace(old, 'folder=fs::path(exe).parent_path()/L"DeliriumSA-local-v3";')
            old = 'if(nm_version()!=100||!ok(nm_init((folder/"fred.xrt").string().c_str()))||!ok(nm_load_task_params((folder/"taskparams.txt").string().c_str())))'
            assert text.count(old) == 1
            before = '''
    // The same DLL owns the ISO reader and the SDK; no baked kv.bin or
    // original game resources are linked into the distribution runtime.
    using PrepareOwned=int(__cdecl*)(const wchar_t*,size_t,unsigned char*,size_t);
    auto sharedCore=GetModuleHandleW(L"sa_delirium_bootstrap.dll");
    auto prepareOwned=sharedCore?reinterpret_cast<PrepareOwned>(GetProcAddress(sharedCore,"sa_delirium_prepare")):nullptr;
    const auto ownedRoot=fs::path(exe).parent_path().wstring();
    unsigned char preparationStatus[2048]{};
    if(!prepareOwned||prepareOwned(ownedRoot.data(),ownedRoot.size(),preparationStatus,sizeof(preparationStatus))!=0){
        status="owned_resource_preparation_failed";event(status,-1,-1,reinterpret_cast<char*>(preparationStatus));return 0;
    }
'''
            text = text.replace(old, before + 'if(nm_version()!=100)')
        (args.out / path.name).write_text(text, encoding='utf-8', newline='\n')
    (args.out / 'source-receipt.json').write_text(json.dumps({
        'source_sha256': records, 'changes': ['Use clean shared ISO/SDK DLL and v3 resource cache'],
        'native_abi': 'existing_verified_SA_1_0_unchanged',
        'fixtures': 'private validation only' if args.include_test_diagnostics else 'authoring hooks and exports omitted',
        'omitted': omitted,
        'game_binaries_or_resources_copied': False,
    }, indent=2), encoding='utf-8')
    print(json.dumps({'prepared_files': len(records), 'out': str(args.out)}))


if __name__ == '__main__':
    main()
