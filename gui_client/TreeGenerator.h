/*=====================================================================
TreeGenerator.h
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "TreeParams.h"
#include <cstdint>
#include <string>
#include <vector>


struct TreeMeshVertex
{
	TreeVec3 pos;
	TreeVec3 normal;
	float u = 0.0f;
	float v = 0.0f;
	int material_index = 0;
};


struct TreeMeshData
{
	std::vector<TreeMeshVertex> vertices;
	std::vector<uint32_t> indices;
};


class TreeGenerator
{
public:
	static TreeMeshData generate(const TreeParams& params);
	static std::string writeObjToTempFile(const TreeParams& params);
	static int runSmokeCheck(const std::string& report_path);

	static float random01(uint32_t& state);
	static float randomRange(uint32_t& state, float min_v, float max_v);

private:
	static void generateTrunk(const TreeParams& params, TreeMeshData& mesh, std::vector<TreeVec3>& branch_tips);
	static void generateBranches(const TreeParams& params, TreeMeshData& mesh, std::vector<TreeVec3>& branch_tips);
	static void generateLeaves(const TreeParams& params, TreeMeshData& mesh, const std::vector<TreeVec3>& branch_tips);
	static void generateSelectionHull(const TreeParams& params, TreeMeshData& mesh);
};
