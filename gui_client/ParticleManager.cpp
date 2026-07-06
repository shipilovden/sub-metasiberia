/*=====================================================================
ParticleManager.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ParticleManager.h"


#include "PhysicsWorld.h"
#include "TerrainDecalManager.h"
#include "../shared/ResourceManager.h"
#include "../shared/Resource.h"
#include "../graphics/ImageMap.h"
#include <utils/FileUtils.h>
#include <utils/StringUtils.h>
#include <tracy/Tracy.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>


namespace
{

static float evaluateParticleCurve(const Particle::Curve curve, const float t_, const float custom_mid = 0.5f)
{
	const float t = myClamp(t_, 0.f, 1.f);
	switch(curve)
	{
	case Particle::Curve_Linear:     return t;
	case Particle::Curve_EaseIn:     return t * t;
	case Particle::Curve_EaseOut:    return 1.f - (1.f - t) * (1.f - t);
	case Particle::Curve_SmoothStep: return t * t * (3.f - 2.f * t);
	case Particle::Curve_Custom:
	{
		const float mid = myClamp(custom_mid, 0.f, 1.f);
		if(t < 0.5f)
			return 2.f * mid * t;
		else
			return mid + (1.f - mid) * (2.f * t - 1.f);
	}
	}
	return t;
}


static uint8 toByte(const float x)
{
	return (uint8)myClamp((int)std::round(myClamp(x, 0.f, 1.f) * 255.f), 0, 255);
}


static float smoothPulse(const float dist, const float centre, const float width)
{
	const float x = std::fabs(dist - centre) / myMax(width, 1.0e-4f);
	return myClamp(1.f - x, 0.f, 1.f);
}

} // anonymous namespace


std::string ParticleManager::normaliseSpritePath(const std::string& sprite_path) const
{
	size_t begin = 0;
	while(begin < sprite_path.size() && std::isspace((unsigned char)sprite_path[begin]))
		++begin;

	size_t end = sprite_path.size();
	while(end > begin && std::isspace((unsigned char)sprite_path[end - 1]))
		--end;

	std::string path = sprite_path.substr(begin, end - begin);
	std::replace(path.begin(), path.end(), '\\', '/');
	if(path.empty())
		return path;

	if(path[0] == '/' || FileUtils::isPathAbsolute(path))
		return path;

	if(hasPrefix(path, "resources/") || hasPrefix(path, "gl_data/"))
		return "/" + path;

	return path;
}


ParticleManager::ParticleManager(const std::string& base_dir_path_, ResourceManager* resource_manager_, AsyncTextureLoader* async_tex_loader_, OpenGLEngine* opengl_engine_, PhysicsWorld* physics_world_, TerrainDecalManager* terrain_decal_manager_)
:	base_dir_path(base_dir_path_), resource_manager(resource_manager_), opengl_engine(opengl_engine_), physics_world(physics_world_), terrain_decal_manager(terrain_decal_manager_),
	async_tex_loader(async_tex_loader_)
{
	ZoneScoped; // Tracy profiler

	TextureParams params;
	params.wrapping = OpenGLTexture::Wrapping::Wrapping_Clamp;

	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_top.basis",    this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_bottom.basis", this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_left.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_right.basis",  this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_rear.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_front.basis",  this, params));

	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_top.basis",    this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_bottom.basis", this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_left.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_right.basis",  this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_rear.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_front.basis",  this, params));
}


ParticleManager::~ParticleManager()
{
	// Cancel any pending async downloads.
	for(size_t i=0; i<loading_handles.size(); ++i)
		async_tex_loader->cancelLoadingTexture(loading_handles[i]);
	loading_handles.clear();

	clearParticles();
}


void ParticleManager::textureLoaded(Reference<OpenGLTexture> texture, const std::string& local_filename)
{
	// conPrint("ParticleManager::textureLoaded: local_filename: '" + local_filename + "'");

	if(     local_filename == "/resources/sprites/smoke_sprite_top.basis")    smoke_sprite_top    = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_bottom.basis") smoke_sprite_bottom = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_left.basis")   smoke_sprite_left   = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_right.basis")  smoke_sprite_right  = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_rear.basis")   smoke_sprite_rear   = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_front.basis")  smoke_sprite_front  = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_top.basis")     foam_sprite_top     = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_bottom.basis")  foam_sprite_bottom  = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_left.basis")    foam_sprite_left    = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_right.basis")   foam_sprite_right   = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_rear.basis")    foam_sprite_rear    = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_front.basis")   foam_sprite_front   = texture;
	else
	{
		custom_sprite_textures[local_filename] = texture;
		custom_sprite_loading_paths.erase(local_filename);
	}
}


void ParticleManager::clearParticles()
{
	for(size_t i=0; i<particles.size(); ++i)
		if(particles[i].gl_ob)
			opengl_engine->removeObject(particles[i].gl_ob);

	particles.clear();
}


void ParticleManager::clearParticlesForEmitter(UID emitter_uid)
{
	for(size_t i=0; i<particles.size();)
	{
		if(particles[i].emitter_uid == emitter_uid)
		{
			if(particles[i].gl_ob)
				opengl_engine->removeObject(particles[i].gl_ob);
			mySwap(particles[i], particles.back());
			particles.pop_back();
		}
		else
			++i;
	}
}


Reference<OpenGLTexture> ParticleManager::getOrLoadCustomSpriteTexture(const std::string& sprite_path)
{
	const std::string path = normaliseSpritePath(sprite_path);
	if(path.empty())
		return NULL;

	if(hasPrefix(path, "builtin:"))
		return getOrCreateBuiltinSpriteTexture(path);

	auto loaded_it = custom_sprite_textures.find(path);
	if(loaded_it != custom_sprite_textures.end())
		return loaded_it->second;

	TextureParams params;
	params.wrapping = OpenGLTexture::Wrapping::Wrapping_Clamp;

	const bool windows_abs_path = path.size() >= 3 && std::isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/';
	if(windows_abs_path || FileUtils::isPathAbsolute(path))
	{
		try
		{
			OpenGLTextureRef texture = opengl_engine->getTexture(path, params);
			custom_sprite_textures[path] = texture;
			return texture;
		}
		catch(glare::Exception& e)
		{
			conPrint("ParticleManager: failed to load custom particle sprite '" + path + "': " + e.what());
			return NULL;
		}
	}

	if(path[0] == '/')
	{
		if(custom_sprite_loading_paths.insert(path).second)
			loading_handles.push_back(async_tex_loader->startLoadingTexture(path, this, params));

		loaded_it = custom_sprite_textures.find(path);
		if(loaded_it != custom_sprite_textures.end())
			return loaded_it->second;

		return NULL;
	}

	if(resource_manager)
	{
		try
		{
			const URLString resource_url(path);
			ResourceRef resource = resource_manager->getExistingResourceForURL(resource_url);
			if(resource.nonNull() && resource->getState() == Resource::State_Present)
			{
				const std::string local_path = resource_manager->pathForURL(resource_url);
				OpenGLTextureRef texture = opengl_engine->getTexture(local_path, params);
				custom_sprite_textures[path] = texture;
				return texture;
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("ParticleManager: failed to load resource particle sprite '" + path + "': " + e.what());
			return NULL;
		}
	}

	return NULL;
}


Reference<OpenGLTexture> ParticleManager::getOrCreateBuiltinSpriteTexture(const std::string& sprite_path)
{
	auto loaded_it = builtin_sprite_textures.find(sprite_path);
	if(loaded_it != builtin_sprite_textures.end())
		return loaded_it->second;

	const std::string name = hasPrefix(sprite_path, "builtin:") ? sprite_path.substr(8) : sprite_path;
	const int W = 128;
	const int H = 128;
	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 4);
	map->zero();

	for(int y=0; y<H; ++y)
	{
		for(int x=0; x<W; ++x)
		{
			const float u = ((float)x + 0.5f) / (float)W;
			const float v = ((float)y + 0.5f) / (float)H;
			const float px = u * 2.f - 1.f;
			const float py = v * 2.f - 1.f;
			const float r = std::sqrt(px * px + py * py);
			const float ang = std::atan2(py, px);

			float rr = 1.f;
			float gg = 1.f;
			float bb = 1.f;
			float a = 0.f;

			if(name == "spark")
			{
				const float core = std::exp(-r * r * 18.f);
				const float streak = std::exp(-py * py * 90.f) * std::exp(-std::fabs(px) * 2.6f);
				a = myClamp(core + streak * 0.8f, 0.f, 1.f);
				rr = 1.f; gg = 0.72f; bb = 0.22f;
			}
			else if(name == "star")
			{
				const float core = std::exp(-r * r * 28.f);
				const float rays = std::pow(myClamp(std::fabs(std::cos(4.f * ang)), 0.f, 1.f), 18.f) * std::exp(-r * 2.7f);
				a = myClamp(core + rays * 0.7f, 0.f, 1.f);
				rr = 0.82f; gg = 0.92f; bb = 1.f;
			}
			else if(name == "ring")
			{
				const float ring = smoothPulse(r, 0.42f, 0.18f);
				const float core = std::exp(-r * r * 9.f) * 0.16f;
				a = myClamp(ring * ring + core, 0.f, 1.f);
				rr = 1.f; gg = 0.62f + 0.25f * std::cos(ang); bb = 0.22f + 0.18f * std::sin(ang * 2.f);
			}
			else if(name == "black_hole")
			{
				const float event_horizon = std::exp(-r * r * 42.f);
				const float inner_shadow = std::exp(-r * r * 13.f);
				const float ring = smoothPulse(r, 0.48f, 0.17f);
				const float hot_arc = std::pow(myClamp(0.5f + 0.5f * std::sin(2.8f * ang + 6.0f * r), 0.f, 1.f), 1.8f);
				a = myClamp(myMax(inner_shadow * 0.94f, ring * (0.72f + 0.28f * hot_arc)), 0.f, 1.f);
				const float ring_mix = myClamp(ring * (0.6f + 0.4f * hot_arc), 0.f, 1.f);
				rr = Maths::lerp(0.0f, 1.0f, ring_mix);
				gg = Maths::lerp(0.0f, 0.62f, ring_mix);
				bb = Maths::lerp(0.0f, 0.18f, ring_mix) + event_horizon * 0.03f;
			}
			else if(name == "nebula")
			{
				const float swirl = std::sin(9.f * r - 2.7f * ang) * 0.5f + 0.5f;
				const float cloud = std::exp(-r * r * (2.2f + 1.1f * swirl));
				a = myClamp(cloud * (0.58f + 0.42f * swirl), 0.f, 1.f);
				rr = 0.42f + 0.32f * swirl; gg = 0.36f + 0.28f * (1.f - swirl); bb = 1.f;
			}
			else if(name == "streak")
			{
				const float line = std::exp(-px * px * 80.f);
				const float tail = std::exp(-std::fabs(py) * 2.8f);
				a = myClamp(line * tail, 0.f, 1.f);
				rr = 0.75f; gg = 0.88f; bb = 1.f;
			}
			else if(name == "flame")
			{
				const float vertical = myClamp(1.f - (v * 1.08f), 0.f, 1.f);
				const float waist = std::exp(-(px * px) * (5.f + 9.f * v));
				a = myClamp(waist * std::pow(vertical, 0.45f) * (r < 0.98f ? 1.f : 0.f), 0.f, 1.f);
				rr = 1.f; gg = 0.24f + 0.58f * vertical; bb = 0.04f + 0.08f * vertical;
			}
			else if(name == "foam")
			{
				const float bubble = std::exp(-r * r * 8.f);
				const float rim = smoothPulse(r, 0.46f, 0.16f);
				a = myClamp(bubble * 0.55f + rim * 0.55f, 0.f, 1.f);
				rr = 0.82f; gg = 0.94f; bb = 1.f;
			}
			else if(name == "snowflake")
			{
				const float core = std::exp(-r * r * 24.f);
				const float arms = std::pow(myClamp(std::fabs(std::cos(3.f * ang)), 0.f, 1.f), 22.f) * std::exp(-r * 3.2f);
				a = myClamp(core + arms * 0.55f, 0.f, 1.f);
				rr = 0.96f; gg = 0.99f; bb = 1.f;
			}
			else if(name == "comet")
			{
				const float head = std::exp(-((px - 0.36f) * (px - 0.36f) + py * py) * 22.f);
				const float tail = std::exp(-myMax(0.f, px + 0.18f) * 0.8f) * std::exp(-py * py * (10.f + 8.f * myClamp(px + 1.f, 0.f, 1.f)));
				a = myClamp(head + tail * (px < 0.42f ? 0.82f : 0.f), 0.f, 1.f);
				rr = 0.78f + 0.22f * head; gg = 0.92f; bb = 1.f;
			}
			else if(name == "galaxy")
			{
				const float swirl = std::sin(12.f * r - 3.0f * ang);
				const float arms = std::pow(myClamp(0.5f + 0.5f * swirl, 0.f, 1.f), 2.2f) * std::exp(-r * 1.9f);
				const float core = std::exp(-r * r * 18.f);
				a = myClamp(core + arms * 0.85f, 0.f, 1.f);
				rr = 0.78f + 0.22f * core; gg = 0.58f + 0.26f * arms; bb = 1.f;
			}
			else if(name == "flare")
			{
				const float core = std::exp(-r * r * 32.f);
				const float horizontal = std::exp(-py * py * 380.f) * std::exp(-std::fabs(px) * 1.6f);
				const float vertical = std::exp(-px * px * 380.f) * std::exp(-std::fabs(py) * 1.6f);
				a = myClamp(core + horizontal * 0.72f + vertical * 0.35f, 0.f, 1.f);
				rr = 1.f; gg = 0.72f + 0.2f * core; bb = 0.28f + 0.45f * core;
			}
			else if(name == "beam")
			{
				const float line = std::exp(-px * px * 260.f);
				const float core = std::exp(-px * px * 60.f) * std::exp(-py * py * 1.4f);
				a = myClamp(line * (0.72f + 0.28f * std::cos(py * 18.f)) + core * 0.45f, 0.f, 1.f);
				rr = 0.42f; gg = 0.78f; bb = 1.f;
			}
			else if(name == "dust")
			{
				const float grain = std::sin((x * 12.9898f + y * 78.233f) * 0.11f) * 43758.5453f;
				const float noise = grain - std::floor(grain);
				const float cloud = std::exp(-r * r * 3.8f);
				a = myClamp(cloud * std::pow(noise, 3.0f) * 0.95f, 0.f, 1.f);
				rr = 0.72f + 0.28f * noise; gg = 0.62f + 0.25f * noise; bb = 0.92f;
			}
			else if(name == "spiral")
			{
				const float spiral = smoothPulse(std::sin(10.f * r - 2.4f * ang) * 0.5f + 0.5f, 0.72f, 0.18f);
				const float fade = std::exp(-r * r * 2.1f);
				a = myClamp((spiral * 0.95f + std::exp(-r * r * 18.f) * 0.38f) * fade, 0.f, 1.f);
				rr = 0.64f; gg = 0.36f + 0.35f * spiral; bb = 1.f;
			}
			else if(name == "aurora")
			{
				const float curtain = std::exp(-px * px * 7.f) * (0.62f + 0.38f * std::sin(17.f * v + 4.f * std::sin(5.f * u)));
				const float vertical_fade = std::pow(myClamp(1.f - std::fabs(py) * 0.78f, 0.f, 1.f), 1.2f);
				a = myClamp(curtain * vertical_fade, 0.f, 1.f);
				rr = 0.22f; gg = 1.f; bb = 0.58f + 0.32f * v;
			}
			else if(name == "bubble")
			{
				const float rim = smoothPulse(r, 0.58f, 0.12f);
				const float shine = std::exp(-((px + 0.28f) * (px + 0.28f) + (py + 0.32f) * (py + 0.32f)) * 70.f);
				a = myClamp(rim * 0.85f + shine * 0.75f, 0.f, 1.f);
				rr = 0.65f; gg = 0.9f; bb = 1.f;
			}
			else if(name == "shard")
			{
				const float blade = std::exp(-std::fabs(px + py * 0.26f) * 5.0f) * std::exp(-std::fabs(py) * 1.7f);
				const float tip = std::exp(-((py + 0.45f) * (py + 0.45f) + px * px * 2.f) * 10.f);
				a = myClamp((blade + tip * 0.35f) * (r < 0.98f ? 1.f : 0.f), 0.f, 1.f);
				rr = 0.86f; gg = 0.94f; bb = 1.f;
			}
			else if(name == "smoke_wisp")
			{
				const float wave = 0.5f + 0.5f * std::sin(8.f * py + 3.f * std::sin(5.f * px));
				const float plume = std::exp(-(px * px) * (3.2f + 5.5f * v)) * std::pow(myClamp(1.f - v * 0.92f, 0.f, 1.f), 0.65f);
				a = myClamp(plume * (0.45f + 0.55f * wave), 0.f, 1.f);
				rr = 0.72f; gg = 0.73f; bb = 0.76f;
			}
			else
			{
				a = std::exp(-r * r * 6.5f);
				rr = 1.f; gg = 1.f; bb = 1.f;
			}

			uint8* const pixel = map->getPixel((size_t)x, (size_t)y);
			pixel[0] = toByte(rr);
			pixel[1] = toByte(gg);
			pixel[2] = toByte(bb);
			pixel[3] = toByte(a);
		}
	}

	TextureParams params;
	params.wrapping = OpenGLTexture::Wrapping::Wrapping_Clamp;
	Reference<OpenGLTexture> texture = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__particle_builtin_" + name), *map, params);
	builtin_sprite_textures[sprite_path] = texture;
	return texture;
}


size_t ParticleManager::getNumParticlesForEmitter(UID emitter_uid) const
{
	size_t count = 0;
	for(size_t i=0; i<particles.size(); ++i)
		if(particles[i].emitter_uid == emitter_uid)
			++count;
	return count;
}


void ParticleManager::addParticle(const Particle& particle_)
{
	// conPrint("addParticle, particles.size(): " + toString(particles.size()));

	const size_t MAX_NUM_PARTICLES = 2048;

	size_t use_index;
	if(particles.size() >= MAX_NUM_PARTICLES) // If we have enough particles already:
	{
		use_index = rng.nextUInt((uint32)particles.size()); // Pick a random existing particle to replace

		// Remove existing particle at this index
		opengl_engine->removeObject(particles[use_index].gl_ob);
	}
	else
	{
		use_index = particles.size();
		particles.resize(use_index + 1);
	}


	// Add gl ob
	GLObjectRef ob = opengl_engine->allocateObject();
	ob->mesh_data = opengl_engine->getSpriteQuadMeshData();
	ob->materials.resize(1);
	const float glow = particle_.additive_glow ? particle_.glow_strength : 1.f;
	ob->materials[0].albedo_linear_rgb = Colour3f(particle_.colour.r * glow, particle_.colour.g * glow, particle_.colour.b * glow);
	ob->materials[0].alpha = particle_.cur_opacity;
	ob->materials[0].participating_media = true;
	ob->materials[0].alpha_blend = false;
	ob->materials[0].transparent = false;
	ob->materials[0].allow_alpha_test = false;
	ob->materials[0].emission_linear_rgb = particle_.additive_glow ? Colour3f(glow) : Colour3f(0.7f);
	ob->materials[0].emission_scale = particle_.additive_glow ? myMax(1.f, glow) : 1.f;
	ob->materials[0].cast_shadows = false;
	ob->materials[0].simple_double_sided = true;
	if(particle_.custom_sprite_texture.nonNull())
	{
		ob->materials[0].albedo_texture             = particle_.custom_sprite_texture;
		ob->materials[0].metallic_roughness_texture = particle_.custom_sprite_texture;
		ob->materials[0].lightmap_texture           = particle_.custom_sprite_texture;
		ob->materials[0].emission_texture           = particle_.custom_sprite_texture;
		ob->materials[0].backface_albedo_texture    = particle_.custom_sprite_texture;
		ob->materials[0].transmission_texture       = particle_.custom_sprite_texture;
	}
	else if(particle_.particle_type == Particle::ParticleType_Smoke)
	{
		ob->materials[0].albedo_texture             = smoke_sprite_top;
		ob->materials[0].metallic_roughness_texture = smoke_sprite_bottom;
		ob->materials[0].lightmap_texture           = smoke_sprite_left;
		ob->materials[0].emission_texture           = smoke_sprite_right;
		ob->materials[0].backface_albedo_texture    = smoke_sprite_rear;
		ob->materials[0].transmission_texture       = smoke_sprite_front;
	}
	else if(particle_.particle_type == Particle::ParticleType_Foam)
	{
		ob->materials[0].albedo_texture             = foam_sprite_top;
		ob->materials[0].metallic_roughness_texture = foam_sprite_bottom;
		ob->materials[0].lightmap_texture           = foam_sprite_left;
		ob->materials[0].emission_texture           = foam_sprite_right;
		ob->materials[0].backface_albedo_texture    = foam_sprite_rear;
		ob->materials[0].transmission_texture       = foam_sprite_front;
	}

	ob->materials[0].materialise_start_time = opengl_engine->getCurrentTime(); // For participating media and decals: materialise_start_time = spawn time
	ob->materials[0].dopacity_dt = particle_.use_lifetime_curve ? 0.f : particle_.dopacity_dt;

	ob->ob_to_world_matrix = Matrix4f::translationMatrix(particle_.pos) * Matrix4f::uniformScaleMatrix(particle_.width);
	ob->ob_to_world_matrix.e[1] = particle_.theta; // Since object-space vert positions are just (0,0,0) for particle geometry, we can store info in the model matrix.
	opengl_engine->addObject(ob);

	Particle particle = particle_;
	particle.gl_ob = ob;

	particles[use_index] = particle;
}


void ParticleManager::think(const float dt)
{
	//Timer timer;

	const bool water_buoyancy_enabled = physics_world->getWaterBuoyancyEnabled();
	const float water_z = physics_world->getWaterZ();

	for(size_t i=0; i<particles.size();)
	{
		Particle& particle = particles[i];

		assert(particle.pos.isFinite());

		const Vec4f pos_delta = particle.vel * dt;

		
		RayTraceResult results;
		results.hit_object = NULL;
		if(particle.collide_surfaces)
			physics_world->traceRay(particle.pos, particle.vel, dt, /*ignore body id=*/JPH::BodyID(), results);

		float remaining_dt = dt;
		if(results.hit_object)
		{
			const float to_hit_dt = results.hit_t;
			assert(to_hit_dt <= dt);
			remaining_dt -= to_hit_dt;

			const Vec4f hitpos = particle.pos + particle.vel * to_hit_dt;

			// Reflect velocity vector in hit normal
			particle.vel -= results.hit_normal_ws * (2 * dot(results.hit_normal_ws, particle.vel));
			particle.vel *= particle.restitution; // Apply restitution factor for inelastic collisions.
			if(particle.collision_friction > 0.f)
			{
				const Vec4f normal_vel = results.hit_normal_ws * dot(results.hit_normal_ws, particle.vel);
				const Vec4f tangent_vel = particle.vel - normal_vel;
				particle.vel = normal_vel + tangent_vel * (1.f - myClamp(particle.collision_friction, 0.f, 1.f));
			}

			assert(particle.pos.isFinite());
			assert(particle.vel.isFinite());

			particle.pos = hitpos + 
				results.hit_normal_ws * 1.0e-3f + // nudge off surface
				particle.vel * remaining_dt;

			assert(particle.pos.isFinite());
			assert(particle.vel.isFinite());

			if(particle.die_when_hit_surface)
				particle.cur_opacity = -1;
		}
		else
		{
			particle.pos += pos_delta;

			if(water_buoyancy_enabled && (particle.pos[2] < water_z))
			{
				if(particle.die_when_hit_surface && (particle.vel[2] < 0)) // If should die when hit surface, and are moving downwards:
				{
					particle.cur_opacity = -1;

					// Create foam decal at hit position
					Vec4f foam_pos = particle.pos;
					foam_pos[2] = water_z;
					terrain_decal_manager->addFoamDecal(foam_pos, /*width=*/particle.width, /*opacity=*/1.f, TerrainDecalManager::DecalType_SparseFoam);
				}

				// underwater
				particle.vel[2] = myMax(particle.vel[2], 0.5f); // apply buoyancy in a hacky way while not limiting positive z velocity (e.g. for water spray shooting out of water)
			}
			else
				particle.vel[2] -= 9.81f * particle.gravity_scale * dt; // Apply gravity
		}

		assert(particle.vel.isFinite());

		if(particle.turbulence_strength > 0.f)
		{
			const Vec4f turbulence_accel(
				-1.f + 2.f * rng.unitRandom(),
				-1.f + 2.f * rng.unitRandom(),
				-1.f + 2.f * rng.unitRandom(),
				0.f
			);
			particle.vel += turbulence_accel * (particle.turbulence_strength * dt);
		}

		particle.vel += particle.wind_accel * dt;
		particle.vel[2] += 9.81f * particle.buoyancy_lift * dt;

		Vec4f rel_to_force_centre = particle.pos - particle.force_centre;
		rel_to_force_centre[3] = 0.f;
		const float force_dist2 = rel_to_force_centre.length2();
		if(force_dist2 > 1.0e-8f)
		{
			const float force_dist = std::sqrt(force_dist2);
			const bool inside_force_radius = particle.attractor_radius <= 0.f || force_dist <= particle.attractor_radius;
			const float radius_falloff = particle.attractor_radius > 0.f ? myClamp(1.f - force_dist / particle.attractor_radius, 0.f, 1.f) : 1.f;

			if(particle.radial_accel != 0.f)
				particle.vel += normalise(rel_to_force_centre) * (particle.radial_accel * radius_falloff * dt);

			if(particle.vortex_strength != 0.f && inside_force_radius)
			{
				Vec4f tangent(-rel_to_force_centre[1], rel_to_force_centre[0], 0.f, 0.f);
				if(tangent.length2() > 1.0e-8f)
					particle.vel += normalise(tangent) * (particle.vortex_strength * radius_falloff * dt);
			}

			if(particle.attractor_strength != 0.f && inside_force_radius)
			{
				if(particle.black_hole_mode && particle.attractor_strength > 0.f)
				{
					const float safe_dist = myMax(force_dist, myMax(particle.event_horizon_radius, 0.05f));
					const float inverse_square_falloff = 1.f / myMax(safe_dist * safe_dist, 0.0025f);
					particle.vel += normalise(-rel_to_force_centre) * (particle.attractor_strength * inverse_square_falloff * dt);
					if(force_dist <= particle.event_horizon_radius)
						particle.cur_opacity = -1.f;
				}
				else
					particle.vel += normalise(-rel_to_force_centre) * (particle.attractor_strength * radius_falloff * dt);
			}
		}

		if(particle.linear_damping > 0.f)
			particle.vel *= std::exp(-particle.linear_damping * dt);

		// Apply wind-resistance drag force
		const float v_mag2 = particle.vel.length2();
		if(v_mag2 > Maths::square(1.0e-3f))
		{
			// ||a|| = F_d rho * ||v||^2 C_d A / m

			// dvel = -vel/||vel|| * ||a|| * dt    = vel * (||a|| * dt / ||v||)
			// vel' = vel + devl = vel - vel * (||a|| * dt / ||v||)
			// vel' = vel - vel * (F_d rho * ||v||^2 C_d A * dt / (m * ||v||))
			// vel' = vel - vel * (F_d rho * ||v|| C_d A * dt / m)
			// vel' = vel * (1 - F_d rho * ||v|| C_d A * dt / m)

			const float rho = 1.293f; // air density, kg m^-3
			const float projected_forwards_area = particle.area;
			const float forwards_C_d = 0.5f; // drag coefficient
			const float forwards_F_d = 0.5f * rho * v_mag2 * forwards_C_d * projected_forwards_area;
			const float mass = particle.mass;
			const float accel_mag = myMin(10.f, forwards_F_d / mass);

			// dvel = -vel/||vel|| * ||a|| * dt    = vel * (||a|| * dt / ||vel||)
			// vel' = vel + dvel = vel - vel * (||a|| * dt / ||vel||)
			// vel' = vel * (1 - (||a|| * dt / ||vel||))
			particle.vel *= myMax(0.f, 1.f - accel_mag * dt / std::sqrt(v_mag2));

			assert(particle.vel.isFinite());
		}

		assert(particle.pos.isFinite());
		assert(particle.vel.isFinite());
		
		if(particle.use_lifetime_curve)
		{
			particle.age_s += dt;
			const float lifetime = myMax(particle.lifetime_s, 0.001f);
			const float t = myClamp(particle.age_s / lifetime, 0.f, 1.f);
			particle.width = Maths::lerp(particle.start_width, particle.end_width, evaluateParticleCurve(particle.size_curve, t, particle.size_curve_mid));
			particle.cur_opacity = Maths::lerp(particle.start_opacity, particle.end_opacity, evaluateParticleCurve(particle.opacity_curve, t, particle.opacity_curve_mid));

			particle.gl_ob->materials[0].alpha = particle.cur_opacity;
			opengl_engine->updateAllMaterialDataOnGPU(*particle.gl_ob);
		}
		else
		{
			particle.cur_opacity += particle.dopacity_dt * dt;
			particle.width       += particle.dwidth_dt   * dt;
		}

		particle.theta += particle.theta_speed * dt;
		particle.gl_ob->ob_to_world_matrix = translationMulUniformScaleMatrix(/*translation=*/particle.pos, /*scale=*/particle.width);
		particle.gl_ob->ob_to_world_matrix.e[1] = particle.theta; // Since object-space vert positions are just (0,0,0) for particle geometry, we can store info in the model matrix.

		opengl_engine->updateObjectTransformData(*particle.gl_ob);

		// NOTE: changing alpha directly in shader based on particle lifetime now.
		//particle.gl_ob->materials[0].alpha = particle.cur_opacity;
		//opengl_engine->updateAllMaterialDataOnGPU(*particle.gl_ob); // Since opacity changed.

		if(particle.cur_opacity <= 0 || (particle.use_lifetime_curve && particle.age_s >= particle.lifetime_s))
		{
			//conPrint("removed particle");
			opengl_engine->removeObject(particle.gl_ob);

			// Remove particle: swap with last particle in array
			mySwap(particle, particles.back());
			particles.pop_back(); // Now remove last array element.
			
			// Don't increment i as we there is a new particle in position i that we want to process.
		}
		else
			++i;
	}

	//conPrint("ParticleManager::think() took " + timer.elapsedStringMSWIthNSigFigs(4) + " for " + toString(particles.size()) + " particles.");
}
