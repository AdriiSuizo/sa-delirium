"""Check native model-store guards against the owner's file at several ASLR bases.

Build/review dependency: pefile. No game bytes are emitted or copied to source.
"""
from pathlib import Path
import argparse, hashlib, json, re, struct
import pefile

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', required=True, type=Path)
    a = ap.parse_args(); exe = a.game / 'gta-sa.exe'
    root = Path(__file__).resolve().parents[1]
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == '15e3cfedba9a841df67d8194e7249afb493b0e10d6138fb8ebab2c136e543efb'
    pe = pefile.PE(str(exe)); original = bytearray(pe.get_memory_mapped_image())
    relocations = [e.rva for block in pe.DIRECTORY_ENTRY_BASERELOC for e in block.entries if e.type == 3]
    header = re.sub(r'\s+', '', (root/'native/SteamModelStoreBindings.hpp').read_text())
    hashes = {bytes(int(v,16) for v in re.findall(r'0x[0-9a-f]+',m)) for m in re.findall(r'\{((?:0x[0-9a-f]+,?){32})\}',header)}
    docs = [json.loads((root/'native'/name).read_text()) for name in
            ['steam-model-store-bindings.json', 'steam-time-model-store-bindings.json', 'steam-atomic-model-store-bindings.json']]
    rows = []; references = 0
    for doc in docs:
        base = int(doc['original_counter'],16)
        for row in doc['references']:
            rva = int(row['instruction_va'],16)-0x400000
            op = int(row['operand_va'],16)-0x400000
            expected = bytes.fromhex(row['canonical_prefix_sha256'])
            assert expected in hashes, 'Runtime header differs from reviewed metadata'
            assert struct.unpack_from('<I',original,op)[0] == base+row['delta']
            assert hashlib.sha256(original[rva:rva+7]).digest() == expected
            rows.append((rva,expected)); references += 1
        for key in ['native_constructor','native_add']:
            rva = int(doc[key],16)-0x400000
            expected = hashlib.sha256(original[rva:rva+7]).digest()
            assert expected in hashes, key+' runtime guard uses the wrong prefix'
            rows.append((rva,expected))
    checks = 0
    for base in [0x400000,0x1280000,0x1800000]:
        delta = base-0x400000; live = bytearray(original)
        for rva in relocations:
            struct.pack_into('<I',live,rva,(struct.unpack_from('<I',live,rva)[0]+delta)&0xffffffff)
        for rva,expected in rows:
            prefix = bytearray(live[rva:rva+10])
            for fixup in relocations:
                if rva <= fixup < rva+7:
                    off = fixup-rva
                    struct.pack_into('<I',prefix,off,(struct.unpack_from('<I',prefix,off)[0]-delta)&0xffffffff)
            assert hashlib.sha256(prefix[:7]).digest() == expected
            prefix[0] ^= 1
            assert hashlib.sha256(prefix[:7]).digest() != expected
            checks += 2
    print(json.dumps({'status':'passed','stores':len(docs),'reviewed_references':references,
                      'aslr_bases':3,'positive_and_negative_checks':checks,'game_bytes_emitted':False}))

if __name__ == '__main__': main()
