/*=====================================================================
TreeGenerator.cpp
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TreeGenerator.h"
#include "TreePresets.h"
#include "TreeSerialization.h"


#include <utils/FileUtils.h>
#include <utils/FileChecksum.h>
#include <utils/PlatformUtils.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>


namespace
{
static const float PI = 3.14159265358979323846f;


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
	return len > 1.0e-6f ? mul(v, 1.0f / len) : fallback;
}


TreeVec3 lerp(const TreeVec3& a, const TreeVec3& b, float t)
{
	return add(a, mul(sub(b, a), t));
}


int qualitySegments(const TreeParams& p)
{
	const int requested = std::max(p.trunkSegments, p.branchSegmentsByLevel[0]);
	if(p.quality == TreeQuality::Low)
		return std::max(5, std::min(requested, 8));
	if(p.quality == TreeQuality::High)
		return std::max(8, std::min(requested + 4, 24));
	return std::max(6, std::min(requested, 14));
}


int qualitySections(const TreeParams& p)
{
	const int requested = std::max(p.trunkSections, p.branchSectionsByLevel[0]);
	if(p.quality == TreeQuality::Low)
		return std::max(3, std::min(requested, 6));
	if(p.quality == TreeQuality::High)
		return std::max(6, std::min(requested + 4, 24));
	return std::max(4, std::min(requested, 12));
}


int qualityLeafCount(const TreeParams& p)
{
	if(p.leafType == TreeLeafType::None)
		return 0;
	if(p.quality == TreeQuality::Low)
		return std::min(p.leafCount, 250);
	if(p.quality == TreeQuality::High)
		return std::min(p.leafCount, 2000);
	return std::min(p.leafCount, 900);
}


void addCylinderBetween(TreeMeshData& mesh, const TreeVec3& a, const TreeVec3& b, float r0, float r1, int segments, int sections, int material_index, float twist)
{
	segments = std::max(3, segments);
	sections = std::max(1, sections);

	const uint32_t base = (uint32_t)mesh.vertices.size();
	const TreeVec3 axis = normalise(sub(b, a));
	TreeVec3 tangent = normalise(cross(axis, makeVec3(0, 0, 1)), makeVec3(1, 0, 0));
	TreeVec3 bitangent = normalise(cross(axis, tangent), makeVec3(0, 0, 1));

	for(int i=0; i<=sections; ++i)
	{
		const float t = (float)i / (float)sections;
		const TreeVec3 center = lerp(a, b, t);
		const float radius = r0 + (r1 - r0) * t;
		const float twist_angle = twist * t;
		const float ct = std::cos(twist_angle);
		const float st = std::sin(twist_angle);
		const TreeVec3 ring_tangent = add(mul(tangent, ct), mul(bitangent, st));
		const TreeVec3 ring_bitangent = add(mul(tangent, -st), mul(bitangent, ct));

		for(int j=0; j<=segments; ++j)
		{
			const float a0 = (2.0f * PI * (float)j) / (float)segments;
			const TreeVec3 n = normalise(add(mul(ring_tangent, std::cos(a0)), mul(ring_bitangent, std::sin(a0))));
			TreeMeshVertex v;
			v.pos = add(center, mul(n, radius));
			v.normal = n;
			v.u = (float)j / (float)segments;
			v.v = t;
			v.material_index = material_index;
			mesh.vertices.push_back(v);
		}
	}

	const int stride = segments + 1;
	for(int i=0; i<sections; ++i)
	{
		for(int j=0; j<segments; ++j)
		{
			const uint32_t v0 = base + (uint32_t)(i * stride + j);
			const uint32_t v1 = v0 + 1;
			const uint32_t v2 = v0 + (uint32_t)stride;
			const uint32_t v3 = v2 + 1;
			mesh.indices.push_back(v0); mesh.indices.push_back(v2); mesh.indices.push_back(v1);
			mesh.indices.push_back(v1); mesh.indices.push_back(v2); mesh.indices.push_back(v3);
		}
	}
}


void addLeafQuad(TreeMeshData& mesh, const TreeVec3& center, float size, float angle, int material_index)
{
	const uint32_t base = (uint32_t)mesh.vertices.size();
	const float half = size * 0.5f;
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	const TreeVec3 right = makeVec3(c, 0, s);
	const TreeVec3 up = makeVec3(0, 1, 0);
	const TreeVec3 n = normalise(cross(right, up), makeVec3(0, 0, 1));

	const TreeVec3 p0 = add(add(center, mul(right, -half)), mul(up, size));
	const TreeVec3 p1 = add(center, mul(right, -half));
	const TreeVec3 p2 = add(center, mul(right, half));
	const TreeVec3 p3 = add(add(center, mul(right, half)), mul(up, size));

	const TreeVec3 pts[4] = {p0, p1, p2, p3};
	const float uvs[8] = {0, 1, 0, 0, 1, 0, 1, 1};
	for(int i=0; i<4; ++i)
	{
		TreeMeshVertex v;
		v.pos = pts[i];
		v.normal = n;
		v.u = uvs[i * 2];
		v.v = uvs[i * 2 + 1];
		v.material_index = material_index;
		mesh.vertices.push_back(v);
	}

	mesh.indices.push_back(base); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
	mesh.indices.push_back(base); mesh.indices.push_back(base + 2); mesh.indices.push_back(base + 3);
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
		params.trunkHeight = params.height;
		params.branchLength *= s;
		params.branchRadius *= std::sqrt(std::max(0.05f, s));
		params.leafSize *= std::sqrt(std::max(0.05f, s));
	}

	TreeMeshData mesh;
	std::vector<TreeVec3> branch_tips;
	generateTrunk(params, mesh, branch_tips);
	generateBranches(params, mesh, branch_tips);
	generateLeaves(params, mesh, branch_tips);
	return mesh;
}


void TreeGenerator::generateTrunk(const TreeParams& params, TreeMeshData& mesh, std::vector<TreeVec3>& branch_tips)
{
	const int segments = qualitySegments(params);
	const int sections = qualitySections(params);
	std::vector<TreeVec3> centers;
	centers.reserve((size_t)sections + 1);

	for(int i=0; i<=sections; ++i)
	{
		const float t = (float)i / (float)sections;
		const float bend = params.trunkCurve * std::sin(t * PI) * params.trunkHeight * 0.08f;
		centers.push_back(makeVec3(bend, t * params.trunkHeight, bend * 0.35f));
	}

	for(int i=0; i<sections; ++i)
	{
		const float t0 = (float)i / (float)sections;
		const float t1 = (float)(i + 1) / (float)sections;
		const float r0 = params.trunkRadius * (1.0f - (1.0f - params.trunkTaper) * t0);
		const float r1 = params.trunkRadius * (1.0f - (1.0f - params.trunkTaper) * t1);
		addCylinderBetween(mesh, centers[i], centers[i + 1], r0, r1, segments, 1, 0, params.trunkTwist);
	}

	branch_tips.push_back(centers.back());
}


void TreeGenerator::generateBranches(const TreeParams& params, TreeMeshData& mesh, std::vector<TreeVec3>& branch_tips)
{
	uint32_t rng = params.seed ^ 0x9e3779b9u;
	const int levels = std::max(0, params.branchLevels);

	for(int level=0; level<levels; ++level)
	{
		const int child_level = std::min(level + 1, 3);
		const int count = std::max(0, params.branchChildrenByLevel[std::min(level, 3)]);
		const float level_t = levels > 1 ? (float)level / (float)(levels - 1) : 0.0f;
		const float level_start = std::max(params.branchStartHeight, params.branchStartByLevel[child_level]);
		const float base_h = level_start + (1.0f - level_start) * (level_t * 0.78f);
		const int radial_segments = std::max(3, params.branchSegmentsByLevel[child_level]);
		const int branch_sections = std::max(1, params.branchSectionsByLevel[child_level] / 3);

		for(int i=0; i<count; ++i)
		{
			const float slot = (float)i / (float)std::max(1, count);
			const float h = std::min(0.96f, base_h + randomRange(rng, -0.06f, 0.10f));
			const float y = h * params.trunkHeight;
			const float curve = params.trunkCurve * std::sin(h * PI) * params.trunkHeight * 0.08f;
			const TreeVec3 start = makeVec3(curve, y, curve * 0.35f);
			const float around = 2.0f * PI * (slot + randomRange(rng, -0.20f, 0.20f));
			const float angle = (params.branchAngleByLevel[child_level] + randomRange(rng, -params.branchRandomness * 20.0f, params.branchRandomness * 20.0f)) * PI / 180.0f;
			TreeVec3 out = makeVec3(std::cos(around) * std::sin(angle), std::cos(angle), std::sin(around) * std::sin(angle));
			const TreeVec3 force_dir = normalise(params.branchForceDirection, makeVec3(0, 1, 0));
			out = normalise(add(out, mul(force_dir, params.branchForceStrength * (1.0f + (float)level))));
			const float len = params.branchLengthByLevel[child_level] * randomRange(rng, 1.0f - params.branchRandomness * 0.35f, 1.0f + params.branchRandomness * 0.35f);
			const TreeVec3 gnarl = makeVec3(
				randomRange(rng, -params.branchGnarlinessByLevel[child_level], params.branchGnarlinessByLevel[child_level]) * len * 0.12f,
				randomRange(rng, -params.branchGnarlinessByLevel[child_level], params.branchGnarlinessByLevel[child_level]) * len * 0.06f,
				randomRange(rng, -params.branchGnarlinessByLevel[child_level], params.branchGnarlinessByLevel[child_level]) * len * 0.12f
			);
			const TreeVec3 mid = add(add(start, mul(out, len * 0.55f)), gnarl);
			const TreeVec3 tip = add(start, add(add(mul(out, len), makeVec3(0, params.branchCurve * len * 0.25f, 0)), mul(gnarl, 0.55f)));
			const float r0 = params.branchRadiusByLevel[child_level];
			const float r1 = std::max(0.01f, r0 * params.branchTaperByLevel[child_level]);
			addCylinderBetween(mesh, start, mid, r0, (r0 + r1) * 0.5f, radial_segments, branch_sections, 0, params.branchTwistByLevel[child_level]);
			addCylinderBetween(mesh, mid, tip, (r0 + r1) * 0.5f, r1, radial_segments, branch_sections, 0, params.branchTwistByLevel[child_level]);
			branch_tips.push_back(tip);
		}
	}
}


void TreeGenerator::generateLeaves(const TreeParams& params, TreeMeshData& mesh, const std::vector<TreeVec3>& branch_tips)
{
	const int leaf_count = qualityLeafCount(params);
	if(leaf_count <= 0)
		return;

	uint32_t rng = params.seed ^ 0x85ebca6bu;
	const float crown_center_y = params.trunkHeight * (params.preset == TreePresetType::Bush ? 0.55f : 0.78f);
	const float crown_radius = std::max(params.branchLength * 0.75f, params.trunkRadius * 3.0f);
	const float crown_height = std::max(params.trunkHeight * 0.28f, params.leafSize * 2.0f);

	for(int i=0; i<leaf_count; ++i)
	{
		TreeVec3 center;
		if(!branch_tips.empty() && random01(rng) < 0.65f)
		{
			center = branch_tips[(size_t)(random01(rng) * (float)branch_tips.size()) % branch_tips.size()];
			center.x += randomRange(rng, -params.branchLength * 0.28f, params.branchLength * 0.28f);
			center.y += randomRange(rng, -params.leafSize, params.leafSize * 1.8f);
			center.z += randomRange(rng, -params.branchLength * 0.28f, params.branchLength * 0.28f);
		}
		else
		{
			const float a = randomRange(rng, 0.0f, 2.0f * PI);
			const float r = std::sqrt(random01(rng)) * crown_radius;
			center = makeVec3(std::cos(a) * r, crown_center_y + randomRange(rng, -crown_height * 0.5f, crown_height * 0.5f), std::sin(a) * r);
		}

		const float size = params.leafSize * randomRange(rng, 1.0f - params.leafSizeRandomness, 1.0f + params.leafSizeRandomness);
		const float angle = randomRange(rng, 0.0f, 2.0f * PI) + params.leafAngle * PI / 180.0f;
		addLeafQuad(mesh, center, size, angle, 1);
		if(params.billboardMode == TreeBillboardMode::DoubleCross)
			addLeafQuad(mesh, center, size, angle + PI * 0.5f, 1);
	}
}

std::string TreeGenerator::writeObjToTempFile(const TreeParams& params)
{
	const TreeMeshData mesh = generate(params);
	const std::string path = FileUtils::join(PlatformUtils::getTempDirPath(), "metasiberia_tree_" + std::to_string(params.seed) + ".obj");

	std::ostringstream s;
	s << std::setprecision(8);
	s << "# Metasiberia procedural tree\n";
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
			s << "usemtl " << (current_mat == 0 ? "bark" : "leaves") << "\n";
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
	d.trunkHeight = 10.0f;

	const std::string path_a1 = writeObjToTempFile(a);
	const uint64 hash_a1 = FileChecksum::fileChecksum(path_a1);
	const std::string path_a2 = writeObjToTempFile(b);
	const uint64 hash_a2 = FileChecksum::fileChecksum(path_a2);
	const std::string path_c = writeObjToTempFile(c);
	const uint64 hash_c = FileChecksum::fileChecksum(path_c);
	const std::string path_d = writeObjToTempFile(d);
	const uint64 hash_d = FileChecksum::fileChecksum(path_d);

	const TreeMeshData mesh = generate(a);
	const bool same_seed_same_mesh = hash_a1 == hash_a2;
	const bool changed_seed_changes_mesh = hash_a1 != hash_c;
	const bool changed_param_changes_mesh = hash_a1 != hash_d;
	const bool non_empty_mesh = !mesh.vertices.empty() && !mesh.indices.empty();
	const bool ok = same_seed_same_mesh && changed_seed_changes_mesh && changed_param_changes_mesh && non_empty_mesh;

	std::ostringstream s;
	s << "{\n";
	s << "  \"ok\": " << (ok ? "true" : "false") << ",\n";
	s << "  \"same_seed_same_mesh\": " << (same_seed_same_mesh ? "true" : "false") << ",\n";
	s << "  \"changed_seed_changes_mesh\": " << (changed_seed_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"changed_param_changes_mesh\": " << (changed_param_changes_mesh ? "true" : "false") << ",\n";
	s << "  \"non_empty_mesh\": " << (non_empty_mesh ? "true" : "false") << ",\n";
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
