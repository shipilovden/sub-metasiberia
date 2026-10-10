/*=====================================================================
TerrainSystem.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "ThreadMessages.h"
#include "TerrainScattering.h"
#include "PhysicsObject.h"
#include <opengl/IncludeOpenGL.h>
#include <opengl/OpenGLTexture.h>
#include <opengl/OpenGLEngine.h>
#include <graphics/ImageMap.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <utils/Array2D.h>
#include <utils/StackAllocator.h>
#include <utils/Mutex.h>
#include <maths/Matrix4f.h>
#include <maths/vec3.h>
#include <string>
#include <map>
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;
class PhysicsWorld;
class BiomeManager;


/*=====================================================================
TerrainSystem
-------------
=====================================================================*/


//-------------------------- Specification of Terrain - heightmaps and mask maps to use on each section --------------------------
// Similar to TerrainSpec in WorldSettings.h but with paths instead of URLs

struct TerrainPathSpecSection
{
	int x, y; // section coordinates.  (0,0) is section centered on world origin.

	OpenGLTextureKey heightmap_path;
	OpenGLTextureKey mask_map_path;
	OpenGLTextureKey tree_mask_map_path;
	OpenGLTextureKey road_mask_map_path;
	OpenGLTextureKey building_mask_map_path;
};

struct TerrainPathSpec
{
	std::vector<TerrainPathSpecSection> section_specs;

	OpenGLTextureKey detail_col_map_paths[4];
	OpenGLTextureKey detail_height_map_paths[4];

	float terrain_section_width_m;
	float terrain_height_scale;
	float default_terrain_z;
	float water_z;
	uint32 flags;
};
//-------------------------- End Specification of Terrain --------------------------


enum TerrainSculptTool
{
	TerrainSculptTool_Ridge = 0,
	TerrainSculptTool_Raise,
	TerrainSculptTool_Lower,
	TerrainSculptTool_SoftRaise,
	TerrainSculptTool_HardRaise,
	TerrainSculptTool_SoftLower,
	TerrainSculptTool_HardLower,
	TerrainSculptTool_Pinch,
	TerrainSculptTool_Inflate,
	TerrainSculptTool_Deflate,
	TerrainSculptTool_Clay,
	TerrainSculptTool_Blob,
	TerrainSculptTool_Amplify,
	TerrainSculptTool_Dampen,
	TerrainSculptTool_Smooth,
	TerrainSculptTool_Polish,
	TerrainSculptTool_Sharpen,
	TerrainSculptTool_Flatten,
	TerrainSculptTool_Plateau,
	TerrainSculptTool_Ramp,
	TerrainSculptTool_Cliff,
	TerrainSculptTool_Wall,
	TerrainSculptTool_Basin,
	TerrainSculptTool_Gorge,
	TerrainSculptTool_Berm,
	TerrainSculptTool_Saddle,
	TerrainSculptTool_Notch,
	TerrainSculptTool_Lake,
	TerrainSculptTool_Fill,
	TerrainSculptTool_Lowland,
	TerrainSculptTool_Coast,
	TerrainSculptTool_Shelf,
	TerrainSculptTool_Cove,
	TerrainSculptTool_Spit,
	TerrainSculptTool_River,
	TerrainSculptTool_Channel,
	TerrainSculptTool_Delta,
	TerrainSculptTool_Sea,
	TerrainSculptTool_StampVolcano,
	TerrainSculptTool_StampCrater,
	TerrainSculptTool_StampHill,
	TerrainSculptTool_StampCone,
	TerrainSculptTool_StampMesa,
	TerrainSculptTool_StampCaldera,
	TerrainSculptTool_StampRidge,
	TerrainSculptTool_StampSpire,
	TerrainSculptTool_StampPyramid,
	TerrainSculptTool_StampBowl,
	TerrainSculptTool_StampArch,
	TerrainSculptTool_StampAtoll,
	TerrainSculptTool_StampTwin,
	TerrainSculptTool_StampDunes,
	TerrainSculptTool_StampTor,
	TerrainSculptTool_StampRamp,
	TerrainSculptTool_IslandClassic,
	TerrainSculptTool_IslandHigh,
	TerrainSculptTool_IslandLow,
	TerrainSculptTool_IslandBarrier,
	TerrainSculptTool_IslandTwin,
	TerrainSculptTool_IslandCaldera,
	TerrainSculptTool_IslandMesa,
	TerrainSculptTool_IslandRias,
	TerrainSculptTool_IslandSpit,
	TerrainSculptTool_IslandArch,
	TerrainSculptTool_IslandChain,
	TerrainSculptTool_IslandReef,
	TerrainSculptTool_IslandRing,
	TerrainSculptTool_IslandCrescent,
	TerrainSculptTool_IslandStar,
	TerrainSculptTool_IslandSpiral,
	TerrainSculptTool_IslandSerpent,
	TerrainSculptTool_IslandShards,
	TerrainSculptTool_IslandCrystal,
	TerrainSculptTool_IslandPlateau,
	TerrainSculptTool_IslandMushroom,
	TerrainSculptTool_IslandHeart,
	TerrainSculptTool_IslandNeedle,
	TerrainSculptTool_IslandLagoonChain
};


