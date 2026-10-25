#!/usr/bin/env python3
"""Exercise the generic EDF importer on the preserved PhysioNet/OpenNeuro files."""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
import math
import sys
import tempfile
from pathlib import Path

import numpy as np


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_minimal_bdf(path: Path) -> None:
    """Write a tiny standards-shaped 24-bit BDF fixture for MNE's reader."""
    def field(value: object, size: int) -> bytes:
        return str(value).encode("ascii").ljust(size, b" ")[:size]

    labels = ("Fp1", "Fp2")
    header_size = 256 + len(labels) * 256
    fixed = (
        b"\xffBIOSEMI" + field("X X X X", 80) + field("Startdate 01-JAN-2000 BDF fixture", 80)
        + b"01.01.00" + b"00.00.00" + field(header_size, 8) + field("24BIT", 44)
        + field(1, 8) + field(1, 8) + field(len(labels), 4)
    )
    if len(fixed) != 256:
        raise AssertionError(f"BDF fixed header has {len(fixed)} bytes, expected 256")
    signal_fields = b"".join(
        (
            b"".join(field(name, 16) for name in labels),
            b"".join(field("", 80) for _ in labels),
            b"".join(field("uV", 8) for _ in labels),
            b"".join(field(-100, 8) for _ in labels),
            b"".join(field(100, 8) for _ in labels),
            b"".join(field(-8388608, 8) for _ in labels),
            b"".join(field(8388607, 8) for _ in labels),
            b"".join(field("", 80) for _ in labels),
            b"".join(field(10, 8) for _ in labels),
            b"".join(field("", 32) for _ in labels),
        )
    )
    if len(signal_fields) != len(labels) * 256:
        raise AssertionError("BDF signal header has an invalid size")

    def sample24(value: int) -> bytes:
        if value < 0:
            value += 1 << 24
        return bytes((value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF))

    data = b"".join(sample24(round(1000 * math.sin(i / 2))) for _ in labels for i in range(10))
    path.write_bytes(fixed + signal_fields + data)


def load_importer(path: Path):
    spec = importlib.util.spec_from_file_location("prepare_recording_smoke", path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Could not load importer module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    raw_root = root / "neurocontrol" / "datasets" / "raw"
    importer = root / "neurocontrol" / "tools" / "prepare_recording.py"
    cases = [
        ("PhysioNet S001R14", raw_root / "S001R14.edf", root / "neurocontrol" / "models" / "S001_feet_vs_rest.json"),
        ("OpenNeuro walk6min", next((raw_root / "openneuro").rglob("*task-walk6min_eeg.edf")), root / "neurocontrol" / "models" / "S01_walk_vs_stop.json"),
        ("OpenNeuro stop6min", next((raw_root / "openneuro").rglob("*task-stop6min_eeg.edf")), root / "neurocontrol" / "models" / "S01_walk_vs_stop.json"),
    ]
    source_hashes = {name: sha256(source) for name, source, _ in cases}
    importer_module = load_importer(importer)
    with tempfile.TemporaryDirectory(prefix="metasiberia-neuro-import-") as temp:
        output_root = Path(temp)
        for index, (name, source, model_path) in enumerate(cases):
            metadata_path = output_root / f"recording-{index}.json"
            original_argv = sys.argv
            sys.argv = [str(importer), "--recording", str(source), "--metadata", str(metadata_path)]
            try:
                importer_module.main()
            finally:
                sys.argv = original_argv
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            model = json.loads(model_path.read_text(encoding="utf-8"))
            data_path = metadata_path.with_suffix(".f32")
            expected_count = metadata["samples"] * len(metadata["channels"])
            samples = np.fromfile(data_path, dtype="<f4")
            if metadata["format"] != "metasiberia-neurocontrol-f32-v1" or samples.size != expected_count:
                raise SystemExit(f"{name}: prepared stream shape/format is invalid")
            if not np.isfinite(samples).all() or sha256(data_path) != metadata["data_sha256"]:
                raise SystemExit(f"{name}: prepared stream contains invalid values or a bad checksum")
            if sha256(source) != source_hashes[name]:
                raise SystemExit(f"{name}: source recording changed during import")
            compatible = metadata["sample_rate_hz"] == model["sample_rate_hz"] and metadata["channels"] == model["channels"]
            print(
                f"{name}: {metadata['source_format']}, {len(metadata['channels'])} channels @ "
                f"{metadata['sample_rate_hz']} Hz, {metadata['duration_seconds']:.2f}s, "
                f"events={len(metadata['annotations'])}, sidecars={len(metadata['event_sources'])}, "
                f"exact_model_compatibility={compatible}"
            )
            if not compatible:
                raise SystemExit(f"{name}: expected model channel order/sample rate compatibility")

        bdf_source = output_root / "tiny-valid-recording.bdf"
        write_minimal_bdf(bdf_source)
        bdf_metadata_path = output_root / "bdf-recording.json"
        original_argv = sys.argv
        sys.argv = [str(importer), "--recording", str(bdf_source), "--metadata", str(bdf_metadata_path)]
        try:
            importer_module.main()
        finally:
            sys.argv = original_argv
        bdf_metadata = json.loads(bdf_metadata_path.read_text(encoding="utf-8"))
        if bdf_metadata["source_format"] != "BDF" or bdf_metadata["sample_rate_hz"] != 160 or len(bdf_metadata["channels"]) != 2:
            raise SystemExit(f"BDF fixture metadata is invalid: {bdf_metadata}")
        print("BDF 24-bit fixture: parsed, resampled to 160 Hz, channels/provenance recorded")

        # Validate generic BIDS event-sidecar recognition without embedding a dataset name.
        fake_recording = output_root / "sub-demo_ses-a_task-anything_eeg.edf"
        event_sidecar = output_root / "sub-demo_ses-a_task-anything_acq-test_events.tsv"
        with event_sidecar.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=("onset", "duration", "trial_type"), delimiter="\t")
            writer.writeheader()
            writer.writerow({"onset": "1.25", "duration": "2", "trial_type": "WALK"})
        events, sources = importer_module.read_bids_event_sidecars(fake_recording, 160, 1000)
        if len(events) != 1 or events[0]["label"] != "WALK" or events[0]["start_sample"] != 200 or not sources:
            raise SystemExit("Generic BIDS events.tsv sidecar parsing failed")
        print("Generic BIDS events.tsv labels and sample offsets: OK")


if __name__ == "__main__":
    main()
