#pragma once
#if !defined(USE_SDL)
#include <QtGui/QImage>
#include <QtCore/QString>
#include <memory>
#include <cstddef>

// Native Qt recording adapter. Encoding is off the GUI thread. Timestamped frame
// history is bounded by ceil(0.2 * fps) + 2 pending images / 512 MiB with audio.
// Missed capture deadlines repeat the last frame at that time, not slow time.
class PhotoVideoRecorder {
public:
	// Construct only when the user presses Record. No devices are opened with
	// both options false. world_audio captures this process tree by default.
	// With external_world_audio=true it accepts the engine's final stereo mix via
	// submitWorldAudio instead: no loopback device or OS build restriction. This
	// includes engine voices/media, but excludes CEF's direct audio output.
	// Windows: H.264 + optional 48 kHz stereo AAC (160 kbit/s). Audio choices are
	// fixed for the clip. Unsupported process capture fails through error().
	PhotoVideoRecorder(const QString& path, int fps, int bitrate, const QImage& first_frame,
		bool microphone = false, bool world_audio = false, bool external_world_audio = false);
	~PhotoVideoRecorder();
	void submit(const QImage& frame);
	// Interleaved float stereo at 48000 Hz, at most 48000 frames per call.
	// Copies/converts into bounded state-owned storage; never calls the encoder.
	// The first packet uses the recording clock, later packets are sample-contiguous.
	// Bad rate/data/overflow stops recording and is exposed through error().
	// Register only after Record; synchronously detach the engine callback BEFORE
	// stop()/destruction. Calls after stop are ignored while the object is alive.
	void submitWorldAudio(const float* stereo, std::size_t frames, unsigned sample_rate) noexcept;
	void stop(); // Asynchronous finalisation; keep polling finished().
	bool stopping() const;
	bool finished() const;
	QString error() const;
private:
	struct State;
	std::unique_ptr<State> state;
};
#endif
