/*=====================================================================
VoxelTools.cpp
--------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelTools.h"


#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>


namespace
{
struct Coord
{
	Coord() : x(0), y(0), z(0) {}
	Coord(const int x_, const int y_, const int z_) : x(x_), y(y_), z(z_) {}
	explicit Coord(const Vec3<int>& value) : x(value.x), y(value.y), z(value.z) {}

	bool operator==(const Coord& other) const { return x == other.x && y == other.y && z == other.z; }
	bool operator<(const Coord& other) const
	{
		if(x != other.x) return x < other.x;
		if(y != other.y) return y < other.y;
		return z < other.z;
	}

	Vec3<int> toVec3() const { return Vec3<int>(x, y, z); }
	int x, y, z;
};


struct CoordHash
{
	size_t operator()(const Coord& value) const
	{
		// Integer avalanche; deterministic and independent of pointer size.
		uint64_t h = static_cast<uint32_t>(value.x) * 0x9E3779B185EBCA87ull;
		h ^= static_cast<uint32_t>(value.y) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
		h ^= static_cast<uint32_t>(value.z) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
		return static_cast<size_t>(h);
	}
};


using MaterialMap = std::unordered_map<Coord, int, CoordHash>;


bool validMaterial(const int material)
{
	return material >= 0 && material <= VoxelEditorData::MAX_MATERIAL_INDEX;
}


int sanitiseMaterial(const int material)
{
	return validMaterial(material) ? material : VoxelChange::NO_MATERIAL;
}


bool validVoxelCoord(const Coord& coord)
{
	// These are the limits accepted by the existing Substrata voxel mesher.
	return coord.x >= -32768 && coord.x <= 32766 &&
		coord.y >= -32768 && coord.y <= 32766 &&
		coord.z >= -32768 && coord.z <= 32766;
}


MaterialMap makeMaterialMap(const glare::AllocatorVector<Voxel, 16>& voxels)
{
	MaterialMap result;
	result.reserve(voxels.size());
	for(size_t i=0; i<voxels.size(); ++i)
		if(validMaterial(voxels[i].mat_index))
			result[Coord(voxels[i].pos)] = voxels[i].mat_index; // Last duplicate matches current mesher behaviour.
	return result;
}


int materialAt(const MaterialMap& materials, const Coord& coord)
{
	const auto it = materials.find(coord);
	return it == materials.end() ? VoxelChange::NO_MATERIAL : it->second;
}


const char* commandName(const VoxelToolType tool)
{
	switch(tool)
	{
	case VoxelToolType::Brush:  return "Brush stroke";
	case VoxelToolType::Eraser: return "Erase voxels";
	case VoxelToolType::Paint:  return "Paint voxels";
	case VoxelToolType::Box:    return "Create box";
	case VoxelToolType::Sphere: return "Create sphere";
	case VoxelToolType::Picker: return "Pick colour";
	case VoxelToolType::Line:   return "Create voxel line";
	case VoxelToolType::Fill:   return "Fill connected voxels";
	case VoxelToolType::Select: return "Select voxel region";
	}
	return "Voxel edit";
}


int resolvedLayerIndex(const VoxelToolSettings& settings, const VoxelEditorState& state)
{
	return settings.layer_index >= 0 ? settings.layer_index : state.active_layer;
}


size_t resolvedMaxVoxels(const VoxelToolSettings& settings)
{
	return std::max<size_t>(1, std::min<size_t>(settings.max_voxels, 1000000));
}


bool mirrorCoordinate(const int value, const int origin, int& result)
{
	const int64_t mirrored = static_cast<int64_t>(origin) * 2 - value;
	if(mirrored < std::numeric_limits<int>::min() || mirrored > std::numeric_limits<int>::max())
		return false;
	result = static_cast<int>(mirrored);
	return true;
}


void addPointAndMirrors(const Coord& point, const VoxelToolSettings& settings, const size_t max_points,
	std::set<Coord>& points, bool& truncated)
{
	std::vector<Coord> variants(1, point);
	const bool enabled[3] = { settings.mirror_x, settings.mirror_y, settings.mirror_z };
	const int origins[3] = { settings.mirror_origin.x, settings.mirror_origin.y, settings.mirror_origin.z };
	for(int axis=0; axis<3; ++axis)
	{
		if(!enabled[axis])
			continue;
		const size_t old_size = variants.size();
		for(size_t i=0; i<old_size; ++i)
		{
			Coord mirrored = variants[i];
			int* component = axis == 0 ? &mirrored.x : (axis == 1 ? &mirrored.y : &mirrored.z);
			if(mirrorCoordinate(*component, origins[axis], *component))
				variants.push_back(mirrored);
		}
	}

	for(const Coord& variant : variants)
	{
		if(!validVoxelCoord(variant))
			continue;
		if(points.find(variant) != points.end())
			continue;
		if(points.size() >= max_points)
		{
			truncated = true;
			return;
		}
		points.insert(variant);
	}
}


bool brushSphereContains(const int dx, const int dy, const int dz, const int low, const int high)
{
	const double centre = (static_cast<double>(low) + high) * 0.5;
	const double radius = (high - low + 1) * 0.5;
	const double x = (dx - centre) / radius;
	const double y = (dy - centre) / radius;
	const double z = (dz - centre) / radius;
	return x*x + y*y + z*z <= 1.0 + 1.0e-12;
}


bool hollowBrushSphereCell(const int dx, const int dy, const int dz, const int low, const int high)
{
	static const int offsets[6][3] = {{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
	for(const auto& offset : offsets)
		if(!brushSphereContains(dx + offset[0], dy + offset[1], dz + offset[2], low, high))
			return true;
	return false;
}


void buildStampPoints(const VoxelToolInput& input, const VoxelToolSettings& settings, const size_t max_points,
	std::set<Coord>& points, bool& truncated)
{
	const int size = std::max(1, std::min(16, settings.brush_size));
	const int low = -(size - 1) / 2;
	const int high = size / 2;
	for(int dx=low; dx<=high && !truncated; ++dx)
		for(int dy=low; dy<=high && !truncated; ++dy)
			for(int dz=low; dz<=high && !truncated; ++dz)
			{
				if(settings.brush_shape == VoxelBrushShape::Sphere)
				{
					if(!brushSphereContains(dx, dy, dz, low, high))
						continue;
					if(settings.hollow && !hollowBrushSphereCell(dx, dy, dz, low, high))
						continue;
				}
				else if(settings.hollow && dx != low && dx != high && dy != low && dy != high && dz != low && dz != high)
					continue;

				const int64_t x = static_cast<int64_t>(input.start.x) + dx;
				const int64_t y = static_cast<int64_t>(input.start.y) + dy;
				const int64_t z = static_cast<int64_t>(input.start.z) + dz;
				if(x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
					y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max() ||
					z < std::numeric_limits<int>::min() || z > std::numeric_limits<int>::max())
					continue;
				addPointAndMirrors(Coord(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)), settings, max_points, points, truncated);
			}
}


void buildBoxPoints(const VoxelToolInput& input, const VoxelToolSettings& settings, const size_t max_points,
	std::set<Coord>& points, bool& truncated)
{
	const int min_x = std::min(input.start.x, input.end.x), max_x = std::max(input.start.x, input.end.x);
	const int min_y = std::min(input.start.y, input.end.y), max_y = std::max(input.start.y, input.end.y);
	const int min_z = std::min(input.start.z, input.end.z), max_z = std::max(input.start.z, input.end.z);
	for(int64_t x=min_x; x<=static_cast<int64_t>(max_x) && !truncated; ++x)
		for(int64_t y=min_y; y<=static_cast<int64_t>(max_y) && !truncated; ++y)
			for(int64_t z=min_z; z<=static_cast<int64_t>(max_z) && !truncated; ++z)
			{
				if(settings.hollow && x != min_x && x != max_x && y != min_y && y != max_y && z != min_z && z != max_z)
					continue;
				addPointAndMirrors(Coord(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)), settings, max_points, points, truncated);
			}
}


bool ellipsoidContains(const int x, const int y, const int z, const int min_x, const int max_x,
	const int min_y, const int max_y, const int min_z, const int max_z)
{
	const double centre_x = (static_cast<double>(min_x) + max_x) * 0.5;
	const double centre_y = (static_cast<double>(min_y) + max_y) * 0.5;
	const double centre_z = (static_cast<double>(min_z) + max_z) * 0.5;
	const double radius_x = std::max(0.5, (static_cast<double>(max_x) - min_x + 1.0) * 0.5);
	const double radius_y = std::max(0.5, (static_cast<double>(max_y) - min_y + 1.0) * 0.5);
	const double radius_z = std::max(0.5, (static_cast<double>(max_z) - min_z + 1.0) * 0.5);
	const double dx = (x - centre_x) / radius_x;
	const double dy = (y - centre_y) / radius_y;
	const double dz = (z - centre_z) / radius_z;
	return dx*dx + dy*dy + dz*dz <= 1.0 + 1.0e-12;
}


bool hollowEllipsoidCell(const int x, const int y, const int z, const int min_x, const int max_x,
	const int min_y, const int max_y, const int min_z, const int max_z)
{
	static const int offsets[6][3] = {{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
	for(const auto& offset : offsets)
		if(!ellipsoidContains(x + offset[0], y + offset[1], z + offset[2], min_x, max_x, min_y, max_y, min_z, max_z))
			return true;
	return false;
}


void buildSpherePoints(const VoxelToolInput& input, const VoxelToolSettings& settings, const size_t max_points,
	std::set<Coord>& points, bool& truncated)
{
	const int min_x = std::min(input.start.x, input.end.x), max_x = std::max(input.start.x, input.end.x);
	const int min_y = std::min(input.start.y, input.end.y), max_y = std::max(input.start.y, input.end.y);
	const int min_z = std::min(input.start.z, input.end.z), max_z = std::max(input.start.z, input.end.z);
	for(int64_t x=min_x; x<=static_cast<int64_t>(max_x) && !truncated; ++x)
		for(int64_t y=min_y; y<=static_cast<int64_t>(max_y) && !truncated; ++y)
			for(int64_t z=min_z; z<=static_cast<int64_t>(max_z) && !truncated; ++z)
			{
				if(!ellipsoidContains(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), min_x, max_x, min_y, max_y, min_z, max_z))
					continue;
				if(settings.hollow && !hollowEllipsoidCell(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), min_x, max_x, min_y, max_y, min_z, max_z))
					continue;
				addPointAndMirrors(Coord(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)), settings, max_points, points, truncated);
			}
}


int interpolateLineComponent(const int start, const int end, const int64_t step, const int64_t steps)
{
	if(steps == 0)
		return start;
	const int64_t delta = static_cast<int64_t>(end) - start;
	const int64_t numerator = delta * step;
	const int64_t rounded = numerator >= 0 ? (numerator + steps / 2) / steps : (numerator - steps / 2) / steps;
	return static_cast<int>(static_cast<int64_t>(start) + rounded);
}


void buildLinePoints(const VoxelToolInput& input, const VoxelToolSettings& settings, const size_t max_points,
	std::set<Coord>& points, bool& truncated)
{
	const int64_t dx = std::abs(static_cast<int64_t>(input.end.x) - input.start.x);
	const int64_t dy = std::abs(static_cast<int64_t>(input.end.y) - input.start.y);
	const int64_t dz = std::abs(static_cast<int64_t>(input.end.z) - input.start.z);
	const int64_t steps = std::max(dx, std::max(dy, dz));
	for(int64_t step=0; step<=steps && !truncated; ++step)
	{
		const Vec3<int> centre(
			interpolateLineComponent(input.start.x, input.end.x, step, steps),
			interpolateLineComponent(input.start.y, input.end.y, step, steps),
			interpolateLineComponent(input.start.z, input.end.z, step, steps));
		buildStampPoints(VoxelToolInput(centre), settings, max_points, points, truncated);
	}
}


bool validSelectionBounds(const VoxelSelectionBounds& bounds)
{
	return bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z &&
		validVoxelCoord(Coord(bounds.min)) && validVoxelCoord(Coord(bounds.max));
}


void setLimitExceeded(VoxelToolResult& result, const char* operation)
{
	result.command = VoxelEditCommand(operation);
	result.truncated = true;
	result.error = std::string(operation) + " exceeded the voxel safety limit; no voxels were changed.";
}


bool buildConnectedFill(const VoxelToolInput& input, const VoxelToolSettings& settings,
	const VoxelEditorState& state, const int layer_index, const MaterialMap& materials,
	VoxelToolResult& result)
{
	const Coord seed(input.start);
	if(!validVoxelCoord(seed))
	{
		result.error = "The fill seed is outside the supported voxel coordinate range.";
		return false;
	}

	const int source_material = materialAt(materials, seed);
	if(!validMaterial(source_material) || !VoxelEditorData::layerOwnsMaterial(state, layer_index, source_material))
	{
		result.error = "Connected fill requires an existing voxel in the active layer.";
		return false;
	}
	if(source_material == settings.material_index)
		return true;

	const size_t max_voxels = resolvedMaxVoxels(settings);
	std::vector<Coord> region;
	region.reserve(std::min(max_voxels, materials.size()));
	std::unordered_set<Coord, CoordHash> visited;
	visited.reserve(std::min(max_voxels, materials.size()));
	region.push_back(seed);
	visited.insert(seed);

	static const int neighbour_offsets[6][3] = {
		{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
	};
	for(size_t read_index=0; read_index<region.size(); ++read_index)
	{
		const Coord current = region[read_index];
		for(const auto& offset : neighbour_offsets)
		{
			const Coord neighbour(current.x + offset[0], current.y + offset[1], current.z + offset[2]);
			if(!validVoxelCoord(neighbour) || visited.find(neighbour) != visited.end())
				continue;
			if(materialAt(materials, neighbour) != source_material)
				continue;
			if(region.size() >= max_voxels)
			{
				setLimitExceeded(result, "Connected fill");
				return false;
			}
			visited.insert(neighbour);
			region.push_back(neighbour);
		}
	}

	std::sort(region.begin(), region.end());
	for(const Coord& coord : region)
		result.command.recordChange(coord.toVec3(), source_material, settings.material_index, layer_index);
	result.command.compact();
	return true;
}

} // namespace


VoxelSelectionBounds::VoxelSelectionBounds(const Vec3<int>& first, const Vec3<int>& second)
:
	min(std::min(first.x, second.x), std::min(first.y, second.y), std::min(first.z, second.z)),
	max(std::max(first.x, second.x), std::max(first.y, second.y), std::max(first.z, second.z))
{}


bool VoxelSelectionBounds::contains(const Vec3<int>& coord) const
{
	return coord.x >= min.x && coord.x <= max.x &&
		coord.y >= min.y && coord.y <= max.y &&
		coord.z >= min.z && coord.z <= max.z;
}


Vec3<int> VoxelSelectionBounds::extent() const
{
	auto component_extent = [](const int low, const int high)
	{
		const int64_t value = static_cast<int64_t>(high) - low + 1;
		return static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(value, std::numeric_limits<int>::max())));
	};
	return Vec3<int>(component_extent(min.x, max.x), component_extent(min.y, max.y), component_extent(min.z, max.z));
}


void VoxelClipboard::clear()
{
	extent = Vec3<int>(0, 0, 0);
	source_layer_index = -1;
	voxels.clear();
}


void VoxelEditCommand::recordChange(const Vec3<int>& coord, const int before_material_, const int after_material_, const int layer_index)
{
	const int before_material = sanitiseMaterial(before_material_);
	const int after_material = sanitiseMaterial(after_material_);
	const CoordKey key(coord.x, coord.y, coord.z);
	const auto it = change_index.find(key);
	if(it == change_index.end())
	{
		VoxelChange change;
		change.coord = coord;
		change.before_material = before_material;
		change.after_material = after_material;
		change.layer_index = layer_index;
		change_index[key] = changes.size();
		changes.push_back(change);
	}
	else
	{
		VoxelChange& change = changes[it->second];
		change.after_material = after_material;
	}
}


void VoxelEditCommand::rebuildIndex()
{
	change_index.clear();
	for(size_t i=0; i<changes.size(); ++i)
		change_index[CoordKey(changes[i].coord.x, changes[i].coord.y, changes[i].coord.z)] = i;
}


void VoxelEditCommand::append(const VoxelEditCommand& next)
{
	for(const VoxelChange& change : next.changes)
		recordChange(change.coord, change.before_material, change.after_material, change.layer_index);
	compact();
}


void VoxelEditCommand::compact()
{
	std::vector<VoxelChange> compacted;
	compacted.reserve(changes.size());
	for(const VoxelChange& change : changes)
		if(change.before_material != change.after_material)
			compacted.push_back(change);
	changes.swap(compacted);
	rebuildIndex();
}


void VoxelEditCommand::apply(glare::AllocatorVector<Voxel, 16>& voxels, const bool use_after) const
{
	struct Target
	{
		int material = VoxelChange::NO_MATERIAL;
		bool handled = false;
	};
	std::unordered_map<Coord, Target, CoordHash> targets;
	targets.reserve(changes.size());
	for(const VoxelChange& change : changes)
		targets[Coord(change.coord)] = Target{use_after ? change.after_material : change.before_material, false};

	size_t write_index = 0;
	for(size_t read_index=0; read_index<voxels.size(); ++read_index)
	{
		const Coord coord(voxels[read_index].pos);
		auto target_it = targets.find(coord);
		if(target_it == targets.end())
		{
			if(write_index != read_index)
				voxels[write_index] = voxels[read_index];
			++write_index;
			continue;
		}

		Target& target = target_it->second;
		if(!target.handled)
		{
			target.handled = true;
			if(validMaterial(target.material))
				voxels[write_index++] = Voxel(coord.toVec3(), target.material);
		}
		// Any additional source voxel at this coordinate is a legacy duplicate
		// and is deliberately discarded by the edited coordinate.
	}
	voxels.resize(write_index);

	// Preserve command order for deterministic sparse-list output.
	for(const VoxelChange& change : changes)
	{
		auto target_it = targets.find(Coord(change.coord));
		if(target_it != targets.end() && !target_it->second.handled && validMaterial(target_it->second.material))
		{
			voxels.push_back(Voxel(change.coord, target_it->second.material));
			target_it->second.handled = true;
		}
	}
}


void VoxelEditCommand::redo(glare::AllocatorVector<Voxel, 16>& voxels) const { apply(voxels, true); }
void VoxelEditCommand::undo(glare::AllocatorVector<Voxel, 16>& voxels) const { apply(voxels, false); }
void VoxelEditCommand::redo(WorldObject& object) const { redo(object.getDecompressedVoxels()); }
void VoxelEditCommand::undo(WorldObject& object) const { undo(object.getDecompressedVoxels()); }


namespace VoxelTools
{

int pickMaterial(const glare::AllocatorVector<Voxel, 16>& voxels, const Vec3<int>& coord,
	const VoxelEditorState& state, const bool active_layer_only)
{
	for(size_t i=voxels.size(); i>0; --i)
	{
		const Voxel& voxel = voxels[i - 1];
		if(voxel.pos != coord || !validMaterial(voxel.mat_index))
			continue;
		const int layer_index = VoxelEditorData::materialLayerIndex(state, voxel.mat_index);
		if(active_layer_only && layer_index != state.active_layer)
			continue;
		if(layer_index >= 0 && layer_index < static_cast<int>(state.layers.size()) && !state.layers[layer_index].visible)
			continue;
		return voxel.mat_index;
	}
	return VoxelChange::NO_MATERIAL;
}


VoxelToolResult buildCommand(const VoxelToolType tool, const VoxelToolInput& input,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	const glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result;
	result.command.name = commandName(tool);
	if(tool == VoxelToolType::Picker)
	{
		result.picked_material_index = pickMaterial(voxels, input.start, state, false);
		return result;
	}
	if(tool == VoxelToolType::Select)
		return result; // Selection state is owned by the UI; voxel data is unchanged.

	const int layer_index = resolvedLayerIndex(settings, state);
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()))
	{
		result.error = "No valid active voxel layer.";
		return result;
	}
	if(state.layers[layer_index].locked)
	{
		result.error = "The active voxel layer is locked.";
		return result;
	}
	if(tool != VoxelToolType::Eraser &&
		(!validMaterial(settings.material_index) || !VoxelEditorData::layerOwnsMaterial(state, layer_index, settings.material_index)))
	{
		result.error = "The selected material is not assigned to the active voxel layer.";
		return result;
	}

	const size_t max_points = resolvedMaxVoxels(settings);
	MaterialMap materials = makeMaterialMap(voxels);
	if(tool == VoxelToolType::Fill)
	{
		buildConnectedFill(input, settings, state, layer_index, materials, result);
		return result;
	}
	if(tool == VoxelToolType::Line &&
		(!validVoxelCoord(Coord(input.start)) || !validVoxelCoord(Coord(input.end))))
	{
		result.error = "The voxel line endpoints are outside the supported coordinate range.";
		return result;
	}

	std::set<Coord> points;
	if(tool == VoxelToolType::Box)
		buildBoxPoints(input, settings, max_points, points, result.truncated);
	else if(tool == VoxelToolType::Sphere)
		buildSpherePoints(input, settings, max_points, points, result.truncated);
	else if(tool == VoxelToolType::Line)
		buildLinePoints(input, settings, max_points, points, result.truncated);
	else
		buildStampPoints(input, settings, max_points, points, result.truncated);

	for(const Coord& point : points)
	{
		const int before = materialAt(materials, point);
		const bool occupied = validMaterial(before);
		const bool belongs_to_active_layer = occupied && VoxelEditorData::layerOwnsMaterial(state, layer_index, before);
		int after = before;

		if(tool == VoxelToolType::Eraser)
		{
			if(belongs_to_active_layer)
				after = VoxelChange::NO_MATERIAL;
		}
		else if(tool == VoxelToolType::Paint || settings.brush_mode == VoxelBrushMode::Paint)
		{
			if(belongs_to_active_layer)
				after = settings.material_index;
		}
		else if(settings.brush_mode == VoxelBrushMode::Replace)
		{
			if(!occupied || belongs_to_active_layer)
				after = settings.material_index;
		}
		else // Add: never overwrite any layer.
		{
			if(!occupied)
				after = settings.material_index;
		}

		if(before != after)
		{
			result.command.recordChange(point.toVec3(), before, after, layer_index);
			if(validMaterial(after))
				materials[point] = after;
			else
				materials.erase(point);
		}
	}
	result.command.compact();
	return result;
}


VoxelToolResult execute(const VoxelToolType tool, const VoxelToolInput& input,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result = buildCommand(tool, input, settings, state, voxels);
	if(!result.command.empty())
		result.command.redo(voxels);
	return result;
}


VoxelClipboardResult copySelection(const VoxelSelectionBounds& bounds,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	const glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelClipboardResult result;
	if(!validSelectionBounds(bounds))
	{
		result.error = "The voxel selection bounds are outside the supported coordinate range.";
		return result;
	}

	const int layer_index = resolvedLayerIndex(settings, state);
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()))
	{
		result.error = "No valid voxel layer is selected for copying.";
		return result;
	}

	const size_t max_voxels = resolvedMaxVoxels(settings);
	std::vector<std::pair<Coord, int>> selected;
	selected.reserve(std::min(max_voxels, voxels.size()));
	const MaterialMap materials = makeMaterialMap(voxels);
	for(const auto& entry : materials)
	{
		if(!bounds.contains(entry.first.toVec3()) ||
			!VoxelEditorData::layerOwnsMaterial(state, layer_index, entry.second))
			continue;
		if(selected.size() >= max_voxels)
		{
			result.truncated = true;
			result.error = "Copy selection exceeded the voxel safety limit; the clipboard was left empty.";
			return result;
		}
		selected.push_back(entry);
	}
	std::sort(selected.begin(), selected.end(), [](const std::pair<Coord, int>& a, const std::pair<Coord, int>& b)
	{
		return a.first < b.first;
	});

	result.clipboard.extent = bounds.extent();
	result.clipboard.source_layer_index = layer_index;
	result.clipboard.voxels.reserve(selected.size());
	for(const auto& entry : selected)
	{
		VoxelClipboardVoxel voxel;
		voxel.offset = Vec3<int>(entry.first.x - bounds.min.x, entry.first.y - bounds.min.y, entry.first.z - bounds.min.z);
		voxel.material_index = entry.second;
		result.clipboard.voxels.push_back(voxel);
	}
	return result;
}


VoxelToolResult buildDeleteSelectionCommand(const VoxelSelectionBounds& bounds,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	const glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result;
	result.command.name = "Delete voxel selection";
	if(!validSelectionBounds(bounds))
	{
		result.error = "The voxel selection bounds are outside the supported coordinate range.";
		return result;
	}

	const int layer_index = resolvedLayerIndex(settings, state);
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()))
	{
		result.error = "No valid active voxel layer.";
		return result;
	}
	if(state.layers[layer_index].locked)
	{
		result.error = "The active voxel layer is locked.";
		return result;
	}

	const size_t max_voxels = resolvedMaxVoxels(settings);
	std::vector<std::pair<Coord, int>> selected;
	selected.reserve(std::min(max_voxels, voxels.size()));
	const MaterialMap materials = makeMaterialMap(voxels);
	for(const auto& entry : materials)
	{
		if(!bounds.contains(entry.first.toVec3()) ||
			!VoxelEditorData::layerOwnsMaterial(state, layer_index, entry.second))
			continue;
		if(selected.size() >= max_voxels)
		{
			setLimitExceeded(result, "Delete selection");
			return result;
		}
		selected.push_back(entry);
	}
	std::sort(selected.begin(), selected.end(), [](const std::pair<Coord, int>& a, const std::pair<Coord, int>& b)
	{
		return a.first < b.first;
	});
	for(const auto& entry : selected)
		result.command.recordChange(entry.first.toVec3(), entry.second, VoxelChange::NO_MATERIAL, layer_index);
	result.command.compact();
	return result;
}


VoxelToolResult deleteSelection(const VoxelSelectionBounds& bounds,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result = buildDeleteSelectionCommand(bounds, settings, state, voxels);
	if(result.changed())
		result.command.redo(voxels);
	return result;
}


VoxelToolResult buildPasteCommand(const VoxelClipboard& clipboard, const Vec3<int>& destination,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	const glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result;
	result.command.name = "Paste voxel selection";
	const int layer_index = resolvedLayerIndex(settings, state);
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()))
	{
		result.error = "No valid active voxel layer.";
		return result;
	}
	if(state.layers[layer_index].locked)
	{
		result.error = "The active voxel layer is locked.";
		return result;
	}
	if(clipboard.empty())
		return result;

	const size_t max_voxels = resolvedMaxVoxels(settings);
	if(clipboard.voxels.size() > max_voxels)
	{
		setLimitExceeded(result, "Paste selection");
		return result;
	}
	if(clipboard.extent.x <= 0 || clipboard.extent.y <= 0 || clipboard.extent.z <= 0)
	{
		result.error = "The voxel clipboard has invalid extents.";
		return result;
	}

	std::map<Coord, int> targets;
	for(const VoxelClipboardVoxel& clipboard_voxel : clipboard.voxels)
	{
		if(clipboard_voxel.offset.x < 0 || clipboard_voxel.offset.x >= clipboard.extent.x ||
			clipboard_voxel.offset.y < 0 || clipboard_voxel.offset.y >= clipboard.extent.y ||
			clipboard_voxel.offset.z < 0 || clipboard_voxel.offset.z >= clipboard.extent.z)
		{
			result.error = "The voxel clipboard contains an offset outside its extents.";
			return result;
		}

		const int64_t target_x = static_cast<int64_t>(destination.x) + clipboard_voxel.offset.x;
		const int64_t target_y = static_cast<int64_t>(destination.y) + clipboard_voxel.offset.y;
		const int64_t target_z = static_cast<int64_t>(destination.z) + clipboard_voxel.offset.z;
		if(target_x < std::numeric_limits<int>::min() || target_x > std::numeric_limits<int>::max() ||
			target_y < std::numeric_limits<int>::min() || target_y > std::numeric_limits<int>::max() ||
			target_z < std::numeric_limits<int>::min() || target_z > std::numeric_limits<int>::max())
		{
			result.error = "The paste destination overflows voxel coordinates.";
			return result;
		}
		const Coord target(static_cast<int>(target_x), static_cast<int>(target_y), static_cast<int>(target_z));
		if(!validVoxelCoord(target))
		{
			result.error = "The paste destination is outside the supported voxel coordinate range.";
			return result;
		}

		int material = clipboard_voxel.material_index;
		if(!validMaterial(material) || !VoxelEditorData::layerOwnsMaterial(state, layer_index, material))
		{
			if(!validMaterial(settings.material_index) ||
				!VoxelEditorData::layerOwnsMaterial(state, layer_index, settings.material_index))
			{
				result.error = "The clipboard material is unavailable in the destination layer and no valid fallback material is selected.";
				return result;
			}
			material = settings.material_index;
		}
		targets[target] = material;
	}

	const MaterialMap materials = makeMaterialMap(voxels);
	for(const auto& target : targets)
	{
		const int before = materialAt(materials, target.first);
		const bool occupied = validMaterial(before);
		const bool belongs_to_active_layer = occupied && VoxelEditorData::layerOwnsMaterial(state, layer_index, before);
		int after = before;
		if(settings.brush_mode == VoxelBrushMode::Paint)
		{
			if(belongs_to_active_layer)
				after = target.second;
		}
		else if(settings.brush_mode == VoxelBrushMode::Replace)
		{
			if(!occupied || belongs_to_active_layer)
				after = target.second;
		}
		else if(!occupied) // Add never overwrites this or another layer.
			after = target.second;

		if(before != after)
			result.command.recordChange(target.first.toVec3(), before, after, layer_index);
	}
	result.command.compact();
	return result;
}


VoxelToolResult pasteSelection(const VoxelClipboard& clipboard, const Vec3<int>& destination,
	const VoxelToolSettings& settings, const VoxelEditorState& state,
	glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result = buildPasteCommand(clipboard, destination, settings, state, voxels);
	if(result.changed())
		result.command.redo(voxels);
	return result;
}


VoxelToolResult buildDuplicateSelectionCommand(const VoxelSelectionBounds& bounds,
	const Vec3<int>& destination, const VoxelToolSettings& settings,
	const VoxelEditorState& state, const glare::AllocatorVector<Voxel, 16>& voxels)
{
	const VoxelClipboardResult copied = copySelection(bounds, settings, state, voxels);
	if(!copied.succeeded())
	{
		VoxelToolResult result;
		result.command.name = "Duplicate voxel selection";
		result.truncated = copied.truncated;
		result.error = copied.error;
		return result;
	}
	VoxelToolResult result = buildPasteCommand(copied.clipboard, destination, settings, state, voxels);
	result.command.name = "Duplicate voxel selection";
	return result;
}


VoxelToolResult duplicateSelection(const VoxelSelectionBounds& bounds,
	const Vec3<int>& destination, const VoxelToolSettings& settings,
	const VoxelEditorState& state, glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result = buildDuplicateSelectionCommand(bounds, destination, settings, state, voxels);
	if(result.changed())
		result.command.redo(voxels);
	return result;
}


VoxelToolResult buildMoveSelectionCommand(const VoxelSelectionBounds& bounds,
	const Vec3<int>& destination, const VoxelToolSettings& settings,
	const VoxelEditorState& state, const glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result;
	result.command.name = "Move voxel selection";
	const VoxelClipboardResult copied = copySelection(bounds, settings, state, voxels);
	if(!copied.succeeded())
	{
		result.truncated = copied.truncated;
		result.error = copied.error;
		return result;
	}
	if(copied.clipboard.empty())
		return result;

	VoxelToolResult deleted = buildDeleteSelectionCommand(bounds, settings, state, voxels);
	if(!deleted.error.empty())
		return deleted;

	// Paste must evaluate collisions after the source has been removed.  This is
	// a transient working copy only; undo still stores coordinate deltas, never a
	// full object snapshot.
	glare::AllocatorVector<Voxel, 16> after_delete(voxels);
	deleted.command.redo(after_delete);
	VoxelToolResult pasted = buildPasteCommand(copied.clipboard, destination, settings, state, after_delete);
	if(!pasted.error.empty())
		return pasted;
	if(settings.brush_mode == VoxelBrushMode::Paint)
	{
		result.error = "Move selection does not support Paint collision mode; use Add or Replace.";
		return result;
	}

	const int layer_index = resolvedLayerIndex(settings, state);
	const MaterialMap after_delete_materials = makeMaterialMap(after_delete);
	for(const VoxelClipboardVoxel& clipboard_voxel : copied.clipboard.voxels)
	{
		const Coord target(destination.x + clipboard_voxel.offset.x,
			destination.y + clipboard_voxel.offset.y, destination.z + clipboard_voxel.offset.z);
		const int occupant = materialAt(after_delete_materials, target);
		if(!validMaterial(occupant))
			continue;
		if(!VoxelEditorData::layerOwnsMaterial(state, layer_index, occupant))
		{
			result.error = "Move selection would overwrite a voxel owned by another layer; no voxels were changed.";
			return result;
		}
		if(settings.brush_mode == VoxelBrushMode::Add)
		{
			result.error = "Move selection destination is occupied; choose Replace or another destination.";
			return result;
		}
	}

	result.command.append(deleted.command);
	result.command.append(pasted.command);
	result.command.name = "Move voxel selection";
	result.truncated = deleted.truncated || pasted.truncated;
	return result;
}


VoxelToolResult moveSelection(const VoxelSelectionBounds& bounds,
	const Vec3<int>& destination, const VoxelToolSettings& settings,
	const VoxelEditorState& state, glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelToolResult result = buildMoveSelectionCommand(bounds, destination, settings, state, voxels);
	if(result.changed())
		result.command.redo(voxels);
	return result;
}


bool runSelfTest(std::string* details_out)
{
	auto fail = [details_out](const char* message)
	{
		if(details_out)
			*details_out = message;
		return false;
	};

	VoxelEditorState state = VoxelEditorData::defaultForMaterialCount(2);
	glare::AllocatorVector<Voxel, 16> voxels;
	voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

	VoxelToolSettings settings;
	settings.material_index = 1;
	VoxelToolResult add = execute(VoxelToolType::Brush, VoxelToolInput(Vec3<int>(1, 0, 0)), settings, state, voxels);
	if(!add.changed() || voxels.size() != 2 || pickMaterial(voxels, Vec3<int>(1, 0, 0), state) != 1)
		return fail("Brush add failed.");
	add.command.undo(voxels);
	if(voxels.size() != 1)
		return fail("Brush command undo failed.");
	add.command.redo(voxels);

	settings.brush_mode = VoxelBrushMode::Paint;
	VoxelToolResult paint = execute(VoxelToolType::Paint, VoxelToolInput(Vec3<int>(0, 0, 0)), settings, state, voxels);
	if(!paint.changed() || pickMaterial(voxels, Vec3<int>(0, 0, 0), state) != 1)
		return fail("Paint failed.");

	settings.brush_mode = VoxelBrushMode::Add;
	settings.mirror_x = true;
	VoxelToolResult mirrored = execute(VoxelToolType::Brush, VoxelToolInput(Vec3<int>(2, 0, 0)), settings, state, voxels);
	if(mirrored.command.changedCount() != 2 || pickMaterial(voxels, Vec3<int>(-2, 0, 0), state) != 1)
		return fail("Mirrored brush failed.");

	settings = VoxelToolSettings();
	settings.material_index = 1;
	glare::AllocatorVector<Voxel, 16> line_voxels;
	VoxelToolResult line = execute(VoxelToolType::Line,
		VoxelToolInput(Vec3<int>(0, 0, 0), Vec3<int>(3, 2, 1)), settings, state, line_voxels);
	if(line.error.size() != 0 || line.command.changedCount() != 4 ||
		pickMaterial(line_voxels, Vec3<int>(0, 0, 0), state) != 1 ||
		pickMaterial(line_voxels, Vec3<int>(3, 2, 1), state) != 1)
		return fail("3D voxel line failed.");

	glare::AllocatorVector<Voxel, 16> fill_voxels;
	fill_voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	fill_voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));
	fill_voxels.push_back(Voxel(Vec3<int>(1, 1, 0), 0));
	fill_voxels.push_back(Voxel(Vec3<int>(4, 0, 0), 0));
	settings.max_voxels = 16;
	VoxelToolResult fill = execute(VoxelToolType::Fill, VoxelToolInput(Vec3<int>(0, 0, 0)), settings, state, fill_voxels);
	if(fill.error.size() != 0 || fill.command.changedCount() != 3 ||
		pickMaterial(fill_voxels, Vec3<int>(1, 1, 0), state) != 1 ||
		pickMaterial(fill_voxels, Vec3<int>(4, 0, 0), state) != 0)
		return fail("Bounded connected fill failed.");
	fill.command.undo(fill_voxels);
	if(pickMaterial(fill_voxels, Vec3<int>(1, 1, 0), state) != 0)
		return fail("Connected fill undo failed.");
	settings.max_voxels = 2;
	VoxelToolResult limited_fill = buildCommand(VoxelToolType::Fill, VoxelToolInput(Vec3<int>(0, 0, 0)), settings, state, fill_voxels);
	if(!limited_fill.truncated || limited_fill.error.empty() || limited_fill.changed())
		return fail("Connected fill safety limit was not atomic.");

	settings.max_voxels = 16;
	settings.material_index = 0;
	glare::AllocatorVector<Voxel, 16> selection_voxels;
	selection_voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	selection_voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 1));
	selection_voxels.push_back(Voxel(Vec3<int>(5, 0, 0), 0));
	const VoxelSelectionBounds bounds(Vec3<int>(0, 0, 0), Vec3<int>(1, 0, 0));
	const VoxelClipboardResult copied = copySelection(bounds, settings, state, selection_voxels);
	if(!copied.succeeded() || copied.clipboard.voxelCount() != 2 ||
		copied.clipboard.extent.x != 2 || copied.clipboard.extent.y != 1 || copied.clipboard.extent.z != 1)
		return fail("Voxel selection copy failed.");
	VoxelToolResult deleted = deleteSelection(bounds, settings, state, selection_voxels);
	if(deleted.command.changedCount() != 2 || pickMaterial(selection_voxels, Vec3<int>(0, 0, 0), state) != VoxelChange::NO_MATERIAL)
		return fail("Voxel selection delete failed.");
	deleted.command.undo(selection_voxels);
	if(pickMaterial(selection_voxels, Vec3<int>(0, 0, 0), state) != 0 ||
		pickMaterial(selection_voxels, Vec3<int>(1, 0, 0), state) != 1)
		return fail("Voxel selection delete undo failed.");
	VoxelToolResult pasted = pasteSelection(copied.clipboard, Vec3<int>(10, 0, 0), settings, state, selection_voxels);
	if(pasted.command.changedCount() != 2 || pickMaterial(selection_voxels, Vec3<int>(10, 0, 0), state) != 0 ||
		pickMaterial(selection_voxels, Vec3<int>(11, 0, 0), state) != 1)
		return fail("Voxel selection paste failed.");
	VoxelToolResult duplicated = duplicateSelection(bounds, Vec3<int>(20, 0, 0), settings, state, selection_voxels);
	if(duplicated.command.changedCount() != 2 || pickMaterial(selection_voxels, Vec3<int>(20, 0, 0), state) != 0 ||
		pickMaterial(selection_voxels, Vec3<int>(21, 0, 0), state) != 1)
		return fail("Voxel selection duplicate failed.");
	VoxelToolResult moved = moveSelection(bounds, Vec3<int>(30, 0, 0), settings, state, selection_voxels);
	if(moved.command.changedCount() != 4 || pickMaterial(selection_voxels, Vec3<int>(0, 0, 0), state) != VoxelChange::NO_MATERIAL ||
		pickMaterial(selection_voxels, Vec3<int>(1, 0, 0), state) != VoxelChange::NO_MATERIAL ||
		pickMaterial(selection_voxels, Vec3<int>(30, 0, 0), state) != 0 ||
		pickMaterial(selection_voxels, Vec3<int>(31, 0, 0), state) != 1)
		return fail("Atomic voxel selection move failed.");
	moved.command.undo(selection_voxels);
	if(pickMaterial(selection_voxels, Vec3<int>(0, 0, 0), state) != 0 ||
		pickMaterial(selection_voxels, Vec3<int>(1, 0, 0), state) != 1 ||
		pickMaterial(selection_voxels, Vec3<int>(30, 0, 0), state) != VoxelChange::NO_MATERIAL)
		return fail("Voxel selection move undo failed.");
	VoxelToolResult collided_move = buildMoveSelectionCommand(bounds, Vec3<int>(5, 0, 0), settings, state, selection_voxels);
	if(collided_move.error.empty() || collided_move.changed())
		return fail("Voxel selection move collision was not atomic.");
	VoxelToolResult select_noop = buildCommand(VoxelToolType::Select, VoxelToolInput(Vec3<int>(0, 0, 0)), settings, state, selection_voxels);
	if(!select_noop.error.empty() || select_noop.changed())
		return fail("UI-only voxel selection tool changed voxel data.");

	VoxelEditCommand deduplicated("Deduplication test");
	deduplicated.recordChange(Vec3<int>(8, 9, 10), VoxelChange::NO_MATERIAL, 0, 0);
	deduplicated.recordChange(Vec3<int>(8, 9, 10), 0, 1, 0);
	deduplicated.compact();
	if(deduplicated.changedCount() != 1 || deduplicated.changes[0].before_material != VoxelChange::NO_MATERIAL || deduplicated.changes[0].after_material != 1)
		return fail("Command delta deduplication failed.");

	state.layers[0].locked = true;
	VoxelToolResult locked = buildCommand(VoxelToolType::Eraser, VoxelToolInput(Vec3<int>(0, 0, 0)), settings, state, voxels);
	if(locked.error.empty() || locked.changed())
		return fail("Locked layer accepted an edit.");

	if(details_out)
		*details_out = "ok";
	return true;
}

} // namespace VoxelTools
