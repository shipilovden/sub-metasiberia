#pragma once
#if defined(_WIN32) && !defined(USE_SDL)
#include "PhotoAudioTimeline.h"
#include <atomic>
#include <memory>

namespace PhotoAudio {
// Construct ONLY as part of the user's Record action. This object opens devices.
// Process capture includes this process and its descendants, never the desktop.
class Capture {
public:
	Capture(Timeline& timeline, int64_t epoch100ns, bool microphone, bool world_audio, const std::atomic<bool>& stop);
	~Capture();
	void checkError() const;
	void finish(); // Stop, drain and join; throws on capture/stop failure.
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
#endif
