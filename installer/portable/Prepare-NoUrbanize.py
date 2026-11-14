"""Build-only variant: omit the optional Urbanize package, retain other recipes.

This does not repair MSDelta or change the game runtime. It never modifies a
user installation. The variant skips Urbanize completely, avoiding partially
installed CLEO scripts after an Urbanize delta preparation failure.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path

VERSION = "0.4.3-sin-urbanize.1"
UI_REPLACEMENTS = {
    "Urbanize se descarga del autor si está disponible.":
        "Esta variante del pack completo no instala Urbanize.",
    "Delirium, fixes, aspecto PS2, Trainer, Mod Loader, CLEO, Urbanize e Izzy.":
        "Delirium, fixes, aspecto PS2, Mod Loader, CLEO e Izzy. Variante sin Urbanize.",
}


def make_variant(original: dict, expected_version: str = "0.4.3") -> tuple[dict, dict]:
    if original.get("version") != expected_version or original.get("lab_only"):
        raise ValueError("Unexpected base version or private lab package")
    result = copy.deepcopy(original)
    packages = original["packages"]
    ids = {p["id"] for p in packages if p["id"].lower().startswith("urbanize-")}
    if len(ids) != 1 or not all(p.get("optional") for p in packages if p["id"] in ids):
        raise ValueError("Expected exactly one optional Urbanize package")
    removed = []
    kept = []
    for recipe in original["files"]:
        path = recipe["path"].replace("\\", "/").lower()
        components = {c.lower() for c in recipe.get("components", [])}
        belongs = recipe.get("package") in ids or "urbanize" in components or path.startswith("cleo/urbanize/")
        if belongs:
            if recipe.get("package") not in ids or components - {"urbanize"}:
                raise ValueError("Unexpected shared Urbanize recipe: " + recipe["path"])
            removed.append(recipe)
        else:
            kept.append(recipe)
    removed_paths = {r["path"] for r in removed}
    if "cleo/Urbanize/Urbanize - Root.cs" not in removed_paths:
        raise ValueError("Reported failing Urbanize script was not found")
    result["version"] = VERSION
    result["files"] = copy.deepcopy(kept)
    result["packages"] = [copy.deepcopy(p) for p in packages if p["id"] not in ids]
    result["profile"]["files"] = [f for f in result["profile"]["files"] if f["path"] not in removed_paths]
    result["profile"]["total_bytes"] = sum(f["bytes"] for f in result["profile"]["files"])
    result["profile"]["components"] = [c for c in result["profile"]["components"] if c["id"].lower() != "urbanize"]
    note = "Urbanize omitido en esta variante para evitar el fallo al preparar Urbanize - Root.cs."
    result.setdefault("omissions", []).append(note)
    # This is a precise package selection, not a rewritten gameplay profile.
    if result["files"] != kept:
        raise ValueError("A retained recipe was changed")
    if result["tools_sha256"] != original["tools_sha256"]:
        raise ValueError("Embedded payloads must stay unchanged")
    if any(f.get("package") in ids or f["path"] in removed_paths for f in result["files"]):
        raise ValueError("An Urbanize recipe remains active")
    summary = {
        "base_version": expected_version,
        "version": VERSION,
        "workaround_not_root_cause_fix": True,
        "omitted_packages": sorted(ids),
        "omitted_files": len(removed),
        "retained_files": len(kept),
        "retained_recipes_identical": True,
        "embedded_payloads_unchanged": True,
        "removed_paths": sorted(removed_paths),
    }
    return result, summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    args = parser.parse_args()
    package = args.package.resolve()
    manifest_path = package / "portable-manifest.json"
    original = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    variant, summary = make_variant(original)
    tools = package / "portable-tools.zip"
    digest = hashlib.sha256(tools.read_bytes()).hexdigest()
    if digest != original["tools_sha256"]:
        raise ValueError("Existing tools bundle does not match its own manifest")
    ui_path = args.repo.resolve() / "installer" / "portable" / "PortableSetup.cs"
    ui = ui_path.read_text(encoding="utf-8-sig")
    for before, after in UI_REPLACEMENTS.items():
        if ui.count(before) != 1:
            raise ValueError("Unexpected UI source; no output written: " + before)
        ui = ui.replace(before, after)
    manifest_path.write_text(json.dumps(variant, ensure_ascii=False), encoding="utf-8")
    ui_path.write_text(ui, encoding="utf-8")
    (package / "variant-validation.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items() if k != "removed_paths"}))


if __name__ == "__main__":
    main()
