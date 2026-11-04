"""Collect notices for the active locked Win32 bootstrap dependency graph."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'licenses/Rust-Dependencies.txt')
args=parser.parse_args()
metadata=json.loads(subprocess.check_output(['cargo','metadata','--locked','--offline','--format-version','1','--filter-platform','i686-pc-windows-msvc','--manifest-path',str(ROOT/'bootstrap/Cargo.toml')],cwd=ROOT))
packages={p['id']:p for p in metadata['packages']};nodes={n['id']:n for n in metadata['resolve']['nodes']}
root=next(p['id']for p in packages.values()if p['name']=='sa-delirium-bootstrap')
seen=set();queue=[root]
while queue:
    key=queue.pop()
    if key in seen:continue
    seen.add(key)
    queue.extend(d['pkg']for d in nodes[key]['deps']if any(k['kind']!='dev'for k in d['dep_kinds']))
sections=[];missing=[]
for key in sorted(seen,key=lambda k:(packages[k]['name'],packages[k]['version'])):
    p=packages[key];folder=Path(p['manifest_path']).parent
    files=[f for f in folder.iterdir()if f.is_file()and(f.name.upper().startswith(('LICENSE','COPYING','UNLICENSE')))]
    if p.get('license_file'):
        f=folder/p['license_file']
        if f.is_file()and f not in files:files.append(f)
    # Our GPL snapshot preserves a shared project notice above the crate.
    if not files and not p.get('source'):
        for parent in [folder,*folder.parents]:
            for name in ['COPYING','COPYING.txt']:
                f=parent/name
                if f.is_file():files=[f];break
            if files or parent==ROOT:break
    if not files and not p.get('source')and p.get('license')=='GPL-3.0-only':files=[ROOT/'bootstrap/COPYING']
    if not files and p['name']=='mutants'and p['version']=='0.0.3':files=[ROOT/'licenses/mutants-MIT.txt']
    if not files:missing.append({'name':p['name'],'version':p['version'],'declared_license':p.get('license')});continue
    header=p['name']+' '+p['version']+'\nSPDX: '+str(p.get('license')or'preserved project notice')+'\n'
    if p.get('repository'):header+='Source: '+p['repository']+'\n'
    sections.append(header+'\n'+'\n\n'.join(f.name+'\n'+f.read_text(encoding='utf-8-sig',errors='strict').strip()for f in sorted(files))+'\n')
report={'dependency_packages':len(seen),'notices_collected':len(sections),'missing':missing,'dev_only_dependencies_excluded':True}
print(json.dumps(report))
if missing:raise SystemExit(1)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text('Locked Win32 bootstrap dependency notices\n\n'+'\n'+'\n'.join('\n'+'='*72+'\n'+s for s in sections),encoding='utf-8')
