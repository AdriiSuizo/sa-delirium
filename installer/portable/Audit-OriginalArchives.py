"""Match frozen files to original author archives, including nested packages.

CRC/name/size select candidates only. Installer still checks SHA256 of both
the downloaded archive and every selected member before applying any patch.
Local build paths are emitted solely in the private evidence, not a runtime plan.
"""
from pathlib import Path
import argparse
import binascii
import collections
import hashlib
import json
import subprocess

def digest(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()

def crc(p):
    result=0
    with p.open('rb') as f:
        while block:=f.read(1024*1024): result=binascii.crc32(block,result)
    return f'{result&0xffffffff:08X}'

def members(path,sevenzip):
    run=subprocess.run([str(sevenzip),'l','-slt','-sccUTF-8',str(path)],capture_output=True,text=True,encoding='utf-8',errors='replace')
    if run.returncode or '----------' not in run.stdout: return []
    result=[]
    for block in run.stdout.split('----------',1)[1].split('\n\n'):
        d=dict(line.split(' = ',1) for line in block.strip().splitlines() if ' = ' in line)
        if not d.get('Path') or 'D' in d.get('Attributes','') or not d.get('CRC'):continue
        result.append({'member':d['Path'].replace('\\','/'),'bytes':int(d['Size']),'crc32':d['CRC']})
    return result

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--sevenzip',type=Path,default=Path(r'C:\Program Files\7-Zip\7z.exe'))
    args=ap.parse_args();project=args.project.resolve();repo=Path(__file__).resolve().parents[2]
    archives=[]
    for root in [project/'release-work/portable-fetch/originals',project/'tools/downloads/visual-20261028',project/'tools/downloads/desktop-20261028']:
        for path in root.iterdir():
            if path.suffix.lower() not in {'.zip','.7z','.rar'}:continue
            if any(x in path.name.lower() for x in ['rosa','v hud','directx','sa dlss']):continue
            archives.append(path)
    # The author-hosted Brightened download contains a second ZIP; this is an
    # unchanged member of the outer archive, not a locally repacked distribution.
    archives+=list((project/'tools/mod-staging/visual-20261028/extracted/brightened').glob('*.zip'))
    packages=[];lookup=collections.defaultdict(list)
    for path in archives:
        package={'id':path.stem,'local_build_path':str(path),'sha256':digest(path),'bytes':path.stat().st_size,'members':members(path,args.sevenzip)}
        package['id']=package['id']+'-'+package['sha256'][:10]
        packages.append(package)
        for member in package['members']:
            key=(Path(member['member']).name.casefold(),member['bytes'],member['crc32'])
            lookup[key].append({'package':package['id'],'member':member['member']})
    raw=project/'release-work/portable-fetch/originals/CLEO+.cleo'
    if raw.exists():
        package={'id':'cleo-plus-raw','local_build_path':str(raw),'sha256':digest(raw),'bytes':raw.stat().st_size,'format':'raw'}
        packages.append(package);lookup[(raw.name.casefold(),raw.stat().st_size,crc(raw))].append({'package':package['id'],'member':'@file'})
    frozen=json.loads((repo/'installer/profile-index.json').read_text(encoding='utf-8'))
    audit=json.loads((project/'release-work/evidence/portable-installer/reproduction-audit.json').read_text(encoding='utf-8'))
    kinds={f['path']:f['kind'] for f in audit['files']}
    matched=[];missing=[]
    for f in frozen['files']:
        if set(f['components'])&{'rosa-project-evolved','proper-player-retex'}:continue
        if kinds[f['path']] not in ['original_package_member','unresolved_or_authored_patch']:continue
        path=project/'game'/f['path']
        key=(path.name.casefold(),f['bytes'],crc(path))
        choices=lookup.get(key,[])
        row=dict(f,sources=choices)
        (matched if choices else missing).append(row)
    args.out.parent.mkdir(exist_ok=True,parents=True)
    args.out.write_text(json.dumps({'packages':packages,'matched':matched,'missing':missing},ensure_ascii=False),encoding='utf-8')
    print(json.dumps({'packages':len(packages),'matched':len(matched),'missing':len(missing),'missing_paths':[x['path'] for x in missing]},ensure_ascii=False))

if __name__=='__main__':main()
