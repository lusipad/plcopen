"""Verify that the packaged VSIX contains only the locked production npm tree."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import zipfile


def fail(message: str) -> None:
    raise SystemExit(message)


if len(sys.argv) != 2:
    fail("usage: verify_vsix.py <path.vsix>")

vsix_path = Path(sys.argv[1])
inventory_path = Path(__file__).parents[1] / "production-dependencies.json"
inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
expected_packages = {
    dependency["name"]: dependency
    for dependency in inventory["dependencies"]
}

with zipfile.ZipFile(vsix_path) as archive:
    names = set(archive.namelist())
    required = {
        "extension/package.json",
        "extension/extension.js",
        "extension/extension-core.js",
        "extension/language-configuration.json",
        "extension/production-dependencies.json",
        "extension/THIRD_PARTY_NOTICES.md",
    }
    missing = sorted(required - names)
    if missing:
        fail(f"VSIX missing required entries: {missing}")

    forbidden = [
        name
        for name in names
        if (
            "/@vscode/vsce/" in name
            or name.startswith("extension/test/")
            or name.startswith("extension/scripts/")
            or name.startswith("extension/.ruff_cache/")
            or name == "extension/.gitignore"
            or name == "extension/package-lock.json"
            or "/.bin/" in name
        )
    ]
    if forbidden:
        fail(f"VSIX contains development-only entries: {sorted(forbidden)}")

    packaged = {}
    for name in names:
        parts = name.split("/")
        if (
            len(parts) == 4
            and parts[0:2] == ["extension", "node_modules"]
            and parts[3] == "package.json"
        ):
            package_name = parts[2]
        elif (
            len(parts) == 5
            and parts[0:2] == ["extension", "node_modules"]
            and parts[2].startswith("@")
            and parts[4] == "package.json"
        ):
            package_name = f"{parts[2]}/{parts[3]}"
        else:
            continue
        packaged[package_name] = json.loads(archive.read(name))

    if set(packaged) != set(expected_packages):
        fail(
            "VSIX production package set differs from inventory: "
            f"packaged={sorted(packaged)}, expected={sorted(expected_packages)}"
        )
    for name, expected in expected_packages.items():
        manifest = packaged[name]
        if manifest.get("version") != expected["version"]:
            fail(f"{name} version differs from inventory")
        if manifest.get("license") != expected["license"]:
            fail(f"{name} license differs from inventory")

print(
    f"verified {vsix_path}: {len(expected_packages)} production packages, "
    "no development tree"
)
