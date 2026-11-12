"""Compare the private runtime's body advance with the owned TU8 executable."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--tools", type=Path, required=True, help="Read-only LSC4 tools/xbox360-re")
parser.add_argument("--image", type=Path, required=True, help="Owned unpacked TU8 image")
parser.add_argument("--program", type=Path, required=True, help="Delirium's imported owned IV program")
parser.add_argument("--replay", type=Path, required=True, help="Built vehicle_advance_cases example")
parser.add_argument("--output", type=Path, required=True, help="New local scratch directory")
parser.add_argument("--cases", type=int, default=512)
args = parser.parse_args()

args.output.mkdir(parents=True)
for name in ("ppcrun.py", "single_rounding.py", "vmxpatch.py", "vmx128dis.py"):
    shutil.copyfile(args.tools / name, args.output / name)
source = (args.tools / "checks/advance.py").read_text(encoding="utf-8")
start = source.index("    got = subprocess.run(")
stop = source.index("    if got.returncode:", start)
source = (
    source[:start]
    + "    got = subprocess.run([os.environ['SA_ADVANCE_EXE']], "
      "input=chr(10).join(lines) + chr(10), capture_output=True, text=True)\n"
    + source[stop:]
)
script = args.output / "advance.py"
script.write_text(source, encoding="utf-8")
environment = dict(os.environ, GTA4_IMAGE=str(args.image.resolve()),
                   SA_IV_PROGRAM=str(args.program.resolve()), SA_ADVANCE_EXE=str(args.replay.resolve()),
                   PYTHONDONTWRITEBYTECODE="1")
raise SystemExit(subprocess.run([sys.executable, str(script.resolve()), str(args.cases)],
                               cwd=args.output, env=environment).returncode)
