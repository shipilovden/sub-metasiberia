#!/usr/bin/env python3
"""Static contract check for the desktop NeuroControl MCP registry."""

import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"MCP contract check failed: {message}")


root = Path(sys.argv[1]).resolve()
registry_path = root / "gui_client" / "MCPClientHandler.cpp"
widget_path = root / "gui_client" / "NeuroControlWidget.cpp"
tools_doc_path = root / "neurocontrol" / "docs" / "mcp" / "NEUROCONTROL_MCP_TOOLS.md"
importer_path = root / "neurocontrol" / "tools" / "prepare_recording.py"

registry_source = registry_path.read_text(encoding="utf-8")
widget_source = widget_path.read_text(encoding="utf-8")
tools_doc = tools_doc_path.read_text(encoding="utf-8")
importer_source = importer_path.read_text(encoding="utf-8")

schema_region = registry_source.split("QJsonArray neuroControlToolSchemas()", 1)[1].split(
    "QJsonArray neuroKnowledgeResources()", 1
)[0]
schema_names = re.findall(r'a\.append\(neuroToolSchema\("(neuro\.[a-z0-9_.]+)"', schema_region)
if len(schema_names) != 47:
    fail(f"expected 47 schema declarations, found {len(schema_names)}")
if len(set(schema_names)) != len(schema_names):
    fail("schema registry contains duplicate names")

handler_region = widget_source.split("QJsonObject NeuroControlWidget::handleMCPRequest", 1)[1].split(
    "void NeuroControlWidget::appendDiagnostic", 1
)[0]
handler_names = set(re.findall(r'if\(tool == QStringLiteral\("(neuro\.[a-z0-9_.]+)"\)\)', handler_region))
if set(schema_names) != handler_names:
    missing = sorted(set(schema_names) - handler_names)
    unadvertised = sorted(handler_names - set(schema_names))
    fail(f"schema/handler mismatch; missing handlers={missing}; unadvertised handlers={unadvertised}")

if '"client_build", currentClientBuildInfo()' not in widget_source or '"executable_path"' not in widget_source or '"executable_modified_at_utc"' not in widget_source:
    fail("neuro.get_status must identify the running client build from the executable itself")
if "canonicalOpenNeuroRecordingId" in widget_source or "openNeuroMetadataPath" in widget_source:
    fail("recording discovery must not use a dataset-specific allowlist")
if "filesUnder(raw_dir" not in handler_region or "filesUnder(prepared_data_dir" not in handler_region:
    fail("MCP recording discovery must recurse through both bounded recording roots")
if "pathIsWithin(raw_dir, canonical)" not in handler_region or "pathIsWithin(prepared_data_dir, canonical)" not in handler_region:
    fail("path selectors must be confined to canonical raw/processed roots")
if "preparedRecordingInfo" not in handler_region or "actual_sha.compare(expected_sha, Qt::CaseInsensitive)" not in handler_region:
    fail("prepared F32 loads must validate their JSON pair, dimensions and SHA-256")
if 'arguments.value(QStringLiteral("path"))' not in handler_region or 'arguments.value(QStringLiteral("recording_id"))' not in handler_region:
    fail("recordings must be selectable by bounded full path or catalog ID")
if 'prepareAndLoadRecording(selected.path)' not in handler_region or 'loadRecordingPath(selected.metadata_path)' not in handler_region:
    fail("raw EDF/BDF and prepared F32 must use the existing asynchronous importer and loader")
if 'read_raw_edf' not in importer_source or 'read_raw_bdf' not in importer_source or '"source_sha256"' not in importer_source:
    fail("the importer must support EDF and BDF and preserve source hashes")
if 'read_bids_event_sidecars' not in importer_source or '"transformations"' not in importer_source:
    fail("the importer must preserve available event sidecars and record every conversion")
if '"path"' not in registry_source.split('neuroToolSchema("neuro.load_recording"', 1)[1].split("));", 1)[0]:
    fail("neuro.load_recording schema must advertise the bounded path selector")
if "EDF/EDF+" not in tools_doc or "BDF" not in tools_doc or "datasets/processed" not in tools_doc:
    fail("the MCP guide must document the generic formats and bounded data roots")
for prediction_field in ('"candidate_command"', '"awaiting_confirmation"', '"consecutive_candidate_windows"', '"class_command"'):
    if prediction_field not in widget_source:
        fail(f"neuro.get_prediction must distinguish candidate from emitted command ({prediction_field})")

if '"additionalProperties", false' not in registry_source or '"type", "object"' not in registry_source:
    fail("tool inputs must use closed object schemas")
if 'startsWith(QStringLiteral("neuro."))' not in registry_source:
    fail("the local client must replace server-provided neuro.* schemas with its canonical registry")
if "_metasiberia_neurocontrol_tools" in registry_source:
    fail("the local NeuroControl registry must not be sent upstream to the server")
if "std::string response = forwardToServer(body);" not in registry_source:
    fail("tools/list should forward the original request before local schema composition")
if "spliceRenderViewIntoToolsList(response" not in registry_source:
    fail("the connected Qt bridge must compose the local tool registry into tools/list")
if 'if(uri.startsWith(prefix) && neuro_control_callback)' not in registry_source:
    fail("the OFF build must not serve NeuroControl knowledge resources")
appserver_source = (root / "gui_client" / "CodexAppServerClient.cpp").read_text(encoding="utf-8")
neuro_instruction_prefix = appserver_source.split('" For EEG/BCI/NeuroControl requests', 1)[0]
if neuro_instruction_prefix.rfind('#if defined(ENABLE_NEUROCONTROL)') < neuro_instruction_prefix.rfind('#endif'):
    fail("the OFF build must not instruct the embedded AI to use neuro.*")
