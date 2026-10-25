#include "NeuroClassifier.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
    neurocontrol::Model model;
    model.sample_rate_hz = 2;
    model.window_samples = 4;
    model.channels = {"Cz"};
    model.sos = {{1, 0, 0, 1, 0, 0}};
    model.csp_filters = {{1}};
    model.lda_coefficients = {0};
    model.lda_intercept = 10;
    model.forward_threshold = 0.75;
    neurocontrol::NeuroClassifier classifier(model);
    bool rejected_empty_sample = false;
    try
    {
        classifier.pushSample(nullptr, 0);
    }
    catch(const std::invalid_argument&)
    {
        rejected_empty_sample = true;
    }
    if(!rejected_empty_sample) return 7;
    const float mismatched_channels[] = {0.00001f, 0.00001f};
    bool rejected_channel_mismatch = false;
    try
    {
        classifier.pushSample(mismatched_channels, 2);
    }
    catch(const std::invalid_argument&)
    {
        rejected_channel_mismatch = true;
    }
    if(!rejected_channel_mismatch) return 8;
    for(int i = 1; i <= 14; ++i)
    {
        const float value = i == 7 ? std::numeric_limits<float>::quiet_NaN() : 0.00001f;
        const auto result = classifier.pushSample(&value, 1);
        if(i == 4 && (!result.available || !result.signal_good || result.forward ||
            result.candidate_command != neurocontrol::CommandType::Forward || !result.class_confident ||
            !result.awaiting_confirmation || result.consecutive_candidate_windows != 1)) return 1;
        if(i == 6 && (!result.available || !result.signal_good || !result.forward ||
            result.candidate_command != neurocontrol::CommandType::Forward || result.awaiting_confirmation ||
            result.consecutive_candidate_windows != 2)) return 2;
        if(i == 8 && (!result.available || result.signal_good || result.forward)) return 3;
        if(i == 10 && (!result.available || result.signal_good || result.forward)) return 4;
        if(i == 12 && (!result.available || !result.signal_good || result.forward)) return 5;
        if(i == 14 && (!result.available || !result.signal_good || !result.forward)) return 6;
    }
    std::cout << "Safety: empty/mismatched samples rejected, consecutive positives, invalid EEG stop, recovery passed\n";
    return 0;
}
