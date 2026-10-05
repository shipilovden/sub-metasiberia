#pragma once
#if defined(_WIN32) && !defined(USE_SDL)
#include <QtCore/QString>
#include <QtGui/QImage>
#include <cstdint>
#include <memory>

namespace PhotoAudio {
// Thread-confined synchronous native MP4 sink. No capture or device access.
// Keep default MF throttling enabled to bound encoder buffering.
class Muxer {
public:
	Muxer(const QString& path, int width, int height, int fps, int bitrate, bool audio);
	~Muxer();
	void video(const QImage& image, int64_t index);
	void audio(const int16_t* stereo, int frames, int64_t first_frame);
	void finish();
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
#endif