struct TerrainSculptedHeightmap
{
	int x, y;
	ImageMapFloatRef map;
};

struct TerrainSculptedMaskMap
{
	int x, y;
	bool tree_mask;
	ImageMapUInt8Ref map;
};


struct TerrainDataSection
{
	OpenGLTextureKey heightmap_path;
	OpenGLTextureKey mask_map_path;
	OpenGLTextureKey tree_mask_map_path;
	OpenGLTextureKey road_mask_map_path;
	OpenGLTextureKey building_mask_map_path;

	Map2DRef heightmap;
	ImageMapFloatRef sculpt_heightmap;
	OpenGLTextureRef heightmap_gl_tex;
	OpenGLTextureRef sculpt_heightmap_gl_tex;
	bool sculpt_heightmap_texture_dirty;
	Map2DRef maskmap;
	ImageMapUInt8Ref sculpt_maskmap;
	ImageMapUInt8Ref sculpt_treemaskmap;
	bool sculpt_maskmap_texture_dirty;
	OpenGLTextureRef mask_gl_tex;

	Map2DRef treemaskmap;
	Map2DRef road_maskmap;
	Map2DRef building_maskmap;
	OpenGLTextureRef road_mask_gl_tex;
	OpenGLTextureRef building_mask_gl_tex;
	GLObjectRef road_mask_decal_gl_ob;
	GLObjectRef building_mask_decal_gl_ob;
};


struct TerrainChunkData
{
	int vert_res_with_borders;
	
	Reference<OpenGLMeshRenderData> mesh_data;

	PhysicsShape physics_shape;
};


class MakeTerrainChunkTask : public glare::Task
{
public:
	virtual void run(size_t thread_index) override;

	virtual void removedFromQueue() override;

	uint64 node_id;
	float chunk_x, chunk_y; // world-space coords of lower left corner of chunk.
	float chunk_w; // Width of chunk in world-space (m)
	bool build_physics_ob;

	TerrainSystem* terrain;

	TerrainChunkData chunk_data; // Result of building chunk

	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;

	glare::AtomicInt* num_uncompleted_tasks_ptr;
};


class TerrainChunkGeneratedMsg : public ThreadMessage
{
public:
	TerrainChunkGeneratedMsg() : ThreadMessage(Msg_TerrainChunkGeneratedMsg) {}

	float chunk_x, chunk_y; // world-space coords of lower left corner of chunk.
	float chunk_w; // Width of chunk in world-space (m)
	uint64 node_id;

	TerrainChunkData chunk_data;
};


// Quad-tree node
struct TerrainNode : public RefCounted
{
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainNode() : building(false), subtree_built(false) {}
	
	GLObjectRef gl_ob;
	PhysicsObjectRef physics_ob;

