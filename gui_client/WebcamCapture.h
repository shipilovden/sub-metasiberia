/*=====================================================================
WebcamCapture.h
---------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once

#include <utils/Reference.h>
#include <utils/Timer.h>
#include <maths/vec2.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUI.h>
#include <opengl/OpenGLEngine.h>
#include <string>

#if defined(_WIN32)
// Forward declarations for Windows Media Foundation types
struct IMFMediaSource;
struct IMFSourceReader;
struct IMFMediaType;
#endif

class GUIClient;

/*=====================================================================
WebcamCapture
-------------
Captures video from webcam and displays it in a GLUIImage widget.
=====================================================================*/
class WebcamCapture
{
public:
	WebcamCapture();
	~WebcamCapture();

	void create(GUIClient* gui_client_, GLUIRef gl_ui_, Reference<OpenGLEngine>& opengl_engine_);
	void destroy();

	void setEnabled(bool enabled);
	bool isEnabled() const { return enabled; }

	void update(); // Call this periodically to update the video frame

private:
	void startCapture();
	void stopCapture();
	void updateFrame();

	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;

	bool enabled;
	GLUIImageRef webcam_image_widget;
	Reference<OpenGLTexture> webcam_texture;

#if defined(_WIN32)
	IMFMediaSource* capture_source;
	IMFSourceReader* source_reader;
	IMFMediaType* media_type;
	int frame_width;
	int frame_height;
	Timer frame_timer;
	double last_frame_time;
	Reference<ImageMap<uint8, UInt8ComponentValueTraits>> frame_buffer;
#endif
};
