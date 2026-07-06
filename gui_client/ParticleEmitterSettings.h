/*=====================================================================
ParticleEmitterSettings.h
-------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <graphics/colour3.h>
#include <string>


struct ParticleEmitterSettings
{
	enum ParticleKind
	{
		ParticleKind_Smoke,
		ParticleKind_Foam,
		ParticleKind_Spark,
		ParticleKind_Streak,
		ParticleKind_Star,
		ParticleKind_Ring,
		ParticleKind_Nebula,
		ParticleKind_Flame,
		ParticleKind_Snowflake,
		ParticleKind_SoftDisc
	};

	enum Direction
	{
		Direction_Up,
		Direction_Forward,
		Direction_Down,
		Direction_Random,
		Direction_Custom
	};

	enum Shape
	{
		Shape_Point,
		Shape_Disc,
		Shape_Sphere,
		Shape_Box,
		Shape_Ring,
		Shape_Cylinder,
		Shape_Cone,
		Shape_Line,
		Shape_Hemisphere
	};

	enum Curve
	{
		Curve_Linear,
		Curve_EaseIn,
		Curve_EaseOut,
		Curve_SmoothStep,
		Curve_Custom
	};

	enum RenderMode
	{
		RenderMode_Soft,
		RenderMode_AdditiveGlow
	};

	ParticleEmitterSettings();

	static const char* contentMarker();
	static bool isParticleEmitterContent(const std::string& content);
	static ParticleEmitterSettings defaultSmoke();
	static ParticleEmitterSettings presetSettings(const std::string& preset_name);
	static ParticleEmitterSettings fromContent(const std::string& content, std::string* parse_error_out = 0);
	static std::string serialiseToContent(const ParticleEmitterSettings& settings);

	std::string preset_name;
	bool enabled;
	ParticleKind kind;
	Direction direction;
	Shape shape;
	RenderMode render_mode;
	std::string sprite_path;
	float custom_dir_x;
	float custom_dir_y;
	float custom_dir_z;
	float rate_per_sec;
	int max_spawn_per_frame;
	int max_particles;
	float emitter_radius;
	float speed;
	float speed_jitter;
	float spread_deg;
	float turbulence_strength;
	float lifetime_s;
	float start_width;
	float end_width;
	Curve size_curve;
	float size_curve_mid;
	float size_jitter;
	float opacity;
	float end_opacity;
	Curve opacity_curve;
	float opacity_curve_mid;
	float opacity_jitter;
	Colour3f colour;
	float glow_strength;
	float rotation_deg;
	float rotation_jitter_deg;
	float spin_deg_per_sec;
	float spin_jitter_deg_per_sec;
	bool burst_enabled;
	int burst_count;
	float burst_interval_s;
	float max_spawn_distance;
	float wind_accel_x;
	float wind_accel_y;
	float wind_accel_z;
	float vortex_strength;
	float attractor_strength;
	float attractor_radius;
	bool black_hole_mode;
	float event_horizon_radius;
	float radial_accel;
	float linear_damping;
	float buoyancy_lift;
	float gravity_scale;
	float drag_area;
	float mass;
	float restitution;
	float collision_friction;
	bool collide_surfaces;
	bool die_when_hit_surface;
};
