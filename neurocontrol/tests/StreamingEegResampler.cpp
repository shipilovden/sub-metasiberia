#include "../processing/StreamingEegResampler.h"
#include "../processing/LslShadowPipeline.h"
#include "../processing/LslAuditReportWriter.h"
#include "../processing/NeuroClassifier.h"
#include "../acquisition/LslRecordingFile.h"
#include <json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const char* message)
{
	if(condition) return true;
	std::cerr << message << '\n';
	return false;
}

struct ScopedDirectory
{
	std::filesystem::path path;
	~ScopedDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

bool testIdentityAndRatio()
{
	std::string error;
	neurocontrol::StreamingEegResampler identity;
	if(!require(identity.configure(160, 160, 2, error), "identity configuration failed")) return false;
	const float pair[] = {0.25f, -0.5f};
	std::vector<std::vector<float>> output;
	if(!require(identity.pushSample(pair, 2, output, error), "identity push failed")) return false;
	if(!require(output.size() == 1 && output[0][0] == pair[0] && output[0][1] == pair[1], "identity changed sample values")) return false;

	neurocontrol::StreamingEegResampler converted;
	if(!require(converted.configure(500, 160, 1, error), "500-to-160 configuration failed")) return false;
	return require(converted.upFactor() == 8 && converted.downFactor() == 25 && converted.filterLength() == 251 &&
		std::abs(converted.groupDelayMilliseconds() - 31.25) < 1e-9,
		"500-to-160 ratio, FIR length, or group delay is incorrect");
}

bool resampleSine(double frequency_hz, double& rms, std::size_t& output_count)
{
	std::string error;
	neurocontrol::StreamingEegResampler resampler;
	if(!resampler.configure(500, 160, 1, error)) { std::cerr << error << '\n'; return false; }
	std::vector<std::vector<float>> output;
	std::vector<float> result;
	constexpr int input_count = 2500;
	for(int i = 0; i < input_count; ++i)
	{
		const float sample = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * frequency_hz * i / 500.0));
		if(!resampler.pushSample(&sample, 1, output, error)) { std::cerr << error << '\n'; return false; }
		for(const auto& item : output) result.push_back(item[0]);
	}
	output_count = result.size();
	if(result.size() < 300) return false;
	const std::size_t first = result.size() - 300;
	double power = 0.0;
	for(std::size_t i = first; i < result.size(); ++i) power += static_cast<double>(result[i]) * result[i];
	rms = std::sqrt(power / 300.0);
	return true;
}

bool testPassbandAndAntiAlias()
{
	double passband_rms = 0.0, rejected_rms = 0.0;
	std::size_t passband_count = 0, rejected_count = 0;
	if(!require(resampleSine(10.0, passband_rms, passband_count), "passband signal failed to resample")) return false;
	if(!require(resampleSine(120.0, rejected_rms, rejected_count), "stopband signal failed to resample")) return false;
	if(!require(passband_count == 790 && rejected_count == passband_count, "500-to-160 output cadence/count is incorrect")) return false;
	if(!require(passband_rms > 0.68 && passband_rms < 0.73, "10 Hz passband amplitude is outside tolerance")) return false;
	return require(rejected_rms < 0.01, "120 Hz content was not sufficiently attenuated before 160 Hz decimation");
}

bool testWarmupResetAndInvalidSamples()
{
	std::string error;
	neurocontrol::StreamingEegResampler resampler;
	if(!require(resampler.configure(500, 160, 1, error), "reset test configuration failed")) return false;
	std::vector<std::vector<float>> output;
	const float sample = 1.0f;
	for(int i = 0; i < 32; ++i)
		if(!require(resampler.pushSample(&sample, 1, output, error), "warmup push failed")) return false;
	if(!require(output.empty(), "FIR emitted a startup transient before its full support was available")) return false;
	if(!require(resampler.pushSample(&sample, 1, output, error) && output.size() == 1, "FIR did not emit after its startup warmup")) return false;
	resampler.reset();
	for(int i = 0; i < 32; ++i)
		if(!require(resampler.pushSample(&sample, 1, output, error) && output.empty(), "reset did not repeat FIR warmup")) return false;
	const float invalid = std::numeric_limits<float>::quiet_NaN();
	if(!require(!resampler.pushSample(&invalid, 1, output, error) && !error.empty(), "non-finite input was not rejected")) return false;
	return require(resampler.pushSample(&sample, 1, output, error) && output.empty(), "invalid input did not reset FIR warmup state");
}

