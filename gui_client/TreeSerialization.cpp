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


namespace
{
static const char* const TREE_OBJECT_MARKER = "metasiberia_tree_object_v1";


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
	return TreePresets::Oak();
}


void clamp(TreeParams& p)
{
	p.seed = p.seed == 0 ? 1 : p.seed;
	p.name = p.name.empty() ? "Tree" : p.name.substr(0, 128);
	p.height = clampValue(p.height, 0.5f, 60.0f);
	p.scale = clampValue(p.scale, 0.05f, 20.0f);
	p.trunkHeight = clampValue(p.trunkHeight, 0.25f, 60.0f);
	p.trunkRadius = clampValue(p.trunkRadius, 0.02f, 5.0f);
	p.trunkTaper = clampValue(p.trunkTaper, 0.02f, 1.0f);
	p.trunkCurve = clampValue(p.trunkCurve, -2.0f, 2.0f);
	p.trunkTwist = clampValue(p.trunkTwist, -3.14159f, 3.14159f);
	p.trunkSegments = clampValue(p.trunkSegments, 3, 32);
	p.trunkSections = clampValue(p.trunkSections, 1, 48);

	p.branchLevels = clampValue(p.branchLevels, 0, 5);
	p.branchesPerLevel = clampValue(p.branchesPerLevel, 0, 16);
	p.branchAngle = clampValue(p.branchAngle, 0.0f, 120.0f);
	p.branchLength = clampValue(p.branchLength, 0.05f, 30.0f);
	p.branchRadius = clampValue(p.branchRadius, 0.01f, 3.0f);
	p.branchTaper = clampValue(p.branchTaper, 0.02f, 1.0f);
	p.branchCurve = clampValue(p.branchCurve, -2.0f, 2.0f);
	p.branchTwist = clampValue(p.branchTwist, -3.14159f, 3.14159f);
	p.branchRandomness = clampValue(p.branchRandomness, 0.0f, 2.0f);
	p.branchStartHeight = clampValue(p.branchStartHeight, 0.0f, 0.95f);

	p.leafCount = clampValue(p.leafCount, 0, p.quality == TreeQuality::High ? 2000 : (p.quality == TreeQuality::Medium ? 900 : 250));
	p.leafSize = clampValue(p.leafSize, 0.02f, 10.0f);
	p.leafSizeRandomness = clampValue(p.leafSizeRandomness, 0.0f, 1.0f);
	p.leafAlpha = clampValue(p.leafAlpha, 0.0f, 1.0f);
	p.leafStartLevel = clampValue(p.leafStartLevel, 0, 5);
}


TreeParams fromContent(const std::string& content, std::string* parse_error_out)
{
	TreeParams p = defaultParams();
	if(parse_error_out)
		parse_error_out->clear();

	if(!isTreeContent(content))
		return p;

	const size_t json_start = content.find('{');
	if(json_start == std::string::npos)
		return p;

	try
	{
		JSONParser parser;
		parser.parseBuffer(content.data() + json_start, content.size() - json_start);
		if(parser.nodes.empty())
			return p;

		const JSONNode& root = parser.nodes[0];
		if(root.type != JSONNode::Type_Object)
			return p;

		p.seed = (uint32_t)root.getChildIntValueWithDefaultVal(parser, "seed", (int)p.seed);
		p.preset = presetFromString(root.getChildStringValueWithDefaultVal(parser, "preset", presetToString(p.preset)));
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

		p.leafType = leafTypeFromString(root.getChildStringValueWithDefaultVal(parser, "leafType", leafTypeToString(p.leafType)));
		p.leafCount = root.getChildIntValueWithDefaultVal(parser, "leafCount", p.leafCount);
		p.leafSize = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafSize", p.leafSize);
		p.leafSizeRandomness = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafSizeRandomness", p.leafSizeRandomness);
		p.leafColor = readColour(parser, root, "leafColor", p.leafColor);
		p.leafAlpha = (float)root.getChildDoubleValueWithDefaultVal(parser, "leafAlpha", p.leafAlpha);
		p.leafStartLevel = root.getChildIntValueWithDefaultVal(parser, "leafStartLevel", p.leafStartLevel);
		p.billboardMode = billboardModeFromString(root.getChildStringValueWithDefaultVal(parser, "billboardMode", billboardModeToString(p.billboardMode)));

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
	s << "  \"schema_version\": 1,\n";
	s << "  \"preset\": \"" << presetToString(p.preset) << "\",\n";
	s << "  \"seed\": " << p.seed << ",\n";
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
	s << "  \"leafType\": \"" << leafTypeToString(p.leafType) << "\",\n";
	s << "  \"leafCount\": " << p.leafCount << ",\n";
	s << "  \"leafSize\": " << p.leafSize << ",\n";
	s << "  \"leafSizeRandomness\": " << p.leafSizeRandomness << ",\n";
	s << "  \"leafColor\": " << colourToJson(p.leafColor) << ",\n";
	s << "  \"leafAlpha\": " << p.leafAlpha << ",\n";
	s << "  \"leafStartLevel\": " << p.leafStartLevel << ",\n";
	s << "  \"billboardMode\": \"" << billboardModeToString(p.billboardMode) << "\",\n";
	s << "  \"quality\": \"" << qualityToString(p.quality) << "\",\n";
	s << "  \"lodEnabled\": " << (p.lodEnabled ? "true" : "false") << ",\n";
	s << "  \"collisionMode\": \"" << collisionModeToString(p.collisionMode) << "\",\n";
	s << "  \"castShadows\": " << (p.castShadows ? "true" : "false") << "\n";
	s << "}\n";
	return s.str();
}

} // namespace TreeSerialization
