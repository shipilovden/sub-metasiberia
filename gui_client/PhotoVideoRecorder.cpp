#include "PhotoVideoRecorder.h"
#if !defined(USE_SDL)
#include "PhotoAudioVideoTimeline.h"
#include "PhotoAudioTimeline.h"
#include "PhotoAudioFeed.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#include "PhotoAudioCapture.h"
#include "PhotoAudioMuxer.h"
#include "PhotoAudioWindows.h"
#endif

struct PhotoVideoRecorder::State {
	std::mutex mutex;
	std::condition_variable wake;
	std::thread thread;
	std::unique_ptr<PhotoAudio::VideoTimeline> frames;
	// Available immediately on construction; no worker-owned pointer publication.
	PhotoAudio::Timeline timeline;
	PhotoAudio::Feed feed;
	std::atomic_flag feed_busy=ATOMIC_FLAG_INIT;
	std::atomic<PhotoAudio::Feed::Error> feed_error{PhotoAudio::Feed::ok};
	bool external_world = false; // Immutable once the constructor returns.
	QString failure;
	std::atomic<bool> done{false}, stop_requested{false};
	using Clock = std::chrono::steady_clock;
	Clock::time_point started = Clock::now();
#ifdef _WIN32
	int64_t epoch = PhotoAudio::clock100ns();
#endif
	long long end_frame = 1;
	int fps;
	void fail(const char* message) noexcept {
		try {
			std::lock_guard<std::mutex> lock(mutex);
			if(failure.isEmpty()) failure=QString::fromUtf8(message);
		} catch(...) {
			// An allocation failure must not escape the audio engine callback.
		}
		stop_requested=true;
		wake.notify_all();
	}
	void failFeed(PhotoAudio::Feed::Error code) noexcept {
		auto expected=PhotoAudio::Feed::ok;
		feed_error.compare_exchange_strong(expected,code);
		stop_requested=true;
		wake.notify_all();
	}
};

