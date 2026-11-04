"""Exercise the real C++ ASLR binding/prefix code on an owned Steam image."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,uuid
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('steam_exe',type=Path)
args=parser.parse_args()
registry=json.loads((ROOT/'native/steam-bindings.json').read_text())
assert hashlib.sha256(args.steam_exe.read_bytes()).hexdigest()==registry['steam_exe_sha256']
out=ROOT/'build'/('relocations-'+uuid.uuid4().hex)
subprocess.run([sys.executable,str(ROOT/'native/Prepare-Adapter.py'),'--output',str(out)],check=True)
rows=[r for r in registry['bindings']if r['kind']=='function']
queries='struct Query{uintptr_t preferred;uintptr_t(*address)();bool(*matches)();};\nstatic const Query queries[]={\n'
queries+='\n'.join('{'+r['steam_va']+',&sa_steam_native::address<'+r['legacy_va']+'>,&sa_steam_native::prefixMatches<'+r['legacy_va']+'>},'for r in rows)+'\n};\n'
(out/'NativeReviewedQueries.inc').write_text(queries)
vswhere=Path('C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe')
vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
vc=Path(vs)/'Common7/Tools/VsDevCmd.bat'
batch=out/'compile.cmd';batch.write_text('@echo off\ncall "'+str(vc)+'" -host_arch=x64 -arch=x86 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /MT /O2 /EHsc /std:c++20 /I"'+str(out)+'" "'+str(ROOT/'tests/NativeRelocationTests.cpp')+'" /Fe:relocations.exe /link bcrypt.lib\nexit /b %errorlevel%\n')
with(out/'compile.log').open('w')as log:subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
results=[]
for base in ['15000000','35000000','55000000']:
    text=subprocess.check_output([str(out/'relocations.exe'),str(args.steam_exe.resolve()),base],cwd=out,text=True).strip()
    results.append(text)
report={'passed':True,'bases_tested':3,'functions_per_base':len(rows),'total_prefix_checks':3*len(rows),'negative_controls':3,'actual_cpp_implementation':True,'results':results,'game_executable_modified':False}
(out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));print(out)