bool testRejectedConfiguration()
{
	std::string error;
	neurocontrol::StreamingEegResampler resampler;
	if(!require(!resampler.configure(0, 160, 1, error) && !error.empty(), "irregular/zero input rate was not rejected")) return false;
	const float sample = 0.0f;
	std::vector<std::vector<float>> output;
	return require(!resampler.pushSample(&sample, 1, output, error), "unconfigured resampler accepted a sample");
}

bool testResampledSignalFeedsClassifier()
{
	neurocontrol::Model model;
	model.sample_rate_hz = 160;
	model.window_samples = 32;
	model.step_samples = 32;
	model.channels = {"C3"};
	model.sos = {{1.0, 0.0, 0.0, 1.0, 0.0, 0.0}};
	model.csp_filters = {{1.0}};
	model.lda_coefficients = {0.0};
	model.lda_intercept = 2.0;
	model.forward_threshold = 0.75;
	neurocontrol::NeuroClassifier classifier(model);
	neurocontrol::StreamingEegResampler resampler;
	std::string error;
	if(!require(resampler.configure(500, model.sample_rate_hz, 1, error), "pipeline resampler configuration failed")) return false;
	std::vector<std::vector<float>> output;
	std::size_t available_windows = 0;
	bool saw_unconfirmed_candidate = false;
	bool saw_confirmed_classifier_command = false;
	for(int i = 0; i < 500 * 5; ++i)
	{
		const float sample = static_cast<float>(1e-5 * std::sin(2.0 * 3.14159265358979323846 * 10.0 * i / 500.0));
		if(!resampler.pushSample(&sample, 1, output, error)) { std::cerr << error << '\n'; return false; }
		for(const auto& converted : output)
		{
			const auto result = classifier.pushSample(converted.data(), converted.size());
			if(!result.available) continue;
			++available_windows;
			if(result.awaiting_confirmation) saw_unconfirmed_candidate = true;
			if(result.command == neurocontrol::CommandType::Forward) saw_confirmed_classifier_command = true;
		}
	}
	return require(available_windows >= 2 && saw_unconfirmed_candidate && saw_confirmed_classifier_command,
		"synthetic 500-to-160 Hz stream did not exercise classifier window/confirmation logic");
}

bool testCompatibilityAndArbitraryChannelMapping()
{
	neurocontrol::LslShadowCompatibilityRequest request;
	request.advertised_rate_hz = 500.0;
	request.model_rate_hz = 160;
	request.receive_rate_windows_hz = {499.0, 500.0, 501.0, 500.0, 499.5};
	request.source_channels = {{"ECG", "ECG", "µV"}, {"C3", "EEG", "µV"}, {"AUX", "AUX", "V"}, {"C4", "EEG", "µV"}};
	request.model_channels = {"C4", "C3"};
	request.source_montage = request.model_montage = "Monopolar 19";
	request.source_reference = request.model_reference = "A1, A2";
	auto compatible = neurocontrol::evaluateLslShadowCompatibility(request);
	if(!require(compatible.compatible && compatible.channel_indices == std::vector<std::size_t>{3, 1},
		"compatibility failed to map a model's arbitrary channel order from a larger source set")) return false;
	request.receive_rate_windows_hz = {479.0, 480.0, 479.5, 479.2, 479.3};
	auto rate_blocked = neurocontrol::evaluateLslShadowCompatibility(request);
	if(!require(!rate_blocked.compatible && rate_blocked.resampling_supported &&
		std::find(rate_blocked.reason_codes.begin(), rate_blocked.reason_codes.end(), "receive_rate_outside_tolerance") != rate_blocked.reason_codes.end(),
		"out-of-tolerance receive rate did not block classification independently of resampler support")) return false;
	request.receive_rate_windows_hz = {500.0, 500.0, 500.0, 500.0, 500.0};
	request.source_montage.clear();
	request.source_reference.clear();
	auto unconfirmed = neurocontrol::evaluateLslShadowCompatibility(request);
	if(!require(!unconfirmed.compatible && unconfirmed.manual_montage_reference_confirmation_required,
		"unknown source montage/reference was not blocked pending operator confirmation")) return false;
	request.operator_confirmed_missing_montage_reference = true;
	auto confirmed = neurocontrol::evaluateLslShadowCompatibility(request);
	if(!require(confirmed.compatible && confirmed.montage_reference_state == "missing_metadata_operator_confirmed",
		"explicit confirmation did not allow the otherwise compatible unknown contract")) return false;
	request.source_montage = "10-20";
	auto mismatch = neurocontrol::evaluateLslShadowCompatibility(request);
	return require(!mismatch.compatible && mismatch.montage_reference_state == "mismatch",
		"operator confirmation improperly overrode a known montage mismatch");
}

