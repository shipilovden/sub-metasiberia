"""Reproducible S001 imagery preprocessing and model export.

Only T0 (rest) and T2 (both feet imagery) enter the binary model. T1 (both
hands imagery) is retained as an untrained negative challenge set.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct

import mne
import numpy as np
from scipy.signal import butter, sosfilt


ROOT = pathlib.Path(__file__).resolve().parents[1]
RAW_DIR = ROOT / "datasets" / "raw"
PROCESSED_DIR = ROOT / "datasets" / "processed"
CHANNELS = ("FC3", "FC1", "FCz", "FC2", "FC4", "C5", "C3", "C1", "Cz", "C2", "C4", "C6", "CP3", "CP1", "CPz", "CP2", "CP4")
SAMPLE_RATE = 160
WINDOW_SECONDS = 2.0
WINDOW_SAMPLES = int(SAMPLE_RATE * WINDOW_SECONDS)
START_OFFSET_SECONDS = 0.5
SOS = butter(4, (8.0, 30.0), btype="bandpass", fs=SAMPLE_RATE, output="sos")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_run(run: int, channels: tuple[str, ...] = CHANNELS,
             sos: np.ndarray = SOS) -> tuple[np.ndarray, np.ndarray, dict]:
    """Read EDF+, export unchanged EEG samples for causal C++ playback."""
    if run not in (4, 6, 8, 10, 12, 14):
        raise ValueError("This pipeline supports the PhysioNet imagery runs R04/R06/R08/R10/R12/R14")
    path = RAW_DIR / f"S001R{run:02d}.edf"
    if not path.exists():
        raise FileNotFoundError(f"Download {path.name} with tools/download_physionet.py first")
    raw = mne.io.read_raw_edf(path, preload=True, verbose="ERROR")
    mne.datasets.eegbci.standardize(raw)
    if raw.info["sfreq"] != SAMPLE_RATE:
        raise ValueError(f"Unexpected sample rate in {path.name}: {raw.info['sfreq']}")
    if not channels or any(name not in raw.ch_names for name in channels):
        raise ValueError(f"Missing selected sensorimotor electrodes in {path.name}: {set(channels) - set(raw.ch_names)}")
    original_eeg_count = len(mne.pick_types(raw.info, eeg=True, exclude=[]))
    if original_eeg_count != 64:
        raise ValueError(f"Expected 64 EEG electrodes, found {original_eeg_count} in {path.name}")
    montage = mne.channels.make_standard_montage("colin27_1005")
    electrode_positions = montage.get_positions()["ch_pos"]
    if any(name not in electrode_positions for name in channels):
        raise ValueError("Standard montage is missing a selected electrode position")
    samples = raw.get_data(picks=list(channels)).astype(np.float64, copy=False)
    filtered = sosfilt(sos, samples, axis=1)
    annotations = []
    for annotation in raw.annotations:
        if annotation["description"] not in ("T0", "T1", "T2"):
            continue
        start = round(float(annotation["onset"]) * SAMPLE_RATE)
        end = round((float(annotation["onset"]) + float(annotation["duration"])) * SAMPLE_RATE)
        annotations.append({"label": annotation["description"], "start_sample": max(0, start), "end_sample": min(samples.shape[1], end)})
    if not {"T0", "T1", "T2"}.issubset({item["label"] for item in annotations}):
        raise ValueError(f"Missing T0/T1/T2 annotations in {path.name}")
    PROCESSED_DIR.mkdir(parents=True, exist_ok=True)
    # Sample-major little-endian float32; filtering is applied during playback.
    binary_path = PROCESSED_DIR / f"S001R{run:02d}.f32"
    binary_path.write_bytes(samples.T.astype("<f4", copy=False).tobytes(order="C"))
    metadata = {
        "format": "metasiberia-neurocontrol-f32-v1",
        "source_file": path.name,
        "source_sha256": sha256(path),
        "run": run,
        "sample_rate_hz": SAMPLE_RATE,
        "units": "volts",
        "channels": list(channels),
        "eeg_channel_count_in_edf": original_eeg_count,
        "samples": int(samples.shape[1]),
        "annotations": annotations,
        "data_file": binary_path.name,
        "data_sha256": sha256(binary_path),
        "electrode_montage": "international 10-10; sensorimotor subset",
        "electrode_positions_meters_colin27_1005": {
            name: [float(value) for value in electrode_positions[name]] for name in channels
        },
    }
    (PROCESSED_DIR / f"S001R{run:02d}.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return samples, filtered, metadata


def windows(filtered: np.ndarray, metadata: dict, labels: tuple[str, ...],
            window_samples: int = WINDOW_SAMPLES,
            start_offset_seconds: float = START_OFFSET_SECONDS,
            step_samples: int | None = None) -> tuple[np.ndarray, np.ndarray]:
    """Non-overlapping, onset-offset windows completely inside each event."""
    x, y = [], []
    offset = round(start_offset_seconds * SAMPLE_RATE)
    stride = window_samples if step_samples is None else step_samples
    if window_samples <= 0 or stride <= 0:
        raise ValueError("Window length and step must be positive")
    for item in metadata["annotations"]:
        if item["label"] not in labels:
            continue
        first = item["start_sample"] + offset
        end = item["end_sample"]
        for sample in range(first, end - window_samples + 1, stride):
            x.append(filtered[:, sample:sample + window_samples])
            y.append(item["label"])
    if not x:
        return np.empty((0, len(metadata["channels"]), window_samples)), np.empty((0,), dtype="<U2")
    return np.asarray(x), np.asarray(y)


def csp_log_power(filters: np.ndarray, epochs: np.ndarray) -> np.ndarray:
    projected = np.einsum("kc,nct->nkt", filters, epochs)
    return np.log(np.maximum(np.mean(np.square(projected), axis=2), 1e-30))
