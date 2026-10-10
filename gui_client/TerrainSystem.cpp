/*=====================================================================
TerrainSystem.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainSystem.h"


#include "OpenGLEngine.h"
#include "OpenGLShader.h"
#include "BiomeManager.h"
#include "TerrainTests.h"
#include "graphics/PerlinNoise.h"
#include "PhysicsWorld.h"
#include "../shared/ImageDecoding.h"
#include "../shared/WorldSettings.h"
#include <utils/TaskManager.h>
#include <utils/Lock.h>
#include <utils/FileUtils.h>
#include <utils/ContainerUtils.h>
#include <utils/RuntimeCheck.h>
#include <utils/PlatformUtils.h>
#include "graphics/Voronoi.h"
#include "graphics/FormatDecoderGLTF.h"
#include "graphics/PNGDecoder.h"
#include "graphics/EXRDecoder.h"
#include "graphics/jpegdecoder.h"
#include "graphics/SRGBUtils.h"
#include "opengl/GLMeshBuilding.h"
#include "opengl/MeshPrimitiveBuilding.h"
#include "meshoptimizer/src/meshoptimizer.h"
#include "../dll/include/IndigoMesh.h"
#include <tracy/Tracy.hpp>
#include <algorithm>
#include <functional>
#include <limits>


// Build a single RGBA decal texture from TerrainGen's independent grayscale
// reference masks.  The alpha channel controls coverage; the RGB colour keeps
// roads and building footprints visually distinct from the terrain materials.
static float sampleReferenceMaskAtPixel(const Map2D* mask, size_t x, size_t y, size_t output_width, size_t output_height)
{
	if(!mask)
		return 0.f;

	// TerrainGen exports ordinary 8-bit PNGs.  Read those pixels directly so
	// applying a 1024^2 mask never performs millions of expensive filtered
	// samples on the UI/render thread.
	if(const ImageMapUInt8* image = dynamic_cast<const ImageMapUInt8*>(mask))
	{
		const size_t source_width = image->getWidth();
		const size_t source_height = image->getHeight();
		if(source_width > 0 && source_height > 0 && image->getN() > 0)
		{
			const size_t source_x = std::min(x * source_width / output_width, source_width - 1);
			const size_t source_y = std::min(y * source_height / output_height, source_height - 1);
			return (float)image->getPixel(source_x, source_y)[0] * (1.f / 255.f);
		}
	}

	const float u = ((float)x + 0.5f) / (float)output_width;
	const float v = ((float)y + 0.5f) / (float)output_height;
	return mask->sampleSingleChannelHighQual(u, v, /*channel=*/0, /*wrap=*/false);
}

static ImageMapUInt8Ref makeReferenceMaskOverlay(const Map2D* mask, uint8 red, uint8 green, uint8 blue, float alpha_scale)
{
	if(!mask || mask->getMapWidth() == 0 || mask->getMapHeight() == 0)
		return ImageMapUInt8Ref();

	const size_t width = mask->getMapWidth();
	const size_t height = mask->getMapHeight();
	ImageMapUInt8Ref overlay = new ImageMapUInt8(width, height, 4);

	for(size_t y=0; y<height; ++y)
	for(size_t x=0; x<width; ++x)
	{
		const float alpha = std::min(1.f, std::max(0.f, sampleReferenceMaskAtPixel(mask, x, y, width, height) * alpha_scale));

		uint8* const pixel = overlay->getPixel(x, y);
		pixel[0] = red;
		pixel[1] = green;
		pixel[2] = blue;
		pixel[3] = (uint8)(alpha * 255.f + 0.5f);
	}

	return overlay;
}


static float terrainIslandNoise(float u, float v, int seed)
{
	return (std::sin(u * 11.7f + v * 4.3f + seed * 0.17f) *
		std::sin(v * 9.1f - u * 3.7f + seed * 0.11f)) * 0.035f;
}


static float terrainIslandSmoothStep(float a, float b, float x)
{
	const float t = myClamp((x - a) / (b - a), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}


// Port of TerrainGen's island silhouette family. The terrain editor supplies
// the height profile and erosion separately; this helper defines each island's
// coastline in normalized local coordinates.
static float terrainIslandMask(int kind, float u, float v, int seed)
{
	const float noise = terrainIslandNoise(u, v, seed);
	const float n2 = terrainIslandNoise(u + 3.1f, v - 2.7f, seed + 17);
	const float angle = std::atan2(v, u);
	const float radius = std::sqrt(u*u + v*v);
	const float continental = std::sqrt(std::pow(u / 0.94f, 2.f) + std::pow(v / 0.76f, 2.f)) - 0.92f + noise;
	switch(kind)
	{
	case 0: return continental; // classic
	case 1: return std::sqrt(std::pow(u / 0.85f, 2.f) + std::pow(v / 0.68f, 2.f)) - 0.88f + noise; // high
	case 2: return std::sqrt(std::pow(u / 1.0f, 2.f) + std::pow(v / 0.82f, 2.f)) - 0.98f + noise; // low cay
	case 3: return std::fabs(u - v * 0.1f) / (0.21f + n2 * 0.04f) + std::max(0.f, std::fabs(v + u * 0.15f) - 0.55f) * 1.35f - 0.83f + noise;
	case 4:
	{
		const float a = std::sqrt(std::pow((u + 0.3f) / 0.43f, 2.f) + std::pow((v + 0.04f) / 0.58f, 2.f));
		const float b = std::sqrt(std::pow((u - 0.3f) / 0.43f, 2.f) + std::pow((v - 0.04f) / 0.58f, 2.f));
		return std::min(a, b) - 0.88f + noise;
	}
	case 5: return continental; // caldera: depression is applied to the height profile
	case 6: return std::sqrt(std::pow(u / 0.86f, 2.f) + std::pow(v / 0.72f, 2.f)) - 0.94f + noise; // mesa
	case 7:
	{
		float mask = continental;
		for(int k=0; k<5; ++k)
		{
			const float cx = -0.48f + k * 0.23f + std::sin(seed * 0.013f + k) * 0.035f;
			const float inlet = std::fabs(u - cx) / (0.055f + k * 0.003f) + std::max(0.f, v + 0.18f) / 0.68f - 1.f;
			mask = std::max(mask, -inlet);
		}
		return mask;
	}
	case 8: return std::min(std::sqrt(std::pow(u / 0.36f, 2.f) + std::pow((v - 0.22f) / 0.55f, 2.f)) - 0.74f,
		std::fabs(u - v * 0.28f) / 0.085f + std::max(0.f, -(v + 0.1f)) * 1.2f - 0.72f) + noise;
	case 9:
	{
		float mask = continental;
		const float hole_a = std::sqrt(std::pow(u - 0.34f, 2.f) + std::pow(v + 0.2f, 2.f));
		const float hole_b = std::sqrt(std::pow(u + 0.28f, 2.f) + std::pow(v - 0.24f, 2.f));
		if(hole_a < 0.15f) mask = std::max(mask, 0.16f - hole_a);
		if(hole_b < 0.12f) mask = std::max(mask, 0.13f - hole_b);
		return mask;
	}
	case 10:
	{
		float best = 9.f;
		for(int k=0; k<5; ++k)
		{
			const float t = k / 4.f;
			const float cx = -0.62f + t * 1.24f + std::sin(seed * 0.014f + k) * 0.05f;
			const float cy = 0.12f + std::sin(k * 1.7f + seed * 0.01f) * 0.16f;
			const float rx = 0.19f + (k % 2) * 0.035f, ry = 0.24f + (k % 3) * 0.025f;
			best = std::min(best, std::sqrt(std::pow((u-cx)/rx,2.f) + std::pow((v-cy)/ry,2.f)) - 0.78f + noise);
		}
		return best;
	}
	case 11: case 12: case 23:
	{
		float best = 9.f;
		const int count = kind == 23 ? 4 : (kind == 11 ? 1 : 1);
		for(int k=0; k<count; ++k)
		{
			const float cx = kind == 23 ? -0.42f + k * 0.28f : 0.f;
			const float cy = kind == 23 ? std::sin(k * 1.2f + seed * 0.01f) * 0.17f : 0.f;
			const float rr = std::sqrt(std::pow((u-cx)/(kind == 11 ? 0.72f : 0.7f),2.f) + std::pow((v-cy)/(kind == 11 ? 0.68f : 0.7f),2.f));
			const float ring = std::fabs(rr - (kind == 11 ? 0.56f : 0.55f)) - (kind == 11 ? 0.17f : 0.18f) + noise;
			best = std::min(best, ring);
		}
		return best;
	}
	case 13:
	{
		const float outer = std::sqrt(std::pow(u / 0.72f,2.f) + std::pow(v / 0.72f,2.f)) - 0.75f + noise;
		const float inner = std::sqrt(std::pow((u - 0.18f) / 0.46f,2.f) + std::pow(v / 0.46f,2.f)) - 0.56f;
		return std::max(outer, -inner);
	}
	case 14: return radius - (0.53f + 0.25f * std::cos(5.f * angle)) + noise;
	case 15:
	{
		const float spiral = std::fabs(std::fmod((angle / 6.2831853f + 0.5f) * 2.2f - radius * 1.1f + 2.f, 1.f) - 0.5f) * 2.2f;
		return std::min(std::fabs(spiral - 0.5f) * 2.2f, radius - 0.84f) - 0.2f + noise;
	}
	case 16: return std::fabs(v - std::sin(u * 3.2f + seed * 0.02f) * 0.34f - std::sin(u * 7.f) * 0.07f) / (0.14f + n2 * 0.02f) + std::max(0.f, std::fabs(u) - 0.84f) * 2.f - 0.88f;
	case 17:
	{
		float best = 9.f;
		for(int k=0; k<7; ++k)
		{
			const float cx = std::sin(seed * 0.02f + k * 1.7f) * 0.43f;
			const float cy = std::cos(seed * 0.017f + k * 1.3f) * 0.38f;
			const float r = 0.12f + ((seed + k * 9) % 5) * 0.025f;
			best = std::min(best, std::sqrt(std::pow((u-cx)/r,2.f) + std::pow((v-cy)/r,2.f)) - 0.72f + noise);
		}
		return best;
	}
	case 18: case 19: case 22: return continental;
	case 20:
	{
		const float stem = std::sqrt(std::pow(u / 0.18f,2.f) + std::pow(v / 0.5f,2.f)) - 0.82f;
		const float cap = std::sqrt(std::pow(u / 0.57f,2.f) + std::pow((v + 0.06f) / 0.4f,2.f)) - 0.83f + noise;
		return std::min(stem, cap);
	}
	case 21:
	{
		const float x = u * 1.1f, y = v * 1.1f + 0.1f;
		return (std::pow(x*x + y*y - 1.f, 3.f) - x*x*y*y*y) * 0.35f + noise;
	}
	default: return continental;
	}
}
TerrainSystem::TerrainSystem()
{
	num_uncompleted_tasks = 0;
	reference_mask_camera_z = 0.f;
	sculpt_stroke_active = false;
	sculpt_geometry_rebuild_pending = false;
	sculpt_material_mask_upload_pending = false;
	sculpt_tree_scattering_rebuild_pending = false;
}


TerrainSystem::~TerrainSystem()
{
}


// Pack normal into GL_INT_2_10_10_10_REV format.
inline static uint32 packNormal(const Vec4f& normal)
{
	const Vec4f scaled_normal = normal * 511.f;
	const Vec4i scaled_normal_int = toVec4i(scaled_normal);
	const int x = scaled_normal_int[0];
	const int y = scaled_normal_int[1];
	const int z = scaled_normal_int[2];
	// ANDing with 1023 isolates the bottom 10 bits.
	return (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20);
}


/*
  Consider terrain chunk below, currently at depth 2 in the tree.


      depth 1
----------------------                                 ^
                                                       | morph_end_dist
              depth 2 -> depth 1 transition region     |
                                                       |
  -  -  -  -  -  - -  -                                |  ^
       ______                                          |  |
      |      |    depth 2                              |  |
      |      |                                         |  | morph_start_dist  
      |______|                                         |  |      
         ^                                             |  |    
         |                                             |  |     
         | dist from camera to nearest point in chunk  |  |    
         |                                             |  |  
         v                                             v  v
         *                                                
         Camera                                                

As the nearest point approaches the depth 1 / depth 2 boundary, we want to continuously morph to the lower-detail representation that it will have at depth 1

So we will provide the following information to the vertex shader: the (2d) AABB of the chunk, to compute dist from camera to nearest point in chunk,
as well as the distance from the camera to the transition region (morph_start_dist), for the current depth (depth 2 in this example) and the depth to the depth 1 / depth 2 boundary (morph_end_dist)







screen space angle 

alpha ~= chunk_w / d

where d = ||campos - chunk_centre||

quad_res = 512 / (2 ^ chunk_lod_lvl)

quad_w = 2 ^ chunk_lod_lvl

quad_w_screenspace ~= quad_w / d

= 2 ^ chunk_lod_lvl / d

say we have some target quad_w_screenspace: quad_w_screenspace_target

quad_w_screenspace_target = 2 ^ chunk_lod_lvl / d

2 ^ chunk_lod_lvl = quad_w_screenspace_target * d

chunk_lod_lvl = log_2(quad_w_screenspace_target * d)

also

d = (2 ^ chunk_lod_lvl) / quad_w_screenspace_target


Say d = 1000, quad_w_screenspace_target = 0.001

then chunk_lod_lvl = log_2(0.001 * 1000) = log_2(1) = 0

Say d = 4000, quad_w_screenspace_target = 0.001

then chunk_lod_lvl = log_2(0.001 * 4000) = log_2(4) = 2


----------------------------
chunk_w = world_w / 2^depth

quad_w = chunk_w / res = world_w / (2^depth * res)

quad_w_screenspace ~= quad_w / d = world_w / (2^depth * res * d)

2^depth * res * d * quad_w_screenspace = world_w

2^depth = world_w / (res * d * quad_w_screenspace)

depth = log2(world_w / (res * d * quad_w_screenspace))

----

max depth quad_w = world_w / (chunk_res * 2^max_depth)
= 131072 / (128 * 2^10) = 131072 / (128 * 1024) = 1


*/

static const bool GEOMORPHING_SUPPORT = false;

//static float world_w = 131072;//8192*4;
static float world_w = 32768;//8192*4;   TODO: make this just large enough to enclose all defined terrain sections.
// static float CHUNK_W = 512.f;
static int chunk_res = 127; // quad res per patch
const float quad_w_screenspace_target = 0.032f;
//const float quad_w_screenspace_target = 0.004f;
static const int max_depth = 14;

static const float MAX_PHYSICS_DIST = 500.f; // Build physics objects for terrain chunks if the closest point on them to camera is <= MAX_PHYSICS_DIST away.

//static float world_w = 4096;
//// static float CHUNK_W = 512.f;
//static int chunk_res = 128; // quad res per patch
//const float quad_w_screenspace_target = 0.1f;
////const float quad_w_screenspace_target = 0.004f;
//static const int max_depth = 2;

// Scale factor for world-space -> heightmap UV conversion.
// Its reciprocal is the width of the terrain in metres.
//static const float terrain_section_w = 8 * 1024;
//static const float terrain_scale_factor = 1.f / terrain_section_w;


static Colour3f depth_colours[] = 
{
	Colour3f(1,0,0),
	Colour3f(0,1,0),
	Colour3f(0,0,1),
	Colour3f(1,1,0),
	Colour3f(0,1,1),
	Colour3f(1,0,1),
	Colour3f(0.2f,0.5f,1),
	Colour3f(1,0.5f,0.2f),
	Colour3f(0.5,1,0.5f),
};


typedef ImageMap<uint16, UInt16ComponentValueTraits> ImageMapUInt16;
typedef Reference<ImageMapUInt16> ImageMapUInt16Ref;


// Create index data for chunk, will be reused for all chunks
static IndexBufAllocationHandle createIndexBufferForChunkWithRes(OpenGLEngine* opengl_engine, int vert_res_with_borders)
{
	const int quad_res_with_borders = vert_res_with_borders - 1;

	js::Vector<uint16, 16> vert_index_buffer_uint16(quad_res_with_borders * quad_res_with_borders * 6);
	uint16* const indices = vert_index_buffer_uint16.data();
	for(int y=0; y<quad_res_with_borders; ++y)
	for(int x=0; x<quad_res_with_borders; ++x)
	{
		// Triangulate the quad in this way to match how Jolt triangulates the height field shape.
		// 
		// 
		// |----|
		// | \  |
		// |  \ |
		// |   \|
		// |----|--> x

		// bot left tri
		const int offset = (y*quad_res_with_borders + x) * 6;
		indices[offset + 0] = (uint16)(y       * vert_res_with_borders + x    ); // bot left
		indices[offset + 1] = (uint16)(y       * vert_res_with_borders + x + 1); // bot right
		indices[offset + 2] = (uint16)((y + 1) * vert_res_with_borders + x    ); // top left

		// top right tri
		indices[offset + 3] = (uint16)(y       * vert_res_with_borders + x + 1); // bot right
		indices[offset + 4] = (uint16)((y + 1) * vert_res_with_borders + x + 1); // top right
		indices[offset + 5] = (uint16)((y + 1) * vert_res_with_borders + x    ); // top left
	}

	return opengl_engine->vert_buf_allocator->allocateIndexDataSpace(vert_index_buffer_uint16.data(), vert_index_buffer_uint16.dataSizeBytes());
}


void TerrainSystem::init(const TerrainPathSpec& spec_, const std::string& base_dir_path, OpenGLEngine* opengl_engine_, PhysicsWorld* physics_world_, BiomeManager* biome_manager_, const Vec3d& campos, glare::TaskManager* task_manager_, 
	glare::StackAllocator& bump_allocator, ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_)
{
	spec = spec_;
	opengl_engine = opengl_engine_;
	physics_world = physics_world_;
	biome_manager = biome_manager_;
	task_manager = task_manager_;
	out_msg_queue = out_msg_queue_;

	terrain_section_w = spec_.terrain_section_width_m;
	terrain_scale_factor = 1.f / spec_.terrain_section_width_m;
	reference_mask_camera_z = (float)campos.z;

	next_id = 0;


	ImageMapUInt8Ref default_height_map = new ImageMapUInt8(1, 1, 1);
	default_height_map->getPixel(0, 0)[0] = 0;
	OpenGLTextureRef default_height_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__default_height_tex__"), *default_height_map);

	ImageMapUInt8Ref default_mask_map = new ImageMapUInt8(1, 1, 4);
	default_mask_map->getPixel(0, 0)[0] = 255;
	default_mask_map->getPixel(0, 0)[1] = 0;
	default_mask_map->getPixel(0, 0)[2] = 0;
	default_mask_map->getPixel(0, 0)[3] = 255;
	OpenGLTextureRef default_mask_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__default_mask_tex__"), *default_mask_map);


	// Set terrain_data_sections paths from spec
	for(size_t i=0; i<spec.section_specs.size(); ++i)
	{
		TerrainPathSpecSection& section_spec = spec.section_specs[i];
		const int dest_x = section_spec.x + TERRAIN_SECTION_OFFSET;
		const int dest_y = section_spec.y + TERRAIN_SECTION_OFFSET;

		if(dest_x < 0 || dest_x >= TERRAIN_DATA_SECTION_RES ||
			dest_y < 0 || dest_y >= TERRAIN_DATA_SECTION_RES)
		{
			conPrint("Warning: invalid section coords for terrain section spec, section spec will not be used.");
		}
		else
		{
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].heightmap_gl_tex = default_height_tex;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].mask_gl_tex      = default_mask_tex;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].heightmap_path = section_spec.heightmap_path;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].mask_map_path  = section_spec.mask_map_path;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].tree_mask_map_path  = section_spec.tree_mask_map_path;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].road_mask_map_path  = section_spec.road_mask_map_path;
			terrain_data_sections[dest_x + dest_y*TERRAIN_DATA_SECTION_RES].building_mask_map_path = section_spec.building_mask_map_path;
		}
	}

	// Set some default OpenGL terrain textures, to use before proper textures are loaded.
	{
		for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
		for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
		{
			TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];
			section.mask_gl_tex = default_mask_tex;
			section.sculpt_heightmap_texture_dirty = false;
			section.sculpt_maskmap_texture_dirty = false;
		}
	}
	{
		auto makeDefaultTerrainColour = [&](const std::string& key, uint8 r, uint8 g, uint8 b) -> OpenGLTextureRef
		{
			ImageMapUInt8Ref colour = new ImageMapUInt8(1, 1, 4);
			uint8* pixel = colour->getPixel(0, 0);
			pixel[0] = r;
			pixel[1] = g;
			pixel[2] = b;
			pixel[3] = 255;
			return opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey(key), *colour);
		};
		const OpenGLTextureRef default_ground = makeDefaultTerrainColour("__terrain_default_ground__", 174, 145, 96);
		const OpenGLTextureRef default_vegetation = makeDefaultTerrainColour("__terrain_default_vegetation__", 72, 112, 48);
		const OpenGLTextureRef default_spare = makeDefaultTerrainColour("__terrain_default_spare__", 128, 128, 128);
		const OpenGLTextureRef default_overlay = makeDefaultTerrainColour("__terrain_default_overlay__", 188, 160, 112);
		opengl_engine->setDetailTexture(0, default_spare);
		opengl_engine->setDetailTexture(1, default_ground);
		opengl_engine->setDetailTexture(2, default_vegetation);
		opengl_engine->setDetailTexture(3, default_overlay);
	}

	for(int i=0; i<4; ++i)
		opengl_engine->setDetailHeightmap(i, opengl_engine->dummy_black_tex);


	//detail_heightmap = JPEGDecoder::decode(".", "C:\\Users\\nick\\Downloads\\cgaxis_dirt_with_large_rocks_38_46_4K\\dirt_with_large_rocks_38_46_height.jpg");
	//detail_heightmap = PNGDecoder::decode("D:\\terrain\\GroundPack2\\SAND-08\\tex\\SAND-08-BEACH_DEPTH_2k.png");
	//detail_heightmap = PNGDecoder::decode("D:\\terrain\\GroundPack2\\SAND-11\\tex\\SAND-11-DUNES_DEPTH_2k.png");
	//small_dune_heightmap = PNGDecoder::decode("C:\\Users\\nick\\Downloads\\sand_ground_59_83_height.png");

	
	root_node = new TerrainNode();
	root_node->parent = NULL;
	root_node->aabb = js::AABBox(Vec4f(-world_w/2, -world_w/2, -1, 1), Vec4f(world_w/2, world_w/2, 1, 1));
	root_node->depth = 0;
	root_node->id = next_id++;
	id_to_node_map[root_node->id] = root_node.ptr();
	

	Timer timer;


	// Make material
	terrain_mat.roughness = 1;
	terrain_mat.geomorphing = true;
	//terrain_mat.albedo_texture = opengl_engine->getTexture("D:\\terrain\\colour.png", /*allow_compression=*/false);
	//terrain_mat.albedo_texture = opengl_engine->getTexture("D:\\terrain\\Colormap_0.png", /*allow_compression=*/false);
	//terrain_mat.albedo_texture = opengl_engine->getTexture("N:\\indigo\\trunk\\testscenes\\ColorChecker_sRGB_from_Ref.png", /*allow_compression=*/true);
	//terrain_mat.albedo_texture = opengl_engine->getTexture("C:\\programming\\terraingen\\vs2022_build\\colour.png", /*allow_compression=*/true);
	//terrain_mat.tex_matrix = Matrix2f(1.f/64, 0, 0, -1.f/64);
	terrain_mat.tex_matrix = Matrix2f(terrain_scale_factor, 0, 0, terrain_scale_factor);
	//terrain_mat.tex_translation = Vec2f(0.5f, 0.5f);
	terrain_mat.terrain = true;

	
	water_mat.water = true;

	//conPrint("Terrain init took " + timer.elapsedString() + " (" + toString(num_chunks) + " chunks)");


	//TEMP: create single large water object
	