PhotoVideoRecorder::PhotoVideoRecorder(const QString& path, int fps, int bitrate, const QImage& first_frame,
	bool microphone, bool world_audio, bool external_world_audio) : state(new State) {
	if(path.isEmpty() || first_frame.isNull() || fps<1 || fps>240 || bitrate<1 || (first_frame.width()&1) || (first_frame.height()&1))
		throw std::runtime_error("Invalid recording parameters (even frame dimensions required)");
	state->fps=fps;
	state->external_world=world_audio && external_world_audio;
	state->frames.reset(new PhotoAudio::VideoTimeline(first_frame,fps,microphone || world_audio));
	State* s=state.get();
	const QSize frame_size=first_frame.size();
	s->thread=std::thread([s,path,bitrate,frame_size,microphone,world_audio]() {
		try {
#ifdef _WIN32
			const bool audio = microphone || world_audio;
			PhotoAudio::Muxer writer(path,frame_size.width(),frame_size.height(),s->fps,bitrate,audio);
			PhotoAudio::Timeline& timeline=s->timeline;
			std::unique_ptr<PhotoAudio::Capture> capture;
			const bool process_audio=world_audio && !s->external_world;
			if((microphone || process_audio) && !s->stop_requested)
				capture.reset(new PhotoAudio::Capture(timeline,s->epoch,microphone,process_audio,s->stop_requested));
			bool capture_joined = false;
			long long index=0;
			for(;;) {
				if(s->feed_error!=PhotoAudio::Feed::ok) throw std::runtime_error(PhotoAudio::Feed::message(s->feed_error));
				if(capture) {
					capture->checkError();
					if(s->stop_requested && !capture_joined) { capture->finish(); capture_joined = true; }
				}
				QImage frame;
				{
					std::unique_lock<std::mutex> lock(s->mutex);
					// Encode completed frame intervals; allow 200 ms for WASAPI packet delivery.
					const auto next=s->started+std::chrono::microseconds((index+1)*1000000/s->fps + (audio ? PhotoAudio::VideoTimeline::packet_delay_us : 0));
					s->wake.wait_until(lock,next,[s]() { return s->stop_requested.load(); });
					if(s->feed_error!=PhotoAudio::Feed::ok) throw std::runtime_error(PhotoAudio::Feed::message(s->feed_error));
					if(!s->failure.isEmpty()) throw std::runtime_error(s->failure.toUtf8().constData());
					if(s->stop_requested && index>=s->end_frame) break;
					frame=s->frames->at(index);
				}
				// stop() may have woken the wait: drain before reading the last audio.
				if(capture && s->stop_requested && !capture_joined) { capture->finish(); capture_joined = true; }
				writer.video(frame,index);
				if(audio) {
					const int64_t end = (index+1)*PhotoAudio::sample_rate/s->fps;
					while(timeline.position() < end) {
						if(capture) capture->checkError();
						const int64_t first = timeline.position();
						const int count = static_cast<int>(std::min<int64_t>(480,end-first));
						auto pcm = timeline.take(count);
						if(s->external_world && !s->feed.mix(pcm.data(),count,first))
							throw std::runtime_error(PhotoAudio::Feed::message(PhotoAudio::Feed::late));
						writer.audio(pcm.data(),count,first);
					}
				}
				++index;
			}
			if(capture) capture->finish();
			writer.finish();
#else
			throw std::runtime_error("MP4 recording is supported on Windows only");
#endif
		} catch(const std::exception& e) {
			s->fail(e.what());
		} catch(...) {
			s->fail("Video encoder failed");
		}
		s->done=true;
	});
}
PhotoVideoRecorder::~PhotoVideoRecorder() { stop(); if(state->thread.joinable()) state->thread.join(); }
void PhotoVideoRecorder::submit(const QImage& frame) {
	std::lock_guard<std::mutex> lock(state->mutex);
	if(!state->stop_requested && !state->done && !frame.isNull() && frame.size()==state->frames->size()) {
		try {
			const auto us=std::chrono::duration_cast<std::chrono::microseconds>(State::Clock::now()-state->started).count();
			state->frames->submit(frame,us);
		} catch(const std::exception& e) {
			state->failure=QString::fromUtf8(e.what());
			state->stop_requested=true; // Stop devices promptly even if the encoder is busy.
			state->wake.notify_all();
		}
	}
}
void PhotoVideoRecorder::submitWorldAudio(const float* stereo, std::size_t frames, unsigned sample_rate) noexcept {
	State* s=state.get();
	if(!s->external_world || s->stop_requested || s->done || frames==0) return;
	if(sample_rate!=PhotoAudio::sample_rate) { s->failFeed(PhotoAudio::Feed::bad_rate); return; }
	if(!stereo || frames>PhotoAudio::sample_rate) { s->failFeed(PhotoAudio::Feed::bad_packet); return; }
	if(s->feed_busy.test_and_set(std::memory_order_acquire)) { s->failFeed(PhotoAudio::Feed::concurrent); return; }
	int64_t first=0;
#ifdef _WIN32
	first=(PhotoAudio::clock100ns()-s->epoch)*PhotoAudio::sample_rate/10000000;
#endif
	const auto error=s->feed.push(stereo,frames,first);
	s->feed_busy.clear(std::memory_order_release);
	if(error!=PhotoAudio::Feed::ok) s->failFeed(error);
}
void PhotoVideoRecorder::stop() {
	std::lock_guard<std::mutex> lock(state->mutex);
	if(state->stop_requested) return;
	const auto us=std::chrono::duration_cast<std::chrono::microseconds>(State::Clock::now()-state->started).count();
	state->end_frame=qMax(1LL,(long long)((us*state->fps+999999)/1000000));
	state->stop_requested=true;
	state->wake.notify_all();
}
bool PhotoVideoRecorder::stopping() const { return state->stop_requested; }
bool PhotoVideoRecorder::finished() const { return state->done; }
QString PhotoVideoRecorder::error() const {
	if(state->feed_error!=PhotoAudio::Feed::ok) return QString::fromUtf8(PhotoAudio::Feed::message(state->feed_error));
	std::lock_guard<std::mutex> lock(state->mutex); return state->failure;
}
#endif
