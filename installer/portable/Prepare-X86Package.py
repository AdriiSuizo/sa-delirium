"""Prepare an x86 installer package from an already verified portable package."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require_x86(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"Not a Windows executable: {path.name}")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset > len(data) - 24 or data[offset:offset + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE header: {path.name}")
    if struct.unpack_from("<H", data, offset + 4)[0] != 0x14C:
        raise ValueError(f"An x86 file is required: {path.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sevenzip", type=Path, required=True)
    parser.add_argument("--delta", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    manifest = json.loads((args.package / "portable-manifest.json").read_text(encoding="utf-8-sig"))
    bundle = args.package / "portable-tools.zip"
    if manifest["lab_only"] or sha(bundle) != manifest["tools_sha256"]:
        raise ValueError("The original package must have a verified production payload")
    replacements = {
        "DeltaTool.exe": args.delta,
        "7z.exe": args.sevenzip / "7z.exe",
        "7z.dll": args.sevenzip / "7z.dll",
        "7-Zip-LICENSE.txt": args.sevenzip / "License.txt",
    }
    for name, path in replacements.items():
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or linked helper: {name}")
        if path.suffix.lower() in {".exe", ".dll"}:
            require_x86(path)
    args.out.mkdir(parents=True, exist_ok=False)
    target = args.out / "portable-tools.zip"
    with zipfile.ZipFile(bundle) as source, zipfile.ZipFile(target, "x", zipfile.ZIP_DEFLATED, compresslevel=7) as destination:
        if not set(replacements) <= set(source.namelist()):
            raise ValueError("The original helper layout changed")
        for entry in source.infolist():
            if entry.is_dir():
                continue
            replacement = replacements.get(entry.filename)
            data = replacement.read_bytes() if replacement else source.read(entry)
            destination.writestr(entry.filename, data)
    manifest["version"] = args.version
    manifest["tools_sha256"] = sha(target)
    (args.out / "portable-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
    shutil.copyfile(args.package / "routing-report.json", args.out / "routing-report.json")
    print(json.dumps({"package": str(args.out), "tools_sha256": manifest["tools_sha256"]}))


if __name__ == "__main__":
    main()
