/*=====================================================================
TreePresets.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TreePresets.h"


namespace TreePresets
{

TreeParams Oak()
{
	TreeParams p;
	p.preset = TreePresetType::Oak;
	p.name = "Oak";
	p.seed = 35729;
	p.height = 8.5f;
	p.trunkHeight = 8.5f;
	p.trunkRadius = 0.48f;
	p.trunkTaper = 0.70f;
	p.trunkCurve = 0.14f;
	p.trunkSegments = 12;
	p.trunkSections = 9;
	p.barkColor = {0.34f, 0.22f, 0.13f, 1.0f};
	p.branchLevels = 3;
	p.branchesPerLevel = 6;
	p.branchAngle = 48.0f;
	p.branchLength = 3.8f;
	p.branchRadius = 0.18f;
	p.branchTaper = 0.62f;
	p.branchCurve = 0.18f;
	p.branchRandomness = 0.36f;
	p.branchStartHeight = 0.32f;
	p.leafType = TreeLeafType::Oak;
	p.leafCount = 520;
	p.leafSize = 0.38f;
	p.leafColor = {0.20f, 0.52f, 0.16f, 1.0f};
	p.billboardMode = TreeBillboardMode::DoubleCross;
	return p;
}


TreeParams Birch()
{
	TreeParams p = Oak();
	p.preset = TreePresetType::Birch;
	p.name = "Birch";
	p.seed = 30631;
	p.height = 10.5f;
	p.trunkHeight = 10.5f;
	p.trunkRadius = 0.30f;
	p.trunkTaper = 0.58f;
	p.trunkCurve = 0.08f;
	p.barkColor = {0.78f, 0.76f, 0.67f, 1.0f};
	p.branchLevels = 3;
	p.branchesPerLevel = 5;
	p.branchAngle = 36.0f;
	p.branchLength = 3.0f;
	p.branchRadius = 0.11f;
	p.branchTaper = 0.72f;
	p.branchCurve = 0.24f;
	p.branchRandomness = 0.45f;
	p.branchStartHeight = 0.42f;
	p.leafType = TreeLeafType::Birch;
	p.leafCount = 430;
	p.leafSize = 0.28f;
	p.leafColor = {0.34f, 0.68f, 0.22f, 1.0f};
	return p;
}


TreeParams Pine()
{
	TreeParams p;
	p.preset = TreePresetType::Pine;
	p.name = "Pine";
	p.seed = 47512;
	p.height = 11.5f;
	p.trunkHeight = 11.5f;
	p.trunkRadius = 0.36f;
	p.trunkTaper = 0.40f;
	p.trunkCurve = 0.04f;
	p.trunkSegments = 10;
	p.trunkSections = 10;
	p.barkColor = {0.28f, 0.17f, 0.09f, 1.0f};
	p.branchLevels = 4;
	p.branchesPerLevel = 8;
	p.branchAngle = 72.0f;
	p.branchLength = 2.8f;
	p.branchRadius = 0.10f;
	p.branchTaper = 0.72f;
	p.branchCurve = 0.08f;
	p.branchRandomness = 0.22f;
	p.branchStartHeight = 0.18f;
	p.leafType = TreeLeafType::PineNeedles;
	p.leafCount = 700;
	p.leafSize = 0.30f;
	p.leafSizeRandomness = 0.18f;
	p.leafColor = {0.12f, 0.36f, 0.14f, 1.0f};
	p.billboardMode = TreeBillboardMode::DoubleCross;
	return p;
}


TreeParams DeadTree()
{
	TreeParams p = Oak();
	p.preset = TreePresetType::DeadTree;
	p.name = "Dead Tree";
	p.seed = 26867;
	p.height = 7.0f;
	p.trunkHeight = 7.0f;
	p.trunkRadius = 0.34f;
	p.trunkTaper = 0.55f;
	p.trunkCurve = 0.28f;
	p.barkColor = {0.18f, 0.15f, 0.12f, 1.0f};
	p.branchLevels = 3;
	p.branchesPerLevel = 4;
	p.branchAngle = 52.0f;
	p.branchLength = 3.0f;
	p.branchRadius = 0.13f;
	p.branchCurve = 0.42f;
	p.branchRandomness = 0.60f;
	p.leafType = TreeLeafType::None;
	p.leafCount = 0;
	return p;
}


TreeParams Bush()
{
	TreeParams p;
	p.preset = TreePresetType::Bush;
	p.name = "Bush";
	p.seed = 21431;
	p.height = 2.2f;
	p.scale = 1.0f;
	p.trunkHeight = 2.0f;
	p.trunkRadius = 0.18f;
	p.trunkTaper = 0.62f;
	p.trunkCurve = 0.25f;
	p.trunkSegments = 8;
	p.trunkSections = 5;
	p.barkColor = {0.26f, 0.18f, 0.10f, 1.0f};
	p.branchLevels = 2;
	p.branchesPerLevel = 9;
	p.branchAngle = 62.0f;
	p.branchLength = 1.8f;
	p.branchRadius = 0.08f;
	p.branchTaper = 0.72f;
	p.branchCurve = 0.30f;
	p.branchRandomness = 0.52f;
	p.branchStartHeight = 0.05f;
	p.leafType = TreeLeafType::Simple;
	p.leafCount = 620;
	p.leafSize = 0.24f;
	p.leafColor = {0.18f, 0.56f, 0.18f, 1.0f};
	p.collisionMode = TreeCollisionMode::None;
	return p;
}


TreeParams preset(TreePresetType type)
{
	switch(type)
	{
	case TreePresetType::Oak:      return Oak();
	case TreePresetType::Birch:    return Birch();
	case TreePresetType::Pine:     return Pine();
	case TreePresetType::DeadTree: return DeadTree();
	case TreePresetType::Bush:     return Bush();
	case TreePresetType::Custom:   break;
	}
	TreeParams p;
	p.preset = TreePresetType::Custom;
	return p;
}

} // namespace TreePresets

