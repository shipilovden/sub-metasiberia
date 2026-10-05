#include "PhotoAudioTimeline.h"
#include <algorithm>
#include <stdexcept>

namespace PhotoAudio {
Timeline::Timeline() : samples(capacity_frames * channels, 0) {}

void Timeline::add(int64_t first, const int16_t* pcm, int frames) {
	if(frames < 0 || frames > capacity_frames || (frames && !pcm))
		throw std::runtime_error("Invalid audio packet");
	std::lock_guard<std::mutex> lock(mutex);
	if(frames == 0 || first + frames <= 0) return;
	if(first < 0) { const int skip = static_cast<int>(-first); pcm += skip * channels; frames -= skip; first = 0; }
	if(first < cursor) throw std::runtime_error("Audio capture arrived too late for the video timeline");
	if(first > cursor + capacity_frames - frames)
		throw std::runtime_error("Audio recording buffer overflow: encoder cannot keep up (5 second limit)");
	for(int i = 0; i < frames; ++i)
		for(int c = 0; c < channels; ++c)
			samples[((first + i) % capacity_frames) * channels + c] += pcm[i * channels + c];
}

std::vector<int16_t> Timeline::take(int frames) {
	if(frames < 0 || frames > capacity_frames) throw std::runtime_error("Invalid audio read size");
	std::vector<int16_t> result(frames * channels);
	std::lock_guard<std::mutex> lock(mutex);
	for(int i = 0; i < frames; ++i) {
		for(int c = 0; c < channels; ++c) {
			auto& value = samples[((cursor + i) % capacity_frames) * channels + c];
			result[i * channels + c] = static_cast<int16_t>(std::max(-32768, std::min(32767, value)));
			value = 0;
		}
	}
	cursor += frames;
	return result;
}
int64_t Timeline::position() const { std::lock_guard<std::mutex> lock(mutex); return cursor; }
}
