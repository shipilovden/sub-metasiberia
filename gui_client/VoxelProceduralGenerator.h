/*=====================================================================
VoxelProceduralGenerator.h
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "VoxelTools.h"
#include <cstdint>
#include <string>


enum class VoxelProceduralType
{
	Box,
	Ellipsoid,
	Rock,
	TerrainPatch,
	NoiseVolume,
	Crystal,
	Wall
};


// All dimensions describe the inclusive axis-aligned generator volume.  The
// origin is its minimum corner, which makes independent X/Y/Z sizes predictable
// for the panel and keeps placement independent of the current camera.
struct VoxelProceduralParams
{
	uint32_t seed = 12345;
	Vec3<int> origin = Vec3<int>(0, 0, 0);
	int size_x = 16;
	int size_y = 16;
	int size_z = 16;

	int wall_thickness = 1;
	bool hollow = false;
	float noise_scale = 0.12f;
	float threshold = 0.5f;
	float density = 1.0f;
	int octaves = 3;
	int detail = 4;

	// colour_rgba is a UI/material-creation hint.  Substrata's voxel payload
	// stores the resolved material_index, not a colour value per cell.
	uint32_t colour_rgba = 0xFFFFFFFFu;
	int material_index = 0;
	int layer_index = -1; // -1 uses VoxelEditorState::active_layer.
	// Default generation replaces the active layer atomically.  Disable this
	// to merge using write_mode without touching cells outside the new shape.
	bool clear_active_layer = true;
	VoxelBrushMode write_mode = VoxelBrushMode::Add;
	size_t max_voxels = 262144;
};


struct VoxelProceduralMetrics
{
	uint64_t footprint_area = 0;
	uint64_t footprint_perimeter = 0;
	uint64_t bounding_volume = 0;
	uint64_t bounding_surface_area = 0;
};


struct VoxelProceduralResult
{
	VoxelEditCommand command;
	VoxelProceduralParams effective_params;
	VoxelProceduralMetrics metrics;
	size_t generated_voxels = 0;
	bool truncated = false;
	std::string error;

	bool changed() const { return !command.empty(); }
};


namespace VoxelProceduralGenerator
{
	static constexpr int MAX_DIMENSION = 256;
	static constexpr int MAX_WALL_THICKNESS = 16;
	static constexpr size_t MAX_GENERATED_VOXELS = 1000000;

	const char* typeName(VoxelProceduralType type);
	VoxelProceduralParams defaultParams(VoxelProceduralType type);
	VoxelProceduralParams sanitiseParams(const VoxelProceduralParams& params);
	VoxelProceduralMetrics computeMetrics(const VoxelProceduralParams& params);

	// Builds but does not apply a single delta command.  Existing voxels and
	// layer ownership are respected according to params.write_mode.
	VoxelProceduralResult buildCommand(VoxelProceduralType type,
		const VoxelProceduralParams& params, const VoxelEditorState& state,
		const glare::AllocatorVector<Voxel, 16>& voxels);

	// Builds and applies the command.  The caller can push result.command as one
	// undo step, then rebuild/synchronise the object through the usual path.
	VoxelProceduralResult execute(VoxelProceduralType type,
		const VoxelProceduralParams& params, const VoxelEditorState& state,
		glare::AllocatorVector<Voxel, 16>& voxels);

	// Deterministic, renderer-free regression coverage for all generators.
	bool runSelfTest(std::string* details_out = nullptr);
}
