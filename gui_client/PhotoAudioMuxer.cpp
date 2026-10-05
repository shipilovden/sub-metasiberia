#include "PhotoAudioMuxer.h"
#if defined(_WIN32) && !defined(USE_SDL)
#include "PhotoAudioTimeline.h"
#include "PhotoAudioWindows.h"
#include <mfreadwrite.h>
#include <cstring>
#include <limits>

namespace PhotoAudio {
struct Muxer::Impl {
	// Declaration order ensures COM objects die before MF/COM shutdown, even on failure.
	ComApartment apartment;
	MediaFoundation platform;
	ComPtr<IMFSinkWriter> writer;
	DWORD video_stream = 0, audio_stream = 0;
	int width, height, fps;
	bool has_audio, finished = false;
	int64_t next_video = 0, next_audio = 0;

	Impl(int w, int h, int f, bool a) : width(w), height(h), fps(f), has_audio(a) {}
	void write(DWORD stream, IMFMediaBuffer* buffer, int64_t time, int64_t duration) {
		ComPtr<IMFSample> sample;
		check(MFCreateSample(&sample), "Creating recording sample");
		check(sample->AddBuffer(buffer), "Attaching recording sample buffer");
		check(sample->SetSampleTime(time), "Setting recording timestamp");
		check(sample->SetSampleDuration(duration), "Setting recording duration");
		check(writer->WriteSample(stream, sample.Get()), "Encoding MP4 sample");
	}
};

static ComPtr<IMFMediaType> videoType(int width, int height, int fps, const GUID& subtype) {
	ComPtr<IMFMediaType> type;
	check(MFCreateMediaType(&type), "Creating video media type");
	check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Setting video type");
	check(type->SetGUID(MF_MT_SUBTYPE, subtype), "Setting video format");
	check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Setting progressive video");
	check(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height), "Setting video dimensions");
	check(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fps, 1), "Setting video rate");
	check(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Setting pixel aspect ratio");
	return type;
}
static ComPtr<IMFMediaType> audioType(const GUID& subtype, bool compressed) {
	ComPtr<IMFMediaType> type;
	check(MFCreateMediaType(&type), "Creating audio media type");
	check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Setting audio type");
	check(type->SetGUID(MF_MT_SUBTYPE, subtype), "Setting audio format");
	check(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels), "Setting stereo audio");
	check(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate), "Setting audio rate");
	check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Setting audio precision");
	check(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, compressed ? 20000 : sample_rate * 4), "Setting audio bitrate");
	if(compressed) {
		check(type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0), "Setting AAC payload");
		check(type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29), "Setting AAC profile");
	} else {
		check(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4), "Setting PCM alignment");
		check(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE), "Setting PCM sample independence");
	}
	return type;
}

Muxer::Muxer(const QString& path, int width, int height, int fps, int bitrate, bool audio)
: impl(new Impl(width, height, fps, audio)) {
	if(path.isEmpty() || width < 2 || height < 2 || (width & 1) || (height & 1) || fps < 1 || fps > 240 || bitrate < 1 ||
		static_cast<int64_t>(width) * height * 4 > std::numeric_limits<LONG>::max())
		throw std::runtime_error("Invalid MP4 recording parameters (even frame dimensions required)");
	ComPtr<IMFAttributes> attributes;
	check(MFCreateAttributes(&attributes, 1), "Creating MP4 sink attributes");
	check(attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4), "Selecting MP4 container");
	check(MFCreateSinkWriterFromURL(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, attributes.Get(), &impl->writer), "Opening MP4 output");
	auto out = videoType(width, height, fps, MFVideoFormat_H264);
	check(out->SetUINT32(MF_MT_AVG_BITRATE, bitrate), "Setting H.264 bitrate");
	check(impl->writer->AddStream(out.Get(), &impl->video_stream), "Adding H.264 stream");
	auto in = videoType(width, height, fps, MFVideoFormat_RGB32);
	check(in->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4), "Setting RGB stride");
	check(impl->writer->SetInputMediaType(impl->video_stream, in.Get(), nullptr), "Configuring H.264 encoder");
	if(audio) {
		auto aac = audioType(MFAudioFormat_AAC, true);
		check(impl->writer->AddStream(aac.Get(), &impl->audio_stream), "Adding AAC stream");
		auto pcm = audioType(MFAudioFormat_PCM, false);
		check(impl->writer->SetInputMediaType(impl->audio_stream, pcm.Get(), nullptr), "Configuring native AAC encoder");
	}
	check(impl->writer->BeginWriting(), "Starting MP4 writer");
}
Muxer::~Muxer() = default;

void Muxer::video(const QImage& image, int64_t index) {
	if(impl->finished || index != impl->next_video || image.size() != QSize(impl->width, impl->height))
		throw std::runtime_error("Invalid video frame or timestamp");
	const QImage frame = image.convertToFormat(QImage::Format_ARGB32);
	if(frame.isNull()) throw std::runtime_error("Could not allocate recording image");
	const DWORD bytes = impl->width * impl->height * 4;
	ComPtr<IMFMediaBuffer> buffer;
	check(MFCreateMemoryBuffer(bytes, &buffer), "Allocating video buffer");
	BYTE* data = nullptr;
	check(buffer->Lock(&data, nullptr, nullptr), "Locking video buffer");
	// MF RGB32 is bottom-up BGRX; Qt ARGB32 is BGRA on Windows.
	for(int y = 0; y < impl->height; ++y)
		std::memcpy(data + static_cast<size_t>(impl->height - y - 1) * impl->width * 4, frame.constScanLine(y), impl->width * 4);
	check(buffer->Unlock(), "Unlocking video buffer");
	check(buffer->SetCurrentLength(bytes), "Setting video buffer length");
	const int64_t start = index * 10000000 / impl->fps;
	impl->write(impl->video_stream, buffer.Get(), start, (index + 1) * 10000000 / impl->fps - start);
	++impl->next_video;
}
void Muxer::audio(const int16_t* stereo, int frames, int64_t first_frame) {
	if(!impl->has_audio || impl->finished || !stereo || frames < 1 || frames > sample_rate || first_frame != impl->next_audio)
		throw std::runtime_error("Invalid audio sample or timestamp");
	const DWORD bytes = frames * 4;
	ComPtr<IMFMediaBuffer> buffer;
	check(MFCreateMemoryBuffer(bytes, &buffer), "Allocating audio buffer");
	BYTE* data = nullptr;
	check(buffer->Lock(&data, nullptr, nullptr), "Locking audio buffer");
	std::memcpy(data, stereo, bytes);
	check(buffer->Unlock(), "Unlocking audio buffer");
	check(buffer->SetCurrentLength(bytes), "Setting audio buffer length");
	const int64_t start = first_frame * 10000000 / sample_rate;
	impl->write(impl->audio_stream, buffer.Get(), start, (first_frame + frames) * 10000000 / sample_rate - start);
	impl->next_audio += frames;
}
void Muxer::finish() {
	if(impl->finished) return;
	check(impl->writer->Finalize(), "Finalising MP4 (file may be incomplete)");
	impl->finished = true;
}
}
#endif
