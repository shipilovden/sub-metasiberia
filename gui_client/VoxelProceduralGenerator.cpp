/*=====================================================================
VoxelProceduralGenerator.cpp
----------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelProceduralGenerator.h"


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <unordered_map>
#include <vector>


// Clean-room procedural geometry and deterministic noise.  This implementation
// does not contain or depend on Goxel source code.
namespace
{
static constexpr int MIN_VOXEL_COORD = -32768;
static constexpr int MAX_VOXEL_COORD = 32766;
static constexpr size_t MAX_CANDIDATE_SAMPLES = 8000000;


template <class T>
T clampValue(const T value, const T min_value, const T max_value)
{
	return std::max(min_value, std::min(max_value, value));
}


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


bool validVoxelCoord(const int64_t x, const int64_t y, const int64_t z)
{
	return x >= MIN_VOXEL_COORD && x <= MAX_VOXEL_COORD &&
		y >= MIN_VOXEL_COORD && y <= MAX_VOXEL_COORD &&
		z >= MIN_VOXEL_COORD && z <= MAX_VOXEL_COORD;
}


MaterialMap makeMaterialMap(const glare::AllocatorVector<Voxel, 16>& voxels)
{
	MaterialMap result;
	result.reserve(voxels.size());
	for(size_t i=0; i<voxels.size(); ++i)
		if(validMaterial(voxels[i].mat_index))
			result[Coord(voxels[i].pos)] = voxels[i].mat_index;
	return result;
}


int materialAt(const MaterialMap& materials, const Coord& coord)
{
	const auto it = materials.find(coord);
	return it == materials.end() ? VoxelChange::NO_MATERIAL : it->second;
}


uint32_t mix32(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352Du;
	value ^= value >> 15;
	value *= 0x846CA68Bu;
	value ^= value >> 16;
	return value;
}


uint32_t latticeHash(const int64_t x, const int64_t y, const int64_t z, const uint32_t seed)
{
	uint32_t value = mix32(seed ^ 0xA511E9B3u);
	value = mix32(value ^ static_cast<uint32_t>(x) ^ static_cast<uint32_t>(static_cast<uint64_t>(x) >> 32));
	value = mix32(value ^ static_cast<uint32_t>(y) ^ static_cast<uint32_t>(static_cast<uint64_t>(y) >> 32));
	return mix32(value ^ static_cast<uint32_t>(z) ^ static_cast<uint32_t>(static_cast<uint64_t>(z) >> 32));
}


double unitHash(const int64_t x, const int64_t y, const int64_t z, const uint32_t seed)
{
	return static_cast<double>(latticeHash(x, y, z, seed) & 0x00FFFFFFu) / 16777215.0;
}


double smoothStep(const double value)
{
	return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}


double lerp(const double a, const double b, const double t)
{
	return a + (b - a) * t;
}


double valueNoise(const double x, const double y, const double z, const uint32_t seed)
{
	const int64_t x0 = static_cast<int64_t>(std::floor(x));
	const int64_t y0 = static_cast<int64_t>(std::floor(y));
	const int64_t z0 = static_cast<int64_t>(std::floor(z));
	const double tx = smoothStep(x - static_cast<double>(x0));
	const double ty = smoothStep(y - static_cast<double>(y0));
	const double tz = smoothStep(z - static_cast<double>(z0));

	const double n000 = unitHash(x0,     y0,     z0,     seed);
	const double n100 = unitHash(x0 + 1, y0,     z0,     seed);
	const double n010 = unitHash(x0,     y0 + 1, z0,     seed);
	const double n110 = unitHash(x0 + 1, y0 + 1, z0,     seed);
	const double n001 = unitHash(x0,     y0,     z0 + 1, seed);
	const double n101 = unitHash(x0 + 1, y0,     z0 + 1, seed);
	const double n011 = unitHash(x0,     y0 + 1, z0 + 1, seed);
	const double n111 = unitHash(x0 + 1, y0 + 1, z0 + 1, seed);

	const double nx00 = lerp(n000, n100, tx);
	const double nx10 = lerp(n010, n110, tx);
	const double nx01 = lerp(n001, n101, tx);
	const double nx11 = lerp(n011, n111, tx);
	return lerp(lerp(nx00, nx10, ty), lerp(nx01, nx11, ty), tz);
}


double fractalNoise(const double x, const double y, const double z, const VoxelProceduralParams& params)
{
	double frequency = params.noise_scale;
	double amplitude = 1.0;
	double sum = 0.0;
	double weight = 0.0;
	for(int octave=0; octave<params.octaves; ++octave)
	{
		sum += valueNoise(x * frequency, y * frequency, z * frequency,
			params.seed + static_cast<uint32_t>(octave) * 0x9E3779B9u) * amplitude;
		weight += amplitude;
		frequency *= 2.0;
		amplitude *= 0.5;
	}
	return weight > 0.0 ? sum / weight : 0.5;
}


class PointCollector
{
public:
	explicit PointCollector(const size_t max_points_)
	: max_points(max_points_), sample_limit(std::min(MAX_CANDIDATE_SAMPLES,
		std::max<size_t>(262144, max_points_ * 16)))
	{}

	bool beginSample()
	{
		if(stopped_)
			return false;
		if(samples >= sample_limit)
		{
			truncated_ = true;
			stopped_ = true;
			return false;
		}
		++samples;
		return true;
	}

	void add(const int64_t x, const int64_t y, const int64_t z)
	{
		if(!validVoxelCoord(x, y, z))
		{
			truncated_ = true;
			return;
		}
		const Coord point(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
		if(points.find(point) != points.end())
			return;
		if(points.size() >= max_points)
		{
			truncated_ = true;
			stopped_ = true;
			return;
		}
		points.insert(point);
	}

	bool stopped() const { return stopped_; }
	bool truncated() const { return truncated_; }

	std::set<Coord> points;

private:
	size_t max_points;
	size_t sample_limit;
	size_t samples = 0;
	bool stopped_ = false;
	bool truncated_ = false;
};


bool boxShellCell(const int x, const int y, const int z, const VoxelProceduralParams& params)
{
	if(!params.hollow)
		return true;
	const int distance_to_face = std::min(
		std::min(std::min(x, params.size_x - 1 - x), std::min(y, params.size_y - 1 - y)),
		std::min(z, params.size_z - 1 - z));
	return distance_to_face < params.wall_thickness;
}


bool ellipsoidContains(const int x, const int y, const int z, const VoxelProceduralParams& params, const int inset)
{
	const int size_x = params.size_x - inset * 2;
	const int size_y = params.size_y - inset * 2;
	const int size_z = params.size_z - inset * 2;
	if(size_x <= 0 || size_y <= 0 || size_z <= 0)
		return false;

	const double centre_x = (params.size_x - 1) * 0.5;
	const double centre_y = (params.size_y - 1) * 0.5;
	const double centre_z = (params.size_z - 1) * 0.5;
	const double radius_x = std::max(0.5, size_x * 0.5);
	const double radius_y = std::max(0.5, size_y * 0.5);
	const double radius_z = std::max(0.5, size_z * 0.5);
	const double dx = (x - centre_x) / radius_x;
	const double dy = (y - centre_y) / radius_y;
	const double dz = (z - centre_z) / radius_z;
	return dx*dx + dy*dy + dz*dz <= 1.0 + 1.0e-12;
}


void makeIrregularShell(std::set<Coord>& points, const int thickness)
{
	if(points.empty())
		return;
	std::set<Coord> shell;
	for(const Coord& point : points)
	{
		bool boundary = false;
		for(int distance=1; distance<=thickness && !boundary; ++distance)
		{
			const Coord neighbours[6] = {
				Coord(point.x - distance, point.y, point.z), Coord(point.x + distance, point.y, point.z),
				Coord(point.x, point.y - distance, point.z), Coord(point.x, point.y + distance, point.z),
				Coord(point.x, point.y, point.z - distance), Coord(point.x, point.y, point.z + distance)
			};
			for(const Coord& neighbour : neighbours)
				if(points.find(neighbour) == points.end())
				{
					boundary = true;
					break;
				}
		}
		if(boundary)
			shell.insert(point);
	}
	points.swap(shell);
}


void generateBox(const VoxelProceduralParams& params, PointCollector& collector)
{
	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
			for(int z=0; z<params.size_z && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				if(boxShellCell(x, y, z, params))
					collector.add(static_cast<int64_t>(params.origin.x) + x,
						static_cast<int64_t>(params.origin.y) + y, static_cast<int64_t>(params.origin.z) + z);
			}
}


void generateEllipsoid(const VoxelProceduralParams& params, PointCollector& collector)
{
	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
			for(int z=0; z<params.size_z && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				if(!ellipsoidContains(x, y, z, params, 0))
					continue;
				if(params.hollow && ellipsoidContains(x, y, z, params, params.wall_thickness))
					continue;
				collector.add(static_cast<int64_t>(params.origin.x) + x,
					static_cast<int64_t>(params.origin.y) + y, static_cast<int64_t>(params.origin.z) + z);
			}
}


void generateRock(const VoxelProceduralParams& params, PointCollector& collector)
{
	const double centre_x = (params.size_x - 1) * 0.5;
	const double centre_y = (params.size_y - 1) * 0.5;
	const double centre_z = (params.size_z - 1) * 0.5;
	const double radius_x = std::max(0.5, params.size_x * 0.5);
	const double radius_y = std::max(0.5, params.size_y * 0.5);
	const double radius_z = std::max(0.5, params.size_z * 0.5);
	const double roughness = 0.13 + params.detail * 0.012;

	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
			for(int z=0; z<params.size_z && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				const double nx = (x - centre_x) / radius_x;
				const double ny = (y - centre_y) / radius_y;
				const double nz = (z - centre_z) / radius_z;
				const double distance = std::sqrt(nx*nx + ny*ny + nz*nz);
				const double noise = fractalNoise(params.origin.x + x, params.origin.y + y, params.origin.z + z, params);
				const double threshold_bias = (0.5 - params.threshold) * 0.24;
				const double surface = 0.72 + params.density * 0.18 + (noise - 0.5) * roughness * 2.0 + threshold_bias;
				if(distance <= surface)
					collector.add(static_cast<int64_t>(params.origin.x) + x,
						static_cast<int64_t>(params.origin.y) + y, static_cast<int64_t>(params.origin.z) + z);
			}

	if(params.hollow)
		makeIrregularShell(collector.points, params.wall_thickness);
}


void generateTerrain(const VoxelProceduralParams& params, PointCollector& collector)
{
	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
		{
			const double noise = fractalNoise(params.origin.x + x, params.origin.y + y, params.origin.z, params);
			const double shaped_noise = clampValue(noise - params.threshold + 0.5, 0.0, 1.0);
			const double height_fraction = 0.08 + params.density * (0.12 + shaped_noise * 0.80);
			const int height = clampValue(static_cast<int>(std::lround(height_fraction * params.size_z)), 1, params.size_z);
			const int first_z = params.hollow ? std::max(0, height - params.wall_thickness) : 0;
			for(int z=first_z; z<height && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				collector.add(static_cast<int64_t>(params.origin.x) + x,
					static_cast<int64_t>(params.origin.y) + y, static_cast<int64_t>(params.origin.z) + z);
			}
		}
}


void generateNoiseVolume(const VoxelProceduralParams& params, PointCollector& collector)
{
	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
			for(int z=0; z<params.size_z && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				const int64_t world_x = static_cast<int64_t>(params.origin.x) + x;
				const int64_t world_y = static_cast<int64_t>(params.origin.y) + y;
				const int64_t world_z = static_cast<int64_t>(params.origin.z) + z;
				const double noise = fractalNoise(static_cast<double>(world_x), static_cast<double>(world_y),
					static_cast<double>(world_z), params);
				const double chance = unitHash(world_x, world_y, world_z, params.seed ^ 0xD1B54A35u);
				if(noise >= params.threshold && chance <= params.density)
					collector.add(world_x, world_y, world_z);
			}

	if(params.hollow)
		makeIrregularShell(collector.points, params.wall_thickness);
}


bool crystalShardContains(const int x, const int y, const int z, const VoxelProceduralParams& params, const int shard)
{
	const bool main_shard = shard == 0;
	const double random_x = unitHash(shard, 1, 0, params.seed ^ 0x23A5D1B7u);
	const double random_y = unitHash(shard, 2, 0, params.seed ^ 0x91E10DA5u);
	const double random_h = unitHash(shard, 3, 0, params.seed ^ 0xC13FA9A9u);
	const double centre_x = (params.size_x - 1) * (main_shard ? 0.5 : (0.20 + random_x * 0.60));
	const double centre_y = (params.size_y - 1) * (main_shard ? 0.5 : (0.20 + random_y * 0.60));
	const double shard_height = std::max(2.0, params.size_z * (main_shard ? 1.0 : (0.42 + random_h * 0.38)));
	if(z >= shard_height)
		return false;

	const double width_scale = main_shard ? 0.28 : (0.10 + random_h * 0.08);
	const double radius_x = std::max(1.0, params.size_x * width_scale);
	const double radius_y = std::max(1.0, params.size_y * width_scale);
	const double height_t = z / std::max(1.0, shard_height - 1.0);
	double taper = 1.0;
	if(height_t < 0.15)
		taper = 0.55 + height_t / 0.15 * 0.45;
	else if(height_t > 0.68)
		taper = std::max(0.05, (1.0 - height_t) / 0.32);

	const double dx = std::abs(x - centre_x) / radius_x;
	const double dy = std::abs(y - centre_y) / radius_y;
	const double faceted_distance = std::max(std::max(dx, dy), (dx + dy) * 0.58);
	return faceted_distance <= taper;
}


void generateCrystal(const VoxelProceduralParams& params, PointCollector& collector)
{
	const int shard_count = clampValue(params.detail, 1, 8);
	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
			for(int z=0; z<params.size_z && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				bool inside = false;
				for(int shard=0; shard<shard_count && !inside; ++shard)
					inside = crystalShardContains(x, y, z, params, shard);
				if(inside)
					collector.add(static_cast<int64_t>(params.origin.x) + x,
						static_cast<int64_t>(params.origin.y) + y, static_cast<int64_t>(params.origin.z) + z);
			}

	if(params.hollow)
		makeIrregularShell(collector.points, params.wall_thickness);
}


void generateWall(const VoxelProceduralParams& params, PointCollector& collector)
{
	const int crenel_height = std::max(1, params.size_z / 8);
	const int crenel_width = std::max(1, params.size_x / std::max(2, params.detail * 2));
	for(int x=0; x<params.size_x && !collector.stopped(); ++x)
		for(int y=0; y<params.size_y && !collector.stopped(); ++y)
			for(int z=0; z<params.size_z && !collector.stopped(); ++z)
			{
				if(!collector.beginSample())
					break;
				if(params.hollow && !boxShellCell(x, y, z, params))
					continue;
				if(z >= params.size_z - crenel_height && ((x / crenel_width) & 1) != 0)
					continue;
				const double height_fraction = params.size_z > 1 ? static_cast<double>(z) / (params.size_z - 1) : 1.0;
				const double damage_chance = (1.0 - params.density) * (0.15 + height_fraction * 0.85);
				if(unitHash(x, y, z, params.seed ^ 0xB5297A4Du) < damage_chance)
					continue;
				collector.add(static_cast<int64_t>(params.origin.x) + x,
					static_cast<int64_t>(params.origin.y) + y, static_cast<int64_t>(params.origin.z) + z);
			}
}


const char* commandName(const VoxelProceduralType type)
{
	switch(type)
	{
	case VoxelProceduralType::Box:          return "Generate voxel box";
	case VoxelProceduralType::Ellipsoid:    return "Generate voxel ellipsoid";
	case VoxelProceduralType::Rock:         return "Generate voxel rock";
	case VoxelProceduralType::TerrainPatch: return "Generate voxel terrain";
	case VoxelProceduralType::NoiseVolume:  return "Generate voxel noise volume";
	case VoxelProceduralType::Crystal:      return "Generate voxel crystal";
	case VoxelProceduralType::Wall:         return "Generate voxel wall";
	}
	return "Generate voxels";
}


bool sameCommands(const VoxelEditCommand& a, const VoxelEditCommand& b)
{
	if(a.name != b.name || a.changes.size() != b.changes.size())
		return false;
	for(size_t i=0; i<a.changes.size(); ++i)
	{
		const VoxelChange& left = a.changes[i];
		const VoxelChange& right = b.changes[i];
		if(left.coord != right.coord || left.before_material != right.before_material ||
			left.after_material != right.after_material || left.layer_index != right.layer_index)
			return false;
	}
	return true;
}

} // namespace


namespace VoxelProceduralGenerator
{

const char* typeName(const VoxelProceduralType type)
{
	switch(type)
	{
	case VoxelProceduralType::Box:          return "Box";
	case VoxelProceduralType::Ellipsoid:    return "Ellipsoid";
	case VoxelProceduralType::Rock:         return "Rock";
	case VoxelProceduralType::TerrainPatch: return "Terrain patch";
	case VoxelProceduralType::NoiseVolume:  return "Noise volume";
	case VoxelProceduralType::Crystal:      return "Crystal";
	case VoxelProceduralType::Wall:         return "Wall";
	}
	return "Unknown";
}


VoxelProceduralParams defaultParams(const VoxelProceduralType type)
{
	VoxelProceduralParams params;
	switch(type)
	{
	case VoxelProceduralType::Box:
		params.size_x = 12; params.size_y = 12; params.size_z = 12;
		break;
	case VoxelProceduralType::Ellipsoid:
		params.size_x = 16; params.size_y = 16; params.size_z = 16;
		break;
	case VoxelProceduralType::Rock:
		params.size_x = 20; params.size_y = 20; params.size_z = 14;
		params.noise_scale = 0.10f; params.octaves = 4; params.detail = 6;
		break;
	case VoxelProceduralType::TerrainPatch:
		params.size_x = 32; params.size_y = 32; params.size_z = 12;
		params.noise_scale = 0.07f; params.octaves = 4;
		break;
	case VoxelProceduralType::NoiseVolume:
		params.size_x = 24; params.size_y = 24; params.size_z = 24;
		params.noise_scale = 0.13f; params.threshold = 0.55f; params.density = 0.75f;
		break;
	case VoxelProceduralType::Crystal:
		params.size_x = 16; params.size_y = 16; params.size_z = 28;
		params.detail = 4;
		break;
	case VoxelProceduralType::Wall:
		params.size_x = 32; params.size_y = 3; params.size_z = 12;
		params.detail = 5; params.density = 0.94f;
		break;
	}
	return params;
}


VoxelProceduralParams sanitiseParams(const VoxelProceduralParams& params_)
{
	VoxelProceduralParams params = params_;
	params.size_x = clampValue(params.size_x, 1, MAX_DIMENSION);
	params.size_y = clampValue(params.size_y, 1, MAX_DIMENSION);
	params.size_z = clampValue(params.size_z, 1, MAX_DIMENSION);
	params.wall_thickness = clampValue(params.wall_thickness, 1, MAX_WALL_THICKNESS);
	if(!std::isfinite(params.noise_scale)) params.noise_scale = 0.12f;
	if(!std::isfinite(params.threshold)) params.threshold = 0.5f;
	if(!std::isfinite(params.density)) params.density = 1.0f;
	params.noise_scale = clampValue(params.noise_scale, 0.005f, 4.0f);
	params.threshold = clampValue(params.threshold, 0.0f, 1.0f);
	params.density = clampValue(params.density, 0.0f, 1.0f);
	params.octaves = clampValue(params.octaves, 1, 8);
	params.detail = clampValue(params.detail, 1, 16);
	params.max_voxels = clampValue<size_t>(params.max_voxels, 1, MAX_GENERATED_VOXELS);
	switch(params.write_mode)
	{
	case VoxelBrushMode::Add:
	case VoxelBrushMode::Replace:
	case VoxelBrushMode::Paint:
		break;
	default:
		params.write_mode = VoxelBrushMode::Add;
		break;
	}
	return params;
}


VoxelProceduralMetrics computeMetrics(const VoxelProceduralParams& params_)
{
	const VoxelProceduralParams params = sanitiseParams(params_);
	const uint64_t x = static_cast<uint64_t>(params.size_x);
	const uint64_t y = static_cast<uint64_t>(params.size_y);
	const uint64_t z = static_cast<uint64_t>(params.size_z);
	VoxelProceduralMetrics metrics;
	metrics.footprint_area = x * y;
	metrics.footprint_perimeter = 2 * (x + y);
	metrics.bounding_volume = x * y * z;
	metrics.bounding_surface_area = 2 * (x * y + x * z + y * z);
	return metrics;
}


VoxelProceduralResult buildCommand(const VoxelProceduralType type,
	const VoxelProceduralParams& params_, const VoxelEditorState& state,
	const glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelProceduralResult result;
	result.effective_params = sanitiseParams(params_);
	result.metrics = computeMetrics(result.effective_params);
	result.command.name = commandName(type);
	const VoxelProceduralParams& params = result.effective_params;

	const int layer_index = params.layer_index >= 0 ? params.layer_index : state.active_layer;
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
	if(!validMaterial(params.material_index) || !VoxelEditorData::layerOwnsMaterial(state, layer_index, params.material_index))
	{
		result.error = "The selected material is not assigned to the active voxel layer.";
		return result;
	}
	if(params.clear_active_layer && params.write_mode == VoxelBrushMode::Paint)
	{
		result.error = "Paint mode cannot be combined with Clear active layer; choose Add or Replace.";
		return result;
	}

	MaterialMap materials = makeMaterialMap(voxels);
	std::vector<Coord> active_layer_points;
	if(params.clear_active_layer)
	{
		active_layer_points.reserve(materials.size());
		for(const auto& entry : materials)
			if(VoxelEditorData::layerOwnsMaterial(state, layer_index, entry.second))
				active_layer_points.push_back(entry.first);
		std::sort(active_layer_points.begin(), active_layer_points.end());
		if(active_layer_points.size() >= params.max_voxels)
		{
			result.error = "The active layer is too large to replace within the voxel operation safety cap.";
			return result;
		}
	}

	// Reserve enough command capacity to clear the layer atomically.  This can
	// be conservative when generated points overlap old points, but guarantees
	// that a successful result never contains a partial layer replacement.
	const size_t generation_limit = params.clear_active_layer ?
		(params.max_voxels - active_layer_points.size()) : params.max_voxels;
	PointCollector collector(generation_limit);
	switch(type)
	{
	case VoxelProceduralType::Box:          generateBox(params, collector); break;
	case VoxelProceduralType::Ellipsoid:    generateEllipsoid(params, collector); break;
	case VoxelProceduralType::Rock:         generateRock(params, collector); break;
	case VoxelProceduralType::TerrainPatch: generateTerrain(params, collector); break;
	case VoxelProceduralType::NoiseVolume:  generateNoiseVolume(params, collector); break;
	case VoxelProceduralType::Crystal:      generateCrystal(params, collector); break;
	case VoxelProceduralType::Wall:         generateWall(params, collector); break;
	default:
		result.error = "Unknown voxel procedural generator.";
		return result;
	}
	result.generated_voxels = collector.points.size();
	result.truncated = collector.truncated();
	if(result.truncated)
	{
		result.error = "The procedural generator exceeded its safety or coordinate limit; no voxels were changed.";
		result.command = VoxelEditCommand(commandName(type));
		return result;
	}

	if(params.clear_active_layer)
		for(const Coord& point : active_layer_points)
		{
			const int before = materialAt(materials, point);
			result.command.recordChange(point.toVec3(), before, VoxelChange::NO_MATERIAL, layer_index);
			materials.erase(point);
		}

	for(const Coord& point : collector.points)
	{
		const int before = materialAt(materials, point);
		const bool occupied = validMaterial(before);
		const bool belongs_to_layer = occupied && VoxelEditorData::layerOwnsMaterial(state, layer_index, before);
		int after = before;
		if(params.write_mode == VoxelBrushMode::Paint)
		{
			if(belongs_to_layer)
				after = params.material_index;
		}
		else if(params.write_mode == VoxelBrushMode::Replace)
		{
			if(!occupied || belongs_to_layer)
				after = params.material_index;
		}
		else if(!occupied)
			after = params.material_index;

		if(before != after)
		{
			result.command.recordChange(point.toVec3(), before, after, layer_index);
			materials[point] = after;
		}
	}
	result.command.compact();
	return result;
}


VoxelProceduralResult execute(const VoxelProceduralType type,
	const VoxelProceduralParams& params, const VoxelEditorState& state,
	glare::AllocatorVector<Voxel, 16>& voxels)
{
	VoxelProceduralResult result = buildCommand(type, params, state, voxels);
	if(!result.command.empty())
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
	glare::AllocatorVector<Voxel, 16> empty_voxels;
	VoxelProceduralParams box;
	box.origin = Vec3<int>(-2, 4, 7);
	box.size_x = 4;
	box.size_y = 3;
	box.size_z = 2;
	box.material_index = 1;
	VoxelProceduralResult solid = buildCommand(VoxelProceduralType::Box, box, state, empty_voxels);
	if(!solid.error.empty() || solid.command.changedCount() != 24 || solid.generated_voxels != 24)
		return fail("Procedural box dimensions failed.");
	for(const VoxelChange& change : solid.command.changes)
		if(change.coord.x < -2 || change.coord.x > 1 || change.coord.y < 4 || change.coord.y > 6 ||
			change.coord.z < 7 || change.coord.z > 8)
			return fail("Procedural box escaped its requested bounds.");

	VoxelProceduralResult solid_repeat = buildCommand(VoxelProceduralType::Box, box, state, empty_voxels);
	if(!sameCommands(solid.command, solid_repeat.command))
		return fail("Procedural generation is not deterministic.");

	VoxelProceduralParams hollow = box;
	hollow.origin = Vec3<int>(0, 0, 0);
	hollow.size_x = hollow.size_y = hollow.size_z = 5;
	hollow.hollow = true;
	hollow.wall_thickness = 1;
	VoxelProceduralResult shell = buildCommand(VoxelProceduralType::Box, hollow, state, empty_voxels);
	if(shell.command.changedCount() != 98)
		return fail("Hollow box wall thickness failed.");

	glare::AllocatorVector<Voxel, 16> layer_voxels;
	layer_voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	layer_voxels.push_back(Voxel(Vec3<int>(5, 0, 0), 0));
	VoxelProceduralParams replace_layer = box;
	replace_layer.origin = Vec3<int>(0, 0, 0);
	replace_layer.size_x = replace_layer.size_y = replace_layer.size_z = 1;
	replace_layer.max_voxels = 16;
	VoxelProceduralResult replacement = buildCommand(VoxelProceduralType::Box, replace_layer, state, layer_voxels);
	if(!replacement.error.empty() || replacement.command.changedCount() != 2)
		return fail("Atomic active-layer replacement failed.");
	replacement.command.redo(layer_voxels);
	if(layer_voxels.size() != 1 || layer_voxels[0].pos != Vec3<int>(0, 0, 0) || layer_voxels[0].mat_index != 1)
		return fail("Active-layer replacement produced incorrect voxels.");
	replacement.command.undo(layer_voxels);
	if(layer_voxels.size() != 2)
		return fail("Active-layer replacement is not reversible in one command.");

	VoxelProceduralParams merge = replace_layer;
	merge.origin = Vec3<int>(10, 0, 0);
	merge.clear_active_layer = false;
	VoxelProceduralResult merged = buildCommand(VoxelProceduralType::Box, merge, state, layer_voxels);
	if(merged.command.changedCount() != 1)
		return fail("Non-destructive procedural merge failed.");
	VoxelProceduralParams clear_cap = replace_layer;
	clear_cap.max_voxels = 2;
	VoxelProceduralResult clear_cap_result = buildCommand(VoxelProceduralType::Box, clear_cap, state, layer_voxels);
	if(clear_cap_result.error.empty() || clear_cap_result.changed())
		return fail("Oversized active-layer replacement was not rejected atomically.");

	const VoxelProceduralMetrics metrics = computeMetrics(box);
	if(metrics.footprint_area != 12 || metrics.footprint_perimeter != 14 ||
		metrics.bounding_volume != 24 || metrics.bounding_surface_area != 52)
		return fail("Procedural size metrics failed.");

	const VoxelProceduralType generators[] = {
		VoxelProceduralType::Box, VoxelProceduralType::Ellipsoid, VoxelProceduralType::Rock,
		VoxelProceduralType::TerrainPatch, VoxelProceduralType::NoiseVolume,
		VoxelProceduralType::Crystal, VoxelProceduralType::Wall
	};
	for(const VoxelProceduralType generator : generators)
	{
		VoxelProceduralParams params = defaultParams(generator);
		params.size_x = 10;
		params.size_y = 9;
		params.size_z = 8;
		params.material_index = 1;
		params.threshold = 0.35f;
		params.density = 1.0f;
		params.max_voxels = 4096;
		VoxelProceduralResult first = buildCommand(generator, params, state, empty_voxels);
		VoxelProceduralResult second = buildCommand(generator, params, state, empty_voxels);
		if(!first.error.empty() || first.command.empty())
			return fail("A procedural generator produced no voxels.");
		if(!sameCommands(first.command, second.command))
			return fail("A seeded procedural generator is not deterministic.");
	}

	VoxelProceduralParams capped = box;
	capped.size_x = capped.size_y = capped.size_z = 32;
	capped.max_voxels = 7;
	VoxelProceduralResult cap_result = buildCommand(VoxelProceduralType::Box, capped, state, empty_voxels);
	if(!cap_result.truncated || cap_result.generated_voxels != 7 || !cap_result.command.empty() || cap_result.error.empty())
		return fail("Procedural voxel safety cap failed.");

	VoxelProceduralParams oversized = box;
	oversized.size_x = 9999;
	oversized.wall_thickness = 9999;
	oversized.max_voxels = std::numeric_limits<size_t>::max();
	const VoxelProceduralParams sanitised = sanitiseParams(oversized);
	if(sanitised.size_x != MAX_DIMENSION || sanitised.wall_thickness != MAX_WALL_THICKNESS ||
		sanitised.max_voxels != MAX_GENERATED_VOXELS)
		return fail("Procedural parameter limits failed.");

	VoxelProceduralParams clipped = box;
	clipped.origin = Vec3<int>(32765, 0, 0);
	clipped.size_x = 4;
	VoxelProceduralResult clipped_result = buildCommand(VoxelProceduralType::Box, clipped, state, empty_voxels);
	if(!clipped_result.truncated || !clipped_result.command.empty() || clipped_result.error.empty())
		return fail("Out-of-range procedural coordinates were not reported as truncated.");
	for(const VoxelChange& change : clipped_result.command.changes)
		if(change.coord.x > MAX_VOXEL_COORD)
			return fail("Out-of-range procedural coordinate reached the command.");

	VoxelProceduralParams clear_paint = box;
	clear_paint.clear_active_layer = true;
	clear_paint.write_mode = VoxelBrushMode::Paint;
	VoxelProceduralResult clear_paint_result = buildCommand(VoxelProceduralType::Box, clear_paint, state, layer_voxels);
	if(clear_paint_result.error.empty() || !clear_paint_result.command.empty())
		return fail("Clear-layer Paint mode was not rejected atomically.");

	glare::AllocatorVector<Voxel, 16> applied_voxels;
	solid.command.redo(applied_voxels);
	if(applied_voxels.size() != solid.command.changedCount())
		return fail("Procedural command redo failed.");
	solid.command.undo(applied_voxels);
	if(!applied_voxels.empty())
		return fail("Procedural command is not reversible as one undo step.");

	state.layers[0].locked = true;
	VoxelProceduralResult locked = buildCommand(VoxelProceduralType::Box, box, state, empty_voxels);
	if(locked.error.empty() || locked.changed())
		return fail("Procedural generator edited a locked layer.");

	if(details_out)
		*details_out = "ok";
	return true;
}

} // namespace VoxelProceduralGenerator
