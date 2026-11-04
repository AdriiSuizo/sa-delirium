"""Validate the retained source receipt before building a distribution."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
vendor=ROOT/'vendor'
receipt=json.loads((vendor/'SOURCE-RECEIPT.json').read_text())
changes=receipt.get('intentional_modifications_sha256',{})
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
count=0
for source,original in receipt['original_source_sha256'].items():
    if source.startswith('tools/minhook/'):
        target=vendor/'third-party/minhook'/Path(source).relative_to('tools/minhook')
    elif '/comparison-data/'in source:
        target=vendor/'src/delirium-physics/crates/comparison-data'/source.split('/comparison-data/',1)[1]
    else:target=vendor/source
    assert target.is_file(),f'Missing source: {source}'
    assert sha(target)==changes.get(source,original),f'Undocumented source change: {source}'
    count+=1
denied={'.iso','.exe','.dll','.asi','.xrt','.xad','.ifp','.img','.scm','.b','.env'}
for file in vendor.rglob('*'):
    if not file.is_file()or'target'in file.relative_to(vendor).parts:continue
    assert file.suffix.lower()not in denied,f'Non-source distribution input: {file}'
    assert file.name!='kv.bin','Old constant payload must never be distributed'
for name,expected in receipt['public_sdk_maps_sha256'].items():
    assert sha(vendor/'src/delirium-physics/crates/comparison-data/src'/name)==expected
print(json.dumps({'source_records_checked':count,'documented_modifications':len(changes),
                  'old_kv_payload_present':False,'public_map_fingerprints_match':True}))
