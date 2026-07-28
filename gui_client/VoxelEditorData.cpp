/*=====================================================================
VoxelEditorData.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelEditorData.h"
#include "../shared/WorldObject.h"


#include <Exception.h>
#include <JSONParser.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>


namespace
{
static const char* const VOXEL_EDITOR_MARKER = "metasiberia_voxel_editor_v1";
static const int VOXEL_EDITOR_SCHEMA_VERSION = 1;


template <class T>
T clampValue(const T value, const T min_value, const T max_value)
{
	return std::max(min_value, std::min(max_value, value));
}


std::string sanitiseLayerName(const std::string& value, const size_t fallback_index)
{
	std::string result = value.empty() ? ("Layer " + std::to_string(fallback_index + 1)) : value.substr(0, 64);
	for(char& c : result)
		if(static_cast<unsigned char>(c) < 32)
			c = ' ';
	return result;
}


std::string jsonEscape(const std::string& value)
{
	std::string result;
	result.reserve(value.size() + 8);
	static const char hex[] = "0123456789abcdef";
	for(const unsigned char c : value)
	{
		switch(c)
		{
		case '\\': result += "\\\\"; break;
		case '"':  result += "\\\""; break;
		case '\b': result += "\\b"; break;
		case '\f': result += "\\f"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:
			if(c < 32)
			{
				result += "\\u00";
				result.push_back(hex[(c >> 4) & 0xF]);
				result.push_back(hex[c & 0xF]);
			}
			else
				result.push_back(static_cast<char>(c));
			break;
		}
	}
	return result;
}


uint8_t floatToByte(const float value)
{
	if(!std::isfinite(value))
		return 0;
	return static_cast<uint8_t>(std::lround(clampValue(value, 0.0f, 1.0f) * 255.0f));
}


uint32_t materialColourRGBA(const WorldMaterial& material)
{
	return
		(static_cast<uint32_t>(floatToByte(material.colour_rgb.r)) << 24) |
		(static_cast<uint32_t>(floatToByte(material.colour_rgb.g)) << 16) |
		(static_cast<uint32_t>(floatToByte(material.colour_rgb.b)) << 8) |
		 static_cast<uint32_t>(floatToByte(material.opacity.val));
}


void appendUniqueColour(std::vector<uint32_t>& colours, const uint32_t colour)
{
	if(colours.size() >= static_cast<size_t>(VoxelEditorData::MAX_PALETTE_COLOURS))
		return;
	if(std::find(colours.begin(), colours.end(), colour) == colours.end())
		colours.push_back(colour);
}


void clampColourVector(std::vector<uint32_t>& colours)
{
	std::vector<uint32_t> unique;
	unique.reserve(std::min<size_t>(colours.size(), VoxelEditorData::MAX_PALETTE_COLOURS));
	for(const uint32_t colour : colours)
		appendUniqueColour(unique, colour);
	colours.swap(unique);
}


void readColourArray(const JSONParser& parser, const JSONNode& root, const char* key, std::vector<uint32_t>& result)
{
	if(!root.hasChild(key))
		return;
	const JSONNode& array = root.getChildNode(parser, key);
	if(array.type != JSONNode::Type_Array)
		return;

	result.clear();
	for(size_t i=0; i<array.child_indices.size() && result.size() < static_cast<size_t>(VoxelEditorData::MAX_PALETTE_COLOURS); ++i)
	{
		const JSONNode& node = parser.nodes[array.child_indices[i]];
		if(node.type != JSONNode::Type_Number)
			continue;
		const double value = node.getDoubleValue();
		if(std::isfinite(value) && value >= 0.0 && value <= 4294967295.0)
			result.push_back(static_cast<uint32_t>(value));
	}
}

} // namespace


namespace VoxelEditorData
{

const char* contentMarker()
{
	return VOXEL_EDITOR_MARKER;
}


bool isVoxelEditorContent(const std::string& content)
{
	size_t i = 0;
	while(i < content.size() && static_cast<unsigned char>(content[i]) <= 32)
		++i;
	return content.compare(i, std::strlen(VOXEL_EDITOR_MARKER), VOXEL_EDITOR_MARKER) == 0;
}


const char* renderModeToString(const VoxelRenderMode mode)
{
	switch(mode)
	{
	case VoxelRenderMode::Greedy:        return "greedy";
	case VoxelRenderMode::Cubes:         return "cubes";
	case VoxelRenderMode::MarchingCubes: return "marching_cubes";
	}
	return "greedy";
}


VoxelRenderMode renderModeFromString(const std::string& value)
{
	if(value == "cubes")
		return VoxelRenderMode::Cubes;
	if(value == "marching_cubes" || value == "marching-cubes" || value == "smooth")
		return VoxelRenderMode::MarchingCubes;
	return VoxelRenderMode::Greedy;
}


VoxelEditorState defaultForMaterialCount(const size_t material_count)
{
	VoxelEditorState state;
	VoxelLayer layer;
	layer.name = "Layer 1";
	const size_t count = std::min<size_t>(std::max<size_t>(material_count, 1), MAX_MATERIAL_INDEX + 1);
	layer.material_indices.reserve(count);
	for(size_t i=0; i<count; ++i)
	{
		layer.material_indices.push_back(static_cast<int>(i));
		layer.material_base_opacities.push_back(1.0f);
	}
	state.layers.push_back(layer);

	state.palette.colours = {
		0xFFFFFFFFu, 0x202020FFu, 0xD74747FFu, 0x47B85DFFu,
		0x477AD7FFu, 0xE2C14BFFu, 0xA45DD7FFu, 0x53C9C9FFu
	};
	state.palette.current_colour = state.palette.colours[0];
	clamp(state);
	return state;
}


VoxelEditorState defaultForObject(const WorldObject& object)
{
	VoxelEditorState state = defaultForMaterialCount(object.materials.size());
	for(size_t i=0; i<state.layers[0].material_indices.size(); ++i)
	{
		const int material_index = state.layers[0].material_indices[i];
		if(material_index >= 0 && material_index < static_cast<int>(object.materials.size()) && object.materials[material_index].nonNull())
			state.layers[0].material_base_opacities[i] = clampValue(object.materials[material_index]->opacity.val, 0.0f, 1.0f);
	}
	if(!isVoxelEditorContent(object.content))
		state.legacy_content = object.content;
	state.palette.colours.clear();
	for(size_t i=0; i<object.materials.size() && state.palette.colours.size() < static_cast<size_t>(MAX_PALETTE_COLOURS); ++i)
		if(object.materials[i].nonNull())
			appendUniqueColour(state.palette.colours, materialColourRGBA(*object.materials[i]));

	if(state.palette.colours.empty())
		state.palette.colours.push_back(0xFFFFFFFFu);
	state.palette.current_colour = state.palette.colours[0];
	clamp(state);
	return state;
}


void clamp(VoxelEditorState& state)
{
	state.schema_version = VOXEL_EDITOR_SCHEMA_VERSION;
	if(state.layers.empty())
		state.layers.push_back(VoxelLayer());
	if(state.layers.size() > static_cast<size_t>(MAX_LAYERS))
		state.layers.resize(MAX_LAYERS);

	std::array<bool, MAX_MATERIAL_INDEX + 1> material_claimed{};
	for(size_t layer_i=0; layer_i<state.layers.size(); ++layer_i)
	{
		VoxelLayer& layer = state.layers[layer_i];
		layer.name = sanitiseLayerName(layer.name, layer_i);
		if(!std::isfinite(layer.opacity))
			layer.opacity = 1.0f;
		layer.opacity = clampValue(layer.opacity, 0.0f, 1.0f);

		std::vector<std::pair<int, float> > valid_materials;
		valid_materials.reserve(layer.material_indices.size());
		for(size_t material_i=0; material_i<layer.material_indices.size(); ++material_i)
		{
			const int material_index = layer.material_indices[material_i];
			if(material_index < 0 || material_index > MAX_MATERIAL_INDEX || material_claimed[material_index])
				continue;
			material_claimed[material_index] = true;
			float base_opacity = material_i < layer.material_base_opacities.size() ? layer.material_base_opacities[material_i] : 1.0f;
			if(!std::isfinite(base_opacity))
				base_opacity = 1.0f;
			valid_materials.push_back(std::make_pair(material_index, clampValue(base_opacity, 0.0f, 1.0f)));
		}
		std::sort(valid_materials.begin(), valid_materials.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.first < b.first; });
		layer.material_indices.clear();
		layer.material_base_opacities.clear();
		for(const std::pair<int, float>& material : valid_materials)
		{
			layer.material_indices.push_back(material.first);
			layer.material_base_opacities.push_back(material.second);
		}
	}

	state.active_layer = clampValue(state.active_layer, 0, static_cast<int>(state.layers.size()) - 1);
	state.current_material_index = clampValue(state.current_material_index, 0, MAX_MATERIAL_INDEX);
	// Keep the explicitly selected layer.  If its previous colour belongs to a
	// different layer, select this layer's first material instead.  Empty layers
	// remain empty until the UI explicitly assigns a material to them.
	if(!layerOwnsMaterial(state, state.active_layer, state.current_material_index))
	{
		if(!state.layers[state.active_layer].material_indices.empty())
			state.current_material_index = state.layers[state.active_layer].material_indices[0];
	}

	clampColourVector(state.palette.colours);
	clampColourVector(state.palette.recent_colours);
	if(state.palette.colours.empty())
		state.palette.colours.push_back(0xFFFFFFFFu);

	if(!std::isfinite(state.surface_threshold))
		state.surface_threshold = 0.5f;
	state.surface_threshold = clampValue(state.surface_threshold, 0.0f, 1.0f);
	switch(state.render_mode)
	{
	case VoxelRenderMode::Greedy:
	case VoxelRenderMode::Cubes:
	case VoxelRenderMode::MarchingCubes:
		break;
	default:
		state.render_mode = VoxelRenderMode::Greedy;
		break;
	}
}


VoxelEditorState fromContent(const std::string& content, const VoxelEditorState& fallback,
	std::string* parse_error_out, bool* migrated_out)
{
	if(parse_error_out)
		parse_error_out->clear();
	if(migrated_out)
		*migrated_out = false;

	VoxelEditorState result = fallback;
	clamp(result);
	if(!isVoxelEditorContent(content))
	{
		if(migrated_out)
			*migrated_out = true;
		return result;
	}

	const size_t json_start = content.find('{');
	if(json_start == std::string::npos)
	{
		if(parse_error_out)
			*parse_error_out = "Voxel editor metadata has no JSON object.";
		return result;
	}

	try
	{
		JSONParser parser;
		parser.parseBuffer(content.data() + json_start, content.size() - json_start);
		if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
			throw glare::Exception("Voxel editor metadata root is not an object.");
		const JSONNode& root = parser.nodes[0];

		const int source_schema = root.getChildIntValueWithDefaultVal(parser, "schema_version", 0);
		if(migrated_out)
			*migrated_out = source_schema != VOXEL_EDITOR_SCHEMA_VERSION;

		result.active_layer = root.getChildIntValueWithDefaultVal(parser, "active_layer",
			root.getChildIntValueWithDefaultVal(parser, "activeLayer", result.active_layer));
		result.current_material_index = root.getChildIntValueWithDefaultVal(parser, "current_material_index",
			root.getChildIntValueWithDefaultVal(parser, "currentMaterialIndex", result.current_material_index));
		result.render_mode = renderModeFromString(root.getChildStringValueWithDefaultVal(parser, "render_mode",
			root.getChildStringValueWithDefaultVal(parser, "renderMode", renderModeToString(result.render_mode))));
		result.smooth_normals = root.getChildBoolValueWithDefaultVal(parser, "smooth_normals", result.smooth_normals);
		result.surface_threshold = static_cast<float>(root.getChildDoubleValueWithDefaultVal(parser, "surface_threshold", result.surface_threshold));
		result.legacy_content = root.getChildStringValueWithDefaultVal(parser, "legacy_content", result.legacy_content);

		if(root.hasChild("layers"))
		{
			const JSONNode& layers = root.getChildNode(parser, "layers");
			if(layers.type == JSONNode::Type_Array)
			{
				result.layers.clear();
				for(size_t i=0; i<layers.child_indices.size() && result.layers.size() < static_cast<size_t>(MAX_LAYERS); ++i)
				{
					const JSONNode& layer_node = parser.nodes[layers.child_indices[i]];
					if(layer_node.type != JSONNode::Type_Object)
						continue;
					VoxelLayer layer;
					layer.name = layer_node.getChildStringValueWithDefaultVal(parser, "name", "Layer");
					layer.visible = layer_node.getChildBoolValueWithDefaultVal(parser, "visible", true);
					layer.locked = layer_node.getChildBoolValueWithDefaultVal(parser, "locked", false);
					layer.opacity = static_cast<float>(layer_node.getChildDoubleValueWithDefaultVal(parser, "opacity", 1.0));
					if(layer_node.hasChild("material_indices"))
					{
						const JSONNode& indices = layer_node.getChildNode(parser, "material_indices");
						if(indices.type == JSONNode::Type_Array)
							for(const uint32 child_index : indices.child_indices)
								if(parser.nodes[child_index].type == JSONNode::Type_Number)
									layer.material_indices.push_back(parser.nodes[child_index].getIntValue());
					}
					if(layer_node.hasChild("material_base_opacities"))
					{
						const JSONNode& opacities = layer_node.getChildNode(parser, "material_base_opacities");
						if(opacities.type == JSONNode::Type_Array)
							for(const uint32 child_index : opacities.child_indices)
								if(parser.nodes[child_index].type == JSONNode::Type_Number)
									layer.material_base_opacities.push_back(static_cast<float>(parser.nodes[child_index].getDoubleValue()));
					}
					result.layers.push_back(layer);
				}
			}
		}

		if(root.hasChild("palette"))
		{
			const JSONNode& palette = root.getChildNode(parser, "palette");
			if(palette.type == JSONNode::Type_Object)
			{
				const double current = palette.getChildDoubleValueWithDefaultVal(parser, "current", result.palette.current_colour);
				if(std::isfinite(current) && current >= 0.0 && current <= 4294967295.0)
					result.palette.current_colour = static_cast<uint32_t>(current);
				readColourArray(parser, palette, "colours", result.palette.colours);
				readColourArray(parser, palette, "recent", result.palette.recent_colours);
			}
		}
	}
	catch(const glare::Exception& e)
	{
		if(parse_error_out)
			*parse_error_out = e.what();
		result = fallback;
	}
	catch(...)
	{
		if(parse_error_out)
			*parse_error_out = "Could not parse voxel editor metadata.";
		result = fallback;
	}

	clamp(result);
	return result;
}


VoxelEditorState fromObject(const WorldObject& object, std::string* parse_error_out, bool* migrated_out)
{
	const VoxelEditorState fallback = defaultForObject(object);
	return fromContent(object.content, fallback, parse_error_out, migrated_out);
}


std::string serialiseToContent(const VoxelEditorState& state_)
{
	VoxelEditorState state = state_;
	clamp(state);

	std::ostringstream out;
	out << VOXEL_EDITOR_MARKER << "\n{";
	out << "\"type\":\"voxel_editor\",\"schema_version\":" << VOXEL_EDITOR_SCHEMA_VERSION;
	out << ",\"active_layer\":" << state.active_layer;
	out << ",\"current_material_index\":" << state.current_material_index;
	out << ",\"render_mode\":\"" << renderModeToString(state.render_mode) << "\"";
	out << ",\"smooth_normals\":" << (state.smooth_normals ? "true" : "false");
	out << ",\"surface_threshold\":" << state.surface_threshold;
	out << ",\"legacy_content\":\"" << jsonEscape(state.legacy_content) << "\"";
	out << ",\"layers\":[";
	for(size_t i=0; i<state.layers.size(); ++i)
	{
		if(i != 0)
			out << ',';
		const VoxelLayer& layer = state.layers[i];
		out << "{\"name\":\"" << jsonEscape(layer.name) << "\",\"visible\":" << (layer.visible ? "true" : "false")
			<< ",\"locked\":" << (layer.locked ? "true" : "false") << ",\"opacity\":" << layer.opacity
			<< ",\"material_indices\":[";
		for(size_t m=0; m<layer.material_indices.size(); ++m)
		{
			if(m != 0)
				out << ',';
			out << layer.material_indices[m];
		}
		out << "],\"material_base_opacities\":[";
		for(size_t m=0; m<layer.material_base_opacities.size(); ++m)
		{
			if(m != 0)
				out << ',';
			out << layer.material_base_opacities[m];
		}
		out << "]}";
	}
	out << "],\"palette\":{\"current\":" << state.palette.current_colour << ",\"colours\":[";
	for(size_t i=0; i<state.palette.colours.size(); ++i)
	{
		if(i != 0)
			out << ',';
		out << state.palette.colours[i];
	}
	out << "],\"recent\":[";
	for(size_t i=0; i<state.palette.recent_colours.size(); ++i)
	{
		if(i != 0)
			out << ',';
		out << state.palette.recent_colours[i];
	}
	out << "]}}\n";

	return out.str();
}


bool storeOnObject(WorldObject& object, const VoxelEditorState& state)
{
	if(object.object_type != WorldObject::ObjectType_VoxelGroup)
		return false;
	const std::string content = serialiseToContent(state);
	if(content.size() >= WorldObject::MAX_CONTENT_SIZE)
		return false;
	object.content = content;
	return true;
}


int materialLayerIndex(const VoxelEditorState& state, const int material_index)
{
	if(material_index < 0 || material_index > MAX_MATERIAL_INDEX)
		return -1;
	for(size_t i=0; i<state.layers.size(); ++i)
		if(std::find(state.layers[i].material_indices.begin(), state.layers[i].material_indices.end(), material_index) != state.layers[i].material_indices.end())
			return static_cast<int>(i);
	return -1;
}


bool layerOwnsMaterial(const VoxelEditorState& state, const int layer_index, const int material_index)
{
	return layer_index >= 0 && layer_index < static_cast<int>(state.layers.size()) &&
		std::find(state.layers[layer_index].material_indices.begin(), state.layers[layer_index].material_indices.end(), material_index) != state.layers[layer_index].material_indices.end();
}


bool ensureMaterialInLayer(VoxelEditorState& state, const int layer_index, const int material_index)
{
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()) || material_index < 0 || material_index > MAX_MATERIAL_INDEX)
		return false;
	float preserved_base_opacity = 1.0f;
	for(size_t source_layer_i=0; source_layer_i<state.layers.size(); ++source_layer_i)
	{
		VoxelLayer& layer = state.layers[source_layer_i];
		for(size_t material_i=0; material_i<layer.material_indices.size(); ++material_i)
			if(layer.material_indices[material_i] == material_index)
			{
				if(material_i < layer.material_base_opacities.size())
					preserved_base_opacity = layer.material_base_opacities[material_i];
				if(static_cast<int>(source_layer_i) == layer_index)
				{
					state.current_material_index = material_index;
					state.active_layer = layer_index;
					return true;
				}
				layer.material_indices.erase(layer.material_indices.begin() + material_i);
				if(material_i < layer.material_base_opacities.size())
					layer.material_base_opacities.erase(layer.material_base_opacities.begin() + material_i);
				break;
			}
	}
	state.layers[layer_index].material_indices.push_back(material_index);
	state.layers[layer_index].material_base_opacities.push_back(clampValue(preserved_base_opacity, 0.0f, 1.0f));
	state.current_material_index = material_index;
	state.active_layer = layer_index;
	clamp(state);
	return true;
}


float materialBaseOpacity(const VoxelEditorState& state, const int layer_index, const int material_index)
{
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()))
		return 1.0f;
	const VoxelLayer& layer = state.layers[layer_index];
	for(size_t i=0; i<layer.material_indices.size(); ++i)
		if(layer.material_indices[i] == material_index)
			return i < layer.material_base_opacities.size() ? layer.material_base_opacities[i] : 1.0f;
	return 1.0f;
}


bool setMaterialBaseOpacity(VoxelEditorState& state, const int layer_index, const int material_index, const float opacity)
{
	if(layer_index < 0 || layer_index >= static_cast<int>(state.layers.size()) || !std::isfinite(opacity))
		return false;
	VoxelLayer& layer = state.layers[layer_index];
	for(size_t i=0; i<layer.material_indices.size(); ++i)
		if(layer.material_indices[i] == material_index)
		{
			if(layer.material_base_opacities.size() < layer.material_indices.size())
				layer.material_base_opacities.resize(layer.material_indices.size(), 1.0f);
			layer.material_base_opacities[i] = clampValue(opacity, 0.0f, 1.0f);
			return true;
		}
	return false;
}


const VoxelLayer* activeLayer(const VoxelEditorState& state)
{
	return state.active_layer >= 0 && state.active_layer < static_cast<int>(state.layers.size()) ? &state.layers[state.active_layer] : nullptr;
}


VoxelLayer* activeLayer(VoxelEditorState& state)
{
	return state.active_layer >= 0 && state.active_layer < static_cast<int>(state.layers.size()) ? &state.layers[state.active_layer] : nullptr;
}


bool runSelfTest(std::string* details_out)
{
	auto fail = [details_out](const char* message)
	{
		if(details_out)
			*details_out = message;
		return false;
	};

	VoxelEditorState state = defaultForMaterialCount(400);
	state.legacy_content = "legacy voxel content\nwith a quoted \"value\"";
	for(int i=1; i<24; ++i)
	{
		VoxelLayer layer;
		layer.name = "Layer \"" + std::to_string(i) + "\"";
		layer.material_indices.push_back(i); // Duplicates are intentionally clamped away.
		state.layers.push_back(layer);
	}
	for(uint32_t i=0; i<100; ++i)
	{
		state.palette.colours.push_back(i);
		state.palette.recent_colours.push_back(1000 + i);
	}
	clamp(state);
	if(state.layers.size() != MAX_LAYERS || state.palette.colours.size() > MAX_PALETTE_COLOURS || state.palette.recent_colours.size() > MAX_PALETTE_COLOURS)
		return fail("Voxel metadata limits were not enforced.");

	const std::string content = serialiseToContent(state);
	if(content.size() >= WorldObject::MAX_CONTENT_SIZE || !isVoxelEditorContent(content))
		return fail("Serialised voxel metadata exceeded the content limit.");

	std::string error;
	bool migrated = true;
	const VoxelEditorState parsed = fromContent(content, defaultForMaterialCount(1), &error, &migrated);
	if(!error.empty() || migrated || parsed.layers.size() != state.layers.size() || parsed.render_mode != state.render_mode || parsed.legacy_content != state.legacy_content)
		return fail("Voxel metadata round-trip failed.");
	if(materialLayerIndex(parsed, 0) != 0 || materialLayerIndex(parsed, 255) != -1)
		return fail("Voxel layer material ownership is invalid.");

	if(details_out)
		*details_out = "ok";
	return true;
}

} // namespace VoxelEditorData