#if 1
	if(BitUtils::isBitSet(spec.flags, TerrainSpec::WATER_ENABLED_FLAG))
	{
		// Keep one finely tessellated patch at the camera. Surround it with
		// progressively coarser rings and tiled distant water, so vertices near
		// the camera are never stretched across many kilometres.
		const int num_water_rects = 1 + 4 * 3 + (9 * 9 - 1);
		Reference<OpenGLMeshRenderData> middle_mesh = MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), 64);
		Reference<OpenGLMeshRenderData> far_mesh = MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), 32);
		for(int i=0; i<num_water_rects; ++i)
		{
			GLObjectRef ob = opengl_engine->allocateObject();
			ob->mesh_data = i == 0 ? MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), 256) : (i <= 8 ? middle_mesh : far_mesh);
			if(i == 0) { ob->mesh_data->aabb_os.min_[2] = -3.f; ob->mesh_data->aabb_os.max_[2] = 3.f; }
			ob->materials.resize(1);
			ob->materials[0] = water_mat;
			ob->ob_to_world_matrix = Matrix4f::identity();
			opengl_engine->addObject(ob);
			water_gl_obs.push_back(ob);
		}
		water_mesh_centre = Vec3d(1.e30);
		updateWaterMeshCentre(campos);



		// Create cylinder for water boundary
		{
			GLObjectRef gl_ob = opengl_engine->allocateObject();
			const float wall_h = 1000.0f;
			gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -wall_h) * Matrix4f::scaleMatrix(25000, 25000, wall_h);
			gl_ob->mesh_data = MeshPrimitiveBuilding::makeCylinderMesh(*opengl_engine->vert_buf_allocator.ptr(), /*end_caps=*/false);

			gl_ob->materials.resize(1);
			gl_ob->materials[0].albedo_linear_rgb = Colour3f(1,0,0);
			//gl_ob->materials[0] = water_mat;

			opengl_engine->addObject(gl_ob);
			water_gl_obs.push_back(gl_ob);
		}

		opengl_engine->getCurrentScene()->draw_water = true;
		opengl_engine->getCurrentScene()->water_level_z = spec.water_z; // Controls caustic drawing
	}
	else
	{
		opengl_engine->getCurrentScene()->draw_water = false;
		opengl_engine->getCurrentScene()->water_level_z = 0.0; // Controls caustic drawing
	}
#endif


	// Create index data for chunk, will be reused for all chunks
	this->vert_res_10_index_buffer  = createIndexBufferForChunkWithRes(opengl_engine, /*vert_res_with_borders=*/10);
	this->vert_res_130_index_buffer = createIndexBufferForChunkWithRes(opengl_engine, /*vert_res_with_borders=*/130);

	//testTerrainSystem(*this); // TEMP


	terrain_scattering.init(base_dir_path, this, opengl_engine_, physics_world, biome_manager_, campos, bump_allocator);
}

void TerrainSystem::updateReferenceMaskOverlay(int section_x, int section_y, TerrainDataSection& section)
{
	if(section.road_mask_decal_gl_ob.nonNull())
	{
		opengl_engine->removeObject(section.road_mask_decal_gl_ob);
		section.road_mask_decal_gl_ob = NULL;
	}
	if(section.building_mask_decal_gl_ob.nonNull())
	{
		opengl_engine->removeObject(section.building_mask_decal_gl_ob);
		section.building_mask_decal_gl_ob = NULL;
	}
	section.road_mask_gl_tex = NULL;
	section.building_mask_gl_tex = NULL;

	TextureParams texture_params;
	texture_params.use_sRGB = false;
	texture_params.allow_compression = false;
	// Reference masks are raster images.  Bilinear filtering removes the
	// visible 4 m / pixel stair-steps when a 2048 px mask covers an 8192 m
	// terrain section.  Mipmaps stay disabled so the lines do not disappear
	// when the camera is close to the terrain.
	texture_params.filtering = OpenGLTexture::Filtering_Bilinear;
	texture_params.wrapping = OpenGLTexture::Wrapping_Clamp;
	texture_params.use_mipmaps = false;

	// Terrain decals are projected onto the already-rendered terrain.  The
	// decal renderer needs the camera to remain outside the projection cube;
	// keep its top just below the current camera and update it as the camera
	// moves.  The terrain visible from the camera is then inside the cube.
	const float min_z = spec.default_terrain_z - 1000.f;
	const float max_z = std::max(min_z + 1.f, reference_mask_camera_z - 1.f);

	const float z_depth = std::max(1.f, max_z - min_z);
	const float z_centre = (min_z + max_z) * 0.5f;

	auto create_decal = [&](const OpenGLTextureRef& texture) -> GLObjectRef
	{
		if(texture.isNull())
			return GLObjectRef();

		GLObjectRef decal_ob = opengl_engine->allocateObject();
		decal_ob->mesh_data = opengl_engine->getCubeMeshData();
		decal_ob->materials.resize(1);
		decal_ob->materials[0].albedo_linear_rgb = Colour3f(1.f);
		decal_ob->materials[0].albedo_texture = texture;
		decal_ob->materials[0].simple_double_sided = true;
		decal_ob->materials[0].cast_shadows = false;
		decal_ob->materials[0].decal = true;
		decal_ob->ob_to_world_matrix =
			Matrix4f::translationMatrix(section_x * terrain_section_w, section_y * terrain_section_w, z_centre) *
			Matrix4f::scaleMatrix(terrain_section_w, terrain_section_w, z_depth) *
			Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
		opengl_engine->addObject(decal_ob);
		return decal_ob;
	};

	// Keep the two masks as separate decals.  A building mask must not replace
	// or recolour road pixels in the combined image.
	if(section.building_maskmap.nonNull())
	{
		// Keep reference building areas visible without the strong red/orange cast
		// that can be mistaken for a sculpting warning or a terrain height limit.
		ImageMapUInt8Ref building_overlay = makeReferenceMaskOverlay(section.building_maskmap.ptr(), 145, 151, 158, 0.28f);
		if(building_overlay.nonNull())
		{
			const size_t key_hash = std::hash<std::string>()(std::string("building|") + std::string(section.building_mask_map_path.begin(), section.building_mask_map_path.end()));
			const OpenGLTextureKey texture_key = OpenGLTextureKey("__terrain_building_mask_" + toString(section_x) + "_" + toString(section_y) + "_" + toString(key_hash));
			section.building_mask_gl_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(texture_key, *building_overlay, texture_params);
			section.building_mask_decal_gl_ob = create_decal(section.building_mask_gl_tex);
		}
	}

	if(section.road_maskmap.nonNull())
	{
		ImageMapUInt8Ref road_overlay = makeReferenceMaskOverlay(section.road_maskmap.ptr(), 24, 24, 24, 0.95f);
		if(road_overlay.nonNull())
		{
			const size_t key_hash = std::hash<std::string>()(std::string("road|") + std::string(section.road_mask_map_path.begin(), section.road_mask_map_path.end()));
			const OpenGLTextureKey texture_key = OpenGLTextureKey("__terrain_road_mask_" + toString(section_x) + "_" + toString(section_y) + "_" + toString(key_hash));
			section.road_mask_gl_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(texture_key, *road_overlay, texture_params);
			section.road_mask_decal_gl_ob = create_decal(section.road_mask_gl_tex);
		}
	}
}


void TerrainSystem::updateReferenceMaskDecalTransforms(float camera_z)
{
	reference_mask_camera_z = camera_z;

	const float min_z = spec.default_terrain_z - 1000.f;
	const float max_z = std::max(min_z + 1.f, reference_mask_camera_z - 1.f);
	const float z_depth = std::max(1.f, max_z - min_z);
	const float z_centre = (min_z + max_z) * 0.5f;

	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];
		const Matrix4f decal_transform =
			Matrix4f::translationMatrix((x - TERRAIN_SECTION_OFFSET) * terrain_section_w, (y - TERRAIN_SECTION_OFFSET) * terrain_section_w, z_centre) *
			Matrix4f::scaleMatrix(terrain_section_w, terrain_section_w, z_depth) *
			Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
		if(section.road_mask_decal_gl_ob.nonNull())
		{
			section.road_mask_decal_gl_ob->ob_to_world_matrix = decal_transform;
			opengl_engine->setObjectTransformData(*section.road_mask_decal_gl_ob);
		}
		if(section.building_mask_decal_gl_ob.nonNull())
		{
			section.building_mask_decal_gl_ob->ob_to_world_matrix = decal_transform;
			opengl_engine->setObjectTransformData(*section.building_mask_decal_gl_ob);
		}
	}
}


void TerrainSystem::handleTextureLoaded(const OpenGLTextureKey& path, const Map2DRef& map)
{
	// conPrint("TerrainSystem::handleTextureLoaded(): path: '" + toStdString(path) + "'");
	ZoneScoped; // Tracy profiler
	Lock heightmap_lock(heightmaps_mutex);

	assert(opengl_engine->isOpenGLTextureInsertedForKey(OpenGLTextureKey(path)));

	bool terrain_needs_rebuild = false;

	for(int i=0; i<4; ++i)
	{
		if(spec.detail_col_map_paths[i] == path)
		{
			opengl_engine->setDetailTexture(i, opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path)));
		}

		if(spec.detail_height_map_paths[i] == path)
		{
			opengl_engine->setDetailHeightmap(i, opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path)));

			detail_heightmaps[i] = map;
		}
	}

	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];
		bool reference_mask_changed = false;

		if(section.heightmap_path == path)
		{
			section.heightmap = map;
			section.heightmap_gl_tex = opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path));
			terrain_needs_rebuild = true;
			reference_mask_changed = true;
		}
		if(section.mask_map_path == path)
		{
			section.maskmap = map;
			section.mask_gl_tex = opengl_engine->getTextureIfLoaded(OpenGLTextureKey(path));
			terrain_needs_rebuild = true;
		}
		if(section.tree_mask_map_path == path)
		{
			section.treemaskmap = map;
			terrain_needs_rebuild = true;
		}
		if(section.road_mask_map_path == path)
		{
			section.road_maskmap = map;
			reference_mask_changed = true;
		}
		if(section.building_mask_map_path == path)
		{
			section.building_maskmap = map;
			reference_mask_changed = true;
		}

		if(reference_mask_changed)
			updateReferenceMaskOverlay(x - TERRAIN_SECTION_OFFSET, y - TERRAIN_SECTION_OFFSET, section);
	}

	if(terrain_needs_rebuild)
	{
		// Reload terrain:
		removeSubtree(root_node.ptr(), root_node->old_subtree_gl_obs, root_node->old_subtree_phys_obs);

		terrain_scattering.rebuild();
	}
}


bool TerrainSystem::isTextureUsedByTerrain(const OpenGLTextureKey& path) const
{
	for(int i=0; i<4; ++i)
	{
		if(spec.detail_col_map_paths[i] == path)
			return true;

		if(spec.detail_height_map_paths[i] == path)
			return true;
	}

	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];

		if(section.heightmap_path == path)
			return true;
		if(section.mask_map_path == path)
			return true;
		if(section.tree_mask_map_path == path)
			return true;
		if(section.road_mask_map_path == path)
			return true;
		if(section.building_mask_map_path == path)
			return true;
	}

	return false;
}


void TerrainSystem::rebuildScattering()
{
	terrain_scattering.rebuild();
}


void TerrainSystem::invalidateVegetationMap(const js::AABBox& aabb_ws)
{
	terrain_scattering.invalidateVegetationMap(aabb_ws);
}


ImageMapFloatRef TerrainSystem::makeEditableHeightmap(TerrainDataSection& section)
{
	if(section.sculpt_heightmap.nonNull())
		return section.sculpt_heightmap;

	const ImageMapFloat* source = dynamic_cast<const ImageMapFloat*>(section.heightmap.ptr());
	if(!source || source->getN() == 0)
	{
		const size_t resolution = 512;
		ImageMapFloatRef editable = new ImageMapFloat(resolution, resolution, 1);
		const float initial_height = std::fabs(spec.terrain_height_scale) > 1.0e-6f ?
			spec.default_terrain_z / spec.terrain_height_scale : 0.f;
		std::fill(editable->getData(), editable->getData() + editable->getDataSize(), initial_height);
		section.sculpt_heightmap = editable;
		section.heightmap = editable;
		section.sculpt_heightmap_texture_dirty = true;
		return editable;
	}

	ImageMapFloatRef editable = new ImageMapFloat(source->getWidth(), source->getHeight(), source->getN());
	std::copy(source->getData(), source->getData() + source->getDataSize(), editable->getData());
	editable->setGamma(source->getGamma());
	section.sculpt_heightmap = editable;
	section.heightmap = editable;
	return editable;
}


TerrainDataSection* TerrainSystem::getSectionForSculptCoords(int section_x, int section_y)
{
	const int array_x = section_x + TERRAIN_SECTION_OFFSET;
	const int array_y = section_y + TERRAIN_SECTION_OFFSET;
	if(array_x < 0 || array_x >= TERRAIN_DATA_SECTION_RES || array_y < 0 || array_y >= TERRAIN_DATA_SECTION_RES)
		return NULL;
	return &terrain_data_sections[array_x + array_y * TERRAIN_DATA_SECTION_RES];
}


const TerrainDataSection* TerrainSystem::getSectionForSculptCoords(int section_x, int section_y) const
{
	const int array_x = section_x + TERRAIN_SECTION_OFFSET;
	const int array_y = section_y + TERRAIN_SECTION_OFFSET;
	if(array_x < 0 || array_x >= TERRAIN_DATA_SECTION_RES || array_y < 0 || array_y >= TERRAIN_DATA_SECTION_RES)
		return NULL;
	return &terrain_data_sections[array_x + array_y * TERRAIN_DATA_SECTION_RES];
}


void TerrainSystem::applySculptPatch(const TerrainSculptPatch& patch, bool use_after_values)
{
	TerrainDataSection* section = getSectionForSculptCoords(patch.section_x, patch.section_y);
	if(!section)
		return;
	ImageMapFloatRef map = makeEditableHeightmap(*section);
	if(map.isNull())
		return;

	const std::vector<float>& values = use_after_values ? patch.after : patch.before;
	if(values.size() != (size_t)(patch.width * patch.height) || map->getN() == 0)
		return;

	for(int y=0; y<patch.height; ++y)
	for(int x=0; x<patch.width; ++x)
		map->getPixel((size_t)(patch.x0 + x), (size_t)(patch.y0 + y))[0] = values[(size_t)(x + y * patch.width)];
	section->sculpt_heightmap_texture_dirty = true;
}


void TerrainSystem::applySculptMaskPatch(const TerrainSculptMaskPatch& patch, bool use_after_values)
{
	TerrainDataSection* section = getSectionForSculptCoords(patch.section_x, patch.section_y);
	if(!section || patch.width < 1 || patch.height < 1 || patch.channels < 1)
		return;
	const std::vector<uint8>& values = use_after_values ? patch.after : patch.before;
	if(values.size() != (size_t)patch.width * (size_t)patch.height * (size_t)patch.channels)
		return;
	ImageMapUInt8Ref& map = patch.tree_mask ? section->sculpt_treemaskmap : section->sculpt_maskmap;
	if(map.isNull() || (int)map->getWidth() != patch.width || (int)map->getHeight() != patch.height || (int)map->getN() != patch.channels)
		map = new ImageMapUInt8((size_t)patch.width, (size_t)patch.height, patch.channels);
	std::copy(values.begin(), values.end(), map->getData());
	if(patch.tree_mask)
		sculpt_tree_scattering_rebuild_pending = true;
	else
	{
		section->sculpt_maskmap_texture_dirty = true;
		sculpt_material_mask_upload_pending = true;
	}
}


void TerrainSystem::beginSculptStroke()
{
	current_sculpt_stroke.patches.clear();
	current_sculpt_stroke.mask_patches.clear();
	sculpt_stroke_active = true;
}


void TerrainSystem::endSculptStroke()
{
	if(!sculpt_stroke_active)
		return;

	// Mask painting stores one full-map snapshot for each section touched during
	// this stroke. Capture the final state once here, rather than copying the
	// full map for every mouse-move sample.
	for(TerrainSculptMaskPatch& patch : current_sculpt_stroke.mask_patches)
	{
		if(!patch.after.empty())
			continue; // Generated island strokes already carry their after image.
		TerrainDataSection* section = getSectionForSculptCoords(patch.section_x, patch.section_y);
		if(!section)
			continue;
		const ImageMapUInt8Ref& map = patch.tree_mask ? section->sculpt_treemaskmap : section->sculpt_maskmap;
		if(map.nonNull() && map->getWidth() == (size_t)patch.width && map->getHeight() == (size_t)patch.height && map->getN() == patch.channels)
			patch.after.assign(map->getData(), map->getData() + map->getDataSize());
	}

	if(!current_sculpt_stroke.patches.empty() || !current_sculpt_stroke.mask_patches.empty())
	{
		const bool has_height_patches = !current_sculpt_stroke.patches.empty();
		bool has_tree_mask_patches = false;
		for(const TerrainSculptMaskPatch& patch : current_sculpt_stroke.mask_patches)
			has_tree_mask_patches = has_tree_mask_patches || patch.tree_mask;
		sculpt_undo_stack.push_back(std::move(current_sculpt_stroke));
		if(sculpt_undo_stack.size() > 8)
			sculpt_undo_stack.erase(sculpt_undo_stack.begin());
		sculpt_redo_stack.clear();
		if(has_height_patches)
			sculpt_geometry_rebuild_pending = true;
		if(has_tree_mask_patches)
			sculpt_tree_scattering_rebuild_pending = true;
	}
	current_sculpt_stroke.patches.clear();
	current_sculpt_stroke.mask_patches.clear();
	sculpt_stroke_active = false;
}


