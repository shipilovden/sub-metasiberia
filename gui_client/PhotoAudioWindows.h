#pragma once
#if defined(_WIN32) && !defined(USE_SDL)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
// glare-core/utils/WeakReference.h shadows the SDK's weakreference.h on the
// case-insensitive Windows include path. Use the SDK's explicit winrt sibling
// path so COM's IWeakReference exists before any MF/WRL header is parsed.
#include <../winrt/weakreference.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace PhotoAudio {
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

inline void check(HRESULT hr, const char* operation) {
	if(FAILED(hr)) {
		char code[32]; std::snprintf(code, sizeof(code), " (HRESULT 0x%08lX)", static_cast<unsigned long>(hr));
		throw std::runtime_error(std::string(operation) + code);
	}
}
struct ComApartment {
	ComApartment() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Recording COM initialisation"); }
	~ComApartment() { CoUninitialize(); }
	ComApartment(const ComApartment&) = delete;
	ComApartment& operator=(const ComApartment&) = delete;
};
struct MediaFoundation {
	MediaFoundation() { check(MFStartup(MF_VERSION), "Recording Media Foundation initialisation"); }
	~MediaFoundation() { MFShutdown(); }
	MediaFoundation(const MediaFoundation&) = delete;
	MediaFoundation& operator=(const MediaFoundation&) = delete;
};
struct Event {
	HANDLE handle;
	explicit Event(bool manual = false) : handle(CreateEventW(nullptr, manual, FALSE, nullptr)) {
		if(!handle) check(HRESULT_FROM_WIN32(GetLastError()), "Creating audio event");
	}
	~Event() { CloseHandle(handle); }
	Event(const Event&) = delete;
	Event& operator=(const Event&) = delete;
};
// WASAPI GetBuffer's QPC timestamp is already in 100-ns units.
inline int64_t clock100ns() {
	LARGE_INTEGER ticks, frequency;
	QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&frequency);
	return (ticks.QuadPart / frequency.QuadPart) * 10000000 +
		(ticks.QuadPart % frequency.QuadPart) * 10000000 / frequency.QuadPart;
}
}
#endif
