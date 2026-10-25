#!/usr/bin/env python3
"""Run the native classifier against the prepared held-out S001R14 recording."""

import math
import subprocess
import sys
from pathlib import Path


executable, model_path, recording_path = map(Path, sys.argv[1:4])
for path in (executable, model_path, recording_path):
    if not path.is_file():
        raise SystemExit(f"classifier smoke input is missing: {path}")

result = subprocess.run(
    [str(executable), str(model_path), str(recording_path)],
    check=False,
    capture_output=True,
    text=True,
)
if result.returncode:
    raise SystemExit(f"native classifier exited with {result.returncode}: {result.stderr.strip()}")

rows = []
for line in result.stdout.splitlines():
    fields = line.split(",")
    if len(fields) != 4:
        raise SystemExit("native classifier returned a malformed prediction row")
    sample_index = int(fields[0])
    probability = float(fields[1])
    signal_good = fields[2] == "1"
    forward = fields[3] == "1"
    if not math.isfinite(probability) or not 0.0 <= probability <= 1.0:
        raise SystemExit("native classifier returned a non-finite/out-of-range probability")
    if fields[2] not in {"0", "1"} or fields[3] not in {"0", "1"}:
        raise SystemExit("native classifier returned an invalid state flag")
    rows.append((sample_index, probability, signal_good, forward))

if len(rows) < 10:
    raise SystemExit(f"expected at least 10 held-out predictions, received {len(rows)}")
if any(right[0] <= left[0] for left, right in zip(rows, rows[1:])):
    raise SystemExit("classifier sample indices are not strictly increasing")

print(f"Native classifier smoke OK: {len(rows)} finite predictions; {sum(row[3] for row in rows)} FORWARD decisions")
