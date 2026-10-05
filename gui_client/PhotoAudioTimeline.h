#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

// Fixed 48 kHz stereo PCM timeline shared by capture and encoding threads.
// Missing packets are silence; sources are summed with saturation on read.
// Five seconds is a hard limit, not a growing queue. Overrun/late data fail.
namespace PhotoAudio {
constexpr int sample_rate = 48000;
constexpr int channels = 2;
constexpr int capacity_frames = sample_rate * 5;

class Timeline {
public:
	Timeline();
	void add(int64_t first_frame, const int16_t* pcm, int frames);
	std::vector<int16_t> take(int frames);
	int64_t position() const;
private:
	mutable std::mutex mutex;
	std::vector<int32_t> samples;
	int64_t cursor = 0;
};
}
