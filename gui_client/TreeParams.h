/*=====================================================================
TreeParams.h
------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <string>
#include <cstdint>


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
