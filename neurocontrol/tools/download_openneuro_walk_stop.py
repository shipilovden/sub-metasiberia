"""Fetch a pinned, one-participant/one-session WALK vs STOP EEG sample."""

from __future__ import annotations

import hashlib
import json
import pathlib
import tempfile
import urllib.request


DATASET = "ds007788"
SNAPSHOT = "1.0.1"
SUBJECT = "01"
SESSION = "02"
ROOT = pathlib.Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "datasets" / "raw" / "openneuro" / DATASET / f"sub-{SUBJECT}" / f"ses-{SESSION}" / "eeg"


def selected_files() -> list[str]:
    prefix = f"sub-{SUBJECT}_ses-{SESSION}"
    files = [f"{prefix}_coordsystem.json", f"{prefix}_electrodes.json", f"{prefix}_electrodes.tsv"]
    for task in ("training", "walk6min", "stop6min"):
        files.append(f"{prefix}_task-{task}_eeg.edf")
        files.append(f"{prefix}_task-{task}_eeg.json")
        acquisitions = ("rexcommand", "rexstate") if task == "training" else (
            "infoclosedloop", "rexcommand", "rexstate"
        )
        for acquisition in acquisitions:
            files.extend((
                f"{prefix}_task-{task}_acq-{acquisition}_events.json",
                f"{prefix}_task-{task}_acq-{acquisition}_events.tsv",
            ))
    return files


def fetch_file(relative_path: str) -> dict:
    destination = DESTINATION / relative_path
    if destination.exists():
        raise FileExistsError(f"Refusing to overwrite existing dataset file: {destination}")
    url = f"https://openneuro.org/crn/datasets/{DATASET}/snapshots/{SNAPSHOT}/files/sub-{SUBJECT}:ses-{SESSION}:eeg:{relative_path}"
    request = urllib.request.Request(url, headers={"User-Agent": "MetaSiberia-NeuroControl-research/1.0"})
    digest = hashlib.sha256()
    md5 = hashlib.md5(usedforsecurity=False)
    byte_count = 0
    destination.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(request, timeout=60) as response:
        if response.status != 200:
            raise OSError(f"OpenNeuro returned HTTP {response.status} for {relative_path}")
        declared_size = response.headers.get("Content-Length")
        etag = response.headers.get("ETag", "").strip('"')
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=f".{destination.name}.", suffix=".part", delete=False) as stream:
            temporary = pathlib.Path(stream.name)
            try:
                while block := response.read(1024 * 1024):
                    stream.write(block)
                    digest.update(block)
                    md5.update(block)
                    byte_count += len(block)
                stream.flush()
            except BaseException:
                temporary.unlink(missing_ok=True)
                raise
    if declared_size is not None and byte_count != int(declared_size):
        temporary.unlink(missing_ok=True)
        raise OSError(f"Incomplete OpenNeuro download for {relative_path}: {byte_count} / {declared_size} bytes")
    if etag and "-" not in etag and md5.hexdigest() != etag.lower():
        temporary.unlink(missing_ok=True)
        raise OSError(f"OpenNeuro S3 ETag (MD5) mismatch for {relative_path}")
    temporary.replace(destination)
    return {"path": relative_path, "bytes": byte_count, "sha256": digest.hexdigest(), "s3_etag": etag or None}


def main() -> None:
    manifest_path = DESTINATION.parent / "download-manifest.json"
    if manifest_path.exists():
        raise FileExistsError(f"Refusing to overwrite existing download manifest: {manifest_path}")
    manifest = {
        "dataset": DATASET,
        "snapshot": SNAPSHOT,
        "dataset_doi": "10.18112/openneuro.ds007788.v1.0.1",
        "subject": f"sub-{SUBJECT}",
        "session": f"ses-{SESSION}",
        "license": "CC0",
        "files": [],
    }
    for relative_path in selected_files():
        item = fetch_file(relative_path)
        manifest["files"].append(item)
        print(f"Downloaded {relative_path}: {item['bytes']} bytes, SHA-256 {item['sha256']}", flush=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Manifest: {manifest_path}")


if __name__ == "__main__":
    main()