float TerrainSystem::getIslandPreviewHeight(int island_kind, float u, float v, int seed,
	float sea_floor_m, float land_base_m, float peak_m)
{
	const int island_seed = seed + island_kind * 97;
	const float mask = terrainIslandMask(island_kind, u, v, island_seed);
	const float edge = terrainIslandSmoothStep(0.12f, -0.55f, mask);
	const float centre_radius = std::sqrt(u*u + v*v);
	const float sea_z = myClamp(sea_floor_m, -160.f, -10.f);
	const float land_z = myClamp(land_base_m, 0.f, 50.f);
	const float peak_z = myClamp(peak_m, land_z + 6.f, 1200.f);
	const float shoreline = terrainIslandSmoothStep(0.02f, 0.28f, edge);
	const float hills = std::pow(terrainIslandSmoothStep(0.18f, 0.75f, edge), 1.4f);
	const float peaks = std::pow(terrainIslandSmoothStep(0.4f, 1.f, edge), 2.1f);
	float island_z = sea_z + shoreline * (land_z - sea_z) + hills * 12.f +
		peaks * (peak_z - land_z) * 0.68f;
	if(island_kind == 1)
		island_z = sea_z + shoreline * (land_z - sea_z) + std::pow(edge, 0.68f) * (peak_z - land_z);
	else if(island_kind == 2 || island_kind == 8)
		island_z = sea_z + shoreline * (land_z - sea_z) + edge * myMin(24.f, peak_z - land_z);
	else if(island_kind == 5)
	{
		if(centre_radius < 0.38f) island_z = land_z + 5.f + edge * 3.f;
		else if(centre_radius < 0.58f) island_z += (peak_z - land_z) * 0.45f * (1.f - std::fabs(centre_radius - 0.48f) / 0.1f);
	}
	else if(island_kind == 6 || island_kind == 19)
	{
		if(edge > 0.3f)
			island_z = land_z + (peak_z - land_z) * (island_kind == 6 ? 0.72f : 0.82f) + terrainIslandNoise(u*3.f, v*3.f, island_seed) * 4.f;
	}
	else if(island_kind == 10)
		island_z += (peak_z - land_z) * std::pow(edge, 1.15f) * 0.35f;
	else if(island_kind == 11 || island_kind == 12 || island_kind == 23)
		island_z = sea_z + shoreline * (land_z - sea_z) + edge * myMin(18.f, peak_z - land_z);
	else if(island_kind == 18)
	{
		const float spires = std::pow(myMax(0.f, std::sin(u * 17.f + island_seed) * std::sin(v * 14.f - island_seed * 0.3f)), 2.2f);
		island_z += spires * edge * (peak_z - land_z) * 0.4f;
	}
	else if(island_kind == 20)
	{
		const float stem = std::sqrt(std::pow(u / 0.18f,2.f) + std::pow(v / 0.5f,2.f));
		island_z = sea_z + shoreline * (land_z - sea_z) + (stem < 0.85f ? (peak_z - land_z) * 0.28f : edge * (peak_z - land_z));
	}
	else if(island_kind == 22)
	{
		const float needle = myMax(0.f, 1.f - centre_radius / 0.16f);
		island_z += std::pow(needle, 2.3f) * (peak_z - land_z) * 0.75f;
	}
	const float micro = terrainIslandNoise(u * 4.f + 1.3f, v * 4.f - 2.1f, island_seed + 41);
	island_z += micro * myClamp(edge * 2.5f, 0.f, 1.f) * myMin(7.f, (peak_z - land_z) * 0.06f);
	return island_z;
}


