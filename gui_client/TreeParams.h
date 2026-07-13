/*=====================================================================
TreeParams.h
------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <string>
#include <cstdint>
#include <array>


struct TreeVec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};


struct TreeColor
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};


enum class TreePresetType
{
	Oak,
	Birch,
	Pine,
	DeadTree,
	Bush,
	Custom
};


enum class TreeLeafType
{
	Simple,
	Oak,
	Birch,
	PineNeedles,
	None
};


enum class TreeBillboardMode
{
	Single,
	DoubleCross,
	MeshLeaves
};


enum class TreeQuality
{
	Low,
	Medium,
	High
};


enum class TreeCollisionMode
{
	None,
	TrunkOnly,
	Simplified
};

enum class TreeType
{
	Deciduous,
	Evergreen
};


struct TreeParams
{
	uint32_t seed = 12345;
	TreeType type = TreeType::Deciduous;
	TreePresetType preset = TreePresetType::Oak;
	std::string name = "Tree";

	float height = 8.0f;
	float scale = 1.0f;

	TreeVec3 position {0.0f, 0.0f, 0.0f};
	TreeVec3 rotation {0.0f, 0.0f, 0.0f};

	float trunkHeight = 8.0f;
	float trunkRadius = 0.4f;
	float trunkTaper = 0.75f;
	float trunkCurve = 0.15f;
	float trunkTwist = 0.0f;
	int trunkSegments = 12;
	int trunkSections = 8;

	TreeColor barkColor {0.35f, 0.20f, 0.10f, 1.0f};
	std::string barkTextureType = "Bark001";
	bool barkTextured = true;
	bool barkFlatShading = false;
	float barkTextureScaleX = 1.0f;
	float barkTextureScaleY = 1.0f;

	int branchLevels = 3;
	int branchesPerLevel = 5;
	float branchAngle = 35.0f;
	float branchLength = 3.5f;
	float branchRadius = 0.16f;
	float branchTaper = 0.65f;
	float branchCurve = 0.2f;
	float branchTwist = 0.1f;
	float branchRandomness = 0.35f;
	float branchStartHeight = 0.25f;
	TreeVec3 branchForceDirection {0.0f, 1.0f, 0.0f};
	float branchForceStrength = 0.01f;
	float branchGnarliness = 0.2f;
	std::array<float, 4> branchAngleByLevel {0.0f, 70.0f, 60.0f, 60.0f};
	std::array<int, 4> branchChildrenByLevel {7, 7, 5, 0};
	std::array<float, 4> branchLengthByLevel {8.0f, 4.0f, 2.0f, 0.8f};
	std::array<float, 4> branchRadiusByLevel {0.45f, 0.18f, 0.10f, 0.04f};
	std::array<int, 4> branchSectionsByLevel {10, 8, 6, 4};
	std::array<int, 4> branchSegmentsByLevel {10, 8, 6, 4};
	std::array<float, 4> branchStartByLevel {0.0f, 0.40f, 0.30f, 0.30f};
	std::array<float, 4> branchTaperByLevel {0.70f, 0.70f, 0.70f, 0.70f};
	std::array<float, 4> branchTwistByLevel {0.0f, 0.0f, 0.0f, 0.0f};
	std::array<float, 4> branchGnarlinessByLevel {0.15f, 0.20f, 0.30f, 0.02f};

	TreeLeafType leafType = TreeLeafType::Simple;
	int leafCount = 500;
	float leafAngle = 10.0f;
	float leafSize = 0.35f;
	float leafSizeRandomness = 0.25f;
	TreeColor leafColor {0.20f, 0.60f, 0.18f, 1.0f};
	float leafAlpha = 1.0f;
	float leafAlphaTest = 0.5f;
	bool leafRoundedNormals = true;
	int leafStartLevel = 1;
	TreeBillboardMode billboardMode = TreeBillboardMode::DoubleCross;

	bool trellisEnabled = false;
	TreeVec3 trellisPosition {0.0f, -2.0f, 0.0f};
	float trellisWidth = 10.0f;
	float trellisHeight = 20.0f;
	float trellisSpacing = 2.0f;
	float trellisForceStrength = 0.02f;
	float trellisForceMaxDistance = 3.0f;
	float trellisForceFalloff = 1.0f;
	float trellisCylinderRadius = 0.05f;
	bool trellisVisible = true;
	TreeColor trellisColor {0.55f, 0.27f, 0.07f, 1.0f};

	TreeQuality quality = TreeQuality::Medium;
	bool lodEnabled = true;
	TreeCollisionMode collisionMode = TreeCollisionMode::TrunkOnly;
	bool castShadows = true;
};
