// Isolated Windows tests: no application launch, no microphone access.
// Build with PhotoVideoRecorder.cpp, PhotoAudio{Capture,Muxer,Timeline}.cpp,
// Qt5::Core/Gui, mfplat mfreadwrite mfuuid ole32 mmdevapi winmm.
// Usage: audio_native.exe <new-output-directory> [--loopback]
// --loopback plays a synthetic tone in this test process on supported Windows.
#include "../../gui_client/PhotoVideoRecorder.h"
#include "../../gui_client/PhotoAudioCapture.h"
#include "../../gui_client/PhotoAudioMuxer.h"
#include "../../gui_client/PhotoAudioTimeline.h"
#include "../../gui_client/PhotoAudioWindows.h"
#include "../../gui_client/PhotoAudioVideoTimeline.h"
#include "../../gui_client/PhotoAudioFeed.h"
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmsystem.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <chrono>
#include <thread>
#include <limits>

using namespace PhotoAudio;
static constexpr DWORD video_stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
static constexpr DWORD audio_stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
static constexpr DWORD all_streams=static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
static void require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
static void throws(const std::function<void()>& f, const char* expected) {
	try { f(); } catch(const std::exception& e) {
		require(std::string(e.what()).find(expected) != std::string::npos, "Wrong error reported"); return;
	}
	throw std::runtime_error("Expected failure was not reported");
}
static QImage frame() {
	QImage image(160, 120, QImage::Format_RGB888);
	for(int y = 0; y < image.height(); ++y) for(int x = 0; x < image.width(); ++x)
		image.setPixelColor(x, y, y < 60 ? Qt::red : Qt::blue);
	return image;
}
static void timelineTests() {
	Timeline mix;
	const int16_t a[] = {30000,-30000,1000,-1000};
	const int16_t b[] = {10000,-10000,2000,-2000};
	mix.add(2,a,2); mix.add(2,b,2);
	auto pcm = mix.take(5);
	require(pcm == std::vector<int16_t>({0,0,0,0,32767,-32768,3000,-3000,0,0}), "Mix/saturation/gap failed");
	throws([&]() { mix.add(1,a,2); }, "too late");
	throws([&]() { mix.add(mix.position()+capacity_frames,a,2); }, "overflow");
	throws([&]() { mix.take(capacity_frames+1); }, "read size");
	mix.take(capacity_frames-6);
	mix.add(mix.position(),a,2);
	require(mix.take(2) == std::vector<int16_t>(a,a+4), "Circular buffer wrap failed");
	Timeline leading;
	leading.add(-1,a,2);
	require(leading.take(1) == std::vector<int16_t>({1000,-1000}), "Pre-start audio trim failed");
	std::puts("PASS: bounded mixer, stereo saturation, gaps, wrap, leading trim, overflow and late packets");
}

static void videoTimelineTests() {
	QImage red(2,2,QImage::Format_RGB888); red.fill(Qt::red);
	QImage blue=red; blue.fill(Qt::blue);
	QImage green=red; green.fill(Qt::green);
	// Simulate encoder delayed by 200 ms: newer content is already available
	// when earlier frames are encoded, but must never be used retroactively.
	VideoTimeline history(red,30,true);
	history.submit(blue,50000);
	history.submit(green,150000);
	require(history.at(0).pixelColor(0,0)==QColor(Qt::red),"Future video leaked into timestamp zero");
	require(history.at(1).pixelColor(0,0)==QColor(Qt::red),"Future video leaked before 50 ms");
	require(history.at(2).pixelColor(0,0)==QColor(Qt::blue),"50 ms capture missing at 66 ms");
	require(history.at(4).pixelColor(0,0)==QColor(Qt::blue),"150 ms capture appeared too early");
	require(history.at(5).pixelColor(0,0)==QColor(Qt::green),"150 ms capture missing at 166 ms");
	require(history.at(50).pixelColor(0,0)==QColor(Qt::green),"Sparse capture did not repeat last frame");
	VideoTimeline bounded(red,30,true);
	for(int i=1;i<=8;++i) bounded.submit(blue,i*33334LL);
	require(bounded.pendingCount()==8,"Incorrect video history bound");
	throws([&]() { bounded.submit(green,300001); },"overflow");
	VideoTimeline coalesced(red,30,true);
	for(int i=1;i<30000;++i) coalesced.submit(blue,i);
	require(coalesced.pendingCount()==1 && coalesced.retainedBytes()==red.sizeInBytes()*2,"High-rate submit grew video history");
	require(coalesced.at(0).pixelColor(0,0)==QColor(Qt::red),"Coalescing changed a past frame");
	require(coalesced.at(1).pixelColor(0,0)==QColor(Qt::blue),"Coalescing lost latest eligible frame");
	std::puts("PASS: deterministic capture-time selection with 200 ms encoder lag, sparse frames, coalescing and bounded history");
}

