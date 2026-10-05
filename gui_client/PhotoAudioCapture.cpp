// The process-loopback declarations require a Windows 10.0.20348+ SDK.
// These macros affect this translation unit only, not the application's OS floor.
#if defined(_WIN32) && !defined(USE_SDL)
#include <sdkddkver.h>
#if !defined(NTDDI_WIN10_FE)
#error Photo audio capture requires Windows SDK 10.0.20348 or newer
#endif
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10_FE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "PhotoAudioCapture.h"
#include "PhotoAudioWindows.h"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/implements.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace PhotoAudio {
namespace {
// COM retains this agile callback until completion. All callback-visible state,
// including activation parameters and the event, belongs to it, not the recorder.
// A cancelled/timed-out recorder can therefore safely disappear first.
class Activation final : public Microsoft::WRL::RuntimeClass<
	Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
	IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
	Event completed{true};
	AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
	PROPVARIANT variant{};
	HRESULT result = E_PENDING;
	ComPtr<IAudioClient> client;

	Activation() {
		parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
		parameters.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
		parameters.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
		variant.vt = VT_BLOB;
		variant.blob.cbSize = sizeof(parameters);
		variant.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);
	}
	STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
		ComPtr<IUnknown> unknown;
		HRESULT activated = E_FAIL;
		result = operation->GetActivateResult(&activated, &unknown);
		if(SUCCEEDED(result)) result = activated;
		if(SUCCEEDED(result)) result = unknown ? unknown.As(&client) : E_NOINTERFACE;
		SetEvent(completed.handle);
		return S_OK;
	}
};

void requireProcessCaptureOS() {
	using GetVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
	const auto getVersion = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
	if(!getVersion || getVersion(&version) != 0 || version.dwMajorVersion < 10 || version.dwBuildNumber < 20348)
		throw std::runtime_error("Application audio requires Windows build 20348 or newer with process loopback; desktop capture is never used");
}

struct Source {
	// Destroy client before its event, and release every COM interface on this thread.
	Event event;
	ComPtr<IAudioClient> client;
	ComPtr<IAudioCaptureClient> capture;
	bool started = false, seen_packet = false;
	int64_t next_frame = -1;
	const char* name;
	explicit Source(const char* n) : name(n) {}
	~Source() { if(started) client->Stop(); }

	void initialise(bool loopback) {
		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = channels;
		format.nSamplesPerSec = sample_rate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = 4;
		format.nAvgBytesPerSec = sample_rate * 4;
		const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
			AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
		check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format, nullptr), "Initialising 48 kHz stereo PCM capture");
		check(client->SetEventHandle(event.handle), "Setting capture event");
		check(client->GetService(IID_PPV_ARGS(&capture)), "Opening WASAPI capture service");
	}
	void start() { check(client->Start(), "Starting WASAPI capture"); started = true; }
	void stop() {
		if(started) { check(client->Stop(), "Stopping WASAPI capture"); started = false; }
	}
	void drain(Timeline& timeline, int64_t epoch) {
		UINT32 available = 0;
		int drained = 0;
		for(;;) {
			check(capture->GetNextPacketSize(&available), "Reading WASAPI packet size (device may have disconnected)");
			if(!available) break;
			BYTE* bytes = nullptr;
			UINT32 frames = 0;
			DWORD flags = 0;
			UINT64 qpc = 0;
			check(capture->GetBuffer(&bytes, &frames, &flags, nullptr, &qpc), "Reading WASAPI audio");
			try {
				if(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
					throw std::runtime_error("Audio device returned an invalid capture timestamp");
				if(seen_packet && (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY))
					throw std::runtime_error("Audio capture lost samples (device discontinuity)");
				if(frames > capacity_frames || drained > capacity_frames - static_cast<int>(frames))
					throw std::runtime_error("WASAPI capture backlog exceeded the bounded audio buffer");
				int64_t first = (static_cast<int64_t>(qpc) - epoch) * sample_rate / 10000000;
				// QPC rounding can put adjacent packets a sample apart. Preserve larger
				// gaps (including time spent silent) and keep the clocks synchronised.
				if(next_frame >= 0 && first >= next_frame - 2 && first <= next_frame + 2) first = next_frame;
				if(!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
					timeline.add(first, reinterpret_cast<const int16_t*>(bytes), static_cast<int>(frames));
				next_frame = first + frames;
				seen_packet = true;
				drained += frames;
			} catch(...) { capture->ReleaseBuffer(frames); throw; }
			check(capture->ReleaseBuffer(frames), "Releasing WASAPI audio packet");
		}
	}
};
}

