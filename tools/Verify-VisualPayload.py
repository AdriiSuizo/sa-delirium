"""Validate a closed data-only payload before putting it into any archive."""
from pathlib import Path,PurePosixPath
import json,sys
import blake3

def verify(root):
    manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    assert manifest['schema']==1
    expected={'manifest.json','provenance.json'};count=0
    def resource(row):
        nonlocal count
        rel=PurePosixPath(row['file'])
        assert not rel.is_absolute()and '..'not in rel.parts and ':'not in str(rel)and '\\'not in str(rel)
        assert str(rel)=='blobs/'+row['blake3'] and len(row['blake3'])==64
        path=root.joinpath(*rel.parts);assert path.resolve().is_relative_to(root.resolve())
        assert path.stat().st_size==row['size']
        h=blake3.blake3()
        with path.open('rb')as f:
            while b:=f.read(1<<20):h.update(b)
        assert h.hexdigest()==row['blake3'],str(rel)
        expected.add(str(rel));count+=1
    for img in manifest['imgs']:
        assert img['target'].startswith(('models/','anim/'))and img['target'].endswith('.img')
        assert len(img['base']['blake3'])==64 and img['base']['size']>0
        names=set()
        for row in img['entries']:
            name=row['name'];assert 0<len(name)<=23 and name.isascii()and name.lower()==name
            assert not any(x in name for x in ['/',':','\\','\0'])and name not in names
            names.add(name);resource(row)
    for row in manifest['files']:
        assert row['target'].startswith(('data/','models/','anim/','audio/'))
        assert Path(row['target']).suffix.lower()not in ['.asi','.dll','.exe','.cleo','.cs','.scm']
        if row.get('mode','copy')=='copy':resource(row)
        elif row['mode']=='splice':
            cursor=0
            for splice in row['splices']:
                assert cursor<=splice['offset']<=row['base']['size']
                cursor=splice['offset']+splice['delete'];assert cursor<=row['base']['size']
                resource(splice)
            assert len(row['result_blake3'])==64 and row['result_size']>=0
        else:raise ValueError('Unknown visual file recipe')
    actual={p.relative_to(root).as_posix()for p in root.rglob('*')if p.is_file()}
    assert expected==actual,{'unexpected':sorted(actual-expected),'missing':sorted(expected-actual)}
    print(json.dumps({'status':'valid_data_only_payload','references':count,'blobs':len(expected)-2,
                      'redistributable':manifest['redistributable']}))
if __name__=='__main__':verify(Path(sys.argv[1]))
