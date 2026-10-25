"""Validate the three S001 imagery EDF+ runs and write a reproducible report."""

from __future__ import annotations

import collections
import argparse
import hashlib
import json
import pathlib

import mne
import numpy as np


ROOT = pathlib.Path(__file__).resolve().parents[1]
RAW = ROOT / "datasets" / "raw"
RUNS = (6, 10, 14)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def expected_hashes() -> dict[str, str]:
    checksum_path = RAW / "SHA256SUMS.txt"
    if not checksum_path.is_file():
        raise FileNotFoundError(f"Official PhysioNet checksum list is missing: {checksum_path}")
    checksums = {}
    for line in checksum_path.read_text(encoding="ascii").splitlines():
        digest, relative_path = line.split(maxsplit=1)
        checksums[relative_path.lstrip("* ")] = digest.lower()
    return checksums


def audit(runs: tuple[int, ...] = RUNS) -> dict:
    hashes = expected_hashes()
    recordings = []
    for run in runs:
        path = (RAW / f"S001R{run:02d}.edf").resolve()
        if not path.is_file():
            raise FileNotFoundError(f"Missing scientific recording: {path}")
        digest = sha256(path)
        expected = hashes.get(f"S001/{path.name}")
        if expected is None or digest != expected:
            raise ValueError(f"Official PhysioNet SHA-256 does not match: {path}")
        raw = mne.io.read_raw_edf(path, preload=True, verbose="ERROR")
        data = raw.get_data(picks="eeg")
        if data.shape[0] != 64 or not np.isfinite(data).all():
            raise ValueError(f"EDF signal channels are incomplete or non-finite: {path}")
        events = collections.Counter(str(item["description"]) for item in raw.annotations)
        if any(events[label] == 0 for label in ("T0", "T1", "T2")):
            raise ValueError(f"EDF experimental annotations are incomplete: {path}")
        recordings.append({
            "path": str(path),
            "bytes": path.stat().st_size,
            "sha256": digest,
            "official_sha256_verified": True,
            "duration_seconds": raw.n_times / float(raw.info["sfreq"]),
            "sample_rate_hz": float(raw.info["sfreq"]),
            "eeg_channels": data.shape[0],
            "channel_names": raw.ch_names,
            "annotations": dict(events),
            "finite_signal_values_verified": True,
        })
    return {"dataset": "PhysioNet EEG Motor Movement/Imagery v1.0.0", "subject": "S001", "recordings": recordings}


def report_path(runs: tuple[int, ...]) -> pathlib.Path:
    return RAW / ("audit.json" if runs == RUNS else "audit_hands.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, nargs=3, default=list(RUNS))
    arguments = parser.parse_args()
    runs = tuple(arguments.runs)
    if runs not in (RUNS, (4, 8, 12)):
        parser.error("Only S001 imagery runs 6/10/14 or 4/8/12 are supported")
    report = audit(runs)
    output = report_path(runs)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for item in report["recordings"]:
        print(f"{pathlib.Path(item['path']).name}: {item['bytes']} bytes, SHA-256 {item['sha256']}, "
              f"{item['duration_seconds']:.2f} s, {item['sample_rate_hz']:.0f} Hz, "
              f"{item['eeg_channels']} EEG channels, events {item['annotations']}")
    print(f"Report: {output}")
