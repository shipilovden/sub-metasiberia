// Standalone hard-limit and representative large-mesh checks. No server or world access.
// c++ -std=c++17 -O2 scripts/test_mcp_mesh_limits.cpp -o <temporary-path>
#include "../webserver/MCPMeshAdvanced.h"
#include <cassert>
#include <iostream>

using namespace MCPMeshOps;

static Mesh triangles(size_t count, bool reuse_geometry)
{
	Mesh mesh;
	mesh.reserve(count);
	for(size_t i = 0; i < count; ++i)
	{
		const double x = reuse_geometry ? 0.0 : (double)i * 3.0;
		Triangle t;
		t.vertices[0].position = Vec3(x, 0, 0);
		t.vertices[1].position = Vec3(x + 1, 0, 0);
		t.vertices[2].position = Vec3(x, 1, 0);
		for(Vertex& v : t.vertices)
		{
			v.normal = Vec3(0, 0, 1);
			v.u = v.position.x;
			v.v = v.position.y;
		}
		mesh.push_back(t);
	}
	return mesh;
}

template<class F> static void rejects(F f)
{
	bool rejected = false;
	try { f(); } catch(const std::runtime_error&) { rejected = true; }
	assert(rejected);
}

int main()
{
	static_assert(MAX_OPERATION_TRIANGLES == 100000, "Shared per-operation triangle ceiling");
	static_assert(MAX_BOOLEAN_INPUT_TRIANGLES == 100000, "Boolean input ceiling");
	static_assert(MAX_BOOLEAN_OUTPUT_TRIANGLES == 100000, "Boolean output ceiling");
	static_assert(Detail::MAX_INPUT_TRIANGLES == 100000, "Mesh editing input ceiling");
	static_assert(Detail::MAX_OUTPUT_TRIANGLES == 100000, "Mesh editing output ceiling");
	static_assert(AdvancedDetail::MAX_TRIANGLES == 100000, "Advanced mesh operation ceiling");

	// Exercise deformation above the historical 40,000-triangle limit.
	const Mesh over_40k = triangles(40001, false);
	Deformation twist;
	twist.mode = "twist";
	twist.min = -1;
	twist.max = 1;
	twist.amount = .2;
	const Mesh deformed = deformMesh(over_40k, twist);
	assert(deformed.size() == 40001);

	// UV editing accepts exactly 100,000 triangles, while the next triangle is rejected.
	const Mesh at_limit = triangles(MAX_OPERATION_TRIANGLES, true);
	UVEdit uv;
	uv.mode = "transform";
	uv.offset = {{.25, .5}};
	const Mesh transformed = editUV(at_limit, uv);
	assert(transformed.size() == MAX_OPERATION_TRIANGLES);
	assert(transformed.front().vertices.front().u == .25);
	assert(transformed.front().vertices.front().v == .5);
	const Mesh over_limit = triangles(MAX_OPERATION_TRIANGLES + 1, true);
	rejects([&] { editUV(over_limit, uv); });

	// A single subdivision can produce exactly the output ceiling.
	const Mesh subdivision_input = triangles(MAX_OPERATION_TRIANGLES / 4, false);
	Subdivision subdivision;
	subdivision.levels = 1;
	const Mesh subdivision_output = subdivideMesh(subdivision_input, subdivision);
	assert(subdivision_output.size() == MAX_OPERATION_TRIANGLES);
	const Mesh subdivision_over = triangles(MAX_OPERATION_TRIANGLES / 4 + 1, false);
	rejects([&] { subdivideMesh(subdivision_over, subdivision); });

	std::cout << "Mesh limits passed: 40,001-triangle deformation; 100,000-triangle UV edit and subdivision output; over-limit rejection.\n";
}