static void feedQueueTests() {
	Feed feed;
	const float source[]={0.5f,-0.5f,2.0f,-2.0f};
	require(feed.push(source,2,2)==Feed::ok,"Initial engine feed failed");
	require(feed.push(source,2,999)==Feed::ok,"Continuous engine feed failed");
	int16_t mixed[12]={0,0,0,0,20000,-20000,0,0,0,0,0,0};
	require(feed.mix(mixed,6,0),"Engine mixing failed");
	require(std::vector<int16_t>(mixed,mixed+12)==std::vector<int16_t>({0,0,0,0,32767,-32768,32767,-32768,16384,-16384,32767,-32768}),"Engine gap, float conversion, clipping or sample continuity failed");
	Feed wrap;
	std::vector<float> input(480*2,0.25f);
	for(int64_t at=0;at<capacity_frames*3;at+=480) {
		require(wrap.push(input.data(),480,at)==Feed::ok,"Engine ring wrapped incorrectly on write");
		std::vector<int16_t> output(480*2,0);
		require(wrap.mix(output.data(),480,at),"Engine ring wrapped incorrectly on read");
		require(std::all_of(output.begin(),output.end(),[](int16_t value) { return value==8192; }),"Engine ring contains stale samples");
	}
	std::puts("PASS: lock-free engine handoff, first-packet alignment, continuity, saturation and three ring wraps");
}

