"""Convert EDF/EDF+ or BDF into NeuroControl's verified sample-major F32 format."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
import re

import mne
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREFERRED_CHANNELS = ("FC3", "FC1", "FCz", "FC2", "FC4", "C5", "C3", "C1", "Cz", "C2", "C4", "C6", "CP3", "CP1", "CPz", "CP2", "CP4")
SAMPLE_RATE = 160
MAX_SAMPLES = SAMPLE_RATE * 60 * 60
MAX_CHANNELS = 256


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_bids_event_sidecars(recording: pathlib.Path, sample_rate: int, samples: int) -> tuple[list[dict], list[dict]]:
    """Read matching BIDS events.tsv sidecars without assuming a dataset/task name."""
    stem = recording.stem
    prefix = re.sub(r"_eeg$", "", stem, flags=re.IGNORECASE)
    sidecars = sorted(p for p in recording.parent.glob("*_events.tsv") if p.name.startswith(prefix))
    events: list[dict] = []
    sources: list[dict] = []
    for sidecar in sidecars:
        source_events = 0
        with sidecar.open("r", encoding="utf-8-sig", newline="") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                try:
                    onset = float(row.get("onset", ""))
                except (TypeError, ValueError):
                    continue
                try:
                    duration = max(0.0, float(row.get("duration", "0") or 0))
                except (TypeError, ValueError):
                    duration = 0.0
                label = next((str(row.get(key, "")).strip() for key in ("trial_type", "value", "event", "label", "stim_type") if str(row.get(key, "")).strip()), "event")
                start_sample = round(onset * sample_rate)
                end_sample = round((onset + duration) * sample_rate)
                events.append({
                    "label": label,
                    "start_sample": max(0, min(samples, start_sample)),
                    "end_sample": max(0, min(samples, end_sample)),
                    "onset_seconds": onset,
                    "duration_seconds": duration,
                    "source_file": sidecar.name,
                    "source_type": "bids_events_tsv",
                    "metadata": row,
                })
                source_events += 1
        sources.append({"file": sidecar.name, "sha256": sha256(sidecar), "events_read": source_events})
    return events, sources


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recording", "--edf", dest="recording", required=True, type=pathlib.Path,
                        help="input EDF/EDF+ or BDF file; --edf remains as a compatibility alias")
    parser.add_argument("--metadata", required=True, type=pathlib.Path)
    args = parser.parse_args()
    recording = args.recording.resolve()
    metadata_path = args.metadata.resolve()
    suffix = recording.suffix.lower()
    if not recording.is_file() or suffix not in (".edf", ".bdf"):
        parser.error("--recording must point to an existing EDF/EDF+ or BDF file")

    print(f"Чтение {suffix[1:].upper()} и метаданных каналов…", flush=True)
    if suffix == ".bdf":
        raw = mne.io.read_raw_bdf(recording, preload=False, verbose="ERROR")
    else:
        raw = mne.io.read_raw_edf(recording, preload=False, verbose="ERROR")

    original_channel_names = list(raw.ch_names)
    original_channel_types = list(raw.get_channel_types())
    original_sample_rate = float(raw.info["sfreq"])
    original_samples = int(raw.n_times)
    original_duration = original_samples / original_sample_rate
    transformations: list[dict] = []

    # PhysioNet names commonly contain the verified EEGBCI reference suffix.
    # Normalize only that known convention; otherwise keep input labels intact.
    if any(name.upper().endswith(("-REF", "-LE", ".")) for name in raw.ch_names):
        before = list(raw.ch_names)
        mne.datasets.eegbci.standardize(raw)
        transformations.append({"operation": "mne_eegbci_standardize", "reason": "recognized EEGBCI reference suffix", "channel_names_before": before, "channel_names_after": list(raw.ch_names)})

    eeg_indices = mne.pick_types(raw.info, eeg=True, exclude=[])
    eeg_names = [raw.ch_names[index] for index in eeg_indices]
    if len(eeg_names) < 2:
        raise ValueError("В записи найдено меньше двух каналов, распознанных MNE как EEG")
    # Keep the established NeuroControl model montage when it exists in full.
    # For other montages, preserve every EEG channel in source order and expose
    # the exact resulting order for strict model compatibility checks.
    if all(name in eeg_names for name in PREFERRED_CHANNELS):
        selected = list(PREFERRED_CHANNELS)
        selection_reason = "existing NeuroControl 10-10 sensorimotor channel order"
    else:
        selected = eeg_names
        selection_reason = "all MNE EEG channels in source order"
    if len(selected) > MAX_CHANNELS:
        raise ValueError(f"Количество EEG-каналов превышает допустимый предел {MAX_CHANNELS}")

    if not np.isfinite(original_sample_rate) or original_sample_rate <= 0 or original_sample_rate > 4096:
        raise ValueError("Частота дискретизации вне поддерживаемого диапазона 1–4096 Гц")
    if original_duration <= 0 or original_duration > 3600:
        raise ValueError("Поддерживаются записи длительностью не более одного часа")
    selected_source_names = list(raw.ch_names)
    omitted_source_names = [name for name in selected_source_names if name not in selected]
    if raw.n_times * len(selected) * 8 > 1024**3:
        raise ValueError("Объём выбранных EEG-каналов превышает безопасный предел обработки 1 ГиБ")
    raw.pick(selected)
    raw.load_data()

    target_rate = SAMPLE_RATE
    if original_sample_rate != target_rate:
        # MNE's polyphase resampler is the existing verified import path.
        raw.resample(target_rate, npad="auto", verbose="ERROR")
        transformations.append({"operation": "mne_raw_resample", "from_hz": original_sample_rate, "to_hz": target_rate, "method": "MNE resample with automatic padding"})
    sample_rate = int(round(raw.info["sfreq"]))
    data = raw.get_data()
    samples = data.astype("<f4", copy=False)
    if samples.shape[1] <= 0 or samples.shape[1] > MAX_SAMPLES:
        raise ValueError("Запись должна содержать от одного отсчёта до одного часа данных")
    if not np.isfinite(samples).all():
        raise ValueError("Запись содержит NaN или бесконечные отсчёты; поток не будет импортирован")
    if samples.shape[0] != len(selected):
        raise ValueError("Число выбранных каналов не совпадает с размером массива данных")

    annotations = []
    for annotation in raw.annotations:
        label = str(annotation["description"]).strip()
        if not label:
            continue
        onset = float(annotation["onset"])
        duration = max(0.0, float(annotation["duration"]))
        first = round(onset * sample_rate)
        end = round((onset + duration) * sample_rate)
        annotations.append({
            "label": label,
            "start_sample": max(0, min(samples.shape[1], first)),
            "end_sample": max(0, min(samples.shape[1], end)),
            "onset_seconds": onset,
            "duration_seconds": duration,
            "source_type": "recording_annotation",
        })
    sidecar_events, sidecar_sources = read_bids_event_sidecars(recording, sample_rate, int(samples.shape[1]))
    annotations.extend(sidecar_events)
    transformations.append({"operation": "select_channels", "reason": selection_reason, "channels": selected,
                            "omitted_channel_names": omitted_source_names})
    transformations.append({"operation": "serialize_sample_major_f32", "byte_order": "little-endian", "units": "volts"})

    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    binary_path = metadata_path.with_suffix(".f32")
    print("Сохранение многоканального потока, происхождения и событий…", flush=True)
    binary_path.write_bytes(samples.T.tobytes(order="C"))
    metadata = {
        "format": "metasiberia-neurocontrol-f32-v1",
        "source_file": recording.name,
        "source_path": str(recording),
        "source_format": "BDF" if suffix == ".bdf" else "EDF/EDF+",
        "source_sha256": sha256(recording),
        "sample_rate_hz": sample_rate,
        "original_sample_rate_hz": original_sample_rate,
        "duration_seconds": float(samples.shape[1]) / sample_rate,
        "original_duration_seconds": original_duration,
        "units": "volts",
        "channels": selected,
        "source_channels": original_channel_names,
        "source_channel_types": original_channel_types,
        "samples": int(samples.shape[1]),
        "annotations": annotations,
        "event_sources": sidecar_sources,
        "data_file": binary_path.name,
        "data_sha256": sha256(binary_path),
        "preprocessing": {
            "transformations": transformations,
            "filtering": "none; causal model filter is applied during playback",
            "resampled_to_hz": sample_rate,
        },
        "provenance": {"source_file": recording.name, "source_path": str(recording), "source_sha256": sha256(recording),
                       "event_sources": sidecar_sources},
    }
    metadata_path.write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Подготовлено: {metadata_path}", flush=True)


if __name__ == "__main__":
    main()
