"""Download the public upstream snowboard assets into an explicit staging directory.

Network + local-output operation. Does not deploy, edit a database, or restart services.
The server needs these files in its dist_resources directory before startup.
"""

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import struct
import urllib.error
import urllib.request


MODEL = "_UB_2026_Black_Red_Snowboard_glb_13786249306117967103"
TEXTURES = (
    "GLB_image_16953310986919210825_jpg_16953310986919210825",
    "GLB_image_9161215156477569574_jpg_9161215156477569574",
    "GLB_image_3368751828563657912_jpg_3368751828563657912",
)
SOURCE = "https://substrata.info/resource/"
MAX_BYTES = 64 * 1024 * 1024


def asset_names():
    required = {MODEL + ".bmesh", *(name + ".jpg" for name in TEXTURES)}
    names = [MODEL + lod + opt + ".bmesh"
             for lod in ("", "_lod1", "_lod2") for opt in ("", "_opt3")]
    names += [name + lod + ext for name in TEXTURES
              for lod in ("", "_lod1", "_lod2") for ext in (".jpg", ".basis")]
    return names, required


def fetch(name, required, destination):
    url = SOURCE + name
    try:
        with urllib.request.urlopen(url, timeout=60) as response:
            if response.status != 200:
                raise RuntimeError(f"Unexpected HTTP status for {name}: {response.status}")
            data = response.read(MAX_BYTES + 1)
    except urllib.error.HTTPError as error:
        if error.code == 404 and name not in required:
            return {"name": name, "available": False}
        raise
    if not data or len(data) > MAX_BYTES or data.lstrip().lower().startswith((b"<!doctype", b"<html")):
        raise RuntimeError(f"Invalid asset response: {name}")
    if name.endswith(".jpg") and not data.startswith(b"\xff\xd8"):
        raise RuntimeError(f"Invalid JPEG: {name}")
    if name.endswith(".bmesh"):
        if len(data) < 8:
            raise RuntimeError(f"Truncated mesh: {name}")
        magic, version = struct.unpack_from("<II", data)
        if magic != 12456751 or not 1 <= version <= 3:
            raise RuntimeError(f"Unsupported mesh header: {name}, version {version}")
    sha256 = hashlib.sha256(data).hexdigest()
    target = destination / name
    if target.exists():
        if hashlib.sha256(target.read_bytes()).hexdigest() != sha256:
            raise RuntimeError(f"Refusing to replace differing file: {target}")
    else:
        # Exclusive creation avoids replacing pre-existing content.
        with target.open("xb") as output:
            output.write(data)
    return {"name": name, "available": True, "source": url,
            "bytes": len(data), "sha256": sha256}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=Path, help="Explicit staging directory")
    parser.add_argument("--dry-run", action="store_true", help="List URLs without downloading or writing")
    args = parser.parse_args()
    names, required = asset_names()
    if args.dry_run:
        for name in names:
            print(SOURCE + name)
        return
    args.destination.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        records = list(pool.map(lambda name: fetch(name, required, args.destination), names))
    manifest = args.destination / "manifest.json"
    manifest.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    for record in records:
        print(record["name"], record.get("bytes", "unavailable optional derivative"))
    print("Manifest:", manifest)


if __name__ == "__main__":
    main()