bool testSharedPipelineChunkingAndResets()
{
	constexpr std::size_t source_channels = 5;
	constexpr int input_rate = 500;
	constexpr int count = 5000;
	const std::vector<std::size_t> selected{3, 1};
	std::vector<neurocontrol::EegFrame> frames(static_cast<std::size_t>(count));
	for(int i = 0; i < count; ++i)
	{
		auto& frame = frames[static_cast<std::size_t>(i)];
		frame.signal_good = true;
		frame.display_timestamp_seconds = static_cast<double>(i) / input_rate;
		frame.volts.resize(source_channels, 0.0f);
		frame.volts[1] = static_cast<float>(1e-5 * std::sin(2.0 * 3.14159265358979323846 * 10.0 * i / input_rate));
		frame.volts[3] = static_cast<float>(1.7e-5 * std::cos(2.0 * 3.14159265358979323846 * 7.0 * i / input_rate));
	}
	std::string error;
	neurocontrol::LslShadowPipeline whole, chunked;
	if(!require(whole.configure(input_rate, 160, source_channels, selected, nullptr, error), "whole-chunk shared pipeline configuration failed")) return false;
	if(!require(chunked.configure(input_rate, 160, source_channels, selected, nullptr, error), "split-chunk shared pipeline configuration failed")) return false;
	neurocontrol::LslShadowPipelineBatch whole_batch;
	if(!require(whole.processChunk(frames.data(), frames.size(), whole_batch, error), "whole-chunk shared pipeline processing failed")) return false;
	std::vector<std::vector<float>> chunked_output;
	const std::array<std::size_t, 8> chunk_sizes{{1, 17, 256, 3, 1024, 89, 511, 2048}};
	std::size_t offset = 0, chunk_index = 0;
	while(offset < frames.size())
	{
		const std::size_t size = std::min(chunk_sizes[chunk_index++ % chunk_sizes.size()], frames.size() - offset);
		neurocontrol::LslShadowPipelineBatch batch;
		if(!require(chunked.processChunk(frames.data() + offset, size, batch, error), "variable-size shared pipeline chunk failed")) return false;
		chunked_output.insert(chunked_output.end(), batch.resampled_samples.begin(), batch.resampled_samples.end());
		offset += size;
	}
	if(!require(whole_batch.resampled_samples.size() == chunked_output.size(), "resampler output count changed with chunk boundaries")) return false;
	for(std::size_t i = 0; i < chunked_output.size(); ++i)
		for(std::size_t channel = 0; channel < selected.size(); ++channel)
			if(!require(std::abs(whole_batch.resampled_samples[i][channel] - chunked_output[i][channel]) < 1e-7f,
				"resampler values changed with chunk boundaries")) return false;
	if(!require(chunked.matches(input_rate, 160, source_channels, selected, false), "pipeline configuration match check failed")) return false;
	chunked.reset(neurocontrol::LslShadowResetReason::QueueOverflow);
	chunked.reset(neurocontrol::LslShadowResetReason::ConnectionLost);
	chunked.reset(neurocontrol::LslShadowResetReason::Reconnected);
	chunked.reset(neurocontrol::LslShadowResetReason::Manual);
	auto after_reset = frames.front();
	after_reset.display_timestamp_seconds = 1.0;
	neurocontrol::LslShadowPipelineBatch reset_batch;
	if(!require(chunked.processChunk(&after_reset, 1, reset_batch, error), "pipeline failed after reconnect/reset")) return false;
	const auto& reset_stats = chunked.stats();
	if(!require(reset_stats.queue_overflow_resets == 1 && reset_stats.connection_loss_resets == 1 &&
		reset_stats.reconnect_resets == 1 && reset_stats.manual_resets == 1, "reset causes were not retained in diagnostics")) return false;
	auto gap_frames = std::array<neurocontrol::EegFrame, 2>{frames[0], frames[1]};
	gap_frames[0].display_timestamp_seconds = 1.002;
	gap_frames[1].display_timestamp_seconds = 2.002;
	if(!require(chunked.processChunk(gap_frames.data(), gap_frames.size(), reset_batch, error), "pipeline gap recovery failed")) return false;
	if(!require(chunked.stats().timestamp_gap_resets == 1, "timestamp discontinuity did not reset stream state")) return false;
	gap_frames[1].signal_good = false;
	if(!require(!chunked.processChunk(gap_frames.data() + 1, 1, reset_batch, error) && chunked.stats().invalid_frame_resets == 1,
		"invalid frame did not reset the pipeline and increment its error counter")) return false;
	return true;
}

