#include "LslShadowPipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace neurocontrol {
namespace {

std::string normalized(std::string value)
{
	const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
	const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
	if(first >= last) return {};
	value.assign(first, last);
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::string channelKind(const LslChannelContract& channel)
{
	const std::string type = normalized(channel.type);
	const std::string name = normalized(channel.name);
	if(type == "ecg" || type == "ekg" || name.find("ecg") != std::string::npos ||
		name.find("ekg") != std::string::npos || name.find("\xD0\xAD\xD0\x9A\xD0\x93") != std::string::npos ||
		name.find("\xD1\x8D\xD0\xBA\xD0\xB3") != std::string::npos)
		return "ecg";
	if(type.empty() || type == "eeg") return "eeg";
	return type;
}

double voltageScaleToVolts(const std::string& unit_value)
{
	const std::string unit = normalized(unit_value);
	if(unit == "v" || unit == "volt" || unit == "volts") return 1.0;
	if(unit == "mv" || unit == "millivolt" || unit == "millivolts") return 1e-3;
	if(unit == "uv" || unit == "µv" || unit == "μv" || unit == "мкв" || unit == "мкВ" || unit == "мкв" ||
		unit == "microvolt" || unit == "microvolts" ||
		unit == "micro-v" || unit == "microvolt(s)") return 1e-6;
	return 0.0;
}

bool isIntegerRate(double rate)
{
	return std::isfinite(rate) && rate >= 8.0 && rate <= 4000.0 &&
		std::abs(rate - std::round(rate)) <= 0.01;
}

} // namespace

LslShadowCompatibility evaluateLslShadowCompatibility(const LslShadowCompatibilityRequest& request)
{
	LslShadowCompatibility result;
	result.output_rate_hz = request.model_rate_hz;
	result.source_channel_count = request.source_channels.size();
	const auto block = [&result](const char* code, const char* reason) {
		result.reason_codes.emplace_back(code);
		result.reasons.emplace_back(reason);
	};
	if(!isIntegerRate(request.advertised_rate_hz))
		block("nominal_rate_not_integer", "The advertised LSL rate must be a supported integer rate.");
	else
		result.input_rate_hz = static_cast<int>(std::llround(request.advertised_rate_hz));
	if(request.model_rate_hz < 8 || request.model_rate_hz > 2000)
		block("model_rate_invalid", "The model rate is outside the supported resampler range.");
	if(result.input_rate_hz > 0 && request.model_rate_hz >= 8 && request.model_rate_hz <= 2000)
	{
		StreamingEegResampler probe;
		std::string error;
		result.resampling_supported = probe.configure(result.input_rate_hz, request.model_rate_hz,
			std::max<std::size_t>(1, request.model_channels.size()), error);
		if(!result.resampling_supported) block("resampler_unsupported", error.c_str());
	}

	if(request.model_channels.empty() || request.source_channels.empty())
		block("channel_lists_empty", "The source or model channel list is empty.");
	else
	{
		std::map<std::string, std::vector<std::size_t>> source_indices;
		for(std::size_t i = 0; i < request.source_channels.size(); ++i)
		{
			const std::string name = normalized(request.source_channels[i].name);
			if(!name.empty()) source_indices[name].push_back(i);
		}
		std::set<std::string> requested_names;
		for(const std::string& model_name_value : request.model_channels)
		{
			const std::string model_name = normalized(model_name_value);
			const auto found = source_indices.find(model_name);
			if(model_name.empty() || !requested_names.insert(model_name).second || found == source_indices.end() || found->second.size() != 1)
			{
				block("channel_unmatched", "A model channel is missing, duplicated, or ambiguous in the LSL channel list.");
				result.channel_indices.clear();
				break;
			}
			const std::size_t source_index = found->second.front();
			const LslChannelContract& source_channel = request.source_channels[source_index];
			if(channelKind(source_channel) != "eeg")
			{
				block("channel_not_eeg", "A mapped model channel is not identified as EEG.");
				result.channel_indices.clear();
				break;
			}
			if(voltageScaleToVolts(source_channel.unit) <= 0.0)
			{
				block("unit_unsupported", "A mapped EEG channel has an unsupported or unknown voltage unit.");
				result.channel_indices.clear();
				break;
			}
			result.channel_indices.push_back(source_index);
		}
	}

	if(request.receive_rate_windows_hz.size() < 5)
	{
		result.receive_rate_ready = false;
		block("receive_rate_pending", "Five one-second receive-rate measurements are required.");
	}
	else
	{
		result.receive_rate_ready = true;
		const bool valid_rates = std::all_of(request.receive_rate_windows_hz.begin(), request.receive_rate_windows_hz.end(),
			[](double rate) { return std::isfinite(rate) && rate > 0.0; });
		if(!valid_rates)
			block("receive_rate_invalid", "Receive-rate measurements contain invalid values.");
		else if(result.input_rate_hz > 0)
		{
			const double sum = std::accumulate(request.receive_rate_windows_hz.begin(), request.receive_rate_windows_hz.end(), 0.0);
			result.observed_receive_rate_hz = sum / request.receive_rate_windows_hz.size();
			const auto bounds = std::minmax_element(request.receive_rate_windows_hz.begin(), request.receive_rate_windows_hz.end());
			const double allowed_mean_error = std::max(2.0, result.input_rate_hz * 0.03);
			const double allowed_window_span = std::max(10.0, result.input_rate_hz * 0.12);
			result.receive_rate_stable = std::abs(result.observed_receive_rate_hz - request.advertised_rate_hz) <= allowed_mean_error &&
				(*bounds.second - *bounds.first) <= allowed_window_span;
			if(!result.receive_rate_stable)
				block("receive_rate_outside_tolerance", "Measured receive rate is outside the configured stability tolerance; shadow classification is blocked.");
		}
	}

	const std::string source_montage = normalized(request.source_montage);
	const std::string source_reference = normalized(request.source_reference);
	const std::string model_montage = normalized(request.model_montage);
	const std::string model_reference = normalized(request.model_reference);
	const bool montage_missing = source_montage.empty() || model_montage.empty();
	const bool reference_missing = source_reference.empty() || model_reference.empty();
	if(!montage_missing && source_montage != model_montage)
	{
		result.montage_reference_state = "mismatch";
		block("montage_mismatch", "Source and model electrode montages differ.");
	}
	else if(!reference_missing && source_reference != model_reference)
	{
		result.montage_reference_state = "mismatch";
		block("reference_mismatch", "Source and model reference schemes differ.");
	}
	else if(montage_missing || reference_missing)
	{
		result.manual_montage_reference_confirmation_required = true;
		if(request.operator_confirmed_missing_montage_reference)
			result.montage_reference_state = "missing_metadata_operator_confirmed";
		else
		{
			result.montage_reference_state = "missing_metadata_confirmation_required";
			block("electrode_contract_confirmation_required", "Montage or reference metadata is missing; explicit operator confirmation is required.");
		}
	}
	else result.montage_reference_state = "metadata_match";

	result.compatible = result.reasons.empty() && result.channel_indices.size() == request.model_channels.size() &&
		result.resampling_supported && result.receive_rate_ready && result.receive_rate_stable;
	return result;
}

bool LslShadowPipeline::configure(int input_rate_hz, int output_rate_hz, std::size_t source_channel_count,
	std::vector<std::size_t> selected_channel_indices, const Model* diagnostic_model, std::string& error)
{
	configured_ = false;
	classifier_.reset();
	selected_channel_indices_.clear();
	selected_sample_.clear();
	stats_ = {};
	error.clear();
	if(source_channel_count == 0 || source_channel_count > 256 || selected_channel_indices.empty() || selected_channel_indices.size() > 256)
	{
		error = "Invalid LSL shadow channel selection.";
		return false;
	}
	std::set<std::size_t> unique_indices;
	for(std::size_t index : selected_channel_indices)
		if(index >= source_channel_count || !unique_indices.insert(index).second)
		{
			error = "LSL shadow channel indices are out of range or duplicated.";
			return false;
		}
	if(diagnostic_model && (!diagnostic_model->valid() || diagnostic_model->sample_rate_hz != output_rate_hz ||
		diagnostic_model->channels.size() != selected_channel_indices.size()))
	{
		error = "Diagnostic model does not match the configured output rate and channel count.";
		return false;
	}
	if(!resampler_.configure(input_rate_hz, output_rate_hz, selected_channel_indices.size(), error)) return false;
	if(diagnostic_model) classifier_ = std::make_unique<NeuroClassifier>(*diagnostic_model);
	selected_channel_indices_ = std::move(selected_channel_indices);
	source_channel_count_ = source_channel_count;
	selected_sample_.resize(selected_channel_indices_.size());
	configured_ = true;
	resetState();
	return true;
}

bool LslShadowPipeline::matches(int input_rate_hz, int output_rate_hz, std::size_t source_channel_count,
	const std::vector<std::size_t>& selected_channel_indices, bool with_classifier) const
{
	return configured_ && inputRateHz() == input_rate_hz && outputRateHz() == output_rate_hz &&
		source_channel_count_ == source_channel_count && selected_channel_indices_ == selected_channel_indices &&
		static_cast<bool>(classifier_) == with_classifier;
}

void LslShadowPipeline::resetState()
{
	resampler_.reset();
	if(classifier_) classifier_->reset();
	have_previous_display_timestamp_ = false;
	previous_display_timestamp_seconds_ = 0.0;
}

void LslShadowPipeline::countReset(LslShadowResetReason reason)
{
	switch(reason)
	{
	case LslShadowResetReason::Manual: ++stats_.manual_resets; break;
	case LslShadowResetReason::TimestampGap: ++stats_.timestamp_gap_resets; break;
	case LslShadowResetReason::InvalidFrame: ++stats_.invalid_frame_resets; break;
	case LslShadowResetReason::QueueOverflow: ++stats_.queue_overflow_resets; break;
	case LslShadowResetReason::Stall: ++stats_.stall_resets; break;
	case LslShadowResetReason::ConnectionLost: ++stats_.connection_loss_resets; break;
	case LslShadowResetReason::Reconnected: ++stats_.reconnect_resets; break;
	}
	resetState();
}

void LslShadowPipeline::reset(LslShadowResetReason reason)
{
	countReset(reason);
}

bool LslShadowPipeline::processChunk(const EegFrame* frames, std::size_t frame_count,
	LslShadowPipelineBatch& output, std::string& error)
{
	output = {};
	error.clear();
	if(!configured_ || (!frames && frame_count != 0))
	{
		error = "LSL shadow pipeline is not configured or received a null chunk.";
		stats_.last_error = error;
		++stats_.errors;
		return false;
	}
	const auto start = std::chrono::steady_clock::now();
	bool all_frames_valid = true;
	for(std::size_t frame_index = 0; frame_index < frame_count; ++frame_index)
	{
		const EegFrame& frame = frames[frame_index];
		++stats_.input_samples;
		if(!frame.signal_good || frame.volts.size() != source_channel_count_ || !std::isfinite(frame.display_timestamp_seconds))
		{
			countReset(LslShadowResetReason::InvalidFrame);
			++stats_.errors;
			all_frames_valid = false;
			if(error.empty()) error = "LSL shadow input frame is invalid.";
			stats_.last_error = error;
			continue;
		}
		if(have_previous_display_timestamp_)
		{
			const double interval = frame.display_timestamp_seconds - previous_display_timestamp_seconds_;
			if(!std::isfinite(interval) || interval <= 0.0 || interval > 5.0 / inputRateHz())
				countReset(LslShadowResetReason::TimestampGap);
		}
		previous_display_timestamp_seconds_ = frame.display_timestamp_seconds;
		have_previous_display_timestamp_ = true;
		bool channels_valid = true;
		for(std::size_t channel = 0; channel < selected_channel_indices_.size(); ++channel)
		{
			const float value = frame.volts[selected_channel_indices_[channel]];
			if(!std::isfinite(value)) { channels_valid = false; break; }
			selected_sample_[channel] = value;
		}
		if(!channels_valid)
		{
			countReset(LslShadowResetReason::InvalidFrame);
			++stats_.errors;
			all_frames_valid = false;
			if(error.empty()) error = "LSL shadow frame contains a non-finite selected channel.";
			stats_.last_error = error;
			continue;
		}
		std::string resample_error;
		if(!resampler_.pushSample(selected_sample_.data(), selected_sample_.size(), resampled_sample_, resample_error))
		{
			countReset(LslShadowResetReason::InvalidFrame);
			++stats_.errors;
			all_frames_valid = false;
			if(error.empty()) error = resample_error;
			stats_.last_error = error;
			continue;
		}
		for(const auto& resampled : resampled_sample_)
		{
			output.resampled_samples.push_back(resampled);
			++stats_.output_samples;
			if(classifier_)
			{
				Classification classification = classifier_->pushSample(resampled.data(), resampled.size());
				if(classification.available)
				{
					++stats_.classifier_windows;
					output.classifications.push_back(classification);
				}
			}
		}
	}
	stats_.last_processing_batch_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return all_frames_valid;
}

} // namespace neurocontrol
