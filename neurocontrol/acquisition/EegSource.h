#pragma once

#include <string>
#include <vector>

namespace neurocontrol {

// Hardware and recorded sources can implement this contract without changing
// classification or avatar input. The source timestamp is never substituted
// for the receiver's monotonic arrival clock.
struct EegFrame {
    double timestamp_seconds = 0.0;
    double source_timestamp_seconds = 0.0;
    double arrival_monotonic_seconds = 0.0;
    double display_timestamp_seconds = 0.0;
    std::vector<float> volts;
	// Present only when the source exposes native float32 values. This remains
	// in source units and is not altered by the volts conversion.
	std::vector<float> raw_source_values;
	bool raw_source_values_exact = false;
    bool signal_good = false;
};

class EegSource {
public:
    virtual ~EegSource() = default;
    virtual int sampleRateHz() const = 0;
    virtual const std::vector<std::string>& channels() const = 0;
    virtual bool start(std::string& error) = 0;
    virtual void stop() = 0;
    virtual bool read(EegFrame& frame, std::string& error) = 0;
};

} // namespace neurocontrol
