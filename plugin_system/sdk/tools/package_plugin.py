#!/usr/bin/env python3
"""Build a .mspkg from a plugin project directory using Python's standard library."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import zipfile

from validate_plugin import validate_package


def package_plugin(project: Path, output: Path) -> dict:
    project = project.resolve(strict=True)
    output = output.resolve()
    try:
        if os.path.commonpath([str(project), str(output)]) == str(project):
            raise ValueError("package output must be outside the plugin source directory")
    except ValueError as error:
        if "outside the plugin source directory" in str(error):
            raise
    manifest_path = project / "plugin.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = [manifest_path]
    for candidate in sorted(project.rglob("*")):
        if candidate == manifest_path:
            continue
        if candidate.is_symlink():
            raise ValueError(f"symbolic links are not allowed in plugin sources: {candidate}")
        if candidate.is_dir():
            continue
        if not candidate.is_file():
            raise ValueError(f"unsupported package source: {candidate}")
        if any(part.startswith(".") or part in {"__pycache__", "dist"} for part in candidate.relative_to(project).parts):
            continue
        files.append(candidate)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix="metasiberia-plugin-", suffix=".mspkg", dir=output.parent, delete=False) as temp:
        temporary = Path(temp.name)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for source in files:
                relative = source.relative_to(project).as_posix()
                archive.write(source, relative)
        result = validate_package(str(temporary))
        os.replace(temporary, output)
        result["path"] = str(output.resolve())
        return result
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.suffix.lower() != ".mspkg":
        parser.error("--output must use the .mspkg extension")
    manifest = json.loads((args.project / "plugin.json").read_text(encoding="utf-8"))
    output = args.output or args.project.parent / "dist" / f"{manifest['id']}-{manifest['version']}.mspkg"
    result = package_plugin(args.project, output)
    print("PACKAGED: {path}\nVALID: {id} {version} files={files} sha256={sha256}".format(**result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
