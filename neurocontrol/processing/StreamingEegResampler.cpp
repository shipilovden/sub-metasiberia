#include "StreamingEegResampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace neurocontrol {
namespace {

int gcd(int a, int b)
{
	while(b != 0)
	{
		const int remainder = a % b;
		a = b;
		b = remainder;
	}
	return a;
}

double sinc(double value)
{
	if(std::abs(value) < 1e-12) return 1.0;
	const double radians = 3.14159265358979323846 * value;
	return std::sin(radians) / radians;
}

} // namespace

bool StreamingEegResampler::configure(int input_rate_hz, int output_rate_hz,
	std::size_t channel_count, std::string& error)
{
	reset();
	input_rate_hz_ = output_rate_hz_ = up_ = down_ = 0;
	channel_count_ = 0;
	taps_.clear();
	error.clear();
	if(input_rate_hz < 8 || input_rate_hz > 4000 || output_rate_hz < 8 || output_rate_hz > 2000 ||
		channel_count == 0 || channel_count > 256)
	{
		error = "EEG sample rates or channel count are outside the supported range.";
		return false;
	}
	const int divisor = gcd(input_rate_hz, output_rate_hz);
	const int up = output_rate_hz / divisor;
	const int down = input_rate_hz / divisor;
	const int maximum_factor = std::max(up, down);
	if(maximum_factor > 128)
	{
		error = "The rational EEG resampling ratio is too large for the bounded streaming FIR.";
		return false;
	}

	input_rate_hz_ = input_rate_hz;
	output_rate_hz_ = output_rate_hz;
	up_ = up;
	down_ = down;
	channel_count_ = channel_count;
	if(up_ == 1 && down_ == 1)
	{
		taps_.assign(1, 1.0);
		return true;
	}

	const std::size_t length = static_cast<std::size_t>(10 * maximum_factor + 1);
	const double centre = static_cast<double>(length - 1) * 0.5;
	const double cutoff = 1.0 / static_cast<double>(maximum_factor);
	taps_.resize(length);
	double sum = 0.0;
	for(std::size_t i = 0; i < length; ++i)
	{
		const double offset = static_cast<double>(i) - centre;
		const double ideal = cutoff * sinc(cutoff * offset);
		const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(i) /
			static_cast<double>(length - 1);
		const double blackman = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
		taps_[i] = ideal * blackman;
		sum += taps_[i];
	}
	if(!std::isfinite(sum) || std::abs(sum) < 1e-15)
	{
		reset();
		error = "Could not normalize the EEG anti-alias filter.";
		return false;
	}
	const double gain = static_cast<double>(up_) / sum;
	for(double& tap : taps_) tap *= gain;
	return true;
}

void StreamingEegResampler::reset()
{
	history_.clear();
	history_start_index_ = 0;
	input_count_ = 0;
	next_output_index_ = 0;
}

bool StreamingEegResampler::pushSample(const float* sample, std::size_t channels,
	std::vector<std::vector<float>>& output_samples, std::string& error)
{
	output_samples.clear();
	error.clear();
	if(!sample || channels != channel_count_ || channel_count_ == 0 || taps_.empty())
	{
		reset();
		error = "EEG resampler is not configured or sample channel count does not match.";
		return false;
	}
	for(std::size_t channel = 0; channel < channels; ++channel)
	{
		if(!std::isfinite(sample[channel]))
		{
			reset();
			error = "EEG resampler received a non-finite sample.";
			return false;
		}
	}

	if(isIdentity())
	{
		output_samples.emplace_back(sample, sample + channels);
		return true;
	}

	history_.emplace_back(sample, sample + channels);
	const std::uint64_t current_input_index = input_count_++;
	const std::uint64_t latest_high_rate_index = current_input_index * static_cast<std::uint64_t>(up_);
	const std::uint64_t last_tap = static_cast<std::uint64_t>(taps_.size() - 1);
	while(next_output_index_ <= latest_high_rate_index / static_cast<std::uint64_t>(down_))
	{
		const std::uint64_t output_high_rate_index = next_output_index_ * static_cast<std::uint64_t>(down_);
		++next_output_index_;
		if(output_high_rate_index < last_tap) continue; // discard FIR startup transient

		std::vector<double> sums(channel_count_, 0.0);
		const std::uint64_t maximum_input_index = output_high_rate_index / static_cast<std::uint64_t>(up_);
		for(std::uint64_t tap_index = output_high_rate_index % static_cast<std::uint64_t>(up_);
			tap_index < taps_.size(); tap_index += static_cast<std::uint64_t>(up_))
		{
			const std::uint64_t input_index = (output_high_rate_index - tap_index) / static_cast<std::uint64_t>(up_);
			if(input_index > maximum_input_index || input_index >= input_count_ || input_index < history_start_index_) continue;
			const std::size_t history_index = static_cast<std::size_t>(input_index - history_start_index_);
			if(history_index >= history_.size())
			{
				reset();
				error = "EEG resampler history invariant failed.";
				return false;
			}
			const std::vector<float>& input = history_[history_index];
			const double coefficient = taps_[static_cast<std::size_t>(tap_index)];
			for(std::size_t channel = 0; channel < channel_count_; ++channel)
				sums[channel] += coefficient * input[channel];
		}
		std::vector<float> output(channel_count_);
		for(std::size_t channel = 0; channel < channel_count_; ++channel)
			output[channel] = static_cast<float>(sums[channel]);
		if(!std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value); }))
		{
			reset();
			error = "EEG resampler produced a non-finite sample.";
			return false;
		}
		output_samples.push_back(std::move(output));
	}

	const std::uint64_t next_high_rate_index = next_output_index_ * static_cast<std::uint64_t>(down_);
	const std::uint64_t earliest_needed = next_high_rate_index > last_tap ?
		(next_high_rate_index - last_tap + static_cast<std::uint64_t>(up_) - 1) / static_cast<std::uint64_t>(up_) : 0;
	while(!history_.empty() && history_start_index_ < earliest_needed)
	{
		history_.pop_front();
		++history_start_index_;
	}
	return true;
}

double StreamingEegResampler::groupDelayMilliseconds() const
{
	if(input_rate_hz_ <= 0 || taps_.empty()) return 0.0;
	return (static_cast<double>(taps_.size() - 1) * 0.5) /
		(static_cast<double>(up_) * input_rate_hz_) * 1000.0;
}

} // namespace neurocontrol
