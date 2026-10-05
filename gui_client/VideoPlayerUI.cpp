#include "VideoPlayerUI.h"
#include <graphics/ImageMap.h>
#include <graphics/SRGBUtils.h>
#include <utils/StringUtils.h>
#include <cmath>

namespace
{
// All player layers are nearer than the world HUD, within the [-1, 1] clip range.
const float SCRIM_Z = -0.99994f;
const float TRACK_Z = -0.99995f;
const float FILL_Z = -0.99996f;
const float CONTROL_Z = -0.99997f;

std::string timeString(double seconds)
{
	const int total = (int)myClamp(seconds, 0.0, 359999.0);
	const auto two = [](int n) { return (n < 10 ? "0" : "") + toString(n); };
	return (total >= 3600 ? toString(total / 3600) + ":" + two(total / 60 % 60) : toString(total / 60)) + ":" + two(total % 60);
}
}

GLUIImageRef VideoPlayerUI::makeImage(const std::string& texture, float z)
{
	GLUIImageRef image = new GLUIImage(ui, engine, "", "", z);
	image->overlay_ob->material.tex_translation = Vec2f(0.f, 1.f);
	if(!texture.empty())
	{
		TextureParams params;
		params.allow_compression = false;
		params.wrapping = OpenGLTexture::Wrapping_Clamp;
		image->overlay_ob->material.albedo_texture = engine->getTexture(texture, params);
	}
	return image;
}

VideoPlayerUI::VideoPlayerUI(GLUI& ui_, Reference<OpenGLEngine>& engine_, const std::string& resources)
: ui(ui_), engine(engine_)
{
	const std::string icons = resources + "/icons/lucide/player/";
	TextureParams params;
	params.allow_compression = false;
	params.wrapping = OpenGLTexture::Wrapping_Clamp;
	play_icon = engine->getTexture(icons + "play.png", params);
	pause_icon = engine->getTexture(icons + "pause.png", params);
	sound_icon = engine->getTexture(icons + "volume-2.png", params);
	muted_icon = engine->getTexture(icons + "volume-x.png", params);
	play = makeImage("", CONTROL_Z);
	mute = makeImage("", CONTROL_Z);
	close = makeImage(icons + "minimize-2.png", CONTROL_Z);
	scrim = makeImage("", SCRIM_Z);

	// White alpha mask: a smooth black fade into the bottom of the video.
	ImageMapUInt8Ref gradient = new ImageMapUInt8(1, 128, 4);
	for(size_t y = 0; y < 128; ++y)
	{
		uint8* p = gradient->getPixel(0, y);
		p[0] = p[1] = p[2] = 255;
		p[3] = (uint8)(235 * std::pow(y / 127.0, 0.65));
	}
	scrim->overlay_ob->material.albedo_texture = engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__video_player_scrim"), *gradient, params);
	scrim->setColour(Colour3f(0.f));

	ImageMapUInt8Ref disc = new ImageMapUInt8(48, 48, 4);
	for(size_t y = 0; y < 48; ++y)
	for(size_t x = 0; x < 48; ++x)
	{
		uint8* p = disc->getPixel(x, y);
		p[0] = p[1] = p[2] = 255;
		const double distance = std::hypot((double)x - 23.5, (double)y - 23.5);
		p[3] = (uint8)(255 * myClamp(23.5 - distance, 0.0, 1.0));
	}
	const OpenGLTextureRef disc_texture = engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__video_player_disc"), *disc, params);
	for(Slider* s : { &seek, &loudness })
	{
		s->track = makeImage("", TRACK_Z);
		s->track->setColour(toLinearSRGB(Colour3f(0.55f)));
		s->track->setAlpha(0.65f);
		s->fill = makeImage("", FILL_Z);
		s->fill->setColour(Colour3f(1.f));
		s->knob = makeImage("", CONTROL_Z);
		s->knob->overlay_ob->material.albedo_texture = disc_texture;
	}
	GLUITextView::CreateArgs text_args;
	text_args.font_size_px = 13;
	text_args.padding_px = 0;
	text_args.background_alpha = 0;
	text_args.text_colour = Colour3f(1.f);
	text_args.text_selectable = false;
	text_args.z = CONTROL_Z;
	time = new GLUITextView(ui, engine, "0:00 / --:--", Vec2f(0.f), text_args);
	layout(Rect2f(Vec2f(-1.f, -0.5f), Vec2f(1.f, 0.5f)));
	setState(false, false, 1.f, 0, 0);
}

void VideoPlayerUI::layout(const Rect2f& video_rect)
{
	// Scale down only for very small viewports; all hit areas stay aligned with rendering.
	const float px = myMin(ui.getUIWidthForDevIndepPixelWidth(1.f), video_rect.getWidths().x / 360.f);
	const float left = video_rect.getMin().x, bottom = video_rect.getMin().y, width = video_rect.getWidths().x;
	const float height = myMin(100.f * px, video_rect.getWidths().y);
	panel = Rect2f(Vec2f(left, bottom), Vec2f(left + width, bottom + height));
	scrim->setPosAndDims(panel.getMin(), panel.getWidths());
	const float margin = 16.f * px, button = 40.f * px;
	const float row_y = bottom + 8.f * px;
	play->setPosAndDims(Vec2f(left + margin - 8.f * px, row_y), Vec2f(button));
	mute->setPosAndDims(Vec2f(left + margin + button, row_y), Vec2f(button));
	close->setPosAndDims(Vec2f(left + width - margin - button + 8.f * px, row_y), Vec2f(button));
	loudness.hit = Rect2f(Vec2f(left + margin + 2 * button + 4.f * px, row_y + 6.f * px),
		Vec2f(left + margin + 2 * button + 84.f * px, row_y + 34.f * px));
	seek.hit = Rect2f(Vec2f(left + margin, bottom + 49.f * px), Vec2f(left + width - margin, bottom + 73.f * px));
	time->setPos(Vec2f(loudness.hit.getMax().x + 16.f * px, row_y + 15.f * px));
	time->setVisible(width >= 420.f * ui.getUIWidthForDevIndepPixelWidth(1.f));
	updateSlider(seek);
	updateSlider(loudness);
}

