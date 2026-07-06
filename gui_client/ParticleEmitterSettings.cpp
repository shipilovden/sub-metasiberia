/*=====================================================================
ParticleEmitterSettings.cpp
---------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ParticleEmitterSettings.h"


#include <JSONParser.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>


namespace
{
static const char* const PARTICLE_EMITTER_MARKER = "metasiberia_particle_emitter_v1";


double clampDouble(const double v, const double min_v, const double max_v)
{
	return std::max(min_v, std::min(max_v, v));
}


int clampInt(const int v, const int min_v, const int max_v)
{
	return std::max(min_v, std::min(max_v, v));
}


const char* kindToString(const ParticleEmitterSettings::ParticleKind kind)
{
	switch(kind)
	{
	case ParticleEmitterSettings::ParticleKind_Smoke:     return "smoke";
	case ParticleEmitterSettings::ParticleKind_Foam:      return "foam";
	case ParticleEmitterSettings::ParticleKind_Spark:     return "spark";
	case ParticleEmitterSettings::ParticleKind_Streak:    return "streak";
	case ParticleEmitterSettings::ParticleKind_Star:      return "star";
	case ParticleEmitterSettings::ParticleKind_Ring:      return "ring";
	case ParticleEmitterSettings::ParticleKind_Nebula:    return "nebula";
	case ParticleEmitterSettings::ParticleKind_Flame:     return "flame";
	case ParticleEmitterSettings::ParticleKind_Snowflake: return "snowflake";
	case ParticleEmitterSettings::ParticleKind_SoftDisc:  return "soft_disc";
	}
	return "smoke";
}


ParticleEmitterSettings::ParticleKind kindFromString(const std::string& s)
{
	if(s == "foam")
		return ParticleEmitterSettings::ParticleKind_Foam;
	if(s == "spark")
		return ParticleEmitterSettings::ParticleKind_Spark;
	if(s == "streak")
		return ParticleEmitterSettings::ParticleKind_Streak;
	if(s == "star")
		return ParticleEmitterSettings::ParticleKind_Star;
	if(s == "ring")
		return ParticleEmitterSettings::ParticleKind_Ring;
	if(s == "nebula")
		return ParticleEmitterSettings::ParticleKind_Nebula;
	if(s == "flame")
		return ParticleEmitterSettings::ParticleKind_Flame;
	if(s == "snowflake")
		return ParticleEmitterSettings::ParticleKind_Snowflake;
	if(s == "soft_disc")
		return ParticleEmitterSettings::ParticleKind_SoftDisc;
	return ParticleEmitterSettings::ParticleKind_Smoke;
}


const char* directionToString(const ParticleEmitterSettings::Direction direction)
{
	switch(direction)
	{
	case ParticleEmitterSettings::Direction_Up:      return "up";
	case ParticleEmitterSettings::Direction_Forward: return "forward";
	case ParticleEmitterSettings::Direction_Down:    return "down";
	case ParticleEmitterSettings::Direction_Random:  return "random";
	case ParticleEmitterSettings::Direction_Custom:  return "custom";
	}
	return "up";
}


ParticleEmitterSettings::Direction directionFromString(const std::string& s)
{
	if(s == "forward")
		return ParticleEmitterSettings::Direction_Forward;
	if(s == "down")
		return ParticleEmitterSettings::Direction_Down;
	if(s == "random")
		return ParticleEmitterSettings::Direction_Random;
	if(s == "custom")
		return ParticleEmitterSettings::Direction_Custom;
	return ParticleEmitterSettings::Direction_Up;
}


const char* shapeToString(const ParticleEmitterSettings::Shape shape)
{
	switch(shape)
	{
	case ParticleEmitterSettings::Shape_Point:      return "point";
	case ParticleEmitterSettings::Shape_Disc:       return "disc";
	case ParticleEmitterSettings::Shape_Sphere:     return "sphere";
	case ParticleEmitterSettings::Shape_Box:        return "box";
	case ParticleEmitterSettings::Shape_Ring:       return "ring";
	case ParticleEmitterSettings::Shape_Cylinder:   return "cylinder";
	case ParticleEmitterSettings::Shape_Cone:       return "cone";
	case ParticleEmitterSettings::Shape_Line:       return "line";
	case ParticleEmitterSettings::Shape_Hemisphere: return "hemisphere";
	}
	return "disc";
}


ParticleEmitterSettings::Shape shapeFromString(const std::string& s)
{
	if(s == "point")
		return ParticleEmitterSettings::Shape_Point;
	if(s == "sphere")
		return ParticleEmitterSettings::Shape_Sphere;
	if(s == "box")
		return ParticleEmitterSettings::Shape_Box;
	if(s == "ring")
		return ParticleEmitterSettings::Shape_Ring;
	if(s == "cylinder")
		return ParticleEmitterSettings::Shape_Cylinder;
	if(s == "cone")
		return ParticleEmitterSettings::Shape_Cone;
	if(s == "line")
		return ParticleEmitterSettings::Shape_Line;
	if(s == "hemisphere")
		return ParticleEmitterSettings::Shape_Hemisphere;
	return ParticleEmitterSettings::Shape_Disc;
}


const char* curveToString(const ParticleEmitterSettings::Curve curve)
{
	switch(curve)
	{
	case ParticleEmitterSettings::Curve_Linear:     return "linear";
	case ParticleEmitterSettings::Curve_EaseIn:     return "ease_in";
	case ParticleEmitterSettings::Curve_EaseOut:    return "ease_out";
	case ParticleEmitterSettings::Curve_SmoothStep: return "smoothstep";
	case ParticleEmitterSettings::Curve_Custom:     return "custom";
	}
	return "linear";
}


ParticleEmitterSettings::Curve curveFromString(const std::string& s)
{
	if(s == "ease_in")
		return ParticleEmitterSettings::Curve_EaseIn;
	if(s == "ease_out")
		return ParticleEmitterSettings::Curve_EaseOut;
	if(s == "smoothstep")
		return ParticleEmitterSettings::Curve_SmoothStep;
	if(s == "custom")
		return ParticleEmitterSettings::Curve_Custom;
	return ParticleEmitterSettings::Curve_Linear;
}


const char* renderModeToString(const ParticleEmitterSettings::RenderMode render_mode)
{
	switch(render_mode)
	{
	case ParticleEmitterSettings::RenderMode_Soft:         return "soft";
	case ParticleEmitterSettings::RenderMode_AdditiveGlow: return "additive_glow";
	}
	return "soft";
}


ParticleEmitterSettings::RenderMode renderModeFromString(const std::string& s)
{
	if(s == "additive_glow" || s == "glow")
		return ParticleEmitterSettings::RenderMode_AdditiveGlow;
	return ParticleEmitterSettings::RenderMode_Soft;
}


int colourChannelToByte(const float x)
{
	return clampInt((int)std::round(clampDouble(x, 0.0, 1.0) * 255.0), 0, 255);
}


std::string colourToHex(const Colour3f& c)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", colourChannelToByte(c.r), colourChannelToByte(c.g), colourChannelToByte(c.b));
	return std::string(buf);
}


int hexDigit(const char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}


Colour3f colourFromHex(const std::string& s, const Colour3f& default_col)
{
	if(s.size() != 7 || s[0] != '#')
		return default_col;

	const int r = hexDigit(s[1]) * 16 + hexDigit(s[2]);
	const int g = hexDigit(s[3]) * 16 + hexDigit(s[4]);
	const int b = hexDigit(s[5]) * 16 + hexDigit(s[6]);
	return Colour3f(r / 255.f, g / 255.f, b / 255.f);
}


std::string jsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for(size_t i=0; i<s.size(); ++i)
	{
		const char c = s[i];
		if(c == '\\' || c == '"')
		{
			out.push_back('\\');
			out.push_back(c);
		}
		else if(c == '\n')
			out += "\\n";
		else if(c == '\r')
			out += "\\r";
		else if(c == '\t')
			out += "\\t";
		else
			out.push_back(c);
	}
	return out;
}


size_t firstNonWhitespace(const std::string& s)
{
	size_t i = 0;
	while(i < s.size() && std::isspace((unsigned char)s[i]))
		++i;
	return i;
}


void clampSettings(ParticleEmitterSettings& s)
{
	if(s.sprite_path.size() > 4096)
		s.sprite_path.resize(4096);
	s.rate_per_sec       = (float)clampDouble(s.rate_per_sec,       0.0,   400.0);
	s.max_spawn_per_frame = clampInt(s.max_spawn_per_frame,         1,     256);
	s.max_particles      = clampInt(s.max_particles,                1,    2048);
	s.custom_dir_x       = (float)clampDouble(s.custom_dir_x,       -1.0,    1.0);
	s.custom_dir_y       = (float)clampDouble(s.custom_dir_y,       -1.0,    1.0);
	s.custom_dir_z       = (float)clampDouble(s.custom_dir_z,       -1.0,    1.0);
	if(s.custom_dir_x * s.custom_dir_x + s.custom_dir_y * s.custom_dir_y + s.custom_dir_z * s.custom_dir_z < 1.0e-6f)
	{
		s.custom_dir_x = 0.f;
		s.custom_dir_y = 0.f;
		s.custom_dir_z = 1.f;
	}
	s.emitter_radius     = (float)clampDouble(s.emitter_radius,     0.0,   100.0);
	s.speed              = (float)clampDouble(s.speed,              0.0,   200.0);
	s.speed_jitter       = (float)clampDouble(s.speed_jitter,       0.0,     1.0);
	s.spread_deg         = (float)clampDouble(s.spread_deg,         0.0,   180.0);
	s.turbulence_strength = (float)clampDouble(s.turbulence_strength, 0.0, 50.0);
	s.lifetime_s         = (float)clampDouble(s.lifetime_s,         0.05,  120.0);
	s.start_width        = (float)clampDouble(s.start_width,        0.005, 100.0);
	s.end_width          = (float)clampDouble(s.end_width,          0.005, 100.0);
	s.size_curve_mid     = (float)clampDouble(s.size_curve_mid,     0.0,     1.0);
	s.size_jitter        = (float)clampDouble(s.size_jitter,        0.0,     1.0);
	s.opacity            = (float)clampDouble(s.opacity,            0.0,   1.0);
	s.end_opacity        = (float)clampDouble(s.end_opacity,        0.0,   1.0);
	s.opacity_curve_mid  = (float)clampDouble(s.opacity_curve_mid,  0.0,     1.0);
	s.opacity_jitter     = (float)clampDouble(s.opacity_jitter,     0.0,     1.0);
	s.glow_strength      = (float)clampDouble(s.glow_strength,      0.2,   8.0);
	s.rotation_deg       = (float)clampDouble(s.rotation_deg,    -360.0,  360.0);
	s.rotation_jitter_deg = (float)clampDouble(s.rotation_jitter_deg, 0.0, 360.0);
	s.spin_deg_per_sec   = (float)clampDouble(s.spin_deg_per_sec,-720.0,  720.0);
	s.spin_jitter_deg_per_sec = (float)clampDouble(s.spin_jitter_deg_per_sec, 0.0, 720.0);
	s.burst_count        = clampInt(s.burst_count,                  1,     512);
	s.burst_interval_s   = (float)clampDouble(s.burst_interval_s,   0.05,  120.0);
	s.max_spawn_distance = (float)clampDouble(s.max_spawn_distance,  0.0,  10000.0);
	s.wind_accel_x       = (float)clampDouble(s.wind_accel_x,      -50.0,  50.0);
	s.wind_accel_y       = (float)clampDouble(s.wind_accel_y,      -50.0,  50.0);
	s.wind_accel_z       = (float)clampDouble(s.wind_accel_z,      -50.0,  50.0);
	s.vortex_strength    = (float)clampDouble(s.vortex_strength,   -50.0,  50.0);
	s.attractor_strength = (float)clampDouble(s.attractor_strength,-50.0,  50.0);
	s.attractor_radius   = (float)clampDouble(s.attractor_radius,   0.0,  100.0);
	s.event_horizon_radius = (float)clampDouble(s.event_horizon_radius, 0.0, 20.0);
	s.radial_accel       = (float)clampDouble(s.radial_accel,      -50.0,  50.0);
	s.linear_damping     = (float)clampDouble(s.linear_damping,      0.0,  10.0);
	s.buoyancy_lift      = (float)clampDouble(s.buoyancy_lift,      -5.0,   5.0);
	s.gravity_scale      = (float)clampDouble(s.gravity_scale,     -5.0,   5.0);
	s.drag_area          = (float)clampDouble(s.drag_area,          0.0,   1.0);
	s.mass               = (float)clampDouble(s.mass,               1.0e-9, 10.0);
	s.restitution        = (float)clampDouble(s.restitution,        0.0,   1.0);
	s.collision_friction = (float)clampDouble(s.collision_friction, 0.0,   1.0);
	s.colour.r           = (float)clampDouble(s.colour.r,           0.0,   1.0);
	s.colour.g           = (float)clampDouble(s.colour.g,           0.0,   1.0);
	s.colour.b           = (float)clampDouble(s.colour.b,           0.0,   1.0);
}


void makePresetImmediatelyVisible(ParticleEmitterSettings& s)
{
	s.max_spawn_distance = std::max(s.max_spawn_distance, 160.f);
	s.max_spawn_per_frame = std::max(s.max_spawn_per_frame, 128);
	s.max_particles = std::max(s.max_particles, 640);
	s.burst_count = std::max(s.burst_count, 80);
	s.opacity = std::max(s.opacity, s.render_mode == ParticleEmitterSettings::RenderMode_AdditiveGlow ? 0.72f : 0.55f);
	if(s.render_mode == ParticleEmitterSettings::RenderMode_AdditiveGlow)
		s.glow_strength = std::max(s.glow_strength, 4.0f);

	switch(s.kind)
	{
	case ParticleEmitterSettings::ParticleKind_Smoke:
		s.burst_count = std::max(s.burst_count, 64);
		break;
	case ParticleEmitterSettings::ParticleKind_Foam:
		s.start_width = std::max(s.start_width, 0.10f);
		s.end_width = std::max(s.end_width, 0.22f);
		s.burst_count = std::max(s.burst_count, 80);
		break;
	case ParticleEmitterSettings::ParticleKind_Spark:
		s.start_width = std::max(s.start_width, 0.08f);
		s.end_width = std::max(s.end_width, 0.035f);
		s.burst_count = std::max(s.burst_count, 96);
		break;
	case ParticleEmitterSettings::ParticleKind_Streak:
		s.start_width = std::max(s.start_width, 0.12f);
		s.end_width = std::max(s.end_width, 0.09f);
		s.burst_count = std::max(s.burst_count, 128);
		s.max_spawn_per_frame = std::max(s.max_spawn_per_frame, 160);
		break;
	case ParticleEmitterSettings::ParticleKind_Star:
		s.start_width = std::max(s.start_width, 0.14f);
		s.end_width = std::max(s.end_width, 0.16f);
		s.burst_count = std::max(s.burst_count, 128);
		break;
	case ParticleEmitterSettings::ParticleKind_Ring:
		s.start_width = std::max(s.start_width, 0.22f);
		s.end_width = std::max(s.end_width, 0.28f);
		s.burst_count = std::max(s.burst_count, 112);
		break;
	case ParticleEmitterSettings::ParticleKind_Nebula:
		s.start_width = std::max(s.start_width, 0.45f);
		s.end_width = std::max(s.end_width, 0.75f);
		s.opacity = std::max(s.opacity, 0.42f);
		s.burst_count = std::max(s.burst_count, 112);
		break;
	case ParticleEmitterSettings::ParticleKind_Flame:
		s.start_width = std::max(s.start_width, 0.25f);
		s.end_width = std::max(s.end_width, 0.85f);
		s.burst_count = std::max(s.burst_count, 128);
		break;
	case ParticleEmitterSettings::ParticleKind_Snowflake:
		s.start_width = std::max(s.start_width, 0.10f);
		s.end_width = std::max(s.end_width, 0.12f);
		s.burst_count = std::max(s.burst_count, 128);
		break;
	case ParticleEmitterSettings::ParticleKind_SoftDisc:
		s.start_width = std::max(s.start_width, 0.18f);
		s.end_width = std::max(s.end_width, 0.30f);
		s.burst_count = std::max(s.burst_count, 96);
		break;
	}
}

} // anonymous namespace


ParticleEmitterSettings::ParticleEmitterSettings()
:	preset_name("smoke"),
	enabled(true),
	kind(ParticleKind_Smoke),
	direction(Direction_Up),
	shape(Shape_Disc),
	render_mode(RenderMode_Soft),
	sprite_path(""),
	custom_dir_x(0.f),
	custom_dir_y(0.f),
	custom_dir_z(1.f),
	rate_per_sec(18.f),
	max_spawn_per_frame(32),
	max_particles(256),
	emitter_radius(0.12f),
	speed(1.4f),
	speed_jitter(0.15f),
	spread_deg(35.f),
	turbulence_strength(0.18f),
	lifetime_s(3.0f),
	start_width(0.25f),
	end_width(1.2f),
	size_curve(Curve_EaseOut),
	size_curve_mid(0.5f),
	size_jitter(0.25f),
	opacity(0.72f),
	end_opacity(0.f),
	opacity_curve(Curve_EaseOut),
	opacity_curve_mid(0.5f),
	opacity_jitter(0.15f),
	colour(0.82f, 0.82f, 0.82f),
	glow_strength(1.8f),
	rotation_deg(0.f),
	rotation_jitter_deg(360.f),
	spin_deg_per_sec(0.f),
	spin_jitter_deg_per_sec(25.f),
	burst_enabled(false),
	burst_count(12),
	burst_interval_s(1.0f),
	max_spawn_distance(90.f),
	wind_accel_x(0.f),
	wind_accel_y(0.f),
	wind_accel_z(0.f),
	vortex_strength(0.f),
	attractor_strength(0.f),
	attractor_radius(4.f),
	black_hole_mode(false),
	event_horizon_radius(0.12f),
	radial_accel(0.f),
	linear_damping(0.f),
	buoyancy_lift(0.f),
	gravity_scale(0.2f),
	drag_area(2.0e-4f),
	mass(1.0e-6f),
	restitution(0.2f),
	collision_friction(0.15f),
	collide_surfaces(false),
	die_when_hit_surface(false)
{
}


const char* ParticleEmitterSettings::contentMarker()
{
	return PARTICLE_EMITTER_MARKER;
}


bool ParticleEmitterSettings::isParticleEmitterContent(const std::string& content)
{
	const size_t start_i = firstNonWhitespace(content);
	const size_t marker_len = std::strlen(PARTICLE_EMITTER_MARKER);
	return content.size() >= start_i + marker_len && content.compare(start_i, marker_len, PARTICLE_EMITTER_MARKER) == 0;
}


ParticleEmitterSettings ParticleEmitterSettings::defaultSmoke()
{
	return ParticleEmitterSettings();
}


ParticleEmitterSettings ParticleEmitterSettings::presetSettings(const std::string& preset_name)
{
	ParticleEmitterSettings settings;
	settings.preset_name = preset_name;

	if(preset_name == "steam")
	{
		settings.kind = ParticleKind_SoftDisc;
		settings.sprite_path = "builtin:soft_disc";
		settings.direction = Direction_Up;
		settings.shape = Shape_Disc;
		settings.rate_per_sec = 32.f;
		settings.max_particles = 220;
		settings.emitter_radius = 0.18f;
		settings.speed = 0.85f;
		settings.speed_jitter = 0.35f;
		settings.spread_deg = 18.f;
		settings.turbulence_strength = 0.35f;
		settings.lifetime_s = 2.4f;
		settings.start_width = 0.12f;
		settings.end_width = 0.85f;
		settings.size_curve = Curve_EaseOut;
		settings.size_jitter = 0.35f;
		settings.opacity = 0.45f;
		settings.opacity_curve = Curve_EaseOut;
		settings.opacity_jitter = 0.25f;
		settings.colour = Colour3f(0.72f, 0.78f, 0.86f);
		settings.gravity_scale = -0.08f;
		settings.drag_area = 3.0e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "foam_spray")
	{
		settings.kind = ParticleKind_Foam;
		settings.sprite_path = "builtin:foam";
		settings.direction = Direction_Forward;
		settings.shape = Shape_Disc;
		settings.rate_per_sec = 45.f;
		settings.max_spawn_per_frame = 64;
		settings.max_particles = 260;
		settings.emitter_radius = 0.08f;
		settings.speed = 4.0f;
		settings.speed_jitter = 0.45f;
		settings.spread_deg = 38.f;
		settings.turbulence_strength = 0.65f;
		settings.lifetime_s = 1.25f;
		settings.start_width = 0.06f;
		settings.end_width = 0.24f;
		settings.size_curve = Curve_EaseOut;
		settings.size_jitter = 0.35f;
		settings.opacity = 0.82f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(0.85f, 0.92f, 1.0f);
		settings.burst_enabled = true;
		settings.burst_count = 18;
		settings.burst_interval_s = 0.7f;
		settings.gravity_scale = 0.75f;
		settings.drag_area = 5.0e-5f;
		settings.restitution = 0.1f;
		settings.collide_surfaces = true;
		settings.die_when_hit_surface = true;
	}
	else if(preset_name == "fire")
	{
		settings.kind = ParticleKind_Flame;
		settings.sprite_path = "builtin:flame";
		settings.direction = Direction_Up;
		settings.shape = Shape_Cone;
		settings.rate_per_sec = 180.f;
		settings.max_spawn_per_frame = 256;
		settings.max_particles = 1200;
		settings.emitter_radius = 0.16f;
		settings.speed = 2.0f;
		settings.speed_jitter = 0.45f;
		settings.spread_deg = 30.f;
		settings.turbulence_strength = 2.0f;
		settings.lifetime_s = 1.65f;
		settings.start_width = 0.34f;
		settings.end_width = 1.25f;
		settings.size_curve = Curve_EaseOut;
		settings.size_jitter = 0.45f;
		settings.opacity = 1.0f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_EaseIn;
		settings.opacity_jitter = 0.2f;
		settings.colour = Colour3f(1.0f, 0.42f, 0.08f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 7.5f;
		settings.rotation_jitter_deg = 360.f;
		settings.spin_deg_per_sec = 35.f;
		settings.spin_jitter_deg_per_sec = 90.f;
		settings.burst_enabled = true;
		settings.burst_count = 220;
		settings.burst_interval_s = 0.12f;
		settings.gravity_scale = -0.22f;
		settings.buoyancy_lift = 0.18f;
		settings.drag_area = 2.5e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "snow")
	{
		settings.kind = ParticleKind_Snowflake;
		settings.sprite_path = "builtin:snowflake";
		settings.direction = Direction_Down;
		settings.shape = Shape_Box;
		settings.rate_per_sec = 22.f;
		settings.max_particles = 480;
		settings.emitter_radius = 4.0f;
		settings.speed = 0.42f;
		settings.speed_jitter = 0.75f;
		settings.spread_deg = 20.f;
		settings.turbulence_strength = 0.12f;
		settings.lifetime_s = 9.0f;
		settings.start_width = 0.045f;
		settings.end_width = 0.06f;
		settings.size_curve = Curve_SmoothStep;
		settings.size_jitter = 0.5f;
		settings.opacity = 0.62f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.25f;
		settings.colour = Colour3f(0.96f, 0.98f, 1.0f);
		settings.wind_accel_x = 0.22f;
		settings.wind_accel_y = 0.05f;
		settings.gravity_scale = 0.08f;
		settings.drag_area = 4.0e-4f;
		settings.restitution = 0.0f;
		settings.collide_surfaces = true;
		settings.die_when_hit_surface = true;
	}
	else if(preset_name == "sparks")
	{
		settings.kind = ParticleKind_Spark;
		settings.sprite_path = "builtin:spark";
		settings.direction = Direction_Up;
		settings.shape = Shape_Point;
		settings.rate_per_sec = 30.f;
		settings.max_particles = 180;
		settings.emitter_radius = 0.02f;
		settings.speed = 4.8f;
		settings.speed_jitter = 0.65f;
		settings.spread_deg = 55.f;
		settings.turbulence_strength = 2.2f;
		settings.lifetime_s = 0.75f;
		settings.start_width = 0.035f;
		settings.end_width = 0.012f;
		settings.size_curve = Curve_EaseIn;
		settings.size_jitter = 0.5f;
		settings.opacity = 0.95f;
		settings.opacity_curve = Curve_EaseIn;
		settings.opacity_jitter = 0.25f;
		settings.colour = Colour3f(1.0f, 0.65f, 0.14f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 4.0f;
		settings.spin_jitter_deg_per_sec = 240.f;
		settings.burst_enabled = true;
		settings.burst_count = 12;
		settings.burst_interval_s = 0.55f;
		settings.gravity_scale = 0.9f;
		settings.drag_area = 1.0e-5f;
		settings.restitution = 0.45f;
		settings.collide_surfaces = true;
	}
	else if(preset_name == "magic")
	{
		settings.kind = ParticleKind_Star;
		settings.sprite_path = "builtin:star";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 36.f;
		settings.max_particles = 360;
		settings.emitter_radius = 0.32f;
		settings.speed = 0.72f;
		settings.speed_jitter = 0.65f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 1.1f;
		settings.lifetime_s = 2.7f;
		settings.start_width = 0.06f;
		settings.end_width = 0.26f;
		settings.size_curve = Curve_SmoothStep;
		settings.size_jitter = 0.55f;
		settings.opacity = 0.58f;
		settings.end_opacity = 0.08f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.35f;
		settings.colour = Colour3f(0.38f, 0.76f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 2.6f;
		settings.spin_deg_per_sec = 35.f;
		settings.spin_jitter_deg_per_sec = 150.f;
		settings.vortex_strength = 0.8f;
		settings.attractor_strength = -0.15f;
		settings.attractor_radius = 1.6f;
		settings.burst_enabled = true;
		settings.burst_count = 16;
		settings.burst_interval_s = 0.85f;
		settings.gravity_scale = 0.0f;
		settings.drag_area = 3.5e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "embers")
	{
		settings.kind = ParticleKind_Spark;
		settings.sprite_path = "builtin:spark";
		settings.direction = Direction_Up;
		settings.shape = Shape_Disc;
		settings.rate_per_sec = 26.f;
		settings.max_particles = 240;
		settings.emitter_radius = 0.22f;
		settings.speed = 1.1f;
		settings.speed_jitter = 0.8f;
		settings.spread_deg = 42.f;
		settings.turbulence_strength = 0.75f;
		settings.lifetime_s = 3.2f;
		settings.start_width = 0.035f;
		settings.end_width = 0.09f;
		settings.size_curve = Curve_Custom;
		settings.size_curve_mid = 0.28f;
		settings.size_jitter = 0.7f;
		settings.opacity = 0.86f;
		settings.opacity_curve = Curve_EaseIn;
		settings.opacity_jitter = 0.3f;
		settings.colour = Colour3f(1.0f, 0.42f, 0.08f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 3.5f;
		settings.spin_jitter_deg_per_sec = 220.f;
		settings.gravity_scale = -0.06f;
		settings.drag_area = 1.5e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "rain")
	{
		settings.kind = ParticleKind_Streak;
		settings.sprite_path = "builtin:streak";
		settings.direction = Direction_Down;
		settings.shape = Shape_Box;
		settings.rate_per_sec = 90.f;
		settings.max_spawn_per_frame = 128;
		settings.max_particles = 700;
		settings.emitter_radius = 6.0f;
		settings.speed = 8.5f;
		settings.speed_jitter = 0.2f;
		settings.spread_deg = 5.f;
		settings.turbulence_strength = 0.08f;
		settings.lifetime_s = 1.8f;
		settings.start_width = 0.025f;
		settings.end_width = 0.035f;
		settings.opacity = 0.55f;
		settings.opacity_curve = Curve_Linear;
		settings.colour = Colour3f(0.62f, 0.78f, 1.0f);
		settings.wind_accel_x = 0.35f;
		settings.gravity_scale = 0.28f;
		settings.drag_area = 1.0e-5f;
		settings.collide_surfaces = true;
		settings.die_when_hit_surface = true;
	}
	else if(preset_name == "plasma")
	{
		settings.kind = ParticleKind_Ring;
		settings.sprite_path = "builtin:ring";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 42.f;
		settings.max_particles = 520;
		settings.emitter_radius = 0.5f;
		settings.speed = 0.55f;
		settings.speed_jitter = 0.75f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 1.8f;
		settings.lifetime_s = 2.0f;
		settings.start_width = 0.12f;
		settings.end_width = 0.38f;
		settings.size_curve = Curve_SmoothStep;
		settings.opacity = 0.72f;
		settings.end_opacity = 0.06f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(0.28f, 0.82f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 4.8f;
		settings.spin_deg_per_sec = 80.f;
		settings.spin_jitter_deg_per_sec = 260.f;
		settings.vortex_strength = 2.6f;
		settings.attractor_strength = 0.18f;
		settings.attractor_radius = 2.2f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 1.0e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "nebula")
	{
		settings.kind = ParticleKind_Nebula;
		settings.sprite_path = "builtin:nebula";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 70.f;
		settings.max_spawn_per_frame = 192;
		settings.max_particles = 1300;
		settings.emitter_radius = 2.4f;
		settings.speed = 0.18f;
		settings.speed_jitter = 0.9f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.18f;
		settings.lifetime_s = 18.f;
		settings.start_width = 0.9f;
		settings.end_width = 3.2f;
		settings.size_curve = Curve_EaseOut;
		settings.size_jitter = 0.8f;
		settings.opacity = 0.48f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.45f;
		settings.colour = Colour3f(0.34f, 0.54f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 4.5f;
		settings.spin_jitter_deg_per_sec = 12.f;
		settings.vortex_strength = 0.16f;
		settings.attractor_radius = 5.0f;
		settings.burst_enabled = true;
		settings.burst_count = 192;
		settings.burst_interval_s = 1.4f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "starfield")
	{
		settings.kind = ParticleKind_Star;
		settings.sprite_path = "builtin:star";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 12.f;
		settings.max_particles = 900;
		settings.emitter_radius = 8.0f;
		settings.speed = 0.02f;
		settings.speed_jitter = 1.0f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.f;
		settings.lifetime_s = 60.f;
		settings.start_width = 0.045f;
		settings.end_width = 0.06f;
		settings.size_curve = Curve_Linear;
		settings.size_jitter = 0.9f;
		settings.opacity = 0.85f;
		settings.end_opacity = 0.75f;
		settings.opacity_curve = Curve_Linear;
		settings.opacity_jitter = 0.25f;
		settings.colour = Colour3f(0.86f, 0.92f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 3.0f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "black_hole")
	{
		settings.kind = ParticleKind_Ring;
		settings.sprite_path = "builtin:black_hole";
		settings.direction = Direction_Random;
		settings.shape = Shape_Ring;
		settings.rate_per_sec = 160.f;
		settings.max_spawn_per_frame = 256;
		settings.max_particles = 1500;
		settings.emitter_radius = 1.7f;
		settings.speed = 0.9f;
		settings.speed_jitter = 0.55f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.05f;
		settings.lifetime_s = 6.5f;
		settings.start_width = 0.42f;
		settings.end_width = 0.18f;
		settings.size_curve = Curve_EaseIn;
		settings.opacity = 1.0f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(1.0f, 1.0f, 1.0f);
		settings.render_mode = RenderMode_Soft;
		settings.glow_strength = 1.0f;
		settings.spin_deg_per_sec = 260.f;
		settings.spin_jitter_deg_per_sec = 420.f;
		settings.vortex_strength = 11.0f;
		settings.attractor_strength = 4.2f;
		settings.attractor_radius = 3.6f;
		settings.black_hole_mode = true;
		settings.event_horizon_radius = 0.42f;
		settings.burst_enabled = true;
		settings.burst_count = 256;
		settings.burst_interval_s = 0.55f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "gravity_well")
	{
		settings.kind = ParticleKind_SoftDisc;
		settings.sprite_path = "builtin:soft_disc";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 110.f;
		settings.max_spawn_per_frame = 192;
		settings.max_particles = 1200;
		settings.emitter_radius = 3.5f;
		settings.speed = 0.75f;
		settings.speed_jitter = 0.8f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.12f;
		settings.lifetime_s = 7.5f;
		settings.start_width = 0.18f;
		settings.end_width = 0.10f;
		settings.opacity = 0.9f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(0.58f, 0.72f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 5.5f;
		settings.vortex_strength = 4.2f;
		settings.attractor_strength = 5.2f;
		settings.attractor_radius = 4.8f;
		settings.burst_enabled = true;
		settings.burst_count = 192;
		settings.burst_interval_s = 0.9f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "meteor_shower")
	{
		settings.kind = ParticleKind_Streak;
		settings.sprite_path = "builtin:streak";
		settings.direction = Direction_Custom;
		settings.custom_dir_x = -0.35f;
		settings.custom_dir_y = 0.35f;
		settings.custom_dir_z = -0.86f;
		settings.shape = Shape_Box;
		settings.rate_per_sec = 18.f;
		settings.max_particles = 320;
		settings.emitter_radius = 5.0f;
		settings.speed = 9.0f;
		settings.speed_jitter = 0.3f;
		settings.spread_deg = 12.f;
		settings.turbulence_strength = 0.04f;
		settings.lifetime_s = 2.4f;
		settings.start_width = 0.10f;
		settings.end_width = 0.025f;
		settings.opacity = 0.92f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(1.0f, 0.62f, 0.22f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 5.5f;
		settings.gravity_scale = 0.08f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = true;
		settings.die_when_hit_surface = true;
	}
	else if(preset_name == "electric_arc")
	{
		settings.kind = ParticleKind_Streak;
		settings.sprite_path = "builtin:streak";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 70.f;
		settings.max_spawn_per_frame = 128;
		settings.max_particles = 420;
		settings.emitter_radius = 0.42f;
		settings.speed = 3.5f;
		settings.speed_jitter = 0.95f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 9.0f;
		settings.lifetime_s = 0.28f;
		settings.start_width = 0.035f;
		settings.end_width = 0.11f;
		settings.size_curve = Curve_Custom;
		settings.size_curve_mid = 0.92f;
		settings.opacity = 1.0f;
		settings.opacity_curve = Curve_EaseIn;
		settings.opacity_jitter = 0.12f;
		settings.colour = Colour3f(0.28f, 0.72f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 6.5f;
		settings.spin_jitter_deg_per_sec = 720.f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "fireflies")
	{
		settings.kind = ParticleKind_SoftDisc;
		settings.sprite_path = "builtin:soft_disc";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 8.f;
		settings.max_particles = 220;
		settings.emitter_radius = 2.2f;
		settings.speed = 0.28f;
		settings.speed_jitter = 1.0f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.85f;
		settings.lifetime_s = 10.f;
		settings.start_width = 0.045f;
		settings.end_width = 0.07f;
		settings.size_curve = Curve_SmoothStep;
		settings.opacity = 0.75f;
		settings.end_opacity = 0.2f;
		settings.opacity_curve = Curve_Custom;
		settings.opacity_curve_mid = 0.85f;
		settings.colour = Colour3f(0.82f, 1.0f, 0.34f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 4.4f;
		settings.gravity_scale = -0.02f;
		settings.drag_area = 2.0e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "comet_tail")
	{
		settings.kind = ParticleKind_Streak;
		settings.sprite_path = "builtin:comet";
		settings.direction = Direction_Custom;
		settings.custom_dir_x = -0.86f;
		settings.custom_dir_y = 0.12f;
		settings.custom_dir_z = 0.36f;
		settings.shape = Shape_Point;
		settings.rate_per_sec = 140.f;
		settings.max_spawn_per_frame = 256;
		settings.max_particles = 1300;
		settings.emitter_radius = 0.05f;
		settings.speed = 4.6f;
		settings.speed_jitter = 0.45f;
		settings.spread_deg = 12.f;
		settings.turbulence_strength = 0.35f;
		settings.lifetime_s = 6.2f;
		settings.start_width = 0.34f;
		settings.end_width = 2.6f;
		settings.size_curve = Curve_EaseOut;
		settings.size_jitter = 0.55f;
		settings.opacity = 0.88f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_EaseOut;
		settings.opacity_jitter = 0.25f;
		settings.colour = Colour3f(0.72f, 0.9f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 6.5f;
		settings.burst_enabled = true;
		settings.burst_count = 240;
		settings.burst_interval_s = 0.35f;
		settings.wind_accel_x = -0.24f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 6.0e-5f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "galaxy_spiral")
	{
		settings.kind = ParticleKind_Nebula;
		settings.sprite_path = "builtin:galaxy";
		settings.direction = Direction_Random;
		settings.shape = Shape_Disc;
		settings.rate_per_sec = 150.f;
		settings.max_spawn_per_frame = 256;
		settings.max_particles = 1500;
		settings.emitter_radius = 2.8f;
		settings.speed = 0.22f;
		settings.speed_jitter = 0.72f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.08f;
		settings.lifetime_s = 20.f;
		settings.start_width = 0.52f;
		settings.end_width = 1.35f;
		settings.size_curve = Curve_SmoothStep;
		settings.size_jitter = 0.7f;
		settings.opacity = 0.68f;
		settings.end_opacity = 0.08f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.35f;
		settings.colour = Colour3f(0.72f, 0.56f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 5.2f;
		settings.spin_deg_per_sec = 12.f;
		settings.spin_jitter_deg_per_sec = 55.f;
		settings.vortex_strength = 6.0f;
		settings.attractor_strength = 0.35f;
		settings.attractor_radius = 4.2f;
		settings.burst_enabled = true;
		settings.burst_count = 256;
		settings.burst_interval_s = 1.2f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "supernova")
	{
		settings.kind = ParticleKind_Star;
		settings.sprite_path = "builtin:flare";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 8.f;
		settings.max_spawn_per_frame = 256;
		settings.max_particles = 900;
		settings.emitter_radius = 0.08f;
		settings.speed = 4.8f;
		settings.speed_jitter = 0.85f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.2f;
		settings.lifetime_s = 2.4f;
		settings.start_width = 0.18f;
		settings.end_width = 2.4f;
		settings.size_curve = Curve_EaseOut;
		settings.size_jitter = 0.8f;
		settings.opacity = 0.95f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_EaseIn;
		settings.opacity_jitter = 0.18f;
		settings.colour = Colour3f(1.0f, 0.72f, 0.32f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 7.0f;
		settings.burst_enabled = true;
		settings.burst_count = 180;
		settings.burst_interval_s = 4.0f;
		settings.radial_accel = 8.0f;
		settings.linear_damping = 0.18f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "pulsar_beam")
	{
		settings.kind = ParticleKind_Streak;
		settings.sprite_path = "builtin:beam";
		settings.direction = Direction_Custom;
		settings.custom_dir_x = 1.f;
		settings.custom_dir_y = 0.f;
		settings.custom_dir_z = 0.05f;
		settings.shape = Shape_Line;
		settings.rate_per_sec = 120.f;
		settings.max_spawn_per_frame = 160;
		settings.max_particles = 620;
		settings.emitter_radius = 0.42f;
		settings.speed = 7.0f;
		settings.speed_jitter = 0.18f;
		settings.spread_deg = 4.f;
		settings.turbulence_strength = 0.05f;
		settings.lifetime_s = 1.2f;
		settings.start_width = 0.055f;
		settings.end_width = 0.18f;
		settings.opacity = 0.9f;
		settings.end_opacity = 0.08f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(0.34f, 0.78f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 6.2f;
		settings.burst_enabled = true;
		settings.burst_count = 36;
		settings.burst_interval_s = 0.22f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "solar_wind")
	{
		settings.kind = ParticleKind_SoftDisc;
		settings.sprite_path = "builtin:dust";
		settings.direction = Direction_Custom;
		settings.custom_dir_x = 1.f;
		settings.custom_dir_y = 0.12f;
		settings.custom_dir_z = 0.02f;
		settings.shape = Shape_Box;
		settings.rate_per_sec = 80.f;
		settings.max_spawn_per_frame = 128;
		settings.max_particles = 900;
		settings.emitter_radius = 4.5f;
		settings.speed = 2.2f;
		settings.speed_jitter = 0.75f;
		settings.spread_deg = 18.f;
		settings.turbulence_strength = 0.22f;
		settings.lifetime_s = 6.0f;
		settings.start_width = 0.045f;
		settings.end_width = 0.16f;
		settings.size_jitter = 0.75f;
		settings.opacity = 0.42f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_EaseOut;
		settings.opacity_jitter = 0.45f;
		settings.colour = Colour3f(1.0f, 0.82f, 0.38f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 2.8f;
		settings.wind_accel_x = 0.32f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "cosmic_dust")
	{
		settings.kind = ParticleKind_Nebula;
		settings.sprite_path = "builtin:dust";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 34.f;
		settings.max_particles = 850;
		settings.emitter_radius = 3.8f;
		settings.speed = 0.16f;
		settings.speed_jitter = 1.0f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.12f;
		settings.lifetime_s = 22.f;
		settings.start_width = 0.08f;
		settings.end_width = 0.34f;
		settings.size_jitter = 0.9f;
		settings.opacity = 0.34f;
		settings.end_opacity = 0.02f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.6f;
		settings.colour = Colour3f(0.78f, 0.72f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 2.0f;
		settings.vortex_strength = 0.28f;
		settings.attractor_radius = 5.5f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "wormhole")
	{
		settings.kind = ParticleKind_Ring;
		settings.sprite_path = "builtin:spiral";
		settings.direction = Direction_Random;
		settings.shape = Shape_Ring;
		settings.rate_per_sec = 150.f;
		settings.max_spawn_per_frame = 256;
		settings.max_particles = 1500;
		settings.emitter_radius = 1.7f;
		settings.speed = 1.0f;
		settings.speed_jitter = 0.75f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.16f;
		settings.lifetime_s = 4.2f;
		settings.start_width = 0.34f;
		settings.end_width = 0.72f;
		settings.size_curve = Curve_SmoothStep;
		settings.opacity = 0.95f;
		settings.end_opacity = 0.02f;
		settings.opacity_curve = Curve_EaseIn;
		settings.colour = Colour3f(0.58f, 0.32f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 7.0f;
		settings.spin_deg_per_sec = 170.f;
		settings.spin_jitter_deg_per_sec = 300.f;
		settings.vortex_strength = 8.0f;
		settings.attractor_strength = 2.4f;
		settings.attractor_radius = 3.8f;
		settings.burst_enabled = true;
		settings.burst_count = 256;
		settings.burst_interval_s = 0.45f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "ion_thruster")
	{
		settings.kind = ParticleKind_Streak;
		settings.sprite_path = "builtin:beam";
		settings.direction = Direction_Down;
		settings.shape = Shape_Cone;
		settings.rate_per_sec = 110.f;
		settings.max_spawn_per_frame = 160;
		settings.max_particles = 650;
		settings.emitter_radius = 0.18f;
		settings.speed = 4.2f;
		settings.speed_jitter = 0.22f;
		settings.spread_deg = 12.f;
		settings.turbulence_strength = 0.16f;
		settings.lifetime_s = 1.6f;
		settings.start_width = 0.07f;
		settings.end_width = 0.55f;
		settings.size_curve = Curve_EaseOut;
		settings.opacity = 0.85f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_EaseOut;
		settings.colour = Colour3f(0.36f, 0.72f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 5.8f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 2.0e-5f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "aurora_curtain")
	{
		settings.kind = ParticleKind_SoftDisc;
		settings.sprite_path = "builtin:aurora";
		settings.direction = Direction_Up;
		settings.shape = Shape_Line;
		settings.rate_per_sec = 42.f;
		settings.max_particles = 640;
		settings.emitter_radius = 3.2f;
		settings.speed = 0.42f;
		settings.speed_jitter = 0.7f;
		settings.spread_deg = 18.f;
		settings.turbulence_strength = 0.75f;
		settings.lifetime_s = 9.0f;
		settings.start_width = 0.55f;
		settings.end_width = 1.5f;
		settings.size_curve = Curve_SmoothStep;
		settings.size_jitter = 0.55f;
		settings.opacity = 0.36f;
		settings.end_opacity = 0.0f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.35f;
		settings.colour = Colour3f(0.22f, 1.0f, 0.62f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 2.7f;
		settings.wind_accel_y = 0.12f;
		settings.gravity_scale = -0.05f;
		settings.drag_area = 2.0e-4f;
		settings.collide_surfaces = false;
	}
	else if(preset_name == "energy_shield")
	{
		settings.kind = ParticleKind_Ring;
		settings.sprite_path = "builtin:bubble";
		settings.direction = Direction_Random;
		settings.shape = Shape_Sphere;
		settings.rate_per_sec = 58.f;
		settings.max_particles = 760;
		settings.emitter_radius = 1.25f;
		settings.speed = 0.28f;
		settings.speed_jitter = 0.85f;
		settings.spread_deg = 180.f;
		settings.turbulence_strength = 0.32f;
		settings.lifetime_s = 3.6f;
		settings.start_width = 0.16f;
		settings.end_width = 0.52f;
		settings.size_curve = Curve_SmoothStep;
		settings.opacity = 0.55f;
		settings.end_opacity = 0.08f;
		settings.opacity_curve = Curve_SmoothStep;
		settings.opacity_jitter = 0.2f;
		settings.colour = Colour3f(0.28f, 0.88f, 1.0f);
		settings.render_mode = RenderMode_AdditiveGlow;
		settings.glow_strength = 3.8f;
		settings.vortex_strength = 1.4f;
		settings.radial_accel = 0.8f;
		settings.linear_damping = 0.25f;
		settings.gravity_scale = 0.f;
		settings.drag_area = 0.f;
		settings.collide_surfaces = false;
	}
	else
	{
		settings.preset_name = "smoke";
	}

	makePresetImmediatelyVisible(settings);
	clampSettings(settings);
	return settings;
}


ParticleEmitterSettings ParticleEmitterSettings::fromContent(const std::string& content, std::string* parse_error_out)
{
	ParticleEmitterSettings settings = defaultSmoke();
	if(parse_error_out)
		parse_error_out->clear();

	if(!isParticleEmitterContent(content))
		return settings;

	const size_t json_start = content.find('{');
	if(json_start == std::string::npos)
		return settings;

	try
	{
		JSONParser parser;
		parser.parseBuffer(content.data() + json_start, content.size() - json_start);
		if(parser.nodes.empty())
			return settings;

		const JSONNode& root = parser.nodes[0];
		if(root.type != JSONNode::Type_Object)
			return settings;

		settings.preset_name = root.getChildStringValueWithDefaultVal(parser, "preset", settings.preset_name);
		settings.enabled = root.getChildBoolValueWithDefaultVal(parser, "enabled", settings.enabled);
		settings.kind = kindFromString(root.getChildStringValueWithDefaultVal(parser, "kind", kindToString(settings.kind)));
		settings.direction = directionFromString(root.getChildStringValueWithDefaultVal(parser, "direction", directionToString(settings.direction)));
		settings.shape = shapeFromString(root.getChildStringValueWithDefaultVal(parser, "shape", shapeToString(settings.shape)));
		settings.render_mode = renderModeFromString(root.getChildStringValueWithDefaultVal(parser, "render_mode", renderModeToString(settings.render_mode)));
		settings.sprite_path = root.getChildStringValueWithDefaultVal(parser, "sprite_path", settings.sprite_path);
		settings.custom_dir_x = (float)root.getChildDoubleValueWithDefaultVal(parser, "custom_dir_x", settings.custom_dir_x);
		settings.custom_dir_y = (float)root.getChildDoubleValueWithDefaultVal(parser, "custom_dir_y", settings.custom_dir_y);
		settings.custom_dir_z = (float)root.getChildDoubleValueWithDefaultVal(parser, "custom_dir_z", settings.custom_dir_z);
		settings.rate_per_sec = (float)root.getChildDoubleValueWithDefaultVal(parser, "rate_per_sec", settings.rate_per_sec);
		settings.max_spawn_per_frame = root.getChildIntValueWithDefaultVal(parser, "max_spawn_per_frame", settings.max_spawn_per_frame);
		settings.max_particles = root.getChildIntValueWithDefaultVal(parser, "max_particles", settings.max_particles);
		settings.emitter_radius = (float)root.getChildDoubleValueWithDefaultVal(parser, "emitter_radius", settings.emitter_radius);
		settings.speed = (float)root.getChildDoubleValueWithDefaultVal(parser, "speed", settings.speed);
		settings.speed_jitter = (float)root.getChildDoubleValueWithDefaultVal(parser, "speed_jitter", settings.speed_jitter);
		settings.spread_deg = (float)root.getChildDoubleValueWithDefaultVal(parser, "spread_deg", settings.spread_deg);
		settings.turbulence_strength = (float)root.getChildDoubleValueWithDefaultVal(parser, "turbulence_strength", settings.turbulence_strength);
		settings.lifetime_s = (float)root.getChildDoubleValueWithDefaultVal(parser, "lifetime_s", settings.lifetime_s);
		settings.start_width = (float)root.getChildDoubleValueWithDefaultVal(parser, "start_width", settings.start_width);
		settings.end_width = (float)root.getChildDoubleValueWithDefaultVal(parser, "end_width", settings.end_width);
		settings.size_curve = curveFromString(root.getChildStringValueWithDefaultVal(parser, "size_curve", curveToString(settings.size_curve)));
		settings.size_curve_mid = (float)root.getChildDoubleValueWithDefaultVal(parser, "size_curve_mid", settings.size_curve_mid);
		settings.size_jitter = (float)root.getChildDoubleValueWithDefaultVal(parser, "size_jitter", settings.size_jitter);
		settings.opacity = (float)root.getChildDoubleValueWithDefaultVal(parser, "opacity", settings.opacity);
		settings.end_opacity = (float)root.getChildDoubleValueWithDefaultVal(parser, "end_opacity", settings.end_opacity);
		settings.opacity_curve = curveFromString(root.getChildStringValueWithDefaultVal(parser, "opacity_curve", curveToString(settings.opacity_curve)));
		settings.opacity_curve_mid = (float)root.getChildDoubleValueWithDefaultVal(parser, "opacity_curve_mid", settings.opacity_curve_mid);
		settings.opacity_jitter = (float)root.getChildDoubleValueWithDefaultVal(parser, "opacity_jitter", settings.opacity_jitter);
		settings.colour = colourFromHex(root.getChildStringValueWithDefaultVal(parser, "colour", colourToHex(settings.colour)), settings.colour);
		settings.glow_strength = (float)root.getChildDoubleValueWithDefaultVal(parser, "glow_strength", settings.glow_strength);
		settings.rotation_deg = (float)root.getChildDoubleValueWithDefaultVal(parser, "rotation_deg", settings.rotation_deg);
		settings.rotation_jitter_deg = (float)root.getChildDoubleValueWithDefaultVal(parser, "rotation_jitter_deg", settings.rotation_jitter_deg);
		settings.spin_deg_per_sec = (float)root.getChildDoubleValueWithDefaultVal(parser, "spin_deg_per_sec", settings.spin_deg_per_sec);
		settings.spin_jitter_deg_per_sec = (float)root.getChildDoubleValueWithDefaultVal(parser, "spin_jitter_deg_per_sec", settings.spin_jitter_deg_per_sec);
		settings.burst_enabled = root.getChildBoolValueWithDefaultVal(parser, "burst_enabled", settings.burst_enabled);
		settings.burst_count = root.getChildIntValueWithDefaultVal(parser, "burst_count", settings.burst_count);
		settings.burst_interval_s = (float)root.getChildDoubleValueWithDefaultVal(parser, "burst_interval_s", settings.burst_interval_s);
		settings.max_spawn_distance = (float)root.getChildDoubleValueWithDefaultVal(parser, "max_spawn_distance", settings.max_spawn_distance);
		settings.wind_accel_x = (float)root.getChildDoubleValueWithDefaultVal(parser, "wind_accel_x", settings.wind_accel_x);
		settings.wind_accel_y = (float)root.getChildDoubleValueWithDefaultVal(parser, "wind_accel_y", settings.wind_accel_y);
		settings.wind_accel_z = (float)root.getChildDoubleValueWithDefaultVal(parser, "wind_accel_z", settings.wind_accel_z);
		settings.vortex_strength = (float)root.getChildDoubleValueWithDefaultVal(parser, "vortex_strength", settings.vortex_strength);
		settings.attractor_strength = (float)root.getChildDoubleValueWithDefaultVal(parser, "attractor_strength", settings.attractor_strength);
		settings.attractor_radius = (float)root.getChildDoubleValueWithDefaultVal(parser, "attractor_radius", settings.attractor_radius);
		settings.black_hole_mode = root.getChildBoolValueWithDefaultVal(parser, "black_hole_mode", settings.black_hole_mode);
		settings.event_horizon_radius = (float)root.getChildDoubleValueWithDefaultVal(parser, "event_horizon_radius", settings.event_horizon_radius);
		settings.radial_accel = (float)root.getChildDoubleValueWithDefaultVal(parser, "radial_accel", settings.radial_accel);
		settings.linear_damping = (float)root.getChildDoubleValueWithDefaultVal(parser, "linear_damping", settings.linear_damping);
		settings.buoyancy_lift = (float)root.getChildDoubleValueWithDefaultVal(parser, "buoyancy_lift", settings.buoyancy_lift);
		settings.gravity_scale = (float)root.getChildDoubleValueWithDefaultVal(parser, "gravity_scale", settings.gravity_scale);
		settings.drag_area = (float)root.getChildDoubleValueWithDefaultVal(parser, "drag_area", settings.drag_area);
		settings.mass = (float)root.getChildDoubleValueWithDefaultVal(parser, "mass", settings.mass);
		settings.restitution = (float)root.getChildDoubleValueWithDefaultVal(parser, "restitution", settings.restitution);
		settings.collision_friction = (float)root.getChildDoubleValueWithDefaultVal(parser, "collision_friction", settings.collision_friction);
		settings.collide_surfaces = root.getChildBoolValueWithDefaultVal(parser, "collide_surfaces", settings.collide_surfaces);
		settings.die_when_hit_surface = root.getChildBoolValueWithDefaultVal(parser, "die_when_hit_surface", settings.die_when_hit_surface);
	}
	catch(...)
	{
		if(parse_error_out)
			*parse_error_out = "Failed to parse particle emitter settings.";
		return defaultSmoke();
	}

	clampSettings(settings);
	return settings;
}


std::string ParticleEmitterSettings::serialiseToContent(const ParticleEmitterSettings& settings_)
{
	ParticleEmitterSettings settings = settings_;
	clampSettings(settings);

	std::ostringstream s;
	s << PARTICLE_EMITTER_MARKER << "\n";
	s << std::setprecision(8);
	s << "{\n";
	s << "  \"preset\": \"" << jsonEscape(settings.preset_name) << "\",\n";
	s << "  \"enabled\": " << (settings.enabled ? "true" : "false") << ",\n";
	s << "  \"kind\": \"" << kindToString(settings.kind) << "\",\n";
	s << "  \"direction\": \"" << directionToString(settings.direction) << "\",\n";
	s << "  \"shape\": \"" << shapeToString(settings.shape) << "\",\n";
	s << "  \"render_mode\": \"" << renderModeToString(settings.render_mode) << "\",\n";
	s << "  \"sprite_path\": \"" << jsonEscape(settings.sprite_path) << "\",\n";
	s << "  \"custom_dir_x\": " << settings.custom_dir_x << ",\n";
	s << "  \"custom_dir_y\": " << settings.custom_dir_y << ",\n";
	s << "  \"custom_dir_z\": " << settings.custom_dir_z << ",\n";
	s << "  \"rate_per_sec\": " << settings.rate_per_sec << ",\n";
	s << "  \"max_spawn_per_frame\": " << settings.max_spawn_per_frame << ",\n";
	s << "  \"max_particles\": " << settings.max_particles << ",\n";
	s << "  \"emitter_radius\": " << settings.emitter_radius << ",\n";
	s << "  \"speed\": " << settings.speed << ",\n";
	s << "  \"speed_jitter\": " << settings.speed_jitter << ",\n";
	s << "  \"spread_deg\": " << settings.spread_deg << ",\n";
	s << "  \"turbulence_strength\": " << settings.turbulence_strength << ",\n";
	s << "  \"lifetime_s\": " << settings.lifetime_s << ",\n";
	s << "  \"start_width\": " << settings.start_width << ",\n";
	s << "  \"end_width\": " << settings.end_width << ",\n";
	s << "  \"size_curve\": \"" << curveToString(settings.size_curve) << "\",\n";
	s << "  \"size_curve_mid\": " << settings.size_curve_mid << ",\n";
	s << "  \"size_jitter\": " << settings.size_jitter << ",\n";
	s << "  \"opacity\": " << settings.opacity << ",\n";
	s << "  \"end_opacity\": " << settings.end_opacity << ",\n";
	s << "  \"opacity_curve\": \"" << curveToString(settings.opacity_curve) << "\",\n";
	s << "  \"opacity_curve_mid\": " << settings.opacity_curve_mid << ",\n";
	s << "  \"opacity_jitter\": " << settings.opacity_jitter << ",\n";
	s << "  \"colour\": \"" << colourToHex(settings.colour) << "\",\n";
	s << "  \"glow_strength\": " << settings.glow_strength << ",\n";
	s << "  \"rotation_deg\": " << settings.rotation_deg << ",\n";
	s << "  \"rotation_jitter_deg\": " << settings.rotation_jitter_deg << ",\n";
	s << "  \"spin_deg_per_sec\": " << settings.spin_deg_per_sec << ",\n";
	s << "  \"spin_jitter_deg_per_sec\": " << settings.spin_jitter_deg_per_sec << ",\n";
	s << "  \"burst_enabled\": " << (settings.burst_enabled ? "true" : "false") << ",\n";
	s << "  \"burst_count\": " << settings.burst_count << ",\n";
	s << "  \"burst_interval_s\": " << settings.burst_interval_s << ",\n";
	s << "  \"max_spawn_distance\": " << settings.max_spawn_distance << ",\n";
	s << "  \"wind_accel_x\": " << settings.wind_accel_x << ",\n";
	s << "  \"wind_accel_y\": " << settings.wind_accel_y << ",\n";
	s << "  \"wind_accel_z\": " << settings.wind_accel_z << ",\n";
	s << "  \"vortex_strength\": " << settings.vortex_strength << ",\n";
	s << "  \"attractor_strength\": " << settings.attractor_strength << ",\n";
	s << "  \"attractor_radius\": " << settings.attractor_radius << ",\n";
	s << "  \"black_hole_mode\": " << (settings.black_hole_mode ? "true" : "false") << ",\n";
	s << "  \"event_horizon_radius\": " << settings.event_horizon_radius << ",\n";
	s << "  \"radial_accel\": " << settings.radial_accel << ",\n";
	s << "  \"linear_damping\": " << settings.linear_damping << ",\n";
	s << "  \"buoyancy_lift\": " << settings.buoyancy_lift << ",\n";
	s << "  \"gravity_scale\": " << settings.gravity_scale << ",\n";
	s << "  \"drag_area\": " << settings.drag_area << ",\n";
	s << "  \"mass\": " << settings.mass << ",\n";
	s << "  \"restitution\": " << settings.restitution << ",\n";
	s << "  \"collision_friction\": " << settings.collision_friction << ",\n";
	s << "  \"collide_surfaces\": " << (settings.collide_surfaces ? "true" : "false") << ",\n";
	s << "  \"die_when_hit_surface\": " << (settings.die_when_hit_surface ? "true" : "false") << "\n";
	s << "}\n";
	return s.str();
}
