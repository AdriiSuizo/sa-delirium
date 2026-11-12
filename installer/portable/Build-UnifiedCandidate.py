"""Build a private unified candidate from a verified portable base and current runtime."""
import argparse
import configparser
import io
import json
from pathlib import Path
import zipfile
import hashlib


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--bootstrap", required=True, type=Path)
    parser.add_argument("--decoder", required=True, type=Path)
    parser.add_argument("--avx2", type=Path)
    parser.add_argument("--version", default="0.3.14-unified-preview.1")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.base / "portable-manifest.json").read_text(encoding="utf-8-sig"))
    routing = json.loads((args.base / "routing-report.json").read_text(encoding="utf-8-sig"))
    source_bundle = args.base / "portable-tools.zip"
    if digest(source_bundle.read_bytes()) != manifest["tools_sha256"] or routing["unresolved"]:
        raise ValueError("The base package failed verification")
    receipt = json.loads((args.native / "source-receipt.json").read_text(encoding="utf-8"))
    if receipt["fixtures"] != "authoring hooks and exports omitted":
        raise ValueError("Use a shipping adapter without the test bridge")
    files = {file["path"]: file for file in manifest["files"]}
    updates = {"sa-delirium-bootstrap.exe": (args.bootstrap / "sa-delirium-bootstrap.exe").read_bytes()}
    omitted = set()

    def payload(path, data):
        recipe = files[path]
        if recipe["kind"] != "payload":
            raise ValueError("Unexpected source kind for " + path)
        updates["payload/" + recipe["source"]] = data
        recipe.update(bytes=len(data), sha256=digest(data))

    payload("scripts/DeliriumSA.asi", (args.native / "DeliriumSA.asi").read_bytes())
    payload("scripts/sa_delirium_bootstrap.dll", (args.bootstrap / "sa_delirium_bootstrap.dll").read_bytes())
    avx2 = "scripts/sa_delirium_bootstrap_avx2.dll"
    if args.avx2:
        payload(avx2, args.avx2.read_bytes())
    else:
        omitted.add("payload/" + files.pop(avx2)["source"])
    izzy = [file for file in files.values() if file["components"] == ["izzy-s-lively-vehicle-physics"]]
    if {Path(file["path"]).name for file in izzy} != {"LivelyVehiclePhysics.asi", "LivelyVehiclePhysics.ini", "README_or_dont.txt"}:
        raise ValueError("Unexpected layout for the approved Izzy files")
    for file in izzy:
        del files[file["path"]]
        file["path"] = "DeliriumDrivers/Izzy/" + Path(file["path"]).name
        files[file["path"]] = file

    decoder = args.decoder
    for file in sorted(decoder.iterdir()):
        if file.name in {"vgmstream-cli.exe", "COPYING", "README.md", "USAGE.md"} or file.suffix.lower() == ".dll":
            updates["vgmstream/" + file.name] = file.read_bytes()
    if not {"vgmstream/vgmstream-cli.exe", "vgmstream/COPYING"}.issubset(updates):
        raise ValueError("The decoder and its license are required")

    args.output.mkdir(parents=True, exist_ok=False)
    bundle = args.output / "portable-tools.zip"
    with zipfile.ZipFile(source_bundle) as old, zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as new:
        config = configparser.ConfigParser(interpolation=None)
        config.optionxform = str
        config.read_string(old.read("payload/" + files["DeliriumSA.ini"]["source"]).decode("utf-8-sig"))
        for section, key, value in (("VehicleLab", "Mode", "IV"), ("Combat", "Enabled", "1"),
                                    ("Movement", "NikoOnFoot", "1"), ("VehicleDeformation", "Enabled", "1")):
            if not config.has_section(section):
                config.add_section(section)
            config.set(section, key, value)
        text = io.StringIO()
        config.write(text, space_around_delimiters=False)
        payload("DeliriumSA.ini", text.getvalue().replace("\n", "\r\n").encode("utf-8"))
        for info in old.infolist():
            if info.filename not in updates and info.filename not in omitted:
                new.writestr(info.filename, old.read(info.filename))
        for name, data in updates.items():
            new.writestr(name, data)

    manifest.update(unified=True, version=args.version, files=list(files.values()),
                    tools_sha256=digest(bundle.read_bytes()))
    profile = manifest["profile"]
    profile["profile"] = "sa-delirium-unified" if args.version == "0.4.0" else "sa-delirium-unified-private"
    profile["files"] = [{key: file[key] for key in ("path", "bytes", "sha256", "components")} for file in manifest["files"]]
    profile["total_bytes"] = sum(file["bytes"] for file in manifest["files"])
    routing.update(status="private_unified_candidate", files=len(profile["files"]), bytes=profile["total_bytes"])
    (args.output / "portable-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
    (args.output / "routing-report.json").write_text(json.dumps(routing, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
