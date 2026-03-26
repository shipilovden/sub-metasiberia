#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable, List, Sequence


SCRIPT_PATH = Path(__file__).resolve()
WORKSPACE_ROOT = SCRIPT_PATH.parents[2]
DEFAULT_DEST_DIR = WORKSPACE_ROOT / "recordings"
DEFAULT_REMOTE_DIR = "/sdcard/Movies/Screenrecorder"
DEFAULT_STATE_PATH = DEFAULT_DEST_DIR / ".vive_recording_sync_state.json"
VIDEO_SUFFIXES = (".mp4", ".mov", ".webm")


def print_err(message: str) -> None:
	print(message, file=sys.stderr)


def run_command(args: Sequence[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
	return subprocess.run(args, text=True, capture_output=True, check=check)


def find_adb(explicit_path: str | None) -> Path:
	candidates: List[Path] = []
	if explicit_path:
		candidates.append(Path(explicit_path))

	adb_from_path = shutil.which("adb")
	if adb_from_path:
		candidates.append(Path(adb_from_path))

	local_app_data = os.environ.get("LOCALAPPDATA")
	user_profile = os.environ.get("USERPROFILE")
	if local_app_data:
		candidates.append(Path(local_app_data) / "Android" / "Sdk" / "platform-tools" / "adb.exe")
	if user_profile:
		candidates.append(Path(user_profile) / "AppData" / "Local" / "Android" / "Sdk" / "platform-tools" / "adb.exe")

	candidates.extend([
		Path("C:/Android/platform-tools/adb.exe"),
		Path("C:/platform-tools/adb.exe"),
		Path("C:/Program Files/SideQuest/resources/app/platform-tools/adb.exe"),
	])

	for candidate in candidates:
		if candidate.is_file():
			return candidate

	raise RuntimeError(
		"Could not find adb.exe. Install Android platform-tools or pass --adb-path explicitly."
	)


def list_connected_devices(adb_path: Path) -> List[str]:
	result = run_command([str(adb_path), "devices"], check=True)
	devices: List[str] = []
	for line in result.stdout.splitlines():
		line = line.strip()
		if not line or line.startswith("List of devices attached"):
			continue
		parts = line.split()
		if len(parts) >= 2 and parts[1] == "device":
			devices.append(parts[0])
	return devices


def adb_shell(adb_path: Path, serial: str, command: str) -> str:
	result = run_command([str(adb_path), "-s", serial, "shell", command], check=True)
	return result.stdout


def list_remote_recordings(adb_path: Path, serial: str, remote_dir: str) -> List[str]:
	command = f"ls -1t {remote_dir}"
	try:
		stdout = adb_shell(adb_path, serial, command)
	except subprocess.CalledProcessError as exc:
		stderr = (exc.stderr or "").strip()
		raise RuntimeError(
			f"Could not list remote recordings in '{remote_dir}'. adb stderr: {stderr or '<empty>'}"
		) from exc

	recordings: List[str] = []
	for line in stdout.splitlines():
		name = line.strip()
		if not name or name.startswith("total "):
			continue
		if name.lower().endswith(VIDEO_SUFFIXES):
			recordings.append(f"{remote_dir.rstrip('/')}/{name}")
	return recordings


def remote_basename(remote_path: str) -> str:
	return remote_path.rsplit("/", 1)[-1]


def load_state(state_path: Path) -> dict:
	if not state_path.is_file():
		return {"seen_remote_files": []}
	try:
		return json.loads(state_path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		return {"seen_remote_files": []}


def save_state(state_path: Path, remote_files: Iterable[str]) -> None:
	state_path.parent.mkdir(parents=True, exist_ok=True)
	unique_sorted = sorted(set(remote_files))
	state_path.write_text(
		json.dumps({"seen_remote_files": unique_sorted}, indent=2, ensure_ascii=True),
		encoding="utf-8",
	)


def copy_remote_file(adb_path: Path, serial: str, remote_path: str, dest_dir: Path) -> Path:
	dest_dir.mkdir(parents=True, exist_ok=True)
	dest_path = dest_dir / remote_basename(remote_path)
	run_command([str(adb_path), "-s", serial, "pull", remote_path, str(dest_path)], check=True)
	return dest_path


def seed_seen_remote_files(dest_dir: Path, remote_files: Sequence[str]) -> List[str]:
	existing_names = {path.name for path in dest_dir.glob("*") if path.is_file()}
	return [remote for remote in remote_files if remote_basename(remote) in existing_names]


def copy_latest_files(adb_path: Path, serial: str, remote_dir: str, dest_dir: Path, latest_count: int) -> List[Path]:
	remote_files = list_remote_recordings(adb_path, serial, remote_dir)
	if not remote_files:
		return []

	copied_paths: List[Path] = []
	for remote_path in reversed(remote_files[:latest_count]):
		copied_paths.append(copy_remote_file(adb_path, serial, remote_path, dest_dir))
	return copied_paths


def watch_for_new_recordings(
	adb_path: Path,
	serial: str,
	remote_dir: str,
	dest_dir: Path,
	state_path: Path,
	poll_interval: float,
	latest_on_start: int,
) -> int:
	remote_files = list_remote_recordings(adb_path, serial, remote_dir)
	seen_remote_files = set(load_state(state_path).get("seen_remote_files", []))

	if latest_on_start > 0:
		for remote_path in reversed(remote_files[:latest_on_start]):
			dest_path = copy_remote_file(adb_path, serial, remote_path, dest_dir)
			print(f"Copied recent recording to '{dest_path}'.")
			seen_remote_files.add(remote_path)
	else:
		seen_remote_files.update(seed_seen_remote_files(dest_dir, remote_files))
		seen_remote_files.update(remote_files)

	save_state(state_path, seen_remote_files)
	print(
		f"Watching '{remote_dir}' on device {serial}. "
		f"New recordings will be copied to '{dest_dir}'. Press Ctrl+C to stop."
	)

	try:
		while True:
			time.sleep(poll_interval)
			current_remote_files = list_remote_recordings(adb_path, serial, remote_dir)
			new_remote_files = [remote for remote in reversed(current_remote_files) if remote not in seen_remote_files]
			for remote_path in new_remote_files:
				dest_path = copy_remote_file(adb_path, serial, remote_path, dest_dir)
				print(f"Copied new recording to '{dest_path}'.")
				seen_remote_files.add(remote_path)
			save_state(state_path, seen_remote_files)
	except KeyboardInterrupt:
		print("Stopped recording sync.")
		return 0


def build_arg_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(
		description="Copy the latest VIVE Focus 3 recordings from adb to a local recordings folder."
	)
	parser.add_argument("--adb-path", help="Explicit path to adb.exe")
	parser.add_argument("--serial", help="adb device serial to use when multiple devices are connected")
	parser.add_argument("--remote-dir", default=DEFAULT_REMOTE_DIR, help=f"Remote recording directory (default: {DEFAULT_REMOTE_DIR})")
	parser.add_argument("--dest-dir", default=str(DEFAULT_DEST_DIR), help=f"Destination directory (default: {DEFAULT_DEST_DIR})")
	parser.add_argument("--state-path", default=str(DEFAULT_STATE_PATH), help=f"Watch-mode state file (default: {DEFAULT_STATE_PATH})")
	parser.add_argument("--latest", type=int, default=1, help="Copy the latest N recordings once and exit (default: 1)")
	parser.add_argument("--watch", action="store_true", help="Keep polling the headset and copy each new recording automatically")
	parser.add_argument("--latest-on-start", type=int, default=0, help="When --watch is used, also copy the latest N recordings immediately on startup")
	parser.add_argument("--poll-interval", type=float, default=5.0, help="Watch-mode polling interval in seconds (default: 5)")
	return parser


def resolve_device_serial(adb_path: Path, explicit_serial: str | None) -> str:
	devices = list_connected_devices(adb_path)
	if explicit_serial:
		if explicit_serial not in devices:
			raise RuntimeError(f"Requested adb serial '{explicit_serial}' is not connected. Connected devices: {devices}")
		return explicit_serial

	if not devices:
		raise RuntimeError(
			"No adb devices are connected. Make sure the headset is connected by USB, USB debugging is enabled, and the RSA prompt was accepted."
		)
	if len(devices) > 1:
		raise RuntimeError(f"Multiple adb devices are connected: {devices}. Re-run with --serial.")
	return devices[0]


def main() -> int:
	parser = build_arg_parser()
	args = parser.parse_args()

	try:
		adb_path = find_adb(args.adb_path)
		serial = resolve_device_serial(adb_path, args.serial)
		dest_dir = Path(args.dest_dir).expanduser().resolve()
		state_path = Path(args.state_path).expanduser().resolve()

		if args.watch:
			return watch_for_new_recordings(
				adb_path=adb_path,
				serial=serial,
				remote_dir=args.remote_dir,
				dest_dir=dest_dir,
				state_path=state_path,
				poll_interval=max(args.poll_interval, 1.0),
				latest_on_start=max(args.latest_on_start, 0),
			)

		copied_paths = copy_latest_files(
			adb_path=adb_path,
			serial=serial,
			remote_dir=args.remote_dir,
			dest_dir=dest_dir,
			latest_count=max(args.latest, 1),
		)
		if not copied_paths:
			print(f"No recordings found in '{args.remote_dir}'.")
			return 0

		for path in copied_paths:
			print(f"Copied '{path}'.")
		return 0
	except RuntimeError as exc:
		print_err(f"Error: {exc}")
		return 1
	except subprocess.CalledProcessError as exc:
		stderr = (exc.stderr or "").strip()
		print_err(f"Command failed: {' '.join(exc.cmd)}")
		if stderr:
			print_err(stderr)
		return exc.returncode or 1


if __name__ == "__main__":
	sys.exit(main())