	GLObjectRef vis_aabb_gl_ob;

	// Objects that have been built, but not inserted into the world yet, because a parent node is waiting for all descendant nodes to finish building.
	GLObjectRef pending_gl_ob;
	PhysicsObjectRef pending_physics_ob;

	TerrainNode* parent;

	js::AABBox aabb; // world space AABB
	int depth;
	uint64 id;
	bool building;
	bool subtree_built; // is subtree built?
	Reference<TerrainNode> children[4];

	std::vector<GLObjectRef> old_subtree_gl_obs; // Objects that are still inserted into opengl engine
	std::vector<PhysicsObjectRef> old_subtree_phys_obs;
};


class TerrainSystem : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainSystem();
	~TerrainSystem();

	friend class TerrainTests;
	friend class TerrainScattering;
	friend class MakeTerrainChunkTask;

	void init(const TerrainPathSpec& spec, const std::string& base_dir_path, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, BiomeManager* biome_manager, const Vec3d& campos, glare::TaskManager* task_manager, glare::StackAllocator& bump_allocator, ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue);

	void shutdown();

	// A texture that will be used by the terrain system has been loaded into OpenGL.
	void handleTextureLoaded(const OpenGLTextureKey& path, const Map2DRef& map);

	bool isTextureUsedByTerrain(const OpenGLTextureKey& path) const;

	void handleCompletedMakeChunkTask(const TerrainChunkGeneratedMsg& msg);

	void updateCampos(const Vec3d& campos, glare::StackAllocator& bump_allocator);

	void rebuildScattering();

	void invalidateVegetationMap(const js::AABBox& aabb_ws);

	// Native terrain sculpting.  All calls are made on the GUI/render thread.
	bool traceRay(const Vec3d& origin, const Vec3d& direction, Vec3d& hit_pos_out) const;
	void beginSculptStroke();
	void endSculptStroke();
	bool sculptAtWorld(const Vec3d& hit_pos, const Vec3d* previous_hit_pos, TerrainSculptTool tool,
		float radius_m, float strength_m, float target_height_m,
		float island_sea_floor_m = -85.f, float island_land_base_m = 8.f,
		float island_peak_m = 282.f, int island_seed = 42);
	static float getIslandPreviewHeight(int island_kind, float u, float v, int seed,
		float sea_floor_m, float land_base_m, float peak_m);
	bool paintTerrainMapAtWorld(const Vec3d& hit_pos, float radius_m, float strength,
		int channel, bool tree_mask, bool erase);
	bool smoothAtWorld(const Vec3d& centre, float radius_m, float strength, int passes,
		int* loaded_sections_out = NULL, int* skipped_sections_out = NULL);
	bool isHeightmapSectionLoaded(int section_x, int section_y) const;
	bool canUndoSculpt() const;
	bool canRedoSculpt() const;
	bool undoSculpt();
	bool redoSculpt();
	bool hasSculptedHeightmaps() const;
	void getSculptedHeightmaps(std::vector<TerrainSculptedHeightmap>& maps_out) const;
	bool hasSculptedMaskMaps() const;
	void getSculptedMaskMaps(std::vector<TerrainSculptedMaskMap>& maps_out) const;

	bool isTerrainFullyBuilt();

	std::string getDiagnostics() const;


	Colour4f evalTerrainMask(float p_x, float p_y) const;
	float evalTreeMask(float p_x, float p_y) const; // Return value >= 0.5: tree allowed
	float evalTerrainHeight(float p_x, float p_y, float quad_w) const;

