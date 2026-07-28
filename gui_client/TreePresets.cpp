/*=====================================================================
TreePresets.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
// Preset parameters originate from dgreenheck/ez-tree commit
// 48dc193515135cff2b33515c47f0a8703b977e63.  See
// resources/tree_assets/EZ_TREE_LICENSE.txt for the upstream MIT license.
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
	if(s == "aspen")
		return TreeLeafType::Birch;
	if(s == "ash")
		return TreeLeafType::Simple;
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
	p.height = l0 * unit_scale;
	p.scale = 1.0f;

	p.trunkHeight = p.height;
	p.trunkRadius = r0 * unit_scale;
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
	// EZ-Tree stores radius[1..3] as multipliers of the interpolated parent
	// radius, not as world-space radii.  Only the root radius is converted to
	// Metasiberia metres.
	p.branchRadius = std::max(0.01f, r1);
	p.branchTaper = taper1;
	p.branchCurve = g1;
	p.branchTwist = twist1;
	p.branchRandomness = 1.0f;
	p.branchStartHeight = start1;
	// Upstream force=(0,1,0) is world-up in its Y-up space, hence (0,0,1)
	// in Metasiberia's externally visible Z-up controls.
	p.branchForceDirection = {0.0f, 0.0f, 1.0f};
	p.branchForceStrength = force_strength;
	p.branchGnarliness = g1;
	p.branchAngleByLevel = {0.0f, a1, a2, a3};
	p.branchChildrenByLevel = {c0, c1, c2, 0};
	p.branchLengthByLevel = {l0 * unit_scale, l1 * unit_scale, l2 * unit_scale, l3 * unit_scale};
	p.branchRadiusByLevel = {r0 * unit_scale, std::max(0.01f, r1), std::max(0.01f, r2), std::max(0.01f, r3)};
	p.branchSectionsByLevel = {s0, s1, s2, s3};
	p.branchSegmentsByLevel = {seg0, seg1, seg2, seg3};
	p.branchStartByLevel = {0.0f, start1, start2, start3};
	p.branchTaperByLevel = {taper0, taper1, taper2, taper3};
	p.branchTwistByLevel = {twist0, twist1, twist2, twist3};
	p.branchGnarlinessByLevel = {g0, g1, g2, g3};

	p.leafType = leafTypeFromName(leaf_type);
	// Count is per final-level branch in EZ-Tree.  Multiplying it into one
	// global pool was the main reason the old canopy looked like a brush.
	p.leafCount = leaf_count;
	p.leafAngle = leaf_angle;
	p.leafSize = leaf_size * unit_scale;
	p.leafSizeRandomness = leaf_size_variance;
	p.leafColor = colourFromHex(leaf_tint);
	p.leafAlpha = 1.0f;
	p.leafAlphaTest = alpha_test;
	p.leafRoundedNormals = true;
	p.leafStart = leaf_start;
	p.leafStartLevel = branch_levels;
	p.billboardMode = TreeBillboardMode::DoubleCross;

	p.trellisEnabled = trellis_enabled;
	p.trellisVisible = true;
	p.trellisPosition = {0.0f, 2.0f * unit_scale, 0.0f};
	p.trellisWidth = 2.0f;
	p.trellisHeight = 4.0f;
	p.trellisSpacing = 0.4f;
	p.trellisForceStrength = 0.02f;
	p.trellisForceMaxDistance = 0.6f;
	p.trellisForceFalloff = 1.0f;
	p.trellisCylinderRadius = 0.01f;
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
	// Values below are the upstream dgreenheck/ez-tree JSON presets.  Linear
	// dimensions are converted to Metasiberia metres in makeEZPreset; angular,
	// topology and relative-radius values remain byte-for-byte equivalents.
	if(id == "ash_large") return makeEZPreset("ash_large", "Ash Large", 29919, TreeType::Deciduous, "Bark001", 0xceccbe, 0.5f, 5.0f, 3, 39, 39, 51, 10, 4, 3, 0.01f, -0.05f, 0.20f, 0.16f, 0.05f, 45.0f, 29.42f, 15.3f, 4.6f, 3.03f, 0.53f, 0.79f, 1.11f, 12, 8, 6, 4, 8, 6, 4, 3, 0.32f, 0.34f, 0.0f, 0.70f, 0.62f, 0.76f, 0.0f, 0.09f, -0.07f, 0.0f, 0.0f, "ash", 10, 30, 0.01f, 4.62f, 0.72f, 0xffffff, 0.5f, false);
	if(id == "ash_medium") return makeEZPreset("ash_medium", "Ash Medium", 36330, TreeType::Deciduous, "Bark001", 0xceccbe, 0.5f, 5.0f, 3, 48, 75, 60, 7, 4, 3, 0.01f, 0.03f, 0.25f, 0.20f, 0.09f, 43.47f, 27.14f, 9.51f, 4.6f, 2.0f, 0.63f, 0.76f, 0.70f, 12, 8, 6, 4, 12, 6, 4, 3, 0.23f, 0.33f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.09f, -0.07f, 0.0f, 0.0f, "ash", 16, 55, 0.0f, 2.67f, 0.72f, 0xffffff, 0.5f, false);
	if(id == "ash_small") return makeEZPreset("ash_small", "Ash Small", 26867, TreeType::Deciduous, "Bark001", 0xceccbe, 0.5f, 5.0f, 2, 48, 75, 60, 10, 3, 3, 0.01f, 0.11f, 0.09f, 0.05f, 0.09f, 23.87f, 18.0f, 5.59f, 4.6f, 0.81f, 0.56f, 0.76f, 0.70f, 12, 10, 10, 10, 8, 6, 4, 3, 0.53f, 0.33f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.30f, -0.07f, 0.0f, 0.0f, "ash", 30, 55, 0.0f, 2.05f, 0.717f, 0xffffff, 0.5f, false);
	if(id == "aspen_large") return makeEZPreset("aspen_large", "Aspen Large", 30631, TreeType::Deciduous, "Bark002", 0xffffff, 1.0f, 1.0f, 2, 47, 63, 7, 10, 6, 0, 0.02173913f, 0.05f, -0.03f, 0.12f, 0.02f, 69.60f, 18.56f, 11.19f, 1.0f, 1.11f, 0.58f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.62f, 0.05f, 0.0f, 0.70f, 0.13f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "aspen", 20, 36, 0.15217391f, 3.47826087f, 0.70f, 0xfcff26, 0.5f, false);
	if(id == "aspen_medium") return makeEZPreset("aspen_medium", "Aspen Medium", 18020, TreeType::Deciduous, "Bark002", 0xffffff, 1.0f, 1.0f, 2, 75, 32, 7, 10, 3, 3, 0.0148f, 0.05f, 0.12f, 0.12f, 0.02f, 50.0f, 6.07f, 11.19f, 1.0f, 0.72f, 0.41f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.59f, 0.35f, 0.0f, 0.37f, 0.13f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "aspen", 11, 30, 0.124f, 2.5f, 0.70f, 0xfffa62, 0.5f, false);
	if(id == "aspen_small") return makeEZPreset("aspen_small", "Aspen Small", 36330, TreeType::Deciduous, "Bark002", 0xffffff, 1.0f, 1.0f, 2, 70, 35, 7, 4, 3, 3, 0.01086957f, 0.04f, -0.01f, 0.12f, 0.02f, 23.99f, 3.36f, 7.70f, 1.0f, 0.37f, 0.41f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.45f, 0.33f, 0.0f, 0.37f, 0.13f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "aspen", 13, 30, 0.20f, 2.5f, 0.70f, 0xfffa62, 0.5f, false);
	if(id == "bush_1") return makeEZPreset("bush_1", "Bush 1", 45590, TreeType::Deciduous, "Bark001", 0xceccbe, 0.5f, 5.0f, 3, 21.521739f, 62.608696f, 60, 7, 3, 2, 0.0f, 0.11f, 0.09f, 0.05f, 0.09f, 0.10f, 15.302174f, 5.59f, 4.6f, 0.579348f, 0.952174f, 0.76f, 0.70f, 6, 6, 10, 10, 4, 4, 4, 3, 0.53f, 0.33f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.30f, -0.07f, 0.0f, 0.0f, "ash", 12, 55, 0.0f, 2.445652f, 0.717f, 0xe0ffd5, 0.5f, false);
	if(id == "bush_2") return makeEZPreset("bush_2", "Bush 2", 45590, TreeType::Deciduous, "Bark001", 0xceccbe, 0.5f, 5.0f, 2, 19.565217f, 27.391304f, 60, 10, 3, 2, 0.0f, 0.021739f, 0.108696f, 0.05f, 0.09f, 0.10f, 19.645652f, 7.701087f, 4.6f, 0.579348f, 0.952174f, 0.76f, 0.70f, 3, 4, 10, 10, 4, 4, 4, 3, 0.641304f, 0.706522f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.358696f, -0.043478f, 0.0f, 0.0f, "aspen", 7, 55, 0.0f, 2.445652f, 0.717f, 0xe0ffd5, 0.5f, false);
	if(id == "bush_3") return makeEZPreset("bush_3", "Bush 3", 31343, TreeType::Evergreen, "Bark001", 0xceccbe, 0.5f, 5.0f, 3, 66.521739f, 52.826087f, 0, 13, 4, 4, 0.0f, 0.054348f, 0.065217f, 0.05f, 0.09f, 10.958696f, 21.817391f, 13.130435f, 5.529348f, 0.579348f, 0.952174f, 0.685870f, 0.739130f, 4, 3, 3, 10, 3, 3, 3, 3, 0.141304f, 0.293478f, 0.0f, 0.70f, 0.70f, 0.70f, 0.70f, 0.30f, -0.032609f, 0.0f, 0.0f, "pine", 3, 54, 0.152174f, 3.043478f, 0.456522f, 0x9dc3ff, 0.5f, false);
	if(id == "oak_large") return makeEZPreset("oak_large", "Oak Large", 23399, TreeType::Deciduous, "Bark001", 0xfff3d1, 1.0f, 10.0f, 3, 54, 43, 32, 9, 5, 3, 0.02f, -0.04f, 0.16f, -0.06f, 0.09f, 47.70f, 29.39f, 17.62f, 7.16f, 3.0f, 0.69f, 0.69f, 1.19f, 16, 9, 8, 3, 12, 5, 3, 3, 0.35f, 0.10f, 0.0f, 0.73f, 0.42f, 0.69f, 0.75f, -0.23f, 0.42f, 0.0f, 0.0f, "oak", 10, 36, 0.16f, 4.5f, 0.70f, 0xd5d5cd, 0.5f, false);
	if(id == "oak_medium") return makeEZPreset("oak_medium", "Oak Medium", 35729, TreeType::Deciduous, "Bark001", 0xfff3d1, 1.0f, 10.0f, 3, 54, 58, 32, 6, 4, 3, 0.02f, 0.0f, -0.10f, -0.15f, 0.09f, 37.24f, 11.08f, 12.39f, 7.16f, 1.41f, 0.90f, 0.69f, 1.19f, 8, 6, 3, 1, 7, 5, 3, 3, 0.49f, 0.06f, 0.12f, 0.73f, 0.42f, 0.69f, 0.75f, -0.23f, 0.42f, 0.0f, 0.0f, "oak", 18, 42, 0.16f, 2.5f, 0.70f, 0xd5d5cd, 0.5f, false);
	if(id == "oak_small") return makeEZPreset("oak_small", "Oak Small", 30895, TreeType::Deciduous, "Bark001", 0xfff3d1, 1.0f, 10.0f, 3, 54, 58, 32, 4, 2, 3, 0.01f, 0.07f, -0.08f, 0.11f, 0.09f, 28.08f, 4.55f, 9.78f, 7.16f, 1.0f, 1.02f, 0.69f, 1.19f, 16, 9, 8, 1, 7, 5, 3, 3, 0.49f, 0.06f, 0.12f, 0.73f, 0.42f, 0.69f, 0.75f, -0.23f, 0.42f, 0.0f, 0.0f, "oak", 14, 42, 0.16f, 1.38f, 0.70f, 0xd5d5cd, 0.5f, false);
	if(id == "pine_large") return makeEZPreset("pine_large", "Pine Large", 44166, TreeType::Evergreen, "Bark003", 0xffffff, 1.0f, 1.0f, 1, 129.130435f, 16, 60, 100, 3, 0, 0.009f, 0.05f, 0.08f, 0.0f, 0.0f, 65.252174f, 34.847826f, 27.246739f, 1.0f, 1.271739f, 0.366304f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.293478f, 0.14f, 0.30f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "pine", 18, 17, 0.076087f, 2.608696f, 0.201f, 0xffffff, 0.3f, false);
	if(id == "pine_medium") return makeEZPreset("pine_medium", "Pine Medium", 13977, TreeType::Evergreen, "Bark003", 0xffffff, 1.0f, 1.0f, 1, 110, 16, 60, 82, 3, 5, -0.003f, 0.05f, 0.08f, 0.0f, 0.0f, 50.0f, 23.87f, 14.08f, 1.0f, 1.05f, 0.36f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.27f, 0.14f, 0.30f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "pine", 30, 39, 0.09f, 1.435f, 0.201f, 0xffffff, 0.3f, false);
	if(id == "pine_small") return makeEZPreset("pine_small", "Pine Small", 11744, TreeType::Evergreen, "Bark003", 0xffffff, 1.0f, 1.0f, 1, 117, 60, 60, 91, 7, 5, 0.0f, 0.05f, 0.08f, 0.0f, 0.0f, 39.55f, 12.12f, 10.0f, 1.0f, 0.55f, 0.41f, 0.70f, 0.70f, 12, 10, 8, 6, 8, 6, 4, 3, 0.16f, 0.30f, 0.30f, 0.70f, 0.70f, 0.70f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, "pine", 21, 10, 0.0f, 0.965f, 0.70f, 0xffffff, 0.3f, false);
	if(id == "trellis")
	{
		TreeParams p = makeEZPreset("trellis", "Trellis", 41563, TreeType::Deciduous, "Bark001", 0xffffff, 1.0f, 8.0f, 3, 26, 79, 0, 7, 5, 1, 0.026f, 0.0f, 0.02f, -0.41f, 0.09f, 4.8f, 16.9f, 11.3f, 11.1f, 0.27f, 0.71f, 0.84f, 0.48f, 6, 12, 10, 4, 3, 3, 3, 3, 0.19f, 0.10f, 0.06f, 0.60f, 0.50f, 0.50f, 0.50f, -0.02f, -0.01f, 0.09f, 0.0f, "ash", 13, 30, 0.0f, 1.7f, 0.50f, 0xe7ffd6, 0.5f, true);
		p.billboardMode = TreeBillboardMode::Single;
		// Upstream source position (0,0,1.3) converted to engine Z-up metres.
		p.trellisPosition = {0.0f, -1.3f * 0.20f, 0.0f};
		p.trellisWidth = 20.0f * 0.20f;
		p.trellisHeight = 32.0f * 0.20f;
		p.trellisSpacing = 4.0f * 0.20f;
		p.trellisForceStrength = 0.014f;
		p.trellisForceMaxDistance = 18.2f * 0.20f;
		p.trellisForceFalloff = 1.3f;
		p.trellisCylinderRadius = 0.08f * 0.20f;
		p.trellisColor = colourFromHex(0x543745);
		return p;
	}
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
