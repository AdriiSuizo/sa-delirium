"""Prepare the existing lab bridge for native controller-camera tests; no OS input."""
import argparse
from pathlib import Path
import shutil
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('out',type=Path);args=p.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
for file in args.source.glob('*.hpp'):shutil.copyfile(file,args.out/file.name)
s=(args.source/'AgentBridge.cpp').read_text()
needle='    if(!testSession)return;'
assert s.count(needle)==1
s=s.replace(needle,needle+'\n    *reinterpret_cast<bool*>(0xB6EC2E)=false; // Isolated controller camera only.')
needle='        auditPad[0]=short(std::clamp(auditNumber(text,"sx"),-128.f,128.f));'
assert s.count(needle)==1
s=s.replace(needle,needle+'\n        auditPad[2]=short(std::clamp(auditNumber(text,"rx"),-128.f,128.f));\n        auditPad[3]=short(std::clamp(auditNumber(text,"ry"),-128.f,128.f));')
(args.out/'AgentBridge.cpp').write_text(s)
