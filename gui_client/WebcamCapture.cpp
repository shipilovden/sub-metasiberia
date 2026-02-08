/*=====================================================================
WebcamCapture.cpp
-----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "WebcamCapture.h"
#include "GUIClient.h"
#include <opengl/ui/GLUIImage.h>
#include <opengl/OpenGLTexture.h>
#include <graphics/ImageMap.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>

#if defined(_WIN32)
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <dshow.h>
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

WebcamCapture::WebcamCapture()
:	gui_client(NULL),
	gl_ui(NULL),
	opengl_engine(NULL),
	enabled(false),
	webcam_image_widget(NULL),
	webcam_texture(NULL)
#if defined(_WIN32)
	, capture_source(NULL),
	source_reader(NULL),
	media_type(NULL),
	frame_width(640),
	frame_height(480),
	video_subtype(GUID_NULL),
	last_frame_time(0.0)
#endif
{}

WebcamCapture::~WebcamCapture()
{
	destroy();
}

void WebcamCapture::create(GUIClient* gui_client_, GLUIRef gl_ui_, Reference<OpenGLEngine>& opengl_engine_)
{
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	opengl_engine = opengl_engine_;
}

void WebcamCapture::destroy()
{
	setEnabled(false);
	
	if(webcam_image_widget.nonNull() && gl_ui.nonNull())
	{
		gl_ui->removeWidget(webcam_image_widget);
		webcam_image_widget = NULL;
	}

	gl_ui = NULL;
	opengl_engine = NULL;
	gui_client = NULL;
}

void WebcamCapture::setEnabled(bool enabled_)
{
	if(enabled == enabled_)
		return;

	enabled = enabled_;

	if(enabled)
	{
		startCapture();
	}
	else
	{
		stopCapture();
	}
}

void WebcamCapture::startCapture()
{
#if defined(_WIN32)
	try
	{
		// Initialize COM
		HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if(FAILED(hr) && hr != RPC_E_CHANGED_MODE)
		{
			conPrint("WebcamCapture: CoInitializeEx failed: " + toString((int64)hr));
			enabled = false;
			return;
		}

		// Initialize Media Foundation
		hr = MFStartup(MF_VERSION);
		if(FAILED(hr))
		{
			conPrint("WebcamCapture: MFStartup failed: " + toString((int64)hr));
			enabled = false;
			return;
		}

		// Create video capture device enumerator
		IMFAttributes* pAttributes = NULL;
		hr = MFCreateAttributes(&pAttributes, 1);
		if(SUCCEEDED(hr))
		{
			hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
		}

		IMFActivate** ppDevices = NULL;
		UINT32 count = 0;
		if(SUCCEEDED(hr))
		{
			hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
		}

		if(pAttributes)
			pAttributes->Release();

		if(FAILED(hr) || count == 0)
		{
			conPrint("WebcamCapture: No video capture devices found");
			if(ppDevices)
			{
				for(UINT32 i = 0; i < count; i++)
					ppDevices[i]->Release();
				CoTaskMemFree(ppDevices);
			}
			enabled = false;
			return;
		}

		// Use first available device
		IMFMediaSource* pSource = NULL;
		hr = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&pSource));

		// Release device enumerator
		for(UINT32 i = 0; i < count; i++)
			ppDevices[i]->Release();
		CoTaskMemFree(ppDevices);

		if(FAILED(hr))
		{
			conPrint("WebcamCapture: Failed to activate device: " + toString((int64)hr));
			enabled = false;
			return;
		}

		capture_source = pSource;

		// Create source reader
		IMFSourceReader* pReader = NULL;
		hr = MFCreateSourceReaderFromMediaSource(pSource, NULL, &pReader);
		if(FAILED(hr))
		{
			conPrint("WebcamCapture: Failed to create source reader: " + toString((int64)hr));
			pSource->Release();
			capture_source = NULL;
			enabled = false;
			return;
		}

		source_reader = pReader;

		// Try to get native media type from the source first
		IMFMediaType* pNativeType = NULL;
		hr = source_reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNativeType);
		
		// Try to set RGB32 format first
		IMFMediaType* pMediaType = NULL;
		hr = MFCreateMediaType(&pMediaType);
		if(SUCCEEDED(hr))
		{
			hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		}
		if(SUCCEEDED(hr))
		{
			hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		}
		if(SUCCEEDED(hr))
		{
			hr = pMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		}
		if(SUCCEEDED(hr) && pNativeType)
		{
			// Copy frame size from native type
			UINT32 width = 0, height = 0;
			if(SUCCEEDED(MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height)))
			{
				hr = MFSetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, width, height);
			}
		}
		if(SUCCEEDED(hr))
		{
			hr = source_reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pMediaType);
		}

		// If RGB32 failed, try YUY2 (most common webcam format)
		if(FAILED(hr))
		{
			conPrint("WebcamCapture: RGB32 not supported, trying YUY2...");
			if(pMediaType)
			{
				pMediaType->Release();
				pMediaType = NULL;
			}
			
			hr = MFCreateMediaType(&pMediaType);
			if(SUCCEEDED(hr))
			{
				hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			}
			if(SUCCEEDED(hr))
			{
				hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
			}
			if(SUCCEEDED(hr))
			{
				hr = pMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			}
			if(SUCCEEDED(hr) && pNativeType)
			{
				UINT32 width = 0, height = 0;
				if(SUCCEEDED(MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height)))
				{
					hr = MFSetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, width, height);
				}
			}
			if(SUCCEEDED(hr))
			{
				hr = source_reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pMediaType);
			}
		}

		// If still failed, use native format
		if(FAILED(hr))
		{
			conPrint("WebcamCapture: YUY2 not supported, using native format...");
			if(pMediaType)
			{
				pMediaType->Release();
				pMediaType = NULL;
			}
			pMediaType = pNativeType;
			if(pMediaType)
				pMediaType->AddRef();
		}

		if(pNativeType)
			pNativeType->Release();

		if(!pMediaType)
		{
			conPrint("WebcamCapture: Failed to get any media type");
			source_reader->Release();
			source_reader = NULL;
			capture_source->Release();
			capture_source = NULL;
			enabled = false;
			return;
		}

		// Get actual media type that was set
		IMFMediaType* pActualType = NULL;
		hr = source_reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pActualType);
		if(SUCCEEDED(hr) && pActualType)
		{
			if(pMediaType != pActualType)
			{
				if(pMediaType)
					pMediaType->Release();
				pMediaType = pActualType;
			}
			else
			{
				pActualType->Release();
			}
		}

		media_type = pMediaType;

		// Get frame dimensions
		UINT32 width = 0, height = 0;
		hr = MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &width, &height);
		if(SUCCEEDED(hr))
		{
			frame_width = (int)width;
			frame_height = (int)height;
		}
		else
		{
			frame_width = 640;
			frame_height = 480;
		}

		// Get video subtype (format)
		hr = media_type->GetGUID(MF_MT_SUBTYPE, &video_subtype);
		if(FAILED(hr))
		{
			video_subtype = GUID_NULL;
		}

		// Log format info
		std::string format_name = "Unknown";
		if(IsEqualGUID(video_subtype, MFVideoFormat_RGB32))
			format_name = "RGB32";
		else if(IsEqualGUID(video_subtype, MFVideoFormat_YUY2))
			format_name = "YUY2";
		else if(IsEqualGUID(video_subtype, MFVideoFormat_NV12))
			format_name = "NV12";
		else if(IsEqualGUID(video_subtype, MFVideoFormat_MJPG))
			format_name = "MJPG";

		conPrint("WebcamCapture: Webcam started successfully, resolution: " + toString(frame_width) + "x" + toString(frame_height) + ", format: " + format_name);

		// Create texture for webcam feed
		if(opengl_engine.nonNull())
		{
			TextureParams tex_params;
			tex_params.use_sRGB = false;
			tex_params.allow_compression = false;
			tex_params.filtering = OpenGLTexture::Filtering_Bilinear;
			tex_params.wrapping = OpenGLTexture::Wrapping_Clamp;

			// Create empty texture
			webcam_texture = new OpenGLTexture(frame_width, frame_height, opengl_engine.ptr(),
				ArrayRef<uint8>(NULL, 0),
				OpenGLTextureFormat::Format_RGB_Linear_Uint8,
				tex_params.filtering,
				tex_params.wrapping,
				/*has_mipmaps=*/false
			);
		}

		// Don't create GLUIImage widget for SDL version - webcam is only displayed in Qt UI
		// The black square was appearing because of this widget
		// For Qt version, webcam is displayed in QLabel in MainWindow
	}
	catch(glare::Exception& e)
	{
		conPrint("WebcamCapture: Exception: " + e.what());
		enabled = false;
	}
