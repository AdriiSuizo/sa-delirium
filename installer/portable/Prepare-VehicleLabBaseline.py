"""Derive the private no-Izzy baseline from the verified 0.3.13 build."""

import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


VERSION = "0.3.13-vehicle-lab.baseline.1"
PROFILE = "vehicle-lab-baseline-1"
COMPONENT = "izzy-s-lively-vehicle-physics"
MANIFEST_SHA256 = "790706899929934a55f3a821886db3e1f33c90fafad5e7659542ca9b3af6e669"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def is_izzy(file):
    components = file.get("components", [])
    if COMPONENT not in components:
        return False
    if components != [COMPONENT]:
        raise ValueError(f"Shared component needs review: {file['path']}")
    return True


def prepare(source, output):
    manifest_path = source / "portable-manifest.json"
    source_bundle = source / "portable-tools.zip"
    if digest(manifest_path) != MANIFEST_SHA256:
        raise ValueError("Expected the verified 0.3.13 manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if digest(source_bundle) != manifest["tools_sha256"]:
        raise ValueError("0.3.13 tools bundle fingerprint mismatch")

    removed = [file for file in manifest["files"] if is_izzy(file)]
    omitted_entries = {f"payload/{file['source']}" for file in removed}
    if len(omitted_entries) != 3 or any(file["kind"] != "payload" for file in removed):
        raise ValueError("Unexpected Izzy payload layout")
    manifest["files"] = [file for file in manifest["files"] if not is_izzy(file)]
    profile = manifest["profile"]
    profile["files"] = [file for file in profile["files"] if not is_izzy(file)]
    profile["components"] = [item for item in profile["components"] if item["id"] != COMPONENT]
    profile["profile"] = PROFILE
    profile["total_bytes"] = sum(file["bytes"] for file in profile["files"])
    manifest.update(version=VERSION, lab_only=True)
    manifest["omissions"].append("Izzy's Lively Vehicle Physics")

    with ZipFile(source_bundle) as original:
        entries = original.namelist()
        if len(entries) != len(set(entries)) or not omitted_entries.issubset(entries):
            raise ValueError("Unexpected tools archive layout")
        retained = [entry for entry in entries if entry not in omitted_entries]
        if any("livelyvehiclephysics" in entry.lower() or "lively vehicle physics" in entry.lower()
               for entry in retained):
            raise ValueError("Unaccounted Izzy payload")
        output.mkdir(parents=True)
        output_bundle = output / source_bundle.name
        with ZipFile(output_bundle, "x", ZIP_DEFLATED, compresslevel=7) as candidate:
            for entry in retained:
                candidate.writestr(entry, original.read(entry))

    manifest["tools_sha256"] = digest(output_bundle)
    (output / manifest_path.name).write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"profile": PROFILE, "removed": sorted(omitted_entries),
                      "tools_sha256": manifest["tools_sha256"]}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="A new directory")
    args = parser.parse_args()
    prepare(args.source.resolve(), args.output.resolve())
