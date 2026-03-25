#!/usr/bin/env python3
"""Summarise Metasiberia XR pose traces into human-readable diagnostics."""

from __future__ import annotations

import argparse
import csv
import math
import os
import statistics
import sys
from dataclasses import dataclass
from typing import Iterable, List, Optional


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Analyse xr_pose_trace.csv produced by the Metasiberia Qt/OpenXR client "
            "and report head alignment, horizon offset, height and eye symmetry."
        )
    )
    default_path = os.path.join(
        os.environ.get("APPDATA", ""),
        "Cyberspace",
        "xr_pose_trace.csv",
    )
    parser.add_argument(
        "trace_csv",
        nargs="?",
        default=default_path,
        help=f"Path to xr_pose_trace.csv (default: {default_path})",
    )
    return parser.parse_args()


def parse_float(row: dict[str, str], key: str) -> Optional[float]:
    raw = row.get(key, "").strip()
    if raw == "":
        return None
    try:
        return float(raw)
    except ValueError:
        return None


def parse_bool(row: dict[str, str], key: str) -> bool:
    return row.get(key, "").strip() == "1"


@dataclass
class TraceRow:
    time_s: float
    session_state: str
    reference_space: str
    raw_pitch_deg: Optional[float]
    world_pitch_deg: Optional[float]
    left_world_pitch_deg: Optional[float]
    right_world_pitch_deg: Optional[float]
    world_z: Optional[float]
    left_world_z: Optional[float]
    right_world_z: Optional[float]
    world_active: bool
    world_pos_valid: bool
    world_orient_valid: bool
    left_world_active: bool
    right_world_active: bool

    @property
    def raw_pitch_from_horizon_deg(self) -> Optional[float]:
        return None if self.raw_pitch_deg is None else self.raw_pitch_deg - 90.0

    @property
    def world_pitch_from_horizon_deg(self) -> Optional[float]:
        return None if self.world_pitch_deg is None else self.world_pitch_deg - 90.0

    @property
    def pitch_correction_deg(self) -> Optional[float]:
        if self.raw_pitch_deg is None or self.world_pitch_deg is None:
            return None
        return self.world_pitch_deg - self.raw_pitch_deg

    @property
    def eye_pitch_delta_deg(self) -> Optional[float]:
        if self.left_world_pitch_deg is None or self.right_world_pitch_deg is None:
            return None
        return self.left_world_pitch_deg - self.right_world_pitch_deg

    @property
    def eye_height_delta_m(self) -> Optional[float]:
        if self.left_world_z is None or self.right_world_z is None:
            return None
        return self.left_world_z - self.right_world_z


def load_rows(path: str) -> List[TraceRow]:
    rows: List[TraceRow] = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for raw in reader:
            time_s = parse_float(raw, "time_s")
            if time_s is None:
                continue
            rows.append(
                TraceRow(
                    time_s=time_s,
                    session_state=raw.get("session_state", "").strip(),
                    reference_space=raw.get("reference_space", "").strip(),
                    raw_pitch_deg=parse_float(raw, "raw_pitch_deg"),
                    world_pitch_deg=parse_float(raw, "world_pitch_deg"),
                    left_world_pitch_deg=parse_float(raw, "left_world_pitch_deg"),
                    right_world_pitch_deg=parse_float(raw, "right_world_pitch_deg"),
                    world_z=parse_float(raw, "world_z"),
                    left_world_z=parse_float(raw, "left_world_z"),
                    right_world_z=parse_float(raw, "right_world_z"),
                    world_active=parse_bool(raw, "world_active"),
                    world_pos_valid=parse_bool(raw, "world_pos_valid"),
                    world_orient_valid=parse_bool(raw, "world_orient_valid"),
                    left_world_active=parse_bool(raw, "left_world_active"),
                    right_world_active=parse_bool(raw, "right_world_active"),
                )
            )
    return rows


def valid_world_rows(rows: Iterable[TraceRow]) -> List[TraceRow]:
    return [
        row
        for row in rows
        if row.world_active and row.world_pos_valid and row.world_orient_valid
    ]


