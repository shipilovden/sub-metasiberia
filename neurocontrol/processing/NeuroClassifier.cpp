#include "NeuroClassifier.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace neurocontrol {

bool Model::valid() const
{
    const auto supported_command = [](CommandType command) {
        return command == CommandType::Stop || command == CommandType::Forward ||
            command == CommandType::TurnLeft || command == CommandType::TurnRight;
    };
    const bool has_turn = positive_command == CommandType::TurnLeft || positive_command == CommandType::TurnRight ||
        negative_command == CommandType::TurnLeft || negative_command == CommandType::TurnRight;
    if(sample_rate_hz <= 0 || sample_rate_hz > 10000 || window_samples <= 0 || window_samples > sample_rate_hz * 60 ||
       step_samples < 0 || step_samples > window_samples ||
       channels.empty() || channels.size() > 256 || sos.empty() || sos.size() > 16 ||
       csp_filters.empty() || csp_filters.size() != lda_coefficients.size() ||
       !std::isfinite(lda_intercept) || !std::isfinite(forward_threshold) || forward_threshold <= 0.5 || forward_threshold >= 1.0 ||
       !supported_command(positive_command) || !supported_command(negative_command) || positive_command == negative_command ||
       has_turn != experimental_turn_mapping)
        return false;
    for(const auto& filter : csp_filters)
    {
        if(filter.size() != channels.size()) return false;
        if(!std::all_of(filter.begin(), filter.end(), [](double value) { return std::isfinite(value); })) return false;
    }
    if(!std::all_of(lda_coefficients.begin(), lda_coefficients.end(), [](double value) { return std::isfinite(value); })) return false;
    for(const auto& section : sos)
        if(!std::all_of(section.begin(), section.end(), [](double value) { return std::isfinite(value); }) ||
           std::abs(section[3] - 1.0) > 1e-8) return false;
    return true;
}

NeuroClassifier::NeuroClassifier(Model model) : model_(std::move(model))
{
    if(!model_.valid()) throw std::invalid_argument("Invalid NeuroControl model");
    reset();
}

void NeuroClassifier::reset()
{
    sos_states_.assign(model_.channels.size() * model_.sos.size(), {0.0, 0.0});
    ring_.assign(static_cast<std::size_t>(model_.window_samples) * model_.channels.size(), 0.0);
    invalid_ring_.assign(static_cast<std::size_t>(model_.window_samples), 0);
    sample_count_ = 0;
    consecutive_candidate_ = 0;
    candidate_command_ = CommandType::Stop;
}

Classification NeuroClassifier::pushSample(const float* sample, std::size_t channels)
{
    if(!sample || channels != model_.channels.size())
        throw std::invalid_argument("NeuroControl sample channel count mismatch");
    const std::size_t n_channels = model_.channels.size();
    const std::size_t slot = sample_count_ % static_cast<std::size_t>(model_.window_samples);
    bool invalid_sample = false;
    for(std::size_t channel = 0; channel < n_channels; ++channel)
    {
        double value = sample[channel];
        if(!std::isfinite(value) || std::abs(value) >= 0.002)
        {
            invalid_sample = true;
            value = 0.0;
        }
        for(std::size_t section = 0; section < model_.sos.size(); ++section)
        {
            const auto& s = model_.sos[section];
            auto& state = sos_states_[channel * model_.sos.size() + section];
            const double output = s[0] * value + state[0];
            state[0] = s[1] * value - s[4] * output + state[1];
            state[1] = s[2] * value - s[5] * output;
            value = output;
        }
        ring_[slot * n_channels + channel] = value;
    }
    invalid_ring_[slot] = invalid_sample ? 1 : 0;
    ++sample_count_;
    Classification result;
    result.sample_index = sample_count_;
    result.sample_valid = !invalid_sample;
    const int stride = model_.step_samples > 0 ? model_.step_samples : model_.sample_rate_hz;
    if(sample_count_ < static_cast<std::size_t>(model_.window_samples) ||
       (sample_count_ - static_cast<std::size_t>(model_.window_samples)) % stride != 0)
        return result;
    result.available = true;
    double total_power = 0.0;
    double maximum = 0.0;
    for(double value : ring_)
    {
        if(!std::isfinite(value)) { consecutive_candidate_ = 0; candidate_command_ = CommandType::Stop; return result; }
        total_power += value * value;
        maximum = std::max(maximum, std::abs(value));
    }
    result.signal_good = std::none_of(invalid_ring_.begin(), invalid_ring_.end(), [](unsigned char flag) { return flag != 0; }) &&
        maximum < 0.002 && total_power / ring_.size() > 1e-18;
    if(!result.signal_good) { consecutive_candidate_ = 0; candidate_command_ = CommandType::Stop; return result; }
    double decision = model_.lda_intercept;
    for(std::size_t component = 0; component < model_.csp_filters.size(); ++component)
    {
        double power = 0.0;
        for(int i = 0; i < model_.window_samples; ++i)
        {
            const double* row = &ring_[static_cast<std::size_t>(i) * n_channels];
            double projected = 0.0;
            for(std::size_t channel = 0; channel < n_channels; ++channel)
                projected += model_.csp_filters[component][channel] * row[channel];
            power += projected * projected;
        }
        const double feature = std::log(std::max(power / model_.window_samples, 1e-30));
        decision += model_.lda_coefficients[component] * feature;
    }
    result.forward_probability = decision >= 0 ? 1.0 / (1.0 + std::exp(-decision)) :
        std::exp(decision) / (1.0 + std::exp(decision));
    CommandType candidate = CommandType::Stop;
    bool class_confident = false;
    if(result.forward_probability >= model_.forward_threshold)
    {
        candidate = model_.positive_command;
        class_confident = true;
    }
    else if(result.forward_probability <= 1.0 - model_.forward_threshold)
    {
        candidate = model_.negative_command;
        class_confident = true;
    }

    if(candidate == CommandType::Stop)
    {
        candidate_command_ = CommandType::Stop;
        consecutive_candidate_ = 0;
    }
    else if(candidate == candidate_command_)
        consecutive_candidate_ = std::min(consecutive_candidate_ + 1, 2);
    else
    {
        candidate_command_ = candidate;
        consecutive_candidate_ = 1;
    }
    result.candidate_command = candidate;
    result.class_confident = class_confident;
    result.consecutive_candidate_windows = consecutive_candidate_;
    result.awaiting_confirmation = class_confident && candidate != CommandType::Stop && consecutive_candidate_ < 2;
    // Motion classes need two adjacent confident windows; a stop class is
    // immediate. This keeps turn imagery mapped only when explicitly configured.
    result.command = candidate == CommandType::Stop ? CommandType::Stop :
        consecutive_candidate_ >= 2 ? candidate : CommandType::Stop;
    result.forward = result.command == CommandType::Forward;
    return result;
}

} // namespace neurocontrol
