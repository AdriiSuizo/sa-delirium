"""Refresh vendor/ from the development sources of the GTA SA project.

Every receipt source whose development copy changed is copied again, and new
development files under the vendored source folders are added. A file that
carries a documented release modification keeps that modification: when the
development copy already contains it (it was merged back), the development
copy is taken and the modification record is dropped; otherwise the file is
left as vendored and reported, to be merged by hand.

    python -I tools/Refresh-Vendor.py --project "C:/.../GTA SA" [--dry-run]
        [--merged-upstream src/.../file.rs]
"""
from pathlib import Path
import argparse,hashlib,json,shutil

ROOT=Path(__file__).resolve().parents[1];VENDOR=ROOT/'vendor'
# Development folders mirrored into vendor/ (relative to the project), and the
# file kinds that are sources.
FOLDERS=['src/sa-delirium','src/delirium-physics/crates/delirium/src','src/delirium-physics/crates/delirium-sdk/src',
         'src/delirium-physics/crates/delirium-sdk/include','src/sa-combat/src']
KINDS={'.rs','.hpp','.cpp','.h','.cmd','.toml'}
# Development-only files that are not part of the shipped adapter.
SKIP={'src/sa-delirium/build.cmd'}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def target(source):
    if source.startswith('tools/minhook/'):return VENDOR/'third-party/minhook'/Path(source).relative_to('tools/minhook')
    if '/comparison-data/' in source:return VENDOR/'src/delirium-physics/crates/comparison-data'/source.split('/comparison-data/',1)[1]
    return VENDOR/source

ap=argparse.ArgumentParser();ap.add_argument('--project',type=Path,required=True);ap.add_argument('--dry-run',action='store_true')
ap.add_argument('--merged-upstream',action='append',default=[],metavar='SOURCE',
                help='A modified source reviewed by hand: its development copy contains the modification')
args=ap.parse_args();project=args.project.resolve()
receipt_path=VENDOR/'SOURCE-RECEIPT.json';receipt=json.loads(receipt_path.read_text())
originals=receipt['original_source_sha256'];mods=receipt.setdefault('intentional_modifications_sha256',{})
copied,added,merged,kept=[],[],[],[]
for source,original in list(originals.items()):
    dev=project/source
    if source in SKIP or not dev.is_file() or sha(dev)==original:continue
    if source in mods:
        # The modification is merged back when the old vendored text is a
        # subset of the development text (line by line, in order).
        old=target(source).read_text(encoding='utf-8-sig').replace('\r\n','\n').splitlines()
        new=dev.read_text(encoding='utf-8-sig').replace('\r\n','\n').splitlines()
        it=iter(new)
        if source in args.merged_upstream or all(any(line==n for n in it) for line in old):merged.append(source)
        else:kept.append(source);continue
    copied.append(source)
for folder in FOLDERS:
    for dev in sorted((project/folder).rglob('*')):
        source=dev.relative_to(project).as_posix()
        if dev.is_file() and dev.suffix in KINDS and 'target' not in dev.parts and source not in originals and source not in SKIP:added.append(source)
if not args.dry_run:
    for source in copied+added:
        out=target(source);out.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(project/source,out)
        originals[source]=sha(project/source)
        mods.pop(source,None)
    receipt.setdefault('refreshes',[]).append(dict(copied=len(copied),added=added,modifications_merged_upstream=merged,modifications_kept=kept))
    receipt_path.write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps(dict(copied=copied,added=added,modifications_merged_upstream=merged,modifications_kept=kept,dry_run=args.dry_run),indent=1))