bool testOfflineAuditNoModelAndReportSave()
{
	const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
	ScopedDirectory temp{std::filesystem::temp_directory_path() / ("metasiberia_lsl_audit_" + std::to_string(nonce))};
	std::error_code ec;
	std::filesystem::create_directories(temp.path, ec);
	if(!require(!ec, "offline audit test could not create its temporary directory")) return false;
	const auto recording_path = temp.path / "offline.nlsr";
	std::string error;
	constexpr std::uint64_t input_count = 17860;
	{
		neurocontrol::LslRecordingWriter writer;
		if(!require(writer.open(temp.path / "offline.part", 20, error), "offline audit fixture writer could not open")) return false;
		for(std::uint64_t offset = 0; offset < input_count; offset += 257)
		{
			const auto count = std::min<std::uint64_t>(257, input_count - offset);
			std::vector<neurocontrol::EegFrame> frames;
			frames.reserve(static_cast<std::size_t>(count));
			for(std::uint64_t i = 0; i < count; ++i)
			{
				const auto sample_index = offset + i;
				neurocontrol::EegFrame frame;
				frame.raw_source_values_exact = true;
				frame.raw_source_values.resize(20);
				for(std::size_t channel = 0; channel < 19; ++channel)
					frame.raw_source_values[channel] = static_cast<float>(25.0 * std::sin(2.0 * 3.14159265358979323846 * (8.0 + channel) * sample_index / 500.0));
				frame.raw_source_values[19] = 0.0f; // ECG is deliberately not selected as EEG.
				frame.source_timestamp_seconds = 1790000000.0 + static_cast<double>(sample_index / 500);
				frame.arrival_monotonic_seconds = static_cast<double>(offset) / 500.0; // Repeated per recording chunk.
				frames.push_back(std::move(frame));
			}
			if(!require(writer.appendChunk(frames, error), "offline audit fixture append failed")) return false;
		}
		if(!require(writer.finalize(recording_path, error), "offline audit fixture finalize failed")) return false;
	}

	neurocontrol::LslRecordingReader reader;
	neurocontrol::LslRecordingInfo info;
	if(!require(reader.open(recording_path, info, error), "offline audit fixture did not open")) return false;
	if(!require(reader.verify(info, error) && info.integrity_verified, "offline audit fixture checksum was not verified")) return false;
	std::vector<std::size_t> eeg_channels;
	for(std::size_t i = 0; i < 19; ++i) eeg_channels.push_back(i);
	neurocontrol::LslShadowPipeline pipeline;
	if(!require(pipeline.configure(500, 160, 20, eeg_channels, nullptr, error), "offline audit without a model could not configure 500-to-160 resampling")) return false;
	std::uint64_t output_count = 0;
	for(std::uint64_t offset = 0; offset < info.sample_count; offset += 2048)
	{
		std::vector<neurocontrol::EegFrame> frames;
		const auto count = std::min<std::uint64_t>(2048, info.sample_count - offset);
		if(!require(reader.readRange(offset, count, frames, error), "offline audit fixture range read failed")) return false;
		for(std::size_t i = 0; i < frames.size(); ++i)
		{
			auto& frame = frames[i];
			frame.volts.assign(20, 0.0f);
			for(std::size_t channel : eeg_channels) frame.volts[channel] = frame.raw_source_values[channel] * 1e-6f;
			frame.signal_good = true;
			frame.display_timestamp_seconds = static_cast<double>(offset + i) / 500.0;
		}
		neurocontrol::LslShadowPipelineBatch batch;
		if(!require(pipeline.processChunk(frames.data(), frames.size(), batch, error), "offline audit shared pipeline rejected a fixture chunk")) return false;
		output_count += batch.resampled_samples.size();
	}
	if(!require(info.sample_count == input_count && output_count == 5705 && !pipeline.hasClassifier(),
		"no-model offline audit did not produce 5,705 output frames without a classifier")) return false;

	neurocontrol::LslAuditReportSnapshot report;
	report.experiment_id = "S05";
	report.recording_id = "offline";
	report.audit_state = "completed";
	report.integrity_status = "verified";
	report.input_rate_hz = 500;
	report.target_rate_hz = 160;
	report.input_samples = input_count;
	report.output_samples = output_count;
	report.total_channels = 20;
	report.eeg_channels = 19;
	report.resample_up = pipeline.resampleUp();
	report.resample_down = pipeline.resampleDown();
	report.filter_taps = static_cast<int>(pipeline.filterLength());
	report.group_delay_ms = pipeline.groupDelayMilliseconds();
	report.nominal_duration_seconds = static_cast<double>(input_count) / 500.0;
	report.classifier_ran = false;
	report.avatar_command_sent = false;
	std::filesystem::path saved_path;
	if(!require(neurocontrol::writeLslAuditReport(temp.path, report, saved_path, error), "local aggregate report could not be saved")) return false;
	std::ifstream saved(saved_path, std::ios::binary);
	const std::string markdown((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
	if(!require(markdown.find("17860") != std::string::npos && markdown.find("5705") != std::string::npos &&
		markdown.find("сырые значения ЭЭГ") != std::string::npos && markdown.find("S05_lsl_audit") == std::string::npos,
		"saved local report did not contain expected aggregate values or privacy boundary")) return false;
	const std::string original = markdown;
	if(!require(!neurocontrol::writeLslAuditReport(temp.path, report, saved_path, error) &&
		error.find("will not be overwritten") != std::string::npos, "report writer overwrote an existing experiment report")) return false;
	std::ifstream after_file(temp.path / "neurocontrol" / "docs" / "experiments" / "S05_lsl_audit.md", std::ios::binary);
	const std::string after((std::istreambuf_iterator<char>(after_file)), std::istreambuf_iterator<char>());
	if(!require(after == original, "existing local experiment report changed after overwrite rejection")) return false;
	report.experiment_id = "../S06";
	return require(!neurocontrol::writeLslAuditReport(temp.path, report, saved_path, error),
		"report writer accepted an experiment ID containing a path traversal");
}

int auditSavedRecording(const std::filesystem::path& recording_path, int output_rate)
{
	const std::filesystem::path metadata_path = recording_path.parent_path() / (recording_path.stem().string() + ".json");
	std::ifstream metadata_file(metadata_path, std::ios::binary);
	if(!metadata_file) { std::cerr << "recording metadata sidecar is missing\n"; return 2; }
	nlohmann::json metadata;
	try { metadata_file >> metadata; }
	catch(const std::exception&) { std::cerr << "recording metadata is invalid JSON\n"; return 2; }
	if(metadata.value("format", std::string()) != "metasiberia-neurocontrol-lsl-recording-v1" ||
		metadata.value("status", std::string()) != "complete" || !metadata.value("complete", false) ||
		!metadata.value("raw_samples_preserved", false) ||
		metadata.value("data_file", std::string()) != recording_path.filename().string())
	{
		std::cerr << "recording sidecar does not describe a complete native LSL recording\n";
		return 2;
	}
	const int input_rate = metadata.value("nominal_rate_hz", 0.0) > 0.0 ?
		static_cast<int>(std::llround(metadata.value("nominal_rate_hz", 0.0))) : 0;
	if(output_rate < 8 || output_rate > 2000)
	{
		std::cerr << "target output rate must be between 8 and 2000 Hz\n";
		return 2;
	}
	const auto channels = metadata.value("channels", nlohmann::json::array());
	if(input_rate < 8 || input_rate > 4000 || channels.size() != metadata.value("channel_count", 0) || channels.empty())
	{
		std::cerr << "recording metadata has an unsupported rate or channel layout\n";
		return 2;
	}
	std::vector<std::size_t> eeg_indices;
	std::vector<double> unit_factors;
	for(std::size_t i = 0; i < channels.size(); ++i)
	{
		if(channels[i].value("type", std::string()) != "EEG") continue;
		const double factor = channels[i].value("unit_to_volts", 0.0);
		if(!std::isfinite(factor) || factor <= 0.0)
		{
			std::cerr << "an EEG channel has an invalid unit conversion\n";
			return 2;
		}
		eeg_indices.push_back(i);
		unit_factors.push_back(factor);
	}
	if(eeg_indices.empty()) { std::cerr << "recording has no EEG channels\n"; return 2; }

	neurocontrol::LslRecordingReader reader;
	neurocontrol::LslRecordingInfo info;
	std::string error;
	if(!reader.open(recording_path, info, error) || !info.complete || !reader.verify(info, error))
	{
		std::cerr << "recording integrity check failed: " << error << '\n';
		return 2;
	}
	if(info.channel_count != channels.size() ||
		info.sample_count != metadata.value("sample_count", std::uint64_t{0}))
	{
		std::cerr << "recording header and JSON sample/channel counts differ\n";
		return 2;
	}
	const std::string checksum_text = metadata.value("binary_checksum_fnv1a64", std::string());
	try
	{
		if(std::stoull(checksum_text, nullptr, 16) != info.checksum)
		{
			std::cerr << "recording header and JSON checksums differ\n";
			return 2;
		}
	}
	catch(const std::exception&)
	{
		std::cerr << "recording JSON checksum is malformed\n";
		return 2;
	}

	neurocontrol::LslShadowPipeline pipeline;
	if(!pipeline.configure(input_rate, output_rate, info.channel_count, eeg_indices, nullptr, error))
	{
		std::cerr << "recording resampler configuration failed: " << error << '\n';
		return 2;
	}
	std::set<double> source_timestamps, arrival_timestamps;
	std::vector<double> arrival_intervals;
	std::uint64_t input_frames = 0, output_frames = 0, input_nonfinite = 0, output_nonfinite = 0;
	std::uint64_t source_repeated = 0, source_backwards = 0, source_integer = 0;
	std::uint64_t arrival_backwards = 0, arrival_repeated = 0, arrival_gaps_over_one_second = 0;
	double previous_source = std::numeric_limits<double>::quiet_NaN();
	double previous_arrival = std::numeric_limits<double>::quiet_NaN();
	double first_arrival = std::numeric_limits<double>::quiet_NaN();
	double last_arrival = std::numeric_limits<double>::quiet_NaN();
	double input_power = 0.0, output_power = 0.0;
	const auto processing_start = std::chrono::steady_clock::now();
	for(std::uint64_t offset = 0; offset < info.sample_count; offset += 2048)
	{
		const std::uint64_t count = std::min<std::uint64_t>(2048, info.sample_count - offset);
		std::vector<neurocontrol::EegFrame> frames;
		if(!reader.readRange(offset, count, frames, error))
		{
			std::cerr << "recording frame read failed: " << error << '\n';
			return 2;
		}
		for(std::size_t frame_index = 0; frame_index < frames.size(); ++frame_index)
		{
		auto& frame = frames[frame_index];
			if(frame.raw_source_values.size() != info.channel_count || !frame.raw_source_values_exact)
			{
				std::cerr << "recording frame is not exact native float32 data\n";
				return 2;
			}
			if(std::isfinite(frame.source_timestamp_seconds))
			{
				if(std::floor(frame.source_timestamp_seconds) == frame.source_timestamp_seconds) ++source_integer;
				if(std::isfinite(previous_source))
				{
					if(frame.source_timestamp_seconds == previous_source) ++source_repeated;
					else if(frame.source_timestamp_seconds < previous_source) ++source_backwards;
				}
				source_timestamps.insert(frame.source_timestamp_seconds);
				previous_source = frame.source_timestamp_seconds;
			}
			if(!std::isfinite(frame.arrival_monotonic_seconds))
			{
				std::cerr << "recording contains a non-finite receiver timestamp\n";
				return 2;
			}
			if(!std::isfinite(first_arrival)) first_arrival = frame.arrival_monotonic_seconds;
			if(std::isfinite(previous_arrival))
			{
				if(frame.arrival_monotonic_seconds < previous_arrival) ++arrival_backwards;
				else if(frame.arrival_monotonic_seconds == previous_arrival) ++arrival_repeated;
				else
				{
					const double interval = frame.arrival_monotonic_seconds - previous_arrival;
					arrival_intervals.push_back(interval);
					if(interval > 1.0) ++arrival_gaps_over_one_second;
				}
			}
			arrival_timestamps.insert(frame.arrival_monotonic_seconds);
			previous_arrival = frame.arrival_monotonic_seconds;
			last_arrival = frame.arrival_monotonic_seconds;
			frame.volts.assign(info.channel_count, 0.0f);
			frame.signal_good = true;
			for(std::size_t i = 0; i < eeg_indices.size(); ++i)
			{
				const float raw = frame.raw_source_values[eeg_indices[i]];
				const double volts = static_cast<double>(raw) * unit_factors[i];
				if(!std::isfinite(raw) || !std::isfinite(volts) || std::abs(volts) > std::numeric_limits<float>::max())
				{
					++input_nonfinite;
					frame.signal_good = false;
				}
				else
				{
					frame.volts[eeg_indices[i]] = static_cast<float>(volts);
					input_power += volts * volts;
				}
			}
			if(input_nonfinite != 0)
			{
				std::cerr << "recording contains non-finite EEG input values\n";
				return 1;
			}
			frame.display_timestamp_seconds = static_cast<double>(offset + frame_index) / input_rate;
		}
		neurocontrol::LslShadowPipelineBatch batch;
		if(!pipeline.processChunk(frames.data(), frames.size(), batch, error))
		{
			std::cerr << "shared live/offline LSL pipeline rejected a recording chunk: " << error << '\n';
			return 1;
		}
		input_frames += frames.size();
		for(const auto& output : batch.resampled_samples)
		{
			++output_frames;
			for(float value : output)
			{
				if(!std::isfinite(value)) ++output_nonfinite;
				else output_power += static_cast<double>(value) * value;
			}
		}
	}
	const auto processing_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - processing_start).count();
	const std::uint64_t last_high_rate_index = (input_frames - 1) * pipeline.resampleUp();
	const std::uint64_t max_output_index = last_high_rate_index / pipeline.resampleDown();
	const std::uint64_t first_output_index = (pipeline.filterLength() - 1 + pipeline.resampleDown() - 1) / pipeline.resampleDown();
	const std::uint64_t expected_output_frames = max_output_index >= first_output_index ?
		max_output_index - first_output_index + 1 : 0;
	if(input_frames != info.sample_count || output_frames != expected_output_frames || output_nonfinite != 0)
	{
		std::cerr << "resampled sample count or output-value validation failed\n";
		return 1;
	}
	const double receiver_elapsed = std::isfinite(first_arrival) && std::isfinite(last_arrival) ? last_arrival - first_arrival : 0.0;
	const double input_rms = input_frames > 0 ? std::sqrt(input_power / (input_frames * eeg_indices.size())) : 0.0;
	const double output_rms = output_frames > 0 ? std::sqrt(output_power / (output_frames * eeg_indices.size())) : 0.0;
	double arrival_interval_min = 0.0, arrival_interval_median = 0.0, arrival_interval_max = 0.0;
	if(!arrival_intervals.empty())
	{
		std::sort(arrival_intervals.begin(), arrival_intervals.end());
		arrival_interval_min = arrival_intervals.front();
		arrival_interval_max = arrival_intervals.back();
		const std::size_t middle = arrival_intervals.size() / 2;
		arrival_interval_median = arrival_intervals.size() % 2 == 0 ?
			(arrival_intervals[middle - 1] + arrival_intervals[middle]) * 0.5 : arrival_intervals[middle];
	}
	std::cout << "integrity_verified=1\n"
		<< "channels_total=" << info.channel_count << " eeg_channels=" << eeg_indices.size() << " non_eeg_channels=" << info.channel_count - eeg_indices.size() << '\n'
		<< "input_samples=" << input_frames << " input_rate_nominal_hz=" << input_rate << '\n'
		<< "output_samples=" << output_frames << " output_rate_nominal_hz=" << output_rate << '\n'
		<< "ratio=" << pipeline.resampleUp() << '/' << pipeline.resampleDown()
		<< " fir_taps=" << pipeline.filterLength() << " group_delay_ms=" << pipeline.groupDelayMilliseconds() << '\n'
		<< "input_nominal_sample_duration_s=" << static_cast<double>(input_frames) / input_rate
		<< " output_sample_equivalent_duration_s=" << static_cast<double>(output_frames) / output_rate
		<< " receiver_arrival_elapsed_s=" << receiver_elapsed << '\n'
		<< "source_timestamp_unique=" << source_timestamps.size() << " repeated=" << source_repeated
		<< " integer_valued=" << source_integer << " backwards=" << source_backwards << '\n'
		<< "receiver_arrival_unique=" << arrival_timestamps.size() << " equal_consecutive=" << arrival_repeated
		<< " backwards=" << arrival_backwards << " gaps_over_1s=" << arrival_gaps_over_one_second
		<< " inter_arrival_s_min_median_max=" << arrival_interval_min << '/' << arrival_interval_median << '/' << arrival_interval_max << '\n'
		<< "input_nonfinite=" << input_nonfinite << " output_nonfinite=" << output_nonfinite
		<< " input_rms_volts=" << input_rms << " output_rms_volts=" << output_rms
		<< " processing_ms=" << processing_ms << '\n';
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	if((argc == 3 || argc == 4) && std::string(argv[1]) == "--audit-recording")
	{
		int output_rate = 160;
		if(argc == 4)
		{
			try { output_rate = std::stoi(argv[3]); }
			catch(const std::exception&) { std::cerr << "target output rate must be an integer\n"; return 2; }
		}
		return auditSavedRecording(std::filesystem::path(argv[2]), output_rate);
	}
	if(argc != 1)
	{
		std::cerr << "usage: neuro_lsl_streaming_resampler [--audit-recording PATH.nlsr [TARGET_HZ]]\n";
		return 2;
	}
	return testIdentityAndRatio() && testPassbandAndAntiAlias() &&
		testWarmupResetAndInvalidSamples() && testRejectedConfiguration() &&
		testResampledSignalFeedsClassifier() && testCompatibilityAndArbitraryChannelMapping() &&
		testSharedPipelineChunkingAndResets() && testOfflineAuditNoModelAndReportSave() ? 0 : 1;
}
