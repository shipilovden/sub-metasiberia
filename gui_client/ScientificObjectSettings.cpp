/*=====================================================================
ScientificObjectSettings.cpp
----------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ScientificObjectSettings.h"


#include <JSONParser.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>


namespace
{
static const char* const SCIENTIFIC_OBJECT_MARKER = "metasiberia_scientific_object_v1";


double clampDouble(const double v, const double min_v, const double max_v)
{
	return std::max(min_v, std::min(max_v, v));
}


int clampInt(const int v, const int min_v, const int max_v)
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


int colourChannelToByte(const float x)
{
	return clampInt((int)std::round(clampDouble(x, 0.0, 1.0) * 255.0), 0, 255);
}


std::string colourToHex(const Colour3f& c)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", colourChannelToByte(c.r), colourChannelToByte(c.g), colourChannelToByte(c.b));
	return std::string(buf);
}


int hexDigit(const char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}


Colour3f colourFromHex(const std::string& s, const Colour3f& default_col)
{
	if(s.size() != 7 || s[0] != '#')
		return default_col;

	const int r = hexDigit(s[1]) * 16 + hexDigit(s[2]);
	const int g = hexDigit(s[3]) * 16 + hexDigit(s[4]);
	const int b = hexDigit(s[5]) * 16 + hexDigit(s[6]);
	return Colour3f(r / 255.f, g / 255.f, b / 255.f);
}


bool isKnownScientificObjectField(const std::string& name)
{
	static const char* const names[] = {
		"schema_version", "name", "scientific_type", "description", "source", "author", "tags", "uuid", "created_time", "modified_time",
		"source_mode", "file_path", "source_url", "online_database", "online_query", "online_result_id",
		"load_status", "load_status_message", "data_origin",
		"provenance_source", "provenance_identifier", "provenance_url", "provenance_author", "provenance_loaded_at", "provenance_format", "provenance_version", "provenance_license", "provenance_checksum",
		"molecule_model_version", "provider_adapter_version", "parser_version", "cache_version", "visualization_settings_version",
		"source_data_cache_key", "source_data_cache_path", "image_url", "image_cache_path", "image_checksum", "conformer_status",
		"search_original_query", "search_normalized_query", "search_translation",
		"code_language", "code_text", "prompt_text", "generated_code",
		"data_summary", "atom_table", "bond_table", "point_table", "value_table", "property_table",
		"visualization_mode", "colour_scheme", "display_colour", "material", "atom_radius", "bond_thickness", "point_size", "line_width", "opacity", "object_scale",
		"show_labels", "show_molecule_title", "molecule_title", "show_info_card", "info_card_mode", "info_card_scale", "info_card_distance", "info_card_pinned", "show_legend", "show_hydrogen", "label_mode", "label_scale", "molecule_title_scale", "label_colour", "label_max_count", "label_max_distance", "label_runtime_status",
		"lod_level", "glow_enabled", "glow_strength", "outline_enabled", "wireframe_enabled",
		"measure_distance", "measure_angle", "measure_torsion", "measure_area", "measure_volume", "selection_mode", "selection_state", "selected_atom_indices", "selected_bond_index", "measurements_json", "atom_count", "bond_count", "point_count", "object_dimensions",
		"rotation_animation_enabled", "trajectory_animation_enabled", "vibration_animation_enabled", "time_series_enabled", "animation_speed", "animation_direction", "current_frame", "frame_count", "animation_runtime_status",
		"simulation_enabled", "simulation_type", "simulation_notes", "provider_classifications", "computed_classifications", "user_collections", "favorite", "section_cache_manifest",
		"ai_provider", "ai_model", "ai_endpoint", "ai_uses_user_credentials",
		"collision_enabled", "solid", "trigger", "selectable", "movable", "gravity_enabled", "physics_motion_type", "physics_shape", "collision_layer", "physics_mass", "physics_friction", "physics_restitution",
		"custom_properties"
	};
	for(size_t i=0; i<sizeof(names) / sizeof(names[0]); ++i)
		if(name == names[i])
			return true;
	return false;
}


std::string jsonNodeToString(const JSONParser& parser, const JSONNode& node)
{
	std::ostringstream s;
	s << std::setprecision(8);
	switch(node.type)
	{
	case JSONNode::Type_Number:
		s << node.value.double_v;
		break;
	case JSONNode::Type_String:
		s << "\"" << jsonEscape(node.string_v) << "\"";
		break;
	case JSONNode::Type_Boolean:
		s << (node.value.bool_v ? "true" : "false");
		break;
	case JSONNode::Type_Array:
		s << "[";
		for(size_t i=0; i<node.child_indices.size(); ++i)
		{
			if(i != 0) s << ", ";
			s << jsonNodeToString(parser, parser.nodes[node.child_indices[i]]);
		}
		s << "]";
		break;
	case JSONNode::Type_Object:
		s << "{";
		for(size_t i=0; i<node.name_val_pairs.size(); ++i)
		{
			if(i != 0) s << ", ";
			const JSONNameValuePair& pair = node.name_val_pairs[i];
			s << "\"" << jsonEscape(pair.name) << "\": " << jsonNodeToString(parser, parser.nodes[pair.value_node_index]);
		}
		s << "}";
		break;
	case JSONNode::Type_Null:
		s << "null";
		break;
	}
	return s.str();
}


void clampSettings(ScientificObjectSettings& s)
{
	const size_t max_text_len = 8192;
	if(s.name.size() > 256) s.name.resize(256);
	if(s.scientific_type.size() > 64) s.scientific_type.resize(64);
	if(s.description.size() > max_text_len) s.description.resize(max_text_len);
	if(s.source.size() > 256) s.source.resize(256);
	if(s.author.size() > 256) s.author.resize(256);
	if(s.tags.size() > 2048) s.tags.resize(2048);
	if(s.uuid.size() > 128) s.uuid.resize(128);
	if(s.created_time.size() > 64) s.created_time.resize(64);
	if(s.modified_time.size() > 64) s.modified_time.resize(64);
	if(s.source_mode.size() > 64) s.source_mode.resize(64);
	if(s.file_path.size() > 4096) s.file_path.resize(4096);
	if(s.source_url.size() > 4096) s.source_url.resize(4096);
	if(s.online_database.size() > 128) s.online_database.resize(128);
	if(s.online_query.size() > 512) s.online_query.resize(512);
	if(s.online_result_id.size() > 256) s.online_result_id.resize(256);
	if(s.code_language.size() > 64) s.code_language.resize(64);
	if(s.code_text.size() > max_text_len) s.code_text.resize(max_text_len);
	if(s.prompt_text.size() > max_text_len) s.prompt_text.resize(max_text_len);
	if(s.generated_code.size() > max_text_len) s.generated_code.resize(max_text_len);
	if(s.data_summary.size() > max_text_len) s.data_summary.resize(max_text_len);
	if(s.atom_table.size() > max_text_len) s.atom_table.resize(max_text_len);
	if(s.bond_table.size() > max_text_len) s.bond_table.resize(max_text_len);
	if(s.point_table.size() > max_text_len) s.point_table.resize(max_text_len);
	if(s.value_table.size() > max_text_len) s.value_table.resize(max_text_len);
	if(s.property_table.size() > max_text_len) s.property_table.resize(max_text_len);
	if(s.visualization_mode.size() > 64) s.visualization_mode.resize(64);
	if(s.colour_scheme.size() > 64) s.colour_scheme.resize(64);
	if(s.material.size() > 64) s.material.resize(64);
	if(s.object_dimensions.size() > 512) s.object_dimensions.resize(512);
	if(s.simulation_type.size() > 128) s.simulation_type.resize(128);
	if(s.simulation_notes.size() > max_text_len) s.simulation_notes.resize(max_text_len);
	if(s.ai_provider.size() > 128) s.ai_provider.resize(128);
	if(s.ai_model.size() > 256) s.ai_model.resize(256);
	if(s.ai_endpoint.size() > 4096) s.ai_endpoint.resize(4096);
	if(s.custom_properties.size() > max_text_len) s.custom_properties.resize(max_text_len);
	if(s.load_status.size() > 64) s.load_status.resize(64);
	if(s.load_status_message.size() > 1024) s.load_status_message.resize(1024);
	if(s.data_origin.size() > 64) s.data_origin.resize(64);
	if(s.provenance_source.size() > 256) s.provenance_source.resize(256);
	if(s.provenance_identifier.size() > 256) s.provenance_identifier.resize(256);
	if(s.provenance_url.size() > 4096) s.provenance_url.resize(4096);
	if(s.provenance_author.size() > 256) s.provenance_author.resize(256);
	if(s.provenance_loaded_at.size() > 64) s.provenance_loaded_at.resize(64);
	if(s.provenance_format.size() > 128) s.provenance_format.resize(128);
	if(s.provenance_version.size() > 128) s.provenance_version.resize(128);
	if(s.provenance_license.size() > 256) s.provenance_license.resize(256);
	if(s.provenance_checksum.size() > 256) s.provenance_checksum.resize(256);
	if(s.molecule_model_version.size() > 64) s.molecule_model_version.resize(64);
	if(s.provider_adapter_version.size() > 64) s.provider_adapter_version.resize(64);
	if(s.parser_version.size() > 64) s.parser_version.resize(64);
	if(s.cache_version.size() > 64) s.cache_version.resize(64);
	if(s.visualization_settings_version.size() > 64) s.visualization_settings_version.resize(64);
	if(s.source_data_cache_key.size() > 256) s.source_data_cache_key.resize(256);
	if(s.source_data_cache_path.size() > 4096) s.source_data_cache_path.resize(4096);
	if(s.image_url.size() > 4096) s.image_url.resize(4096);
	if(s.image_cache_path.size() > 4096) s.image_cache_path.resize(4096);
	if(s.image_checksum.size() > 256) s.image_checksum.resize(256);
	if(s.conformer_status.size() > 256) s.conformer_status.resize(256);
	if(s.search_original_query.size() > 512) s.search_original_query.resize(512);
	if(s.search_normalized_query.size() > 512) s.search_normalized_query.resize(512);
	if(s.search_translation.size() > 512) s.search_translation.resize(512);
	if(s.label_mode.size() > 64) s.label_mode.resize(64);
	if(s.molecule_title.size() > 256) s.molecule_title.resize(256);
	if(s.label_runtime_status.size() > 256) s.label_runtime_status.resize(256);
	if(s.animation_runtime_status.size() > 256) s.animation_runtime_status.resize(256);
	if(s.selection_mode.size() > 32) s.selection_mode.resize(32);
	if(s.selection_state.size() > 64) s.selection_state.resize(64);
	if(s.selected_atom_indices.size() > 1024) s.selected_atom_indices.resize(1024);
	if(s.measurements_json.size() > 2048) s.measurements_json.resize(2048);
	if(s.provider_classifications.size() > 2048) s.provider_classifications.resize(2048);
	if(s.computed_classifications.size() > 1024) s.computed_classifications.resize(1024);
	if(s.user_collections.size() > 1024) s.user_collections.resize(1024);
	if(s.section_cache_manifest.size() > 2048) s.section_cache_manifest.resize(2048);
	if(s.physics_motion_type.size() > 64) s.physics_motion_type.resize(64);
	if(s.physics_shape.size() > 64) s.physics_shape.resize(64);
	if(s.collision_layer.size() > 128) s.collision_layer.resize(128);
	if(s.preserved_json_fields.size() > max_text_len) s.preserved_json_fields.clear();

	s.schema_version = clampInt(s.schema_version, 1, 100);
	s.display_colour.r = (float)clampDouble(s.display_colour.r, 0.0, 1.0);
	s.display_colour.g = (float)clampDouble(s.display_colour.g, 0.0, 1.0);
	s.display_colour.b = (float)clampDouble(s.display_colour.b, 0.0, 1.0);
	s.label_colour.r = (float)clampDouble(s.label_colour.r, 0.0, 1.0);
	s.label_colour.g = (float)clampDouble(s.label_colour.g, 0.0, 1.0);
	s.label_colour.b = (float)clampDouble(s.label_colour.b, 0.0, 1.0);
	s.atom_radius = (float)clampDouble(s.atom_radius, 0.01, 10.0);
	s.bond_thickness = (float)clampDouble(s.bond_thickness, 0.001, 10.0);
	s.point_size = (float)clampDouble(s.point_size, 0.001, 20.0);
	s.line_width = (float)clampDouble(s.line_width, 0.001, 20.0);
	s.opacity = (float)clampDouble(s.opacity, 0.02, 1.0);
	s.object_scale = (float)clampDouble(s.object_scale, 0.01, 1000.0);
	s.label_scale = (float)clampDouble(s.label_scale, 0.05, 20.0);
	s.molecule_title_scale = (float)clampDouble(s.molecule_title_scale, 0.05, 20.0);
	if(s.info_card_mode != "molecule" && s.info_card_mode != "atom" && s.info_card_mode != "selection")
		s.info_card_mode = "selection";
	s.info_card_scale = (float)clampDouble(s.info_card_scale, 0.05, 20.0);
	s.info_card_distance = (float)clampDouble(s.info_card_distance, 0.0, 1000.0);
	s.label_max_count = clampInt(s.label_max_count, 0, 1000000);
	s.label_max_distance = (float)clampDouble(s.label_max_distance, 0.0, 1000000.0);
	s.lod_level = clampInt(s.lod_level, 0, 8);
	s.glow_strength = (float)clampDouble(s.glow_strength, 0.0, 10000.0);
	s.atom_count = clampInt(s.atom_count, 0, 100000000);
	s.bond_count = clampInt(s.bond_count, 0, 100000000);
	s.point_count = clampInt(s.point_count, 0, 100000000);
	s.selected_bond_index = clampInt(s.selected_bond_index, -1, 100000000);
	s.animation_speed = (float)clampDouble(s.animation_speed, 0.0, 100.0);
	if(s.animation_direction != "reverse")
		s.animation_direction = "forward";
	s.current_frame = clampInt(s.current_frame, 0, 100000000);
	s.frame_count = clampInt(s.frame_count, 0, 100000000);
	s.physics_mass = (float)clampDouble(s.physics_mass, 0.0, 1000000000.0);
	s.physics_friction = (float)clampDouble(s.physics_friction, 0.0, 10.0);
	s.physics_restitution = (float)clampDouble(s.physics_restitution, 0.0, 10.0);
}
}


ScientificObjectSettings::ScientificObjectSettings()
:	schema_version(1),
	name("Scientific Object"),
	scientific_type("custom"),
	description("Universal scientific object for MetaSiberia."),
	source("manual"),
	author(""),
	tags("science, metasiberia"),
	uuid(""),
	created_time(""),
	modified_time(""),
	source_mode("prompt"),
	file_path(""),
	source_url(""),
	online_database("PubChem"),
	online_query(""),
	online_result_id(""),
	load_status("idle"),
	load_status_message("No scientific data loaded yet."),
	data_origin("user"),
	provenance_source(""),
	provenance_identifier(""),
	provenance_url(""),
	provenance_author(""),
	provenance_loaded_at(""),
	provenance_format(""),
	provenance_version(""),
	provenance_license(""),
	provenance_checksum(""),
	molecule_model_version("molecule_model_v1"),
	provider_adapter_version(""),
	parser_version(""),
	cache_version("pubchem_cache_v1"),
	visualization_settings_version("visualization_settings_v1"),
	source_data_cache_key(""),
	source_data_cache_path(""),
	image_url(""),
	image_cache_path(""),
	image_checksum(""),
	conformer_status("unknown"),
	search_original_query(""),
	search_normalized_query(""),
	search_translation(""),
	code_language("Python"),
	code_text("import math\n\npoints = []\nfor i in range(200):\n    x = -6.28 + i * 12.56 / 199\n    points.append({\"x\": x, \"y\": math.sin(x), \"z\": 0.0})\n\nreturn {\"type\": \"chart\", \"points\": points}\n"),
	prompt_text("Построй график sin(x)"),
	generated_code(""),
	data_summary("No scientific data loaded yet."),
	atom_table(""),
	bond_table(""),
	point_table(""),
	value_table(""),
	property_table(""),
	visualization_mode("points"),
	colour_scheme("CPK"),
	display_colour(0.20f, 0.72f, 1.0f),
	material("matte"),
	atom_radius(1.0f),
	bond_thickness(0.10f),
	point_size(0.05f),
	line_width(0.02f),
	opacity(0.88f),
	object_scale(1.0f),
	show_labels(false),
	show_molecule_title(false),
	molecule_title(""),
	show_info_card(false),
	info_card_mode("selection"),
	info_card_scale(1.0f),
	info_card_distance(1.0f),
	info_card_pinned(false),
	show_legend(true),
	show_hydrogen(true),
	label_mode("element"),
	label_scale(1.0f),
	molecule_title_scale(1.0f),
	label_colour(1.0f, 1.0f, 1.0f),
	label_max_count(128),
	label_max_distance(25.0f),
	label_runtime_status("not_connected"),
	lod_level(1),
	glow_enabled(false),
	glow_strength(0.0f),
	outline_enabled(false),
	wireframe_enabled(false),
	measure_distance(false),
	measure_angle(false),
	measure_torsion(false),
	measure_area(false),
	measure_volume(false),
	selection_mode("atom"),
	selection_state("no_selection"),
	selected_atom_indices(""),
	selected_bond_index(-1),
	measurements_json("[]"),
	atom_count(0),
	bond_count(0),
	point_count(0),
	object_dimensions(""),
	rotation_animation_enabled(false),
	trajectory_animation_enabled(false),
	vibration_animation_enabled(false),
	time_series_enabled(false),
	animation_speed(1.0f),
	animation_direction("forward"),
	current_frame(0),
	frame_count(0),
	animation_runtime_status("not_connected"),
	simulation_enabled(false),
	simulation_type("future"),
	simulation_notes("Reserved for physics, molecular dynamics, fields, CFD and numerical solvers."),
	provider_classifications(""),
	computed_classifications(""),
	user_collections(""),
	favorite(false),
	section_cache_manifest("{}"),
	ai_provider("OpenAI"),
	ai_model(""),
	ai_endpoint(""),
	ai_uses_user_credentials(true),
	collision_enabled(false),
	solid(false),
	trigger(false),
	selectable(true),
	movable(true),
	gravity_enabled(false),
	physics_motion_type("static"),
	physics_shape("mesh"),
	collision_layer("scientific_visual"),
	physics_mass(1.0f),
	physics_friction(0.5f),
	physics_restitution(0.2f),
	preserved_json_fields(""),
	custom_properties("{}")
{
}


const char* ScientificObjectSettings::contentMarker()
{
	return SCIENTIFIC_OBJECT_MARKER;
}


bool ScientificObjectSettings::isScientificObjectContent(const std::string& content)
{
	size_t i = 0;
	while(i < content.size() && std::isspace((unsigned char)content[i]))
		++i;

	return content.compare(i, std::strlen(SCIENTIFIC_OBJECT_MARKER), SCIENTIFIC_OBJECT_MARKER) == 0;
}


ScientificObjectSettings ScientificObjectSettings::defaultObject()
{
	ScientificObjectSettings settings;
	clampSettings(settings);
	return settings;
}


ScientificObjectSettings ScientificObjectSettings::fromContent(const std::string& content, std::string* parse_error_out)
{
	ScientificObjectSettings settings = defaultObject();
	if(parse_error_out)
		parse_error_out->clear();

	if(!isScientificObjectContent(content))
		return settings;

	const size_t json_start = content.find('{');
	if(json_start == std::string::npos)
		return settings;

	try
	{
		JSONParser parser;
		parser.parseBuffer(content.data() + json_start, content.size() - json_start);
		if(parser.nodes.empty())
			return settings;

		const JSONNode& root = parser.nodes[0];
		if(root.type != JSONNode::Type_Object)
			return settings;

		std::ostringstream preserved;
		for(size_t i=0; i<root.name_val_pairs.size(); ++i)
		{
			const JSONNameValuePair& pair = root.name_val_pairs[i];
			if(!isKnownScientificObjectField(pair.name))
				preserved << "  \"" << jsonEscape(pair.name) << "\": " << jsonNodeToString(parser, parser.nodes[pair.value_node_index]) << ",\n";
		}
		settings.preserved_json_fields = preserved.str();

		settings.schema_version = root.getChildIntValueWithDefaultVal(parser, "schema_version", settings.schema_version);
		settings.name = root.getChildStringValueWithDefaultVal(parser, "name", settings.name);
		settings.scientific_type = root.getChildStringValueWithDefaultVal(parser, "scientific_type", settings.scientific_type);
		settings.description = root.getChildStringValueWithDefaultVal(parser, "description", settings.description);
		settings.source = root.getChildStringValueWithDefaultVal(parser, "source", settings.source);
		settings.author = root.getChildStringValueWithDefaultVal(parser, "author", settings.author);
		settings.tags = root.getChildStringValueWithDefaultVal(parser, "tags", settings.tags);
		settings.uuid = root.getChildStringValueWithDefaultVal(parser, "uuid", settings.uuid);
		settings.created_time = root.getChildStringValueWithDefaultVal(parser, "created_time", settings.created_time);
		settings.modified_time = root.getChildStringValueWithDefaultVal(parser, "modified_time", settings.modified_time);

		settings.source_mode = root.getChildStringValueWithDefaultVal(parser, "source_mode", settings.source_mode);
		settings.file_path = root.getChildStringValueWithDefaultVal(parser, "file_path", settings.file_path);
		settings.source_url = root.getChildStringValueWithDefaultVal(parser, "source_url", settings.source_url);
		settings.online_database = root.getChildStringValueWithDefaultVal(parser, "online_database", settings.online_database);
		settings.online_query = root.getChildStringValueWithDefaultVal(parser, "online_query", settings.online_query);
		settings.online_result_id = root.getChildStringValueWithDefaultVal(parser, "online_result_id", settings.online_result_id);
		settings.load_status = root.getChildStringValueWithDefaultVal(parser, "load_status", settings.load_status);
		settings.load_status_message = root.getChildStringValueWithDefaultVal(parser, "load_status_message", settings.load_status_message);
		settings.data_origin = root.getChildStringValueWithDefaultVal(parser, "data_origin", settings.data_origin);
		settings.provenance_source = root.getChildStringValueWithDefaultVal(parser, "provenance_source", settings.provenance_source);
		settings.provenance_identifier = root.getChildStringValueWithDefaultVal(parser, "provenance_identifier", settings.provenance_identifier);
		settings.provenance_url = root.getChildStringValueWithDefaultVal(parser, "provenance_url", settings.provenance_url);
		settings.provenance_author = root.getChildStringValueWithDefaultVal(parser, "provenance_author", settings.provenance_author);
		settings.provenance_loaded_at = root.getChildStringValueWithDefaultVal(parser, "provenance_loaded_at", settings.provenance_loaded_at);
		settings.provenance_format = root.getChildStringValueWithDefaultVal(parser, "provenance_format", settings.provenance_format);
		settings.provenance_version = root.getChildStringValueWithDefaultVal(parser, "provenance_version", settings.provenance_version);
		settings.provenance_license = root.getChildStringValueWithDefaultVal(parser, "provenance_license", settings.provenance_license);
		settings.provenance_checksum = root.getChildStringValueWithDefaultVal(parser, "provenance_checksum", settings.provenance_checksum);
		settings.molecule_model_version = root.getChildStringValueWithDefaultVal(parser, "molecule_model_version", settings.molecule_model_version);
		settings.provider_adapter_version = root.getChildStringValueWithDefaultVal(parser, "provider_adapter_version", settings.provider_adapter_version);
		settings.parser_version = root.getChildStringValueWithDefaultVal(parser, "parser_version", settings.parser_version);
		settings.cache_version = root.getChildStringValueWithDefaultVal(parser, "cache_version", settings.cache_version);
		settings.visualization_settings_version = root.getChildStringValueWithDefaultVal(parser, "visualization_settings_version", settings.visualization_settings_version);
		settings.source_data_cache_key = root.getChildStringValueWithDefaultVal(parser, "source_data_cache_key", settings.source_data_cache_key);
		settings.source_data_cache_path = root.getChildStringValueWithDefaultVal(parser, "source_data_cache_path", settings.source_data_cache_path);
		settings.image_url = root.getChildStringValueWithDefaultVal(parser, "image_url", settings.image_url);
		settings.image_cache_path = root.getChildStringValueWithDefaultVal(parser, "image_cache_path", settings.image_cache_path);
		settings.image_checksum = root.getChildStringValueWithDefaultVal(parser, "image_checksum", settings.image_checksum);
		settings.conformer_status = root.getChildStringValueWithDefaultVal(parser, "conformer_status", settings.conformer_status);
		settings.search_original_query = root.getChildStringValueWithDefaultVal(parser, "search_original_query", settings.search_original_query);
		settings.search_normalized_query = root.getChildStringValueWithDefaultVal(parser, "search_normalized_query", settings.search_normalized_query);
		settings.search_translation = root.getChildStringValueWithDefaultVal(parser, "search_translation", settings.search_translation);
		settings.code_language = root.getChildStringValueWithDefaultVal(parser, "code_language", settings.code_language);
		settings.code_text = root.getChildStringValueWithDefaultVal(parser, "code_text", settings.code_text);
		settings.prompt_text = root.getChildStringValueWithDefaultVal(parser, "prompt_text", settings.prompt_text);
		settings.generated_code = root.getChildStringValueWithDefaultVal(parser, "generated_code", settings.generated_code);

		settings.data_summary = root.getChildStringValueWithDefaultVal(parser, "data_summary", settings.data_summary);
		settings.atom_table = root.getChildStringValueWithDefaultVal(parser, "atom_table", settings.atom_table);
		settings.bond_table = root.getChildStringValueWithDefaultVal(parser, "bond_table", settings.bond_table);
		settings.point_table = root.getChildStringValueWithDefaultVal(parser, "point_table", settings.point_table);
		settings.value_table = root.getChildStringValueWithDefaultVal(parser, "value_table", settings.value_table);
		settings.property_table = root.getChildStringValueWithDefaultVal(parser, "property_table", settings.property_table);

		settings.visualization_mode = root.getChildStringValueWithDefaultVal(parser, "visualization_mode", settings.visualization_mode);
		settings.colour_scheme = root.getChildStringValueWithDefaultVal(parser, "colour_scheme", settings.colour_scheme);
		settings.display_colour = colourFromHex(root.getChildStringValueWithDefaultVal(parser, "display_colour", colourToHex(settings.display_colour)), settings.display_colour);
		settings.material = root.getChildStringValueWithDefaultVal(parser, "material", settings.material);
		settings.atom_radius = (float)root.getChildDoubleValueWithDefaultVal(parser, "atom_radius", settings.atom_radius);
		settings.bond_thickness = (float)root.getChildDoubleValueWithDefaultVal(parser, "bond_thickness", settings.bond_thickness);
		settings.point_size = (float)root.getChildDoubleValueWithDefaultVal(parser, "point_size", settings.point_size);
		settings.line_width = (float)root.getChildDoubleValueWithDefaultVal(parser, "line_width", settings.line_width);
		settings.opacity = (float)root.getChildDoubleValueWithDefaultVal(parser, "opacity", settings.opacity);
		settings.object_scale = (float)root.getChildDoubleValueWithDefaultVal(parser, "object_scale", settings.object_scale);
		settings.show_labels = root.getChildBoolValueWithDefaultVal(parser, "show_labels", settings.show_labels);
		settings.show_molecule_title = root.getChildBoolValueWithDefaultVal(parser, "show_molecule_title", settings.show_molecule_title);
		settings.molecule_title = root.getChildStringValueWithDefaultVal(parser, "molecule_title", settings.molecule_title);
		settings.show_info_card = root.getChildBoolValueWithDefaultVal(parser, "show_info_card", settings.show_info_card);
		settings.info_card_mode = root.getChildStringValueWithDefaultVal(parser, "info_card_mode", settings.info_card_mode);
		settings.info_card_scale = (float)root.getChildDoubleValueWithDefaultVal(parser, "info_card_scale", settings.info_card_scale);
		settings.info_card_distance = (float)root.getChildDoubleValueWithDefaultVal(parser, "info_card_distance", settings.info_card_distance);
		settings.info_card_pinned = root.getChildBoolValueWithDefaultVal(parser, "info_card_pinned", settings.info_card_pinned);
		settings.show_legend = root.getChildBoolValueWithDefaultVal(parser, "show_legend", settings.show_legend);
		settings.show_hydrogen = root.getChildBoolValueWithDefaultVal(parser, "show_hydrogen", settings.show_hydrogen);
		settings.label_mode = root.getChildStringValueWithDefaultVal(parser, "label_mode", settings.label_mode);
		settings.label_scale = (float)root.getChildDoubleValueWithDefaultVal(parser, "label_scale", settings.label_scale);
		settings.molecule_title_scale = (float)root.getChildDoubleValueWithDefaultVal(parser, "molecule_title_scale", settings.molecule_title_scale);
		settings.label_colour = colourFromHex(root.getChildStringValueWithDefaultVal(parser, "label_colour", colourToHex(settings.label_colour)), settings.label_colour);
		settings.label_max_count = root.getChildIntValueWithDefaultVal(parser, "label_max_count", settings.label_max_count);
		settings.label_max_distance = (float)root.getChildDoubleValueWithDefaultVal(parser, "label_max_distance", settings.label_max_distance);
		settings.label_runtime_status = root.getChildStringValueWithDefaultVal(parser, "label_runtime_status", settings.label_runtime_status);
		settings.lod_level = root.getChildIntValueWithDefaultVal(parser, "lod_level", settings.lod_level);
		settings.glow_enabled = root.getChildBoolValueWithDefaultVal(parser, "glow_enabled", settings.glow_enabled);
		settings.glow_strength = (float)root.getChildDoubleValueWithDefaultVal(parser, "glow_strength", settings.glow_strength);
		settings.outline_enabled = root.getChildBoolValueWithDefaultVal(parser, "outline_enabled", settings.outline_enabled);
		settings.wireframe_enabled = root.getChildBoolValueWithDefaultVal(parser, "wireframe_enabled", settings.wireframe_enabled);

		settings.measure_distance = root.getChildBoolValueWithDefaultVal(parser, "measure_distance", settings.measure_distance);
		settings.measure_angle = root.getChildBoolValueWithDefaultVal(parser, "measure_angle", settings.measure_angle);
		settings.measure_torsion = root.getChildBoolValueWithDefaultVal(parser, "measure_torsion", settings.measure_torsion);
		settings.measure_area = root.getChildBoolValueWithDefaultVal(parser, "measure_area", settings.measure_area);
		settings.measure_volume = root.getChildBoolValueWithDefaultVal(parser, "measure_volume", settings.measure_volume);
		settings.selection_mode = root.getChildStringValueWithDefaultVal(parser, "selection_mode", settings.selection_mode);
		settings.selection_state = root.getChildStringValueWithDefaultVal(parser, "selection_state", settings.selection_state);
		settings.selected_atom_indices = root.getChildStringValueWithDefaultVal(parser, "selected_atom_indices", settings.selected_atom_indices);
		settings.selected_bond_index = root.getChildIntValueWithDefaultVal(parser, "selected_bond_index", settings.selected_bond_index);
		settings.measurements_json = root.getChildStringValueWithDefaultVal(parser, "measurements_json", settings.measurements_json);
		settings.atom_count = root.getChildIntValueWithDefaultVal(parser, "atom_count", settings.atom_count);
		settings.bond_count = root.getChildIntValueWithDefaultVal(parser, "bond_count", settings.bond_count);
		settings.point_count = root.getChildIntValueWithDefaultVal(parser, "point_count", settings.point_count);
		settings.object_dimensions = root.getChildStringValueWithDefaultVal(parser, "object_dimensions", settings.object_dimensions);

		settings.rotation_animation_enabled = root.getChildBoolValueWithDefaultVal(parser, "rotation_animation_enabled", settings.rotation_animation_enabled);
		settings.trajectory_animation_enabled = root.getChildBoolValueWithDefaultVal(parser, "trajectory_animation_enabled", settings.trajectory_animation_enabled);
		settings.vibration_animation_enabled = root.getChildBoolValueWithDefaultVal(parser, "vibration_animation_enabled", settings.vibration_animation_enabled);
		settings.time_series_enabled = root.getChildBoolValueWithDefaultVal(parser, "time_series_enabled", settings.time_series_enabled);
		settings.animation_speed = (float)root.getChildDoubleValueWithDefaultVal(parser, "animation_speed", settings.animation_speed);
		settings.animation_direction = root.getChildStringValueWithDefaultVal(parser, "animation_direction", settings.animation_direction);
		settings.current_frame = root.getChildIntValueWithDefaultVal(parser, "current_frame", settings.current_frame);
		settings.frame_count = root.getChildIntValueWithDefaultVal(parser, "frame_count", settings.frame_count);
		settings.animation_runtime_status = root.getChildStringValueWithDefaultVal(parser, "animation_runtime_status", settings.animation_runtime_status);

		settings.simulation_enabled = root.getChildBoolValueWithDefaultVal(parser, "simulation_enabled", settings.simulation_enabled);
		settings.simulation_type = root.getChildStringValueWithDefaultVal(parser, "simulation_type", settings.simulation_type);
		settings.simulation_notes = root.getChildStringValueWithDefaultVal(parser, "simulation_notes", settings.simulation_notes);
		settings.provider_classifications = root.getChildStringValueWithDefaultVal(parser, "provider_classifications", settings.provider_classifications);
		settings.computed_classifications = root.getChildStringValueWithDefaultVal(parser, "computed_classifications", settings.computed_classifications);
		settings.user_collections = root.getChildStringValueWithDefaultVal(parser, "user_collections", settings.user_collections);
		settings.favorite = root.getChildBoolValueWithDefaultVal(parser, "favorite", settings.favorite);
		settings.section_cache_manifest = root.getChildStringValueWithDefaultVal(parser, "section_cache_manifest", settings.section_cache_manifest);

		settings.ai_provider = root.getChildStringValueWithDefaultVal(parser, "ai_provider", settings.ai_provider);
		settings.ai_model = root.getChildStringValueWithDefaultVal(parser, "ai_model", settings.ai_model);
		settings.ai_endpoint = root.getChildStringValueWithDefaultVal(parser, "ai_endpoint", settings.ai_endpoint);
		settings.ai_uses_user_credentials = root.getChildBoolValueWithDefaultVal(parser, "ai_uses_user_credentials", settings.ai_uses_user_credentials);
		settings.collision_enabled = root.getChildBoolValueWithDefaultVal(parser, "collision_enabled", settings.collision_enabled);
		settings.solid = root.getChildBoolValueWithDefaultVal(parser, "solid", settings.solid);
		settings.trigger = root.getChildBoolValueWithDefaultVal(parser, "trigger", settings.trigger);
		settings.selectable = root.getChildBoolValueWithDefaultVal(parser, "selectable", settings.selectable);
		settings.movable = root.getChildBoolValueWithDefaultVal(parser, "movable", settings.movable);
		settings.gravity_enabled = root.getChildBoolValueWithDefaultVal(parser, "gravity_enabled", settings.gravity_enabled);
		settings.physics_motion_type = root.getChildStringValueWithDefaultVal(parser, "physics_motion_type", settings.physics_motion_type);
		settings.physics_shape = root.getChildStringValueWithDefaultVal(parser, "physics_shape", settings.physics_shape);
		settings.collision_layer = root.getChildStringValueWithDefaultVal(parser, "collision_layer", settings.collision_layer);
		settings.physics_mass = (float)root.getChildDoubleValueWithDefaultVal(parser, "physics_mass", settings.physics_mass);
		settings.physics_friction = (float)root.getChildDoubleValueWithDefaultVal(parser, "physics_friction", settings.physics_friction);
		settings.physics_restitution = (float)root.getChildDoubleValueWithDefaultVal(parser, "physics_restitution", settings.physics_restitution);
		settings.custom_properties = root.getChildStringValueWithDefaultVal(parser, "custom_properties", settings.custom_properties);
	}
	catch(...)
	{
		if(parse_error_out)
			*parse_error_out = "Failed to parse scientific object settings.";
		return defaultObject();
	}

	clampSettings(settings);
	return settings;
}


std::string ScientificObjectSettings::serialiseToContent(const ScientificObjectSettings& settings_)
{
	ScientificObjectSettings settings = settings_;
	clampSettings(settings);

	std::ostringstream s;
	s << SCIENTIFIC_OBJECT_MARKER << "\n";
	s << std::setprecision(8);
	s << "{\n";
	s << "  \"schema_version\": " << settings.schema_version << ",\n";
	s << "  \"name\": \"" << jsonEscape(settings.name) << "\",\n";
	s << "  \"scientific_type\": \"" << jsonEscape(settings.scientific_type) << "\",\n";
	s << "  \"description\": \"" << jsonEscape(settings.description) << "\",\n";
	s << "  \"source\": \"" << jsonEscape(settings.source) << "\",\n";
	s << "  \"author\": \"" << jsonEscape(settings.author) << "\",\n";
	s << "  \"tags\": \"" << jsonEscape(settings.tags) << "\",\n";
	s << "  \"uuid\": \"" << jsonEscape(settings.uuid) << "\",\n";
	s << "  \"created_time\": \"" << jsonEscape(settings.created_time) << "\",\n";
	s << "  \"modified_time\": \"" << jsonEscape(settings.modified_time) << "\",\n";
	s << "  \"source_mode\": \"" << jsonEscape(settings.source_mode) << "\",\n";
	s << "  \"file_path\": \"" << jsonEscape(settings.file_path) << "\",\n";
	s << "  \"source_url\": \"" << jsonEscape(settings.source_url) << "\",\n";
	s << "  \"online_database\": \"" << jsonEscape(settings.online_database) << "\",\n";
	s << "  \"online_query\": \"" << jsonEscape(settings.online_query) << "\",\n";
	s << "  \"online_result_id\": \"" << jsonEscape(settings.online_result_id) << "\",\n";
	s << "  \"load_status\": \"" << jsonEscape(settings.load_status) << "\",\n";
	s << "  \"load_status_message\": \"" << jsonEscape(settings.load_status_message) << "\",\n";
	s << "  \"data_origin\": \"" << jsonEscape(settings.data_origin) << "\",\n";
	s << "  \"provenance_source\": \"" << jsonEscape(settings.provenance_source) << "\",\n";
	s << "  \"provenance_identifier\": \"" << jsonEscape(settings.provenance_identifier) << "\",\n";
	s << "  \"provenance_url\": \"" << jsonEscape(settings.provenance_url) << "\",\n";
	s << "  \"provenance_author\": \"" << jsonEscape(settings.provenance_author) << "\",\n";
	s << "  \"provenance_loaded_at\": \"" << jsonEscape(settings.provenance_loaded_at) << "\",\n";
	s << "  \"provenance_format\": \"" << jsonEscape(settings.provenance_format) << "\",\n";
	s << "  \"provenance_version\": \"" << jsonEscape(settings.provenance_version) << "\",\n";
	s << "  \"provenance_license\": \"" << jsonEscape(settings.provenance_license) << "\",\n";
	s << "  \"provenance_checksum\": \"" << jsonEscape(settings.provenance_checksum) << "\",\n";
	s << "  \"molecule_model_version\": \"" << jsonEscape(settings.molecule_model_version) << "\",\n";
	s << "  \"provider_adapter_version\": \"" << jsonEscape(settings.provider_adapter_version) << "\",\n";
	s << "  \"parser_version\": \"" << jsonEscape(settings.parser_version) << "\",\n";
	s << "  \"cache_version\": \"" << jsonEscape(settings.cache_version) << "\",\n";
	s << "  \"visualization_settings_version\": \"" << jsonEscape(settings.visualization_settings_version) << "\",\n";
	s << "  \"source_data_cache_key\": \"" << jsonEscape(settings.source_data_cache_key) << "\",\n";
	s << "  \"source_data_cache_path\": \"" << jsonEscape(settings.source_data_cache_path) << "\",\n";
	s << "  \"image_url\": \"" << jsonEscape(settings.image_url) << "\",\n";
	s << "  \"image_cache_path\": \"" << jsonEscape(settings.image_cache_path) << "\",\n";
	s << "  \"image_checksum\": \"" << jsonEscape(settings.image_checksum) << "\",\n";
	s << "  \"conformer_status\": \"" << jsonEscape(settings.conformer_status) << "\",\n";
	s << "  \"search_original_query\": \"" << jsonEscape(settings.search_original_query) << "\",\n";
	s << "  \"search_normalized_query\": \"" << jsonEscape(settings.search_normalized_query) << "\",\n";
	s << "  \"search_translation\": \"" << jsonEscape(settings.search_translation) << "\",\n";
	s << "  \"code_language\": \"" << jsonEscape(settings.code_language) << "\",\n";
	s << "  \"code_text\": \"" << jsonEscape(settings.code_text) << "\",\n";
	s << "  \"prompt_text\": \"" << jsonEscape(settings.prompt_text) << "\",\n";
	s << "  \"generated_code\": \"" << jsonEscape(settings.generated_code) << "\",\n";
	s << "  \"data_summary\": \"" << jsonEscape(settings.data_summary) << "\",\n";
	s << "  \"atom_table\": \"" << jsonEscape(settings.atom_table) << "\",\n";
	s << "  \"bond_table\": \"" << jsonEscape(settings.bond_table) << "\",\n";
	s << "  \"point_table\": \"" << jsonEscape(settings.point_table) << "\",\n";
	s << "  \"value_table\": \"" << jsonEscape(settings.value_table) << "\",\n";
	s << "  \"property_table\": \"" << jsonEscape(settings.property_table) << "\",\n";
	s << "  \"visualization_mode\": \"" << jsonEscape(settings.visualization_mode) << "\",\n";
	s << "  \"colour_scheme\": \"" << jsonEscape(settings.colour_scheme) << "\",\n";
	s << "  \"display_colour\": \"" << colourToHex(settings.display_colour) << "\",\n";
	s << "  \"material\": \"" << jsonEscape(settings.material) << "\",\n";
	s << "  \"atom_radius\": " << settings.atom_radius << ",\n";
	s << "  \"bond_thickness\": " << settings.bond_thickness << ",\n";
	s << "  \"point_size\": " << settings.point_size << ",\n";
	s << "  \"line_width\": " << settings.line_width << ",\n";
	s << "  \"opacity\": " << settings.opacity << ",\n";
	s << "  \"object_scale\": " << settings.object_scale << ",\n";
	s << "  \"show_labels\": " << (settings.show_labels ? "true" : "false") << ",\n";
	s << "  \"show_molecule_title\": " << (settings.show_molecule_title ? "true" : "false") << ",\n";
	s << "  \"molecule_title\": \"" << jsonEscape(settings.molecule_title) << "\",\n";
	s << "  \"show_info_card\": " << (settings.show_info_card ? "true" : "false") << ",\n";
	s << "  \"info_card_mode\": \"" << jsonEscape(settings.info_card_mode) << "\",\n";
	s << "  \"info_card_scale\": " << settings.info_card_scale << ",\n";
	s << "  \"info_card_distance\": " << settings.info_card_distance << ",\n";
	s << "  \"info_card_pinned\": " << (settings.info_card_pinned ? "true" : "false") << ",\n";
	s << "  \"show_legend\": " << (settings.show_legend ? "true" : "false") << ",\n";
	s << "  \"show_hydrogen\": " << (settings.show_hydrogen ? "true" : "false") << ",\n";
	s << "  \"label_mode\": \"" << jsonEscape(settings.label_mode) << "\",\n";
	s << "  \"label_scale\": " << settings.label_scale << ",\n";
	s << "  \"molecule_title_scale\": " << settings.molecule_title_scale << ",\n";
	s << "  \"label_colour\": \"" << colourToHex(settings.label_colour) << "\",\n";
	s << "  \"label_max_count\": " << settings.label_max_count << ",\n";
	s << "  \"label_max_distance\": " << settings.label_max_distance << ",\n";
	s << "  \"label_runtime_status\": \"" << jsonEscape(settings.label_runtime_status) << "\",\n";
	s << "  \"lod_level\": " << settings.lod_level << ",\n";
	s << "  \"glow_enabled\": " << (settings.glow_enabled ? "true" : "false") << ",\n";
	s << "  \"glow_strength\": " << settings.glow_strength << ",\n";
	s << "  \"outline_enabled\": " << (settings.outline_enabled ? "true" : "false") << ",\n";
	s << "  \"wireframe_enabled\": " << (settings.wireframe_enabled ? "true" : "false") << ",\n";
	s << "  \"measure_distance\": " << (settings.measure_distance ? "true" : "false") << ",\n";
	s << "  \"measure_angle\": " << (settings.measure_angle ? "true" : "false") << ",\n";
	s << "  \"measure_torsion\": " << (settings.measure_torsion ? "true" : "false") << ",\n";
	s << "  \"measure_area\": " << (settings.measure_area ? "true" : "false") << ",\n";
	s << "  \"measure_volume\": " << (settings.measure_volume ? "true" : "false") << ",\n";
	s << "  \"selection_mode\": \"" << jsonEscape(settings.selection_mode) << "\",\n";
	s << "  \"selection_state\": \"" << jsonEscape(settings.selection_state) << "\",\n";
	s << "  \"selected_atom_indices\": \"" << jsonEscape(settings.selected_atom_indices) << "\",\n";
	s << "  \"selected_bond_index\": " << settings.selected_bond_index << ",\n";
	s << "  \"measurements_json\": \"" << jsonEscape(settings.measurements_json) << "\",\n";
	s << "  \"atom_count\": " << settings.atom_count << ",\n";
	s << "  \"bond_count\": " << settings.bond_count << ",\n";
	s << "  \"point_count\": " << settings.point_count << ",\n";
	s << "  \"object_dimensions\": \"" << jsonEscape(settings.object_dimensions) << "\",\n";
	s << "  \"rotation_animation_enabled\": " << (settings.rotation_animation_enabled ? "true" : "false") << ",\n";
	s << "  \"trajectory_animation_enabled\": " << (settings.trajectory_animation_enabled ? "true" : "false") << ",\n";
	s << "  \"vibration_animation_enabled\": " << (settings.vibration_animation_enabled ? "true" : "false") << ",\n";
	s << "  \"time_series_enabled\": " << (settings.time_series_enabled ? "true" : "false") << ",\n";
	s << "  \"animation_speed\": " << settings.animation_speed << ",\n";
	s << "  \"animation_direction\": \"" << jsonEscape(settings.animation_direction) << "\",\n";
	s << "  \"current_frame\": " << settings.current_frame << ",\n";
	s << "  \"frame_count\": " << settings.frame_count << ",\n";
	s << "  \"animation_runtime_status\": \"" << jsonEscape(settings.animation_runtime_status) << "\",\n";
	s << "  \"simulation_enabled\": " << (settings.simulation_enabled ? "true" : "false") << ",\n";
	s << "  \"simulation_type\": \"" << jsonEscape(settings.simulation_type) << "\",\n";
	s << "  \"simulation_notes\": \"" << jsonEscape(settings.simulation_notes) << "\",\n";
	s << "  \"provider_classifications\": \"" << jsonEscape(settings.provider_classifications) << "\",\n";
	s << "  \"computed_classifications\": \"" << jsonEscape(settings.computed_classifications) << "\",\n";
	s << "  \"user_collections\": \"" << jsonEscape(settings.user_collections) << "\",\n";
	s << "  \"favorite\": " << (settings.favorite ? "true" : "false") << ",\n";
	s << "  \"section_cache_manifest\": \"" << jsonEscape(settings.section_cache_manifest) << "\",\n";
	s << "  \"ai_provider\": \"" << jsonEscape(settings.ai_provider) << "\",\n";
	s << "  \"ai_model\": \"" << jsonEscape(settings.ai_model) << "\",\n";
	s << "  \"ai_endpoint\": \"" << jsonEscape(settings.ai_endpoint) << "\",\n";
	s << "  \"ai_uses_user_credentials\": " << (settings.ai_uses_user_credentials ? "true" : "false") << ",\n";
	s << "  \"collision_enabled\": " << (settings.collision_enabled ? "true" : "false") << ",\n";
	s << "  \"solid\": " << (settings.solid ? "true" : "false") << ",\n";
	s << "  \"trigger\": " << (settings.trigger ? "true" : "false") << ",\n";
	s << "  \"selectable\": " << (settings.selectable ? "true" : "false") << ",\n";
	s << "  \"movable\": " << (settings.movable ? "true" : "false") << ",\n";
	s << "  \"gravity_enabled\": " << (settings.gravity_enabled ? "true" : "false") << ",\n";
	s << "  \"physics_motion_type\": \"" << jsonEscape(settings.physics_motion_type) << "\",\n";
	s << "  \"physics_shape\": \"" << jsonEscape(settings.physics_shape) << "\",\n";
	s << "  \"collision_layer\": \"" << jsonEscape(settings.collision_layer) << "\",\n";
	s << "  \"physics_mass\": " << settings.physics_mass << ",\n";
	s << "  \"physics_friction\": " << settings.physics_friction << ",\n";
	s << "  \"physics_restitution\": " << settings.physics_restitution << ",\n";
	s << settings.preserved_json_fields;
	s << "  \"custom_properties\": \"" << jsonEscape(settings.custom_properties) << "\"\n";
	s << "}\n";
	return s.str();
}
