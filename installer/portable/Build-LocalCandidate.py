"""PRIVATE verification fixture; deliberately cannot produce a release.

Embeds retained third-party runtime files for a two-input technical test. Their
distribution audit is NOT complete. Never upload this payload/EXE. Final release
must replace restricted components with verified original-author downloads.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import shutil
import subprocess
import zipfile

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();project=args.project.resolve();repo=Path(__file__).resolve().parents[2]
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    payload=out/'helpers';payload.mkdir();(payload/'patches').mkdir();(payload/'payload').mkdir()
    frozen=json.loads((repo/'installer/profile-index.json').read_text(encoding='utf-8'))
    stock=json.loads((project/'release-work/evidence/native-dev5-clean-extract.json').read_text(encoding='utf-8'))['stock_sha256']
    stock_lookup={n.casefold():(n,h) for n,h in stock.items()};by_hash={h:n for n,h in stock.items()}
    original=project/'release-work/lab-release-native-dev6'
    runtime=repo/'build/bootstrap-portable/i686-pc-windows-msvc/release'
    for src,name in [(repo/'build/portable-tools/DeltaTool.exe','DeltaTool.exe'),(runtime/'sa-delirium-bootstrap.exe','sa-delirium-bootstrap.exe'),
        (Path(r'C:\Program Files\7-Zip\7z.exe'),'7z.exe'),(Path(r'C:\Program Files\7-Zip\7z.dll'),'7z.dll'),
        (Path(r'C:\Program Files\7-Zip\License.txt'),'7-Zip-LICENSE.txt')]:shutil.copy2(src,payload/name)
    recipes=[];omissions=[]
    for f in frozen['files']:
        name=f['path'];components=f['components']
        if name.startswith('DeliriumSA/') or set(components)&{'rosa-project-evolved','proper-player-retex','izzy-s-lively-vehicle-physics'} or name in {'gta-sa.exe','cleo/cleo_saves/cs1.sav','GrinchTrainerSA/update.json','delirium_physics.dll'}:
            omissions.append(name);continue
        source=project/'game'/name
        expected=f['sha256'];size=f['bytes'];kind=None;extra={}
        if name=='scripts/DeliriumSA.asi':source=repo/'build/portable-development-runtime4/DeliriumSA.asi';expected=sha(source);size=source.stat().st_size
        if name=='scripts/III.VC.SA.WindowedMode.cfg':
            source=out/'automatic-window.cfg'
            source.write_text('; SA Delirium: use the current PC desktop resolution.\n[MAIN]\nWindowMode=2\nConfineCursor=0\nPauseOnFocusLoss=0\nResumeOnFocus=0\n[WINDOW]\nWidth=0\nHeight=0\nMaximized=0\nPositionX=-1\nPositionY=-1\n',encoding='ascii',newline='\n')
            expected=sha(source);size=source.stat().st_size
        key=name.casefold()
        baseline=stock_lookup.get(key)
        if name=='gta_sa.exe':baseline=stock_lookup['gta-sa.exe']
        if name=='vorbisFile.dll':baseline=None  # ASI loader, not Steam's original Vorbis DLL
        # Steam supplies the model banks and audio it owns. Only actual version
        # compatibility inputs are patched; no restored radio streams are shipped.
        if baseline:
            original_name,h=baseline
            if name in ['anim/anim.img','data/script/main.scm','data/script/script.img','gta_sa.exe']:
                assert sha(source)==expected
                base=original/original_name;assert sha(base)==h,(original_name,h,sha(base))
                patch_name=hashlib.sha256(name.encode()).hexdigest()[:20]+'.pa30'
                patch=payload/'patches'/patch_name
                subprocess.run([str(payload/'DeltaTool.exe'),'make',str(base),str(source),str(patch)],check=True,stdout=subprocess.DEVNULL)
                kind='steam_delta';extra=dict(source=original_name,source_sha256=h,patch=patch_name)
            else:
                base=original/original_name;assert sha(base)==h
                kind='steam';expected=h;size=base.stat().st_size;extra=dict(source=original_name)
        elif expected in by_hash:
            kind='steam';extra=dict(source=by_hash[expected]);base=original/extra['source']
            assert sha(base)==expected,(name,'immutable stock source changed')
            size=base.stat().st_size
        else:
            assert sha(source)==expected,(name,'reference changed')
            target=payload/'payload'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
            kind='payload';extra=dict(source=name)
        recipes.append(dict(path=name,sha256=expected,bytes=size,components=components,kind=kind,**extra))
    name='scripts/sa_delirium_bootstrap.dll';target=payload/'payload'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(runtime/'sa_delirium_bootstrap.dll',target)
    recipes.append(dict(path=name,sha256=sha(target),bytes=target.stat().st_size,components=['delirium'],kind='payload',source=name))
    index=dict(frozen)
    index.update(profile='steam-owned-iso-no-rosa-local-candidate',files=[{k:r[k] for k in ['path','bytes','sha256','components']} for r in recipes],total_bytes=sum(r['bytes'] for r in recipes))
    for c in index['components']:
        c['requested']=c['requested'] and c['id'] not in ['rosa-project-evolved','proper-player-retex','izzy-s-lively-vehicle-physics']
        c['files']=sum(c['id'] in r['components'] for r in recipes);c['bytes']=sum(r['bytes'] for r in recipes if c['id'] in r['components'])
    manifest=dict(schema=1,lab_only=True,profile=index,files=recipes,packages=[],omissions=['GTA V HUD / minimap / weapon wheel','RoSA / bundled Proper Player Retex',"Izzy's Lively Vehicle Physics",'Author saves and runtime-generated state'])
    (out/'portable-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False),encoding='utf-8')
    (out/'DO-NOT-PUBLISH.txt').write_text('PRIVATE TECHNICAL VERIFICATION ONLY. Contains third-party files awaiting distribution review. Never upload this EXE or payload.\n',encoding='utf-8')
    with zipfile.ZipFile(out/'portable-tools.zip','w',zipfile.ZIP_DEFLATED,compresslevel=1,allowZip64=True) as z:
        for f in payload.rglob('*'):
            if f.is_file():z.write(f,f.relative_to(payload).as_posix())
    manifest['tools_sha256']=sha(out/'portable-tools.zip')
    (out/'portable-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False),encoding='utf-8')
    receipt={'status':'local_verification_only_NOT_A_RELEASE','files':len(recipes),'bytes':index['total_bytes'],'payload_zip_bytes':(out/'portable-tools.zip').stat().st_size,'omitted_reference_paths':omissions}
    (out/'build-receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print(json.dumps(receipt|{'omitted_reference_paths':len(omissions)}),flush=True)

if __name__=='__main__':main()
