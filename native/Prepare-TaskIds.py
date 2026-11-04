"""Export only task IDs consumed by our adapter, without copying SDK headers."""
from pathlib import Path
import argparse,hashlib,json,re
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('reference',type=Path)
args=parser.parse_args()
raw=args.reference.read_bytes();text=raw.decode('utf-8-sig')
text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
text=text[text.index('{')+1:text.rindex('}')]
values={};value=-1
for field in text.split(','):
    field=field.strip()
    if not field:continue
    parts=field.split('=');name=parts[0].strip()
    if len(parts)==2:
        v=parts[1].strip();value=int(v,0)if re.fullmatch(r'-?(?:0x[0-9a-fA-F]+|[0-9]+)',v)else values[v]
    else:value+=1
    assert re.fullmatch(r'[A-Z][A-Z0-9_]*',name),name
    values[name]=value
needed=set()
for file in (ROOT/'vendor/src/sa-delirium').iterdir():
    if file.suffix not in ['.hpp','.cpp']:continue
    code=file.read_text(encoding='utf-8-sig')
    code=re.sub(r'/\*.*?\*/|//[^\n]*','',code,flags=re.S)
    needed.update(re.findall(r'\bTASK_[A-Z0-9_]+\b',code))
assert needed<=values.keys(),needed-values.keys()
result={name:values[name]for name in sorted(needed)}
header='#pragma once\n// Native task identifiers used by the existing host adapter.\n// Reference: DK22Pac/plugin-sdk, plugin_sa/game_sa/enums/eTaskType.h\nenum eTaskType : int {\n'
header+='\n'.join(f'    {name} = {value},'for name,value in result.items())+'\n};\n'
(ROOT/'native/NativeTaskIds.hpp').write_text(header,encoding='utf-8',newline='\n')
(ROOT/'native/task-id-provenance.json').write_text(json.dumps({'reference':'https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/enums/eTaskType.h','reference_sha256':hashlib.sha256(raw).hexdigest(),'identifiers':result},indent=2)+'\n',encoding='utf-8')
print(json.dumps({'native_task_identifiers':len(result)}))
