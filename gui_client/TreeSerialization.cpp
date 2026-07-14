/*=====================================================================
TreeSerialization.cpp
---------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TreeSerialization.h"
#include "TreePresets.h"


#include <JSONParser.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <type_traits>


namespace
{
static const char* const TREE_OBJECT_MARKER = "metasiberia_tree_object_v1";
static const int TREE_SCHEMA_VERSION = 2;


template <class T>
T clampValue(const T v, const T min_v, const T max_v)
{
	return std::max(min_v, std::min(max_v, v));
}


std::string jsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for(size_t i=0; i<s.size(); ++i)
	{
		const char c = s[i];
		if(c == '\\' || c == '"')
		{
			out.push_back('\\');
			out.push_back(c);
		}
		else if(c == '\n')
			out += "\\n";
		else if(c == '\r')
			out += "\\r";
		else if(c == '\t')
			out += "\\t";
		else
			out.push_back(c);
	}
	return out;
}


std::string colourToJson(const TreeColor& c)
{
	std::ostringstream s;
	s << std::setprecision(8) << "[" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << "]";
	return s.str();
}


std::string vec3ToJson(const TreeVec3& v)
{
	std::ostringstream s;
	s << std::setprecision(8) << "[" << v.x << ", " << v.y << ", " << v.z << "]";
	return s.str();
}


template <class T, size_t N>
std::string arrayToJson(const std::array<T, N>& a)
{
	std::ostringstream s;
	s << std::setprecision(8) << "[";
	for(size_t i=0; i<N; ++i)
	{
		if(i)
			s << ", ";
		s << a[i];
	}
	s << "]";
	return s.str();
}


template <class T, size_t N>
void readArray(const JSONParser& parser, const JSONNode& root, const char* name, std::array<T, N>& out)
{
	if(!root.hasChild(name))
		return;
	const JSONNode& n = root.getChildNode(parser, name);
	if(n.type != JSONNode::Type_Array)
		return;
	const size_t count = std::min<size_t>(N, n.child_indices.size());
	for(size_t i=0; i<count; ++i)
	{
		if constexpr(std::is_integral<T>::value)
			out[i] = (T)parser.nodes[n.child_indices[i]].getIntValue();
		else
			out[i] = (T)parser.nodes[n.child_indices[i]].getDoubleValue();
	}
}


TreeColor readColour(const JSONParser& parser, const JSONNode& root, const char* name, const TreeColor& fallback)
{
	if(!root.hasChild(name))
		return fallback;

	const JSONNode& n = root.getChildNode(parser, name);
	if(n.type != JSONNode::Type_Array || n.child_indices.size() < 3)
		return fallback;

	TreeColor c = fallback;
	c.r = (float)parser.nodes[n.child_indices[0]].getDoubleValue();
	c.g = (float)parser.nodes[n.child_indices[1]].getDoubleValue();
	c.b = (float)parser.nodes[n.child_indices[2]].getDoubleValue();
	if(n.child_indices.size() >= 4)
		c.a = (float)parser.nodes[n.child_indices[3]].getDoubleValue();
	return c;
}


TreeVec3 readVec3(const JSONParser& parser, const JSONNode& root, const char* name, const TreeVec3& fallback)
{
	if(!root.hasChild(name))
		return fallback;

	const JSONNode& n = root.getChildNode(parser, name);
	if(n.type != JSONNode::Type_Array || n.child_indices.size() < 3)
		return fallback;

	TreeVec3 v = fallback;
	v.x = (float)parser.nodes[n.child_indices[0]].getDoubleValue();
	v.y = (float)parser.nodes[n.child_indices[1]].getDoubleValue();
	v.z = (float)parser.nodes[n.child_indices[2]].getDoubleValue();
	return v;
}

} // namespace


namespace TreeSerialization
{

const char* contentMarker()
{
	return TREE_OBJECT_MARKER;
}


bool isTreeContent(const std::string& content)
{
	size_t i = 0;
	while(i < content.size() && std::isspace((unsigned char)content[i]))
		++i;

	return content.compare(i, std::strlen(TREE_OBJECT_MARKER), TREE_OBJECT_MARKER) == 0;
}


const char* presetToString(TreePresetType preset)
{
	switch(preset)
	{
	case TreePresetType::Oak:      return "oak";
	case TreePresetType::Birch:    return "birch";
	case TreePresetType::Pine:     return "pine";
	case TreePresetType::DeadTree: return "dead_tree";
	case TreePresetType::Bush:     return "bush";
	case TreePresetType::Custom:   return "custom";
	}
	return "custom";
}


TreePresetType presetFromString(const std::string& s)
{
	if(s == "oak") return TreePresetType::Oak;
	if(s == "birch") return TreePresetType::Birch;
	if(s == "pine") return TreePresetType::Pine;
	if(s == "dead_tree" || s == "dead") return TreePresetType::DeadTree;
	if(s == "bush") return TreePresetType::Bush;
	return TreePresetType::Custom;
}


const char* treeTypeToString(TreeType type)
{
	switch(type)
	{
	case TreeType::Deciduous: return "deciduous";
	case TreeType::Evergreen: return "evergreen";
	}
	return "deciduous";
}


TreeType treeTypeFromString(const std::string& s)
{
	if(s == "evergreen") return TreeType::Evergreen;
	return TreeType::Deciduous;
}


const char* leafTypeToString(TreeLeafType type)
{
	switch(type)
	{
	case TreeLeafType::Simple:      return "simple";
	case TreeLeafType::Oak:         return "oak";
	case TreeLeafType::Birch:       return "birch";
	case TreeLeafType::PineNeedles: return "pine_needles";
	case TreeLeafType::None:        return "none";
	}
	return "simple";
}


TreeLeafType leafTypeFromString(const std::string& s)
{
	if(s == "oak") return TreeLeafType::Oak;
	if(s == "birch") return TreeLeafType::Birch;
	if(s == "pine_needles" || s == "pine") return TreeLeafType::PineNeedles;
	if(s == "none") return TreeLeafType::None;
	return TreeLeafType::Simple;
}


const char* billboardModeToString(TreeBillboardMode mode)
{
	switch(mode)
	{
	case TreeBillboardMode::Single:      return "single";
	case TreeBillboardMode::DoubleCross: return "double_cross";
	case TreeBillboardMode::MeshLeaves:  return "mesh_leaves";
	}
	return "double_cross";
}


TreeBillboardMode billboardModeFromString(const std::string& s)
{
	if(s == "single") return TreeBillboardMode::Single;
	if(s == "mesh_leaves") return TreeBillboardMode::MeshLeaves;
	return TreeBillboardMode::DoubleCross;
}


const char* qualityToString(TreeQuality quality)
{
	switch(quality)
	{
	case TreeQuality::Low:    return "low";
	case TreeQuality::Medium: return "medium";
	case TreeQuality::High:   return "high";
	}
	return "medium";
}


TreeQuality qualityFromString(const std::string& s)
{
	if(s == "low") return TreeQuality::Low;
	if(s == "high") return TreeQuality::High;
	return TreeQuality::Medium;
}


const char* collisionModeToString(TreeCollisionMode mode)
{
	switch(mode)
	{
	case TreeCollisionMode::None:       return "none";
	case TreeCollisionMode::TrunkOnly:  return "trunk_only";
	case TreeCollisionMode::Simplified: return "simplified";
	}
	return "trunk_only";
}


TreeCollisionMode collisionModeFromString(const std::string& s)
{
	if(s == "none") return TreeCollisionMode::None;
	if(s == "simplified") return TreeCollisionMode::Simplified;
	return TreeCollisionMode::TrunkOnly;
}


TreeParams defaultParams()
{
	return TreePresets::presetById(TreePresets::defaultPresetId());
}


bool contentNeedsLegacyRepair(const std::string& content)
{
	if(!isTreeContent(content))
		return false;
	return
		(content.find("\"presetId\": \"custom\"") != std::string::npos || content.find("\"preset\": \"custom\"") != std::string::npos) &&
		(content.find("\"trunkHeight\": 2") != std::string::npos || content.find("\"trunkHeight\": 2.0") != std::string::npos) &&
		(content.find("\"trunkRadius\": 0.08") != std::string::npos || content.find("\"trunkRadius\": 0.080") != std::string::npos) &&
		(content.find("\"trunkTaper\": 0.1") != std::string::npos || content.find("\"trunkTaper\": 0.10") != std::string::npos);
}


void clamp(TreeParams& p)
{
	p.seed = p.seed == 0 ? 1 : p.seed;
	p.presetId = p.presetId.empty() ? TreePresets::defaultPresetId() : p.presetId.substr(0, 64);
	p.name = p.name.empty() ? "Tree" : p.name.substr(0, 128);
	p.height = clampValue(p.height, 0.01f, 60.0f);
	p.scale = clampValue(p.scale, 0.05f, 20.0f);
	p.trunkHeight = clampValue(p.trunkHeight, 0.01f, 60.0f);
	p.trunkRadius = clampValue(p.trunkRadius, 0.005f, 5.0f);
	p.trunkTaper = clampValue(p.trunkTaper, 0.0f, 1.0f);
	p.trunkCurve = clampValue(p.trunkCurve, -2.0f, 2.0f);
	p.trunkTwist = clampValue(p.trunkTwist, -3.14159f, 3.14159f);
	p.trunkSegments = clampValue(p.trunkSegments, 3, 32);
	p.trunkSections = clampValue(p.trunkSections, 1, 48);
	p.barkTextureType = p.barkTextureType.empty() ? "Bark001" : p.barkTextureType.substr(0, 32);
	p.barkTextureScaleX = clampValue(p.barkTextureScaleX, 0.05f, 50.0f);
	p.barkTextureScaleY = clampValue(p.barkTextureScaleY, 0.05f, 50.0f);

	p.branchLevels = clampValue(p.branchLevels, 0, 3);
	p.branchesPerLevel = clampValue(p.branchesPerLevel, 0, 128);
	p.branchAngle = clampValue(p.branchAngle, 0.0f, 180.0f);
	p.branchLength = clampValue(p.branchLength, 0.01f, 30.0f);
	p.branchRadius = clampValue(p.branchRadius, 0.01f, 3.0f);
	p.branchTaper = clampValue(p.branchTaper, 0.0f, 1.0f);
	p.branchCurve = clampValue(p.branchCurve, -2.0f, 2.0f);
	p.branchTwist = clampValue(p.branchTwist, -3.14159f, 3.14159f);
	p.branchRandomness = clampValue(p.branchRandomness, 0.0f, 2.0f);
	p.branchStartHeight = clampValue(p.branchStartHeight, 0.0f, 1.0f);
	p.branchForceStrength = clampValue(p.branchForceStrength, -1.0f, 1.0f);
	p.branchGnarliness = clampValue(p.branchGnarliness, -2.0f, 2.0f);
	for(size_t i=0; i<4; ++i)
	{
		p.branchAngleByLevel[i] = clampValue(p.branchAngleByLevel[i], 0.0f, 180.0f);
		p.branchChildrenByLevel[i] = clampValue(p.branchChildrenByLevel[i], 0, 128);
		p.branchLengthByLevel[i] = clampValue(p.branchLengthByLevel[i], 0.01f, 60.0f);
		p.branchRadiusByLevel[i] = clampValue(p.branchRadiusByLevel[i], 0.01f, 5.0f);
		p.branchSectionsByLevel[i] = clampValue(p.branchSectionsByLevel[i], 1, 48);
		p.branchSegmentsByLevel[i] = clampValue(p.branchSegmentsByLevel[i], 3, 32);
		p.branchStartByLevel[i] = clampValue(p.branchStartByLevel[i], 0.0f, 1.0f);
		p.branchTaperByLevel[i] = clampValue(p.branchTaperByLevel[i], 0.0f, 1.0f);
		p.branchTwistByLevel[i] = clampValue(p.branchTwistByLevel[i], -6.28318f, 6.28318f);
		p.branchGnarlinessByLevel[i] = clampValue(p.branchGnarlinessByLevel[i], -3.0f, 3.0f);
	}

	p.leafCount = clampValue(p.leafCount, 0, p.quality == TreeQuality::High ? 2000 : (p.quality == TreeQuality::Medium ? 900 : 250));
	p.leafAngle = clampValue(p.leafAngle, 0.0f, 100.0f);
	p.leafSize = clampValue(p.leafSize, 0.02f, 10.0f);
	p.leafSizeRandomness = clampValue(p.leafSizeRandomness, 0.0f, 1.0f);
	p.leafAlpha = clampValue(p.leafAlpha, 0.0f, 1.0f);
	p.leafAlphaTest = clampValue(p.leafAlphaTest, 0.0f, 1.0f);
	p.leafStart = clampValue(p.leafStart, 0.0f, 1.0f);
	p.leafStartLevel = clampValue(p.leafStartLevel, 0, 5);

	p.trellisWidth = clampValue(p.trellisWidth, 0.1f, 100.0f);
	p.trellisHeight = clampValue(p.trellisHeight, 0.1f, 100.0f);
	p.trellisSpacing = clampValue(p.trellisSpacing, 0.1f, 20.0f);
	p.trellisForceStrength = clampValue(p.trellisForceStrength, 0.0f, 2.0f);
	p.trellisForceMaxDistance = clampValue(p.trellisForceMaxDistance, 0.0f, 50.0f);
	p.trellisForceFalloff = clampValue(p.trellisForceFalloff, 0.1f, 8.0f);
	p.trellisCylinderRadius = clampValue(p.trellisCylinderRadius, 0.005f, 2.0f);
}


TreeParams fromContent(const std::string& content, std::string* parse_error_out, bool* legacy_repair_out, bool* mesh_upgrade_out)
{
	TreeParams p = defaultParams();
	if(parse_error_out)
		parse_error_out->clear();
	if(legacy_repair_out)
		*legacy_repair_out = false;
	if(mesh_upgrade_out)
		*mesh_upgrade_out = false;

	if(!isTreeContent(content))
		return p;

	const size_t json_start = content.find('{');
	if(json_start == std::string::npos)
		return p;

	int schema_version = 1;
	try
	{
		JSONParser parser;
		parser.parseBuffer(content.data() + json_start, content.size() - json_start);
		if(parser.nodes.empty())
			return p;

		const JSONNode& root = parser.nodes[0];
		if(root.type != JSONNode::Type_Object)
			return p;

		schema_version = root.getChildIntValueWithDefaultVal(parser, "schema_version", 1);
		p.seed = (uint32_t)root.getChildIntValueWithDefaultVal(parser, "seed", (int)p.seed);
		p.type = treeTypeFromString(root.getChildStringValueWithDefaultVal(parser, "treeType", treeTypeToString(p.type)));
		p.preset = presetFromString(root.getChildStringValueWithDefaultVal(parser, "preset", presetToString(p.preset)));
		p.presetId = root.getChildStringValueWithDefaultVal(parser, "presetId", p.presetId);
		p.name = root.getChildStringValueWithDefaultVal(parser, "name", p.name);
		p.height = (float)root.getChildDoubleValueWithDefaultVal(parser, "height", p.height);
		p.scale = (float)root.getChildDoubleValueWithDefaultVal(parser, "scale", p.scale);
		p.position = readVec3(parser, root, "position", p.position);
		p.rotation = readVec3(parser, root, "rotation", p.rotation);

		p.trunkHeight = (float)root.getChildDoubleValueWithDefaultVal(parser, "trunkHeight", p.trunkHeight);
		p.trunkRadius = (float)root.getChildDoubleValueWithDefaultVal(parser, "trunkRadius", p.trunkRadius);
		p.trunkTaper = (float)root.getChildDoubleValueWithDefaultVal(parser, "trunkTaper", p.trunkTaper);
		p.trunkCurve = (float)root.getChildDoubleValueWithDefaultVal(parser, "trunkCurve", p.trunkCurve);
		p.trunkTwist = (float)root.getChildDoubleValueWithDefaultVal(parser, "trunkTwist", p.trunkTwist);
		p.trunkSegments = root.getChildIntValueWithDefaultVal(parser, "trunkSegments", p.trunkSegments);
		p.trunkSections = root.getChildIntValueWithDefaultVal(parser, "trunkSections", p.trunkSections);
		p.barkColor = readColour(parser, root, "barkColor", p.barkColor);
		p.barkTextureType = root.getChildStringValueWithDefaultVal(parser, "barkTextureType", p.barkTextureType);
		p.barkTextured = root.getChildBoolValueWithDefaultVal(parser, "barkTextured", p.barkTextured);
		p.barkFlatShading = root.getChildBoolValueWithDefaultVal(parser, "barkFlatShading", p.barkFlatShading);
		p.barkTextureScaleX = (float)root.getChildDoubleValueWithDefaultVal(parser, "barkTextureScaleX", p.barkTextureScaleX);
		p.barkTextureScaleY = (float)root.getChildDoubleValueWithDefaultVal(parser, "barkTextureScaleY", p.barkTextureScaleY);

		p.branchLevels = root.getChildIntValueWithDefaultVal(parser, "branchLevels", p.branchLevels);
		p.branchesPerLevel = root.getChildIntValueWithDefaultVal(parser, "branchesPerLevel", p.branchesPerLevel);
		p.branchAngle = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchAngle", p.branchAngle);
		p.branchLength = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchLength", p.branchLength);
		p.branchRadius = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchRadius", p.branchRadius);
		p.branchTaper = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchTaper", p.branchTaper);
		p.branchCurve = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchCurve", p.branchCurve);
		p.branchTwist = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchTwist", p.branchTwist);
		p.branchRandomness = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchRandomness", p.branchRandomness);
		p.branchStartHeight = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchStartHeight", p.branchStartHeight);
		p.branchForceDirection = readVec3(parser, root, "branchForceDirection", p.branchForceDirection);
		p.branchForceStrength = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchForceStrength", p.branchForceStrength);
		p.branchGnarliness = (float)root.getChildDoubleValueWithDefaultVal(parser, "branchGnarliness", p.branchGnarliness);
		readArray(parser, root, "branchAngleByLevel", p.branchAngleByLevel);
		readArray(parser, root, "branchChildrenByLevel", p.branchChildrenByLevel);
		readArray(parser, root, "branchLengthByLevel", p.branchLengthByLevel);
		readArray(parser, root, "branchRadiusByLevel", p.branchRadiusByLevel);
		readArray(parser, root, "branchSectionsByLevel", p.branchSectionsByLevel);
		readArray(parser, root, "branchSegmentsByLevel", p.branchSegmentsByLevel);
		readArray(parser, root, "branchStartByLevel", p.branchStartByLevel);
		readArray(parser, root, "branchTaperByLevel", p.branchTaperByLevel);
		readArray(parser, root, "branchTwistByLevel", p.branchTwistByLevel);
		readArray(parser, root, "branchGnarlinessByLevel", p.branchGnarlinessByLevel);

		p.leafType = leafTypeFromString(root.getChildStringValueWithDefaultVal(parser, "leafType", leafTypeToString(p.leafType)));
		p.leafCount = root.getChildIntValueWithDefaultVal(parser, "leafCount", p.leafCount);
		p.leafAngle = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafAngle", p.leafAngle);
		p.leafSize = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafSize", p.leafSize);
		p.leafSizeRandomness = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafSizeRandomness", p.leafSizeRandomness);
		p.leafColor = readColour(parser, root, "leafColor", p.leafColor);
		p.leafAlpha = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafAlpha", p.leafAlpha);
		p.leafAlphaTest = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafAlphaTest", p.leafAlphaTest);
		p.leafRoundedNormals = root.getChildBoolValueWithDefaultVal(parser, "leafRoundedNormals", p.leafRoundedNormals);
		p.leafStart = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafStart", p.leafStart);
		p.leafStartLevel = root.getChildIntValueWithDefaultVal(parser, "leafStartLevel", p.leafStartLevel);
		p.billboardMode = billboardModeFromString(root.getChildStringValueWithDefaultVal(parser, "billboardMode", billboardModeToString(p.billboardMode)));

		p.trellisEnabled = root.getChildBoolValueWithDefaultVal(parser, "trellisEnabled", p.trellisEnabled);
		p.trellisPosition = readVec3(parser, root, "trellisPosition", p.trellisPosition);
		p.trellisWidth = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisWidth", p.trellisWidth);
		p.trellisHeight = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisHeight", p.trellisHeight);
		p.trellisSpacing = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisSpacing", p.trellisSpacing);
		p.trellisForceStrength = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisForceStrength", p.trellisForceStrength);
		p.trellisForceMaxDistance = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisForceMaxDistance", p.trellisForceMaxDistance);
		p.trellisForceFalloff = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisForceFalloff", p.trellisForceFalloff);
		p.trellisCylinderRadius = (float)root.getChildDoubleValueWithDefaultVal(parser, "trellisCylinderRadius", p.trellisCylinderRadius);
		p.trellisVisible = root.getChildBoolValueWithDefaultVal(parser, "trellisVisible", p.trellisVisible);
		p.trellisColor = readColour(parser, root, "trellisColor", p.trellisColor);

		p.quality = qualityFromString(root.getChildStringValueWithDefaultVal(parser, "quality", qualityToString(p.quality)));
		p.lodEnabled = root.getChildBoolValueWithDefaultVal(parser, "lodEnabled", p.lodEnabled);
		p.collisionMode = collisionModeFromString(root.getChildStringValueWithDefaultVal(parser, "collisionMode", collisionModeToString(p.collisionMode)));
		p.castShadows = root.getChildBoolValueWithDefaultVal(parser, "castShadows", p.castShadows);
	}
	catch(...)
	{
		if(parse_error_out)
			*parse_error_out = "Failed to parse tree object settings.";
		return defaultParams();
	}

	clamp(p);
	const bool needs_legacy_repair = contentNeedsLegacyRepair(content) ||
		(p.presetId == "custom" && p.trunkHeight <= 2.01f && p.trunkRadius <= 0.081f && p.trunkTaper <= 0.101f);
	if(legacy_repair_out)
		*legacy_repair_out = needs_legacy_repair;
	if(needs_legacy_repair)
	{
		const uint32_t seed = p.seed;
		const float height = p.height;
		const TreeVec3 position = p.position;
		const TreeVec3 rotation = p.rotation;
		const float scale = p.scale;
		p = TreePresets::presetById(TreePresets::defaultPresetId());
		p.seed = seed;
		if(height > 2.5f)
		{
			p.height = height;
			p.trunkHeight = height;
		}
		p.position = position;
		p.rotation = rotation;
		p.scale = scale;
		clamp(p);
	}
	else if(schema_version < TREE_SCHEMA_VERSION)
	{
		// Schema 1 contained hand-approximated preset values (including global
		// leaf counts and absolute child radii).  Upgrade known presets to the
		// exact upstream values while preserving object identity, size and the
		// user's material/optimisation choices.
		TreeParams exact = TreePresets::presetById(p.presetId);
		if(p.presetId != "custom" && exact.presetId == p.presetId)
		{
			const float height_scale = p.height / std::max(0.001f, exact.height);
			const float radial_scale = std::sqrt(std::max(0.05f, height_scale));
			exact.seed = p.seed;
			exact.name = p.name;
			exact.position = p.position;
			exact.rotation = p.rotation;
			exact.scale = p.scale;
			exact.height = p.height;
			exact.trunkHeight *= height_scale;
			exact.trunkRadius *= radial_scale;
			exact.branchLength *= height_scale;
			exact.branchLengthByLevel[0] = exact.trunkHeight;
			for(size_t i=1; i<exact.branchLengthByLevel.size(); ++i)
				exact.branchLengthByLevel[i] *= height_scale;
			exact.branchRadiusByLevel[0] = exact.trunkRadius;
			exact.leafSize *= radial_scale;
			exact.barkColor = p.barkColor;
			exact.barkTextured = p.barkTextured;
			exact.barkFlatShading = p.barkFlatShading;
			exact.leafAlpha = p.leafAlpha;
			exact.leafRoundedNormals = p.leafRoundedNormals;
			exact.quality = p.quality;
			exact.lodEnabled = p.lodEnabled;
			exact.collisionMode = p.collisionMode;
			exact.castShadows = p.castShadows;
			p = exact;
		}
		else
		{
			// Schema 1 custom trees stored every level radius as an absolute
			// engine-space metre value.  The EZ-Tree algorithm uses level 1..3
			// as multipliers of the interpolated parent radius.
			const std::array<float, 4> old_radii = p.branchRadiusByLevel;
			p.branchRadiusByLevel[0] = p.trunkRadius;
			for(size_t i=1; i<p.branchRadiusByLevel.size(); ++i)
				p.branchRadiusByLevel[i] = old_radii[i] / std::max(0.005f, old_radii[i - 1]);
			p.branchRadius = p.branchRadiusByLevel[1];
			// The old approximate generator read the per-level arrays, while the
			// new faithful port exposes convenient level-1 aliases as well.
			p.branchesPerLevel = p.branchChildrenByLevel[0];
			p.branchAngle = p.branchAngleByLevel[1];
			p.branchLength = p.branchLengthByLevel[1];
			p.branchTaper = p.branchTaperByLevel[1];
			p.branchTwist = p.branchTwistByLevel[1];
			p.branchGnarliness = p.branchGnarlinessByLevel[1];
			p.branchStartHeight = p.branchStartByLevel[1];
			const int old_multiplier = p.leafType == TreeLeafType::PineNeedles ? 30 : 45;
			p.leafCount = std::max(1, p.leafCount / old_multiplier);
			p.leafStart = 0.0f;
		}
		clamp(p);
	}
	if(mesh_upgrade_out)
		*mesh_upgrade_out = schema_version < TREE_SCHEMA_VERSION;
	return p;
}


std::string serialiseToContent(const TreeParams& params_)
{
	TreeParams p = params_;
	clamp(p);

	std::ostringstream s;
	s << TREE_OBJECT_MARKER << "\n";
	s << std::setprecision(8);
	s << "{\n";
	s << "  \"type\": \"tree\",\n";
	s << "  \"generator\": \"metasiberia_tree\",\n";
	s << "  \"schema_version\": " << TREE_SCHEMA_VERSION << ",\n";
	s << "  \"preset\": \"" << presetToString(p.preset) << "\",\n";
	s << "  \"presetId\": \"" << jsonEscape(p.presetId) << "\",\n";
	s << "  \"seed\": " << p.seed << ",\n";
	s << "  \"treeType\": \"" << treeTypeToString(p.type) << "\",\n";
	s << "  \"name\": \"" << jsonEscape(p.name) << "\",\n";
	s << "  \"position\": " << vec3ToJson(p.position) << ",\n";
	s << "  \"rotation\": " << vec3ToJson(p.rotation) << ",\n";
	s << "  \"scale\": " << p.scale << ",\n";
	s << "  \"height\": " << p.height << ",\n";
	s << "  \"trunkHeight\": " << p.trunkHeight << ",\n";
	s << "  \"trunkRadius\": " << p.trunkRadius << ",\n";
	s << "  \"trunkTaper\": " << p.trunkTaper << ",\n";
	s << "  \"trunkCurve\": " << p.trunkCurve << ",\n";
	s << "  \"trunkTwist\": " << p.trunkTwist << ",\n";
	s << "  \"trunkSegments\": " << p.trunkSegments << ",\n";
	s << "  \"trunkSections\": " << p.trunkSections << ",\n";
	s << "  \"barkColor\": " << colourToJson(p.barkColor) << ",\n";
	s << "  \"barkTextureType\": \"" << jsonEscape(p.barkTextureType) << "\",\n";
	s << "  \"barkTextured\": " << (p.barkTextured ? "true" : "false") << ",\n";
	s << "  \"barkFlatShading\": " << (p.barkFlatShading ? "true" : "false") << ",\n";
	s << "  \"barkTextureScaleX\": " << p.barkTextureScaleX << ",\n";
	s << "  \"barkTextureScaleY\": " << p.barkTextureScaleY << ",\n";
	s << "  \"branchLevels\": " << p.branchLevels << ",\n";
	s << "  \"branchesPerLevel\": " << p.branchesPerLevel << ",\n";
	s << "  \"branchAngle\": " << p.branchAngle << ",\n";
	s << "  \"branchLength\": " << p.branchLength << ",\n";
	s << "  \"branchRadius\": " << p.branchRadius << ",\n";
	s << "  \"branchTaper\": " << p.branchTaper << ",\n";
	s << "  \"branchCurve\": " << p.branchCurve << ",\n";
	s << "  \"branchTwist\": " << p.branchTwist << ",\n";
	s << "  \"branchRandomness\": " << p.branchRandomness << ",\n";
	s << "  \"branchStartHeight\": " << p.branchStartHeight << ",\n";
	s << "  \"branchForceDirection\": " << vec3ToJson(p.branchForceDirection) << ",\n";
	s << "  \"branchForceStrength\": " << p.branchForceStrength << ",\n";
	s << "  \"branchGnarliness\": " << p.branchGnarliness << ",\n";
	s << "  \"branchAngleByLevel\": " << arrayToJson(p.branchAngleByLevel) << ",\n";
	s << "  \"branchChildrenByLevel\": " << arrayToJson(p.branchChildrenByLevel) << ",\n";
	s << "  \"branchLengthByLevel\": " << arrayToJson(p.branchLengthByLevel) << ",\n";
	s << "  \"branchRadiusByLevel\": " << arrayToJson(p.branchRadiusByLevel) << ",\n";
	s << "  \"branchSectionsByLevel\": " << arrayToJson(p.branchSectionsByLevel) << ",\n";
	s << "  \"branchSegmentsByLevel\": " << arrayToJson(p.branchSegmentsByLevel) << ",\n";
	s << "  \"branchStartByLevel\": " << arrayToJson(p.branchStartByLevel) << ",\n";
	s << "  \"branchTaperByLevel\": " << arrayToJson(p.branchTaperByLevel) << ",\n";
	s << "  \"branchTwistByLevel\": " << arrayToJson(p.branchTwistByLevel) << ",\n";
	s << "  \"branchGnarlinessByLevel\": " << arrayToJson(p.branchGnarlinessByLevel) << ",\n";
	s << "  \"leafType\": \"" << leafTypeToString(p.leafType) << "\",\n";
	s << "  \"leafCount\": " << p.leafCount << ",\n";
	s << "  \"leafAngle\": " << p.leafAngle << ",\n";
	s << "  \"leafSize\": " << p.leafSize << ",\n";
	s << "  \"leafSizeRandomness\": " << p.leafSizeRandomness << ",\n";
	s << "  \"leafColor\": " << colourToJson(p.leafColor) << ",\n";
	s << "  \"leafAlpha\": " << p.leafAlpha << ",\n";
	s << "  \"leafAlphaTest\": " << p.leafAlphaTest << ",\n";
	s << "  \"leafRoundedNormals\": " << (p.leafRoundedNormals ? "true" : "false") << ",\n";
	s << "  \"leafStart\": " << p.leafStart << ",\n";
	s << "  \"leafStartLevel\": " << p.leafStartLevel << ",\n";
	s << "  \"billboardMode\": \"" << billboardModeToString(p.billboardMode) << "\",\n";
	s << "  \"trellisEnabled\": " << (p.trellisEnabled ? "true" : "false") << ",\n";
	s << "  \"trellisPosition\": " << vec3ToJson(p.trellisPosition) << ",\n";
	s << "  \"trellisWidth\": " << p.trellisWidth << ",\n";
	s << "  \"trellisHeight\": " << p.trellisHeight << ",\n";
	s << "  \"trellisSpacing\": " << p.trellisSpacing << ",\n";
	s << "  \"trellisForceStrength\": " << p.trellisForceStrength << ",\n";
	s << "  \"trellisForceMaxDistance\": " << p.trellisForceMaxDistance << ",\n";
	s << "  \"trellisForceFalloff\": " << p.trellisForceFalloff << ",\n";
	s << "  \"trellisCylinderRadius\": " << p.trellisCylinderRadius << ",\n";
	s << "  \"trellisVisible\": " << (p.trellisVisible ? "true" : "false") << ",\n";
	s << "  \"trellisColor\": " << colourToJson(p.trellisColor) << ",\n";
	s << "  \"quality\": \"" << qualityToString(p.quality) << "\",\n";
	s << "  \"lodEnabled\": " << (p.lodEnabled ? "true" : "false") << ",\n";
	s << "  \"collisionMode\": \"" << collisionModeToString(p.collisionMode) << "\",\n";
	s << "  \"castShadows\": " << (p.castShadows ? "true" : "false") << "\n";
	s << "}\n";
	return s.str();
}

} // namespace TreeSerialization
