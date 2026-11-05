"""Collect original license notices for the locked ISO/physics runtime graph."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess

def prepare(repo,out):
    run=subprocess.run(['cargo','metadata','--manifest-path',str(repo/'bootstrap/Cargo.toml'),'--locked','--offline','--format-version','1','--filter-platform','i686-pc-windows-msvc'],capture_output=True,text=True,encoding='utf-8',check=True)
    metadata=json.loads(run.stdout)
    out.mkdir(parents=True,exist_ok=True)
    records=[]
    for package in sorted(metadata['packages'],key=lambda p:(p['name'],p['version'])):
        directory=Path(package['manifest_path']).parent
        files={p for p in directory.iterdir() if p.is_file() and re.match(r'^(?:licen[cs]e|copying|copyright|notice)(?:[._-]|$)',p.name,re.I)}
        if package.get('license_file'):
            path=Path(package['license_file']);files.add(path if path.is_absolute() else directory/path)
        if not files and not package.get('source'):
            files={repo/'bootstrap/COPYING'}
        if not files and package['name']=='mutants' and package['version']=='0.0.3':
            # Upstream omitted its root MIT notice from this published marker
            # crate. Preserve the original notice at its recorded VCS commit.
            files={Path(__file__).with_name('licenses')/'mutants-0.0.3-MIT.txt'}
        if not files:
            raise RuntimeError('Missing original license notice: '+package['name']+' '+package['version'])
        name=package['name']+'-'+package['version']
        destination=out/name;destination.mkdir(exist_ok=True)
        copies=[]
        for file in sorted(files):
            if not file.is_file():raise RuntimeError('License file missing for '+name)
            target=destination/file.name;shutil.copy2(file,target)
            copies.append(dict(path=name+'/'+file.name,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
        records.append(dict(name=package['name'],version=package['version'],license=package['license'],notices=copies))
    # These notices accompany the adapter and ISO support alongside Cargo crates.
    for source,name in [(repo/'bootstrap/COPYING','SA-Delirium-GPL-3.0.txt'),(repo/'vendor/third-party/minhook/LICENSE.txt','MinHook-BSD.txt'),(repo/'licenses/Xenia-BSD-3-Clause.txt','Xenia-BSD-3-Clause.txt'),(repo/'LICENSE.md','SA-Delirium-License.md')]:
        assert source.is_file();shutil.copy2(source,out/name)
    (out/'dependency-licenses.json').write_text(json.dumps(dict(target='i686-pc-windows-msvc',packages=records),indent=2),encoding='utf-8')
    return len(records)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    repo=Path(__file__).resolve().parents[2]
    print(json.dumps(dict(packages=prepare(repo,args.out.resolve()))))