def focused_rows(rows: Iterable[TraceRow]) -> List[TraceRow]:
    return [row for row in valid_world_rows(rows) if row.session_state == "FOCUSED"]


def fmt(value: Optional[float], digits: int = 3) -> str:
    if value is None or not math.isfinite(value):
        return "-"
    return f"{value:.{digits}f}"


def median_or_none(values: Iterable[Optional[float]]) -> Optional[float]:
    clean = [v for v in values if v is not None and math.isfinite(v)]
    if not clean:
        return None
    return statistics.median(clean)


def mean_or_none(values: Iterable[Optional[float]]) -> Optional[float]:
    clean = [v for v in values if v is not None and math.isfinite(v)]
    if not clean:
        return None
    return statistics.fmean(clean)


def min_or_none(values: Iterable[Optional[float]]) -> Optional[float]:
    clean = [v for v in values if v is not None and math.isfinite(v)]
    return min(clean) if clean else None


def max_or_none(values: Iterable[Optional[float]]) -> Optional[float]:
    clean = [v for v in values if v is not None and math.isfinite(v)]
    return max(clean) if clean else None


def find_first_stable_window(rows: List[TraceRow], window_size: int = 5) -> Optional[List[TraceRow]]:
    if len(rows) < window_size:
        return None

    for start in range(0, len(rows) - window_size + 1):
        window = rows[start : start + window_size]
        pitch_from_horizon = [row.world_pitch_from_horizon_deg for row in window]
        heights = [row.world_z for row in window]
        if any(v is None for v in pitch_from_horizon) or any(v is None for v in heights):
            continue

        pitch_abs_max = max(abs(v) for v in pitch_from_horizon if v is not None)
        pitch_spread = max(v for v in pitch_from_horizon if v is not None) - min(v for v in pitch_from_horizon if v is not None)
        height_spread = max(v for v in heights if v is not None) - min(v for v in heights if v is not None)

        if pitch_abs_max <= 5.0 and pitch_spread <= 2.5 and height_spread <= 0.15:
            return window

    return None


def print_section(title: str) -> None:
    print(title)
    print("-" * len(title))


