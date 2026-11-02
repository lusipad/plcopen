"""Verify that the packaged VSIX contains only the locked production npm tree."""

from __future__ import annotations

import hashlib
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
lock = json.loads(inventory_path.with_name("package-lock.json").read_text(encoding="utf-8"))
expected_package_paths = {
    f"extension/{package_path}/package.json"
    for package_path, entry in lock["packages"].items()
    if package_path.startswith("node_modules/") and entry.get("dev") is not True
}
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
        "extension/client-compatibility.json",
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
    packaged_paths = set()
    for name in names:
        parts = name.split("/")
        if parts[-1] != "package.json" or "node_modules" not in parts:
            continue
        # Scoped production overrides may be nested under their consumer.
        index = len(parts) - 1 - parts[::-1].index("node_modules")
        suffix = parts[index + 1 : -1]
        if len(suffix) == 1 and not suffix[0].startswith("@"):
            package_name = suffix[0]
        elif len(suffix) == 2 and suffix[0].startswith("@"):
            package_name = "/".join(suffix)
        else:
            continue
        if package_name in packaged:
            fail(f"VSIX contains duplicate production package: {package_name}")
        packaged[package_name] = json.loads(archive.read(name))
        packaged_paths.add(name)

    if packaged_paths != expected_package_paths:
        fail("VSIX production package installation paths differ from lockfile")

    compatibility = json.loads(
        (Path(__file__).parents[1] / "client-compatibility.json").read_text(encoding="utf-8")
    )
    if json.loads(archive.read("extension/client-compatibility.json")) != compatibility:
        fail("VSIX client adaptation metadata differs from reviewed source")
    for patch in compatibility["patches"]:
        target = f"extension/node_modules/{compatibility['package']}/{patch['path']}"
        if target not in names or hashlib.sha256(archive.read(target)).hexdigest() != patch["adaptedSha256"]:
            fail(f"VSIX contains unprepared or altered client source: {target}")

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
