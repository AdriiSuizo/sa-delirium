"""Build a data-only local visual recipe; never copy original IMG archives.

This importer is deliberately explicit about archives, priority and permissions.
Downloaded files remain untouched. Source/caches are NOT GitHub source files.
Python + blake3 are build-time requirements only; the game's Rust preparer cooks
the original archive on the recipient's machine using bounded streaming I/O.
"""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, shutil, struct, difflib
import blake3

def digest(path):
    with path.open('rb') as f:return blake3.blake3(f.read()).hexdigest() if path.stat().st_size<1<<20 else stream_digest(f)
def stream_digest(f):
    h=blake3.blake3()
    while b:=f.read(1<<20):h.update(b)
    return h.hexdigest()
def img(path):
    size=path.stat().st_size
    with path.open('rb') as f:
        header=f.read(8)
        if len(header)!=8 or header[:4]!=b'VER2':raise ValueError(f'Not VER2: {path}')
        count=struct.unpack('<I',header[4:])[0]
        if not 0<count<1_000_000 or 8+count*32>size:raise ValueError(f'Invalid directory: {path}')
        rows={}
        for _ in range(count):
            off,stream,archive,raw=struct.unpack('<IHH24s',f.read(32));name=raw.split(b'\0',1)[0].decode('ascii').lower()
            length=(archive or stream)*2048
            if name in rows or not length or off*2048+length>size:raise ValueError(f'Bad IMG entry: {path}/{name}')
            rows[name]=(off*2048,length)
        return rows
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game',type=Path,required=True,help='Original compatible Steam installation')
    ap.add_argument('--profile',type=Path,required=True,help='Explicit local source/priority/rights recipe')
    ap.add_argument('--output',type=Path,required=True,help='New SAStandalone-assets directory')
    args=ap.parse_args();profile=json.loads(args.profile.read_text(encoding='utf-8-sig'))
    if args.output.exists():raise SystemExit('Output exists: keep prior evidence and choose a new directory')
    archives={name:img(args.game/name)for name in profile['archives']}
    winners={};ignored=[];conflicts=[];sources=[]
    for mod in sorted(profile['mods'],key=lambda x:(x['priority'],x['id'])):
        source=Path(mod['path']);allowed=set(mod.get('extensions',['.dff','.txd']))
        if not source.is_dir():raise ValueError(f'Missing source: {source}')
        if mod['rights'] not in ['local-only','redistributable','original']:raise ValueError('Declare source rights')
        sources.append({k:mod[k]for k in ['id','priority','rights','url']})
        for f in sorted(source.rglob('*')):
            if not f.is_file()or f.suffix.lower()not in allowed:continue
            name=f.name.lower();matches=[a for a,rows in archives.items()if name in rows]
            if len(matches)>1:
                override=mod.get('archive')
                if override in matches:matches=[override]
                else:raise ValueError(f'Ambiguous archive for {f}: {matches}; specify archive')
            if not matches:
                ignored.append({'mod':mod['id'],'file':str(f.relative_to(source)),'reason':'not in an original archive; requires explicit model/map recipe'});continue
            key=(matches[0],name)
            if key in winners:conflicts.append({'target':list(key),'previous':winners[key][0]['id'],'winner':mod['id']})
            winners[key]=(mod,f)
    args.output.mkdir(parents=True);blobs=args.output/'blobs';blobs.mkdir()
    manifest={'schema':1,'imgs':[],'files':[]}
    published=True;unchanged=0;total=0
    def blob(data):
        h=blake3.blake3(data).hexdigest();dst=blobs/h
        if not dst.exists():dst.write_bytes(data)
        return {'file':'blobs/'+h,'size':len(data),'blake3':h}
    for archive,rows in archives.items():
        entries=[];base=args.game/archive
        with base.open('rb')as original:
            for(a,name),(mod,source)in sorted(winners.items()):
                if a!=archive:continue
                data=source.read_bytes();off,n=rows[name];original.seek(off);owned=original.read(n)
                # A mod archive may carry unmodified official resources. Never
                # put those in the payload; they remain in the owner's base.
                if len(data)<=len(owned)and data==owned[:len(data)]and not any(owned[len(data):]):unchanged+=1;continue
                h=blake3.blake3(data).hexdigest();dst=blobs/h
                if not dst.exists():dst.write_bytes(data)
                entries.append({'name':name,'file':'blobs/'+h,'size':len(data),'blake3':h,'source':mod['id']})
                total+=len(data);published&=mod['rights']!='local-only'
        if entries:manifest['imgs'].append({'target':archive,'base':{'size':base.stat().st_size,'blake3':digest(base)},'entries':entries})
    for item in profile.get('new_assets',[]):
        if item['rights']not in ['local-only','redistributable','original']:raise ValueError('Declare additional asset rights')
        name=item['name'].lower();archive=item['archive']
        assert 0<len(name)<=23 and name.isascii()and PurePosixPath(name).name==name and ':'not in name and '\\'not in name
        assert PurePosixPath(name).suffix in ['.txd','.dff','.col']and name not in archives[archive],'Only explicitly new render/collision assets'
        target=next((a for a in manifest['imgs']if a['target']==archive),None)
        if target is None:
            base=args.game/archive;target={'target':archive,'base':{'size':base.stat().st_size,'blake3':digest(base)},'entries':[]};manifest['imgs'].append(target)
        assert name not in {e['name']for e in target['entries']}
        data=Path(item['source']).read_bytes();target['entries'].append({'name':name,**blob(data),'source':item['id']})
        total+=len(data);published&=item['rights']!='local-only';sources.append({k:item[k]for k in ['id','rights','url']})
        ignored[:]=[x for x in ignored if not(x['mod']==item['id']and Path(x['file']).name.lower()==name)]
    report={'scope':'data-only; no code/script/IDE/IPL/audio recipe inferred','sources':sources,'conflicts':conflicts,'ignored':ignored,
            'unchanged_owner_files_excluded':unchanged,'replacement_bytes':total,'redistributable':published,
            'source_profile_sha256':hashlib.sha256(args.profile.read_bytes()).hexdigest()}
    for item in profile.get('loose_files',[]):
        if item['rights']not in ['local-only','redistributable','original']:raise ValueError('Declare loose-file source rights')
        target=PurePosixPath(item['target']);assert not target.is_absolute()and '..'not in target.parts
        assert target.suffix.lower()not in ['.asi','.dll','.exe','.cs','.cleo','.scm']
        source=Path(item['source']);data=source.read_bytes();base=args.game/str(target)
        if item['mode']=='splice':
            old=base.read_bytes()
            if old==data:continue
            # Diff whole lines: no unchanged official table is put in the pack.
            left=old.splitlines(keepends=True);right=data.splitlines(keepends=True);offsets=[0]
            for line in left:offsets.append(offsets[-1]+len(line))
            edits=[];retained=[]
            # An author's LF conversion is not a reason to redistribute every
            # unchanged official row. Preserve the owner's equal lines verbatim.
            logical=lambda lines:[line.rstrip(b'\r\n')for line in lines]
            for op,a,b,c,d in difflib.SequenceMatcher(a=logical(left),b=logical(right),autojunk=False).get_opcodes():
                if op=='equal':retained.extend(left[a:b])
                else:
                    inserted=b''.join(right[c:d]);retained.append(inserted)
                    edits.append({'offset':offsets[a],'delete':offsets[b]-offsets[a],**blob(inserted)})
            if not edits:continue
            data=b''.join(retained)
            entry={'target':str(target),'mode':'splice','base':{'size':len(old),'blake3':blake3.blake3(old).hexdigest()},'splices':edits,
                   'result_size':len(data),'result_blake3':blake3.blake3(data).hexdigest(),'source':item['id']}
        elif item['mode']=='copy':
            if base.is_file()and base.read_bytes()==data:continue
            entry={'target':str(target),'mode':'copy',**blob(data),'source':item['id']}
        else:raise ValueError('Unknown explicit loose-file mode')
        manifest['files'].append(entry);published&=item['rights']!='local-only'
        report['sources'].append({k:item[k]for k in ['id','rights','url']})
    assert len({v['target'].lower()for v in manifest['imgs']+manifest['files']})==len(manifest['imgs'])+len(manifest['files']),'Duplicate target'
    report['redistributable']=published
    manifest['redistributable']=published
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (args.output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'archives':len(manifest['imgs']),'entries':sum(len(a['entries'])for a in manifest['imgs']),
        'files':len(manifest['files']),'ignored':len(ignored),'replacement_bytes':total,'redistributable':published,'original_identical_excluded':unchanged}))
if __name__=='__main__':main()