bool TerrainSystem::sculptAtWorld(const Vec3d& hit_pos, const Vec3d* previous_hit_pos, TerrainSculptTool tool,
	float radius_m, float strength_m, float target_height_m,
	float island_sea_floor_m, float island_land_base_m, float island_peak_m, int island_seed_value)
{
	radius_m = myClamp(radius_m, 0.25f, terrain_section_w * 0.5f);
	strength_m = myClamp(strength_m, 0.001f, 1000.f);
	if(!isFinite(radius_m) || !isFinite(strength_m) || !isFinite(target_height_m) ||
		!isFinite(spec.terrain_height_scale) || std::fabs(spec.terrain_height_scale) < 1.0e-6f)
		return false;

	// makeTerrainChunkMesh() reads these maps on worker threads.  Hold the
	// same lock for the entire stamp so the mesh builder cannot receive a
	// partially written float map or a half-swapped editable map reference.
	Lock heightmap_lock(heightmaps_mutex);

	if(!sculpt_stroke_active)
		beginSculptStroke();

	const double min_nx = (hit_pos.x - radius_m) / terrain_section_w + 0.5;
	const double max_nx = (hit_pos.x + radius_m) / terrain_section_w + 0.5;
	const double min_ny = (hit_pos.y - radius_m) / terrain_section_w + 0.5;
	const double max_ny = (hit_pos.y + radius_m) / terrain_section_w + 0.5;
	const int min_section_x = Maths::floorToInt(min_nx);
	const int max_section_x = Maths::floorToInt(max_nx);
	const int min_section_y = Maths::floorToInt(min_ny);
	const int max_section_y = Maths::floorToInt(max_ny);
	float ridge_dir_x = 1.f, ridge_dir_y = 0.f;
	if(previous_hit_pos)
	{
		ridge_dir_x = (float)(hit_pos.x - previous_hit_pos->x);
		ridge_dir_y = (float)(hit_pos.y - previous_hit_pos->y);
		const float ridge_dir_length = std::sqrt(ridge_dir_x * ridge_dir_x + ridge_dir_y * ridge_dir_y);
		if(ridge_dir_length > 1.0e-5f)
		{
			ridge_dir_x /= ridge_dir_length;
			ridge_dir_y /= ridge_dir_length;
		}
		else
		{
			ridge_dir_x = 1.f;
			ridge_dir_y = 0.f;
		}
	}
	bool changed = false;

	for(int section_y=min_section_y; section_y<=max_section_y; ++section_y)
	for(int section_x=min_section_x; section_x<=max_section_x; ++section_x)
	{
		TerrainDataSection* section = getSectionForSculptCoords(section_x, section_y);
		if(!section)
			continue;
		ImageMapFloatRef map = makeEditableHeightmap(*section);
		if(map.isNull() || map->getWidth() < 2 || map->getHeight() < 2)
			continue;

		const int width = (int)map->getWidth();
		const int height = (int)map->getHeight();
		const float section_u = (float)(hit_pos.x / terrain_section_w + 0.5 - section_x);
		const float section_v = (float)(hit_pos.y / terrain_section_w + 0.5 - section_y);
		const float pixel_cx = section_u * (width - 1);
		// ImageMap::sampleSingleChannelHighQual() flips its input V coordinate
		// before addressing raw pixels.  evalTerrainHeight() therefore passes
		// (1 - section_v), which resolves to the raw row section_v.  Use that
		// same row here: applying a second flip mirrors a sculpt stamp across the
		// section and makes the terrain change away from the brush cursor.
		const float pixel_cy = section_v * (height - 1);
		const float pixel_radius_x = radius_m / terrain_section_w * (width - 1);
		const float pixel_radius_y = radius_m / terrain_section_w * (height - 1);
		const int x0 = myMax(0, (int)std::floor(pixel_cx - pixel_radius_x) - 1);
		const int x1 = myMin(width - 1, (int)std::ceil(pixel_cx + pixel_radius_x) + 1);
		const int y0 = myMax(0, (int)std::floor(pixel_cy - pixel_radius_y) - 1);
		const int y1 = myMin(height - 1, (int)std::ceil(pixel_cy + pixel_radius_y) + 1);
		if(x1 < x0 || y1 < y0)
			continue;
		bool section_changed = false;
		const int source_x0 = myMax(0, x0 - 2);
		const int source_x1 = myMin(width - 1, x1 + 2);
		const int source_y0 = myMax(0, y0 - 2);
		const int source_y1 = myMin(height - 1, y1 + 2);
		const int source_width = source_x1 - source_x0 + 1;
		const int source_height = source_y1 - source_y0 + 1;
		std::vector<float> source_values((size_t)source_width * source_height);
		for(int sy=source_y0; sy<=source_y1; ++sy)
		for(int sx=source_x0; sx<=source_x1; ++sx)
			source_values[(size_t)(sx - source_x0) + (size_t)(sy - source_y0) * source_width] = map->getPixel((size_t)sx, (size_t)sy)[0];
		auto sourceAt = [&](int sx, int sy) -> float
		{
			sx = myClamp(sx, source_x0, source_x1);
			sy = myClamp(sy, source_y0, source_y1);
			return source_values[(size_t)(sx - source_x0) + (size_t)(sy - source_y0) * source_width];
		};
		float flatten_target = 0.f;
		int flatten_count = 0;
		if(tool == TerrainSculptTool_Flatten)
		{
			for(int y=y0; y<=y1; ++y)
			for(int x=x0; x<=x1; ++x)
			{
				const float world_x = (section_x + (float)x / (float)(width - 1) - 0.5f) * terrain_section_w;
				const float world_y = (section_y + (float)y / (float)(height - 1) - 0.5f) * terrain_section_w;
				const float dx = world_x - (float)hit_pos.x;
				const float dy = world_y - (float)hit_pos.y;
				if(dx * dx + dy * dy <= radius_m * radius_m)
				{
					flatten_target += sourceAt(x, y);
					flatten_count++;
				}
			}
			if(flatten_count > 0)
				flatten_target /= (float)flatten_count;
		}
		const float strength_raw = strength_m / spec.terrain_height_scale;
		const float plateau_target_raw = target_height_m / spec.terrain_height_scale;

		TerrainSculptPatch patch;
		patch.section_x = section_x;
		patch.section_y = section_y;
		patch.x0 = x0;
		patch.y0 = y0;
		patch.width = x1 - x0 + 1;
		patch.height = y1 - y0 + 1;
		patch.before.resize((size_t)patch.width * patch.height);
		patch.after.resize((size_t)patch.width * patch.height);

		for(int y=y0; y<=y1; ++y)
		for(int x=x0; x<=x1; ++x)
		{
			const float px_u = (float)x / (float)(width - 1);
			const float py_v = (float)y / (float)(height - 1);
			const float world_x = (section_x + px_u - 0.5f) * terrain_section_w;
			const float world_y = (section_y + py_v - 0.5f) * terrain_section_w;
			const float dx = world_x - (float)hit_pos.x;
			const float dy = world_y - (float)hit_pos.y;
			const float dist = std::sqrt(dx * dx + dy * dy);
			const size_t patch_i = (size_t)((x - x0) + (y - y0) * patch.width);
			const float old_height = sourceAt(x, y);
			patch.before[patch_i] = old_height;
			const float t = myClamp(dist / radius_m, 0.f, 1.f);
			if(t >= 1.f)
			{
				patch.after[patch_i] = old_height;
				continue;
			}
			const float falloff = std::pow(1.f - t, 1.65f);
			float new_height = old_height;
			if(tool == TerrainSculptTool_Raise)
				new_height += strength_raw * falloff;
			else if(tool == TerrainSculptTool_Lower)
				new_height -= strength_raw * falloff;
			else if(tool == TerrainSculptTool_SoftRaise)
				new_height += strength_raw * std::pow(falloff, 2.2f) * 0.7f;
			else if(tool == TerrainSculptTool_HardRaise)
				new_height += strength_raw * std::pow(falloff, 0.7f);
			else if(tool == TerrainSculptTool_SoftLower)
				new_height -= strength_raw * std::pow(falloff, 2.2f) * 0.7f;
			else if(tool == TerrainSculptTool_HardLower)
				new_height -= strength_raw * std::pow(falloff, 0.7f);
			else if(tool == TerrainSculptTool_Ridge)
			{
				const float across = std::fabs(-ridge_dir_y * dx + ridge_dir_x * dy);
				const float along = std::fabs(ridge_dir_x * dx + ridge_dir_y * dy);
				const float transverse = std::pow(myClamp(1.f - across / radius_m, 0.f, 1.f), 0.7f);
				const float endcap = myClamp(1.f - along / (radius_m * 1.25f), 0.f, 1.f);
				const float profile = myMax(transverse * 0.72f, std::pow(falloff, 0.7f)) * myMax(0.55f, endcap);
				new_height += strength_raw * profile;
			}
			else if(tool == TerrainSculptTool_Pinch)
			{
				const int center_x = myClamp((int)std::round(pixel_cx), 0, width - 1);
				const int center_y = myClamp((int)std::round(pixel_cy), 0, height - 1);
				new_height = old_height + (sourceAt(center_x, center_y) - old_height) * falloff * 0.55f;
			}
			else if(tool == TerrainSculptTool_Inflate)
				new_height += strength_raw * std::pow(1.f - t, 2.4f);
			else if(tool == TerrainSculptTool_Deflate)
				new_height -= strength_raw * std::pow(1.f - t, 2.4f);
			else if(tool == TerrainSculptTool_Clay)
				new_height += strength_raw * std::pow(1.f - t, 1.1f) * 0.55f;
			else if(tool == TerrainSculptTool_Blob)
				new_height += strength_raw * std::exp(-t * t * 3.2f);
			else if(tool == TerrainSculptTool_Amplify || tool == TerrainSculptTool_Dampen)
			{
				const int center_x = myClamp((int)std::round(pixel_cx), 0, width - 1);
				const int center_y = myClamp((int)std::round(pixel_cy), 0, height - 1);
				const float base = sourceAt(center_x, center_y);
				const float factor = tool == TerrainSculptTool_Amplify ? 1.35f : 0.55f;
				new_height = old_height + (base + (old_height - base) * factor - old_height) * falloff * 0.5f;
			}
			else if(tool == TerrainSculptTool_Smooth || tool == TerrainSculptTool_Polish || tool == TerrainSculptTool_Sharpen)
			{
				float average = 0.f;
				int samples = 0;
				for(int oy=-2; oy<=2; ++oy)
				for(int ox=-2; ox<=2; ++ox)
				{
					average += sourceAt(x + ox, y + oy);
					samples++;
				}
				average /= (float)samples;
				if(tool == TerrainSculptTool_Smooth)
					new_height = old_height + (average - old_height) * falloff * 0.6f;
				else if(tool == TerrainSculptTool_Polish)
					new_height = old_height + (average - old_height) * falloff * 0.85f;
				else
					new_height = old_height + (old_height - average) * falloff * (strength_m / 40.f);
			}
			else if(tool == TerrainSculptTool_Ramp)
			{
				const float along = (ridge_dir_x * dx + ridge_dir_y * dy) / radius_m;
				const float target = old_height + strength_raw * along * falloff;
				new_height = old_height + (target - old_height) * falloff * 0.75f;
			}
			else if(tool == TerrainSculptTool_Cliff)
			{
				const float across = (-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				const float along = std::fabs(ridge_dir_x * dx + ridge_dir_y * dy);
				const float cap = myClamp(1.f - along / (radius_m * 1.2f), 0.f, 1.f);
				const float step_t = myClamp((across + 0.15f) / 0.3f, 0.f, 1.f);
				const float step = step_t * step_t * (3.f - 2.f * step_t);
				const float target = old_height + strength_raw * (step - 0.5f) * 2.f;
				const float blend = std::pow(1.f - t, 1.1f) * cap * 0.9f;
				new_height = old_height + (target - old_height) * blend;
			}
			else if(tool == TerrainSculptTool_Wall)
			{
				const float across = std::fabs(-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				new_height += strength_raw * std::pow(myMax(0.f, 1.f - across), 3.2f) * std::pow(1.f - t, 1.1f);
			}
			else if(tool == TerrainSculptTool_Basin)
				new_height -= strength_raw * std::pow(1.f - t, 1.5f);
			else if(tool == TerrainSculptTool_Gorge)
			{
				const float across = std::fabs(-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				new_height -= strength_raw * std::pow(myMax(0.f, 1.f - across), 4.f) * std::pow(1.f - t, 1.2f) * 1.2f;
			}
			else if(tool == TerrainSculptTool_Berm)
			{
				const float ring = std::exp(-std::pow((t - 0.65f) / 0.18f, 2.f));
				new_height += strength_raw * ring * falloff;
			}
			else if(tool == TerrainSculptTool_Saddle)
				new_height -= strength_raw * std::pow(1.f - t, 2.f) * 0.8f;
			else if(tool == TerrainSculptTool_Notch)
			{
				const float across = std::fabs(-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				new_height -= strength_raw * std::exp(-std::pow(across / 0.12f, 2.f)) * falloff;
			}
			else if(tool == TerrainSculptTool_Lake)
			{
				const float bowl = std::pow(1.f - t, 1.4f);
				const float water = plateau_target_raw;
				if(old_height > water)
				{
					const float bed = old_height + (water - strength_raw * 0.15f * bowl - old_height) * bowl * 0.9f;
					new_height = std::min(old_height, std::max(water - strength_raw * 0.4f, bed));
				}
				else
					new_height = old_height + (std::min(old_height, water - 1.f) - old_height) * bowl * 0.3f;
				if(t < 0.72f)
					new_height += (std::min(new_height, water) - new_height) * (1.f - t / 0.72f) * 0.95f;
			}
			else if(tool == TerrainSculptTool_Fill)
				new_height += (plateau_target_raw - old_height) * falloff * 0.9f;
			else if(tool == TerrainSculptTool_Lowland)
				new_height += (plateau_target_raw - old_height) * falloff * 0.65f;
			else if(tool == TerrainSculptTool_Coast)
			{
				const float beach_target_raw = (spec.water_z + 2.5f) / spec.terrain_height_scale;
				new_height += (beach_target_raw - old_height) * falloff * 0.78f;
			}
			else if(tool == TerrainSculptTool_Shelf)
			{
				const float sea_raw = spec.water_z / spec.terrain_height_scale;
				const float shelf_target_raw = sea_raw * 0.2f + 4.f / spec.terrain_height_scale * falloff;
				new_height += (shelf_target_raw - old_height) * falloff * 0.7f;
			}
			else if(tool == TerrainSculptTool_Cove)
			{
				const float cove_target_raw = 1.5f / spec.terrain_height_scale;
				new_height += (std::min(old_height, cove_target_raw) - old_height) * std::pow(1.f - t, 1.3f) * 0.8f;
			}
			else if(tool == TerrainSculptTool_Spit)
			{
				const float along = std::fabs(ridge_dir_x * dx + ridge_dir_y * dy) / radius_m;
				const float across = std::fabs(-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				new_height += strength_raw * std::pow(myMax(0.f, 1.f - along), 1.5f) * std::pow(myMax(0.f, 1.f - across), 3.f) * 0.5f;
			}
			else if(tool == TerrainSculptTool_River)
			{
				const float sea_raw = spec.water_z / spec.terrain_height_scale;
				const float target = std::min(sea_raw, old_height - strength_raw * 0.35f);
				new_height += (target - old_height) * falloff * falloff * 0.75f;
			}
			else if(tool == TerrainSculptTool_Channel)
			{
				const float across = std::fabs(-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				new_height -= strength_raw * std::pow(myMax(0.f, 1.f - across), 3.f) * std::pow(1.f - t, 1.2f);
			}
			else if(tool == TerrainSculptTool_Delta)
			{
				const float angle = std::atan2(dy, dx);
				const float fan = std::pow(1.f - t, 1.2f) * (0.6f + 0.4f * std::cos(angle * 3.f));
				const float sea_raw = spec.water_z / spec.terrain_height_scale;
				new_height += (std::min(old_height, sea_raw + (3.f + strength_m * 0.1f) / spec.terrain_height_scale) - old_height) * fan * 0.7f;
			}
			else if(tool == TerrainSculptTool_Sea)
			{
				const float sea_floor_raw = (spec.water_z - 40.f) / spec.terrain_height_scale;
				new_height += (sea_floor_raw - old_height) * falloff * 0.82f;
			}
			else if(tool >= TerrainSculptTool_StampVolcano && tool <= TerrainSculptTool_StampRamp)
			{
				const float along = (ridge_dir_x * dx + ridge_dir_y * dy) / radius_m;
				const float across = (-ridge_dir_y * dx + ridge_dir_x * dy) / radius_m;
				const float safe_t = myClamp(t, 0.f, 1.f);
				if(tool == TerrainSculptTool_StampVolcano)
				{
					const float cone = std::pow(1.f - safe_t, 1.35f);
					const float crater = safe_t < 0.18f ? (1.f - safe_t / 0.18f) * strength_raw * 0.35f : 0.f;
					new_height += (strength_raw * 1.1f * cone - crater) * std::pow(1.f - safe_t, 0.6f);
				}
				else if(tool == TerrainSculptTool_StampCrater)
				{
					const float rim = std::exp(-std::pow((safe_t - 0.55f) / 0.18f, 2.f)) * strength_raw * 0.9f;
					const float hole = std::pow(1.f - myMin(1.f, safe_t / 0.45f), 2.f) * strength_raw * 0.7f;
					new_height += rim - hole * std::pow(1.f - safe_t, 0.5f);
				}
				else if(tool == TerrainSculptTool_StampHill)
					new_height += strength_raw * std::pow(1.f - safe_t, 2.f);
				else if(tool == TerrainSculptTool_StampCone)
					new_height += strength_raw * (1.f - safe_t);
				else if(tool == TerrainSculptTool_StampMesa)
				{
					const float top = safe_t < 0.55f ? 1.f : std::pow(myMax(0.f, 1.f - (safe_t - 0.55f) / 0.45f), 1.5f);
					new_height += strength_raw * top * 0.9f;
				}
				else if(tool == TerrainSculptTool_StampCaldera)
				{
					const float rim = std::exp(-std::pow((safe_t - 0.5f) / 0.16f, 2.f)) * strength_raw;
					const float floor = safe_t < 0.4f ? strength_raw * 0.45f * (1.f - safe_t / 0.4f) : 0.f;
					new_height += rim - floor;
				}
				else if(tool == TerrainSculptTool_StampRidge)
					new_height += strength_raw * std::pow(myMax(0.f, 1.f - std::fabs(across)), 2.2f) * std::pow(1.f - safe_t, 1.2f);
				else if(tool == TerrainSculptTool_StampSpire)
					new_height += strength_raw * std::pow(1.f - safe_t, 3.5f);
				else if(tool == TerrainSculptTool_StampPyramid)
					new_height += strength_raw * myMax(0.f, 1.f - myMax(std::fabs(dx), std::fabs(dy)) / radius_m);
				else if(tool == TerrainSculptTool_StampBowl)
					new_height -= strength_raw * std::pow(1.f - safe_t, 1.5f) * 0.9f;
				else if(tool == TerrainSculptTool_StampArch)
				{
					const float left = std::sqrt(std::pow(along + 0.35f, 2.f) + across * across) / 0.28f;
					const float right = std::sqrt(std::pow(along - 0.35f, 2.f) + across * across) / 0.28f;
					new_height += strength_raw * std::pow(myMax(0.f, 1.f - myMin(left, right)), 1.5f) * falloff;
				}
				else if(tool == TerrainSculptTool_StampAtoll)
				{
					const float ring = std::exp(-std::pow((safe_t - 0.55f) / 0.14f, 2.f));
					new_height += strength_raw * ring * 0.7f - (safe_t < 0.4f ? strength_raw * 0.25f * (1.f - safe_t / 0.4f) : 0.f);
				}
				else if(tool == TerrainSculptTool_StampTwin)
				{
					const float d1 = std::sqrt(std::pow(along + 0.35f, 2.f) + across * across);
					const float d2 = std::sqrt(std::pow(along - 0.35f, 2.f) + across * across);
					new_height += strength_raw * myMax(std::pow(myMax(0.f, 1.f - d1), 2.f), std::pow(myMax(0.f, 1.f - d2), 2.f));
				}
				else if(tool == TerrainSculptTool_StampDunes)
					new_height += (std::sin(world_x * 0.2f + world_y * 0.05f) * 0.4f + 0.15f) * strength_raw * falloff;
				else if(tool == TerrainSculptTool_StampTor)
				{
					const float noise = std::sin(std::floor(world_x / 3.f) * 127.1f + std::floor(world_y / 3.f) * 311.7f) * 43758.5453f;
					const float unit_noise = noise - std::floor(noise);
					if(unit_noise > 0.55f)
						new_height += strength_raw * (unit_noise - 0.55f) * 2.f * std::pow(1.f - safe_t, 2.f);
				}
				else if(tool == TerrainSculptTool_StampRamp)
					new_height += strength_raw * (along * 0.5f + 0.5f) * std::pow(1.f - safe_t, 1.2f);
			}
			else if(tool >= TerrainSculptTool_IslandClassic && tool <= TerrainSculptTool_IslandLagoonChain)
			{
				const int island_kind = (int)tool - (int)TerrainSculptTool_IslandClassic;
				const float u = dx / radius_m;
				const float v = dy / radius_m;
				const float coast_mask = terrainIslandMask(island_kind, u, v, island_seed_value + island_kind * 97);
				const float island_z = getIslandPreviewHeight(island_kind, u, v, island_seed_value,
					island_sea_floor_m, island_land_base_m, island_peak_m);
				// The island only owns its land footprint.  Preserve the surrounding
				// terrain so a stamp on a flat section does not leave a circular ocean pit.
				const float land_coverage = 1.f - terrainIslandSmoothStep(-0.18f, 0.12f, coast_mask);
				const float island_height_raw = island_z / spec.terrain_height_scale;
				new_height = old_height + myMax(0.f, island_height_raw - old_height) * land_coverage;
			}
			else if(tool == TerrainSculptTool_Flatten)
				new_height = old_height + (flatten_target - old_height) * std::pow(1.f - t, 1.2f) * 0.85f;
			else if(tool == TerrainSculptTool_Plateau)
				new_height = old_height + (plateau_target_raw - old_height) * std::pow(1.f - t, 1.2f) * 0.85f;

			patch.after[patch_i] = new_height;
			map->getPixel((size_t)x, (size_t)y)[0] = new_height;
			if(std::fabs(new_height - old_height) > 1.0e-6f)
			{
				changed = true;
				section_changed = true;
			}
		}

		if(section_changed)
		{
			section->sculpt_heightmap_texture_dirty = true;
			current_sculpt_stroke.patches.push_back(std::move(patch));
		}
	}

	if(tool >= TerrainSculptTool_IslandClassic && tool <= TerrainSculptTool_IslandLagoonChain)
	{
		const int island_kind = (int)tool - (int)TerrainSculptTool_IslandClassic;
		const int mask_seed = island_seed_value + island_kind * 97;
		const auto applyGeneratedMask = [&](TerrainDataSection& section, int section_x, int section_y, bool tree_mask)
		{
			ImageMapUInt8Ref& editable = tree_mask ? section.sculpt_treemaskmap : section.sculpt_maskmap;
			Map2DRef source = tree_mask ? section.treemaskmap : section.maskmap;
			if(editable.isNull())
			{
				const ImageMapUInt8* source_u8 = dynamic_cast<const ImageMapUInt8*>(source.ptr());
				
				const ImageMapFloat* heightmap = section.sculpt_heightmap.nonNull() ? section.sculpt_heightmap.ptr() :
					dynamic_cast<const ImageMapFloat*>(section.heightmap.ptr());
				const size_t width = source_u8 && source_u8->getN() >= (tree_mask ? 1 : 3) ? source_u8->getWidth() : heightmap ? heightmap->getWidth() : 256;
				const size_t height = source_u8 && source_u8->getN() >= (tree_mask ? 1 : 3) ? source_u8->getHeight() : heightmap ? heightmap->getHeight() : 256;
				const int actual_channels = tree_mask ? (source_u8 && source_u8->getN() >= 1 ? (int)source_u8->getN() : 1) : 4;
				editable = new ImageMapUInt8(width, height, actual_channels);
				if(tree_mask && source_u8 && source_u8->getN() >= 1)
					std::copy(source_u8->getData(), source_u8->getData() + source_u8->getDataSize(), editable->getData());
				else if(!tree_mask && source_u8 && source_u8->getN() >= 3)
				{
					for(size_t py=0; py<height; ++py)
					for(size_t px=0; px<width; ++px)
					{
						const uint8* src = source_u8->getPixel(px, py);
						uint8* dst = editable->getPixel(px, py);
						dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
						dst[3] = source_u8->getN() >= 4 ? src[3] : 255;
					}
				}
				else
				{
					for(size_t py=0; py<height; ++py)
					for(size_t px=0; px<width; ++px)
					{
						uint8* pixel = editable->getPixel(px, py);
						if(tree_mask) pixel[0] = 255;
						else { pixel[0] = 255; pixel[1] = pixel[2] = 0; pixel[3] = 255; }
					}
				}
			}
			if(editable->getWidth() < 2 || editable->getHeight() < 2 || editable->getN() < (tree_mask ? 1 : 4))
				return;

			TerrainSculptMaskPatch mask_patch;
			mask_patch.section_x = section_x;
			mask_patch.section_y = section_y;
			mask_patch.tree_mask = tree_mask;
			mask_patch.width = (int)editable->getWidth();
			mask_patch.height = (int)editable->getHeight();
			mask_patch.channels = (int)editable->getN();
			mask_patch.before.assign(editable->getData(), editable->getData() + editable->getDataSize());
			mask_patch.after = mask_patch.before;
			bool mask_changed = false;
			const int mask_channels = mask_patch.channels;
			for(int py=0; py<mask_patch.height; ++py)
			for(int px=0; px<mask_patch.width; ++px)
			{
				const float wx = (section_x + (float)px / (mask_patch.width - 1) - 0.5f) * terrain_section_w;
				const float wy = (section_y + (float)py / (mask_patch.height - 1) - 0.5f) * terrain_section_w;
				const float u = (wx - (float)hit_pos.x) / radius_m;
				const float v = (wy - (float)hit_pos.y) / radius_m;
				if(u*u + v*v >= 1.f)
					continue;
				const float coastline = terrainIslandMask(island_kind, u, v, mask_seed);
				const float coverage = 1.f - terrainIslandSmoothStep(-0.18f, 0.12f, coastline);
				if(coverage <= 0.001f)
					continue;
				const ImageMapFloat* heightmap = section.sculpt_heightmap.nonNull() ? section.sculpt_heightmap.ptr() :
					dynamic_cast<const ImageMapFloat*>(section.heightmap.ptr());
				const size_t height_x = heightmap ? std::min((size_t)std::round((float)px / (mask_patch.width - 1) * (heightmap->getWidth() - 1)), heightmap->getWidth() - 1) : 0;
				const size_t height_y = heightmap ? std::min((size_t)std::round((float)py / (mask_patch.height - 1) * (heightmap->getHeight() - 1)), heightmap->getHeight() - 1) : 0;
				const float z = heightmap ? heightmap->getPixel(height_x, height_y)[0] * spec.terrain_height_scale :
					getIslandPreviewHeight(island_kind, u, v, island_seed_value, island_sea_floor_m, island_land_base_m, island_peak_m);
				const float height_fraction = myClamp((z - island_land_base_m) / myMax(1.f, island_peak_m - island_land_base_m), 0.f, 1.f);
				const float sand = 1.f - terrainIslandSmoothStep(0.02f, 0.14f, height_fraction);
				const float rock = terrainIslandSmoothStep(0.52f, 0.88f, height_fraction);
				const float vegetation = myMax(0.f, 1.f - sand - rock);
				const float total = myMax(0.001f, sand + rock + vegetation);
				const uint8* before_pixel = mask_patch.before.data() + ((size_t)px + (size_t)py * mask_patch.width) * mask_channels;
				uint8* after_pixel = mask_patch.after.data() + ((size_t)px + (size_t)py * mask_patch.width) * mask_channels;
				if(tree_mask)
				{
					const float density_noise = terrainIslandNoise(u * 14.f + 8.f, v * 14.f - 5.f, mask_seed + 113);
					const uint8 target = vegetation > 0.34f && density_noise > -0.12f ? 255 : 0;
					after_pixel[0] = (uint8)myClamp((int)std::round(before_pixel[0] + (target - (float)before_pixel[0]) * coverage), 0, 255);
				}
				else
			{
				const float target[3] = { rock / total, sand / total, vegetation / total };
				for(int c=0; c<3; ++c)
					after_pixel[c] = (uint8)myClamp((int)std::round(before_pixel[c] + (target[c] * 255.f - before_pixel[c]) * coverage), 0, 255);
				}
				if(std::memcmp(before_pixel, after_pixel, (size_t)mask_channels) != 0)
					mask_changed = true;
			}
			if(!mask_changed)
				return;
			std::copy(mask_patch.after.begin(), mask_patch.after.end(), editable->getData());
			if(!tree_mask)
				section.sculpt_maskmap_texture_dirty = true;
			current_sculpt_stroke.mask_patches.push_back(std::move(mask_patch));
			changed = true;
		};

		for(int section_y=min_section_y; section_y<=max_section_y; ++section_y)
		for(int section_x=min_section_x; section_x<=max_section_x; ++section_x)
			if(TerrainDataSection* section = getSectionForSculptCoords(section_x, section_y))
			{
				applyGeneratedMask(*section, section_x, section_y, false);
				applyGeneratedMask(*section, section_x, section_y, true);
			}
	}

	return changed;
}


bool TerrainSystem::paintTerrainMapAtWorld(const Vec3d& hit_pos, float radius_m, float strength,
	int channel, bool tree_mask, bool erase)
{
	if(!std::isfinite(hit_pos.x) || !std::isfinite(hit_pos.y) || !std::isfinite(radius_m) ||
		!std::isfinite(strength) || radius_m < 0.25f || radius_m > terrain_section_w * 0.5f ||
		strength <= 0.f || strength > 1.f || (!tree_mask && (channel < 0 || channel > 3)))
		return false;

	const bool auto_end_stroke = !sculpt_stroke_active;
	if(auto_end_stroke)
		beginSculptStroke();
	Lock heightmap_lock(heightmaps_mutex);
	const int min_sx = Maths::floorToInt((hit_pos.x - radius_m) / terrain_section_w + 0.5);
	const int max_sx = Maths::floorToInt((hit_pos.x + radius_m) / terrain_section_w + 0.5);
	const int min_sy = Maths::floorToInt((hit_pos.y - radius_m) / terrain_section_w + 0.5);
	const int max_sy = Maths::floorToInt((hit_pos.y + radius_m) / terrain_section_w + 0.5);
	bool changed = false;
	for(int sy=min_sy; sy<=max_sy; ++sy)
	for(int sx=min_sx; sx<=max_sx; ++sx)
	{
		TerrainDataSection* section = getSectionForSculptCoords(sx, sy);
		if(!section)
			continue;
		ImageMapUInt8Ref& editable = tree_mask ? section->sculpt_treemaskmap : section->sculpt_maskmap;
		Map2DRef source = tree_mask ? section->treemaskmap : section->maskmap;
		if(editable.isNull())
		{
			const ImageMapUInt8* source_u8 = dynamic_cast<const ImageMapUInt8*>(source.ptr());
			if(source_u8 && source_u8->getN() >= (tree_mask ? 1 : 3))
			{
				const int channels = tree_mask ? (int)source_u8->getN() : 4;
				editable = new ImageMapUInt8(source_u8->getWidth(), source_u8->getHeight(), channels);
				if(tree_mask)
					std::copy(source_u8->getData(), source_u8->getData() + source_u8->getDataSize(), editable->getData());
				else
					for(size_t py=0; py<source_u8->getHeight(); ++py)
					for(size_t px=0; px<source_u8->getWidth(); ++px)
					{
						const uint8* src = source_u8->getPixel(px, py);
						uint8* dst = editable->getPixel(px, py);
						dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
						dst[3] = source_u8->getN() >= 4 ? src[3] : 255;
					}
			}
			else
			{
				const ImageMapFloat* heightmap = section->sculpt_heightmap.nonNull() ? section->sculpt_heightmap.ptr() :
					dynamic_cast<const ImageMapFloat*>(section->heightmap.ptr());
				const size_t map_width = heightmap ? heightmap->getWidth() : 256;
				const size_t map_height = heightmap ? heightmap->getHeight() : 256;
				const int num_channels = tree_mask ? 1 : 4;
				editable = new ImageMapUInt8(map_width, map_height, num_channels);
				for(size_t py=0; py<map_height; ++py)
				for(size_t px=0; px<map_width; ++px)
				{
					uint8* pixel = editable->getPixel(px, py);
					if(tree_mask)
						pixel[0] = section->tree_mask_map_path.empty() ? 255 : 0;
					else
					{
						pixel[0] = 255;
						pixel[1] = pixel[2] = 0;
						pixel[3] = 255;
					}
				}
			}
		}
		if(editable->getWidth() < 2 || editable->getHeight() < 2 || editable->getN() < (tree_mask ? 1 : 4))
			continue;

		TerrainSculptMaskPatch first_touch_patch;
		bool already_recorded = false;
		for(const TerrainSculptMaskPatch& patch : current_sculpt_stroke.mask_patches)
			if(patch.section_x == sx && patch.section_y == sy && patch.tree_mask == tree_mask)
			{
				already_recorded = true;
				break;
			}
		const bool record_first_touch = !already_recorded;
		if(record_first_touch)
		{
			first_touch_patch.section_x = sx;
			first_touch_patch.section_y = sy;
			first_touch_patch.tree_mask = tree_mask;
			first_touch_patch.width = (int)editable->getWidth();
			first_touch_patch.height = (int)editable->getHeight();
			first_touch_patch.channels = (int)editable->getN();
			first_touch_patch.before.assign(editable->getData(), editable->getData() + editable->getDataSize());
		}

		const int width = (int)editable->getWidth();
		const int height = (int)editable->getHeight();
		const float u = (float)(hit_pos.x / terrain_section_w + 0.5 - sx);
		const float v = (float)(hit_pos.y / terrain_section_w + 0.5 - sy);
		const float cx = u * (width - 1), cy = v * (height - 1);
		const float rx = radius_m / terrain_section_w * (width - 1);
		const float ry = radius_m / terrain_section_w * (height - 1);
		const int x0 = myMax(0, (int)std::floor(cx - rx));
		const int x1 = myMin(width - 1, (int)std::ceil(cx + rx));
		const int y0 = myMax(0, (int)std::floor(cy - ry));
		const int y1 = myMin(height - 1, (int)std::ceil(cy + ry));
		const ImageMapUInt8* original_u8 = dynamic_cast<const ImageMapUInt8*>(source.ptr());
		bool section_changed = false;
		for(int y=y0; y<=y1; ++y)
		for(int x=x0; x<=x1; ++x)
		{
			const float wx = (sx + (float)x / (width - 1) - 0.5f) * terrain_section_w;
			const float wy = (sy + (float)y / (height - 1) - 0.5f) * terrain_section_w;
			const float dx = wx - (float)hit_pos.x, dy = wy - (float)hit_pos.y;
			const float t = std::sqrt(dx*dx + dy*dy) / radius_m;
			if(t >= 1.f) continue;
			const float blend = strength * std::pow(1.f - t, 1.65f);
			uint8* pixel = editable->getPixel((size_t)x, (size_t)y);
			if(tree_mask)
			{
				const uint8 original = original_u8 ? original_u8->getPixel((size_t)x, (size_t)y)[0] :
					(section->tree_mask_map_path.empty() ? 255 : 0);
				const uint8 target = erase ? original : (channel == 0 ? 255 : 0);
				const uint8 value = (uint8)myClamp((int)std::round(pixel[0] + (target - (float)pixel[0]) * blend), 0, 255);
				if(value != pixel[0]) { pixel[0] = value; changed = true; section_changed = true; }
			}
			else if(erase)
			{
				for(int c=0; c<4; ++c)
				{
					const uint8* original_pixel = original_u8 ? original_u8->getPixel((size_t)x, (size_t)y) : NULL;
					const uint8 target = original_pixel && c < (int)original_u8->getN() ? original_pixel[c] : (c == 0 ? 255 : c == 3 ? 255 : 0);
					const uint8 value = (uint8)myClamp((int)std::round(pixel[c] + (target - (float)pixel[c]) * blend), 0, 255);
					if(value != pixel[c]) { pixel[c] = value; changed = true; section_changed = true; }
				}
			}
			else if(channel == 3)
			{
				// Alpha stores inverse overlay weight, so RGB base layers remain intact.
				const uint8 value = (uint8)myClamp((int)std::round(pixel[3] * (1.f - blend)), 0, 255);
				if(value != pixel[3]) { pixel[3] = value; changed = true; section_changed = true; }
			}
			else
			{
				const int old_selected = pixel[channel];
				const int target_selected = myClamp((int)std::round(old_selected + (255 - old_selected) * blend), 0, 255);
				const int remaining_before = 255 - old_selected;
				const int remaining_after = 255 - target_selected;
				for(int c=0; c<3; ++c)
				{
					const int value = c == channel ? target_selected :
						(remaining_before > 0 ? (int)std::round(pixel[c] * (float)remaining_after / remaining_before) : 0);
					if(value != pixel[c]) { pixel[c] = (uint8)myClamp(value, 0, 255); changed = true; section_changed = true; }
				}
			}
		}
		if(section_changed && record_first_touch)
			current_sculpt_stroke.mask_patches.push_back(std::move(first_touch_patch));
	}
	if(changed)
	{
		if(tree_mask)
			sculpt_tree_scattering_rebuild_pending = true;
		else
			sculpt_material_mask_upload_pending = true;
		// TerrainScattering also reads the blue vegetation weight channel. Rebuild it
		// after vegetation painting/restoring so ground cover follows the edited mask.
		if(!tree_mask && (channel == 2 || erase))
			sculpt_tree_scattering_rebuild_pending = true;
		for(int sy=min_sy; sy<=max_sy; ++sy)
		for(int sx=min_sx; sx<=max_sx; ++sx)
			if(TerrainDataSection* section = getSectionForSculptCoords(sx, sy))
			{
				if(!tree_mask && section->sculpt_maskmap.nonNull())
					section->sculpt_maskmap_texture_dirty = true;
			}
	}
	if(auto_end_stroke)
		endSculptStroke();
	return changed;
}


bool TerrainSystem::isHeightmapSectionLoaded(int section_x, int section_y) const
{
	Lock heightmap_lock(heightmaps_mutex);
	const TerrainDataSection* section = getSectionForSculptCoords(section_x, section_y);
	if(!section)
		return false;
	const ImageMapFloat* map = dynamic_cast<const ImageMapFloat*>(section->heightmap.ptr());
	return map && map->getN() == 1 && map->getWidth() >= 3 && map->getHeight() >= 3;
}


bool TerrainSystem::smoothAtWorld(const Vec3d& centre, float radius_m, float strength, int passes,
	int* loaded_sections_out, int* skipped_sections_out)
{
	if(loaded_sections_out) *loaded_sections_out = 0;
	if(skipped_sections_out) *skipped_sections_out = 0;
	if(!std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(radius_m) || !std::isfinite(strength) ||
		radius_m < 1.f || radius_m > terrain_section_w * 0.5f || strength <= 0.f || strength > 1.f || passes < 1 || passes > 8)
		return false;

	struct SectionWork
	{
		int sx, sy, x0, y0, x1, y1;
		ImageMapFloatRef map;
		std::vector<float> before;
		std::vector<float> values;
	};

	Lock heightmap_lock(heightmaps_mutex);
	endSculptStroke();
	std::vector<SectionWork> sections;
	const int min_sx = Maths::floorToInt((centre.x - radius_m) / terrain_section_w + 0.5);
	const int max_sx = Maths::floorToInt((centre.x + radius_m) / terrain_section_w + 0.5);
	const int min_sy = Maths::floorToInt((centre.y - radius_m) / terrain_section_w + 0.5);
	const int max_sy = Maths::floorToInt((centre.y + radius_m) / terrain_section_w + 0.5);
	for(int sy=min_sy; sy<=max_sy; ++sy)
	for(int sx=min_sx; sx<=max_sx; ++sx)
	{
		const double nearest_x=myClamp(centre.x, (sx-0.5)*terrain_section_w, (sx+0.5)*terrain_section_w);
		const double nearest_y=myClamp(centre.y, (sy-0.5)*terrain_section_w, (sy+0.5)*terrain_section_w);
		const double dx=nearest_x-centre.x, dy=nearest_y-centre.y;
		if(dx*dx+dy*dy > (double)radius_m*radius_m) continue;
		TerrainDataSection* section = getSectionForSculptCoords(sx, sy);
		// A brush centred on an outer terrain edge also overlaps section slots
		// that have no terrain map.  Skip those slots and edit the loaded part of
		// the footprint; failing the whole operation made edge smoothing
		// impossible even when the requested edge itself was loaded.
		if(!section)
		{
			if(skipped_sections_out) ++*skipped_sections_out;
			continue;
		}
		const ImageMapFloat* source = dynamic_cast<const ImageMapFloat*>(section->heightmap.ptr());
		if(!source || source->getN() != 1 || source->getWidth() < 3 || source->getHeight() < 3)
		{
			if(skipped_sections_out) ++*skipped_sections_out;
			continue;
		}
		ImageMapFloatRef map = makeEditableHeightmap(*section);
		if(map.isNull())
		{
			if(skipped_sections_out) ++*skipped_sections_out;
			continue;
		}
		const size_t w = map->getWidth(), h = map->getHeight();
		const double cx = (centre.x / terrain_section_w + 0.5 - sx) * (w - 1);
		const double cy = (centre.y / terrain_section_w + 0.5 - sy) * (h - 1);
		const double prx = radius_m / terrain_section_w * (w - 1), pry = radius_m / terrain_section_w * (h - 1);
		SectionWork work;
		work.sx=sx; work.sy=sy; work.map=map;
		work.x0=myMax(0, (int)std::floor(cx-prx)-1); work.x1=myMin((int)w-1, (int)std::ceil(cx+prx)+1);
		work.y0=myMax(0, (int)std::floor(cy-pry)-1); work.y1=myMin((int)h-1, (int)std::ceil(cy+pry)+1);
		work.before.assign(map->getData(), map->getData()+map->getDataSize());
		work.values=work.before;
		sections.push_back(std::move(work));
	}
	if(sections.empty()) return false;
	if(loaded_sections_out) *loaded_sections_out = (int)sections.size();

	beginSculptStroke();
	bool changed = false;
	for(SectionWork& work : sections)
	{
		const int w=(int)work.map->getWidth(), h=(int)work.map->getHeight();
		const float cx=(float)((centre.x/terrain_section_w+0.5-work.sx)*(w-1));
		const float cy=(float)((centre.y/terrain_section_w+0.5-work.sy)*(h-1));
		const float prx=radius_m/terrain_section_w*(w-1), pry=radius_m/terrain_section_w*(h-1);
		std::vector<float> next(work.values.size());
		for(int pass=0; pass<passes; ++pass)
		{
			next=work.values;
			for(int y=work.y0; y<=work.y1; ++y)
			for(int x=work.x0; x<=work.x1; ++x)
			{
				const float dx=(x-cx)/prx, dy=(y-cy)/pry;
				const float dist=std::sqrt(dx*dx+dy*dy);
				if(dist>=1.f) continue;
				const float t=dist;
				const float falloff=(1.f-t)*(1.f-t)*(2.f*t+1.f);
				float sum=0.f, weights=0.f;
				for(int oy=-1; oy<=1; ++oy)
				for(int ox=-1; ox<=1; ++ox)
				{
					const int px=myClamp(x+ox, 0, w-1), py=myClamp(y+oy, 0, h-1);
					const float weight=(ox==0 ? 2.f : 1.f)*(oy==0 ? 2.f : 1.f);
					sum += work.values[(size_t)px + (size_t)py*w] * weight;
					weights += weight;
				}
				const size_t i=(size_t)x+(size_t)y*w;
				const float alpha=strength*falloff;
				next[i]=work.values[i]+(sum/weights-work.values[i])*alpha;
			}
			work.values.swap(next);
		}
		TerrainSculptPatch patch;
		patch.section_x=work.sx; patch.section_y=work.sy; patch.x0=work.x0; patch.y0=work.y0;
		patch.width=work.x1-work.x0+1; patch.height=work.y1-work.y0+1;
		patch.before.resize((size_t)patch.width*patch.height);
		patch.after.resize((size_t)patch.width*patch.height);
		for(int y=work.y0; y<=work.y1; ++y)
		for(int x=work.x0; x<=work.x1; ++x)
		{
			const size_t i=(size_t)x+(size_t)y*w;
			const size_t pi=(size_t)(x-work.x0)+(size_t)(y-work.y0)*patch.width;
			patch.before[pi]=work.before[i]; patch.after[pi]=work.values[i];
			if(std::fabs(work.values[i]-work.before[i])>1.0e-7f) changed=true;
			work.map->getPixel((size_t)x,(size_t)y)[0]=work.values[i];
		}
		if(!patch.before.empty())
		{
			if(!patch.before.empty() && patch.before != patch.after)
				getSectionForSculptCoords(work.sx, work.sy)->sculpt_heightmap_texture_dirty = true;
			current_sculpt_stroke.patches.push_back(std::move(patch));
		}
	}
	endSculptStroke();
	return changed;
}


bool TerrainSystem::canUndoSculpt() const
{
	return !sculpt_undo_stack.empty() || (sculpt_stroke_active &&
		(!current_sculpt_stroke.patches.empty() || !current_sculpt_stroke.mask_patches.empty()));
}


bool TerrainSystem::canRedoSculpt() const
{
	return !sculpt_redo_stack.empty();
}


bool TerrainSystem::undoSculpt()
{
	endSculptStroke();
	if(sculpt_undo_stack.empty())
		return false;

	Lock heightmap_lock(heightmaps_mutex);
	TerrainSculptStroke stroke = std::move(sculpt_undo_stack.back());
	sculpt_undo_stack.pop_back();
	for(auto it=stroke.patches.rbegin(); it!=stroke.patches.rend(); ++it)
		applySculptPatch(*it, /*use_after_values=*/false);
	for(auto it=stroke.mask_patches.rbegin(); it!=stroke.mask_patches.rend(); ++it)
		applySculptMaskPatch(*it, /*use_after_values=*/false);
	if(!stroke.patches.empty()) sculpt_geometry_rebuild_pending = true;
	sculpt_redo_stack.push_back(std::move(stroke));
	return true;
}


bool TerrainSystem::redoSculpt()
{
	endSculptStroke();
	if(sculpt_redo_stack.empty())
		return false;

	Lock heightmap_lock(heightmaps_mutex);
	TerrainSculptStroke stroke = std::move(sculpt_redo_stack.back());
	sculpt_redo_stack.pop_back();
	for(const TerrainSculptPatch& patch : stroke.patches)
		applySculptPatch(patch, /*use_after_values=*/true);
	for(const TerrainSculptMaskPatch& patch : stroke.mask_patches)
		applySculptMaskPatch(patch, /*use_after_values=*/true);
	if(!stroke.patches.empty()) sculpt_geometry_rebuild_pending = true;
	sculpt_undo_stack.push_back(std::move(stroke));
	return true;
}


bool TerrainSystem::hasSculptedHeightmaps() const
{
	Lock heightmap_lock(heightmaps_mutex);
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
		if(terrain_data_sections[x + y * TERRAIN_DATA_SECTION_RES].sculpt_heightmap.nonNull())
			return true;
	return false;
}


void TerrainSystem::getSculptedHeightmaps(std::vector<TerrainSculptedHeightmap>& maps_out) const
{
	Lock heightmap_lock(heightmaps_mutex);
	maps_out.clear();
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y * TERRAIN_DATA_SECTION_RES];
		if(section.sculpt_heightmap.nonNull())
		{
			TerrainSculptedHeightmap result;
			result.x = x - TERRAIN_SECTION_OFFSET;
			result.y = y - TERRAIN_SECTION_OFFSET;
			result.map = section.sculpt_heightmap;
			maps_out.push_back(result);
		}
	}
}


bool TerrainSystem::hasSculptedMaskMaps() const
{
	Lock heightmap_lock(heightmaps_mutex);
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y * TERRAIN_DATA_SECTION_RES];
		if(section.sculpt_maskmap.nonNull() || section.sculpt_treemaskmap.nonNull())
			return true;
	}
	return false;
}


void TerrainSystem::getSculptedMaskMaps(std::vector<TerrainSculptedMaskMap>& maps_out) const
{
	Lock heightmap_lock(heightmaps_mutex);
	maps_out.clear();
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y * TERRAIN_DATA_SECTION_RES];
		if(section.sculpt_maskmap.nonNull())
		{
			TerrainSculptedMaskMap result;
			result.x = x - TERRAIN_SECTION_OFFSET;
			result.y = y - TERRAIN_SECTION_OFFSET;
			result.tree_mask = false;
			result.map = section.sculpt_maskmap;
			maps_out.push_back(result);
		}
		if(section.sculpt_treemaskmap.nonNull())
		{
			TerrainSculptedMaskMap result;
			result.x = x - TERRAIN_SECTION_OFFSET;
			result.y = y - TERRAIN_SECTION_OFFSET;
			result.tree_mask = true;
			result.map = section.sculpt_treemaskmap;
			maps_out.push_back(result);
		}
	}
}


