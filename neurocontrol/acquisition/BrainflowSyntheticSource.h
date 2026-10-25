#pragma once

#include <memory>
#include <string>
#include <vector>

namespace neurocontrol {

// Isolates BrainFlow headers from the game target. Its public include paths
// contain timestamp.h, which collides with glare-core TimeStamp.h on Windows.
class BrainflowSyntheticSource {
public:
    BrainflowSyntheticSource();
    ~BrainflowSyntheticSource();
    void start();
    void stop();
    int sampleRateHz() const;
    const std::vector<std::string>& channels() const;
    // Returns interleaved sample-major data in microvolts. These labels are
    // generic synthetic channel numbers, not physical electrode locations.
    std::vector<float> readSamples();
    std::vector<double> readMicrovolts();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace neurocontrol
