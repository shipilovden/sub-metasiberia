#pragma once

#include "../acquisition/EegSource.h"
#include "NeuroClassifier.h"
#include "StreamingEegResampler.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neurocontrol {

struct LslChannelContract
{
	std::string name;
	std::string type;
	std::string unit;
};

struct LslShadowCompatibilityRequest
{
	double advertised_rate_hz = 0.0;
	int model_rate_hz = 0;
	std::vector<double> receive_rate_windows_hz;
	std::vector<LslChannelContract> source_channels;
	std::vector<std::string> model_channels;
	std::string source_montage;
	std::string source_reference;
	std::string model_montage;
	std::string model_reference;
	bool operator_confirmed_missing_montage_reference = false;
};

struct LslShadowCompatibility
{
	bool compatible = false;
	bool resampling_supported = false;
	bool receive_rate_ready = false;
	bool receive_rate_stable = false;
	bool manual_montage_reference_confirmation_required = false;
	int input_rate_hz = 0;
	int output_rate_hz = 0;
	double observed_receive_rate_hz = 0.0;
	std::size_t source_channel_count = 0;
	std::vector<std::size_t> channel_indices;
	std::vector<std::string> reason_codes;
	std::vector<std::string> reasons;
	std::string montage_reference_state = "unknown";
};

LslShadowCompatibility evaluateLslShadowCompatibility(const LslShadowCompatibilityRequest& request);

enum class LslShadowResetReason
{
	Manual,
	TimestampGap,
	InvalidFrame,
	QueueOverflow,
	Stall,
	ConnectionLost,
	Reconnected
};

struct LslShadowPipelineStats
{
	std::uint64_t input_samples = 0;
	std::uint64_t output_samples = 0;
	std::uint64_t classifier_windows = 0;
	std::uint64_t errors = 0;
	std::uint64_t timestamp_gap_resets = 0;
	std::uint64_t invalid_frame_resets = 0;
	std::uint64_t queue_overflow_resets = 0;
	std::uint64_t stall_resets = 0;
	std::uint64_t connection_loss_resets = 0;
	std::uint64_t reconnect_resets = 0;
	std::uint64_t manual_resets = 0;
	double last_processing_batch_ms = 0.0;
	std::string last_error;
};

struct LslShadowPipelineBatch
{
	std::vector<std::vector<float>> resampled_samples;
	std::vector<Classification> classifications;
};

// Shared stream processor for live LSL and verified .nlsr playback/audits.
// It consumes already-unit-normalized EegFrame::volts and never emits avatar
// commands. The optional classifier is diagnostic only.
class LslShadowPipeline final
{
public:
	bool configure(int input_rate_hz, int output_rate_hz, std::size_t source_channel_count,
		std::vector<std::size_t> selected_channel_indices, const Model* diagnostic_model,
		std::string& error);
	void reset(LslShadowResetReason reason = LslShadowResetReason::Manual);
	bool processChunk(const EegFrame* frames, std::size_t frame_count,
		LslShadowPipelineBatch& output, std::string& error);

	bool isConfigured() const { return configured_; }
	bool hasClassifier() const { return static_cast<bool>(classifier_); }
	int inputRateHz() const { return resampler_.inputRateHz(); }
	int outputRateHz() const { return resampler_.outputRateHz(); }
	int resampleUp() const { return resampler_.upFactor(); }
	int resampleDown() const { return resampler_.downFactor(); }
	double groupDelayMilliseconds() const { return resampler_.groupDelayMilliseconds(); }
	std::size_t filterLength() const { return resampler_.filterLength(); }
	std::size_t channelCount() const { return selected_channel_indices_.size(); }
	std::size_t sourceChannelCount() const { return source_channel_count_; }
	const std::vector<std::size_t>& selectedChannelIndices() const { return selected_channel_indices_; }
	bool matches(int input_rate_hz, int output_rate_hz, std::size_t source_channel_count,
		const std::vector<std::size_t>& selected_channel_indices, bool with_classifier) const;
	const LslShadowPipelineStats& stats() const { return stats_; }

private:
	void resetState();
	void countReset(LslShadowResetReason reason);

	StreamingEegResampler resampler_;
	std::unique_ptr<NeuroClassifier> classifier_;
	std::vector<std::size_t> selected_channel_indices_;
	std::vector<float> selected_sample_;
	std::vector<std::vector<float>> resampled_sample_;
	std::size_t source_channel_count_ = 0;
	double previous_display_timestamp_seconds_ = 0.0;
	bool have_previous_display_timestamp_ = false;
	bool configured_ = false;
	LslShadowPipelineStats stats_;
};

} // namespace neurocontrol
