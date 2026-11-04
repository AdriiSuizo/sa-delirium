"""Reject a Win32 runtime that would need an unbundled VC++ installation.

Reads the PE import directory using only Python's standard library. No DLL is
loaded or executed. The system DLL allowlist is for supported Windows 10/11.
"""
from pathlib import Path
import argparse,json,struct
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('runtime',type=Path)
args=parser.parse_args();data=args.runtime.read_bytes()
u16=lambda at:struct.unpack_from('<H',data,at)[0]
u32=lambda at:struct.unpack_from('<I',data,at)[0]
assert data[:2]==b'MZ';pe=u32(0x3c)
assert data[pe:pe+4]==b'PE\0\0'and u16(pe+4)==0x14c,'Expected Win32 PE'
optional=pe+24
assert u16(optional)==0x10b,'Expected PE32 optional header'
sections=optional+u16(pe+20)
def raw(rva):
    if rva<u32(optional+60):return rva
    for i in range(u16(pe+6)):
        at=sections+i*40;size,va,stored,offset=struct.unpack_from('<4I',data,at+8)
        if va<=rva<va+max(size,stored):
            assert rva-va<stored,'Import points outside stored section'
            return offset+rva-va
    raise ValueError('Unmapped import RVA')
imports=[];table=u32(optional+104)
if table:
    at=raw(table)
    for i in range(256):
        descriptor=struct.unpack_from('<5I',data,at+i*20)
        if not any(descriptor):break
        name=raw(descriptor[3]);end=data.index(0,name)
        imports.append(data[name:end].decode('ascii').lower())
    else:raise ValueError('Import table lacks terminator')
allowed={'kernel32.dll','api-ms-win-core-synch-l1-2-0.dll','bcryptprimitives.dll','advapi32.dll','ntdll.dll'}
unexpected=sorted(set(imports)-allowed)
print(json.dumps({'runtime':args.runtime.name,'imports':sorted(set(imports)),
                  'unbundled_dependencies':unexpected,'passed':not unexpected}))
raise SystemExit(1 if unexpected else 0)
