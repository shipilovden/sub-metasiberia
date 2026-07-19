/*=====================================================================
DocumentObjectSettings.h
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <cstddef>
#include <string>


// Versioned descriptor stored in WorldObject::content for a Metasiberia
// document object.  Document bytes are deliberately not embedded here:
// local files, URLs and future content-addressed resources are referenced.
struct DocumentObjectSettings
{
	DocumentObjectSettings();

	static const char* contentMarker();
	static bool isDocumentObjectContent(const std::string& content);
	static DocumentObjectSettings defaultObject();
	static DocumentObjectSettings fromContent(const std::string& content, std::string* parse_error_out = 0);

	// Returns false instead of producing a descriptor that cannot pass the
	// existing WorldObject::MAX_CONTENT_SIZE network boundary.
	static bool trySerialiseToContent(const DocumentObjectSettings& settings, std::string& content_out, std::string* error_out = 0);
	static std::string serialiseToContent(const DocumentObjectSettings& settings);

	static constexpr std::size_t MAX_CONTENT_BYTES = 10000;

	int schema_version;
	std::string uuid;
	std::string title;
	std::string author;
	std::string description;
	std::string language;
	std::string tags;

	std::string document_format;
	std::string source_mode;
	std::string local_file_path;
	std::string source_url;
	std::string resource_url;
	std::string encoding;
	std::string checksum;

	std::string linked_object_uid;
	std::string linked_object_uuid;
	std::string sources;
	std::string comments;

	std::string display_mode;
	bool open_on_click;
	bool pinned_to_object;
	bool allow_text_selection;
	bool show_toolbar;
	int initial_page;
	float zoom;
	float screen_width;
	float screen_height;
	float rotation_degrees;

	std::string created_at;
	std::string modified_at;
	std::string preserved_json_fields;
};