static ComPtr<IMFSourceReader> reader(const QString& path) {
	ComPtr<IMFAttributes> attributes;
	check(MFCreateAttributes(&attributes,1), "Reader attributes");
	check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE), "Reader video conversion");
	ComPtr<IMFSourceReader> result;
	check(MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(path.utf16()),attributes.Get(),&result), "Open test MP4");
	return result;
}
static void verify(const QString& path, bool has_audio, int expected_frames, int fps, bool tones, bool timed_change=false) {
	auto source = reader(path);
	ComPtr<IMFMediaType> video;
	check(source->GetNativeMediaType(video_stream,0,&video), "Read native video type");
	GUID subtype{};
	check(video->GetGUID(MF_MT_SUBTYPE,&subtype), "Read video codec");
	require(subtype == MFVideoFormat_H264,"MP4 must contain H.264 video");
	ComPtr<IMFMediaType> audio;
	const HRESULT audio_hr = source->GetNativeMediaType(audio_stream,0,&audio);
	if(has_audio) {
		check(audio_hr,"Read native audio type");
		check(audio->GetGUID(MF_MT_SUBTYPE,&subtype),"Read audio codec");
		require(subtype == MFAudioFormat_AAC,"MP4 must contain AAC audio");
		require(MFGetAttributeUINT32(audio.Get(),MF_MT_AUDIO_NUM_CHANNELS,0)==2,"AAC must be stereo");
		require(MFGetAttributeUINT32(audio.Get(),MF_MT_AUDIO_SAMPLES_PER_SECOND,0)==sample_rate,"AAC must be 48 kHz");
	} else require(audio_hr==MF_E_INVALIDSTREAMNUMBER,"Silent recording unexpectedly has an audio track");
	check(source->SetStreamSelection(all_streams,FALSE),"Deselect streams");
	check(source->SetStreamSelection(video_stream,TRUE),"Select video");
	ComPtr<IMFMediaType> rgb;
	check(MFCreateMediaType(&rgb),"RGB type");
	check(rgb->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"RGB major type");
	check(rgb->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32),"RGB subtype");
	check(source->SetCurrentMediaType(video_stream,nullptr,rgb.Get()),"Decode H.264");
	int count = 0;
	LONGLONG previous = -1, video_end = 0, video_change = -1;
	for(;;) {
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		ComPtr<IMFSample> sample;
		check(source->ReadSample(video_stream,0,nullptr,&flags,&timestamp,&sample),"Decode video sample");
		if(flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
		if(!sample) continue;
		require(timestamp > previous,"Video timestamps not monotonic"); previous = timestamp;
		LONGLONG duration = 0; check(sample->GetSampleDuration(&duration),"Video duration"); video_end=timestamp+duration;
		if(count==0 || timed_change) {
			ComPtr<IMFMediaBuffer> buffer;
			check(sample->ConvertToContiguousBuffer(&buffer),"Decoded video buffer");
			BYTE* bytes = nullptr; DWORD size = 0;
			check(buffer->Lock(&bytes,nullptr,&size),"Lock decoded video");
			// RGB32 with positive stride is bottom-up. Check red top/blue bottom.
			const bool colour = size >= 160*120*4 && bytes[(100*160+80)*4+2]>180 && bytes[(20*160+80)*4]>180;
			if(timed_change && video_change<0 && size>=160*120*4 && bytes[(100*160+80)*4+1]>180)
				video_change=timestamp;
			buffer->Unlock();
			if(count==0) require(colour,"Video orientation or RGB channels changed");
		}
		++count;
	}
	if(expected_frames >= 0) require(count==expected_frames,"Wrong number of video frames");
	require(count>0,"MP4 contains no video frames");
	require(std::llabs(video_end-count*10000000LL/fps)<10000,"Video timeline drift");
	if(has_audio) {
		source = reader(path);
		check(source->SetStreamSelection(all_streams,FALSE),"Deselect streams");
		check(source->SetStreamSelection(audio_stream,TRUE),"Select audio");
		ComPtr<IMFMediaType> pcm;
		check(MFCreateMediaType(&pcm),"PCM type");
		check(pcm->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio),"PCM major");
		check(pcm->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM),"PCM subtype");
		check(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16),"PCM bits");
		check(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2),"PCM channels");
		check(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,sample_rate),"PCM rate");
		check(source->SetCurrentMediaType(audio_stream,nullptr,pcm.Get()),"Decode AAC");
		std::vector<int16_t> decoded;
		previous = -1;
		LONGLONG audio_end = 0, audio_change = -1;
		for(;;) {
			DWORD flags = 0; LONGLONG timestamp = 0;
			ComPtr<IMFSample> sample;
			check(source->ReadSample(audio_stream,0,nullptr,&flags,&timestamp,&sample),"Decode audio sample");
			if(flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
			if(!sample) continue;
			require(timestamp>previous,"Audio timestamps not monotonic"); previous=timestamp;
			LONGLONG duration=0; check(sample->GetSampleDuration(&duration),"AAC duration"); audio_end=timestamp+duration;
			ComPtr<IMFMediaBuffer> buffer;
			check(sample->ConvertToContiguousBuffer(&buffer),"Decoded audio buffer");
			BYTE* bytes=nullptr; DWORD size=0;
			check(buffer->Lock(&bytes,nullptr,&size),"Lock decoded audio");
			const auto values=reinterpret_cast<int16_t*>(bytes);
			if(timed_change && audio_change<0) {
				for(DWORD i=0;i<size/4;++i) if(std::abs(static_cast<int>(values[i*2]))>2000) {
					audio_change=timestamp+static_cast<LONGLONG>(i)*10000000/sample_rate; break;
				}
			}
			decoded.insert(decoded.end(),values,values+size/sizeof(int16_t));
			buffer->Unlock();
		}
		require(!decoded.empty(),"No decoded AAC samples");
		// AAC frames are 1024 samples; allow encoder priming/padding at the ends.
		require(std::llabs(audio_end-video_end)<700000,"Audio/video duration mismatch exceeds AAC padding");
		std::printf("  duration: video %.3f s, audio %.3f s, delta %.3f ms\n",video_end/1.0e7,audio_end/1.0e7,(audio_end-video_end)/1.0e4);
		if(timed_change) {
			require(video_change>=0 && audio_change>=0,"Missing timed audio/video transition");
			require(std::llabs(video_change-audio_change)<800000,"Captured video/audio transition differs by more than 80 ms");
			std::printf("  capture sync: video transition %.3f s, audio onset %.3f s, delta %.3f ms\n",video_change/1.0e7,audio_change/1.0e7,(video_change-audio_change)/1.0e4);
		}
		if(tones) {
			const int start=sample_rate/3;
			const int n=std::min(sample_rate/3,static_cast<int>(decoded.size()/2)-start);
			require(n>1000,"Insufficient decoded tone");
			for(int channel=0;channel<2;++channel) {
				const double frequency=channel==0?440:880;
				double real=0,imag=0;
				for(int i=0;i<n;++i) {
					const double angle=6.283185307179586*frequency*i/sample_rate;
					real+=decoded[(start+i)*2+channel]*std::cos(angle);
					imag+=decoded[(start+i)*2+channel]*std::sin(angle);
				}
				require(std::sqrt(real*real+imag*imag)*2/n>1500,"Synthetic source missing from decoded AAC");
			}
		}
	}
	std::printf("PASS: decoded %s (%d H.264 frames%s)\n",path.toUtf8().constData(),count,has_audio?", AAC":" , silent");
}