struct Capture::Impl {
	Timeline& timeline;
	const int64_t epoch;
	const std::atomic<bool>& external_stop;
	Event cancel{true};
	std::thread thread;
	mutable std::mutex mutex;
	std::string failure;

	Impl(Timeline& t, int64_t e, const std::atomic<bool>& s) : timeline(t), epoch(e), external_stop(s) {}
	bool cancelled() const { return external_stop.load() || WaitForSingleObject(cancel.handle, 0) == WAIT_OBJECT_0; }
	bool processClient(Source& source) {
		requireProcessCaptureOS();
		auto activation = Microsoft::WRL::Make<Activation>();
		if(!activation) throw std::bad_alloc();
		ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
		check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
			&activation->variant, activation.Get(), &operation), "Activating application-only loopback (no desktop fallback)");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for(;;) {
			if(cancelled()) return false;
			const DWORD wait = WaitForSingleObject(activation->completed.handle, 10);
			if(wait == WAIT_OBJECT_0) break;
			if(wait == WAIT_FAILED) check(HRESULT_FROM_WIN32(GetLastError()), "Waiting for process loopback activation");
			if(std::chrono::steady_clock::now() >= deadline)
				throw std::runtime_error("Application audio activation timed out; process capture is unavailable (no desktop fallback)");
		}
		check(activation->result, "Application-only audio capture unavailable (no desktop fallback)");
		source.client = activation->client;
		return true;
	}
	void run(bool microphone, bool world_audio) noexcept {
		const char* context = "Audio capture";
		try {
			ComApartment apartment;
			Source world("Application audio"), mic("Microphone");
			// Resolve process support before ever opening the microphone.
			if(cancelled()) return;
			if(world_audio) {
				context = world.name;
				if(!processClient(world)) return;
				world.initialise(true);
			}
			if(cancelled()) return;
			if(microphone) {
				context = mic.name;
				ComPtr<IMMDeviceEnumerator> enumerator;
				check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "Opening microphone device enumerator");
				ComPtr<IMMDevice> device;
				check(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device), "No default communications microphone is available");
				check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(mic.client.GetAddressOf())), "Microphone access failed; check Windows microphone privacy permission");
				mic.initialise(false);
			}
			if(cancelled()) return;
			if(world_audio) { context = world.name; world.start(); }
			if(microphone && !cancelled()) { context = mic.name; mic.start(); }
			HANDLE events[] = {cancel.handle, world.event.handle, mic.event.handle};
			while(!cancelled()) {
				const DWORD wait = WaitForMultipleObjects(3, events, FALSE, 5);
				if(wait == WAIT_FAILED) check(HRESULT_FROM_WIN32(GetLastError()), "Waiting for audio capture");
				if(world.started) { context = world.name; world.drain(timeline, epoch); }
				if(mic.started) { context = mic.name; mic.drain(timeline, epoch); }
			}
			// Stop both devices before draining: no new packets can arrive while
			// the encoder catches up and finalises the file after Record is released.
			if(world.started) { context = world.name; world.stop(); }
			if(mic.started) { context = mic.name; mic.stop(); }
			if(world.capture) { context = world.name; world.drain(timeline, epoch); }
			if(mic.capture) { context = mic.name; mic.drain(timeline, epoch); }
		} catch(const std::exception& e) {
			std::lock_guard<std::mutex> lock(mutex); failure = std::string(context) + ": " + e.what();
		} catch(...) {
			std::lock_guard<std::mutex> lock(mutex); failure = std::string(context) + ": unexpected capture failure";
		}
	}
	void join() { SetEvent(cancel.handle); if(thread.joinable()) thread.join(); }
	~Impl() { join(); }
};

Capture::Capture(Timeline& timeline, int64_t epoch, bool microphone, bool world_audio, const std::atomic<bool>& stop)
: impl(new Impl(timeline, epoch, stop)) {
	if(!microphone && !world_audio) throw std::runtime_error("No recording audio source selected");
	Impl* worker = impl.get();
	impl->thread = std::thread([worker, microphone, world_audio]() { worker->run(microphone, world_audio); });
}
Capture::~Capture() = default;
void Capture::checkError() const {
	std::lock_guard<std::mutex> lock(impl->mutex);
	if(!impl->failure.empty()) throw std::runtime_error(impl->failure);
}
void Capture::finish() { impl->join(); checkError(); }
}
#endif
