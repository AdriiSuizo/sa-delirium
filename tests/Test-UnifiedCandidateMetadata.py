"""Exercise the real unified builder with authored fixtures, without game data."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
root = args.out.resolve()
root.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[1]
base, native, bootstrap, decoder = [root / name for name in ("base", "native", "bootstrap", "decoder")]
for directory in (base, native, bootstrap, decoder):
    directory.mkdir()
hash_data = lambda data: hashlib.sha256(data).hexdigest()
originals = {
    "scripts/DeliriumSA.asi": b"old-native",
    "scripts/sa_delirium_bootstrap.dll": b"old-bootstrap",
    "scripts/sa_delirium_bootstrap_avx2.dll": b"old-avx2",
    "DeliriumSA.ini": b"[OwnerPreference]\nKeep=1\n",
    "mods/Izzy/LivelyVehiclePhysics.asi": b"unchanged-approved-fixture-asi",
    "mods/Izzy/LivelyVehiclePhysics.ini": b"unchanged-approved-fixture-ini",
    "mods/Izzy/README_or_dont.txt": b"unchanged-approved-fixture-notice",
}
files = [{"path": name, "source": name, "kind": "payload", "bytes": len(data),
          "sha256": hash_data(data), "components": ["izzy-s-lively-vehicle-physics" if "/Izzy/" in name else "delirium"]}
         for name, data in originals.items()]
with zipfile.ZipFile(base / "portable-tools.zip", "w") as archive:
    for name, data in originals.items():
        archive.writestr("payload/" + name, data)
    archive.writestr("sa-delirium-bootstrap.exe", b"old-helper")
manifest = {
    "schema": 1, "version": "0.4.0", "lab_only": False, "files": files,
    "tools_sha256": hash_data((base / "portable-tools.zip").read_bytes()),
    "profile": {"profile": "sa-delirium-unified", "files": [], "total_bytes": -1,
                "components": [{"id": identity, "name": identity, "requested": identity != "unused",
                                "files": 999, "bytes": 999, "extra": "preserved"}
                               for identity in ("delirium", "izzy-s-lively-vehicle-physics", "unused")]},
}
(base / "portable-manifest.json").write_text(json.dumps(manifest))
(base / "routing-report.json").write_text(json.dumps({"unresolved": [], "status": "fixture"}))
(native / "source-receipt.json").write_text(json.dumps({"fixtures": "authoring hooks and exports omitted"}))
(native / "DeliriumSA.asi").write_bytes(b"new-native")
(bootstrap / "sa_delirium_bootstrap.dll").write_bytes(b"baseline-runtime")
(bootstrap / "sa-delirium-bootstrap.exe").write_bytes(b"new-helper")
(root / "avx2.dll").write_bytes(b"avx2-runtime")
for name in ("vgmstream-cli.exe", "COPYING"):
    (decoder / name).write_bytes(b"authored-decoder-fixture")

for index, (version, public, avx2) in enumerate((("0.4.0", True, True), ("0.4.1", True, True),
                                              ("0.4.1-rc.1", False, False))):
    output = root / ("candidate-" + str(index))
    command = [sys.executable, str(repo / "installer/portable/Build-UnifiedCandidate.py"),
               "--base", str(base), "--native", str(native), "--bootstrap", str(bootstrap),
               "--decoder", str(decoder), "--version", version, "--output", str(output)]
    if avx2:
        command += ["--avx2", str(root / "avx2.dll")]
    subprocess.run(command, check=True)
    result = json.loads((output / "portable-manifest.json").read_text())
    routing = json.loads((output / "routing-report.json").read_text())
    profile = result["profile"]
    assert profile["profile"] == ("sa-delirium-unified" if public else "sa-delirium-unified-private")
    assert routing["status"] == ("unified_candidate" if public else "private_unified_candidate")
    components = {entry["id"]: entry for entry in profile["components"]}
    assert components["delirium"]["files"] == (4 if avx2 else 3)
    assert components["izzy-s-lively-vehicle-physics"]["files"] == 3
    assert components["unused"]["files"] == components["unused"]["bytes"] == 0
    assert all(entry["extra"] == "preserved" for entry in components.values())
    assert components["unused"]["requested"] is False
    with zipfile.ZipFile(output / "portable-tools.zip") as archive:
        assert archive.read("sa-delirium-bootstrap.exe") == b"new-helper"
        assert len(archive.namelist()) == len(set(archive.namelist()))
        for recipe in result["files"]:
            data = archive.read("payload/" + recipe["source"])
            assert recipe["bytes"] == len(data) and recipe["sha256"] == hash_data(data)
            if recipe["path"].startswith("DeliriumDrivers/Izzy/"):
                assert data == originals[recipe["source"]]
        ini = archive.read("payload/DeliriumSA.ini")
        assert b"[OwnerPreference]" in ini and b"Keep=1" in ini
        expected_delirium = len(b"new-native") + len(b"baseline-runtime") + len(ini)
        if avx2:
            expected_delirium += len(b"avx2-runtime")
        assert components["delirium"]["bytes"] == expected_delirium
        expected_izzy = sum(len(data) for name, data in originals.items() if "/Izzy/" in name)
        assert components["izzy-s-lively-vehicle-physics"]["bytes"] == expected_izzy
        assert profile["total_bytes"] == routing["bytes"] == expected_delirium + expected_izzy
    assert routing["files"] == len(result["files"]) == (7 if avx2 else 6)
print("Passed stable 0.4.0/0.4.1, prerelease, AVX2 omission, retained bytes and component metadata checks.")
