/*=====================================================================
TreeGenerator.cpp
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
// Native adaptation informed by dgreenheck/ez-tree commit
// 48dc193515135cff2b33515c47f0a8703b977e63.  See
// resources/tree_assets/EZ_TREE_LICENSE.txt for the upstream MIT license.
#include "TreeGenerator.h"
#include "TreePresets.h"
#include "TreeSerialization.h"


#include "../shared/LODGeneration.h"
#include <utils/FileUtils.h>
#include <utils/FileChecksum.h>
#include <utils/PlatformUtils.h>
#include <utils/StringUtils.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <sstream>


namespace
{
static const float PI = 3.14159265358979323846f;
// The EZ-Tree demo units are intentionally large.  The editor exposes metres,
// while the generation maths runs in the original units so force and
// gnarliness produce the same silhouettes as the upstream presets.
static const float EZ_TO_METRES = 0.20f;


TreeVec3 makeVec3(float x, float y, float z)
{
	TreeVec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}


TreeVec3 add(const TreeVec3& a, const TreeVec3& b)
{
	return makeVec3(a.x + b.x, a.y + b.y, a.z + b.z);
}


TreeVec3 sub(const TreeVec3& a, const TreeVec3& b)
{
	return makeVec3(a.x - b.x, a.y - b.y, a.z - b.z);
}


TreeVec3 mul(const TreeVec3& a, float s)
{
	return makeVec3(a.x * s, a.y * s, a.z * s);
}


float dot(const TreeVec3& a, const TreeVec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}


TreeVec3 cross(const TreeVec3& a, const TreeVec3& b)
{
	return makeVec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}


float length(const TreeVec3& v)
{
	return std::sqrt(dot(v, v));
}


TreeVec3 normalise(const TreeVec3& v, const TreeVec3& fallback = makeVec3(0, 1, 0))
{
	const float len = length(v);
	return len > 1.0e-7f ? mul(v, 1.0f / len) : fallback;
}


TreeVec3 lerp(const TreeVec3& a, const TreeVec3& b, float t)
{
	return add(a, mul(sub(b, a), t));
}


// Public TreeParams use the same Z-up axes as the rest of Metasiberia.  The
// faithful EZ-Tree maths below runs in its original Y-up coordinate system.
TreeVec3 engineToSource(const TreeVec3& v)
{
	return makeVec3(v.x, v.z, -v.y);
}


float clampFloat(float v, float min_v, float max_v)
{
	return std::max(min_v, std::min(max_v, v));
}


struct TreeQuat
{
	float x;
	float y;
	float z;
	float w;
};


TreeQuat normaliseQuat(const TreeQuat& q)
{
	const float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
	if(len <= 1.0e-8f)
		return TreeQuat{0, 0, 0, 1};
	const float inv = 1.0f / len;
	return TreeQuat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}


TreeQuat quatMultiply(const TreeQuat& a, const TreeQuat& b)
{
	return TreeQuat{
		a.x*b.w + a.w*b.x + a.y*b.z - a.z*b.y,
		a.y*b.w + a.w*b.y + a.z*b.x - a.x*b.z,
		a.z*b.w + a.w*b.z + a.x*b.y - a.y*b.x,
		a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
	};
}


TreeQuat quatFromAxisAngle(const TreeVec3& axis_, float angle)
{
	const TreeVec3 axis = normalise(axis_, makeVec3(1, 0, 0));
	const float s = std::sin(angle * 0.5f);
	return TreeQuat{axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}


// Matches THREE.Euler's default XYZ order.
TreeQuat quatFromEuler(const TreeVec3& e)
{
	const float c1 = std::cos(e.x * 0.5f);
	const float c2 = std::cos(e.y * 0.5f);
	const float c3 = std::cos(e.z * 0.5f);
	const float s1 = std::sin(e.x * 0.5f);
	const float s2 = std::sin(e.y * 0.5f);
	const float s3 = std::sin(e.z * 0.5f);
	return normaliseQuat(TreeQuat{
		s1*c2*c3 + c1*s2*s3,
		c1*s2*c3 - s1*c2*s3,
		c1*c2*s3 + s1*s2*c3,
		c1*c2*c3 - s1*s2*s3
	});
}


TreeVec3 eulerFromQuat(const TreeQuat& q_)
{
	const TreeQuat q = normaliseQuat(q_);
	const float m11 = 1.0f - 2.0f * (q.y*q.y + q.z*q.z);
	const float m12 = 2.0f * (q.x*q.y - q.w*q.z);
	const float m13 = 2.0f * (q.x*q.z + q.w*q.y);
	const float m22 = 1.0f - 2.0f * (q.x*q.x + q.z*q.z);
	const float m23 = 2.0f * (q.y*q.z - q.w*q.x);
	const float m32 = 2.0f * (q.y*q.z + q.w*q.x);
	const float m33 = 1.0f - 2.0f * (q.x*q.x + q.y*q.y);

	TreeVec3 e;
	e.y = std::asin(clampFloat(m13, -1.0f, 1.0f));
	if(std::fabs(m13) < 0.9999999f)
	{
		e.x = std::atan2(-m23, m33);
		e.z = std::atan2(-m12, m11);
	}
	else
	{
		e.x = std::atan2(m32, m22);
		e.z = 0.0f;
	}
	return e;
}


TreeVec3 applyQuat(const TreeQuat& q_, const TreeVec3& v)
{
	const TreeQuat q = normaliseQuat(q_);
	const TreeVec3 qv = makeVec3(q.x, q.y, q.z);
	const TreeVec3 t = mul(cross(qv, v), 2.0f);
	return add(v, add(mul(t, q.w), cross(qv, t)));
}


TreeQuat slerp(const TreeQuat& a_, const TreeQuat& b_, float t)
{
	TreeQuat a = normaliseQuat(a_);
	TreeQuat b = normaliseQuat(b_);
	float cos_half = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
	if(cos_half < 0.0f)
	{
		b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
		cos_half = -cos_half;
	}
	if(cos_half > 0.9995f)
	{
		return normaliseQuat(TreeQuat{
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t,
			a.w + (b.w - a.w) * t
		});
	}
	const float half = std::acos(clampFloat(cos_half, -1.0f, 1.0f));
	const float sin_half = std::sin(half);
	const float wa = std::sin((1.0f - t) * half) / sin_half;
	const float wb = std::sin(t * half) / sin_half;
	return normaliseQuat(TreeQuat{a.x*wa + b.x*wb, a.y*wa + b.y*wb, a.z*wa + b.z*wb, a.w*wa + b.w*wb});
}


TreeQuat quatFromUnitVectors(const TreeVec3& from_, const TreeVec3& to_)
{
	const TreeVec3 from = normalise(from_);
	const TreeVec3 to = normalise(to_);
	float r = dot(from, to) + 1.0f;
	TreeVec3 xyz;
	if(r < 1.0e-6f)
	{
		r = 0.0f;
		xyz = std::fabs(from.x) > std::fabs(from.z) ? makeVec3(-from.y, from.x, 0) : makeVec3(0, -from.z, from.y);
	}
	else
		xyz = cross(from, to);
	return normaliseQuat(TreeQuat{xyz.x, xyz.y, xyz.z, r});
}


TreeQuat rotateTowards(const TreeQuat& from, const TreeQuat& to, float step)
{
	const TreeQuat a = normaliseQuat(from);
	const TreeQuat b = normaliseQuat(to);
	const float d = std::fabs(a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w);
	const float angle = 2.0f * std::acos(clampFloat(d, -1.0f, 1.0f));
	if(angle <= 1.0e-7f)
		return b;
	return slerp(a, b, std::min(1.0f, std::max(0.0f, step) / angle));
}


// Exact Marsaglia RNG used by dgreenheck/ez-tree/src/lib/rng.js.
class EZTreeRNG
{
public:
	explicit EZTreeRNG(uint32_t seed)
	:	m_w((int32_t)(uint32_t(123456789u) + seed)),
		m_z((int32_t)(uint32_t(987654321u) - seed))
	{}

	float random(float max_v = 1.0f, float min_v = 0.0f)
	{
		const int32_t z_high = m_z >> 16;
		const int32_t w_high = m_w >> 16;
		m_z = (int32_t)(uint32_t(36969u * (uint32_t(m_z) & 65535u)) + uint32_t(z_high));
		m_w = (int32_t)(uint32_t(18000u * (uint32_t(m_w) & 65535u)) + uint32_t(w_high));
		const uint32_t result = (uint32_t(m_z) << 16) + (uint32_t(m_w) & 65535u);
		const double unit = (double)result / 4294967296.0;
		return (float)(((double)max_v - (double)min_v) * unit + (double)min_v);
	}

private:
	int32_t m_w;
	int32_t m_z;
};


int levelIndex(int level)
{
	return std::max(0, std::min(level, 3));
}


float levelLengthMetres(const TreeParams& p, int level)
{
	if(level == 0) return p.trunkHeight;
	if(level == 1) return p.branchLength;
	return p.branchLengthByLevel[levelIndex(level)];
}


float levelRadius(const TreeParams& p, int level)
{
	if(level == 0) return p.trunkRadius / EZ_TO_METRES;
	if(level == 1) return p.branchRadius;
	return p.branchRadiusByLevel[levelIndex(level)];
}


int levelSections(const TreeParams& p, int level)
{
	return level == 0 ? p.trunkSections : p.branchSectionsByLevel[levelIndex(level)];
}


int levelSegments(const TreeParams& p, int level)
{
	return level == 0 ? p.trunkSegments : p.branchSegmentsByLevel[levelIndex(level)];
}


float levelTaper(const TreeParams& p, int level)
{
	if(level == 0) return p.trunkTaper;
	if(level == 1) return p.branchTaper;
	return p.branchTaperByLevel[levelIndex(level)];
}


float levelTwist(const TreeParams& p, int level)
{
	if(level == 0) return p.trunkTwist;
	if(level == 1) return p.branchTwist;
	return p.branchTwistByLevel[levelIndex(level)];
}


float levelGnarliness(const TreeParams& p, int level)
{
	if(level == 0) return p.trunkCurve;
	if(level == 1) return p.branchGnarliness;
	return p.branchGnarlinessByLevel[levelIndex(level)];
}


float childAngleDegrees(const TreeParams& p, int child_level)
{
	return child_level == 1 ? p.branchAngle : p.branchAngleByLevel[levelIndex(child_level)];
}


float childStart(const TreeParams& p, int child_level)
{
	return child_level == 1 ? p.branchStartHeight : p.branchStartByLevel[levelIndex(child_level)];
}


int childCount(const TreeParams& p, int parent_level)
{
	return parent_level == 0 ? p.branchesPerLevel : p.branchChildrenByLevel[levelIndex(parent_level)];
}


float qualityFactor(const TreeParams& p)
{
	if(p.quality == TreeQuality::Low) return 0.40f;
	if(p.quality == TreeQuality::Medium) return 0.70f;
	return 1.0f;
}


int qualityCount(const TreeParams& p, int count)
{
	if(count <= 0)
		return 0;
	return std::max(1, (int)std::round((float)count * qualityFactor(p)));
}


std::vector<int> shuffledIndices(int count, EZTreeRNG& rng)
{
	std::vector<int> values((size_t)count);
	for(int i=0; i<count; ++i)
		values[(size_t)i] = i;
	for(int i=count - 1; i>0; --i)
	{
		const int r = std::min(i, (int)std::floor(rng.random() * (float)(i + 1)));
		std::swap(values[(size_t)i], values[(size_t)r]);
	}
	return values;
}


TreeVec3 nearestTrellisPointSource(const TreeParams& p, const TreeVec3& position)
{
	const TreeVec3 t = mul(engineToSource(p.trellisPosition), 1.0f / EZ_TO_METRES);
	const float width = p.trellisWidth / EZ_TO_METRES;
	const float height = p.trellisHeight / EZ_TO_METRES;
	const float spacing = std::max(0.001f, p.trellisSpacing / EZ_TO_METRES);
	const float min_x = t.x - width * 0.5f;
	const float max_x = t.x + width * 0.5f;
	const float min_y = t.y;
	const float max_y = t.y + height;
	const float clamped_x = clampFloat(position.x, min_x, max_x);
	const float clamped_y = clampFloat(position.y, min_y, max_y);
	const float h_y = clampFloat(std::round((clamped_y - min_y) / spacing) * spacing + min_y, min_y, max_y);
	const float v_x = clampFloat(std::round((clamped_x - min_x) / spacing) * spacing + min_x, min_x, max_x);
	const TreeVec3 on_h = makeVec3(clamped_x, h_y, t.z);
	const TreeVec3 on_v = makeVec3(v_x, clamped_y, t.z);
	return length(sub(position, on_h)) < length(sub(position, on_v)) ? on_h : on_v;
}


struct BranchSection
{
	TreeVec3 origin;
	TreeVec3 orientation;
	float radius;
};


struct SourceBranch
{
	TreeVec3 origin;
	TreeVec3 orientation;
	float branch_length;
	float radius;
	int level;
	int section_count;
	int segment_count;
};


void addLeaf(TreeMeshData& mesh, const TreeParams& p, EZTreeRNG& rng, const TreeVec3& origin, const TreeVec3& orientation)
{
	const float leaf_size = (p.leafSize / EZ_TO_METRES) *
		(1.0f + rng.random(p.leafSizeRandomness, -p.leafSizeRandomness));
	const float w = leaf_size;
	const float l = leaf_size;
	const int billboard_count = p.billboardMode == TreeBillboardMode::Single ? 1 : (p.billboardMode == TreeBillboardMode::MeshLeaves ? 3 : 2);
	const TreeQuat q_orientation = quatFromEuler(orientation);

	for(int billboard_i=0; billboard_i<billboard_count; ++billboard_i)
	{
		const float rotation = billboard_count == 2 ? billboard_i * PI * 0.5f : billboard_i * PI / 3.0f;
		const TreeQuat q_rotation = quatFromAxisAngle(makeVec3(0, 1, 0), rotation);
		const TreeQuat q = quatMultiply(q_orientation, q_rotation);
		const TreeVec3 local[4] = {
			makeVec3(-w * 0.5f, l, 0),
			makeVec3(-w * 0.5f, 0, 0),
			makeVec3( w * 0.5f, 0, 0),
			makeVec3( w * 0.5f, l, 0)
		};
		const float uv[4][2] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
		const TreeVec3 leaf_normal = normalise(applyQuat(q_orientation, makeVec3(0, 0, 1)), makeVec3(0, 0, 1));
		const uint32_t base = (uint32_t)mesh.vertices.size();
		for(int i=0; i<4; ++i)
		{
			TreeMeshVertex v;
			v.pos = add(origin, applyQuat(q, local[i]));
			v.normal = p.leafRoundedNormals ? normalise(add(leaf_normal, sub(v.pos, origin)), leaf_normal) : leaf_normal;
			v.u = uv[i][0];
			v.v = uv[i][1];
			v.material_index = 1;
			mesh.vertices.push_back(v);
		}
		mesh.indices.push_back(base); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
		mesh.indices.push_back(base); mesh.indices.push_back(base + 2); mesh.indices.push_back(base + 3);
	}
}


void addLeavesOnBranch(TreeMeshData& mesh, const TreeParams& p, EZTreeRNG& rng, const std::vector<BranchSection>& sections)
{
	const int count = p.leafType == TreeLeafType::None ? 0 : qualityCount(p, p.leafCount);
	if(count <= 0 || sections.size() < 2)
		return;
	const float radial_offset = rng.random();
	const float start_min = p.leafStart;
	const float height_step = (1.0f - start_min) / (float)count;
	const std::vector<int> angle_slots = shuffledIndices(count, rng);
	for(int i=0; i<count; ++i)
	{
		const float leaf_start = start_min + ((float)i + rng.random()) * height_step;
		const float section_pos = leaf_start * (float)(sections.size() - 1);
		const int section_i = std::max(0, std::min((int)sections.size() - 1, (int)std::floor(section_pos)));
		const BranchSection& a = sections[(size_t)section_i];
		const BranchSection& b = sections[(size_t)std::min((int)sections.size() - 1, section_i + 1)];
		const float alpha = section_pos - (float)section_i;
		const TreeVec3 origin = lerp(a.origin, b.origin, alpha);
		// Preserve upstream's qB.slerp(qA, alpha) ordering.
		const TreeQuat parent_q = slerp(quatFromEuler(b.orientation), quatFromEuler(a.orientation), alpha);
		const float radial_jitter = rng.random(0.5f, -0.5f) * p.branchRandomness;
		const float radial_angle = 2.0f * PI * (radial_offset + ((float)angle_slots[(size_t)i] + radial_jitter) / (float)count);
		const TreeQuat q_tilt = quatFromAxisAngle(makeVec3(1, 0, 0), p.leafAngle * PI / 180.0f);
		const TreeQuat q_radial = quatFromAxisAngle(makeVec3(0, 1, 0), radial_angle);
		const TreeVec3 orientation = eulerFromQuat(quatMultiply(parent_q, quatMultiply(q_radial, q_tilt)));
		addLeaf(mesh, p, rng, origin, orientation);
	}
}


void addChildBranches(
	const TreeParams& p,
	EZTreeRNG& rng,
	std::deque<SourceBranch>& queue,
	int count_,
	int level,
	const std::vector<BranchSection>& sections)
{
	const int count = qualityCount(p, count_);
	if(count <= 0 || sections.size() < 2)
		return;
	const float radial_offset = rng.random();
	const float start_min = childStart(p, level);
	const float height_step = (1.0f - start_min) / (float)count;
	const std::vector<int> angle_slots = shuffledIndices(count, rng);
	for(int i=0; i<count; ++i)
	{
		const float branch_start = start_min + ((float)i + rng.random()) * height_step;
		const float section_pos = branch_start * (float)(sections.size() - 1);
		const int section_i = std::max(0, std::min((int)sections.size() - 1, (int)std::floor(section_pos)));
		const BranchSection& a = sections[(size_t)section_i];
		const BranchSection& b = sections[(size_t)std::min((int)sections.size() - 1, section_i + 1)];
		const float alpha = section_pos - (float)section_i;
		const TreeVec3 origin = lerp(a.origin, b.origin, alpha);
		const float parent_radius = a.radius + (b.radius - a.radius) * alpha;
		const float radius = levelRadius(p, level) * parent_radius;
		const TreeQuat parent_q = slerp(quatFromEuler(b.orientation), quatFromEuler(a.orientation), alpha);
		const float radial_jitter = rng.random(0.5f, -0.5f) * p.branchRandomness;
		const float radial_angle = 2.0f * PI * (radial_offset + ((float)angle_slots[(size_t)i] + radial_jitter) / (float)count);
		const TreeQuat q_tilt = quatFromAxisAngle(makeVec3(1, 0, 0), childAngleDegrees(p, level) * PI / 180.0f);
		const TreeQuat q_radial = quatFromAxisAngle(makeVec3(0, 1, 0), radial_angle);
		const TreeVec3 orientation = eulerFromQuat(quatMultiply(parent_q, quatMultiply(q_radial, q_tilt)));
		float branch_length = levelLengthMetres(p, level) / EZ_TO_METRES;
		if(p.type == TreeType::Evergreen)
			branch_length *= 1.0f - branch_start;
		if(queue.size() < 16384)
			queue.push_back(SourceBranch{origin, orientation, branch_length, radius, level, levelSections(p, level), levelSegments(p, level)});
	}
}


void addCylinderBetween(TreeMeshData& mesh, const TreeVec3& a, const TreeVec3& b, float r0, float r1, int segments, int material_index)
{
	const TreeVec3 axis = normalise(sub(b, a), makeVec3(0, 0, 1));
	TreeVec3 side = normalise(cross(axis, std::fabs(axis.z) < 0.95f ? makeVec3(0, 0, 1) : makeVec3(0, 1, 0)), makeVec3(1, 0, 0));
	const TreeVec3 up = normalise(cross(axis, side), makeVec3(0, 1, 0));
	const uint32_t base = (uint32_t)mesh.vertices.size();
	for(int ring=0; ring<2; ++ring)
	{
		for(int j=0; j<=segments; ++j)
		{
			const float angle = 2.0f * PI * (float)j / (float)segments;
			const TreeVec3 radial = add(mul(side, std::cos(angle)), mul(up, std::sin(angle)));
			TreeMeshVertex v;
			v.pos = add(ring ? b : a, mul(radial, ring ? r1 : r0));
			v.normal = radial;
			v.u = (float)j / (float)segments;
			v.v = (float)ring;
			v.material_index = material_index;
			mesh.vertices.push_back(v);
		}
	}
	for(int j=0; j<segments; ++j)
	{
		const uint32_t i0 = base + (uint32_t)j;
		const uint32_t i1 = i0 + 1;
		const uint32_t i2 = base + (uint32_t)(segments + 1 + j);
		const uint32_t i3 = i2 + 1;
		mesh.indices.push_back(i0); mesh.indices.push_back(i2); mesh.indices.push_back(i1);
		mesh.indices.push_back(i1); mesh.indices.push_back(i2); mesh.indices.push_back(i3);
	}
}


void applyFlatBarkNormals(TreeMeshData& mesh)
{
	TreeMeshData flat;
	flat.vertices.reserve(mesh.indices.size());
	flat.indices.reserve(mesh.indices.size());
	for(size_t i=0; i+2<mesh.indices.size(); i += 3)
	{
		TreeMeshVertex a = mesh.vertices[mesh.indices[i]];
		TreeMeshVertex b = mesh.vertices[mesh.indices[i + 1]];
		TreeMeshVertex c = mesh.vertices[mesh.indices[i + 2]];
		if(a.material_index == 0)
		{
			const TreeVec3 face_n = normalise(cross(sub(b.pos, a.pos), sub(c.pos, a.pos)), a.normal);
			a.normal = b.normal = c.normal = face_n;
		}
		const uint32_t base = (uint32_t)flat.vertices.size();
		flat.vertices.push_back(a);
		flat.vertices.push_back(b);
		flat.vertices.push_back(c);
		flat.indices.push_back(base);
		flat.indices.push_back(base + 1);
		flat.indices.push_back(base + 2);
	}
	mesh = flat;
}

} // namespace


float TreeGenerator::random01(uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	return (float)((state >> 8) & 0x00ffffffu) / 16777216.0f;
}


float TreeGenerator::randomRange(uint32_t& state, float min_v, float max_v)
{
	return min_v + (max_v - min_v) * random01(state);
}


TreeMeshData TreeGenerator::generate(const TreeParams& params_)
{
	TreeParams params = params_;
	TreeSerialization::clamp(params);
	if(std::fabs(params.height - params.trunkHeight) > 0.001f)
	{
		const float s = params.height / std::max(0.001f, params.trunkHeight);
		const float radial_s = std::sqrt(std::max(0.05f, s));
		params.trunkHeight = params.height;
		params.trunkRadius *= radial_s;
		params.branchLength *= s;
		for(size_t i=0; i<params.branchLengthByLevel.size(); ++i)
			params.branchLengthByLevel[i] *= s;
		params.branchRadiusByLevel[0] *= radial_s;
		params.leafSize *= radial_s;
	}

	TreeMeshData mesh;
	generateTree(params, mesh); // Source coordinates: Y-up, source units.

	// Convert once to Substrata engine coordinates and metres.  ModelLoading
	// recognises the metasiberia_tree_ prefix and deliberately does not repeat
	// this conversion or generic OBJ auto-scaling.
	for(size_t i=0; i<mesh.vertices.size(); ++i)
	{
		const TreeVec3 p = mesh.vertices[i].pos;
		const TreeVec3 n = mesh.vertices[i].normal;
		mesh.vertices[i].pos = makeVec3(p.x * EZ_TO_METRES, -p.z * EZ_TO_METRES, p.y * EZ_TO_METRES);
		mesh.vertices[i].normal = normalise(makeVec3(n.x, -n.z, n.y), makeVec3(0, 0, 1));
	}

	generateTrellis(params, mesh); // Trellis is emitted directly in engine space.
	if(params.barkFlatShading)
		applyFlatBarkNormals(mesh);
	return mesh;
}


void TreeGenerator::generateTree(const TreeParams& params, TreeMeshData& mesh)
{
	EZTreeRNG rng(params.seed);
	std::deque<SourceBranch> queue;
	queue.push_back(SourceBranch{
		makeVec3(0, 0, 0),
		makeVec3(0, 0, 0),
		params.trunkHeight / EZ_TO_METRES,
		params.trunkRadius / EZ_TO_METRES,
		0,
		params.trunkSections,
		params.trunkSegments
	});

	size_t processed_count = 0;
	while(!queue.empty() && processed_count++ < 16384)
	{
		const SourceBranch branch = queue.front();
		queue.pop_front();
		const uint32_t index_offset = (uint32_t)mesh.vertices.size();
		TreeVec3 section_orientation = branch.orientation;
		TreeVec3 section_origin = branch.origin;
		// Match the deployed EZ-Tree behaviour.  Its runtime enum is the lower-case
		// string "deciduous", while the historical divisor check uses
		// "Deciduous"; therefore the website does not shorten these sections.
		const float section_length = branch.branch_length / (float)std::max(1, branch.section_count);
		std::vector<BranchSection> sections;
		sections.reserve((size_t)branch.section_count + 1);
		const int wraps_x = std::max(1, (int)std::round(branch.radius * params.barkTextureScaleX));
		const float bark_v_scale = 1.0f / std::max(0.05f, params.barkTextureScaleY);

		for(int i=0; i<=branch.section_count; ++i)
		{
			float section_radius = branch.radius;
			if(i == branch.section_count && branch.level == params.branchLevels)
				section_radius = 0.001f;
			else if(params.type == TreeType::Deciduous)
				section_radius *= 1.0f - levelTaper(params, branch.level) * ((float)i / (float)branch.section_count);
			else
				section_radius *= 1.0f - ((float)i / (float)branch.section_count);

			const TreeQuat q_orientation = quatFromEuler(section_orientation);
			TreeMeshVertex first;
			for(int j=0; j<branch.segment_count; ++j)
			{
				const float angle = 2.0f * PI * (float)j / (float)branch.segment_count;
				const TreeVec3 radial = makeVec3(std::cos(angle), 0, std::sin(angle));
				TreeMeshVertex v;
				v.pos = add(section_origin, mul(applyQuat(q_orientation, radial), section_radius));
				v.normal = normalise(applyQuat(q_orientation, radial), radial);
				v.u = ((float)j / (float)branch.segment_count) * (float)wraps_x;
				v.v = (i % 2 == 0 ? 0.0f : 1.0f) * bark_v_scale;
				v.material_index = 0;
				mesh.vertices.push_back(v);
				if(j == 0)
					first = v;
			}
			first.u = (float)wraps_x;
			mesh.vertices.push_back(first);
			sections.push_back(BranchSection{section_origin, section_orientation, section_radius});

			section_origin = add(section_origin, applyQuat(q_orientation, makeVec3(0, section_length, 0)));
			const float safe_radius = std::max(0.001f, std::fabs(section_radius));
			const float gnarliness = std::max(1.0f, 1.0f / std::sqrt(safe_radius)) * levelGnarliness(params, branch.level);
			section_orientation.x += rng.random(gnarliness, -gnarliness);
			section_orientation.z += rng.random(gnarliness, -gnarliness);

			TreeQuat q_section = quatFromEuler(section_orientation);
			q_section = quatMultiply(q_section, quatFromAxisAngle(makeVec3(0, 1, 0), levelTwist(params, branch.level)));
			const TreeVec3 section_up = normalise(applyQuat(q_section, makeVec3(0, 1, 0)));
			const TreeVec3 raw_target = engineToSource(params.branchForceDirection);
			if(length(raw_target) > 1.0e-6f)
			{
				const TreeVec3 target = normalise(raw_target);
				TreeVec3 axis = cross(section_up, target);
				const float sin_full = length(axis);
				if(sin_full > 1.0e-6f)
				{
					axis = mul(axis, 1.0f / sin_full);
					const float full_angle = std::atan2(sin_full, dot(section_up, target));
					const float step = params.branchForceStrength / safe_radius;
					const float clamped = clampFloat(step, -full_angle, full_angle);
					q_section = quatMultiply(quatFromAxisAngle(axis, clamped), q_section);
				}
			}
			if(params.trellisEnabled && params.trellisForceMaxDistance > 0.0f)
			{
				const TreeVec3 nearest = nearestTrellisPointSource(params, section_origin);
				const TreeVec3 to_trellis = sub(nearest, section_origin);
				const float distance = length(to_trellis);
				const float max_distance = params.trellisForceMaxDistance / EZ_TO_METRES;
				if(distance > 0.001f && distance <= max_distance)
				{
					const float distance_factor = 1.0f - std::pow(distance / max_distance, params.trellisForceFalloff);
					const float strength = params.trellisForceStrength * distance_factor / safe_radius;
					const TreeQuat q_trellis = quatFromUnitVectors(makeVec3(0, 1, 0), normalise(to_trellis));
					q_section = rotateTowards(q_section, q_trellis, strength);
				}
			}
			section_orientation = eulerFromQuat(q_section);
		}

		const int n = branch.segment_count + 1;
		for(int i=0; i<branch.section_count; ++i)
		{
			for(int j=0; j<branch.segment_count; ++j)
			{
				const uint32_t v1 = index_offset + (uint32_t)(i*n + j);
				const uint32_t v2 = v1 + 1;
				const uint32_t v3 = v1 + (uint32_t)n;
				const uint32_t v4 = v2 + (uint32_t)n;
				mesh.indices.push_back(v1); mesh.indices.push_back(v3); mesh.indices.push_back(v2);
				mesh.indices.push_back(v2); mesh.indices.push_back(v3); mesh.indices.push_back(v4);
			}
		}

		if(params.type == TreeType::Deciduous && !sections.empty())
		{
			const BranchSection& last = sections.back();
			if(branch.level < params.branchLevels)
			{
				queue.push_back(SourceBranch{
					last.origin,
					last.orientation,
					levelLengthMetres(params, branch.level + 1) / EZ_TO_METRES,
					last.radius,
					branch.level + 1,
					branch.section_count,
					branch.segment_count
				});
			}
			else if(params.leafType != TreeLeafType::None)
				addLeaf(mesh, params, rng, last.origin, last.orientation);
		}

		if(branch.level == params.branchLevels)
			addLeavesOnBranch(mesh, params, rng, sections);
		else if(branch.level < params.branchLevels)
			addChildBranches(params, rng, queue, childCount(params, branch.level), branch.level + 1, sections);
	}
}


void TreeGenerator::generateTrellis(const TreeParams& params, TreeMeshData& mesh)
{
	if(!params.trellisEnabled || !params.trellisVisible)
		return;
	const float spacing = std::max(0.05f, params.trellisSpacing);
	const int horizontal_count = std::min(256, (int)std::floor(params.trellisHeight / spacing) + 1);
	const int vertical_count = std::min(256, (int)std::floor(params.trellisWidth / spacing) + 1);
	const float min_x = params.trellisPosition.x - params.trellisWidth * 0.5f;
	// Externally this is an engine-space X/Z grid at a fixed engine Y.
	const float engine_y = params.trellisPosition.y;
	for(int i=0; i<horizontal_count; ++i)
	{
		const float z = params.trellisPosition.z + std::min(params.trellisHeight, (float)i * spacing);
		addCylinderBetween(mesh,
			makeVec3(min_x, engine_y, z),
			makeVec3(min_x + params.trellisWidth, engine_y, z),
			params.trellisCylinderRadius, params.trellisCylinderRadius, 8, 2);
	}
	for(int i=0; i<vertical_count; ++i)
	{
		const float x = min_x + std::min(params.trellisWidth, (float)i * spacing);
		addCylinderBetween(mesh,
			makeVec3(x, engine_y, params.trellisPosition.z),
			makeVec3(x, engine_y, params.trellisPosition.z + params.trellisHeight),
			params.trellisCylinderRadius, params.trellisCylinderRadius, 8, 2);
	}
}


std::string TreeGenerator::writeObjToTempFile(const TreeParams& params)
{
	const TreeMeshData mesh = generate(params);
	const std::string path = FileUtils::join(PlatformUtils::getTempDirPath(), "metasiberia_tree_" + std::to_string(params.seed) + ".obj");

	std::ostringstream s;
	s << std::setprecision(8);
	s << "# Metasiberia procedural tree, EZ-Tree-compatible generator v2\n";
	s << "mtllib metasiberia_tree.mtl\n";
	for(size_t i=0; i<mesh.vertices.size(); ++i)
		s << "v " << mesh.vertices[i].pos.x << " " << mesh.vertices[i].pos.y << " " << mesh.vertices[i].pos.z << "\n";
	for(size_t i=0; i<mesh.vertices.size(); ++i)
		s << "vt " << mesh.vertices[i].u << " " << mesh.vertices[i].v << "\n";
	for(size_t i=0; i<mesh.vertices.size(); ++i)
		s << "vn " << mesh.vertices[i].normal.x << " " << mesh.vertices[i].normal.y << " " << mesh.vertices[i].normal.z << "\n";

	int current_mat = -1;
	for(size_t i=0; i+2<mesh.indices.size(); i += 3)
	{
		const int mat = mesh.vertices[mesh.indices[i]].material_index;
		if(mat != current_mat)
		{
			current_mat = mat;
			s << "usemtl " << (current_mat == 0 ? "bark" : (current_mat == 1 ? "leaves" : "trellis")) << "\n";
		}
		const uint32_t a = mesh.indices[i] + 1;
		const uint32_t b = mesh.indices[i + 1] + 1;
		const uint32_t c = mesh.indices[i + 2] + 1;
		s << "f " << a << "/" << a << "/" << a << " " << b << "/" << b << "/" << b << " " << c << "/" << c << "/" << c << "\n";
	}

	FileUtils::writeEntireFileTextMode(path, s.str());
	return path;
}


int TreeGenerator::runSmokeCheck(const std::string& report_path)
{
	TreeParams a = TreePresets::presetById(TreePresets::defaultPresetId());
	a.seed = 12345;
	a.quality = TreeQuality::High;
	TreeSerialization::clamp(a);

	TreeParams b = a;
	TreeParams c = a;
	c.seed = 12346;
	TreeParams d = a;
	d.height *= 1.15f;

	const std::string path_a1 = writeObjToTempFile(a);
	const uint64 hash_a1 = FileChecksum::fileChecksum(path_a1);
	// Load the base mesh before the same-seed variants below overwrite the
	// deterministic temporary OBJ filename.
	const BatchedMeshRef base_lod_mesh = LODGeneration::loadModel(path_a1);
	const std::string path_a2 = writeObjToTempFile(b);
	const uint64 hash_a2 = FileChecksum::fileChecksum(path_a2);
	const std::string path_c = writeObjToTempFile(c);
	const uint64 hash_c = FileChecksum::fileChecksum(path_c);
	const std::string path_d = writeObjToTempFile(d);
	const uint64 hash_d = FileChecksum::fileChecksum(path_d);
	TreeParams branch_variant = a;
	branch_variant.branchAngle += 9.0f;
	const uint64 hash_branch_variant = FileChecksum::fileChecksum(writeObjToTempFile(branch_variant));
	TreeParams leaf_variant = a;
	leaf_variant.leafSize *= 1.25f;
	const uint64 hash_leaf_variant = FileChecksum::fileChecksum(writeObjToTempFile(leaf_variant));
	TreeParams trellis_variant = a;
	trellis_variant.trellisEnabled = true;
	trellis_variant.trellisVisible = true;
	const TreeMeshData trellis_mesh = generate(trellis_variant);
	bool trellis_material_present = false;
	for(size_t i=0; i<trellis_mesh.vertices.size(); ++i)
		trellis_material_present = trellis_material_present || trellis_mesh.vertices[i].material_index == 2;
	const uint64 hash_trellis_variant = FileChecksum::fileChecksum(writeObjToTempFile(trellis_variant));

	const BatchedMeshRef lod1_mesh = LODGeneration::computeLODModel(base_lod_mesh, 1);
	const BatchedMeshRef lod2_mesh = LODGeneration::computeLODModel(base_lod_mesh, 2);
	const bool lod_meshes_non_empty = lod1_mesh->numIndices() >= 3 && lod2_mesh->numIndices() >= 3;

	size_t preset_count = 0;
	const TreePresets::PresetInfo* presets = TreePresets::allPresets(preset_count);
	bool all_presets_non_empty = preset_count > 0;
	size_t min_preset_vertices = (size_t)-1;
	size_t max_preset_vertices = 0;
	for(size_t i=0; i<preset_count; ++i)
	{
		const TreeMeshData preset_mesh = generate(TreePresets::presetById(presets[i].id));
		all_presets_non_empty = all_presets_non_empty && !preset_mesh.vertices.empty() && !preset_mesh.indices.empty();
		min_preset_vertices = std::min(min_preset_vertices, preset_mesh.vertices.size());
		max_preset_vertices = std::max(max_preset_vertices, preset_mesh.vertices.size());
	}

	TreeParams aspen = TreePresets::presetById("aspen_large");
	TreeParams bush = TreePresets::presetById("bush_1");
	TreeParams pine = TreePresets::presetById("pine_large");
	TreeParams ash = TreePresets::presetById("ash_large");
	TreeParams trellis = TreePresets::presetById("trellis");
	TreeSerialization::clamp(aspen);
	TreeSerialization::clamp(bush);
	TreeSerialization::clamp(pine);
	TreeSerialization::clamp(ash);
	TreeSerialization::clamp(trellis);
	const bool exact_preset_values =
		aspen.branchAngle == 47.0f &&
		aspen.branchChildrenByLevel[0] == 10 &&
		std::fabs(aspen.branchRadiusByLevel[1] - 0.58f) < 1.0e-5f &&
		std::fabs(aspen.leafStart - 0.15217391f) < 1.0e-5f &&
		aspen.leafCount == 20 &&
		std::fabs(bush.trunkHeight - 0.02f) < 1.0e-5f &&
		std::fabs(bush.trunkRadius - 0.11586957f) < 1.0e-5f &&
		std::fabs(pine.branchAngle - 129.130435f) < 1.0e-5f &&
		std::fabs(ash.branchTaperByLevel[3]) < 1.0e-6f &&
		trellis.billboardMode == TreeBillboardMode::Single &&
		std::fabs(trellis.trellisPosition.y + 0.26f) < 1.0e-5f &&
		std::fabs(trellis.trellisWidth - 4.0f) < 1.0e-5f &&
		std::fabs(trellis.trellisHeight - 6.4f) < 1.0e-5f;

	const TreeMeshData mesh = generate(a);
	TreeVec3 min_p = mesh.vertices.empty() ? makeVec3(0, 0, 0) : mesh.vertices[0].pos;
	TreeVec3 max_p = min_p;
	bool bark_present = false;
	bool leaves_present = false;
	for(size_t i=0; i<mesh.vertices.size(); ++i)
	{
		const TreeMeshVertex& v = mesh.vertices[i];
		min_p.x = std::min(min_p.x, v.pos.x); min_p.y = std::min(min_p.y, v.pos.y); min_p.z = std::min(min_p.z, v.pos.z);
		max_p.x = std::max(max_p.x, v.pos.x); max_p.y = std::max(max_p.y, v.pos.y); max_p.z = std::max(max_p.z, v.pos.z);
		bark_present = bark_present || v.material_index == 0;
		leaves_present = leaves_present || v.material_index == 1;
	}
	const TreeVec3 dims = makeVec3(max_p.x - min_p.x, max_p.y - min_p.y, max_p.z - min_p.z);
	const bool same_seed_same_mesh = hash_a1 == hash_a2;
	const bool changed_seed_changes_mesh = hash_a1 != hash_c;
	const bool changed_param_changes_mesh = hash_a1 != hash_d;
	const bool branch_control_changes_mesh = hash_a1 != hash_branch_variant;
	const bool leaf_control_changes_mesh = hash_a1 != hash_leaf_variant;
	const bool trellis_changes_mesh = hash_a1 != hash_trellis_variant && trellis_material_present;
	const bool non_empty_mesh = !mesh.vertices.empty() && !mesh.indices.empty() && bark_present && leaves_present;
	const bool z_up = dims.z >= a.height * 0.80f && min_p.z > -a.leafSize * 1.1f;
	const bool metre_scale_preserved = dims.z > 4.0f;
	const bool generated_obj_prefix = hasPrefix(FileUtils::getFilename(path_a1), "metasiberia_tree_");
	const bool ok = same_seed_same_mesh && changed_seed_changes_mesh && changed_param_changes_mesh &&
		branch_control_changes_mesh && leaf_control_changes_mesh && trellis_changes_mesh &&
		lod_meshes_non_empty && all_presets_non_empty && exact_preset_values && non_empty_mesh &&
		z_up && metre_scale_preserved && generated_obj_prefix;
	// Leave the canonical base OBJ on disk for manual/visual inspection rather
	// than whichever same-seed variant happened to run last.
	writeObjToTempFile(a);

	std::ostringstream s;
	s << "{\n";
	s << "  \"ok\": " << (ok ? "true" : "false") << ",\n";
	s << "  \"generator_revision\": 2,\n";
	s << "  \"same_seed_same_mesh\": " << (same_seed_same_mesh ? "true" : "false") << ",\n";
	s << "  \"changed_seed_changes_mesh\": " << (changed_seed_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"changed_param_changes_mesh\": " << (changed_param_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"branch_control_changes_mesh\": " << (branch_control_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"leaf_control_changes_mesh\": " << (leaf_control_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"trellis_changes_mesh\": " << (trellis_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"lod_meshes_non_empty\": " << (lod_meshes_non_empty ? "true" : "false") << ",\n";
	s << "  \"lod1_index_count\": " << lod1_mesh->numIndices() << ",\n";
	s << "  \"lod2_index_count\": " << lod2_mesh->numIndices() << ",\n";
	s << "  \"all_presets_non_empty\": " << (all_presets_non_empty ? "true" : "false") << ",\n";
	s << "  \"exact_preset_values\": " << (exact_preset_values ? "true" : "false") << ",\n";
	s << "  \"preset_count\": " << preset_count << ",\n";
	s << "  \"min_preset_vertex_count\": " << min_preset_vertices << ",\n";
	s << "  \"max_preset_vertex_count\": " << max_preset_vertices << ",\n";
	s << "  \"non_empty_mesh\": " << (non_empty_mesh ? "true" : "false") << ",\n";
	s << "  \"z_up_mesh\": " << (z_up ? "true" : "false") << ",\n";
	s << "  \"metre_scale_preserved\": " << (metre_scale_preserved ? "true" : "false") << ",\n";
	s << "  \"generated_obj_prefix\": " << (generated_obj_prefix ? "true" : "false") << ",\n";
	s << "  \"aabb_dims\": [" << dims.x << ", " << dims.y << ", " << dims.z << "],\n";
	s << "  \"vertex_count\": " << mesh.vertices.size() << ",\n";
	s << "  \"index_count\": " << mesh.indices.size() << ",\n";
	s << "  \"hash_same_seed_a\": " << hash_a1 << ",\n";
	s << "  \"hash_same_seed_b\": " << hash_a2 << ",\n";
	s << "  \"hash_changed_seed\": " << hash_c << ",\n";
	s << "  \"hash_changed_param\": " << hash_d << "\n";
	s << "}\n";
	FileUtils::writeEntireFileTextMode(report_path, s.str());
	return ok ? 0 : 1;
}