bool TerrainSystem::traceRay(const Vec3d& origin, const Vec3d& direction, Vec3d& hit_pos_out) const
{
	if(direction.length2() < 1.0e-12)
		return false;

	const double max_t = 50000.0;
	const int num_steps = 768;
	const double step = max_t / (double)num_steps;
	auto signed_height = [&](double t) -> double
	{
		const Vec3d p = origin + direction * t;
		return p.z - evalTerrainHeight((float)p.x, (float)p.y, 0.f);
	};

	double last_t = 0.0;
	double last_value = signed_height(last_t);
	if(last_value <= 0.0)
	{
		hit_pos_out = origin;
		return true;
	}

	for(int i=1; i<=num_steps; ++i)
	{
		const double cur_t = step * (double)i;
		const double cur_value = signed_height(cur_t);
		if(last_value >= 0.0 && cur_value <= 0.0)
		{
			double lo = last_t;
			double hi = cur_t;
			for(int j=0; j<12; ++j)
			{
				const double mid = (lo + hi) * 0.5;
				if(signed_height(mid) > 0.0)
					lo = mid;
				else
					hi = mid;
			}
			hit_pos_out = origin + direction * hi;
			return true;
		}
		last_t = cur_t;
		last_value = cur_value;
	}
	return false;
}


void TerrainSystem::rebuildAfterSculptIfNeeded()
{
	if(root_node.isNull() || (!sculpt_geometry_rebuild_pending && !sculpt_material_mask_upload_pending && !sculpt_tree_scattering_rebuild_pending))
		return;

	updateSculptedHeightmapTextures();
	// When the first brush stroke converts a source RGB mask to RGBA (for the
	// overlay layer), existing terrain chunks still hold the old texture Ref.
	// Rebuild those chunks once so their material binds the new RGBA texture.
	if(sculpt_geometry_rebuild_pending)
		removeSubtree(root_node.ptr(), root_node->old_subtree_gl_obs, root_node->old_subtree_phys_obs);
	if(sculpt_geometry_rebuild_pending || sculpt_tree_scattering_rebuild_pending)
		terrain_scattering.rebuild();
	sculpt_geometry_rebuild_pending = false;
	sculpt_material_mask_upload_pending = false;
	sculpt_tree_scattering_rebuild_pending = false;
}


void TerrainSystem::updateSculptedHeightmapTextures()
{
	Lock heightmap_lock(heightmaps_mutex);
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	{
		TerrainDataSection& section = terrain_data_sections[x + y * TERRAIN_DATA_SECTION_RES];
		if(!section.sculpt_heightmap_texture_dirty)
			continue;
		ImageMapFloat* map = section.sculpt_heightmap.ptr();
		if(!map || map->getN() != 1 || map->getWidth() < 2 || map->getHeight() < 2)
			continue;

		const size_t width = map->getWidth();
		const size_t height = map->getHeight();
		const ArrayRef<uint8> pixels((const uint8*)map->getData(), map->getDataSize() * sizeof(float));
		if(section.sculpt_heightmap_gl_tex.isNull() ||
			section.sculpt_heightmap_gl_tex->xRes() != width || section.sculpt_heightmap_gl_tex->yRes() != height)
		{
			section.sculpt_heightmap_gl_tex = new OpenGLTexture(width, height, opengl_engine, pixels,
				OpenGLTextureFormat::Format_Greyscale_Float, OpenGLTexture::Filtering_Bilinear,
				OpenGLTexture::Wrapping_Clamp, /*has_mipmaps=*/false);
			section.sculpt_heightmap_gl_tex->setDebugName("Sculpted terrain heightmap");
		}
		else
			section.sculpt_heightmap_gl_tex->loadIntoExistingTexture(0, width, height, width * sizeof(float), pixels, /*bind_needed=*/true);

		// Terrain scattering samples the GPU heightmap in its compute shader.
		// Keep the authored resource texture untouched and point this section at
		// the private editable copy so plants follow sculpting, undo and redo.
		section.heightmap_gl_tex = section.sculpt_heightmap_gl_tex;
		section.sculpt_heightmap_texture_dirty = false;
	}
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	{
		TerrainDataSection& section = terrain_data_sections[x + y * TERRAIN_DATA_SECTION_RES];
		if(!section.sculpt_maskmap_texture_dirty || section.sculpt_maskmap.isNull())
			continue;
		ImageMapUInt8* map = section.sculpt_maskmap.ptr();
		const ArrayRef<uint8> bytes(map->getData(), map->getDataSize());
		if(section.mask_gl_tex.nonNull() && section.mask_gl_tex->xRes() == (int)map->getWidth() &&
			section.mask_gl_tex->yRes() == (int)map->getHeight() &&
			section.mask_gl_tex->getFormat() == OpenGLTextureFormat::Format_RGBA_Linear_Uint8)
		{
			section.mask_gl_tex->loadIntoExistingTexture(0, map->getWidth(), map->getHeight(),
				map->getWidth() * map->getN(), bytes, /*bind_needed=*/true);
		}
		else
		{
			const OpenGLTextureFormat format = map->getN() >= 4 ? OpenGLTextureFormat::Format_RGBA_Linear_Uint8 : OpenGLTextureFormat::Format_RGB_Linear_Uint8;
			section.mask_gl_tex = new OpenGLTexture(map->getWidth(), map->getHeight(), opengl_engine, bytes, format,
				OpenGLTexture::Filtering_Bilinear, OpenGLTexture::Wrapping_Clamp, /*has_mipmaps=*/false);
			section.mask_gl_tex->setDebugName("Sculpted terrain material mask");
			sculpt_geometry_rebuild_pending = true;
		}
		section.sculpt_maskmap_texture_dirty = false;
	}
}


bool TerrainSystem::isTerrainFullyBuilt()
{
	return root_node->subtree_built;
}


// Remove any opengl and physics objects inserted into the opengl and physics engines, in the subtree with given root node.
void TerrainSystem::removeAllNodeDataForSubtree(TerrainNode* node)
{
	if(node->gl_ob.nonNull())
		opengl_engine->removeObject(node->gl_ob);
	if(node->physics_ob.nonNull())
		physics_world->removeObject(node->physics_ob);

	for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
		opengl_engine->removeObject(node->old_subtree_gl_obs[i]);

	for(size_t i=0; i<node->old_subtree_phys_obs.size(); ++i)
		physics_world->removeObject(node->old_subtree_phys_obs[i]);

	for(int i=0; i<4; ++i)
		if(node->children[i].nonNull())
			removeAllNodeDataForSubtree(node->children[i].ptr());
}


