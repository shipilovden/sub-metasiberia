"""Train and independently evaluate a pilot WALK/STOP motor-imagery model.

The open-loop ``training`` EDF supplies the commanded Stop/Walk calibration
labels (x5/x8).  Entire walk6min and stop6min recordings from the same
participant/session are held out as task-labeled test recordings.  The
infoClosedLoop events are decoder output and are deliberately never labels.
"""

from __future__ import annotations

import csv
import argparse
import hashlib
import json
import pathlib
import sys
from datetime import datetime, timezone

import mne
import numpy as np
import scipy
import sklearn
from mne.decoding import CSP
from scipy.signal import sosfilt
from sklearn.discriminant_analysis import LinearDiscriminantAnalysis
from sklearn.metrics import (accuracy_score, balanced_accuracy_score,
                             confusion_matrix, f1_score, precision_score,
                             recall_score)


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "processing"))
from pipeline import CHANNELS, SAMPLE_RATE, SOS, WINDOW_SAMPLES, csp_log_power  # noqa: E402

DATASET = ROOT / "datasets" / "raw" / "openneuro" / "ds007788"
EEG_DIR = DATASET / "sub-01" / "ses-02" / "eeg"
MODEL_PATH = ROOT / "models" / "S01_walk_vs_stop.json"
REPORT_PATH = ROOT / "datasets" / "processed" / "S01_walk_vs_stop_metrics.json"
WINDOW_SECONDS = WINDOW_SAMPLES / SAMPLE_RATE
EVENT_OFFSET_SECONDS = 0.5
FORWARD_THRESHOLD = 0.75  # Fixed before evaluating the held-out recordings.


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_events(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def read_resampled_eeg(task: str) -> tuple[np.ndarray, pathlib.Path]:
    edf = EEG_DIR / f"sub-01_ses-02_task-{task}_eeg.edf"
    if not edf.is_file():
        raise FileNotFoundError(f"Missing downloaded OpenNeuro EDF: {edf}")
    raw = mne.io.read_raw_edf(edf, preload=True, verbose="ERROR")
    mne.datasets.eegbci.standardize(raw)
    missing = [name for name in CHANNELS if name not in raw.ch_names]
    if missing:
        raise ValueError(f"{task}: missing NeuroControl channels: {missing}")
    if float(raw.info["sfreq"]) != 100.0:
        raise ValueError(f"{task}: expected native 100 Hz EDF, got {raw.info['sfreq']}")
    raw.resample(SAMPLE_RATE, npad="auto", verbose="ERROR")
    samples = raw.get_data(picks=list(CHANNELS)).astype(np.float64, copy=False)
    if samples.shape[0] != len(CHANNELS) or not np.isfinite(samples).all():
        raise ValueError(f"{task}: EEG sample shape or values are invalid")
    return samples, edf


def epochs_in_intervals(filtered: np.ndarray,
                        intervals: list[tuple[float, float, int]]) -> tuple[np.ndarray, np.ndarray, list[dict]]:
    epochs: list[np.ndarray] = []
    labels: list[int] = []
    details: list[dict] = []
    offset = round(EVENT_OFFSET_SECONDS * SAMPLE_RATE)
    for start_s, duration_s, label in intervals:
        start = round(start_s * SAMPLE_RATE) + offset
        end = min(filtered.shape[1], round((start_s + duration_s) * SAMPLE_RATE))
        # NeuroClassifier starts its ring windows at stream sample 0 and advances
        # every step_samples.  Align offline epochs to that same recording origin.
        first_aligned = ((start + WINDOW_SAMPLES - 1) // WINDOW_SAMPLES) * WINDOW_SAMPLES
        for first in range(first_aligned, end - WINDOW_SAMPLES + 1, WINDOW_SAMPLES):
            epochs.append(filtered[:, first:first + WINDOW_SAMPLES])
            labels.append(label)
            details.append({
                "onset_seconds": round(first / SAMPLE_RATE, 3),
                "duration_seconds": WINDOW_SECONDS,
                "actual_label": "WALK" if label else "STOP",
            })
    if not epochs:
        raise ValueError("No complete 2-second EEG epochs were extracted")
    return np.asarray(epochs), np.asarray(labels, dtype=np.int8), details


def task_start_and_duration(task: str) -> tuple[float, float]:
    """Get the instructed block bounds from rexCommand's explicit idle marker.

    Only the x99 idle event is used for its time boundary.  x5/x8 in these
    closed-loop recordings are controller outputs and are not evaluation labels.
    """
    events = read_events(EEG_DIR / f"sub-01_ses-02_task-{task}_acq-rexcommand_events.tsv")
    idle = [row for row in events if row["trial_type"] == "x99"]
    active = [row for row in events if row["trial_type"] in ("x5", "x8")]
    if len(idle) != 1 or len(active) < 1:
        raise ValueError(f"{task}: expected a single idle event followed by the 6-minute task")
    start = float(idle[0]["onset"]) + float(idle[0]["duration"])
    if abs(float(active[0]["onset"]) - start) > 0.02:
        raise ValueError(f"{task}: command events do not agree on the active task start")
    total = sum(float(row["duration"]) for row in active)
    if abs(total - 360.0) > 0.05:
        raise ValueError(f"{task}: expected 360 seconds of instructed imagery, got {total:.3f}")
    return start, total


def metrics_for(actual: np.ndarray, predicted: np.ndarray) -> dict:
    matrix = confusion_matrix(actual, predicted, labels=[0, 1]).tolist()
    return {
        "windows": int(len(actual)),
        "accuracy": float(accuracy_score(actual, predicted)),
        "balanced_accuracy": float(balanced_accuracy_score(actual, predicted)),
        "f1_macro": float(f1_score(actual, predicted, average="macro", zero_division=0)),
        "f1_stop": float(f1_score(actual, predicted, pos_label=0, zero_division=0)),
        "f1_walk": float(f1_score(actual, predicted, pos_label=1, zero_division=0)),
        "precision_walk": float(precision_score(actual, predicted, pos_label=1, zero_division=0)),
        "recall_walk": float(recall_score(actual, predicted, pos_label=1, zero_division=0)),
        "confusion_matrix_rows_actual_stop_walk_columns_predicted_stop_walk": matrix,
    }


def single_block_metrics(actual: np.ndarray, predicted: np.ndarray,
                         confident: np.ndarray, expected: int) -> dict:
    """Summarize a one-label recording without implying two-class balance."""
    matrix = confusion_matrix(actual, predicted, labels=[0, 1]).tolist()
    target_confident = (confident == expected)
    confident_mask = confident >= 0
    return {
        "actual_label": "WALK" if expected else "STOP",
        "windows": int(len(actual)),
        "correct_action_count": int(np.count_nonzero(predicted == expected)),
        "target_recall": float(np.mean(predicted == expected)),
        "predicted_window_counts": {
            "STOP": int(np.count_nonzero(predicted == 0)),
            "WALK": int(np.count_nonzero(predicted == 1)),
        },
        "runtime_confident_window_count": int(np.count_nonzero(confident >= 0)),
        "runtime_confident_coverage": float(np.mean(confident >= 0)),
        "runtime_confident_accuracy": (
            float(np.mean(target_confident[confident_mask])) if np.any(confident_mask) else None
        ),
        "confusion_matrix_rows_actual_stop_walk_columns_predicted_stop_walk": matrix,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overwrite-existing", action="store_true",
                        help="replace only this script's S01_walk_vs_stop outputs")
    args = parser.parse_args()
    for path in (MODEL_PATH, REPORT_PATH):
        if path.exists() and not args.overwrite_existing:
            raise FileExistsError(f"Refusing to overwrite existing research output: {path}")

    training_events_path = EEG_DIR / "sub-01_ses-02_task-training_acq-rexcommand_events.tsv"
    training_sidecar_path = EEG_DIR / "sub-01_ses-02_task-training_acq-rexcommand_events.json"
    levels = json.loads(training_sidecar_path.read_text(encoding="utf-8"))["trial_type"]["Levels"]
    if levels.get("x5") != "5: Stop" or levels.get("x8") != "8: Walk":
        raise ValueError(f"Unexpected authoritative OpenNeuro rexCommand labels: {levels}")
    training_intervals = []
    for event in read_events(training_events_path):
        code = event["trial_type"]
        if code not in ("x5", "x8"):
            continue
        training_intervals.append((float(event["onset"]), float(event["duration"]), int(code == "x8")))
    if not training_intervals or {item[2] for item in training_intervals} != {0, 1}:
        raise ValueError("Open-loop training events must contain both STOP (x5) and WALK (x8)")

    print("Чтение open-loop training EDF…", flush=True)
    training_samples, training_edf = read_resampled_eeg("training")
    training_filtered = sosfilt(SOS, training_samples, axis=1)
    train_x, train_y, _ = epochs_in_intervals(training_filtered, training_intervals)

    test_sets: dict[str, tuple[np.ndarray, np.ndarray, list[dict], pathlib.Path]] = {}
    for task, target_label in (("stop6min", 0), ("walk6min", 1)):
        print(f"Чтение независимой от обучения записи {task}…", flush=True)
        samples, edf = read_resampled_eeg(task)
        filtered = sosfilt(SOS, samples, axis=1)
        onset, duration = task_start_and_duration(task)
        intervals = [(onset, duration, target_label)]
        x, y, details = epochs_in_intervals(filtered, intervals)
        test_sets[task] = (x, y, details, edf)

    print(f"Обучение CSP+LDA: train={len(train_x)} окон; held-out="
          f"{sum(len(v[1]) for v in test_sets.values())} окон…", flush=True)
    y_unique = np.unique(train_y)
    if y_unique.tolist() != [0, 1]:
        raise ValueError("Both training classes must be present")
    csp = CSP(n_components=min(4, len(CHANNELS)), reg="ledoit_wolf", log=True, norm_trace=False)
    csp.fit(train_x, train_y)
    filters = np.asarray(csp.filters_[:min(4, len(CHANNELS))], dtype=np.float64)
    train_features = csp_log_power(filters, train_x)
    lda = LinearDiscriminantAnalysis()
    lda.fit(train_features, train_y)
    if lda.classes_.tolist() != [0, 1]:
        raise ValueError(f"Unexpected LDA class order: {lda.classes_}")

    all_actual: list[int] = []
    all_predicted: list[int] = []
    all_confident: list[int] = []
    predictions: dict[str, list[dict]] = {}
    per_task: dict[str, dict] = {}
    for task, (x, actual, details, _) in test_sets.items():
        features = csp_log_power(filters, x)
        probability = lda.predict_proba(features)[:, 1]
        # Match the current runtime: motion requires >= 0.75; everything else
        # maps conservatively to STOP, including the no-command confidence band.
        predicted = (probability >= FORWARD_THRESHOLD).astype(np.int8)
        confident = np.where(probability >= FORWARD_THRESHOLD, 1,
                             np.where(probability <= 1.0 - FORWARD_THRESHOLD, 0, -1)).astype(np.int8)
        all_actual.extend(actual.tolist())
        all_predicted.extend(predicted.tolist())
        all_confident.extend(confident.tolist())
        per_task[task] = single_block_metrics(actual, predicted, confident, int(actual[0]))
        predictions[task] = [
            {**row,
             "predicted_label": "WALK" if int(pred) else "STOP",
             "forward_probability": float(prob),
             "confident_runtime_class": ("WALK" if conf == 1 else
                                         "STOP" if conf == 0 else None)}
            for row, pred, prob, conf in zip(details, predicted, probability, confident)
        ]

    actual_array = np.asarray(all_actual, dtype=np.int8)
    predicted_array = np.asarray(all_predicted, dtype=np.int8)
    confident_array = np.asarray(all_confident, dtype=np.int8)
    overall = metrics_for(actual_array, predicted_array)
    confident_mask = confident_array >= 0
    overall["runtime_confident_window_count"] = int(np.count_nonzero(confident_mask))
    overall["runtime_confident_coverage"] = float(np.mean(confident_mask))
    overall["runtime_confident_accuracy"] = (
        float(np.mean(confident_array[confident_mask] == actual_array[confident_mask]))
        if np.any(confident_mask) else None
    )
    overall["runtime_confident_confusion_matrix_rows_actual_stop_walk_columns_predicted_stop_walk"] = (
        confusion_matrix(actual_array[confident_mask], confident_array[confident_mask], labels=[0, 1]).tolist()
        if np.any(confident_mask) else [[0, 0], [0, 0]]
    )
    trained_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
    model = {
        "format": "metasiberia-neurocontrol-csp-lda-v1",
        "trained_at_utc": trained_at,
        "sample_rate_hz": SAMPLE_RATE,
        "channels": list(CHANNELS),
        "window_samples": WINDOW_SAMPLES,
        "step_samples": WINDOW_SAMPLES,
        "sos_filter": SOS.tolist(),
        "sos_bandpass_8_30_hz": SOS.tolist(),
        "filter_band_hz": [8.0, 30.0],
        "notch_50_hz": False,
        "normalization": "CSP spatial projection + log-power; no additional per-recording normalization",
        "preprocessing": {
            "channels_in_order": list(CHANNELS),
            "sample_rate_hz": SAMPLE_RATE,
            "resampled_from_hz": 100.0,
            "filter_band_hz": [8.0, 30.0],
            "notch_50_hz": False,
            "window_samples": WINDOW_SAMPLES,
            "step_samples": WINDOW_SAMPLES,
            "event_offset_seconds": EVENT_OFFSET_SECONDS,
            "window_grid_origin": "recording sample 0; windows rounded up to next 2-second runtime boundary after event offset",
            "normalization": "CSP spatial projection + log-power; no additional per-recording normalization",
        },
        "csp_filters": filters.tolist(),
        "lda_coefficients": lda.coef_[0].tolist(),
        "lda_intercept": float(lda.intercept_[0]),
        "forward_threshold": FORWARD_THRESHOLD,
        "training_task": "training",
        "training_recording": training_edf.name,
        "training_recording_sha256": sha256(training_edf),
        "held_out_recordings": [path.name for _, _, _, path in test_sets.values()],
        "negative_class": "imagined standing still (STOP)",
        "positive_class": "imagined walking (WALK)",
        "negative_class_ru": "Воображение неподвижного стояния (STOP)",
        "positive_class_ru": "Воображаемая ходьба (WALK)",
        "negative_command": "STOP",
        "positive_command": "FORWARD",
        "command_mapping_is_experimental": False,
        "python_versions": {
            "mne": mne.__version__, "numpy": np.__version__,
            "scipy": scipy.__version__, "scikit_learn": sklearn.__version__,
        },
        "meaning": "Walking motor imagery; distinct from both-feet movement imagery.",
    }
    report = {
        "trained_at_utc": trained_at,
        "dataset": "OpenNeuro ds007788 snapshot 1.0.1 (CC0)",
        "subject": "sub-01",
        "session": "ses-02",
        "training_task": "training (open-loop Stop/Walk cycles)",
        "training_recording": training_edf.name,
        "training_recording_sha256": sha256(training_edf),
        "training_event_source": training_events_path.name,
        "training_label_mapping": {"x5": "STOP", "x8": "WALK"},
        "training_windows": int(len(train_y)),
        "training_windows_by_class": {
            "STOP": int(np.count_nonzero(train_y == 0)),
            "WALK": int(np.count_nonzero(train_y == 1)),
        },
        "test_tasks": ["stop6min", "walk6min"],
        "test_recordings": {
            task: {"filename": data[3].name, "sha256": sha256(data[3]),
                  "windows": len(data[1]),
                  "actual_label": "WALK" if task == "walk6min" else "STOP"}
            for task, data in test_sets.items()
        },
        "test_label_basis": "Task instruction/task name for each full 6-minute imagery block; acq-infoclosedloop predictions and closed-loop rexcommand outputs are not used as ground truth.",
        "native_sample_rate_hz": 100,
        "model_sample_rate_hz": SAMPLE_RATE,
        "channels_in_runtime_order": list(CHANNELS),
        "window_seconds": WINDOW_SECONDS,
        "step_seconds": WINDOW_SECONDS,
        "event_offset_seconds": EVENT_OFFSET_SECONDS,
        "window_grid_origin": "recording sample 0; windows rounded up to next 2-second runtime boundary after event offset",
        "forward_threshold_fixed_before_test": FORWARD_THRESHOLD,
        "runtime_mapping": "WALK if probability >= 0.75; otherwise STOP action; runtime-confidence band is also reported per window.",
        "metrics": overall,
        "per_task_metrics": per_task,
        "predictions": predictions,
        "limitations": [
            "Single participant and one session; this is a pilot, not evidence of generalization to other people or sessions.",
            "Held-out blocks were collected in closed-loop. They are independent files from training, but the participant had visual/physical exoskeleton feedback.",
            "No artifact-removal step was added; the dataset is raw. Movement and feedback artifacts may affect the scores.",
            "These offline scores do not authorize avatar control; runtime safety and additional subject/session validation are still required.",
        ],
    }
    MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    MODEL_PATH.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    report["model_file"] = MODEL_PATH.name
    report["model_sha256"] = sha256(MODEL_PATH)
    REPORT_PATH.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"model": str(MODEL_PATH), "report": str(REPORT_PATH),
                      "training_windows": len(train_y), "metrics": overall,
                      "per_task_metrics": per_task}, indent=2))


if __name__ == "__main__":
    main()