static void synthetic(const QString& path, int fps) {
	Muxer mux(path,160,120,fps,1000000,true);
	Timeline mix;
	QImage image=frame();
	int64_t position=0;
	for(int v=0;v<fps;++v) {
		mux.video(image,v);
		const int64_t end=(v+1)*sample_rate/fps;
		while(position<end) {
			const int n=static_cast<int>(std::min<int64_t>(480,end-position));
			std::vector<int16_t> left(n*2,0),right(n*2,0);
			for(int i=0;i<n;++i) if(position+i>=4800) {
				left[i*2]=static_cast<int16_t>(5000*std::sin(6.283185307179586*440*(position+i)/sample_rate));
				right[i*2+1]=static_cast<int16_t>(6000*std::sin(6.283185307179586*880*(position+i)/sample_rate));
			}
			mix.add(position,left.data(),n); mix.add(position,right.data(),n);
			const auto pcm=mix.take(n);
			mux.audio(pcm.data(),n,position);
			position+=n;
		}
	}
	mux.finish(); mux.finish(); // Idempotent finalisation.
	throws([&]() { mux.video(image,fps); },"Invalid video");
}
static void waitFor(PhotoVideoRecorder& recorder) {
	QElapsedTimer timer; timer.start();
	while(!recorder.finished() && timer.elapsed()<15000) QThread::msleep(10);
	require(recorder.finished(),"Recorder shutdown exceeded 15 seconds");
}
static bool processSupported() {
	using Fn=LONG(WINAPI*)(OSVERSIONINFOW*);
	auto fn=reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
	OSVERSIONINFOW version{}; version.dwOSVersionInfoSize=sizeof(version);
	return fn && fn(&version)==0 && version.dwBuildNumber>=20348;
}
static void recorderTests(const QString& dir) {
	const auto image=frame();
	const QString silent=dir+"/legacy.mp4";
	{
		PhotoVideoRecorder recorder(silent,30,1000000,image); // Keep old four-argument API.
		QThread::msleep(450);
		recorder.submit(image.convertToFormat(QImage::Format_RGB16)); // Accept changing input formats.
		QThread::msleep(350);
		recorder.stop(); recorder.stop();
		recorder.submit(image);
		waitFor(recorder);
		require(recorder.error().isEmpty(),recorder.error().toUtf8().constData());
	}
	verify(silent,false,-1,30,false);
	{
		PhotoVideoRecorder recorder(dir+"/immediate.mp4",30,1000000,image,false,false);
		recorder.stop(); waitFor(recorder);
		require(recorder.error().isEmpty(),recorder.error().toUtf8().constData());
	}
	verify(dir+"/immediate.mp4",false,-1,30,false);
	{
		PhotoVideoRecorder recorder(dir+"/destructor.mp4",30,1000000,image);
		QThread::msleep(100); // Destructor must stop/join/finalise without explicit stop.
	}
	verify(dir+"/destructor.mp4",false,-1,30,false);
	{
		PhotoVideoRecorder invalid(dir+"/absent/output.mp4",30,1000000,image);
		waitFor(invalid); require(!invalid.error().isEmpty(),"Output failure was hidden");
	}
	throws([&]() { PhotoVideoRecorder invalid(dir+"/odd.mp4",30,1000000,QImage(161,120,QImage::Format_RGB32)); },"Invalid recording");
	if(!processSupported()) {
		PhotoVideoRecorder unsupported(dir+"/unsupported.mp4",30,1000000,image,false,true);
		waitFor(unsupported);
		require(unsupported.error().contains("20348") && unsupported.error().contains("desktop"),"Unsupported process capture did not fail explicitly");
		std::printf("PASS: unsupported OS: %s\n",unsupported.error().toUtf8().constData());
	}
	std::puts("PASS: legacy/four-argument API, frame formats, repeated stop, destructor, parameter and sink errors");
}

