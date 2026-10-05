#pragma once
#if !defined(USE_SDL)
#include <QtGui/QImage>
#include <cstdint>
#include <deque>
#include <stdexcept>

namespace PhotoAudio {
// Called under the recorder mutex. Timestamps are capture times relative to
// Record, not the time the encoder eventually consumes a frame. Header-only
// so the deterministic timing test exercises the recorder's exact selection.
class VideoTimeline {
public:
	static constexpr int packet_delay_us = 200000;
	static constexpr int64_t memory_limit = 512LL * 1024 * 1024;
	VideoTimeline(const QImage& first, int rate, bool audio)
	: current(first), fps(rate), max_pending(audio ? (rate + 4) / 5 + 2 : 2), retained_bytes(first.sizeInBytes()) {
		if(retained_bytes > memory_limit) throw std::runtime_error("Recording video history exceeds 512 MiB limit");
	}
	void submit(const QImage& frame, int64_t captured_us) {
		if(captured_us < last_capture_us) throw std::runtime_error("Video capture clock moved backwards");
		last_capture_us = captured_us;
		// First output timestamp at or after this capture. A later capture in the
		// same interval replaces an earlier one, so submit frequency cannot grow memory.
		const int64_t boundary = (captured_us * fps + 999999) / 1000000;
		const bool replace = !pending.empty() && pending.back().boundary == boundary;
		const int64_t bytes = retained_bytes + frame.sizeInBytes() - (replace ? pending.back().image.sizeInBytes() : 0);
		if((!replace && pending.size() >= max_pending) || bytes > memory_limit)
			throw std::runtime_error("Video recording history overflow: encoder cannot keep up (bounded frames / 512 MiB limit)");
		if(replace) pending.back().image = frame;
		else pending.push_back({boundary,frame});
		retained_bytes = bytes;
	}
	QImage at(int64_t frame_index) {
		if(frame_index < last_output) throw std::runtime_error("Video output clock moved backwards");
		last_output = frame_index;
		while(!pending.empty() && pending.front().boundary <= frame_index) {
			retained_bytes -= current.sizeInBytes();
			current = pending.front().image;
			pending.pop_front();
		}
		return current;
	}
	QSize size() const { return current.size(); }
	size_t pendingCount() const { return pending.size(); }
	int64_t retainedBytes() const { return retained_bytes; }
private:
	struct Frame { int64_t boundary; QImage image; };
	QImage current;
	const int fps;
	const size_t max_pending;
	std::deque<Frame> pending;
	int64_t retained_bytes, last_capture_us = 0, last_output = -1;
};
}
#endif
