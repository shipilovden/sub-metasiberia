/*=====================================================================
TreePresets.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TreePresets.h"


#include <algorithm>
#include <cmath>


namespace
{
static const TreePresets::PresetInfo PRESETS[] =
{
	{"ash_large", "Ash Large"},
	{"ash_medium", "Ash Medium"},
	{"ash_small", "Ash Small"},
	{"aspen_large", "Aspen Large"},
	{"aspen_medium", "Aspen Medium"},
	{"aspen_small", "Aspen Small"},
	{"bush_1", "Bush 1"},
	{"bush_2", "Bush 2"},
	{"bush_3", "Bush 3"},
	{"oak_large", "Oak Large"},
	{"oak_medium", "Oak Medium"},
	{"oak_small", "Oak Small"},
	{"pine_large", "Pine Large"},
	{"pine_medium", "Pine Medium"},
	{"pine_small", "Pine Small"},
	{"trellis", "Trellis"},
};


TreeColor colourFromHex(const unsigned int rgb)
{
	return TreeColor{
		(float)((rgb >> 16) & 0xff) / 255.0f,
		(float)((rgb >> 8) & 0xff) / 255.0f,
		(float)(rgb & 0xff) / 255.0f,
		1.0f
	};
}


TreeLeafType leafTypeFromName(const char* name)
{
	const std::string s(name ? name : "");
	if(s == "oak")
		return TreeLeafType::Oak;
	if(s == "aspen" || s == "ash")
		return TreeLeafType::Birch;
	if(s == "pine")
		return TreeLeafType::PineNeedles;
	return TreeLeafType::Simple;
}


TreeParams makeEZPreset(
	const char* id,
	const char* display_name,
	const uint32_t seed,
	const TreeType type,
	const char* bark_type,
	const unsigned int bark_tint,
	const float bark_scale_x,
	const float bark_scale_y,
	const int branch_levels,
	const float a1,
	const float a2,
	const float a3,
	const int c0,
	const int c1,
	const int c2,
	const float force_strength,
	const float g0,
	const float g1,
	const float g2,
	const float g3,
	const float l0,
	const float l1,
	const float l2,
	const float l3,
	const float r0,
	const float r1,
	const float r2,
	const float r3,
	const int s0,
	const int s1,
	const int s2,
	const int s3,
	const int seg0,
	const int seg1,
	const int seg2,
	const int seg3,
	const float start1,
	const float start2,
	const float start3,
	const float taper0,
	const float taper1,
	const float taper2,
	const float taper3,
	const float twist0,
	const float twist1,
	const float twist2,
	const float twist3,
	const char* leaf_type,
	const int leaf_count,
	const float leaf_angle,
	const float leaf_start,
	const float leaf_size,
	const float leaf_size_variance,
	const unsigned int leaf_tint,
	const float alpha_test,
	const bool trellis_enabled)
{
	const float unit_scale = 0.20f;
	TreeParams p;
	p.presetId = id;
	p.name = display_name;
	p.seed = seed;
	p.type = type;
	const std::string preset_id(id ? id : "");
	if(preset_id.find("oak_") == 0)
		p.preset = TreePresetType::Oak;
	else if(preset_id.find("aspen_") == 0 || preset_id.find("ash_") == 0)
		p.preset = TreePresetType::Birch;
	else if(preset_id.find("pine_") == 0)
		p.preset = TreePresetType::Pine;
	else if(preset_id.find("bush_") == 0)
		p.preset = TreePresetType::Bush;
	else
		p.preset = TreePresetType::Custom;
	p.height = std::max(1.2f, l0 * unit_scale);
	p.scale = 1.0f;

	p.trunkHeight = p.height;
	p.trunkRadius = std::max(0.08f, r0 * unit_scale);
	p.trunkTaper = taper0;
	p.trunkCurve = g0;
	p.trunkTwist = twist0;
	p.trunkSegments = seg0;
	p.trunkSections = s0;
	p.barkColor = colourFromHex(bark_tint);
	p.barkTextureType = bark_type;
	p.barkTextured = true;
	p.barkFlatShading = false;
	p.barkTextureScaleX = bark_scale_x;
	p.barkTextureScaleY = bark_scale_y;

	p.branchLevels = branch_levels;
	p.branchesPerLevel = c0;
	p.branchAngle = a1;
	p.branchLength = l1 * unit_scale;
	p.branchRadius = std::max(0.02f, r1 * unit_scale);
	p.branchTaper = taper1;
	p.branchCurve = g1;
	p.branchTwist = twist1;
	p.branchRandomness = 0.45f;
	p.branchStartHeight = start1;
	p.branchForceDirection = {0.0f, 0.0f, 1.0f};
	p.branchForceStrength = force_strength;
	p.branchGnarliness = std::max(0.0f, g1);
	p.branchAngleByLevel = {0.0f, a1, a2, a3};
	p.branchChildrenByLevel = {c0, c1, c2, 0};
	p.branchLengthByLevel = {l0 * unit_scale, l1 * unit_scale, l2 * unit_scale, l3 * unit_scale};
	p.branchRadiusByLevel = {std::max(0.08f, r0 * unit_scale), std::max(0.02f, r1 * unit_scale), std::max(0.02f, r2 * unit_scale), std::max(0.015f, r3 * unit_scale)};
	p.branchSectionsByLevel = {s0, s1, s2, s3};
	p.branchSegmentsByLevel = {seg0, seg1, seg2, seg3};
	p.branchStartByLevel = {0.0f, start1, start2, start3};
	p.branchTaperByLevel = {taper0, taper1, taper2, taper3};
	p.branchTwistByLevel = {twist0, twist1, twist2, twist3};
	p.branchGnarlinessByLevel = {std::max(0.0f, g0), std::max(0.0f, g1), std::max(0.0f, g2), std::max(0.0f, g3)};

	p.leafType = leafTypeFromName(leaf_type);
	p.leafCount = (leafTypeFromName(leaf_type) == TreeLeafType::PineNeedles) ? leaf_count * 30 : leaf_count * 45;
	p.leafAngle = leaf_angle;
	p.leafSize = std::max(0.08f, leaf_size * unit_scale);
	p.leafSizeRandomness = leaf_size_variance;
	p.leafColor = colourFromHex(leaf_tint);
	p.leafAlpha = 1.0f;
	p.leafAlphaTest = alpha_test;
	p.leafRoundedNormals = true;
	p.leafStartLevel = std::max(0, (int)std::floor(leaf_start * 4.0f));
	p.billboardMode = TreeBillboardMode::DoubleCross;

	p.trellisEnabled = trellis_enabled;
	p.trellisVisible = trellis_enabled;
	p.trellisWidth = 2.0f;
	p.trellisHeight = std::max(2.0f, p.height);
	p.trellisSpacing = 0.5f;
	p.trellisCylinderRadius = 0.025f;
	p.quality = TreeQuality::High;
	p.lodEnabled = true;
	p.collisionMode = TreeCollisionMode::TrunkOnly;
	p.castShadows = true;
	return p;
}
}


namespace TreePresets
{

const PresetInfo* allPresets(size_t& count)
{
	count = sizeof(PRESETS) / sizeof(PRESETS[0]);
	return PRESETS;
}


const char* defaultPresetId()
{
	return "ash_medium";
}


TreeParams presetById(const std::string& id)
{
	if(id == "ash_large") return makeEZPreset("ash_large", "Ash Large", 29919, TreeType::Deciduous, "Bark001", 0xceb0b0, 0.5f, 5.0f, 3, 42, 70, 65, 10, 4, 3, 0.01f, 0.03f, 0.25f, 0.20f, 0.09f, 45.0f, 29.42f, 10.4f, 5.1f, 2.1f, 0.68f, 0.78f, 0.72f, 12, 8, 6, 4, 12, 6, 4, 3, 0.23f, 0.33f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.09f, -0.07f, 0.0f, 0.0f, "ash", 22, 55, 0.0f, 3.0f, 0.72f, 0xffffff, 0.5f, false);
	if(id == "ash_medium") return makeEZPreset("ash_medium", "Ash Medium", 36330, TreeType::Deciduous, "Bark001", 0xceb0be, 0.5f, 5.0f, 3, 48, 75, 60, 7, 4, 3, 0.01f, 0.03f, 0.25f, 0.20f, 0.09f, 43.47f, 27.14f, 9.51f, 4.6f, 2.0f, 0.63f, 0.76f, 0.70f, 12, 8, 6, 4, 12, 6, 4, 3, 0.23f, 0.33f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.09f, -0.07f, 0.0f, 0.0f, "ash", 16, 55, 0.0f, 2.67f, 0.72f, 0xffffff, 0.5f, false);
	if(id == "ash_small") return makeEZPreset("ash_small", "Ash Small", 26867, TreeType::Deciduous, "Bark001", 0xceb0be, 0.5f, 5.0f, 2, 50, 65, 60, 10, 3, 3, 0.01f, 0.03f, 0.22f, 0.18f, 0.09f, 23.87f, 18.0f, 6.2f, 3.0f, 1.3f, 0.42f, 0.38f, 0.32f, 8, 6, 4, 3, 8, 5, 4, 3, 0.24f, 0.32f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.06f, -0.06f, 0.0f, 0.0f, "ash", 12, 55, 0.0f, 1.85f, 0.72f, 0xffffff, 0.5f, false);
	if(id == "aspen_large") return makeEZPreset("aspen_large", "Aspen Large", 30631, TreeType::Deciduous, "Bark002", 0xffffff, 1.0f, 8.0f, 2, 34, 52, 60, 10, 6, 0, 0.015f, 0.02f, 0.18f, 0.15f, 0.05f, 69.60f, 18.56f, 10.0f, 4.0f, 1.3f, 0.34f, 0.22f, 0.15f, 14, 8, 5, 3, 10, 6, 4, 3, 0.42f, 0.20f, 0.20f, 0.55f, 0.70f, 0.70f, 0.70f, 0.0f, 0.08f, 0.0f, 0.0f, "aspen", 22, 45, 0.0f, 2.2f, 0.55f, 0xa6d96a, 0.5f, false);
	if(id == "aspen_medium") return makeEZPreset("aspen_medium", "Aspen Medium", 18020, TreeType::Deciduous, "Bark002", 0xffffff, 1.0f, 8.0f, 2, 38, 55, 60, 10, 3, 3, 0.015f, 0.02f, 0.18f, 0.15f, 0.05f, 50.0f, 6.07f, 8.0f, 3.4f, 1.0f, 0.26f, 0.20f, 0.14f, 12, 7, 5, 3, 9, 5, 4, 3, 0.48f, 0.16f, 0.18f, 0.55f, 0.70f, 0.70f, 0.70f, 0.0f, 0.08f, 0.0f, 0.0f, "aspen", 17, 45, 0.0f, 1.8f, 0.55f, 0xa6d96a, 0.5f, false);
	if(id == "aspen_small") return makeEZPreset("aspen_small", "Aspen Small", 36330, TreeType::Deciduous, "Bark002", 0xffffff, 1.0f, 8.0f, 2, 40, 55, 60, 4, 3, 3, 0.015f, 0.02f, 0.18f, 0.15f, 0.05f, 23.99f, 3.36f, 6.0f, 2.8f, 0.72f, 0.18f, 0.16f, 0.12f, 8, 5, 4, 3, 7, 4, 3, 3, 0.45f, 0.16f, 0.18f, 0.55f, 0.70f, 0.70f, 0.70f, 0.0f, 0.08f, 0.0f, 0.0f, "aspen", 12, 45, 0.0f, 1.25f, 0.55f, 0xa6d96a, 0.5f, false);
	if(id == "bush_1") return makeEZPreset("bush_1", "Bush 1", 45590, TreeType::Deciduous, "Bark001", 0x9f7b50, 1.0f, 4.0f, 3, 72, 65, 50, 7, 3, 2, 0.025f, 0.25f, 0.35f, 0.30f, 0.10f, 0.10f, 15.30f, 8.0f, 3.0f, 0.35f, 0.24f, 0.16f, 0.10f, 4, 5, 4, 3, 6, 5, 4, 3, 0.02f, 0.15f, 0.15f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.1f, 0.0f, 0.0f, "ash", 22, 50, 0.0f, 1.0f, 0.55f, 0x3b8a2e, 0.5f, false);
	if(id == "bush_2") return makeEZPreset("bush_2", "Bush 2", 45590, TreeType::Deciduous, "Bark001", 0x9f7b50, 1.0f, 4.0f, 2, 75, 60, 50, 10, 3, 2, 0.025f, 0.25f, 0.30f, 0.25f, 0.10f, 0.10f, 19.65f, 6.0f, 2.5f, 0.32f, 0.22f, 0.14f, 0.10f, 4, 5, 4, 3, 6, 5, 4, 3, 0.02f, 0.15f, 0.15f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.1f, 0.0f, 0.0f, "ash", 24, 50, 0.0f, 1.0f, 0.55f, 0x3f8f35, 0.5f, false);
	if(id == "bush_3") return makeEZPreset("bush_3", "Bush 3", 31343, TreeType::Evergreen, "Bark001", 0x8a6845, 1.0f, 4.0f, 3, 70, 62, 48, 13, 4, 4, 0.020f, 0.20f, 0.26f, 0.24f, 0.09f, 10.96f, 21.82f, 8.0f, 3.0f, 0.40f, 0.24f, 0.14f, 0.10f, 5, 6, 4, 3, 6, 5, 4, 3, 0.02f, 0.12f, 0.12f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.1f, 0.0f, 0.0f, "pine", 26, 45, 0.0f, 0.85f, 0.35f, 0xffffff, 0.35f, false);
	if(id == "oak_large") return makeEZPreset("oak_large", "Oak Large", 23399, TreeType::Deciduous, "Bark001", 0xfff3d1, 1.0f, 10.0f, 3, 54, 58, 32, 9, 5, 3, 0.02f, 0.0f, 0.0f, 0.0f, 0.09f, 47.70f, 29.39f, 14.0f, 8.0f, 1.7f, 1.0f, 0.75f, 0.42f, 10, 7, 4, 2, 8, 6, 4, 3, 0.49f, 0.06f, 0.12f, 0.73f, 0.42f, 0.69f, 0.75f, -0.23f, 0.42f, 0.0f, 0.0f, "oak", 24, 42, 0.16f, 3.0f, 0.70f, 0xd5d04d, 0.5f, false);
	if(id == "oak_medium") return makeEZPreset("oak_medium", "Oak Medium", 35729, TreeType::Deciduous, "Bark001", 0xfff3d1, 1.0f, 10.0f, 3, 54, 58, 32, 6, 4, 3, 0.02f, 0.0f, 0.0f, 0.0f, 0.09f, 37.24f, 11.08f, 12.39f, 7.16f, 1.41f, 0.90f, 0.69f, 1.19f, 8, 6, 3, 1, 7, 5, 3, 3, 0.49f, 0.06f, 0.12f, 0.73f, 0.42f, 0.69f, 0.75f, -0.23f, 0.42f, 0.0f, 0.0f, "oak", 18, 42, 0.16f, 2.5f, 0.70f, 0xd5d04d, 0.5f, false);
	if(id == "oak_small") return makeEZPreset("oak_small", "Oak Small", 30895, TreeType::Deciduous, "Bark001", 0xfff3d1, 1.0f, 10.0f, 3, 52, 56, 32, 4, 2, 3, 0.02f, 0.0f, 0.0f, 0.0f, 0.09f, 28.08f, 4.55f, 8.0f, 4.0f, 1.0f, 0.50f, 0.35f, 0.24f, 7, 5, 3, 1, 7, 5, 3, 3, 0.42f, 0.06f, 0.12f, 0.73f, 0.42f, 0.69f, 0.75f, -0.23f, 0.42f, 0.0f, 0.0f, "oak", 12, 42, 0.16f, 1.8f, 0.70f, 0xd5d04d, 0.5f, false);
	if(id == "pine_large") return makeEZPreset("pine_large", "Pine Large", 44166, TreeType::Evergreen, "Bark003", 0xffffff, 1.0f, 1.0f, 1, 110, 16, 60, 100, 3, 0, -0.003f, 0.05f, 0.08f, 0.0f, 0.0f, 65.25f, 34.85f, 16.0f, 1.0f, 1.35f, 0.44f, 0.30f, 0.20f, 14, 10, 8, 6, 8, 6, 4, 3, 0.27f, 0.14f, 0.30f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "pine", 38, 39, 0.09f, 1.8f, 0.20f, 0xffffff, 0.3f, false);
	if(id == "pine_medium") return makeEZPreset("pine_medium", "Pine Medium", 13977, TreeType::Evergreen, "Bark003", 0xffffff, 1.0f, 1.0f, 1, 110, 16, 60, 82, 3, 5, -0.003f, 0.05f, 0.08f, 0.0f, 0.0f, 50.0f, 23.87f, 14.08f, 1.0f, 1.05f, 0.36f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.27f, 0.14f, 0.30f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "pine", 30, 39, 0.09f, 1.435f, 0.201f, 0xffffff, 0.3f, false);
	if(id == "pine_small") return makeEZPreset("pine_small", "Pine Small", 11744, TreeType::Evergreen, "Bark003", 0xffffff, 1.0f, 1.0f, 1, 108, 16, 60, 91, 7, 5, -0.003f, 0.05f, 0.08f, 0.0f, 0.0f, 39.55f, 12.12f, 8.0f, 1.0f, 0.82f, 0.28f, 0.30f, 0.20f, 10, 8, 6, 4, 8, 6, 4, 3, 0.27f, 0.14f, 0.30f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "pine", 24, 39, 0.09f, 1.1f, 0.201f, 0xffffff, 0.3f, false);
	if(id == "trellis") return makeEZPreset("trellis", "Trellis", 41563, TreeType::Deciduous, "Bark001", 0xb48b5c, 1.0f, 5.0f, 3, 70, 55, 45, 7, 5, 1, 0.05f, 0.15f, 0.20f, 0.18f, 0.05f, 4.80f, 16.90f, 8.0f, 3.0f, 0.45f, 0.25f, 0.16f, 0.10f, 5, 6, 4, 3, 6, 5, 4, 3, 0.05f, 0.15f, 0.12f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.05f, 0.0f, 0.0f, "ash", 20, 50, 0.0f, 1.1f, 0.55f, 0x4f8a35, 0.5f, true);
	return presetById(defaultPresetId());
}


TreeParams Oak()
{
	TreeParams p = presetById("oak_medium");
	p.preset = TreePresetType::Oak;
	return p;
}


TreeParams Birch()
{
	TreeParams p = presetById("aspen_medium");
	p.preset = TreePresetType::Birch;
	p.name = "Birch";
	return p;
}


TreeParams Pine()
{
	TreeParams p = presetById("pine_medium");
	p.preset = TreePresetType::Pine;
	return p;
}


TreeParams DeadTree()
{
	TreeParams p = presetById("ash_small");
	p.preset = TreePresetType::DeadTree;
	p.presetId = "dead_tree";
	p.name = "Dead Tree";
	p.leafType = TreeLeafType::None;
	p.leafCount = 0;
	p.leafColor = {0.35f, 0.28f, 0.20f, 1.0f};
	p.barkColor = {0.18f, 0.15f, 0.12f, 1.0f};
	return p;
}


TreeParams Bush()
{
	TreeParams p = presetById("bush_1");
	p.preset = TreePresetType::Bush;
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
	TreeParams p = presetById(defaultPresetId());
	p.preset = TreePresetType::Custom;
	p.presetId = "custom";
	return p;
}

} // namespace TreePresets
