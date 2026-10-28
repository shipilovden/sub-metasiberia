#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace neurocontrol {

// Streaming rational-rate converter for regular, interleaved EEG samples.
// Downsampling uses a causal, linear-phase Blackman-windowed low-pass FIR
// before decimation. The initial FIR warm-up outputs are intentionally omitted.
class StreamingEegResampler {
public:
	bool configure(int input_rate_hz, int output_rate_hz, std::size_t channel_count, std::string& error);
	void reset();
	bool pushSample(const float* sample, std::size_t channels,
		std::vector<std::vector<float>>& output_samples, std::string& error);

	int inputRateHz() const { return input_rate_hz_; }
	int outputRateHz() const { return output_rate_hz_; }
	int upFactor() const { return up_; }
	int downFactor() const { return down_; }
	std::size_t channelCount() const { return channel_count_; }
	std::size_t filterLength() const { return taps_.size(); }
	double groupDelayMilliseconds() const;
	bool isIdentity() const { return up_ == 1 && down_ == 1; }

private:
	int input_rate_hz_ = 0;
	int output_rate_hz_ = 0;
	int up_ = 0;
	int down_ = 0;
	std::size_t channel_count_ = 0;
	std::vector<double> taps_;
	std::deque<std::vector<float>> history_;
	std::uint64_t history_start_index_ = 0;
	std::uint64_t input_count_ = 0;
	std::uint64_t next_output_index_ = 0;
};

} // namespace neurocontrol
