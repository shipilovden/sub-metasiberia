#pragma once

#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUI.h>

// A modal, backend-independent overlay. Input is dispatched explicitly by the
// fullscreen viewer, never through the world's GLUI widgets.
class VideoPlayerUI : public RefCounted
{
public:
	enum Command { None, TogglePlay, ToggleMute, Seek, Volume, Close };
	struct Action
	{
		Action(Command command_ = None, double value_ = 0) : command(command_), value(value_) {}
		Command command;
		double value;
	};

	VideoPlayerUI(GLUI& ui, Reference<OpenGLEngine>& engine, const std::string& resources);
	void layout(const Rect2f& video_rect);
	void setState(bool paused, bool muted, float volume, double position, double duration);
	Action mousePressed(MouseEvent& event);
	Action mouseMoved(MouseEvent& event);
	Action mouseReleased(MouseEvent& event);
	Action wheel(MouseWheelEvent& event);
	bool contains(const Vec2f& gl_coords) const;
	double getDuration() const { return duration; }
	double getPosition() const { return position; }
	float getVolume() const { return volume; }

private:
	struct Slider
	{
		Rect2f hit;
		GLUIImageRef track, fill, knob;
		double value = 0;
	};
	GLUIImageRef makeImage(const std::string& texture, float z);
	void updateSlider(Slider& slider);
	double valueAt(const Slider& slider, const Vec2f& point) const;
	void updateTime();
	GLUI& ui;
	Reference<OpenGLEngine> engine;
	GLUIImageRef scrim, play, mute, close;
	GLUITextViewRef time;
	OpenGLTextureRef play_icon, pause_icon, sound_icon, muted_icon;
	Slider seek, loudness;
	Rect2f panel;
	Command dragging = None;
	float volume = 1;
	double position = 0, duration = 0;
	std::string time_text;
};
