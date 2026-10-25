"""Compare streaming C++ predictions with the same causal Python pipeline."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

import numpy as np
from scipy.signal import sosfilt

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "processing"))
from pipeline import csp_log_power  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=pathlib.Path)
    args = parser.parse_args()
    model_path = ROOT / "models" / "S001_feet_vs_rest.json"
    data_path = ROOT / "datasets" / "processed" / "S001R14.f32"
    model = json.loads(model_path.read_text(encoding="utf-8"))
    channels = len(model["channels"])
    raw = np.fromfile(data_path, dtype="<f4").reshape(-1, channels).astype(np.float64)
    filtered = sosfilt(np.asarray(model["sos_bandpass_8_30_hz"]), raw, axis=0)
    filters = np.asarray(model["csp_filters"])
    weights = np.asarray(model["lda_coefficients"])
    window = model["window_samples"]
    stride = model.get("step_samples") or model["sample_rate_hz"]
    process = subprocess.run([str(args.executable), str(model_path), str(data_path)], check=True, text=True, capture_output=True)
    cpp_rows = [line.split(",") for line in process.stdout.splitlines()]
    expected_ends = list(range(window, len(raw) + 1, stride))
    if len(cpp_rows) != len(expected_ends):
        raise AssertionError(f"C++ produced {len(cpp_rows)} predictions; expected {len(expected_ends)}")
    largest_error = 0.0
    for row, end in zip(cpp_rows, expected_ends):
        if int(row[0]) != end or row[2] != "1":
            raise AssertionError(f"Bad sample time or quality at {end}: {row}")
        epoch = filtered[end - window:end].T[np.newaxis, :, :]
        decision = float(csp_log_power(filters, epoch)[0] @ weights + model["lda_intercept"])
        expected = float(1.0 / (1.0 + np.exp(-decision)))
        error = abs(float(row[1]) - expected)
        largest_error = max(largest_error, error)
        if error > 0.0001:
            raise AssertionError(f"C++/Python model mismatch at {end}: {error}")
    print(f"Matched {len(cpp_rows)} real EEG inferences; max probability difference {largest_error:.6g}")


if __name__ == "__main__":
    main()
