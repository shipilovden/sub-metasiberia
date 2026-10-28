#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace neurocontrol {

struct LslAuditReportSnapshot
{
	std::string experiment_id;
	std::string recording_id;
	std::string audit_state;
	std::string integrity_status;
	std::string integrity_reason;
	int input_rate_hz = 0;
	int target_rate_hz = 0;
	int model_rate_hz = 0;
	std::uint64_t input_samples = 0;
	std::uint64_t output_samples = 0;
	std::uint32_t total_channels = 0;
	std::uint32_t eeg_channels = 0;
	int resample_up = 0;
	int resample_down = 0;
	int filter_taps = 0;
	double group_delay_ms = 0.0;
	double processing_ms = 0.0;
	double nominal_duration_seconds = 0.0;
	std::uint64_t pipeline_errors = 0;
	std::uint64_t invalid_input_values = 0;
	std::uint64_t nonfinite_output_values = 0;
	std::uint64_t timestamp_gap_resets = 0;
	std::uint64_t invalid_frame_resets = 0;
	std::string model_compatibility_state;
	std::vector<std::string> model_compatibility_reasons;
	bool receive_rate_measurable_from_archive = false;
	bool classifier_ran = false;
	bool avatar_command_sent = false;
};

// Writes a generated aggregate-only report beneath <project>/neurocontrol/docs/experiments.
// The MCP caller never supplies a path or raw report body, and an existing SNN report is never overwritten.
bool writeLslAuditReport(const std::filesystem::path& project_root,
	const LslAuditReportSnapshot& snapshot, std::filesystem::path& output_path, std::string& error);

} // namespace neurocontrol
