#!/usr/bin/env python3
"""Validate the deliberately small, portable Metasiberia .mspkg ZIP profile."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import sys
import os
import unicodedata
import zipfile

SUPPORTED_API_MAJOR = 1
SUPPORTED_API_MINOR = 2
SUPPORTED_CAPABILITIES = {
    "ui.command", "ui.notification", "ui.panel", "scene.read.summary",
}
from pathlib import PurePosixPath

MAX_ARCHIVE = 64 * 1024 * 1024
MAX_EXPANDED = 64 * 1024 * 1024
MAX_FILE = 16 * 1024 * 1024
MAX_FILES = 512
MAX_RATIO = 200


def _safe_package_path(name: str, mode: int) -> bool:
    if (not name or name.endswith("/") or "\\" in name or "\x00" in name or
            unicodedata.normalize("NFC", name) != name):
        return False
    pure = PurePosixPath(name)
    parts = name.split("/")
    if (pure.is_absolute() or any(part in ("", ".", "..") for part in parts) or
            re.match(r"^[A-Za-z]:", name) or stat.S_ISLNK(mode)):
        return False
    for part in parts:
        if part.endswith((" ", ".")):
            return False
        stem = part.split(".", 1)[0].upper()
        if stem in {"CON", "PRN", "AUX", "NUL"} or re.fullmatch(r"(?:COM|LPT)[1-9]", stem):
            return False
    return True


def validate_package(path: str) -> dict:
    if os.path.getsize(path) > MAX_ARCHIVE:
        raise ValueError("compressed package exceeds size limit")
    with open(path, "rb") as source:
        package_hash = hashlib.sha256(source.read()).hexdigest()
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > MAX_FILES:
            raise ValueError("package has too many entries")
        if sum(entry.file_size for entry in entries) > MAX_EXPANDED:
            raise ValueError("expanded package exceeds size limit")
        names: set[str] = set()
        exact_names: set[str] = set()
        for entry in entries:
            name = entry.filename
            mode = entry.external_attr >> 16
            if not _safe_package_path(name, mode):
                raise ValueError(f"unsafe package path: {name!r}")
            if entry.file_size > MAX_FILE:
                raise ValueError(f"package file exceeds size limit: {name}")
            if entry.flag_bits & 0x1:
                raise ValueError("encrypted package entries are not supported")
            if entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise ValueError(f"unsupported compression method: {name}")
            if entry.file_size and (entry.compress_size == 0 or entry.file_size > entry.compress_size * MAX_RATIO):
                raise ValueError(f"package entry exceeds the {MAX_RATIO}:1 compression ratio limit: {name}")
            folded_name = unicodedata.normalize("NFC", name).casefold()
            if folded_name in names:
                raise ValueError(f"duplicate package path: {name}")
            names.add(folded_name)
            exact_names.add(name)
            # Reading every entry verifies ZIP CRCs before the SDK declares a package valid.
            if len(archive.read(entry)) != entry.file_size:
                raise ValueError(f"truncated package entry: {name}")
        if "plugin.json" not in exact_names:
            raise ValueError("plugin.json is missing")
        manifest = json.loads(archive.read("plugin.json"))
        if not isinstance(manifest, dict):
            raise ValueError("plugin.json must contain an object")
        required = ("schemaVersion", "id", "name", "version", "runtime", "entry", "api", "permissions")
        if any(key not in manifest for key in required):
            raise ValueError("manifest is missing a required field")
        api = manifest["api"]
        if (type(manifest["schemaVersion"]) is not int or manifest["schemaVersion"] != 1 or not isinstance(api, dict) or
                type(api.get("major")) is not int or type(api.get("minor", 0)) is not int or
                api.get("major") != SUPPORTED_API_MAJOR or not 0 <= api.get("minor", 0) <= SUPPORTED_API_MINOR):
            raise ValueError("unsupported manifest or Plugin API version")
        if not isinstance(manifest["id"], str) or not re.fullmatch(
                r"[a-z0-9]+(?:[.-][a-z0-9]+)*(?:\.[a-z0-9]+(?:-[a-z0-9]+)*)+", manifest["id"]):
            raise ValueError("invalid reverse-domain plugin id")
        if not isinstance(manifest["version"], str) or not re.fullmatch(
                r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", manifest["version"]):
            raise ValueError("invalid semantic version")
        if not isinstance(manifest["name"], str) or not manifest["name"].strip():
            raise ValueError("plugin name must be a non-empty string")
        if not isinstance(manifest["runtime"], str) or not manifest["runtime"].strip():
            raise ValueError("plugin runtime must be a non-empty string")
        if (not isinstance(manifest["permissions"], list) or
                any(not isinstance(item, str) or not item for item in manifest["permissions"]) or
                len(set(manifest["permissions"])) != len(manifest["permissions"])):
            raise ValueError("permissions must be a list of unique non-empty strings")
        unsupported = sorted(set(manifest["permissions"]) - SUPPORTED_CAPABILITIES)
        if unsupported:
            raise ValueError("unsupported Plugin API capability: " + ", ".join(unsupported))
        if not isinstance(manifest["entry"], str) or unicodedata.normalize("NFC", manifest["entry"]) != manifest["entry"]:
            raise ValueError("entrypoint path is invalid")
        if unicodedata.normalize("NFC", manifest["entry"]).casefold() not in names:
            raise ValueError("declared entrypoint is missing")
        icon = manifest.get("icon")
        if icon and (not isinstance(icon, str) or unicodedata.normalize("NFC", icon).casefold() not in names):
            raise ValueError("declared icon is missing")
        return {"id": manifest["id"], "version": manifest["version"],
                "runtime": manifest["runtime"], "files": len(names),
                "sha256": package_hash}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", help="path to a .mspkg ZIP archive")
    args = parser.parse_args()
    try:
        result = validate_package(args.package)
    except (OSError, zipfile.BadZipFile, json.JSONDecodeError, UnicodeDecodeError, KeyError, TypeError, ValueError) as error:
        print(f"INVALID: {error}", file=sys.stderr)
        return 1
    print("VALID: {id} {version} runtime={runtime} files={files} sha256={sha256}".format(**result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
