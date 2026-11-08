/*=====================================================================
GameRaySpatialIndex.h
---------------------
Bounded uniform-grid broad phase for server-authoritative Luau ray and swept-AABB queries.
All access is protected by the owning ServerWorldState world mutex.
=====================================================================*/
#pragma once

#include "../shared/WorldObject.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

class GameRaySpatialIndex
{
public:
	static const size_t MAX_QUERY_CANDIDATES = 8192;

	void clear()
	{
		cells.clear();
		object_cells.clear();
		overflow_objects.clear();
		cell_reference_count = 0;
		built = false;
	}

	bool isBuilt() const { return built; }

	template<class ObjectMap>
	void rebuild(const ObjectMap& objects)
	{
		clear();
		for(const auto& entry : objects)
			indexObject(*entry.second);
		built = true;
	}

	void updateObject(const WorldObject& object)
	{
		if(!built) return;
		removeObject(object.uid);
		indexObject(object);
	}

	void removeObject(const UID& uid)
	{
		const auto object_it = object_cells.find(uid);
		if(object_it != object_cells.end())
		{
			for(const Cell& cell : object_it->second)
			{
				auto cell_it = cells.find(cell);
				if(cell_it == cells.end()) continue;
				cell_it->second.erase(uid);
				if(cell_it->second.empty()) cells.erase(cell_it);
				if(cell_reference_count > 0) --cell_reference_count;
			}
			object_cells.erase(object_it);
		}
		overflow_objects.erase(uid);
	}

	bool candidatesForSegment(const Vec3d& origin, const Vec3d& direction, double max_distance,
		std::vector<UID>& candidates_out) const
	{
		if(!origin.isFinite() || !direction.isFinite() || !std::isfinite(max_distance) || max_distance < 0.0)
		{
			candidates_out.clear();
			return false;
		}
		const Vec3d end = origin + direction * max_distance;
		if(!end.isFinite())
		{
			candidates_out.clear();
			return false;
		}
		return candidatesForBounds(Vec3d(std::min(origin.x, end.x), std::min(origin.y, end.y), std::min(origin.z, end.z)),
			Vec3d(std::max(origin.x, end.x), std::max(origin.y, end.y), std::max(origin.z, end.z)), candidates_out);
	}

	bool candidatesForBounds(const Vec3d& bounds_min, const Vec3d& bounds_max, std::vector<UID>& candidates_out) const
	{
		candidates_out.clear();
		if(!built || !bounds_min.isFinite() || !bounds_max.isFinite() ||
			bounds_min.x > bounds_max.x || bounds_min.y > bounds_max.y || bounds_min.z > bounds_max.z)
			return false;
		static const double MAX_INDEXED_COORDINATE = 1.0e7;
		if(std::abs(bounds_min.x) > MAX_INDEXED_COORDINATE || std::abs(bounds_min.y) > MAX_INDEXED_COORDINATE ||
			std::abs(bounds_min.z) > MAX_INDEXED_COORDINATE || std::abs(bounds_max.x) > MAX_INDEXED_COORDINATE ||
			std::abs(bounds_max.y) > MAX_INDEXED_COORDINATE || std::abs(bounds_max.z) > MAX_INDEXED_COORDINATE)
			return false;
		const int min_x = cellCoordinate(bounds_min.x);
		const int min_y = cellCoordinate(bounds_min.y);
		const int min_z = cellCoordinate(bounds_min.z);
		const int max_x = cellCoordinate(bounds_max.x);
		const int max_y = cellCoordinate(bounds_max.y);
		const int max_z = cellCoordinate(bounds_max.z);
		const std::int64_t query_cell_count = (std::int64_t)(max_x - min_x + 1) * (max_y - min_y + 1) * (max_z - min_z + 1);
		if(query_cell_count < 1 || query_cell_count > 512) return false;

		std::set<UID> unique_candidates;
		for(const UID& uid : overflow_objects)
		{
			unique_candidates.insert(uid);
			if(unique_candidates.size() > MAX_QUERY_CANDIDATES) return false;
		}
		for(int x = min_x; x <= max_x; ++x)
			for(int y = min_y; y <= max_y; ++y)
				for(int z = min_z; z <= max_z; ++z)
				{
					const auto cell_it = cells.find(Cell(x, y, z));
					if(cell_it == cells.end()) continue;
					for(const UID& uid : cell_it->second)
					{
						unique_candidates.insert(uid);
						if(unique_candidates.size() > MAX_QUERY_CANDIDATES) return false;
					}
				}
		candidates_out.assign(unique_candidates.begin(), unique_candidates.end());
		return true;
	}

private:
	struct Cell
	{
		Cell() : x(0), y(0), z(0) {}
		Cell(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
		bool operator<(const Cell& other) const
		{
			if(x != other.x) return x < other.x;
			if(y != other.y) return y < other.y;
			return z < other.z;
		}
		int x, y, z;
	};

	static int cellCoordinate(double value)
	{
		static const double CELL_SIZE = 64.0;
		return (int)std::floor(value / CELL_SIZE);
	}

	void indexObject(const WorldObject& object)
	{
		if(object.state == WorldObject::State_Dead ||
			(!object.game_spawn_owner_uid.valid() && (!object.isCollidable() || (object.flags & WorldObject::IS_SENSOR_FLAG) != 0)))
			return;
		const js::AABBox bounds = object.getAABBWS();
		if(!std::isfinite(bounds.min_[0]) || !std::isfinite(bounds.min_[1]) || !std::isfinite(bounds.min_[2]) ||
			!std::isfinite(bounds.max_[0]) || !std::isfinite(bounds.max_[1]) || !std::isfinite(bounds.max_[2])) return;
		if(std::abs((double)bounds.min_[0]) > 1.0e7 || std::abs((double)bounds.min_[1]) > 1.0e7 || std::abs((double)bounds.min_[2]) > 1.0e7 ||
			std::abs((double)bounds.max_[0]) > 1.0e7 || std::abs((double)bounds.max_[1]) > 1.0e7 || std::abs((double)bounds.max_[2]) > 1.0e7)
		{
			overflow_objects.insert(object.uid);
			return;
		}
		const int min_x = cellCoordinate(bounds.min_[0]), min_y = cellCoordinate(bounds.min_[1]), min_z = cellCoordinate(bounds.min_[2]);
		const int max_x = cellCoordinate(bounds.max_[0]), max_y = cellCoordinate(bounds.max_[1]), max_z = cellCoordinate(bounds.max_[2]);
		const std::int64_t count = (std::int64_t)(max_x - min_x + 1) * (max_y - min_y + 1) * (max_z - min_z + 1);
		// Very large objects stay in a bounded overflow list and are checked by
		// every short ray. This avoids inserting a single building into thousands
		// of grid buckets.
		if(count < 1 || count > 512 || cell_reference_count + (size_t)count > 2000000)
		{
			overflow_objects.insert(object.uid);
			return;
		}
		std::vector<Cell>& object_cell_list = object_cells[object.uid];
		object_cell_list.reserve((size_t)count);
		for(int x = min_x; x <= max_x; ++x)
			for(int y = min_y; y <= max_y; ++y)
				for(int z = min_z; z <= max_z; ++z)
				{
					const Cell cell(x, y, z);
					cells[cell].insert(object.uid);
					object_cell_list.push_back(cell);
					++cell_reference_count;
				}
	}

	std::map<Cell, std::set<UID>> cells;
	std::map<UID, std::vector<Cell>> object_cells;
	std::set<UID> overflow_objects;
	size_t cell_reference_count = 0;
	bool built = false;
};
