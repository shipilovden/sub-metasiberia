#include "BrainflowSyntheticSource.h"

#include <board_shim.h>
#include <stdexcept>

namespace neurocontrol {

struct BrainflowSyntheticSource::Impl {
    std::unique_ptr<BoardShim> board;
    std::vector<int> channels;
    std::vector<std::string> channel_names;
    int sample_rate = 0;
};

BrainflowSyntheticSource::BrainflowSyntheticSource() : impl_(new Impl()) {}
BrainflowSyntheticSource::~BrainflowSyntheticSource() { stop(); }

void BrainflowSyntheticSource::start()
{
    stop();
    const int id = static_cast<int>(BoardIds::SYNTHETIC_BOARD);
    BrainFlowInputParams params;
    impl_->board.reset(new BoardShim(id, params));
    impl_->board->prepare_session();
    impl_->board->start_stream();
    impl_->channels = BoardShim::get_eeg_channels(id);
    impl_->sample_rate = BoardShim::get_sampling_rate(id);
    if(impl_->channels.empty()) throw std::runtime_error("Synthetic Board has no EEG channels");
    impl_->channel_names.clear();
    // Synthetic Board may not expose physical electrode names. Preserve the
    // actual BrainFlow row index instead of inventing a 10-10 location.
    std::vector<std::string> eeg_names;
    try { eeg_names = BoardShim::get_eeg_names(id); }
    catch(const std::exception&) {}
    for(std::size_t i = 0; i < impl_->channels.size(); ++i)
        impl_->channel_names.push_back(eeg_names.size() == impl_->channels.size() ? eeg_names[i] :
            "Синт. ЭЭГ " + std::to_string(i + 1) + " [" + std::to_string(impl_->channels[i]) + "]");
}

void BrainflowSyntheticSource::stop()
{
    if(impl_->board)
    {
		try { impl_->board->stop_stream(); }
		catch(const std::exception&) {}
		try { impl_->board->release_session(); }
        catch(const std::exception&) {}
        impl_->board.reset();
    }
    impl_->channels.clear();
    impl_->channel_names.clear();
    impl_->sample_rate = 0;
}

int BrainflowSyntheticSource::sampleRateHz() const { return impl_->sample_rate; }

const std::vector<std::string>& BrainflowSyntheticSource::channels() const { return impl_->channel_names; }

std::vector<float> BrainflowSyntheticSource::readSamples()
{
    if(!impl_->board) return {};
    const auto data = impl_->board->get_board_data();
    const std::size_t channel_count = impl_->channels.size();
    const std::size_t sample_count = static_cast<std::size_t>(data.get_size(1));
    std::vector<float> result;
    result.reserve(sample_count * channel_count);
    for(std::size_t sample = 0; sample < sample_count; ++sample)
        for(int channel : impl_->channels)
            result.push_back(static_cast<float>(data.at(channel, static_cast<int>(sample))));
    return result;
}

std::vector<double> BrainflowSyntheticSource::readMicrovolts()
{
    if(!impl_->board) return {};
    const auto data = impl_->board->get_board_data();
    std::vector<double> result;
    result.reserve(data.get_size(1));
    for(int i = 0; i < data.get_size(1); ++i) result.push_back(data.at(impl_->channels.front(), i));
    return result;
}

} // namespace neurocontrol
