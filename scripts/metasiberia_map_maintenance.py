#!/usr/bin/env python3
"""
Metasiberia map maintenance utility:
- sample: record map tile progress + write public JSON status
- regen: trigger low-cost map refresh (admin_regen_map_tiles_post) when queue is idle
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.error import URLError
from urllib.request import Request, urlopen
import ssl


BASE_URL = os.environ.get("METASIBERIA_BASE_URL", "https://vr.metasiberia.com").rstrip("/")
STATE_BIN = Path(os.environ.get("METASIBERIA_STATE_BIN", "/root/cyberspace_server_state/server_state.bin"))
PUBLIC_DIR = Path(os.environ.get("METASIBERIA_PUBLIC_DIR", "/root/cyberspace_server_state/webserver_public_files"))
PROGRESS_CSV = Path(os.environ.get("METASIBERIA_MAP_PROGRESS_CSV", "/var/log/metasiberia-map-progress.csv"))
REFRESH_LOG = Path(os.environ.get("METASIBERIA_MAP_REFRESH_LOG", "/var/log/metasiberia-map-refresh.log"))
MAP_PROGRESS_JSON = PUBLIC_DIR / "map_progress.json"
MAP_REFRESH_STATUS_JSON = PUBLIC_DIR / "map_refresh_status.json"
MAX_PROGRESS_LINES = int(os.environ.get("METASIBERIA_MAP_PROGRESS_MAX_LINES", "5000"))
PUBLIC_PROGRESS_POINTS = int(os.environ.get("METASIBERIA_MAP_PROGRESS_PUBLIC_POINTS", "288"))
TMP_CLEAN_SECONDS = int(os.environ.get("METASIBERIA_TMP_CLEAN_SECONDS", "86400"))


def now_iso() -> str:
	return datetime.now().astimezone().isoformat(timespec="seconds")


def ensure_parent_dir(path: Path) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)


def write_json_atomic(path: Path, payload: Dict) -> None:
	ensure_parent_dir(path)
	tmp_path = path.with_suffix(path.suffix + ".tmp")
	tmp_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
	os.replace(tmp_path, path)


def parse_counts(html: str) -> Tuple[int, int, int]:
	done = html.count("state: Done")
	not_done = html.count("state: Not done")
	return done, not_done, done + not_done


def fetch_url(path: str, token: str, method: str = "GET", timeout_s: int = 15) -> Tuple[int, str, str]:
	url = BASE_URL + path
	req = Request(
		url=url,
		method=method,
		headers={
			"Cookie": f"site-b={token}",
			"User-Agent": "metasiberia-map-maintenance/1.0",
		},
	)
	ctx = ssl._create_unverified_context()
	with urlopen(req, context=ctx, timeout=timeout_s) as resp:
		body = resp.read().decode("utf-8", "ignore")
		return resp.status, resp.geturl(), body


def extract_candidate_tokens(state_bin: Path) -> List[str]:
	if not state_bin.is_file():
		return []
	try:
		out = subprocess.check_output(
			["strings", "-n", "8", str(state_bin)],
			text=True,
			errors="ignore",
		)
	except Exception:
		return []
	return sorted(set(re.findall(r"[A-Za-z0-9+/]{22}==", out)))


def find_admin_token() -> Optional[str]:
	candidates = extract_candidate_tokens(STATE_BIN)
	for token in candidates:
		try:
			_, _, body = fetch_url("/admin_map", token, method="GET", timeout_s=5)
		except Exception:
			continue
		if "You are logged in as" in body and ("Admin" in body or "Админ" in body):
			return token
	return None


def trim_progress_csv() -> None:
	if not PROGRESS_CSV.exists():
		return
	lines = PROGRESS_CSV.read_text(encoding="utf-8", errors="ignore").splitlines()
	if len(lines) <= MAX_PROGRESS_LINES:
		return
	PROGRESS_CSV.write_text("\n".join(lines[-MAX_PROGRESS_LINES:]) + "\n", encoding="utf-8")


def read_recent_progress_points() -> List[Dict]:
	if not PROGRESS_CSV.exists():
		return []
	lines = PROGRESS_CSV.read_text(encoding="utf-8", errors="ignore").splitlines()
	points: List[Dict] = []
	for line in lines[-PUBLIC_PROGRESS_POINTS:]:
		parts = line.split(",")
		if len(parts) != 4:
			continue
		try:
			points.append(
				{
					"ts": parts[0],
					"done": int(parts[1]),
					"not_done": int(parts[2]),
					"total": int(parts[3]),
				}
			)
		except ValueError:
			continue
	return points


def append_progress(ts: str, done: int, not_done: int, total: int) -> None:
	ensure_parent_dir(PROGRESS_CSV)
	with PROGRESS_CSV.open("a", encoding="utf-8") as f:
		f.write(f"{ts},{done},{not_done},{total}\n")
	trim_progress_csv()


def append_refresh_log(message: str) -> None:
	ensure_parent_dir(REFRESH_LOG)
	with REFRESH_LOG.open("a", encoding="utf-8") as f:
		f.write(f"{now_iso()} {message}\n")


def cleanup_tmp_files() -> int:
	now = time.time()
	removed = 0
	for pattern in ("/tmp/tile_*.jpg", "/tmp/screenshot_*.jpg"):
		for path_str in glob.glob(pattern):
			try:
				path = Path(path_str)
				if (now - path.stat().st_mtime) > TMP_CLEAN_SECONDS:
					path.unlink(missing_ok=True)
					removed += 1
			except Exception:
				pass
	return removed


def write_progress_json(last_status: Dict) -> None:
	write_json_atomic(
		MAP_PROGRESS_JSON,
		{
			"updated_at": now_iso(),
			"last_status": last_status,
			"points": read_recent_progress_points(),
		},
	)


def sample_mode() -> int:
	token = find_admin_token()
	if not token:
		err = {
			"ok": False,
			"mode": "sample",
			"updated_at": now_iso(),
			"error": "no_admin_session_token_found",
		}
		write_json_atomic(MAP_REFRESH_STATUS_JSON, err)
		write_progress_json(err)
		append_refresh_log("sample failed: no admin session token found")
		return 1

	try:
		_, _, body = fetch_url("/admin_map", token, method="GET")
	except URLError as e:
		err = {
			"ok": False,
			"mode": "sample",
			"updated_at": now_iso(),
			"error": f"admin_map_fetch_failed: {e}",
		}
		write_json_atomic(MAP_REFRESH_STATUS_JSON, err)
		write_progress_json(err)
		append_refresh_log(f"sample failed: {err['error']}")
		return 1

	done, not_done, total = parse_counts(body)
	ts = now_iso()
	append_progress(ts, done, not_done, total)
	tmp_removed = cleanup_tmp_files()

	status = {
		"ok": True,
		"mode": "sample",
		"updated_at": ts,
		"done": done,
		"not_done": not_done,
		"total": total,
		"tmp_removed": tmp_removed,
	}
	write_json_atomic(MAP_REFRESH_STATUS_JSON, status)
	write_progress_json(status)
	print(json.dumps(status, ensure_ascii=False))
	return 0


def regen_mode() -> int:
	token = find_admin_token()
	if not token:
		err = {
			"ok": False,
			"mode": "regen",
			"updated_at": now_iso(),
			"error": "no_admin_session_token_found",
		}
		write_json_atomic(MAP_REFRESH_STATUS_JSON, err)
		write_progress_json(err)
		append_refresh_log("regen failed: no admin session token found")
		return 1

	_, _, before_body = fetch_url("/admin_map", token, method="GET")
	before_done, before_not_done, before_total = parse_counts(before_body)

	# Economy mode: do not enqueue a new full refresh while previous queue is still processing.
	if before_not_done > 0:
		status = {
			"ok": True,
			"mode": "regen",
			"updated_at": now_iso(),
			"action": "skipped_pending_queue",
			"before_done": before_done,
			"before_not_done": before_not_done,
			"before_total": before_total,
		}
		write_json_atomic(MAP_REFRESH_STATUS_JSON, status)
		write_progress_json(status)
		append_refresh_log(
			f"regen skipped: pending queue (done={before_done}, not_done={before_not_done}, total={before_total})"
		)
		print(json.dumps(status, ensure_ascii=False))
		return 0

	post_status, post_url, _ = fetch_url("/admin_regen_map_tiles_post", token, method="POST")
	time.sleep(2.0)
	_, _, after_body = fetch_url("/admin_map", token, method="GET")
	after_done, after_not_done, after_total = parse_counts(after_body)
	ts = now_iso()

	append_progress(ts, after_done, after_not_done, after_total)
	tmp_removed = cleanup_tmp_files()

	status = {
		"ok": True,
		"mode": "regen",
		"updated_at": ts,
		"action": "regen_triggered",
		"post_status": post_status,
		"post_url": post_url,
		"before_done": before_done,
		"before_not_done": before_not_done,
		"before_total": before_total,
		"after_done": after_done,
		"after_not_done": after_not_done,
		"after_total": after_total,
		"tmp_removed": tmp_removed,
	}
	write_json_atomic(MAP_REFRESH_STATUS_JSON, status)
	write_progress_json(status)
	append_refresh_log(
		"regen triggered: "
		f"before(done={before_done},not_done={before_not_done},total={before_total}) "
		f"after(done={after_done},not_done={after_not_done},total={after_total}) status={post_status}"
	)
	print(json.dumps(status, ensure_ascii=False))
	return 0


def main() -> int:
	parser = argparse.ArgumentParser(description="Metasiberia map maintenance utility")
	parser.add_argument("mode", choices=["sample", "regen"], help="sample progress or trigger regen")
	args = parser.parse_args()

	PUBLIC_DIR.mkdir(parents=True, exist_ok=True)

	if args.mode == "sample":
		return sample_mode()
	if args.mode == "regen":
		return regen_mode()
	return 2


if __name__ == "__main__":
	sys.exit(main())