void TerrainSystem::shutdown()
{
	// Wait for any MakeTerrainChunkTasks to finish, since they have pointers to this object
	const int max_num_wait_iters = 10000;
	int z = 0;
	assert(num_uncompleted_tasks >= 0);
	while(num_uncompleted_tasks != 0)
	{
		assert(num_uncompleted_tasks >= 0);
		PlatformUtils::Sleep(1);
		z++;
		if(z > max_num_wait_iters)
		{
			conPrint("Internal error: failed to wait for all MakeTerrainChunkTasks: num_uncompleted_tasks=" + toString(num_uncompleted_tasks));
			break;
		}
	}

	terrain_scattering.shutdown();

	if(root_node.nonNull())
		removeAllNodeDataForSubtree(root_node.ptr());
	root_node = NULL;

	id_to_node_map.clear();

	this->vert_res_10_index_buffer = IndexBufAllocationHandle();
	this->vert_res_130_index_buffer = IndexBufAllocationHandle();

	for(size_t i=0; i<water_gl_obs.size(); ++i)
		opengl_engine->removeObject(water_gl_obs[i]);
	water_gl_obs.clear();
	opengl_engine->getCurrentScene()->water_coast_texture = nullptr;
	water_bathymetry_update_time = -1.0;

	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];
		if(section.road_mask_decal_gl_ob.nonNull())
			opengl_engine->removeObject(section.road_mask_decal_gl_ob);
		if(section.building_mask_decal_gl_ob.nonNull())
			opengl_engine->removeObject(section.building_mask_decal_gl_ob);
		section.road_mask_decal_gl_ob = NULL;
		section.building_mask_decal_gl_ob = NULL;
		section.road_mask_gl_tex = NULL;
		section.building_mask_gl_tex = NULL;
	}
}
void TerrainSystem::updateCampos(const Vec3d& campos, glare::StackAllocator& bump_allocator)
{
	updateWaterMeshCentre(campos);
	rebuildAfterSculptIfNeeded();
	updateWaterBathymetry(campos);
	updateReferenceMaskDecalTransforms((float)campos.z);
	updateSubtree(root_node.ptr(), campos);

	terrain_scattering.updateCampos(campos, bump_allocator);
}


/*static void appendSubtreeString(TerrainNode* node, std::string& s)
{
	for(int i=0; i<node->depth; ++i)
		s.push_back(' ');

	s += "node, id " + toString(node->id) + " building: " + boolToString(node->building) + ", subtree_built: " + boolToString(node->subtree_built) + "\n";

	if(node->children[0].nonNull())
	{
		for(int i=0; i<4; ++i)
			appendSubtreeString(node->children[i].ptr(), s);
	}
}*/


struct TerrainSysDiagnosticsInfo
{
	int num_interior_nodes;
	int num_leaf_nodes;
	int max_depth;
	size_t geom_gpu_mem_usage;
	size_t physics_obs_mem_usage;
};

static void processSubtreeDiagnostics(TerrainNode* node, TerrainSysDiagnosticsInfo& info)
{
	info.max_depth = myMax(info.max_depth, node->depth);

	if(node->children[0].nonNull())
	{
		info.num_interior_nodes++;

		for(int i=0; i<4; ++i)
			processSubtreeDiagnostics(node->children[i].ptr(), info);
	}
	else
	{
		info.num_leaf_nodes++;

		if(node->gl_ob.nonNull())         info.geom_gpu_mem_usage += node->gl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;
		if(node->pending_gl_ob.nonNull()) info.geom_gpu_mem_usage += node->pending_gl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;

		if(node->physics_ob.nonNull())         info.physics_obs_mem_usage += node->physics_ob->shape.size_B;
		if(node->pending_physics_ob.nonNull()) info.physics_obs_mem_usage += node->pending_physics_ob->shape.size_B;

		for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
			info.geom_gpu_mem_usage += node->old_subtree_gl_obs[i]->mesh_data->getTotalMemUsage().geom_gpu_usage;

		for(size_t i=0; i<node->old_subtree_phys_obs.size(); ++i)
			info.physics_obs_mem_usage += node->old_subtree_phys_obs[i]->shape.size_B;
	}
}


std::string TerrainSystem::getDiagnostics() const
{
	/*std::string s;
	if(root_node.nonNull())
		appendSubtreeString(root_node.ptr(), s);
	return s;*/

	TerrainSysDiagnosticsInfo info;
	info.num_interior_nodes = 0;
	info.num_leaf_nodes = 0;
	info.max_depth = 0;
	info.geom_gpu_mem_usage = 0;
	info.physics_obs_mem_usage = 0;

	if(root_node.nonNull())
		processSubtreeDiagnostics(root_node.ptr(), info);

	std::string s = 
		"num interior nodes: " + toString(info.num_interior_nodes) + "\n" +
		"num leaf nodes: " + toString(info.num_leaf_nodes) + "\n" +
		"max depth: " + toString(info.max_depth) + "\n" +
		"geom_gpu_mem_usage: " + getNiceByteSize(info.geom_gpu_mem_usage) + "\n" +
		"physics_obs_mem_usage: " + getNiceByteSize(info.physics_obs_mem_usage) + "\n";


	size_t detail_heightmaps_cpu_mem = 0;
	for(int i=0; i<4; ++i)
		detail_heightmaps_cpu_mem += detail_heightmaps[i].nonNull() ? detail_heightmaps[i]->getByteSize() : 0;

	s += "detail_heightmaps_cpu_mem: " + getNiceByteSize(detail_heightmaps_cpu_mem) + "\n";


	size_t detail_tex_GPU_mem = 0;
	size_t detail_heightmap_GPU_mem = 0;
	for(int i=0; i<4; ++i)
	{
		if(opengl_engine->getDetailTexture(i).nonNull())   detail_tex_GPU_mem += opengl_engine->getDetailTexture(i)->getTotalStorageSizeB();
		if(opengl_engine->getDetailHeightmap(i).nonNull()) detail_heightmap_GPU_mem += opengl_engine->getDetailHeightmap(i)->getTotalStorageSizeB();
	}

	s += "detail tex GPU mem:       " + getNiceByteSize(detail_tex_GPU_mem) + "\n";
	s += "detail heightmap GPU mem: " + getNiceByteSize(detail_heightmap_GPU_mem) + "\n";

	size_t terrain_section_heightmap_CPU_mem = 0;
	size_t terrain_section_maskmap_CPU_mem = 0;
	size_t terrain_section_heightmap_GPU_mem = 0;
	size_t terrain_section_maskmap_GPU_mem = 0;
	for(int x=0; x<TERRAIN_DATA_SECTION_RES; ++x)
	for(int y=0; y<TERRAIN_DATA_SECTION_RES; ++y)
	{
		const TerrainDataSection& section = terrain_data_sections[x + y*TERRAIN_DATA_SECTION_RES];

		if(section.heightmap.nonNull()) terrain_section_heightmap_CPU_mem += section.heightmap->getByteSize();
		if(section.maskmap.nonNull())   terrain_section_maskmap_CPU_mem   += section.maskmap->getByteSize();

		if(section.heightmap_gl_tex.nonNull()) terrain_section_heightmap_GPU_mem += section.heightmap_gl_tex->getTotalStorageSizeB();
		if(section.mask_gl_tex.nonNull())      terrain_section_maskmap_GPU_mem   += section.mask_gl_tex->getTotalStorageSizeB();
	}

	s += "terrain section heightmap CPU mem: " + getNiceByteSize(terrain_section_heightmap_CPU_mem) + "\n";
	s += "terrain section maskmap CPU mem:   " + getNiceByteSize(terrain_section_maskmap_CPU_mem) + "\n";
	s += "terrain section heightmap GPU mem: " + getNiceByteSize(terrain_section_heightmap_GPU_mem) + "\n";
	s += "terrain section maskmap GPU mem:   " + getNiceByteSize(terrain_section_maskmap_GPU_mem) + "\n";

	s += "Terrain scattering:\n" +
		terrain_scattering.getDiagnostics();

	return s;
}


//struct VoronoiBasisNoise01
//{
//	inline static float eval(const Vec4f& p)
//	{
//		Vec4f closest_p;
//		float dist;
//		Voronoi::evaluate3d(p, 1.0f, closest_p, dist);
//		return dist;
//	}
//};


//static inline Vec2f toVec2f(const Vec4f& v)
//{
//	return Vec2f(v[0], v[1]);
//}

static inline float fbm(ImageMapFloat& fbm_imagemap, Vec2f p)
{
	// NOTE: textures are effecively flipped upside down in OpenGL, negate y to compensate.
	return (fbm_imagemap.sampleSingleChannelTiled(p.x, -p.y, 0) - 0.5f) * 2.f;
}

static inline Vec2f rot(Vec2f p)
{
	const float theta = 1.618034 * 3.141592653589 * 2;
	return Vec2f(cos(theta) * p.x - sin(theta) * p.y, sin(theta) * p.x + cos(theta) * p.y);
}

static inline float fbmMix(ImageMapFloat& fbm_imagemap, const Vec2f& p)
{
	return 
		fbm(fbm_imagemap, p) +
		fbm(fbm_imagemap, rot(p * 2)) * 0.5f;
}


// p_x, p_y are world space coordinates.
Colour4f TerrainSystem::evalTerrainMask(float p_x, float p_y) const
{
	const float nx = p_x * terrain_scale_factor + 0.5f; // Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float ny = p_y * terrain_scale_factor + 0.5f;

	// Work out which source terrain data section we are reading from
	const int section_x = Maths::floorToInt(nx) + TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(ny) + TERRAIN_SECTION_OFFSET;
	if(section_x < 0 || section_x >= 8 || section_y < 0 || section_y >= 8)
		return Colour4f(1,0,0,0);
	const TerrainDataSection& section = terrain_data_sections[section_x + section_y*TERRAIN_DATA_SECTION_RES]; // terrain_data_sections.elem(section_x, section_y);
	const Map2D* maskmap = section.sculpt_maskmap.nonNull() ? static_cast<const Map2D*>(section.sculpt_maskmap.ptr()) : section.maskmap.ptr();
	if(!maskmap)
		return Colour4f(1,0,0,0);

	const float section_nx = nx - Maths::floorToInt(nx);
	const float section_ny = ny - Maths::floorToInt(ny);
	return maskmap->vec3Sample(section_nx, 1.f - section_ny, /*wrap=*/false);
}


// Return value >= 0.5: tree allowed
// p_x, p_y are world space coordinates.
float TerrainSystem::evalTreeMask(float p_x, float p_y) const
{
	const float nx = p_x * terrain_scale_factor + 0.5f; // Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float ny = p_y * terrain_scale_factor + 0.5f;

	// Work out which source terrain data section we are reading from
	const int section_x = Maths::floorToInt(nx) + TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(ny) + TERRAIN_SECTION_OFFSET;
	if(section_x < 0 || section_x >= 8 || section_y < 0 || section_y >= 8)
		return 1;
	const TerrainDataSection& section = terrain_data_sections[section_x + section_y*TERRAIN_DATA_SECTION_RES]; // terrain_data_sections.elem(section_x, section_y);
	const Map2D* treemaskmap = section.sculpt_treemaskmap.nonNull() ? static_cast<const Map2D*>(section.sculpt_treemaskmap.ptr()) : section.treemaskmap.ptr();
	if(!treemaskmap)
	{
		// With no configured mask, preserve the historical "trees allowed" default.
		// If a mask was configured but is still missing/failed to load, fail closed:
		// rendering trees everywhere would make a valid black/white mask look ignored.
		return section.tree_mask_map_path.empty() ? 1.f : 0.f;
	}

	const float section_nx = nx - Maths::floorToInt(nx);
	const float section_ny = ny - Maths::floorToInt(ny);
	return treemaskmap->sampleSingleChannelTiled(section_nx, 1.f - section_ny, /*channel=*/0);
}


void TerrainSystem::updateWaterBathymetry(const Vec3d& campos)
{
	if(water_gl_obs.empty()) return;
	const double now = opengl_engine->getCurrentTime();
	const Vec3d centre(std::floor(campos.x / 8.0) * 8.0, std::floor(campos.y / 8.0) * 8.0, spec.water_z);
	if(water_bathymetry_update_time >= 0 && now - water_bathymetry_update_time < 0.25) return;
	if(centre == water_bathymetry_centre && now - water_bathymetry_update_time < 1.0) return;
	water_bathymetry_centre = centre;
	water_bathymetry_update_time = now;
	// Two-metre terrain samples. The first row contains mapping metadata, not
	// geometry. Sampling is independent of avatars/boats and of the camera view.
	const int res = 129;
	const float span = 256.f, step = span / (res - 1);
	const float ox = (float)centre.x - span * 0.5f, oy = (float)centre.y - span * 0.5f;
	std::vector<float> heights(res * res);
	for(int y=0; y<res; ++y) for(int x=0; x<res; ++x)
		heights[y*res+x] = evalTerrainHeight(ox+x*step, oy+y*step, step);
	std::vector<float> data(res * (res+1) * 4, 0.f);
	data[0]=ox; data[1]=oy; data[2]=1.f/span; data[3]=spec.water_z;
	for(int y=0; y<res; ++y) for(int x=0; x<res; ++x)
	{
		const int xl=std::max(0,x-1), xr=std::min(res-1,x+1), yb=std::max(0,y-1), yt=std::min(res-1,y+1);
		const int offset=((y+1)*res+x)*4;
		data[offset]=heights[y*res+x];
		data[offset+1]=(heights[y*res+xr]-heights[y*res+xl])/((xr-xl)*step);
		data[offset+2]=(heights[yt*res+x]-heights[yb*res+x])/((yt-yb)*step);
		data[offset+3]=1.f;
	}
	auto& texture = opengl_engine->getCurrentScene()->water_coast_texture;
	const ArrayRef<uint8> bytes((const uint8*)data.data(), data.size()*sizeof(float));
	if(!texture)
		texture = new OpenGLTexture(res, res+1, opengl_engine, bytes, OpenGLTextureFormat::Format_RGBA_Linear_Float,
			OpenGLTexture::Filtering_Nearest, OpenGLTexture::Wrapping_Clamp, false);
	else
		texture->loadIntoExistingTexture(0, res, res+1, res*4*sizeof(float), bytes, true);
}


void TerrainSystem::updateWaterMeshCentre(const Vec3d& campos)
{
	const int num_water_rects = 1 + 4 * 3 + (9 * 9 - 1);
	if(water_gl_obs.size() < num_water_rects)
		return;
	const Vec3d centre(std::floor(campos.x / 2.0) * 2.0, std::floor(campos.y / 2.0) * 2.0, spec.water_z);
	if(centre == water_mesh_centre)
		return;
	water_mesh_centre = centre;
	int i = 0;
	auto place_water = [&](float x, float y, float w, float h)
	{
		water_gl_obs[i]->ob_to_world_matrix = Matrix4f::translationMatrix((float)centre.x + x, (float)centre.y + y, spec.water_z) *
			Matrix4f::scaleMatrix(w, h, 1.f);
		opengl_engine->updateObjectTransformData(*water_gl_obs[i]);
		++i;
	};
	place_water(-128.f, -128.f, 256.f, 256.f);
	const float ring_half_sizes[4] = {128.f, 2048.f, 8192.f, 20000.f};
	for(int ring=0; ring<3; ++ring)
	{
		const float inner = ring_half_sizes[ring], outer = ring_half_sizes[ring + 1];
		place_water(-outer, -outer, 2*outer, outer-inner);
		place_water(-outer, inner, 2*outer, outer-inner);
		place_water(-outer, -inner, outer-inner, 2*inner);
		place_water(inner, -inner, outer-inner, 2*inner);
	}
	const float tile_w = 40000.f;
	for(int y=-4; y<=4; ++y)
	for(int x=-4; x<=4; ++x)
		if(x != 0 || y != 0)
			place_water((x - 0.5f) * tile_w, (y - 0.5f) * tile_w, tile_w, tile_w);
}


