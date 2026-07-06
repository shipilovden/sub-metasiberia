/*=====================================================================
ParticleManager.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "../shared/UID.h"
#include <opengl/AsyncTextureLoader.h>
#include <opengl/IncludeOpenGL.h>
#include <opengl/OpenGLTexture.h>
#include <opengl/OpenGLEngine.h>
#include <maths/PCG32.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <map>
#include <set>
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;
class PhysicsWorld;
class BiomeManager;
class TerrainDecalManager;
class ResourceManager;


struct Particle
{
	GLARE_ALIGNED_16_NEW_DELETE

	enum ParticleType
	{
		ParticleType_Smoke,
		ParticleType_Foam
	};

	enum Curve
	{
		Curve_Linear,
		Curve_EaseIn,
		Curve_EaseOut,
		Curve_SmoothStep,
		Curve_Custom
	};

	Particle() : emitter_uid(UID::invalidUID()), restitution(0.5f), width(1.f), dwidth_dt(0.5f), cur_opacity(1.f), dopacity_dt(-0.3f), theta(0.f), theta_speed(0.f), colour(0.8f), mass(1.0e-6f), area(1.0e-6f),
		gravity_scale(1.f), turbulence_strength(0.f), wind_accel(0, 0, 0, 0), force_centre(0, 0, 0, 1), vortex_strength(0.f), attractor_strength(0.f), attractor_radius(0.f),
		radial_accel(0.f), linear_damping(0.f), buoyancy_lift(0.f), collision_friction(0.f),
		use_lifetime_curve(false), age_s(0.f), lifetime_s(1.f), start_width(1.f), end_width(1.f), start_opacity(1.f), end_opacity(0.f),
		size_curve(Curve_Linear), size_curve_mid(0.5f), opacity_curve(Curve_Linear), opacity_curve_mid(0.5f), collide_surfaces(true), die_when_hit_surface(false),
		black_hole_mode(false), event_horizon_radius(0.f), particle_type(ParticleType_Smoke), additive_glow(false), glow_strength(1.f) {}

	Vec4f pos;
	Vec4f vel;

	GLObjectRef gl_ob;
	UID emitter_uid;

	Colour3f colour;

	float area; // particle cross-sectional area (m^2).  Larger area = more wind drag.  TODO: just store ratio of area to mass?
	float mass;
	float restitution; // "Restitution of body (dimensionless number, usually between 0 and 1, 0 = completely inelastic collision response, 1 = completely elastic collision response)"
	float gravity_scale;
	float turbulence_strength;
	Vec4f wind_accel;
	Vec4f force_centre;
	float vortex_strength;
	float attractor_strength;
	float attractor_radius;
	float radial_accel;
	float linear_damping;
	float buoyancy_lift;
	float collision_friction;
	bool black_hole_mode;
	float event_horizon_radius;

	float width;
	float dwidth_dt;

	float cur_opacity;
	float dopacity_dt;

	float theta; // rotation around axis to camera
	float theta_speed;

	bool use_lifetime_curve;
	float age_s;
	float lifetime_s;
	float start_width;
	float end_width;
	float start_opacity;
	float end_opacity;
	Curve size_curve;
	float size_curve_mid;
	Curve opacity_curve;
	float opacity_curve_mid;

	bool collide_surfaces;
	bool die_when_hit_surface;

	ParticleType particle_type;
	bool additive_glow;
	float glow_strength;
	Reference<OpenGLTexture> custom_sprite_texture;
};


/*=====================================================================
ParticleManager
---------------
The basic idea is to simulate point particles with ray-traced collisions, and a simple physics model with 
bouncing off surfaces and with wind resistance.
See https://github.com/jrouwe/JoltPhysics/discussions/756 for a discussion of the approach.
=====================================================================*/
class ParticleManager final : public RefCounted, public AsyncTextureLoadedHandler
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	ParticleManager(const std::string& base_dir_path, ResourceManager* resource_manager, AsyncTextureLoader* async_tex_loader, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, TerrainDecalManager* terrain_decal_manager);
	~ParticleManager();

	void clearParticles();
	void clearParticlesForEmitter(UID emitter_uid);
	virtual void textureLoaded(Reference<OpenGLTexture> texture, const std::string& local_filename) override;
	Reference<OpenGLTexture> getOrLoadCustomSpriteTexture(const std::string& sprite_path);
	void addParticle(const Particle& particle);

	void think(float dt);
	size_t getNumParticles() const { return particles.size(); }
	size_t getNumParticlesForEmitter(UID emitter_uid) const;

private:
	std::string normaliseSpritePath(const std::string& sprite_path) const;
	Reference<OpenGLTexture> getOrCreateBuiltinSpriteTexture(const std::string& sprite_path);

	std::string base_dir_path;
	ResourceManager* resource_manager;
	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;
	TerrainDecalManager* terrain_decal_manager;
	PCG32 rng;
	std::vector<Particle> particles;

	Reference<OpenGLTexture> smoke_sprite_top;
	Reference<OpenGLTexture> smoke_sprite_bottom;
	Reference<OpenGLTexture> smoke_sprite_left;
	Reference<OpenGLTexture> smoke_sprite_right;
	Reference<OpenGLTexture> smoke_sprite_rear;
	Reference<OpenGLTexture> smoke_sprite_front;

	Reference<OpenGLTexture> foam_sprite_top;
	Reference<OpenGLTexture> foam_sprite_bottom;
	Reference<OpenGLTexture> foam_sprite_left;
	Reference<OpenGLTexture> foam_sprite_right;
	Reference<OpenGLTexture> foam_sprite_rear;
	Reference<OpenGLTexture> foam_sprite_front;

	std::map<std::string, Reference<OpenGLTexture> > custom_sprite_textures;
	std::set<std::string> custom_sprite_loading_paths;
	std::map<std::string, Reference<OpenGLTexture> > builtin_sprite_textures;

	AsyncTextureLoader* async_tex_loader;
	std::vector<AsyncTextureLoadingHandle> loading_handles;
};