stop_region = widget_source.split("void NeuroControlWidget::stop()", 1)[1].split("void NeuroControlWidget::tick()", 1)[0]
if 'if(lsl_recording_playback_) { stopLslRecordingPlayback(); return; }' in stop_region or 'currentIndex() == 4 && !playing_) return' in stop_region:
    fail("emergency stop must not return before clearing controller state")
if 'client_->setNeuroControlEnabled(false)' not in stop_region or 'publishStop()' not in stop_region:
    fail("stop must disable the controller and issue STOP for every source")
if 'EMERGENCY_STOP_FAILED' not in handler_region:
    fail("neuro.emergency_stop must verify that the controller accepted STOP")
if 'lsl_cleanup_pending' not in handler_region:
    fail("neuro.emergency_stop must disclose when asynchronous LSL teardown is still pending")
lsl_status_region = widget_source.split("QJsonObject NeuroControlWidget::lslStatusJson() const", 1)[1].split("void NeuroControlWidget::startExperimentSession()", 1)[0]
for worker_field in ('reader_worker_exists', 'reader_worker_finished', 'reader_stop_requested', 'reader_cleanup_pending'):
    if worker_field not in lsl_status_region:
        fail(f"neuro.get_lsl_status must expose LSL reader lifecycle field {worker_field}")
for lsl_tool in (
    "neuro.configure_lsl",
    "neuro.discover_lsl_streams",
    "neuro.get_lsl_status",
    "neuro.get_lsl_timestamps",
    "neuro.connect_lsl_stream",
    "neuro.disconnect_lsl_stream",
    "neuro.run_lsl_diagnostic",
    "neuro.start_lsl_recording",
    "neuro.stop_lsl_recording",
    "neuro.get_lsl_recording_status",
    "neuro.list_lsl_recordings",
    "neuro.load_lsl_recording",
    "neuro.play_lsl_recording",
    "neuro.resume_playback",
    "neuro.stop_lsl_recording_playback",
    "neuro.seek_lsl_recording",
    "neuro.get_lsl_recording_timestamps",
    "neuro.configure_lsl_visualization",
):
    if lsl_tool not in schema_names:
        fail(f"the local MCP registry is missing {lsl_tool}")

visualization_schema = schema_region.split('neuroToolSchema("neuro.configure_lsl_visualization"', 1)[1].split("));", 1)[0]
if 'numberProperty("Вертикальный масштаб в микровольтах.", 5.0, 1000.0)' not in visualization_schema:
    fail("LSL visualization MCP scale limits must match the 5..1000 µV UI control")
if "зарегистрированы 47 инструментов" not in widget_source or "26 инструментов" in widget_source:
    fail("the embedded NeuroControl help must describe the current MCP tool count")
if "визуализации и диагностики" not in widget_source or "не подключается к классификатору" not in widget_source:
    fail("the embedded NeuroControl help must state that LSL is isolated from classification and avatar control")

expected_docs = {
    "NEUROCONTROL_OVERVIEW.md",
    "NEUROCONTROL_ARCHITECTURE.md",
    "NEUROCONTROL_GLOSSARY_RU.md",
    "NEUROCONTROL_DATASETS.md",
    "NEUROCONTROL_MODELS.md",
    "NEUROCONTROL_MCP_TOOLS.md",
    "NEUROCONTROL_EXPERIMENTS.md",
    "NEUROCONTROL_TROUBLESHOOTING.md",
    "NEUROCONTROL_MEDICAL_EEG.md",
    "NEUROCONTROL_AI_INSTRUCTIONS.md",
}
docs_dir = root / "neurocontrol" / "docs" / "mcp"
missing_docs = sorted(name for name in expected_docs if not (docs_dir / name).is_file())
if missing_docs:
    fail(f"knowledge files are missing: {missing_docs}")
if "47" not in tools_doc or "neuro.configure_lsl" not in tools_doc or "neuro.run_lsl_diagnostic" not in tools_doc or "neuro.start_lsl_recording" not in tools_doc:
    fail("the MCP tools guide does not document the current 47-tool registry and LSL recording workflow")

lsl_source = (root / "neurocontrol" / "acquisition" / "LslEegSource.cpp").read_text(encoding="utf-8")
if "discoverInstalledLabRecorderLibrary" not in lsl_source or "validateWindowsX64Library" not in lsl_source:
    fail("automatic LSL setup must discover and validate a Windows x64 LabRecorder library")
if "0x8664" not in lsl_source or "app-labrecorder" not in lsl_source.lower():
    fail("automatic DLL selection must validate AMD64 and the LabRecorder package marker")
if "source_->setCurrentIndex(index)" not in handler_region or "lsl_live" not in handler_region:
    fail("MCP source selection must use the live Qt source selector")
if "report_markdown" not in widget_source or "run_lsl_diagnostic" not in handler_region:
    fail("LSL diagnostic must expose its report through MCP")
for capability in (
    "raw_samples_preserved",
    "receiver_arrival_monotonic_seconds",
    "source_timestamp_ieee754_hex",
    "unit_to_volts",
    "lsl_recording_playback_",
    "classifier_connected",
    "avatar_control_enabled",
):
    if capability not in widget_source:
        fail(f"LSL recording/playback safety contract is missing {capability}")

print(f"MCP contracts OK: {len(schema_names)} unique schemas, matching handlers, 10 knowledge files")