def main() -> int:
    args = parse_args()

    if not os.path.isfile(args.trace_csv):
        print(f"Trace file not found: {args.trace_csv}", file=sys.stderr)
        return 1

    rows = load_rows(args.trace_csv)
    if not rows:
        print(f"No parseable rows in trace file: {args.trace_csv}", file=sys.stderr)
        return 1

    valid_rows = valid_world_rows(rows)
    focused = focused_rows(rows)
    stable_window = find_first_stable_window(focused)

    print(f"XR trace: {args.trace_csv}")
    print(f"Rows: {len(rows)}, valid world-pose rows: {len(valid_rows)}, focused rows: {len(focused)}")

    print_section("Session")
    first_valid = valid_rows[0] if valid_rows else None
    if first_valid is not None:
        print(f"First valid sample at {fmt(first_valid.time_s, 3)} s, state={first_valid.session_state}, reference_space={first_valid.reference_space}")
    else:
        print("No valid world-pose samples found.")

    print_section("Pitch")
    raw_pitch_from_horizon = [row.raw_pitch_from_horizon_deg for row in valid_rows]
    world_pitch_from_horizon = [row.world_pitch_from_horizon_deg for row in valid_rows]
    pitch_correction = [row.pitch_correction_deg for row in focused]
    print(
        "Raw pitch from horizon (deg): "
        f"median={fmt(median_or_none(raw_pitch_from_horizon), 2)}, "
        f"min={fmt(min_or_none(raw_pitch_from_horizon), 2)}, "
        f"max={fmt(max_or_none(raw_pitch_from_horizon), 2)}"
    )
    print(
        "Corrected world pitch from horizon (deg): "
        f"median={fmt(median_or_none(world_pitch_from_horizon), 2)}, "
        f"min={fmt(min_or_none(world_pitch_from_horizon), 2)}, "
        f"max={fmt(max_or_none(world_pitch_from_horizon), 2)}"
    )
    print(
        "Focused correction raw->world (deg): "
        f"mean={fmt(mean_or_none(pitch_correction), 2)}, "
        f"median={fmt(median_or_none(pitch_correction), 2)}"
    )

    print_section("Height")
    heights = [row.world_z for row in valid_rows]
    print(
        "World head height z (m): "
        f"median={fmt(median_or_none(heights), 3)}, "
        f"min={fmt(min_or_none(heights), 3)}, "
        f"max={fmt(max_or_none(heights), 3)}"
    )
    if stable_window is not None:
        stable_height = mean_or_none([row.world_z for row in stable_window])
        stable_pitch = mean_or_none([row.world_pitch_from_horizon_deg for row in stable_window])
        print(
            "First stable focused neutral window: "
            f"{fmt(stable_window[0].time_s, 3)}-{fmt(stable_window[-1].time_s, 3)} s, "
            f"height~{fmt(stable_height, 3)} m, "
            f"pitch_from_horizon~{fmt(stable_pitch, 2)} deg"
        )
    else:
        print("No stable focused neutral window detected.")

    print_section("Eye symmetry")
    eye_pitch_delta = [row.eye_pitch_delta_deg for row in focused]
    eye_height_delta = [row.eye_height_delta_m for row in focused]
    print(
        "Left-right eye world pitch delta (deg): "
        f"median={fmt(median_or_none(eye_pitch_delta), 4)}, "
        f"max_abs={fmt(max((abs(v) for v in eye_pitch_delta if v is not None), default=None), 4)}"
    )
    print(
        "Left-right eye world height delta (m): "
        f"median={fmt(median_or_none(eye_height_delta), 4)}, "
        f"max_abs={fmt(max((abs(v) for v in eye_height_delta if v is not None), default=None), 4)}"
    )

    print_section("Heuristics")
    notes: List[str] = []

    stable_pitch = None
    stable_height = None
    if stable_window is not None:
        stable_pitch = mean_or_none([row.world_pitch_from_horizon_deg for row in stable_window])
        stable_height = mean_or_none([row.world_z for row in stable_window])

    correction_median = median_or_none(pitch_correction)
    if correction_median is not None and abs(correction_median) >= 5.0:
        notes.append(
            f"Calibration is applying a non-trivial pitch correction of about {fmt(correction_median, 2)} deg."
        )
    if stable_pitch is not None and abs(stable_pitch) <= 5.0:
        notes.append("A stable focused window reaches near-horizon alignment. Head neutralisation appears to work in that window.")
    elif stable_pitch is not None:
        notes.append("Even the first stable focused window is still far from horizon. Suspect head-alignment or runtime recenter issues.")
    else:
        notes.append("No stable neutral focused window was found. Capture a trace right after pressing Home while wearing the headset and looking straight.")

    if stable_height is not None:
        if 1.45 <= stable_height <= 1.90:
            notes.append(
                f"Stable head height is about {fmt(stable_height, 3)} m. STAGE floor alignment looks plausible for a standing user."
            )
        elif stable_height < 1.20:
            notes.append(
                f"Stable head height is only about {fmt(stable_height, 3)} m. That points to crouched capture, wrong floor origin or seated/low headset calibration."
            )
        else:
            notes.append(
                f"Stable head height is about {fmt(stable_height, 3)} m. Check this against the real user height and recenter posture."
            )

    max_eye_pitch_abs = max((abs(v) for v in eye_pitch_delta if v is not None), default=0.0)
    if max_eye_pitch_abs <= 0.2:
        notes.append("Left/right eye world pitch is symmetric. This does not look like a per-eye transform mismatch.")
    else:
        notes.append(
            f"Left/right eye world pitch delta reaches {fmt(max_eye_pitch_abs, 3)} deg. Inspect per-eye pose composition."
        )

    if stable_height is not None and 1.45 <= stable_height <= 1.90 and stable_pitch is not None and abs(stable_pitch) <= 5.0:
        notes.append("If the avatar still looks sunk or too small with these numbers, the likely bug is avatar rig eye-height / model grounding, not OpenXR floor space.")

    for note in notes:
        print(f"* {note}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
