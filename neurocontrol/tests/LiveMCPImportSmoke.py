#!/usr/bin/env python3
"""Exercise NeuroControl imports through a running built Qt client's real MCP endpoint."""

from __future__ import annotations

import http.client
import json
import sys
import tempfile
import time
import uuid
from pathlib import Path

from RecordingImportSmoke import write_minimal_bdf


class LiveMCP:
    def __init__(self, port: int = 8095) -> None:
        self.connection = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        self.request_id = 0

    def request(self, method: str, params: dict | None = None, notification: bool = False) -> dict:
        self.request_id += 1
        payload: dict = {"jsonrpc": "2.0", "method": method}
        if not notification:
            payload["id"] = self.request_id
        if params is not None:
            payload["params"] = params
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.connection.request("POST", "/mcp", body=body, headers={"Content-Type": "application/json", "Accept": "application/json"})
        response = self.connection.getresponse()
        data = response.read()
        if notification:
            if response.status != 202:
                raise RuntimeError(f"{method}: expected HTTP 202, got {response.status}")
            return {}
        if response.status != 200:
            raise RuntimeError(f"{method}: HTTP {response.status}: {data[:500]!r}")
        envelope = json.loads(data)
        if envelope.get("id") != self.request_id:
            raise RuntimeError(f"{method}: JSON-RPC response id mismatch")
        if "error" in envelope:
            raise RuntimeError(f"{method}: JSON-RPC error: {envelope['error']}")
        return envelope["result"]

    def call_raw(self, name: str, arguments: dict | None = None) -> dict:
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})

    def call(self, name: str, arguments: dict | None = None) -> dict:
        result = self.call_raw(name, arguments)
        structured = result.get("structuredContent", {})
        if result.get("isError"):
            error = structured.get("error", {})
            raise RuntimeError(f"{name}: {error.get('code', 'MCP_ERROR')}: {error.get('message', result.get('content'))}")
        return structured

    def close(self) -> None:
        self.connection.close()


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: LiveMCPImportSmoke.py <repo-root> <expected-client-exe>")
    root = Path(sys.argv[1]).resolve()
    expected_exe = Path(sys.argv[2]).resolve()
    client = LiveMCP()
    try:
        initialized = client.request("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {}, "resources": {"subscribe": False, "listChanged": False}},
            "clientInfo": {"name": "neurocontrol-live-import-smoke", "version": "1"},
        })
        if initialized.get("protocolVersion") != "2024-11-05":
            raise SystemExit(f"Unexpected MCP protocol version: {initialized.get('protocolVersion')}")
        client.request("notifications/initialized", notification=True)
        registry = client.request("tools/list").get("tools", [])
        tools = {item.get("name"): item for item in registry if isinstance(item, dict)}
        for name in ("neuro.list_recordings", "neuro.load_recording", "neuro.load_model", "neuro.check_model_compatibility"):
            if name not in tools:
                raise SystemExit(f"The live MCP tools/list lacks {name}")
        load_schema = tools["neuro.load_recording"].get("inputSchema", {}).get("properties", {})
        if not {"recording_id", "path"}.issubset(load_schema):
            raise SystemExit("Live MCP schema does not expose both safe selectors")

        status = client.call("neuro.get_status")
        running_exe = Path(status.get("client_build", {}).get("executable_path", "")).resolve()
        if running_exe != expected_exe:
            raise SystemExit(f"MCP is served by {running_exe}, expected built client {expected_exe}")
        if status.get("playback_running") or status.get("avatar_control_enabled"):
            raise SystemExit("Refusing import test because playback or avatar control is active")

        catalog = client.call("neuro.list_recordings")
        recordings = catalog.get("recordings", [])
        ids = {item.get("recording_id") for item in recordings}
        required_ids = {"S001R14", "S01_walk6min", "S01_stop6min"}
        if not required_ids.issubset(ids):
            raise SystemExit(f"Live recording catalog is missing preserved IDs: {sorted(required_ids - ids)}")
        model_list = client.call("neuro.list_models")
        model_ids = {item.get("model_id") for item in model_list.get("models", [])}
        required_models = {"S001_feet_vs_rest", "S01_walk_vs_stop"}
        if not required_models.issubset(model_ids):
            raise SystemExit(f"Live model catalog is missing: {sorted(required_models - model_ids)}")

        model = client.call("neuro.load_model", {"model_id": "S001_feet_vs_rest"})
        if not model.get("loaded"):
            raise SystemExit("PhysioNet model did not load through live MCP")
        imported = client.call("neuro.load_recording", {"recording_id": "S001R14"})
        if imported.get("state") != "preparing" or not imported.get("job_id"):
            raise SystemExit(f"PhysioNet EDF did not enter the async importer: {imported}")
        deadline = time.monotonic() + 120
        job = {}
        while time.monotonic() < deadline:
            job = client.call("neuro.get_training_status")
            if job.get("job_id") == imported["job_id"] and not job.get("running"):
                break
            time.sleep(0.5)
        if job.get("job_id") != imported["job_id"] or job.get("state") != "succeeded" or not job.get("success"):
            raise SystemExit(f"PhysioNet import did not finish successfully: {job}")
        compatibility = client.call("neuro.check_model_compatibility", {"model_id": "S001_feet_vs_rest"})
        if compatibility.get("compatible") is not True:
            raise SystemExit(f"PhysioNet model compatibility failed: {compatibility}")
        print("Live MCP: S001_feet_vs_rest + raw S001R14 EDF imported and compatible")

        model = client.call("neuro.load_model", {"model_id": "S01_walk_vs_stop"})
        if not model.get("loaded"):
            raise SystemExit("WALK/STOP model did not load through live MCP")
        nested_walk = next((
            item for item in recordings
            if str(item.get("recording_id", "")).startswith("raw:")
            and "task-walk6min_eeg" in str(item.get("file_name", "")).lower()
        ), None)
        if nested_walk is None:
            raise SystemExit("Live MCP catalog lacks the nested OpenNeuro walk6min EDF")
        raw_walk = client.call("neuro.load_recording", {"recording_id": nested_walk["recording_id"]})
        if raw_walk.get("state") != "preparing" or not raw_walk.get("job_id"):
            raise SystemExit(f"Nested OpenNeuro EDF did not enter the async importer: {raw_walk}")
        deadline = time.monotonic() + 120
        job = {}
        while time.monotonic() < deadline:
            job = client.call("neuro.get_training_status")
            if job.get("job_id") == raw_walk["job_id"] and not job.get("running"):
                break
            time.sleep(0.5)
        if job.get("job_id") != raw_walk["job_id"] or job.get("state") != "succeeded" or not job.get("success"):
            raise SystemExit(f"Nested OpenNeuro EDF import did not finish successfully: {job}")
        compatibility = client.call("neuro.check_model_compatibility", {"model_id": "S01_walk_vs_stop"})
        if compatibility.get("compatible") is not True:
            raise SystemExit(f"Nested OpenNeuro model compatibility failed: {compatibility}")
        print("Live MCP: nested OpenNeuro walk6min EDF found by generic ID, imported and compatible")

        walk = client.call("neuro.load_recording", {"recording_id": "S01_walk6min"})
        if not walk.get("loaded") or walk.get("playback_started"):
            raise SystemExit(f"OpenNeuro walk ID import failed: {walk}")
        compatibility = client.call("neuro.check_model_compatibility", {"model_id": "S01_walk_vs_stop"})
        if compatibility.get("compatible") is not True:
            raise SystemExit(f"WALK model compatibility failed: {compatibility}")
        print("Live MCP: S01_walk_vs_stop + S01_walk6min ID loaded and compatible")

        stop_f32 = root / "neurocontrol" / "datasets" / "processed" / "S01_stop6min.f32"
        stop = client.call("neuro.load_recording", {"path": str(stop_f32)})
        if not stop.get("loaded") or stop.get("playback_started"):
            raise SystemExit(f"OpenNeuro F32 path selector failed: {stop}")
        compatibility = client.call("neuro.check_model_compatibility", {"model_id": "S01_walk_vs_stop"})
        if compatibility.get("compatible") is not True:
            raise SystemExit(f"STOP model compatibility failed: {compatibility}")
        print("Live MCP: S01_walk_vs_stop + S01_stop6min F32 full-path selector loaded and compatible")

        raw_root = root / "neurocontrol" / "datasets" / "raw"
        imported_root = (root / "neurocontrol" / "datasets" / "processed" / "imported").resolve()
        fixture_name = f"mcp_bdf_smoke_{uuid.uuid4().hex}"
        output_pair: tuple[Path, Path] | None = None
        with tempfile.TemporaryDirectory(prefix="mcp-bdf-smoke-", dir=raw_root) as fixture_directory:
            fixture_path = Path(fixture_directory) / f"{fixture_name}.bdf"
            write_minimal_bdf(fixture_path)
            try:
                bdf = client.call("neuro.load_recording", {"path": str(fixture_path)})
                if bdf.get("state") != "preparing" or not bdf.get("job_id"):
                    raise SystemExit(f"BDF path did not enter the async importer: {bdf}")
                deadline = time.monotonic() + 120
                job = {}
                while time.monotonic() < deadline:
                    job = client.call("neuro.get_training_status")
                    if job.get("job_id") == bdf["job_id"] and not job.get("running"):
                        break
                    time.sleep(0.5)
                if job.get("job_id") != bdf["job_id"] or job.get("state") != "succeeded" or not job.get("success"):
                    raise SystemExit(f"BDF MCP import did not finish successfully: {job}")
                bdf_info = client.call("neuro.get_recording_info")
                bdf_metadata = bdf_info.get("metadata", {})
                bdf_metadata_path = Path(bdf_info.get("metadata_path", "")).resolve()
                bdf_data_name = bdf_metadata.get("data_file", "")
                if (bdf_metadata.get("source_format") != "BDF" or bdf_metadata.get("sample_rate_hz") != 160
                        or len(bdf_metadata.get("channels", [])) != 2):
                    raise SystemExit(f"BDF MCP metadata is invalid: {bdf_metadata}")
                bdf_data_path = bdf_metadata_path.parent / bdf_data_name
                if (bdf_metadata_path.parent != imported_root or not bdf_metadata_path.name.startswith(f"{fixture_name}_")
                        or Path(bdf_data_name).name != bdf_data_name or bdf_data_path.parent != imported_root):
                    raise SystemExit("Refusing to remove an unexpected BDF smoke output path")
                output_pair = (bdf_metadata_path, bdf_data_path)
                compatibility = client.call("neuro.check_model_compatibility", {"model_id": "S01_walk_vs_stop"})
                if compatibility.get("compatible") is not False:
                    raise SystemExit(f"Two-channel BDF fixture should fail exact 17-channel model compatibility: {compatibility}")
                print("Live MCP: 24-bit BDF path imported with provenance; incompatible montage correctly rejected")
            finally:
                restored = client.call("neuro.load_recording", {"recording_id": "S01_stop6min"})
                if not restored.get("loaded") or restored.get("playback_started"):
                    raise SystemExit(f"Could not restore prepared STOP recording after BDF smoke: {restored}")
                if output_pair:
                    for output_path in output_pair:
                        if output_path.exists():
                            output_path.unlink()

        blocked = client.call_raw("neuro.get_recording_info", {"path": r"C:\Windows\win.ini"})
        error = blocked.get("structuredContent", {}).get("error", {})
        if not blocked.get("isError") or error.get("code") != "RECORDING_NOT_FOUND_OR_PATH_NOT_ALLOWED":
            raise SystemExit(f"MCP did not reject a path outside the allowed data roots: {blocked}")
        final_status = client.call("neuro.get_status")
        if final_status.get("playback_running") or final_status.get("avatar_control_enabled"):
            raise SystemExit("Import smoke unexpectedly started playback or avatar control")
        print("Live MCP: path outside datasets/raw/processed rejected; playback=False; avatar_control=False")
    finally:
        client.close()


if __name__ == "__main__":
    main()
