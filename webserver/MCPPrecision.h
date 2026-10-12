// Private MCP precision tools. Include after MCPModeling.h (no Qt dependencies).
#pragma once

namespace MCPHandlers { namespace Modeling {

struct PrecisionBounds
{
	Vec3d min, max, size, center;

	explicit PrecisionBounds(const WorldObject& ob)
	{
		const js::AABBox box = ob.getAABBWS();
		if(!ObjectCopyPermissions::finiteBounds(box))
			throw glare::Exception("Object has invalid stored world bounds.");
		min = Vec3d(box.min_[0], box.min_[1], box.min_[2]);
		max = Vec3d(box.max_[0], box.max_[1], box.max_[2]);
		size = max - min;
		center = (min + max) * 0.5;
	}
};

static double precisionComponent(const Vec3d& v, int axis)
{
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

static void precisionSetComponent(Vec3d& v, int axis, double value)
{
	if(axis == 0) v.x = value;
	else if(axis == 1) v.y = value;
	else v.z = value;
}

static int precisionAxis(const JSONParser& p, const JSONNode& n)
{
	const std::string axis = n.getChildStringValue(p, "axis");
	if(axis == "x") return 0;
	if(axis == "y") return 1;
	if(axis == "z") return 2;
	throw glare::Exception("axis must be x, y or z in world coordinates.");
}

static UID precisionTargetUID(const JSONParser& p, const JSONNode& n)
{
	const double value = number(p, n, "target_uid", -1, 0, 9007199254740991.0);
	if(value != std::floor(value)) throw glare::Exception("target_uid must be an integer.");
	return UID((uint64)value);
}

static Vec3d precisionAnchor(const PrecisionBounds& bounds, const std::string& anchor)
{
	if(anchor == "min") return bounds.min;
	if(anchor == "center") return bounds.center;
	if(anchor == "max") return bounds.max;
	throw glare::Exception("Bounding-box anchor must be min, center or max.");
}

static std::string precisionBoundsJSON(const WorldObject& ob)
{
	const PrecisionBounds bounds(ob);
	return "{\"uid\":" + toString(ob.uid.value()) +
		",\"world_aabb_min_m\":" + vecJSON(bounds.min) +
		",\"world_aabb_max_m\":" + vecJSON(bounds.max) +
		",\"world_aabb_size_m\":" + vecJSON(bounds.size) +
		",\"world_aabb_center_m\":" + vecJSON(bounds.center) +
		",\"object_origin_m\":" + vecJSON(ob.pos) +
		",\"scale\":" + vecJSON(Vec3d(ob.scale.x, ob.scale.y, ob.scale.z)) +
		",\"measurement_basis\":\"transformed_stored_local_bounds\","
		"\"limitations\":\"World-axis enclosing bounds, not local architectural dimensions or exact mesh surface distance. Rotation can enlarge these bounds. Animation and scripted motion are not evaluated.\"}";
}

static std::string measureObject(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n)
{
	WorldStateLock lock(all.mutex);
	const WorldObjectRef ob = findObject(w, lock, objectUID(p, n));
	const PrecisionBounds bounds(*ob);
	std::string result = "{\"object\":" + precisionBoundsJSON(*ob);
	if(n.hasChild("expected_size"))
	{
		const Vec3d expected = vector(p, n, "expected_size", Vec3d(0, 0, 0), 0, 1e7);
		const double tolerance = number(p, n, "tolerance_m", 0.001, 0.000001, 1000);
		const Vec3d delta = bounds.size - expected;
		const bool passes = std::abs(delta.x) <= tolerance && std::abs(delta.y) <= tolerance && std::abs(delta.z) <= tolerance;
		result += ",\"dimension_check\":{\"expected_world_aabb_size_m\":" + vecJSON(expected) +
			",\"actual_minus_expected_m\":" + vecJSON(delta) + ",\"tolerance_m\":" + doubleToString(tolerance) +
			",\"within_tolerance\":" + std::string(passes ? "true" : "false") + "}";
	}
	else if(n.hasChild("tolerance_m")) throw glare::Exception("tolerance_m requires expected_size.");
	if(n.hasChild("target_uid"))
	{
		const WorldObjectRef target = findObject(w, lock, precisionTargetUID(p, n));
		const PrecisionBounds other(*target);
		Vec3d separation(0, 0, 0);
		for(int axis = 0; axis < 3; ++axis)
			precisionSetComponent(separation, axis, std::max(
				precisionComponent(other.min, axis) - precisionComponent(bounds.max, axis),
				precisionComponent(bounds.min, axis) - precisionComponent(other.max, axis)));
		const Vec3d gap(std::max(0.0, separation.x), std::max(0.0, separation.y), std::max(0.0, separation.z));
		result += ",\"target\":" + precisionBoundsJSON(*target) +
			",\"aabb_signed_axis_separation_m\":" + vecJSON(separation) +
			",\"aabb_gap_m\":" + doubleToString(std::sqrt(gap.x * gap.x + gap.y * gap.y + gap.z * gap.z)) +
			",\"pair_note\":\"Positive axis separation is an AABB gap; zero touches bounds; negative is bounding-interval overlap. AABB overlap does not prove mesh contact or collision.\"";
	}
	return result + "}";
}

static std::string precisionTransform(ServerAllWorldsState& all, ServerWorldState& w,
	const JSONParser& p, const JSONNode& n, const UserID& user, const std::string& operation)
{
	WorldStateLock lock(all.mutex);
	if(!user.valid()) throw glare::Exception("Authentication is required.");
	if(all.isInReadOnlyMode()) throw glare::Exception("Server is in read-only mode.");
	const WorldObjectRef ob = findObject(w, lock, objectUID(p, n));
	if(!editable(*ob, w, user)) throw glare::Exception("Permission denied for this object.");
	const PrecisionBounds before(*ob);
	WorldObjectRef candidate = new WorldObject();
	candidate->copyNetworkStateFrom(*ob);
	candidate->uid = ob->uid;
	candidate->transformChanged();
	const int axis = precisionAxis(p, n);
	const double tolerance = number(p, n, "tolerance_m", 0.001, 0.000001, 1);
	double requested = 0;
	std::string measured_anchor;

	if(operation == "resize")
	{
		requested = number(p, n, "size_m", -1, 0.000001, 100000);
		const double old_size = precisionComponent(before.size, axis);
		if(old_size < 0.000001) throw glare::Exception("Cannot resize a zero-size bounding-box axis.");
		const double factor = requested / old_size;
		const Vec3d scale(ob->scale.x * factor, ob->scale.y * factor, ob->scale.z * factor);
		for(int a = 0; a < 3; ++a)
		{
			const double component = std::abs(precisionComponent(scale, a));
			if(!std::isfinite(component) || component < 0.001 || component > 100)
				throw glare::Exception("Uniform resize would exceed the supported absolute scale range 0.001..100.");
		}
		candidate->scale = Vec3f((float)scale.x, (float)scale.y, (float)scale.z);
		candidate->transformChanged();
		const std::string anchor = n.getChildStringValueWithDefaultVal(p, "anchor", "center");
		candidate->pos = candidate->pos + precisionAnchor(before, anchor) - precisionAnchor(PrecisionBounds(*candidate), anchor);
	}
	else if(operation == "align")
	{
		const UID target_uid = precisionTargetUID(p, n);
		if(target_uid == ob->uid) throw glare::Exception("Source and target must be different objects.");
		const WorldObjectRef target = findObject(w, lock, target_uid);
		measured_anchor = n.getChildStringValue(p, "source_anchor");
		const std::string target_anchor = n.getChildStringValue(p, "target_anchor");
		const double gap = number(p, n, "offset_m", 0, -100000, 100000);
		requested = precisionComponent(precisionAnchor(PrecisionBounds(*target), target_anchor), axis) + gap;
		const double delta = requested - precisionComponent(precisionAnchor(before, measured_anchor), axis);
		precisionSetComponent(candidate->pos, axis, precisionComponent(candidate->pos, axis) + delta);
	}
	else if(operation == "snap")
	{
		const double grid = number(p, n, "grid_m", -1, 0.000001, 100000);
		const double origin = number(p, n, "grid_origin_m", 0, -1e7, 1e7);
		requested = origin + std::round((precisionComponent(candidate->pos, axis) - origin) / grid) * grid;
		precisionSetComponent(candidate->pos, axis, requested);
	}
	else throw glare::Exception("Unknown precision operation.");

	for(int a = 0; a < 3; ++a)
		if(!std::isfinite(precisionComponent(candidate->pos, a)) || std::abs(precisionComponent(candidate->pos, a)) > 1e7)
			throw glare::Exception("Resulting object origin is outside supported world coordinates.");
	candidate->transformChanged();
	const PrecisionBounds after(*candidate);
	const double actual = operation == "resize" ? precisionComponent(after.size, axis) :
		operation == "align" ? precisionComponent(precisionAnchor(after, measured_anchor), axis) : precisionComponent(candidate->pos, axis);
	const double delta = actual - requested;
	if(std::abs(delta) > tolerance)
		throw glare::Exception("Stored bounds precision cannot achieve the requested tolerance at this size/world position. No change was made.");
	permission(*candidate, w, lock, user);
	const std::string before_json = precisionBoundsJSON(*ob);
	dirtyChunk(w, lock, *ob);
	ob->copyNetworkStateFrom(*candidate);
	ob->transformChanged();
	dirtyChunk(w, lock, *ob);
	ob->last_modified_time = TimeStamp::currentTime();
	ob->from_remote_other_dirty = true;
	w.addWorldObjectAsDBDirty(ob, lock);
	w.getDirtyFromRemoteObjects(lock).insert(ob);
	all.markAsChanged();
	return "{\"operation\":\"" + operation + "\",\"before\":" + before_json +
		",\"after\":" + precisionBoundsJSON(*ob) + ",\"requested_m\":" + doubleToString(requested) +
		",\"actual_m\":" + doubleToString(actual) + ",\"actual_minus_requested_m\":" + doubleToString(delta) +
		",\"tolerance_m\":" + doubleToString(tolerance) + ",\"within_tolerance\":true}";
}

} } // namespace MCPHandlers::Modeling
