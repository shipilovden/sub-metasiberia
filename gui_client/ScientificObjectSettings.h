/*=====================================================================
ScientificObjectSettings.h
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <graphics/colour3.h>
#include <string>


struct ScientificObjectSettings
{
	ScientificObjectSettings();

	static const char* contentMarker();
	static bool isScientificObjectContent(const std::string& content);
	static ScientificObjectSettings defaultObject();
	static ScientificObjectSettings fromContent(const std::string& content, std::string* parse_error_out = 0);
	static std::string serialiseToContent(const ScientificObjectSettings& settings);

	int schema_version;
	std::string name;
	std::string scientific_type;
	std::string description;
	std::string source;
	std::string author;
	std::string tags;
	std::string uuid;
	std::string created_time;
	std::string modified_time;

	std::string source_mode;
	std::string file_path;
	std::string source_url;
	std::string online_database;
	std::string online_query;
	std::string online_result_id;
	std::string load_status;
	std::string load_status_message;
	std::string data_origin;
	std::string provenance_source;
	std::string provenance_identifier;
	std::string provenance_url;
	std::string provenance_author;
	std::string provenance_loaded_at;
	std::string provenance_format;
	std::string provenance_version;
	std::string provenance_license;
	std::string provenance_checksum;
	std::string molecule_model_version;
	std::string provider_adapter_version;
	std::string parser_version;
	std::string cache_version;
	std::string visualization_settings_version;
	std::string source_data_cache_key;
	std::string source_data_cache_path;
	std::string image_url;
	std::string image_cache_path;
	std::string image_checksum;
	std::string conformer_status;
	std::string search_original_query;
	std::string search_normalized_query;
	std::string search_translation;
	std::string code_language;
	std::string code_text;
	std::string prompt_text;
	std::string generated_code;

	std::string data_summary;
	std::string atom_table;
	std::string bond_table;
	std::string point_table;
	std::string value_table;
	std::string property_table;

	std::string visualization_mode;
	std::string colour_scheme;
	Colour3f display_colour;
	std::string material;
	float atom_radius;
	float bond_thickness;
	float point_size;
	float line_width;
	float opacity;
	float object_scale;
	bool show_labels;
	bool atom_labels_pinned;
	bool show_molecule_title;
	std::string molecule_title;
	bool molecule_title_pinned;
	bool show_info_card;
	std::string info_card_mode;
	float info_card_scale;
	float info_card_distance;
	bool info_card_pinned;
	bool info_card_dark_background;
	std::string info_card_stand_type;
	bool info_card_auto_fit_text;
	float info_card_stand_width;
	float info_card_stand_height;
	float info_card_stand_depth;
	bool show_legend;
	bool show_hydrogen;
	std::string label_mode;
	float label_scale;
	float molecule_title_scale;
	Colour3f label_colour;
	int label_max_count;
	float label_max_distance;
	std::string label_runtime_status;
	int lod_level;
	bool glow_enabled;
	float glow_strength;
	bool outline_enabled;
	bool wireframe_enabled;

	bool measure_distance;
	bool measure_angle;
	bool measure_torsion;
	bool measure_area;
	bool measure_volume;
	std::string selection_mode;
	std::string selection_state;
	std::string selected_atom_indices;
	int selected_bond_index;
	std::string measurements_json;
	int atom_count;
	int bond_count;
	int point_count;
	std::string object_dimensions;

	bool rotation_animation_enabled;
	bool trajectory_animation_enabled;
	bool vibration_animation_enabled;
	bool time_series_enabled;
	float animation_speed;
	std::string animation_direction;
	int current_frame;
	int frame_count;
	std::string animation_runtime_status;

	bool simulation_enabled;
	std::string simulation_type;
	std::string simulation_notes;

	std::string provider_classifications;
	std::string computed_classifications;
	std::string user_collections;
	bool favorite;
	std::string section_cache_manifest;

	std::string ai_provider;
	std::string ai_model;
	std::string ai_endpoint;
	bool ai_uses_user_credentials;

	bool collision_enabled;
	bool solid;
	bool trigger;
	bool selectable;
	bool movable;
	bool gravity_enabled;
	std::string physics_motion_type;
	std::string physics_shape;
	std::string collision_layer;
	float physics_mass;
	float physics_friction;
	float physics_restitution;

	std::string preserved_json_fields;
	std::string custom_properties;
};
