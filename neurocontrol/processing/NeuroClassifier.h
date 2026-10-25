#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "../avatar/NeuroCommand.h"

namespace neurocontrol {

struct Model {
    int sample_rate_hz = 0;
    int window_samples = 0;
    int step_samples = 0;
    std::vector<std::string> channels;
    std::vector<std::array<double, 6>> sos;
    std::vector<std::vector<double>> csp_filters;
    std::vector<double> lda_coefficients;
    double lda_intercept = 0.0;
    double forward_threshold = 0.75;
    CommandType positive_command = CommandType::Forward;
    CommandType negative_command = CommandType::Stop;
    bool experimental_turn_mapping = false;
    bool valid() const;
};

struct Classification {
    bool available = false;
    bool sample_valid = false;
    bool signal_good = false;
    bool forward = false;
    double forward_probability = 0.0;
    CommandType candidate_command = CommandType::Stop;
    bool class_confident = false;
    bool awaiting_confirmation = false;
    int consecutive_candidate_windows = 0;
    CommandType command = CommandType::Stop;
    std::size_t sample_index = 0;
};

// Consumes original EDF volt samples at the native sample rate. Its causal SOS
// filter and CSP + LDA math match tools/prepare_train.py. No command timing is
// inferred from event labels: only EEG samples reach this classifier.
class NeuroClassifier {
public:
    explicit NeuroClassifier(Model model);
    void reset();
    Classification pushSample(const float* sample, std::size_t channels);

private:
    Model model_;
    std::vector<std::array<double, 2>> sos_states_;
    std::vector<double> ring_;
    std::vector<unsigned char> invalid_ring_;
    std::size_t sample_count_ = 0;
    int consecutive_candidate_ = 0;
    CommandType candidate_command_ = CommandType::Stop;
};

} // namespace neurocontrol
