"""Prepare the native road-prop probe for the shared collision code."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def inject(out: Path) -> None:
    here = Path(__file__).resolve().parent
    shutil.copyfile(here / "RoadPropFixture.hpp", out / "RoadPropFixture.hpp")
    probe = out / "ExpansionProbe.hpp"
    text = probe.read_text(encoding="utf-8")
    # The road fixture uses the same pedestrian reference as the other probes.
    text = text.replace("static int expansionTarget=-1;",
                        'static int expansionTarget=-1;\n#include "RoadPropFixture.hpp"', 1)
    entry = "static int expansionFixture(int mode){"
    assert text.count(entry) == 1
    text = text.replace(entry, entry + "\n    if((mode>=291&&mode<=293)||mode==295||mode==296)return roadPropFixture(mode);")
    probe.write_text(text, encoding="utf-8", newline="\n")
    adapter = out / "Adapter.cpp"
    text = adapter.read_text(encoding="utf-8")
    tick = "collision->update(worldOrigin);cost.lap(0);"
    assert text.count(tick) == 1
    text = text.replace(tick, "collision->update(worldOrigin);observeRoadProp();cost.lap(0);")
    entry = "    if(mode==46)return recoveryAuditFixture(mode);"
    assert text.count(entry) == 1
    text = text.replace(entry, "    if((mode>=291&&mode<=293)||mode==295||mode==296)return expansionFixture(mode);\n" + entry)
    text = text.replace('#include "BodyContacts.hpp"',
                        'static void observeRoadProp();\n#include "BodyContacts.hpp"', 1)
    adapter.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    builder = here.parent / "installer/portable/Prepare-DevelopmentRuntime.py"
    subprocess.run([sys.executable, str(builder), "--out", str(args.out),
                    "--include-test-diagnostics"], check=True)
    inject(args.out)
