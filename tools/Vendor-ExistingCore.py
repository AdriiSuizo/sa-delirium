"""Snapshot the existing adapted source, without game assets or the old kv payload.

The development project is an input only. Existing destinations are never
overwritten, so an unknown checkout cannot erase another agent's changes.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
RELEASE=Path(__file__).resolve().parents[1]
ALLOWED={'.rs','.h','.hpp','.cpp','.c','.py','.cmd','.ps1','.toml','.lock','.json','.md','.txt','.map','.in'}
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project',type=Path,required=True)
options=parser.parse_args()
project=options.project.resolve();output=RELEASE/'vendor'
if output.exists():raise RuntimeError('Refusing to overwrite an existing vendor snapshot')
records={}
def copy_source(source,target):
    for file in source.rglob('*'):
        if not file.is_file() or any(p in ('.git','target','build','__pycache__') for p in file.relative_to(source).parts):continue
        if file.suffix.lower()not in ALLOWED and not file.name.startswith(('LICENSE','COPYING','NOTICE')):continue
        relative=file.relative_to(source);dest=target/relative
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)
        records[str(file.relative_to(project)).replace('\\','/')]=hashlib.sha256(file.read_bytes()).hexdigest()
for crate in ('delirium','delirium-sdk','delirium-data'):
    copy_source(project/'src/delirium-physics/crates'/crate,output/'src/delirium-physics/crates'/crate)
for crate in ('sa-combat','lsc4-combat','sa-delirium'):
    copy_source(project/'src'/crate,output/'src'/crate)
copy_source(project/'tools/minhook',output/'third-party/minhook')
for name in ('LICENSE','Cargo.lock'):
    source=project/'src/delirium-physics'/name
    if source.exists():shutil.copy2(source,output/'src/delirium-physics'/name)
data=output/'src/delirium-physics/crates/delirium-data'
shutil.copy2(RELEASE/'bootstrap/compatibility/legacy-kv.rs',data/'src/kv.rs')
(data/'Cargo.toml').write_text('''[package]
name="delirium-data"
version="0.1.2"
edition="2024"
license="GPL-3.0-only"
publish=false
[dependencies]
blake3="1"
aes="0.8"
miniz_oxide="0.8"
current_data={package="delirium-data",path="../comparison-data"}
''',encoding='utf-8')
current=project/'release-work/evidence/legacy-iso-abi/comparison-data'
copy_source(current,output/'src/delirium-physics/crates/comparison-data')
(output/'src/delirium-physics/Cargo.toml').write_text('''[workspace]
members=["crates/delirium-data","crates/delirium","crates/delirium-sdk"]
exclude=["crates/comparison-data"]
resolver="3"
[workspace.package]
version="0.1.0"
edition="2024"
license="GPL-3.0-only"
publish=false
[profile.dev]
opt-level=3
''',encoding='utf-8')
(output/'SOURCE-RECEIPT.json').write_text(json.dumps({
 'kind':'existing_adapted_core_source_snapshot',
 'legacy_upstream_base':'b53ef59af7bb35809f71f08d62f564c46c8b4159',
 'current_data_upstream':'93eed4d6a0aafa2e84526730ea1f54dab48ac7d4',
 'original_source_sha256':records,
 'changes':['Replace encrypted kv payload with the tested ISO reader',
            'Standalone SDK workspace and current-data alias'],
 'limits':['Steam native bindings are not fully ported',
           'Tests requiring owned game assets need those local assets',
           'Snapshot does not contain the full visual modpack']},indent=2),encoding='utf-8')
print(json.dumps({'source_files_recorded':len(records),'game_assets_copied':False,'destination':str(output)}))