// p_x, p_y are world space coordinates.
float TerrainSystem::evalTerrainHeight(float p_x, float p_y, float quad_w) const
{
#if 1
	const float MIN_TERRAIN_Z = -50.f; // Have a max under-sea depth.  This allows having a flat sea-floor, which in turn allows a lower-res mesh to be used for seafloor chunks.

	const float nx = p_x * terrain_scale_factor + 0.5f; // Offset by 0.5 so that the central heightmap is centered at (0,0,0).
	const float ny = p_y * terrain_scale_factor + 0.5f;

	// Work out which source terrain data section we are reading from
	const int section_x = Maths::floorToInt(nx) + TERRAIN_SECTION_OFFSET;
	const int section_y = Maths::floorToInt(ny) + TERRAIN_SECTION_OFFSET;
	if(section_x < 0 || section_x >= 8 || section_y < 0 || section_y >= 8)
		return spec.default_terrain_z;
	const TerrainDataSection& section = terrain_data_sections[section_x + section_y*TERRAIN_DATA_SECTION_RES]; // terrain_data_sections.elem(section_x, section_y);
	if(section.heightmap.isNull())
		return spec.default_terrain_z;

	const float section_nx = nx - Maths::floorToInt(nx);
	const float section_ny = ny - Maths::floorToInt(ny);


	//const float dist_from_origin = Vec2f(p_x, p_y).length();
	//const float centre_flatten_factor = Maths::smoothStep(700.f, 1000.f, dist_from_origin); // Start the hills only x metres from origin
//	const float x_edge_w = 400;
//	const float centre_flatten_factor_x = Maths::smoothPulse(-600.f - x_edge_w, -600.f, 800.f, 800.f + x_edge_w, p_x); // Start the hills only x metres from origin
//	const float centre_flatten_factor_y = Maths::smoothPulse(-600.f, -500.f, 800.f, 1000.f, p_y); // Start the hills only x metres from origin
//	const float centre_flatten_factor = centre_flatten_factor_x * centre_flatten_factor_y;
//	const float non_flatten_factor = 1 - centre_flatten_factor;


//	const float seaside_factor = Maths::smoothStep(-1000.f, -300.f, p_y);


	const Map2D* terrain_mask_map = section.sculpt_maskmap.nonNull() ? static_cast<const Map2D*>(section.sculpt_maskmap.ptr()) : section.maskmap.ptr();
	const Colour4f mask_val = terrain_mask_map ? terrain_mask_map->vec3Sample(section_nx, 1.f - section_ny, /*wrap=*/false) : Colour4f(0.f);
			
	// NOTE: textures are effectively flipped upside down in OpenGL, negate y to compensate.
	const float heightmap_terrain_z = section.heightmap->sampleSingleChannelHighQual(section_nx, 1.f - section_ny, /*channel=*/0, /*wrap=*/false);
	//terrain_h = -300 + seaside_factor * 300 + non_flatten_factor * myMax(MIN_TERRAIN_Z, heightmap_terrain_z);// + detail_h;

	//terrain_h = myMax(MIN_TERRAIN_Z, -300 + seaside_factor * 300 + non_flatten_factor * heightmap_terrain_z);// + detail_h;
	const bool exact_heightmap = BitUtils::isBitSet(spec.flags, TerrainSpec::EXACT_HEIGHTMAP_FLAG);
	float terrain_h = exact_heightmap ?
		heightmap_terrain_z * spec.terrain_height_scale :
		myMax(-100000.f, /*non_flatten_factor **/ heightmap_terrain_z * spec.terrain_height_scale);// + detail_h;

	if(!exact_heightmap && terrain_h > MIN_TERRAIN_Z) // Don't apply fine noise on the seafloor.
	{
		// 
		//const float noise_xy_scale = 1 / 200.f;
		//const Vec4f p = Vec4f(p_x * noise_xy_scale, p_y * noise_xy_scale, 0, 1);
		//const float fbm_val = PerlinNoise::ridgedMultifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/10, /*offset=*/0.1f) * 0.2f;
		//const float fbm_val = PerlinNoise::multifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/10, /*offset=*/0.1f) * 5.5f;
		//const float fbm_val = fbmMix(*opengl_engine->fbm_imagemap, Vec2f(p[0], p[1])) * 1.2f;

		// Vegetation noise
		const float veg_noise_xy_scale = 1 / 50.f;
		const float veg_noise_mag = 0.4f * mask_val[2];
		const float veg_fbm_val = (veg_noise_mag > 0) ?
			fbmMix(*opengl_engine->fbm_imagemap, Vec2f(p_x, p_y) * veg_noise_xy_scale) * veg_noise_mag : 
			0.f;
		terrain_h += veg_fbm_val;

		//const float dune_envelope = Maths::smoothStep(water_z + 0.4f, water_z + 1.5f, terrain_h);
		//const float dune_xy_scale = 1 / 2.f;
		//const float dune_h = 0; // small_dune_heightmap->sampleSingleChannelTiled(p_x * dune_xy_scale, p_y * dune_xy_scale, 0) * dune_envelope * 0.1f;

		//Vec2f detail_uvs = Vec2f(p_x, p_y) * (1 / 3.f);
		//detail_uvs.y *= -1.0;
		Vec2f detail_map_0_uvs = Vec2f(nx, ny) * (8.0 * 1024 / 8.0);
		//Vec2f detail_map_1_uvs = Vec2f(nx, ny) * (8.0 * 1024 / 4.0);
		Vec2f detail_map_2_uvs = Vec2f(nx, ny) * (8.0 * 1024 / 4.0);

			

		float rock_weight_env;
		if(mask_val[0] == 0)
			rock_weight_env = 0;
		else
			rock_weight_env =  Maths::smoothStep(0.2f, 0.6f, mask_val[0] + fbmMix(*opengl_engine->fbm_imagemap, detail_map_2_uvs * 0.2f) * 0.2f);
		float rock_height = detail_heightmaps[0].nonNull() ? detail_heightmaps[0]->sampleSingleChannelTiled(detail_map_0_uvs.x, -detail_map_0_uvs.y, 0) * rock_weight_env : 0;

		//float rock_height = mask_val[0] * 10.0;
			
		//if(mask_val[0] > 0 && detail_heightmaps[0].nonNull())
		//	terrain_h += detail_heightmaps[0]->sampleSingleChannelTiled(detail_map_0_uvs.x, detail_map_0_uvs.y, 0) * mask_val[0] * 1.f;
		//if(mask_val[1] > 0 && detail_heightmaps[1].nonNull())
		//	terrain_h += detail_heightmaps[1]->sampleSingleChannelTiled(detail_map_1_uvs.x, detail_map_1_uvs.y, 0) * mask_val[1] * 0.1f;

		terrain_h += rock_height * 0.8f;
	}
	return terrain_h;

#elif 0
	//	return p_x * 0.01f;
	//	const float xy_scale = 1 / 5.f;


	//const float num_octaves = 10;//-std::log2(quad_w * xy_scale);
//	const Vec4f p = Vec4f(p_x * xy_scale, p_y * xy_scale, 0, 1);
	//const float fbm_val = PerlinNoise::multifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/num_octaves, /*offset=*/0.1f);
	//const float fbm_val = PerlinNoise::noise(p) + PerlinNoise::noise(p * 8.0);
//	const float fbm_val = PerlinNoise::FBM(p, 4);

	const float nx = p_x * scale_factor + 0.5f;
	const float ny = p_y * scale_factor + 0.5f;

	float terrain_h;
	if(nx < 0 || nx > 1 || ny < 0 || ny > 1)
		terrain_h = -300;
	else
		terrain_h = (heightmap->sampleSingleChannelTiledHighQual(nx, ny, 0) - 0.57f) * 800;// + fbm_val * 0.5;
		
	return terrain_h;

	//const float detail_xy_scale = 1 / 3.f;
	//return detail_heightmap->sampleSingleChannelTiled(p_x * detail_xy_scale, p_y * detail_xy_scale, 0) * 0.2f;
#else


//	return PerlinNoise::noise(Vec4f(p_x / 30.f, p_y / 30.f, 0, 0)) * 10.f;
	//return p_x * 0.1f;//PerlinNoise::noise(Vec4f(p_x / 30.f, p_y / 30.f, 0, 0)) * 10.f;
	//return 0.f; // TEMP 
	float seaside_factor = Maths::smoothStep(-1000.f, -300.f, p_y);

	const float dist_from_origin = Vec2f(p_x, p_y).length();
	const float centre_flatten_factor = Maths::smoothStep(700.f, 1000.f, dist_from_origin); // Start the hills only x metres from origin
		
	// FBM feature size is (1 / xy_scale) * 2 ^ -num_octaves     = 1 / (xyscale * 2^num_octaves)
	// For example if xy_scale = 1/3000 and num_octaves = 10, we have feature size = 3000 / 1024 ~= 3.
	// We want the feature size to be = quad_w, e.g.
	// quad_w = (1 / xy_scale) * 2 ^ -num_octaves
	// so
	// quad_w * xy_scale = 2 ^ -num_octaves
	// log2(quad_w * xy_scale) = -num_octaves
	// num_octaves = - log2(quad_w * xy_scale)
	const float xy_scale = 1 / 3000.f;

	const float num_octaves = 10;//-std::log2(quad_w * xy_scale);

	const Vec4f p = Vec4f(p_x * xy_scale, p_y * xy_scale, 0, 1);
	//const float fbm_val = PerlinNoise::multifractal<float>(p, /*H=*/1, /*lacunarity=*/2, /*num octaves=*/num_octaves, /*offset=*/0.1f);
	const float fbm_val = PerlinNoise::FBM(p, /*num octaves=*/(int)num_octaves);

//	float fbm_val = 0;
//	static float octave_params[] = 
//	{
//		1.0f, 0.5f,
//		2.0f, 0.25f,
//		4.0f, 0.125f,
//		128.0f, 0.04f,
//		256.0f, 0.02f
//	};
//
//	for(int i=0; i<staticArrayNumElems(octave_params)/2; ++i)
//	{
//		const float scale  = octave_params[i*2 + 0];
//		const float weight = octave_params[i*2 + 1];
//		fbm_val += PerlinNoise::noise(p * scale) * weight;
//	}

	//fbm_val += (1 - std::fabs(PerlinNoise::noise(p * 100.f))) * 0.025f; // Ridged noise
	//fbm_val += VoronoiBasisNoise01::eval(p * 0.1f) * 0.1f;//0.025f; // Ridged noise
	//fbm_val += Voronoi::voronoiFBM(toVec2f(p * 10000.f), 2) * 0.01f;

	return -300 + seaside_factor * 300 + centre_flatten_factor * myMax(0.f, fbm_val - 0.2f) * 600;
#endif
}


void TerrainSystem::makeTerrainChunkMesh(float chunk_x, float chunk_y, float chunk_w, bool build_physics_ob, TerrainChunkData& chunk_data_out) const
{
	// Keep all height queries for this generated mesh on one immutable map
	// state.  In particular, sculptAtWorld() may copy-on-write the source map
	// before updating its pixels, so locking only individual reads is not
	// sufficient to guarantee a coherent chunk.
	Lock heightmap_lock(heightmaps_mutex);

	//Timer timer;
	/*
	 
	An example mesh with interior_vert_res=4, giving vert_res_with_borders=6
	There will be a 1 quad wide skirt border at the edge of the mesh.
	y
	^ 
	|
	-------------------------
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / | skirt
	| /  | /  | /  | /  | /  |
	|------------------------|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|
	|   /|   /|   /|   /|   /|
	|  / |  / |  / |  / |  / |skirt
	| /  | /  | /  | /  | /  |
	|----|----|----|----|----|----> x
	skirt               skirt
	*/

	// Do a quick pass over the data, to see if the heightfield is completely flat here (e.g. is a flat chunk of sea-floor or ground plane).
	bool completely_flat = true;
	{
		const int CHECK_RES = 32;
		const float quad_w = chunk_w / (CHECK_RES - 1);
		const float z_0 = evalTerrainHeight(chunk_x, chunk_y, quad_w);
		for(int y=0; y<CHECK_RES; ++y)
		for(int x=0; x<CHECK_RES; ++x)
		{
			const float p_x = x * quad_w + chunk_x;
			const float p_y = y * quad_w + chunk_y;
			const float z = evalTerrainHeight(p_x, p_y, quad_w);
			if(z != z_0)
			{
				completely_flat = false;
				goto done;
			}
		}
	}
done:

	const int interior_vert_res = completely_flat ? 8 : 128; // Number of vertices along the side of a chunk, excluding the 2 border vertices.  Use a power of 2 for Jolt.
	const int interior_quad_res = interior_vert_res - 1;
	const int vert_res_with_borders = interior_vert_res + 2;
	const int quad_res_with_borders = vert_res_with_borders - 1;

	const float quad_w = chunk_w / interior_quad_res;

	const int jolt_vert_res = interior_vert_res;
	
	Array2D<float> jolt_heightfield;
	if(build_physics_ob)
	{
		jolt_heightfield.resizeNoCopy(jolt_vert_res, jolt_vert_res);
	}

	const size_t normal_size_B = 4;
	size_t vert_size_B = sizeof(Vec3f) + normal_size_B; // position, normal
	if(GEOMORPHING_SUPPORT)
		vert_size_B += sizeof(float) + normal_size_B; // morph-z, morph-normal

	chunk_data_out.vert_res_with_borders = vert_res_with_borders;

	chunk_data_out.mesh_data = new OpenGLMeshRenderData();
	chunk_data_out.mesh_data->vert_data.setAllocator(this->opengl_engine->mem_allocator);
	chunk_data_out.mesh_data->vert_data.resize(vert_size_B * vert_res_with_borders * vert_res_with_borders);
	

	OpenGLMeshRenderData& meshdata = *chunk_data_out.mesh_data;

	meshdata.setIndexType(GL_UNSIGNED_SHORT);

	meshdata.has_uvs = true;
	meshdata.has_shading_normals = true;
	meshdata.batches.resize(1);
	meshdata.batches[0].material_index = 0;
	meshdata.batches[0].num_indices = (uint32)(quad_res_with_borders * quad_res_with_borders * 6);
	meshdata.batches[0].prim_start_offset_B = 0;

	meshdata.num_materials_referenced = 1;

	// NOTE: The order of these attributes should be the same as in OpenGLProgram constructor with the glBindAttribLocations.
	size_t in_vert_offset_B = 0;
	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)vert_size_B;
	pos_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(pos_attrib);
	in_vert_offset_B += sizeof(float) * 3;

	VertexAttrib normal_attrib;
	normal_attrib.enabled = true;
	normal_attrib.num_comps = 4;
	normal_attrib.type = GL_INT_2_10_10_10_REV;
	normal_attrib.normalised = true;
	normal_attrib.stride = (uint32)vert_size_B;
	normal_attrib.offset = (uint32)in_vert_offset_B;
	meshdata.vertex_spec.attributes.push_back(normal_attrib);
	in_vert_offset_B += normal_size_B;

	size_t morph_offset_B, morph_normal_offset_B;
	if(GEOMORPHING_SUPPORT)
	{
		morph_offset_B = in_vert_offset_B;
		VertexAttrib morph_attrib;
		morph_attrib.enabled = true;
		morph_attrib.num_comps = 1;
		morph_attrib.type = GL_FLOAT;
		morph_attrib.normalised = false;
		morph_attrib.stride = (uint32)vert_size_B;
		morph_attrib.offset = (uint32)in_vert_offset_B;
		meshdata.vertex_spec.attributes.push_back(morph_attrib);
		in_vert_offset_B += sizeof(float);

		morph_normal_offset_B = in_vert_offset_B;
		VertexAttrib morph_normal_attrib;
		morph_normal_attrib.enabled = true;
		morph_normal_attrib.num_comps = 4;
		morph_normal_attrib.type = GL_INT_2_10_10_10_REV;
		morph_normal_attrib.normalised = true;
		morph_normal_attrib.stride = (uint32)vert_size_B;
		morph_normal_attrib.offset = (uint32)in_vert_offset_B;
		meshdata.vertex_spec.attributes.push_back(morph_normal_attrib);
		in_vert_offset_B += normal_size_B;
	}

	meshdata.vertex_spec.checkValid();


	assert(in_vert_offset_B == vert_size_B);

	Array2D<float> raw_heightfield(interior_vert_res, interior_vert_res);
	// Array2D<Vec3f> raw_normals(interior_vert_res, interior_vert_res);
	for(int y=0; y<interior_vert_res; ++y)
	for(int x=0; x<interior_vert_res; ++x)
	{
		const float p_x = x * quad_w + chunk_x;
		const float p_y = y * quad_w + chunk_y;
	// 	const float dx = 0.1f;
	// 	const float dy = 0.1f;
	// 
	 	const float z    = evalTerrainHeight(p_x,      p_y,      quad_w); // z = h(p_x, p_y)
	// 	const float z_dx = evalTerrainHeight(p_x + dx, p_y,      quad_w, water); // z_dx = h(p_x + dx, dy)
	// 	const float z_dy = evalTerrainHeight(p_x,      p_y + dy, quad_w, water); // z_dy = h(p_x, p_y + dy)
	// 
	// 	const Vec3f p_dx_minus_p(dx, 0, z_dx - z); // p(p_x + dx, dy) - p(p_x, p_y) = (p_x + dx, d_y, z_dx) - (p_x, p_y, z) = (d_x, 0, z_dx - z)
	// 	const Vec3f p_dy_minus_p(0, dy, z_dy - z);
	// 
	// 	const Vec3f normal = normalise(crossProduct(p_dx_minus_p, p_dy_minus_p));
	// 
	 	raw_heightfield.elem(x, y) = z;
	// 	raw_normals.elem(x, y) = normal;
	}

	//conPrint("eval terrain height took     " + timer.elapsedStringMSWIthNSigFigs(4));
	//timer.reset();

	const float skirt_height = chunk_w * (1 / 128.f) * 0.25f; // The skirt height needs to be large enough to cover any cracks, but smaller is better to avoid wasted fragment drawing.
	const int interior_vert_res_minus_1 = interior_vert_res - 1;
	
	uint8* const vert_data = chunk_data_out.mesh_data->vert_data.data();
	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	for(int y=0; y<vert_res_with_borders; ++y)
	for(int x=0; x<vert_res_with_borders; ++x)
	{
		float p_x; // x coordinate, object space
		int src_x; // x index to use reading from raw_heightfield
		float z_offset = 0;
		if(x == 0) // If edge vert, vert is on bottom of skirt
		{
			p_x = 0;
			src_x = 0;
			z_offset = skirt_height;
		}
		else if(x == vert_res_with_borders-1) // If edge vert, vert is on bottom of skirt
		{
			p_x = interior_vert_res_minus_1 * quad_w;
			src_x = interior_vert_res_minus_1;
			z_offset = skirt_height;
		}
		else
		{
			p_x = (x-1) * quad_w;
			src_x = x-1;
		}

		float p_y; // y coordinate, object space
		int src_y;
		if(y == 0) // If edge vert, vert is on bottom of skirt
		{
			p_y = 0;
			src_y = 0;
			z_offset = skirt_height;
		}
		else if(y == vert_res_with_borders-1) // If edge vert, vert is on bottom of skirt
		{
			p_y = interior_vert_res_minus_1 * quad_w;
			src_y = interior_vert_res_minus_1;
			z_offset = skirt_height;
		}
		else
		{
			p_y = (y-1) * quad_w;
			src_y = y-1;
		}

		const float recip_2_quad_w = 1.f / (quad_w*2);

		// Compute normal and height at vertex.
		// For interior vertices, use central differences from adjacent vertices for computing the normal.
		// This is fast because it avoids calling evalTerrainHeight().
		// For edge vertices, compute normal using evalTerrainHeight() calls, since the resulting normal should match adjacent chunks more closely,
		// for example if the adjacent chunk has different tesselation resolution.
		float h;
		Vec4f normal;
		if(src_x >= 1 && src_x < interior_vert_res_minus_1 && src_y >= 1 && src_y < interior_vert_res_minus_1)
		{
			h = raw_heightfield.elem(src_x, src_y);

			const float dh_dx = (raw_heightfield.elem(src_x+1, src_y) - raw_heightfield.elem(src_x-1, src_y)) * recip_2_quad_w;
			const float dh_dy = (raw_heightfield.elem(src_x, src_y+1) - raw_heightfield.elem(src_x, src_y-1)) * recip_2_quad_w;

			normal = normalise(Vec4f(-dh_dx, -dh_dy, 1, 0));
		}
		else
		{
			// Use large deltas for consistency with the normal generation in the interior of the chunk, otherwise the chunk edge is visible due to different normal generation techniques.
			const float dx = quad_w; 
			const float dy = quad_w;
			
						h    = evalTerrainHeight(chunk_x + p_x,      chunk_y + p_y,      quad_w); // h(p_x, p_y)
			const float h_dx = evalTerrainHeight(chunk_x + p_x + dx, chunk_y + p_y,      quad_w); // h(p_x + dx, dy)
			const float h_dy = evalTerrainHeight(chunk_x + p_x,      chunk_y + p_y + dy, quad_w); // h(p_x, p_y + dy)
			
			const float dh_dx = (h_dx - h) * (1.f / dx);
			const float dh_dy = (h_dy - h) * (1.f / dy);
			
			normal = normalise(Vec4f(-dh_dx, -dh_dy, 1, 0));
		}

		const float p_z = h - z_offset; // Z coordinate taking into account downwards offset for skirt, if applicable.

		if(build_physics_ob)
		{
			const int int_x = x - 1; // Don't include border/skirt vertices in Jolt heightfield.
			const int int_y = y - 1;
			if((int_x >= 0) && (int_x < jolt_vert_res) && (int_y >= 0) && (int_y < jolt_vert_res))
				jolt_heightfield.elem(int_x, jolt_vert_res - 1 - int_y) = p_z;
		}

		const Vec4f pos(p_x, p_y, p_z, 1);
		std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x), &pos, sizeof(float)*3); // Store x,y,z pos coords.

		aabb_os.enlargeToHoldPoint(pos);

		const uint32 packed_normal = packNormal(normal);
		std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x) + sizeof(float) * 3, &packed_normal, sizeof(uint32));

		// Morph z-displacement:
		// Starred vertices, without the morph displacement, should have the position that the lower LOD level triangle would have, below.
		/*
		 y
		 ^ 
		 |    *         *        (*)
		 |----|----|----|----|----|
		 | \  | \  | \  | \  | \  |
		 |  \ |  \ |  \ |  \ |  \ |
		 |   \|   \|   \|   \|   \|
		*|----|----*----|----*----|
		 | \  | \  | \  | \  | \  |
		 |  \ |  \ |  \ |  \ |  \ |
		 |   \|   \|   \|   \|   \|
		 |---------|----|----|---> x             
		      *         *        (*)
	
		y
		 ^ 
		 |    *         *
		 |---------|---------|
		 | \       | \       |
		 |  \      |  \      |
		 |   \     |   \     |
		*|    \    |    \    |*
		 |     \   |     \   |
		 |      \  |      \  |
		 |       \ |       \ |
		 |---------|---------|---> x
		      *         *
		
			  
		*/

		if(GEOMORPHING_SUPPORT)
		{
			float morphed_z = p_z;
			Vec4f morphed_normal = normal;
			//if((y % 2) == 0)
			//{
			//	if(((x % 2) == 1) && (x + 1 < raw_heightfield.getWidth()))
			//	{
			//		assert(x >= 1 && x + 1 < raw_heightfield.getWidth());
			//		morphed_z      = 0.5f   * (raw_heightfield.elem(x-1, y) + raw_heightfield.elem(x+1, y));
			//		morphed_normal = normalise(raw_normals    .elem(x-1, y) + raw_normals    .elem(x+1, y));
			//	}
			//}
			//else
			//{
			//	if(((x % 2) == 0) && (y + 1 < raw_heightfield.getHeight()))
			//	{
			//		assert(y >= 1 && y + 1 < raw_heightfield.getHeight());
			//		morphed_z      = 0.5f   * (raw_heightfield.elem(x, y-1) + raw_heightfield.elem(x, y+1));
			//		morphed_normal = normalise(raw_normals    .elem(x, y-1) + raw_normals    .elem(x, y+1));
			//	}
			//}
			std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x) + morph_offset_B,        &morphed_z,           sizeof(float));

			const uint32 packed_morph_normal = packNormal(morphed_normal);
			std::memcpy(vert_data + vert_size_B * (y * vert_res_with_borders + x) + morph_normal_offset_B, &packed_morph_normal, sizeof(uint32));
		}
	}

	meshdata.aabb_os = aabb_os;

	//conPrint("Creating mesh took           " + timer.elapsedStringMSWIthNSigFigs(4));
	

	if(build_physics_ob)
	{
		//timer.reset();
		
		chunk_data_out.physics_shape = PhysicsWorld::createJoltHeightFieldShape(jolt_vert_res, jolt_heightfield, quad_w);

		//conPrint("Creating physics shape took  " + timer.elapsedStringMSWIthNSigFigs(4));
	}

	//conPrint("---------------");
}


void TerrainSystem::removeLeafGeometry(TerrainNode* node)
{
	checkRemoveObAndSetRefToNull(opengl_engine, node->gl_ob);
	checkRemoveObAndSetRefToNull(physics_world, node->physics_ob);
}


