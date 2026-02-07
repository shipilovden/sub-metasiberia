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

		// Configure source reader to output RGB32 format
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
		if(SUCCEEDED(hr))
		{
			hr = source_reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pMediaType);
		}

		if(FAILED(hr))
		{
			conPrint("WebcamCapture: Failed to set media type: " + toString((int64)hr));
			if(pMediaType)
				pMediaType->Release();
			source_reader->Release();
			source_reader = NULL;
			capture_source->Release();
			capture_source = NULL;
			enabled = false;
			return;
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

		conPrint("WebcamCapture: Webcam started successfully, resolution: " + toString(frame_width) + "x" + toString(frame_height));

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

		// Create GLUIImage widget for displaying webcam feed
		if(gl_ui.nonNull() && opengl_engine.nonNull() && webcam_image_widget.isNull())
		{
			const float widget_size = gl_ui->getUIWidthForDevIndepPixelWidth(320); // 320x240 pixels
			const float aspect_ratio = (float)frame_height / (float)frame_width;
			webcam_image_widget = new GLUIImage(*gl_ui, opengl_engine, "", Vec2f(0.5f - widget_size/2, 0.5f - widget_size * aspect_ratio / 2), Vec2f(widget_size, widget_size * aspect_ratio), "Веб-камера");
			webcam_image_widget->setColour(Colour3f(1.0f));
			webcam_image_widget->setAlpha(1.0f);
			
			// Set texture
			if(webcam_texture.nonNull())
			{
				webcam_image_widget->overlay_ob->material.albedo_texture = webcam_texture;
			}
			
			gl_ui->addWidget(webcam_image_widget);
		}
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

	if(webcam_image_widget.nonNull() && gl_ui.nonNull())
	{
		webcam_image_widget->setVisible(false);
	}

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
	if(!source_reader || !webcam_image_widget.nonNull() || !webcam_texture.nonNull() || !opengl_engine.nonNull())
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

			// Copy RGB32 data to ImageMap (RGB32 is BGRA format, we need RGB)
			const size_t stride = frame_width * 4; // RGB32 = 4 bytes per pixel
			uint8* dest_data = frame_buffer->getData();
			for(int y = 0; y < frame_height; y++)
			{
				const BYTE* src_row = pData + (frame_height - 1 - y) * stride; // Flip vertically
				uint8* dest_row = dest_data + y * frame_width * 3;
				for(int x = 0; x < frame_width; x++)
				{
					// Convert BGRA to RGB
					dest_row[x * 3 + 0] = src_row[x * 4 + 2]; // R
					dest_row[x * 3 + 1] = src_row[x * 4 + 1]; // G
					dest_row[x * 3 + 2] = src_row[x * 4 + 0]; // B
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

		// Show widget if not visible
		if(!webcam_image_widget->isVisible())
		{
			webcam_image_widget->setVisible(true);
		}
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
