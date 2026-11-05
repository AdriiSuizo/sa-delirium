"""Prepare Microsoft's documented minimal DirectSetup redistribution.

The signed official June2010 archive is a build input. Do not copy system DLLs.
Six original redist files provide only D3DX9_43 x86 and XInput1_3 x86.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess

DIRECTX_SHA='053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b'
MEMBERS=['DXSETUP.exe','DSETUP.dll','dsetup32.dll','dxupdate.cab','Jun2010_d3dx9_43_x86.cab','APR2007_xinput_x86.cab']

def prepare(project,out):
    source=project/'release-work/portable-fetch/originals/directx_Jun2010_redist.exe'
    assert hashlib.sha256(source.read_bytes()).hexdigest()==DIRECTX_SHA
    out.mkdir(parents=True,exist_ok=True)
    records=[]
    for name in MEMBERS:
        data=subprocess.run([r'C:\Program Files\7-Zip\7z.exe','x','-so','-spd','-p-',str(source),name],capture_output=True,check=True).stdout
        assert data and len(data)<5*1024*1024
        (out/name).write_bytes(data)
        records.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    (out/'SOURCE.txt').write_text('Microsoft DirectX End-User Runtimes (June 2010)\nOriginal setup files and two required x86 components; no system DLL copies.\nSource: https://www.microsoft.com/en-us/download/details.aspx?id=8109\nMinimal DirectSetup distribution: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/directx-setup-for-game-developers\nMicrosoft software terms are supplied in the unchanged Microsoft setup.\n',encoding='utf-8')
    (out/'source-receipt.json').write_text(json.dumps(dict(source_sha256=DIRECTX_SHA,files=records),indent=2),encoding='utf-8')
    return records

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--project',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    print(json.dumps(prepare(args.project.resolve(),args.out.resolve())))