static void engineFeedTests(const QString& dir) {
	static_assert(noexcept(std::declval<PhotoVideoRecorder&>().submitWorldAudio(nullptr,0,48000)),"Engine callback must be noexcept");
	const auto path=dir+"/engine_feed.mp4";
	{
		PhotoVideoRecorder recorder(path,30,1000000,frame(),false,true,true);
		// Feed immediately after construction, before the encoder has initialised.
		// No capture object/device exists on this path, including on Windows 19045.
		const auto start=std::chrono::steady_clock::now();
		std::vector<float> pcm(480*2);
		for(int packet=0;packet<120;++packet) {
			std::this_thread::sleep_until(start+std::chrono::milliseconds(packet*10));
			if(packet==40) {
				QImage green(160,120,QImage::Format_RGB888); green.fill(Qt::green);
				recorder.submit(green);
			}
			for(int i=0;i<480;++i) {
				pcm[i*2]=packet<40?0.0f:static_cast<float>(0.2*std::sin(6.283185307179586*440*(packet*480+i)/sample_rate));
				pcm[i*2+1]=packet<40?0.0f:static_cast<float>(0.2*std::sin(6.283185307179586*880*(packet*480+i)/sample_rate));
			}
			recorder.submitWorldAudio(pcm.data(),480,48000);
			require(recorder.error().isEmpty(),recorder.error().toUtf8().constData());
		}
		std::this_thread::sleep_until(start+std::chrono::milliseconds(1200));
		// Owner synchronously detaches here, then stops. Post-stop calls are harmless.
		recorder.stop(); recorder.submitWorldAudio(nullptr,480,44100); waitFor(recorder);
		require(recorder.error().isEmpty(),recorder.error().toUtf8().constData());
	}
	verify(path,true,-1,30,true,true);
	{
		PhotoVideoRecorder bad(dir+"/bad_rate.mp4",30,1000000,frame(),false,true,true);
		const float zeros[2]={0,0}; bad.submitWorldAudio(zeros,1,44100); waitFor(bad);
		require(bad.error().contains("48000 Hz"),"Unsupported engine rate was hidden");
	}
	{
		PhotoVideoRecorder bad(dir+"/bad_packet.mp4",30,1000000,frame(),false,true,true);
		const float zeros[2]={0,0}; bad.submitWorldAudio(zeros,48001,48000); waitFor(bad);
		require(bad.error().contains("maximum 48000"),"Unbounded engine packet accepted");
	}
	{
		PhotoVideoRecorder bad(dir+"/nonfinite.mp4",30,1000000,frame(),false,true,true);
		const float invalid[2]={std::numeric_limits<float>::quiet_NaN(),0}; bad.submitWorldAudio(invalid,1,48000); waitFor(bad);
		require(bad.error().contains("non-finite"),"Non-finite engine sample accepted");
	}
	{
		PhotoVideoRecorder bad(dir+"/feed_overflow.mp4",30,1000000,frame(),false,true,true);
		std::vector<float> second(sample_rate*2,0.1f);
		for(int i=0;i<7;++i) bad.submitWorldAudio(second.data(),sample_rate,sample_rate);
		waitFor(bad); require(bad.error().contains("overflow"),"Unbounded engine feed queue accepted");
	}
	std::puts("PASS: immediate engine feed, device-free AAC, timed content sync, noexcept rate/packet/sample errors");
}