void TerrainSystem::removeSubtree(TerrainNode* node, std::vector<GLObjectRef>& old_subtree_gl_obs_in_out, std::vector<PhysicsObjectRef>& old_subtree_phys_obs_in_out)
{
	ContainerUtils::append(old_subtree_gl_obs_in_out, node->old_subtree_gl_obs);
	ContainerUtils::append(old_subtree_phys_obs_in_out, node->old_subtree_phys_obs);

	if(node->children[0].isNull()) // If this is a leaf node:
	{
		// Remove mesh for leaf node, if any
		//removeLeafGeometry(node);
		if(node->gl_ob.nonNull())
			old_subtree_gl_obs_in_out.push_back(node->gl_ob);
		if(node->physics_ob.nonNull())
			old_subtree_phys_obs_in_out.push_back(node->physics_ob);
	}
	else // Else if this node is an interior node:
	{
		// Remove children
		for(int i=0; i<4; ++i)
		{
			removeSubtree(node->children[i].ptr(), old_subtree_gl_obs_in_out, old_subtree_phys_obs_in_out);
			id_to_node_map.erase(node->children[i]->id);

			if(node->children[i]->vis_aabb_gl_ob.nonNull())
				opengl_engine->removeObject(node->children[i]->vis_aabb_gl_ob);

			node->children[i] = NULL;
		}
	}
}


// The root node of the subtree, 'node', has already been created.
void TerrainSystem::createInteriorNodeSubtree(TerrainNode* node, const Vec3d& campos)
{
	// We should split this node into 4 children, and make it an interior node.
	const float cur_w = node->aabb.max_[0] - node->aabb.min_[0];
	const float child_w = cur_w * 0.5f;

	// bot left child
	node->children[0] = new TerrainNode();
	node->children[0]->parent = node;
	node->children[0]->depth = node->depth + 1;
	node->children[0]->aabb = js::AABBox(node->aabb.min_, node->aabb.max_ - Vec4f(child_w, child_w, 0, 0));

	// bot right child
	node->children[1] = new TerrainNode();
	node->children[1]->parent = node;
	node->children[1]->depth = node->depth + 1;
	node->children[1]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, 0, 0, 0), node->aabb.max_ - Vec4f(0, child_w, 0, 0));

	// top right child
	node->children[2] = new TerrainNode();
	node->children[2]->parent = node;
	node->children[2]->depth = node->depth + 1;
	node->children[2]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, child_w, 0, 0), node->aabb.max_);

	// top left child
	node->children[3] = new TerrainNode();
	node->children[3]->parent = node;
	node->children[3]->depth = node->depth + 1;
	node->children[3]->aabb = js::AABBox(node->aabb.min_ + Vec4f(0, child_w, 0, 0), node->aabb.max_ - Vec4f(child_w, 0, 0, 0));

	// Add an AABB visualisation for debugging
	if(false)
	{
		const Colour3f col = depth_colours[(node->depth + 1) % staticArrayNumElems(depth_colours)];
		for(int i=0; i<4; ++i)
		{
			float padding = 0.01f;
			Vec4f padding_v(padding,padding,padding,0);
			node->children[i]->vis_aabb_gl_ob = opengl_engine->makeAABBObject(node->children[i]->aabb.min_ - padding_v, node->children[i]->aabb.max_ + padding_v, Colour4f(col[0], col[1], col[2], 0.2f));
			opengl_engine->addObject(node->children[i]->vis_aabb_gl_ob);
		}
	}


	// Assign child nodes ids and add to id_to_node_map.
	for(int i=0; i<4; ++i)
	{
		node->children[i]->id = next_id++;
		id_to_node_map[node->children[i]->id] = node->children[i].ptr();
	}

	node->subtree_built = false;

	// Recurse to build child trees
	for(int i=0; i<4; ++i)
		createSubtree(node->children[i].ptr(), campos);
}


static const float USE_MIN_DIST_TO_AABB = 5.f;

static const int LOWER_DEPTH_BOUND = 3; // Enforce some tessellation to make sure each chunk lies completely in only one source terrain section.

// The root node of the subtree, 'node', has already been created.
void TerrainSystem::createSubtree(TerrainNode* node, const Vec3d& campos)
{
	//conPrint("Creating subtree, depth " + toString(node->depth) + ", at " + node->aabb.toStringMaxNDecimalPlaces(4));

	const float min_dist = myMax(USE_MIN_DIST_TO_AABB, node->aabb.distanceToPoint(campos.toVec4fPoint()));

	//const int desired_lod_level = myClamp((int)std::log2(quad_w_screenspace_target * min_dist), /*lowerbound=*/0, /*upperbound=*/8);
	// depth = log2(world_w / (res * d * quad_w_screenspace))
	const int desired_depth = myClamp((int)std::log2(world_w / (chunk_res * min_dist * quad_w_screenspace_target)), /*lowerbound=*/LOWER_DEPTH_BOUND, /*upperbound=*/max_depth);

	//assert(desired_lod_level <= node->lod_level);
	//assert(desired_depth >= node->depth);

	if(desired_depth > node->depth)
	{
		createInteriorNodeSubtree(node, campos);
	}
	else
	{
		assert(desired_depth <= node->depth);
		// This node should be a leaf node

		assert(num_uncompleted_tasks >= 0);
		num_uncompleted_tasks++;

		// Create geometry for it
		MakeTerrainChunkTask* task = new MakeTerrainChunkTask();
		task->node_id = node->id;
		task->chunk_x = node->aabb.min_[0];
		task->chunk_y = node->aabb.min_[1];
		task->chunk_w = node->aabb.max_[0] - node->aabb.min_[0];
		task->build_physics_ob = min_dist <= MAX_PHYSICS_DIST;
		//task->build_physics_ob = (max_depth - node->depth) < 3;
		task->terrain = this;
		task->out_msg_queue = out_msg_queue;
		task->num_uncompleted_tasks_ptr = &num_uncompleted_tasks;
		task_manager->addTask(task);

		node->building = true;
		node->subtree_built = false;
	}
}


void TerrainSystem::updateSubtree(TerrainNode* cur, const Vec3d& campos)
{
	// We want each leaf node to have lod_level = desired_lod_level for that node

	// Get distance from camera to node

	const float min_dist = myMax(USE_MIN_DIST_TO_AABB, cur->aabb.distanceToPoint(campos.toVec4fPoint()));
	//printVar(min_dist);

	//const int desired_lod_level = myClamp((int)std::log2(quad_w_screenspace_target * min_dist), /*lowerbound=*/0, /*upperbound=*/8);
	const int desired_depth = myClamp((int)std::log2(world_w / (chunk_res * min_dist * quad_w_screenspace_target)), /*lowerbound=*/LOWER_DEPTH_BOUND, /*upperbound=*/max_depth);

	if(cur->children[0].isNull()) // If 'cur' is a leaf node (has no children, so is not interior node):
	{
		if(desired_depth > cur->depth) // If the desired lod level is greater than the leaf's lod level, we want to split the leaf into 4 child nodes
		{
			// Remove mesh for leaf node, if any
			//removeLeafGeometry(cur);
			// Don't remove leaf geometry yet, wait until subtree geometry is fully built to replace it.
			//cur->num_children_built = 0;
			if(cur->gl_ob.nonNull()) cur->old_subtree_gl_obs.push_back(cur->gl_ob);
			cur->gl_ob = NULL;
			if(cur->physics_ob.nonNull()) cur->old_subtree_phys_obs.push_back(cur->physics_ob);
			cur->physics_ob = NULL;
			
			createSubtree(cur, campos);
		}
	}
	else // Else if 'cur' is an interior node:
	{
		if(desired_depth <= cur->depth) // And it should be a leaf node, or not exist (it is currently too detailed)
		{
			// Change it into a leaf node:

			// Remove children of cur and their subtrees
			for(int i=0; i<4; ++i)
			{
				removeSubtree(cur->children[i].ptr(), cur->old_subtree_gl_obs, cur->old_subtree_phys_obs);
				id_to_node_map.erase(cur->children[i]->id);

				if(cur->children[i]->vis_aabb_gl_ob.nonNull())
					opengl_engine->removeObject(cur->children[i]->vis_aabb_gl_ob);

				cur->children[i] = NULL;
			}
		
			// Start creating geometry for this node:
			// Note that we may already be building geometry for this node, from a previous change from interior node to leaf node.
			// In this case don't make a new task, just wait for existing task.

			assert(cur->gl_ob.isNull());
			if(!cur->building)
			{
				// No chunk at this location, make one
				assert(num_uncompleted_tasks >= 0);
				num_uncompleted_tasks++;

				MakeTerrainChunkTask* task = new MakeTerrainChunkTask();
				task->node_id = cur->id;
				task->chunk_x = cur->aabb.min_[0];
				task->chunk_y = cur->aabb.min_[1];
				task->chunk_w = cur->aabb.max_[0] - cur->aabb.min_[0];
				task->build_physics_ob = min_dist <= MAX_PHYSICS_DIST;
				//task->build_physics_ob = (max_depth - cur->depth) < 3;
				task->terrain = this;
				task->out_msg_queue = out_msg_queue;
				task->num_uncompleted_tasks_ptr = &num_uncompleted_tasks;
				task_manager->addTask(task);

				//conPrint("Making new node chunk");

				cur->subtree_built = false;
				cur->building = true;
			}
		}
		else // Else if 'cur' should still be an interior node:
		{
			assert(cur->children[0].nonNull());
			for(int i=0; i<4; ++i)
				updateSubtree(cur->children[i].ptr(), campos);
		}
	}
}


// The subtree with root node 'node' is fully built, so we can remove any old meshes for it, and insert the new pending meshes.
void TerrainSystem::insertPendingMeshesForSubtree(TerrainNode* node)
{
	// Remove any old subtree GL obs and physics obs, now the mesh for this node is ready.
	for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
		opengl_engine->removeObject(node->old_subtree_gl_obs[i]);
	node->old_subtree_gl_obs.clear();

	for(size_t i=0; i<node->old_subtree_phys_obs.size(); ++i)
		physics_world->removeObject(node->old_subtree_phys_obs[i]);
	node->old_subtree_phys_obs.clear();


	if(node->children[0].isNull()) // If leaf node:
	{
		if(node->pending_gl_ob.nonNull())
		{
//FAILING			assert(node->gl_ob.isNull());
			node->gl_ob = node->pending_gl_ob;
			opengl_engine->addObject(node->gl_ob);
			node->pending_gl_ob = NULL;
		}

		if(node->pending_physics_ob.nonNull())
		{
			//			assert(node->physics_ob.isNull());
			node->physics_ob = node->pending_physics_ob;
			physics_world->addObject(node->physics_ob);
			node->pending_physics_ob = NULL;
		}
	}
	else
	{
		for(int i=0; i<4; ++i)
			insertPendingMeshesForSubtree(node->children[i].ptr());
	}
}


/*

When node a is subdivided into 4 (or more) nodes;
set a counter, num_children_built on node a to zero.
whenever node b, c, d, e is built, walk up tree to parent (a), and increment num_children_built.
When it reaches 4, this means that all children are built.  In that case remove the gl ob from node a, and add all gl obs in the subtrees of node a.

                                  a
 a          =>                    |_______________
                                  |    |    |     |
                                  b    c    d     e

                                  a
 a          =>                    |_______________
                                  |    |    |     |
                                  b    c    d     e
                                       |____________
                                       |     |     |
                                       f     g     h


A subtree with root node n is complete if all leaf nodes in the subtree are built.


When an interior node is changed into a leaf node (e.g. children are removed), remove children but add a list of their gl objects to their parent (old_subtree_gl_obs).

a
|_______________             =>                a
|    |    |     |
b    c    d     e

e.g. when b, c, d, e are removed, add list of their gl objects to node a.   When node a is built, remove gl objects in old_subtree_gl_obs from world, and add node 'a' gl object.

*/


bool TerrainSystem::areAllParentSubtreesBuilt(TerrainNode* node)
{
	TerrainNode* cur = node->parent;
	while(cur)
	{
		if(!cur->subtree_built)
			return false;
		cur = cur->parent;
	}

	return true;
}



void TerrainSystem::handleCompletedMakeChunkTask(const TerrainChunkGeneratedMsg& msg)
{
	//Timer timer;

	// Lookup node based on id
	auto res = id_to_node_map.find(msg.node_id);
	if(res != id_to_node_map.end())
	{
		TerrainNode& node = *res->second;

		node.building = false;
		if(node.children[0].nonNull()) // If this is an interior node:
			return; // Discard the obsolete built mesh.  This will happen if a leaf node gets converted to an interior node while the mesh is building.

		// This node is a leaf node, and we have the mesh for it, therefore the subtree is complete.
		node.subtree_built = true;

		
		Reference<OpenGLMeshRenderData> mesh_data = msg.chunk_data.mesh_data;

		// Update node AABB, now that we have actual heightfield data.
		// Offset node object space AABB by the chunk x, y coords to get the world-space AABB.
		node.aabb = js::AABBox(
			mesh_data->aabb_os.min_ + Vec4f(msg.chunk_x, msg.chunk_y, 0, 0),
			mesh_data->aabb_os.max_ + Vec4f(msg.chunk_x, msg.chunk_y, 0, 0)
		);

		{
			//printVar(mesh_data->vert_index_buffer_uint16.dataSizeBytes());
			//printVar(mesh_data->vert_data.dataSizeBytes());

			//if(!mesh_data->vert_index_buffer.empty())
			//	mesh_data->indices_vbo_handle = opengl_engine->vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer.data(), mesh_data->vert_index_buffer.dataSizeBytes());
			//else
			//	mesh_data->indices_vbo_handle = opengl_engine->vert_buf_allocator->allocateIndexData(mesh_data->vert_index_buffer_uint16.data(), mesh_data->vert_index_buffer_uint16.dataSizeBytes());
			assert(msg.chunk_data.vert_res_with_borders == 10 || msg.chunk_data.vert_res_with_borders == 130);
			if(msg.chunk_data.vert_res_with_borders == 10)
				mesh_data->indices_vbo_handle = this->vert_res_10_index_buffer;
			else if(msg.chunk_data.vert_res_with_borders == 130)
				mesh_data->indices_vbo_handle = this->vert_res_130_index_buffer;
			else
				conPrint("Erropr. invalid msg.chunk_data.vert_res_with_borders");

			mesh_data->vbo_handle = opengl_engine->vert_buf_allocator->allocateVertexDataSpace(mesh_data->vertex_spec.vertStride(), mesh_data->vert_data.data(), mesh_data->vert_data.dataSizeBytes());

			opengl_engine->vert_buf_allocator->getOrCreateAndAssignVAOForMesh(*mesh_data, mesh_data->vertex_spec);

			// Now data has been uploaded to GPU, clear CPU mem
			mesh_data->vert_data.clearAndFreeMem();
			mesh_data->vert_index_buffer.clearAndFreeMem();
			mesh_data->vert_index_buffer_uint16.clearAndFreeMem();
			mesh_data->vert_index_buffer_uint8.clearAndFreeMem();
		}

		GLObjectRef gl_ob = opengl_engine->allocateObject();
		gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(msg.chunk_x, msg.chunk_y, 0);
		gl_ob->mesh_data = mesh_data;

		// d = (2 ^ chunk_lod_lvl) / quad_w_screenspace_target
		//const float lod_transition_dist = (1 << max_lod_level) / quad_w_screenspace_target;
		/*chunk.gl_ob->morph_start_dist = lod_transition_dist;
		chunk.gl_ob->morph_end_dist = lod_transition_dist * 1.02f;*/
		//gl_ob->aabb_min_x = node.aabb.min_[0];
		//gl_ob->aabb_min_y = node.aabb.min_[1];
		//gl_ob->aabb_w = node.aabb.max_[0] - node.aabb.min_[0];

		// Compute distance at which this node will transition to a smaller depth value (node.depth - 1).
		//
		// From above:
		// depth = log2(world_w / (res * d * quad_w_screenspace))
		// 2^depth = world_w / (res * d * quad_w_screenspace);
		// res * d * quad_w_screenspace * 2^depth = world_w
		// d = world_w / (res * quad_w_screenspace * 2^depth)
		const float transition_depth = world_w / (chunk_res * quad_w_screenspace_target * (1 << node.depth));
		//printVar(transition_depth);

		gl_ob->morph_start_dist = transition_depth * 0.75f;
		gl_ob->morph_end_dist   = transition_depth;

		//printVar(gl_ob->morph_start_dist);
		//printVar(gl_ob->morph_end_dist);


		gl_ob->materials.resize(1);
		gl_ob->materials[0] = terrain_mat;
		//assert(node.depth >= 0 && node.depth < staticArrayNumElems(depth_colours));
		//gl_ob->materials[0].albedo_linear_rgb = depth_colours[node.depth % staticArrayNumElems(depth_colours)];

		// Assign mask map as diffuse texture, based on which source section the chunk lies in.
		const float chunk_middle_x = msg.chunk_x + msg.chunk_w/2; // world space x coord in middle of chunk
		const float chunk_middle_y = msg.chunk_y + msg.chunk_w/2;

		const int section_x = Maths::floorToInt(chunk_middle_x / terrain_section_w + 0.5); // section indices
		const int section_y = Maths::floorToInt(chunk_middle_y / terrain_section_w + 0.5);

		const int index_x = section_x + TERRAIN_SECTION_OFFSET; // Indices into terrain_data_sections array
		const int index_y = section_y + TERRAIN_SECTION_OFFSET;

		if(index_x >= 0 && index_x < 8 && index_y >= 0 && index_y < 8)
		{
			const TerrainDataSection& section = terrain_data_sections[index_x + index_y * TERRAIN_DATA_SECTION_RES];
			gl_ob->materials[0].albedo_texture = section.mask_gl_tex;
		}

		// Since we are doing texture clamping, we need to make sure tex coords are mapped to [0, 1] with tex matrix
		// For example, section (1, 2), will have section_x = 1 and section_y = 2.
		// Its uvs are world space coordinates, so will be something like (8192 * 1, 8192 * 2) at corner, before texture matrix multiplication.
		// After multiplciation is (1, 2).  We want to translate that down to (0, 0), so we want to translate by (-1, -2)
		gl_ob->materials[0].tex_translation = Vec2f(-(float)section_x, -(float)section_y);


		PhysicsShape shape = msg.chunk_data.physics_shape;

		PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
		physics_ob->shape = shape;
		physics_ob->pos = Vec4f(msg.chunk_x, msg.chunk_y, 0, 1);
		physics_ob->rot = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
		physics_ob->scale = Vec3f(1.f);

		physics_ob->kinematic = false;
		physics_ob->dynamic = false;

		node.pending_gl_ob = gl_ob;
		node.pending_physics_ob = physics_ob;


		if(areAllParentSubtreesBuilt(&node))
		{
			insertPendingMeshesForSubtree(&node);
		}
		else
		{
			TerrainNode* cur = node.parent;
			while(cur)
			{
				bool cur_subtree_built = true;
				for(int i=0; i<4; ++i)
					if(!cur->children[i]->subtree_built)
					{
						cur_subtree_built = false;
						break;
					}

				if(!cur->subtree_built && cur_subtree_built)
				{
					// If cur subtree was not built before, and now it is:

					if(areAllParentSubtreesBuilt(cur))
					{
						insertPendingMeshesForSubtree(cur);
					}

					cur->subtree_built = true;
				}

				if(!cur_subtree_built)
					break;

				cur = cur->parent;
			}
		}
	}

	//conPrint("TerrainSystem::handleCompletedMakeChunkTask() took " + timer.elapsedString());
}


void MakeTerrainChunkTask::run(size_t thread_index)
{
	try
	{
		assert((*num_uncompleted_tasks_ptr) >= 0);

		// Make terrain
		terrain->makeTerrainChunkMesh(chunk_x, chunk_y, chunk_w, build_physics_ob, /*chunk data out=*/chunk_data);

		// Send message to out-message-queue (e.g. to MainWindow), saying that we have finished the work.
		TerrainChunkGeneratedMsg* msg = new TerrainChunkGeneratedMsg();
		msg->chunk_x = chunk_x;
		msg->chunk_y = chunk_y;
		msg->chunk_w = chunk_w;
		//msg->lod_level = lod_level;
		msg->chunk_data = chunk_data;
		msg->node_id = node_id;
		out_msg_queue->enqueue(msg);

		// Make water
		//TerrainSystem::makeTerrainChunkMesh(chunk_x_i, chunk_y_i, lod_level, /*water=*/true, chunk_data);
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

	assert((*num_uncompleted_tasks_ptr) >= 0);
	(*num_uncompleted_tasks_ptr)--;
}


void MakeTerrainChunkTask::removedFromQueue()
{
	assert((*num_uncompleted_tasks_ptr) >= 0);
	(*num_uncompleted_tasks_ptr)--;
}
