/*=====================================================================
CulturalObjectSettings.h
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <string>


// Bounded, versioned descriptor stored in WorldObject::content.  Large source
// responses and media are referenced by URL/cache key and never embedded here.
struct CulturalObjectSettings
{
	CulturalObjectSettings();

	static const char* contentMarker();
	static bool isCulturalObjectContent(const std::string& content);
	static CulturalObjectSettings defaultObject();
	static CulturalObjectSettings fromContent(const std::string& content, std::string* parse_error_out = 0);
	static std::string serialiseToContent(const CulturalObjectSettings& settings);

	int schema_version;
	std::string uuid;
	std::string title;
	std::string object_type;
	std::string cultural_category;
	std::string description;
	std::string source_mode;
	std::string local_file;
	std::string source_url;
	std::string provider_id;
	std::string provider_record_id;
	std::string retrieval_status;

	std::string alternative_titles;
	std::string creators;
	std::string creation_date;
	std::string country;
	std::string place_of_creation;
	std::string current_location;
	std::string collection;
	std::string inventory_number;
	std::string art_forms;
	std::string museum_classifications;
	std::string disciplines;
	std::string cultures;
	std::string periods;
	std::string materials;
	std::string techniques;
	std::string styles;
	std::string genres;
	std::string functions;
	std::string subjects;
	std::string keywords;
	std::string wikidata_id;
	std::string iiif_id;
	std::string museum_id;
	std::string europeana_id;

	std::string card_title;
	std::string card_subtitle;
	std::string card_summary;
	std::string card_theme;
	std::string card_language;
	std::string card_visible_fields;
	std::string plaque_text;
	bool card_auto_open;
	bool card_open_on_click;
	bool card_pinned;
	float card_scale;

	std::string primary_image_url;
	std::string high_resolution_image_url;
	std::string iiif_manifest_url;
	std::string model_3d_url;
	std::string audio_url;
	std::string video_url;
	std::string documents;
	std::string media_cache_key;
	bool lazy_media_loading;

	std::string license_status;
	std::string rights_holder;
	std::string license_url;
	std::string attribution_text;
	bool allow_display;
	bool allow_download;
	bool allow_modify;
	bool allow_commercial_use;

	std::string provenance;
	std::string exhibitions;
	std::string restorations;
	std::string condition;
	std::string publications;
	std::string related_objects;

	std::string exhibition_scene;
	std::string exhibition_room;
	std::string exhibition_zone;
	std::string placement;
	std::string frame_style;
	std::string pedestal_style;
	std::string case_style;
	bool spotlight;
	float light_intensity;
	bool shadows;
	bool interactive;
	float activation_distance;
	std::string route_id;
	std::string route_stop;
	std::string next_object_uuid;
	std::string curator_note;

	std::string source_records_ref;
	std::string raw_source_ref;
	std::string modified_at;
	std::string preserved_json_fields;
};
