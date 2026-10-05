#pragma once
#include "PhotoAudioTimeline.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace PhotoAudio {
// Single producer (AudioEngine's serialised callback), single consumer (encoder).
// Preallocated, bounded handoff: no callback allocation, mutex, wait, or codec call.
class Feed {
public:
	enum Error { ok, bad_rate, bad_packet, nonfinite, overflow, late, concurrent };
	Feed() : samples(capacity_frames * channels) {}
	Error push(const float* stereo, size_t frames, int64_t first_frame) noexcept {
		const int64_t written = write_position.load(std::memory_order_relaxed);
		const int64_t read = read_position.load(std::memory_order_acquire);
		if(frames > static_cast<size_t>(capacity_frames - (written - read))) return overflow;
		if(written == 0) start_frame.store(first_frame,std::memory_order_relaxed);
		if(start_frame.load(std::memory_order_relaxed) + written < consumed_until.load(std::memory_order_acquire)) return late;
		for(size_t i=0;i<frames;++i) {
			for(int c=0;c<channels;++c) {
				const float value=stereo[i*channels+c];
				if(!std::isfinite(value)) return nonfinite;
				samples[((written+i)%capacity_frames)*channels+c]=static_cast<int16_t>(std::max(-32768.0f,std::min(32767.0f,value*32768.0f)));
			}
		}
		write_position.store(written+static_cast<int64_t>(frames),std::memory_order_release);
		return ok;
	}
	// Adds available engine samples to the microphone timeline. Missing engine
	// samples are silence; a late packet is an explicit error, never a time shift.
	bool mix(int16_t* pcm, int frames, int64_t first_frame) noexcept {
		int64_t read=read_position.load(std::memory_order_relaxed);
		const int64_t written=write_position.load(std::memory_order_acquire);
		const int64_t start=start_frame.load(std::memory_order_relaxed);
		if(read<written && start+read<first_frame) return false;
		for(int i=0;i<frames;++i) {
			if(read<written && start+read==first_frame+i) {
				for(int c=0;c<channels;++c) {
					const int sum=pcm[i*channels+c]+samples[(read%capacity_frames)*channels+c];
					pcm[i*channels+c]=static_cast<int16_t>(std::max(-32768,std::min(32767,sum)));
				}
				++read;
			}
		}
		read_position.store(read,std::memory_order_release);
		consumed_until.store(first_frame+frames,std::memory_order_release);
		return true;
	}
	static const char* message(Error error) noexcept {
		switch(error) {
		case bad_rate: return "Engine recording audio must be 48000 Hz stereo; other sample rates are unsupported";
		case bad_packet: return "Invalid engine recording audio packet (maximum 48000 stereo frames)";
		case nonfinite: return "Engine recording audio contains a non-finite sample";
		case overflow: return "Engine recording audio buffer overflow: encoder cannot keep up (5 second limit)";
		case late: return "Engine recording audio arrived too late for the video timeline";
		case concurrent: return "Engine recording callbacks must be serialised";
		default: return "Engine recording audio callback failed";
		}
	}
private:
	std::vector<int16_t> samples;
	std::atomic<int64_t> write_position{0}, read_position{0}, start_frame{0}, consumed_until{0};
};
static_assert(std::atomic<int64_t>::is_always_lock_free,"Native recording requires lock-free 64-bit counters");
}
