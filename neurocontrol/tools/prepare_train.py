"""Prepare EDF recordings, train CSP + LDA and evaluate on held-out R14."""

from __future__ import annotations

import json
import pathlib
import sys
import argparse
import time
from datetime import datetime, timezone

import mne
import numpy as np
import scipy
import sklearn
from mne.decoding import CSP
from sklearn.discriminant_analysis import LinearDiscriminantAnalysis
from sklearn.metrics import (accuracy_score, balanced_accuracy_score,
                            confusion_matrix, f1_score, precision_score,
                            recall_score)


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "processing"))
from pipeline import (  # noqa: E402
    CHANNELS, PROCESSED_DIR, SAMPLE_RATE, SOS, WINDOW_SAMPLES,
    csp_log_power, load_run, windows,
)
from scipy.signal import butter, iirnotch, tf2sos
from scipy.signal import sosfilt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", choices=("feet", "hands"), default="feet",
                        help="feet: T2 feet imagery vs T0 rest; hands: T2 right fist vs T1 left fist")
    parser.add_argument("--runs", type=int, nargs=3,
                        help="training run A, training run B, held-out run; must match selected task")
    parser.add_argument("--channels", default=",".join(CHANNELS), help="comma-separated 10-10 channel names")
    parser.add_argument("--low-hz", type=float, default=8.0)
    parser.add_argument("--high-hz", type=float, default=30.0)
    parser.add_argument("--window-seconds", type=float, default=2.0)
    parser.add_argument("--step-seconds", type=float, default=2.0)
    parser.add_argument("--offset-seconds", type=float, default=0.5)
    parser.add_argument("--threshold", type=float, default=0.75)
    parser.add_argument("--notch-50", action="store_true", help="append a 50 Hz notch section")
    args = parser.parse_args()
    selected_channels = tuple(name.strip() for name in args.channels.split(",") if name.strip())
    if len(selected_channels) < 2 or len(set(selected_channels)) != len(selected_channels) or any(name not in CHANNELS for name in selected_channels):
        parser.error("--channels must contain unique sensorimotor electrodes from the documented 10-10 channel list")
    if not 0.5 <= args.low_hz < args.high_hz < SAMPLE_RATE / 2:
        parser.error("the filter band must satisfy 0.5 Hz <= low < high < Nyquist (80 Hz)")
    if not 1.0 <= args.window_seconds <= 8.0 or not 0.5 <= args.step_seconds <= args.window_seconds:
        parser.error("window must be 1–8 s and step must be 0.5 s through the window length")
    if not 0.0 <= args.offset_seconds <= 3.0 or not 0.55 <= args.threshold <= 0.95:
        parser.error("event offset must be 0–3 s and confidence threshold 0.55–0.95")
    sos = butter(4, (args.low_hz, args.high_hz), btype="bandpass", fs=SAMPLE_RATE, output="sos")
    if args.notch_50:
        b, a = iirnotch(50.0, 30.0, fs=SAMPLE_RATE)
        sos = np.vstack((sos, tf2sos(b, a)))
    window_samples = round(args.window_seconds * SAMPLE_RATE)
    step_samples = round(args.step_seconds * SAMPLE_RATE)
    if args.task == "feet":
        train_runs, test_run, negative_label, positive_label = (6, 10), 14, "T0", "T2"
        negative_name, positive_name = "rest", "both-feet imagery (FORWARD proxy)"
        negative_name_ru, positive_name_ru = "Покой (T0)", "Воображение движения обеих стоп (T2)"
        negative_command, positive_command = "STOP", "FORWARD"
        model_stem = "S001_feet_vs_rest"
    else:
        train_runs, test_run, negative_label, positive_label = (4, 8), 12, "T1", "T2"
        negative_name, positive_name = "left-fist imagery (assigned TURN_LEFT)", "right-fist imagery (assigned TURN_RIGHT)"
        negative_name_ru, positive_name_ru = "Воображение движения левого кулака (T1)", "Воображение движения правого кулака (T2)"
        negative_command, positive_command = "TURN_LEFT", "TURN_RIGHT"
        model_stem = "S001_left_vs_right_fist"
    if args.runs:
        train_runs, test_run = tuple(args.runs[:2]), args.runs[2]
        allowed = (6, 10, 14) if args.task == "feet" else (4, 8, 12)
        if set(train_runs + (test_run,)) != set(allowed) or test_run in train_runs:
            parser.error(f"--runs for task {args.task} must use training runs {allowed[0]}/{allowed[1]} and held-out R{allowed[2]}")
    started = time.perf_counter()
    print("Загрузка EDF и извлечение размеченных ЭЭГ-эпох…", flush=True)
    runs = {run: load_run(run, selected_channels, sos) for run in (*train_runs, test_run)}
    train_parts = [windows(runs[run][1], runs[run][2], (negative_label, positive_label), window_samples, args.offset_seconds, step_samples) for run in train_runs]
    test_x, test_labels = windows(runs[test_run][1], runs[test_run][2], (negative_label, positive_label), window_samples, args.offset_seconds, step_samples)
    challenge_label = "T1" if args.task == "feet" else "T0"
    challenge_x, _ = windows(runs[test_run][1], runs[test_run][2], (challenge_label,), window_samples, args.offset_seconds, step_samples)
    train_x = np.concatenate([part[0] for part in train_parts])
    train_labels = np.concatenate([part[1] for part in train_parts])
    train_y = (train_labels == positive_label).astype(int)
    test_y = (test_labels == positive_label).astype(int)
    if len(np.unique(train_y)) != 2 or len(np.unique(test_y)) != 2:
        raise ValueError("Both classes must appear in training and held-out recordings")
    n_components = min(4, len(selected_channels))
    print(f"Обучение CSP + LDA: {len(train_x)} обучающих окон, {len(test_x)} отложенных тестовых окон…", flush=True)
    csp = CSP(n_components=n_components, reg="ledoit_wolf", log=True, norm_trace=False)
    csp.fit(train_x, train_y)
    filters = np.asarray(csp.filters_[:n_components], dtype=np.float64)
    lda = LinearDiscriminantAnalysis()
    lda.fit(csp_log_power(filters, train_x), train_y)
    threshold = args.threshold  # fixed before examining the held-out run
    probability = lda.predict_proba(csp_log_power(filters, test_x))[:, 1]
    predicted = (probability >= threshold).astype(int)
    challenge_probability = lda.predict_proba(csp_log_power(filters, challenge_x))[:, 1] if len(challenge_x) else np.empty((0,))
    matrix = confusion_matrix(test_y, predicted, labels=[0, 1]).tolist()
    false_motion_on_challenge = int(np.count_nonzero(challenge_probability >= threshold))
    training_seconds = time.perf_counter() - started
    trained_at_utc = datetime.now(timezone.utc).isoformat(timespec="seconds")
    metrics = {
        "trained_at_utc": trained_at_utc,
        "subject": "S001", "task": args.task, "training_runs": list(train_runs), "held_out_run": test_run,
        "classes": {"0": negative_name, "1": positive_name},
        "classes_ru": {"0": negative_name_ru, "1": positive_name_ru},
        "commands": {"0": negative_command, "1": positive_command},
        "excluded_challenge_class": challenge_label,
        "window_seconds": window_samples / SAMPLE_RATE,
        "step_seconds": step_samples / SAMPLE_RATE,
        "event_offset_seconds": args.offset_seconds,
        "channels": list(selected_channels),
        "filter_band_hz": [args.low_hz, args.high_hz],
        "notch_50_hz": args.notch_50,
        "nonoverlapping_train_test": True,
        "train_windows": int(len(train_x)), "test_windows": int(len(test_x)),
        "challenge_windows": int(len(challenge_x)),
        "forward_threshold": threshold,
        "accuracy": float(accuracy_score(test_y, predicted)),
        "balanced_accuracy": float(balanced_accuracy_score(test_y, predicted)),
        "precision_positive": float(precision_score(test_y, predicted, zero_division=0)),
        "recall_positive": float(recall_score(test_y, predicted, zero_division=0)),
        "f1_positive": float(f1_score(test_y, predicted, zero_division=0)),
        "confusion_matrix_rows_actual_negative_positive": matrix,
        "false_motion_on_negative": int(matrix[0][1]),
        "false_motion_on_challenge": false_motion_on_challenge,
        "training_seconds": training_seconds,
        "label_warning": ("Both-feet imagery is not walking imagery." if args.task == "feet" else
                          "Left/right fist imagery is conditionally mapped to avatar turns; it is not natural body-turn imagery."),
        "label_warning_ru": ("Воображение движения стоп не является воображением ходьбы." if args.task == "feet" else
                             "Воображение левого и правого кулака условно сопоставлено поворотам аватара; это не распознавание естественного намерения повернуть тело."),
    }
    model = {
        "format": "metasiberia-neurocontrol-csp-lda-v1",
        "trained_at_utc": trained_at_utc,
        "sample_rate_hz": SAMPLE_RATE,
        "channels": list(selected_channels),
        "window_samples": window_samples,
        "step_samples": step_samples,
        "sos_filter": sos.tolist(),
        "sos_bandpass_8_30_hz": sos.tolist(),  # legacy C++ model readers
        "filter_band_hz": [args.low_hz, args.high_hz],
        "notch_50_hz": args.notch_50,
        "normalization": "CSP spatial projection + log-power; no additional per-recording normalization",
        "preprocessing": {
            "channels_in_order": list(selected_channels),
            "sample_rate_hz": SAMPLE_RATE,
            "filter_band_hz": [args.low_hz, args.high_hz],
            "notch_50_hz": args.notch_50,
            "window_samples": window_samples,
            "step_samples": step_samples,
            "event_offset_seconds": args.offset_seconds,
            "normalization": "CSP spatial projection + log-power; no additional per-recording normalization",
        },
        "csp_filters": filters.tolist(),
        "lda_coefficients": lda.coef_[0].tolist(),
        "lda_intercept": float(lda.intercept_[0]),
        "forward_threshold": threshold,
        "training_runs": list(train_runs),
        "test_run": test_run,
        "negative_class": negative_name,
        "positive_class": positive_name,
        "negative_class_ru": negative_name_ru,
        "positive_class_ru": positive_name_ru,
        "negative_command": negative_command,
        "positive_command": positive_command,
        "command_mapping_is_experimental": args.task == "hands",
        "python_versions": {"mne": mne.__version__, "numpy": np.__version__, "scipy": scipy.__version__, "scikit_learn": sklearn.__version__},
        "meaning": metrics["label_warning"],
    }
    models = ROOT / "models"
    models.mkdir(exist_ok=True)
    (models / f"{model_stem}.json").write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    (PROCESSED_DIR / f"{model_stem}_metrics.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
