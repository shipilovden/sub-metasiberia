/*=====================================================================
VoxelEditorData.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <cstdint>
#include <string>
#include <vector>


class WorldObject;


// Colours are stored as 0xRRGGBBAA.  The actual voxel-to-colour mapping is
// still provided by WorldObject::materials; the palette is editor metadata.
struct VoxelPalette
{
	uint32_t current_colour = 0xFFFFFFFFu;
	std::vector<uint32_t> colours;
	std::vector<uint32_t> recent_colours;
};


// Substrata voxels carry a material index, so a layer owns material indices
// instead of duplicating the voxel payload.  This keeps legacy voxel objects
// and their on-wire representation intact.
struct VoxelLayer
{
	std::string name = "Layer 1";
	bool visible = true;
	bool locked = false;
	float opacity = 1.0f;
	std::vector<int> material_indices;
	// Aligned with material_indices.  Layer opacity is a non-destructive
	// multiplier; this preserves pre-existing per-material transparency.
	std::vector<float> material_base_opacities;
};


enum class VoxelRenderMode
{
	Greedy,
	Cubes,
	MarchingCubes
};


struct VoxelEditorState
{
	int schema_version = 1;
	std::vector<VoxelLayer> layers;
	int active_layer = 0;
	int current_material_index = 0;
	VoxelPalette palette;
	VoxelRenderMode render_mode = VoxelRenderMode::Greedy;
	bool smooth_normals = false;
	float surface_threshold = 0.5f;
	// Voxel editor metadata occupies WorldObject::content.  Preserve the value
	// that legacy objects (and the generic ObjectEditor) use in this sidecar.
	std::string legacy_content;
};


namespace VoxelEditorData
{
	static constexpr int MAX_LAYERS = 16;
	static constexpr int MAX_PALETTE_COLOURS = 64;
	static constexpr int MAX_MATERIAL_INDEX = 254; // 255 is the mesher's empty-cell sentinel.

	const char* contentMarker();
	bool isVoxelEditorContent(const std::string& content);

	const char* renderModeToString(VoxelRenderMode mode);
	VoxelRenderMode renderModeFromString(const std::string& value);

	VoxelEditorState defaultForMaterialCount(size_t material_count);
	VoxelEditorState defaultForObject(const WorldObject& object);
	VoxelEditorState fromContent(const std::string& content, const VoxelEditorState& fallback,
		std::string* parse_error_out = nullptr, bool* migrated_out = nullptr);
	VoxelEditorState fromObject(const WorldObject& object, std::string* parse_error_out = nullptr,
		bool* migrated_out = nullptr);
	std::string serialiseToContent(const VoxelEditorState& state);
	bool storeOnObject(WorldObject& object, const VoxelEditorState& state);

	void clamp(VoxelEditorState& state);
	int materialLayerIndex(const VoxelEditorState& state, int material_index);
	bool layerOwnsMaterial(const VoxelEditorState& state, int layer_index, int material_index);
	bool ensureMaterialInLayer(VoxelEditorState& state, int layer_index, int material_index);
	float materialBaseOpacity(const VoxelEditorState& state, int layer_index, int material_index);
	bool setMaterialBaseOpacity(VoxelEditorState& state, int layer_index, int material_index, float opacity);
	const VoxelLayer* activeLayer(const VoxelEditorState& state);
	VoxelLayer* activeLayer(VoxelEditorState& state);

	// Deterministic metadata/round-trip test.  It has no renderer or GUI side effects.
	bool runSelfTest(std::string* details_out = nullptr);
}