static void liveLoopback(const QString& path) {
	require(processSupported(),"Live process capture requires Windows build 20348+");
	WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=2;
	format.nSamplesPerSec=sample_rate; format.wBitsPerSample=16; format.nBlockAlign=4; format.nAvgBytesPerSec=sample_rate*4;
	HWAVEOUT output=nullptr;
	require(waveOutOpen(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR,"Synthetic playback unavailable");
	struct Close { HWAVEOUT value; ~Close() { waveOutReset(value); waveOutClose(value); } } close{output};
	std::vector<int16_t> pcm(sample_rate*3*2);
	for(int i=0;i<sample_rate*3;++i) {
		pcm[i*2]=static_cast<int16_t>(5000*std::sin(6.283185307179586*440*i/sample_rate));
		pcm[i*2+1]=static_cast<int16_t>(6000*std::sin(6.283185307179586*880*i/sample_rate));
	}
	WAVEHDR header{}; header.lpData=reinterpret_cast<LPSTR>(pcm.data()); header.dwBufferLength=static_cast<DWORD>(pcm.size()*2);
	require(waveOutPrepareHeader(output,&header,sizeof(header))==MMSYSERR_NOERROR,"Preparing synthetic playback failed");
	struct Unprepare { HWAVEOUT out; WAVEHDR* header; ~Unprepare() { waveOutReset(out); waveOutUnprepareHeader(out,header,sizeof(*header)); } } unprepare{output,&header};
	PhotoVideoRecorder recorder(path,30,1000000,frame(),false,true);
	require(waveOutWrite(output,&header,sizeof(header))==MMSYSERR_NOERROR,"Playing synthetic tone failed");
	QThread::msleep(1600); recorder.stop(); waitFor(recorder);
	require(recorder.error().isEmpty(),recorder.error().toUtf8().constData());
	verify(path,true,-1,30,true);
}

int main(int argc,char** argv) {
	QCoreApplication app(argc,argv);
	try {
		require(argc==2 || (argc==3 && std::string(argv[2])=="--loopback"),"Usage: audio_native <new-output-directory> [--loopback]");
		const QString dir=QString::fromLocal8Bit(argv[1]);
		require(!QFileInfo::exists(dir),"Refusing to overwrite an existing test output directory");
		require(QDir().mkpath(dir),"Cannot create test output directory");
		ComApartment apartment; MediaFoundation platform;
		timelineTests(); videoTimelineTests(); feedQueueTests();
		for(int fps : {30,29}) {
			const auto path=dir+QString("/synthetic_%1.mp4").arg(fps);
			synthetic(path,fps); verify(path,true,fps,fps,true);
		}
		recorderTests(dir);
		engineFeedTests(dir);
		if(argc==3) liveLoopback(dir+"/loopback.mp4");
		std::puts("PASS: all photo audio tests (microphone never opened)");
		return 0;
	} catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