#else
	conPrint("WebcamCapture: Webcam capture not implemented for this platform");
	enabled = false;
#endif
}

void WebcamCapture::stopCapture()
{
#if defined(_WIN32)
	if(source_reader)
	{
		source_reader->Release();
		source_reader = NULL;
	}

	if(media_type)
	{
		media_type->Release();
		media_type = NULL;
	}

	if(capture_source)
	{
		capture_source->Shutdown();
		capture_source->Release();
		capture_source = NULL;
	}

	MFShutdown();

	// Don't hide GLUIImage widget - we don't create it anymore

	webcam_texture = NULL;
	frame_buffer = NULL;

	conPrint("WebcamCapture: Webcam stopped");
#endif
}

void WebcamCapture::update()
{
	if(!enabled || !gl_ui.nonNull() || !opengl_engine.nonNull())
		return;

#if defined(_WIN32)
	// Update frame periodically (target ~30 FPS)
	const double current_time = frame_timer.elapsed();
	if(current_time - last_frame_time < 1.0 / 30.0)
		return;

	last_frame_time = current_time;
	updateFrame();
#endif
}

void WebcamCapture::updateFrame()
{
#if defined(_WIN32)
	if(!source_reader || !webcam_texture.nonNull() || !opengl_engine.nonNull())
		return;

	try
	{
		IMFSample* pSample = NULL;
		DWORD streamFlags = 0;
		LONGLONG timestamp = 0;

		// Read a sample from the source reader
		HRESULT hr = source_reader->ReadSample(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
			0,
			NULL,
			&streamFlags,
			&timestamp,
			&pSample
		);

		if(FAILED(hr))
		{
			// Not an error, just no frame available yet
			return;
		}

		if(streamFlags & MF_SOURCE_READERF_STREAMTICK)
		{
			// Stream tick, no sample
			if(pSample)
				pSample->Release();
			return;
		}

		if(!pSample)
			return;

		// Get the media buffer from the sample
		IMFMediaBuffer* pBuffer = NULL;
		hr = pSample->ConvertToContiguousBuffer(&pBuffer);
		if(FAILED(hr))
		{
			pSample->Release();
			return;
		}

		// Lock the buffer and get pointer to data
		BYTE* pData = NULL;
		DWORD cbData = 0;
		hr = pBuffer->Lock(&pData, NULL, &cbData);
		if(SUCCEEDED(hr))
		{
			// Create or update frame buffer
			if(frame_buffer.isNull() || frame_buffer->getMapWidth() != (size_t)frame_width || frame_buffer->getMapHeight() != (size_t)frame_height)
			{
				frame_buffer = new ImageMap<uint8, UInt8ComponentValueTraits>(frame_width, frame_height, 3);
			}

			uint8* dest_data = frame_buffer->getData();

			// Convert based on video format
			if(IsEqualGUID(video_subtype, MFVideoFormat_RGB32))
			{
				// RGB32 is BGRA format, convert to RGB
				const size_t stride = frame_width * 4; // RGB32 = 4 bytes per pixel
				for(int y = 0; y < frame_height; y++)
				{
					const BYTE* src_row = pData + y * stride; // Read in normal order
					uint8* dest_row = dest_data + y * frame_width * 3; // Write in normal order (no flip)
					for(int x = 0; x < frame_width; x++)
					{
						// Convert BGRA to RGB
						dest_row[x * 3 + 0] = src_row[x * 4 + 2]; // R
						dest_row[x * 3 + 1] = src_row[x * 4 + 1]; // G
						dest_row[x * 3 + 2] = src_row[x * 4 + 0]; // B
					}
				}
			}
			else if(IsEqualGUID(video_subtype, MFVideoFormat_YUY2))
			{
				// YUY2 format: YUYVYUYV... (2 pixels per 4 bytes)
				// Convert YUY2 to RGB
				for(int y = 0; y < frame_height; y++)
				{
					const BYTE* src_row = pData + y * frame_width * 2; // YUY2 = 2 bytes per pixel
					uint8* dest_row = dest_data + y * frame_width * 3; // Write in normal order (no flip)
					for(int x = 0; x < frame_width; x += 2)
					{
						// YUY2: Y0 U Y1 V (2 pixels)
						int Y0 = src_row[x * 2 + 0];
						int U  = src_row[x * 2 + 1];
						int Y1 = src_row[x * 2 + 2];
						int V  = src_row[x * 2 + 3];

						// Convert YUV to RGB for first pixel
						int C = Y0 - 16;
						int D = U - 128;
						int E = V - 128;
						int R1 = (298 * C + 409 * E + 128) >> 8;
						int G1 = (298 * C - 100 * D - 208 * E + 128) >> 8;
						int B1 = (298 * C + 516 * D + 128) >> 8;
						dest_row[x * 3 + 0] = (uint8)myClamp(R1, 0, 255);
						dest_row[x * 3 + 1] = (uint8)myClamp(G1, 0, 255);
						dest_row[x * 3 + 2] = (uint8)myClamp(B1, 0, 255);

						// Convert YUV to RGB for second pixel (if not last pixel)
						if(x + 1 < frame_width)
						{
							C = Y1 - 16;
							int R2 = (298 * C + 409 * E + 128) >> 8;
							int G2 = (298 * C - 100 * D - 208 * E + 128) >> 8;
							int B2 = (298 * C + 516 * D + 128) >> 8;
							dest_row[(x + 1) * 3 + 0] = (uint8)myClamp(R2, 0, 255);
							dest_row[(x + 1) * 3 + 1] = (uint8)myClamp(G2, 0, 255);
							dest_row[(x + 1) * 3 + 2] = (uint8)myClamp(B2, 0, 255);
						}
					}
				}
			}
			else
			{
				// Unknown format - try to use as RGB32 anyway (may not work)
				conPrint("WebcamCapture: Warning - unsupported video format, attempting RGB32 conversion");
				const size_t stride = frame_width * 4;
				for(int y = 0; y < frame_height; y++)
				{
					const BYTE* src_row = pData + y * stride; // Read in normal order
					uint8* dest_row = dest_data + y * frame_width * 3; // Write in normal order (no flip)
					for(int x = 0; x < frame_width; x++)
					{
						dest_row[x * 3 + 0] = src_row[x * 4 + 2];
						dest_row[x * 3 + 1] = src_row[x * 4 + 1];
						dest_row[x * 3 + 2] = src_row[x * 4 + 0];
					}
				}
			}

			// Update OpenGL texture
			if(webcam_texture.nonNull() && frame_buffer.nonNull())
			{
				const size_t row_stride = frame_width * 3;
				webcam_texture->loadIntoExistingTexture(
					0, // mipmap level
					frame_width,
					frame_height,
					row_stride,
					ArrayRef<uint8>(frame_buffer->getData(), frame_width * frame_height * 3),
					/*bind_needed=*/true
				);
			}

			pBuffer->Unlock();
		}

		pBuffer->Release();
		pSample->Release();
	}
	catch(glare::Exception& e)
	{
		conPrint("WebcamCapture::updateFrame: Exception: " + e.what());
	}
	catch(...)
	{
		conPrint("WebcamCapture::updateFrame: Unknown exception");
	}
#endif
}

#if defined(_WIN32) && !defined(EMSCRIPTEN) && !defined(USE_SDL)
#include <QtGui/QImage>
void* WebcamCapture::getCurrentFrameAsQImage() const
{
	if(frame_buffer.isNull() || frame_width <= 0 || frame_height <= 0)
		return nullptr;

	// Convert ImageMap (RGB) to QImage
	static thread_local ::QImage qimg;
	qimg = ::QImage(frame_width, frame_height, ::QImage::Format_RGB888);
	const uint8* src_data = frame_buffer->getData();
	for(int y = 0; y < frame_height; y++)
	{
		uint8* dest_row = qimg.scanLine(y);
		const uint8* src_row = src_data + y * frame_width * 3;
		memcpy(dest_row, src_row, frame_width * 3);
	}

	return &qimg;
}
#endif