private:
	void makeTerrainChunkMesh(float chunk_x, float chunk_y, float chunk_w, bool build_physics_ob, TerrainChunkData& chunk_data_out) const;
	void updateSubtree(TerrainNode* node, const Vec3d& campos);
	void removeSubtree(TerrainNode* node, std::vector<GLObjectRef>& old_children_gl_obs_in_out, std::vector<PhysicsObjectRef>& old_children_phys_obs_in_out);
	void removeLeafGeometry(TerrainNode* node);
	void createInteriorNodeSubtree(TerrainNode* node, const Vec3d& campos);
	void createSubtree(TerrainNode* node, const Vec3d& campos);
	void insertPendingMeshesForSubtree(TerrainNode* node);
	bool areAllParentSubtreesBuilt(TerrainNode* node);
	void removeAllNodeDataForSubtree(TerrainNode* node);
	void updateReferenceMaskOverlay(int section_x, int section_y, TerrainDataSection& section);
	void updateReferenceMaskDecalTransforms(float camera_z);
	void rebuildAfterSculptIfNeeded();
	void updateSculptedHeightmapTextures();
	ImageMapFloatRef makeEditableHeightmap(TerrainDataSection& section);
	TerrainDataSection* getSectionForSculptCoords(int section_x, int section_y);
	const TerrainDataSection* getSectionForSculptCoords(int section_x, int section_y) const;
	struct TerrainSculptPatch;
	struct TerrainSculptMaskPatch;
	struct TerrainSculptStroke;
	void applySculptPatch(const TerrainSculptPatch& patch, bool use_after_values);
	void applySculptMaskPatch(const TerrainSculptMaskPatch& patch, bool use_after_values);

	GLARE_DISABLE_COPY(TerrainSystem);

	std::map<uint64, TerrainNode*> id_to_node_map;
	uint64 next_id;

	OpenGLMaterial terrain_mat;

	OpenGLMaterial water_mat;

	Reference<TerrainNode> root_node;

	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;
	BiomeManager* biome_manager;
	glare::TaskManager* task_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;

	TerrainScattering terrain_scattering;

public:
	static const int TERRAIN_DATA_SECTION_RES = 8;
	static const int TERRAIN_SECTION_OFFSET = TERRAIN_DATA_SECTION_RES / 2;
private:
	TerrainDataSection terrain_data_sections[TERRAIN_DATA_SECTION_RES*TERRAIN_DATA_SECTION_RES];
	
	Map2DRef detail_heightmaps[4];

	IndexBufAllocationHandle vert_res_10_index_buffer;
	IndexBufAllocationHandle vert_res_130_index_buffer;

	TerrainPathSpec spec;

	struct TerrainSculptPatch
	{
		int section_x, section_y;
		int x0, y0, width, height;
		std::vector<float> before;
		std::vector<float> after;
	};

	struct TerrainSculptMaskPatch
	{
		int section_x, section_y;
		bool tree_mask;
		int width, height, channels;
		std::vector<uint8> before;
		std::vector<uint8> after;
	};

	struct TerrainSculptStroke
	{
		std::vector<TerrainSculptPatch> patches;
		std::vector<TerrainSculptMaskPatch> mask_patches;
	};

	std::vector<TerrainSculptStroke> sculpt_undo_stack;
	std::vector<TerrainSculptStroke> sculpt_redo_stack;
	TerrainSculptStroke current_sculpt_stroke;
	bool sculpt_stroke_active;
	bool sculpt_geometry_rebuild_pending;
	bool sculpt_material_mask_upload_pending;
	bool sculpt_tree_scattering_rebuild_pending;

	// Terrain chunks are generated on worker threads while sculpting changes an
	// ImageMapFloat on the GUI thread.  A generated chunk must see one complete
	// heightmap state, never a map while a brush is changing its pixels.
	mutable Mutex heightmaps_mutex;

	// Scale factor for world-space -> heightmap UV conversion.
	// Its reciprocal is the width of the terrain in metres.
	float terrain_section_w;
	float terrain_scale_factor;

	std::vector<GLObjectRef> water_gl_obs;
	Vec3d water_mesh_centre;
	void updateWaterMeshCentre(const Vec3d& campos);
	void updateWaterBathymetry(const Vec3d& campos);
	Vec3d water_bathymetry_centre = Vec3d(1.e30);
	double water_bathymetry_update_time = -1.0;
	float reference_mask_camera_z;

	glare::AtomicInt num_uncompleted_tasks;
};
