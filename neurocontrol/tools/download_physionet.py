"""Download the selected original EDF+ motor-imagery recordings from PhysioNet.

The files are deliberately ignored by Git. This command never modifies an
existing recording. A local manifest records provenance and SHA-256 hashes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import urllib.request


BASE_URL = "https://physionet.org/files/eegmmidb/1.0.0/S001"
CHECKSUM_URL = "https://physionet.org/files/eegmmidb/1.0.0/SHA256SUMS.txt"
DEFAULT_RUNS = (6, 10, 14)
RAW_DIR = pathlib.Path(__file__).resolve().parents[1] / "datasets" / "raw"


def edf_header_ok(path: pathlib.Path) -> bool:
    with path.open("rb") as handle:
        header = handle.read(256)
    if len(header) != 256 or header[:8] != b"0       ":
        return False
    try:
        header_bytes = int(header[184:192].decode("ascii").strip())
        channels = int(header[252:256].decode("ascii").strip())
    except ValueError:
        return False
    return header_bytes >= 256 and channels >= 64 and path.stat().st_size > header_bytes


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, nargs="+", default=DEFAULT_RUNS)
    args = parser.parse_args()
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    checksum_file = RAW_DIR / "SHA256SUMS.txt"
    if not checksum_file.exists():
        with urllib.request.urlopen(CHECKSUM_URL, timeout=90) as response:
            checksum_file.write_bytes(response.read())
    expected_hashes = dict(
        (name, digest) for digest, name in
        (line.split(maxsplit=1) for line in checksum_file.read_text(encoding="ascii").splitlines())
    )
    entries = []
    for run in args.runs:
        if run not in (4, 6, 8, 10, 12, 14):
            parser.error("Only the documented S001 imagery runs 4, 6, 8, 10, 12, 14 are supported")
        name = f"S001R{run:02d}.edf"
        url = f"{BASE_URL}/{name}"
        dest = RAW_DIR / name
        if not dest.exists():
            temp = dest.with_suffix(".edf.part")
            print(f"Downloading {url}", flush=True)
            try:
                with urllib.request.urlopen(url, timeout=90) as response, temp.open("wb") as out:
                    for chunk in iter(lambda: response.read(1024 * 1024), b""):
                        out.write(chunk)
                if not edf_header_ok(temp):
                    raise ValueError(f"Invalid or incomplete EDF file: {name}")
                temp.replace(dest)
            except BaseException:
                temp.unlink(missing_ok=True)
                raise
        if not edf_header_ok(dest):
            raise ValueError(f"Existing EDF is invalid: {dest}")
        digest = sha256(dest)
        if digest != expected_hashes.get(f"S001/{name}"):
            raise ValueError(f"PhysioNet SHA-256 mismatch for {name}")
        entries.append({"file": name, "source": url, "bytes": dest.stat().st_size, "sha256": digest, "official_sha256_verified": True})
        print(f"{name}: {dest.stat().st_size} bytes, SHA-256 {entries[-1]['sha256']}")
    (RAW_DIR / "provenance.json").write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    if tuple(args.runs) in ((6, 10, 14), (4, 8, 12)):
        from audit_dataset import audit, report_path
        runs = tuple(args.runs)
        report_path(runs).write_text(json.dumps(audit(runs), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("Scientific recordings verified by MNE-Python", flush=True)


if __name__ == "__main__":
    main()
