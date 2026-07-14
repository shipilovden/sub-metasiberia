/*=====================================================================
VoxelTools.h
------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "VoxelEditorData.h"
#include "../shared/WorldObject.h"
#include <map>
#include <string>
#include <tuple>
#include <vector>


enum class VoxelToolType
{
	Brush,
	Eraser,
	Paint,
	Box,
	Sphere,
	Picker,
	Line,
	Fill,
	Select // UI-only two-point region selection; use selection helpers below.
};


enum class VoxelBrushShape
{
	Cube,
	Sphere
};


enum class VoxelBrushMode
{
	Add,
	Replace,
	Paint
};


struct VoxelToolSettings
{
	int brush_size = 1;                 // Clamped to [1, 16].
	VoxelBrushShape brush_shape = VoxelBrushShape::Cube;
	VoxelBrushMode brush_mode = VoxelBrushMode::Add;
	bool hollow = false;
	bool mirror_x = false;
	bool mirror_y = false;
	bool mirror_z = false;
	Vec3<int> mirror_origin = Vec3<int>(0, 0, 0);
	int material_index = 0;
	int layer_index = -1;               // -1 uses state.active_layer.
	size_t max_voxels = 262144;         // Operation safety cap, clamped to <= 1,000,000.
};


struct VoxelToolInput
{
	VoxelToolInput() : start(0, 0, 0), end(0, 0, 0) {}
	explicit VoxelToolInput(const Vec3<int>& point) : start(point), end(point) {}
	VoxelToolInput(const Vec3<int>& start_, const Vec3<int>& end_) : start(start_), end(end_) {}

	Vec3<int> start;
	Vec3<int> end; // Inclusive drag endpoint for Box/Sphere; ignored by stamps/picker.
};


struct VoxelChange
{
	static constexpr int NO_MATERIAL = -1;

	Vec3<int> coord = Vec3<int>(0, 0, 0);
	int before_material = NO_MATERIAL;
	int after_material = NO_MATERIAL;
	int layer_index = 0;
};


// A command stores only changed coordinates.  Repeated writes to the same
// coordinate preserve the first before-value and the final after-value.
class VoxelEditCommand
{
public:
	VoxelEditCommand() = default;
	explicit VoxelEditCommand(const std::string& name_) : name(name_) {}

	void recordChange(const Vec3<int>& coord, int before_material, int after_material, int layer_index);
	// Appends a command built against the state after this command.  Overlapping
	// coordinates keep this command's original before-value and the next final value.
	void append(const VoxelEditCommand& next);
	void compact();
	bool empty() const { return changes.empty(); }
	size_t changedCount() const { return changes.size(); }

	void redo(glare::AllocatorVector<Voxel, 16>& voxels) const;
	void undo(glare::AllocatorVector<Voxel, 16>& voxels) const;
	void redo(WorldObject& object) const;
	void undo(WorldObject& object) const;

	std::string name;
	std::vector<VoxelChange> changes;

private:
	using CoordKey = std::tuple<int, int, int>;
	void rebuildIndex();
	void apply(glare::AllocatorVector<Voxel, 16>& voxels, bool use_after) const;
	std::map<CoordKey, size_t> change_index;
};


struct VoxelToolResult
{
	VoxelEditCommand command;
	int picked_material_index = VoxelChange::NO_MATERIAL;
	bool truncated = false;
	std::string error;

	bool changed() const { return !command.empty(); }
};


// Inclusive, normalised voxel-space bounds used by selection operations.
// Selection helpers intentionally operate on one material-backed layer at a
// time, matching the editor's existing layer ownership model.
struct VoxelSelectionBounds
{
	VoxelSelectionBounds() : min(0, 0, 0), max(0, 0, 0) {}
	VoxelSelectionBounds(const Vec3<int>& first, const Vec3<int>& second);

	bool contains(const Vec3<int>& coord) const;
	Vec3<int> extent() const;

	Vec3<int> min;
	Vec3<int> max;
};


struct VoxelClipboardVoxel
{
	Vec3<int> offset = Vec3<int>(0, 0, 0); // Relative to the selection minimum.
	int material_index = VoxelChange::NO_MATERIAL;
};


struct VoxelClipboard
{
	void clear();
	bool empty() const { return voxels.empty(); }
	size_t voxelCount() const { return voxels.size(); }

	Vec3<int> extent = Vec3<int>(0, 0, 0);
	int source_layer_index = -1;
	std::vector<VoxelClipboardVoxel> voxels;
};


struct VoxelClipboardResult
{
	VoxelClipboard clipboard;
	bool truncated = false;
	std::string error;

	bool succeeded() const { return error.empty(); }
};


namespace VoxelTools
{
	// Builds but does not apply a command.  Picker returns no command and places
	// the selected material in picked_material_index.
	VoxelToolResult buildCommand(VoxelToolType tool, const VoxelToolInput& input,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		const glare::AllocatorVector<Voxel, 16>& voxels);

	// Builds and immediately applies the delta to the supplied sparse voxel list.
	// The returned command can be pushed into VoxelUndoStack.
	VoxelToolResult execute(VoxelToolType tool, const VoxelToolInput& input,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		glare::AllocatorVector<Voxel, 16>& voxels);

	int pickMaterial(const glare::AllocatorVector<Voxel, 16>& voxels, const Vec3<int>& coord,
		const VoxelEditorState& state, bool active_layer_only = false);

	// Copies the active (or settings.layer_index) layer using offsets relative
	// to bounds.min.  A selection above the safety cap is rejected atomically.
	VoxelClipboardResult copySelection(const VoxelSelectionBounds& bounds,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		const glare::AllocatorVector<Voxel, 16>& voxels);

	VoxelToolResult buildDeleteSelectionCommand(const VoxelSelectionBounds& bounds,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		const glare::AllocatorVector<Voxel, 16>& voxels);
	VoxelToolResult deleteSelection(const VoxelSelectionBounds& bounds,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		glare::AllocatorVector<Voxel, 16>& voxels);

	// Pastes clipboard offsets relative to destination.  Materials owned by the
	// destination layer are preserved; other materials use settings.material_index.
	// Add/Replace/Paint follow the same collision rules as the brush.
	VoxelToolResult buildPasteCommand(const VoxelClipboard& clipboard, const Vec3<int>& destination,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		const glare::AllocatorVector<Voxel, 16>& voxels);
	VoxelToolResult pasteSelection(const VoxelClipboard& clipboard, const Vec3<int>& destination,
		const VoxelToolSettings& settings, const VoxelEditorState& state,
		glare::AllocatorVector<Voxel, 16>& voxels);

	// Convenience helper: copy bounds, then paste the copy at destination while
	// leaving the source untouched.  The returned paste is one undoable command.
	VoxelToolResult buildDuplicateSelectionCommand(const VoxelSelectionBounds& bounds,
		const Vec3<int>& destination, const VoxelToolSettings& settings,
		const VoxelEditorState& state, const glare::AllocatorVector<Voxel, 16>& voxels);
	VoxelToolResult duplicateSelection(const VoxelSelectionBounds& bounds,
		const Vec3<int>& destination, const VoxelToolSettings& settings,
		const VoxelEditorState& state, glare::AllocatorVector<Voxel, 16>& voxels);

	// Moves a selection as one atomic undo command.  Paste collision semantics
	// still follow settings.brush_mode; a failed paste leaves the source intact.
	VoxelToolResult buildMoveSelectionCommand(const VoxelSelectionBounds& bounds,
		const Vec3<int>& destination, const VoxelToolSettings& settings,
		const VoxelEditorState& state, const glare::AllocatorVector<Voxel, 16>& voxels);
	VoxelToolResult moveSelection(const VoxelSelectionBounds& bounds,
		const Vec3<int>& destination, const VoxelToolSettings& settings,
		const VoxelEditorState& state, glare::AllocatorVector<Voxel, 16>& voxels);

	bool runSelfTest(std::string* details_out = nullptr);
}