void VideoPlayerUI::updateSlider(Slider& s)
{
	const float px = myMin(ui.getUIWidthForDevIndepPixelWidth(1.f), panel.getWidths().x / 360.f);
	const float x = s.hit.getMin().x, width = s.hit.getWidths().x;
	const float y = (s.hit.getMin().y + s.hit.getMax().y) * 0.5f;
	const float radius = 5.f * px;
	s.track->setPosAndDims(Vec2f(x, y - 1.5f * px), Vec2f(width, 3.f * px));
	s.fill->setPosAndDims(Vec2f(x, y - 1.5f * px), Vec2f(width * (float)s.value, 3.f * px));
	s.knob->setPosAndDims(Vec2f(x + width * (float)s.value - radius, y - radius), Vec2f(2 * radius));
}

void VideoPlayerUI::setState(bool paused, bool muted, float volume_, double position_, double duration_)
{
	play->overlay_ob->material.albedo_texture = paused ? play_icon : pause_icon;
	mute->overlay_ob->material.albedo_texture = (muted || volume_ <= 0) ? muted_icon : sound_icon;
	position = position_;
	duration = duration_;
	volume = volume_;
	if(dragging != Seek && duration > 0)
		seek.value = myClamp(position / duration, 0.0, 1.0);
	if(dragging != Volume)
		loudness.value = muted ? 0 : volume;
	updateSlider(seek);
	updateSlider(loudness);
	updateTime();
}

void VideoPlayerUI::updateTime()
{
	const std::string label = timeString(dragging == Seek ? seek.value * duration : position) + " / " + (duration > 0 ? timeString(duration) : "--:--");
	if(label != time_text)
	{
		time_text = label;
		time->setText(ui, label);
	}
}

double VideoPlayerUI::valueAt(const Slider& s, const Vec2f& point) const
{
	return s.hit.getWidths().x > 0 ? myClamp((double)(point.x - s.hit.getMin().x) / s.hit.getWidths().x, 0.0, 1.0) : 0;
}

bool VideoPlayerUI::contains(const Vec2f& gl_coords) const
{
	return panel.inClosedRectangle(ui.UICoordsForOpenGLCoords(gl_coords));
}

VideoPlayerUI::Action VideoPlayerUI::mousePressed(MouseEvent& event)
{
	if(event.button != MouseButton::Left)
		return Action();
	const Vec2f p = ui.UICoordsForOpenGLCoords(event.gl_coords);
	if(play->getRect().inClosedRectangle(p)) return Action(TogglePlay);
	if(mute->getRect().inClosedRectangle(p)) return Action(ToggleMute);
	if(close->getRect().inClosedRectangle(p)) return Action(Close);
	if(seek.hit.inClosedRectangle(p))
	{
		dragging = Seek;
		seek.value = valueAt(seek, p);
		updateSlider(seek);
		updateTime();
	}
	else if(loudness.hit.inClosedRectangle(p))
	{
		dragging = Volume;
		loudness.value = valueAt(loudness, p);
		updateSlider(loudness);
		return Action(Volume, loudness.value);
	}
	return Action();
}

VideoPlayerUI::Action VideoPlayerUI::mouseMoved(MouseEvent& event)
{
	const Vec2f p = ui.UICoordsForOpenGLCoords(event.gl_coords);
	for(GLUIImage* icon : { play.ptr(), mute.ptr(), close.ptr() })
		icon->setColour(icon->getRect().inClosedRectangle(p) ? toLinearSRGB(Colour3f(0.45f, 0.8f, 1.f)) : Colour3f(1.f));
	if(dragging == Seek)
	{
		seek.value = valueAt(seek, p);
		updateSlider(seek);
		updateTime();
	}
	else if(dragging == Volume)
	{
		loudness.value = valueAt(loudness, p);
		updateSlider(loudness);
		return Action(Volume, loudness.value);
	}
	return Action();
}

VideoPlayerUI::Action VideoPlayerUI::mouseReleased(MouseEvent& event)
{
	if(event.button != MouseButton::Left) return Action();
	const Command command = dragging;
	mouseMoved(event); // Include the final cursor position, even outside the track.
	dragging = None;
	return Action(command, command == Seek ? seek.value : loudness.value);
}

VideoPlayerUI::Action VideoPlayerUI::wheel(MouseWheelEvent& event)
{
	const Vec2f p = ui.UICoordsForOpenGLCoords(event.gl_coords);
	if(loudness.hit.inClosedRectangle(p) || mute->getRect().inClosedRectangle(p))
		return Action(Volume, myClamp(loudness.value + event.angle_delta.y / 120.0 * 0.05, 0.0, 1.0));
	return Action();
}
