/*=====================================================================
DocumentObjectSettings.cpp
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "DocumentObjectSettings.h"


#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QString>
#include <algorithm>


namespace
{
static QString toQString(const std::string& value)
{
	return QString::fromUtf8(value.data(), (int)value.size());
}


static std::string toStdString(const QString& value)
{
	const QByteArray bytes = value.toUtf8();
	return std::string(bytes.constData(), (std::size_t)bytes.size());
}


static std::string boundedString(const QJsonObject& object, const char* key, const std::string& fallback, int max_bytes)
{
	const QJsonValue value = object.value(QString::fromLatin1(key));
	if(!value.isString())
		return fallback;

	QByteArray bytes = value.toString().toUtf8();
	if(bytes.size() > max_bytes)
		bytes.truncate(max_bytes);
	return std::string(bytes.constData(), (std::size_t)bytes.size());
}


static bool boolValue(const QJsonObject& object, const char* key, bool fallback)
{
	const QJsonValue value = object.value(QString::fromLatin1(key));
	return value.isBool() ? value.toBool() : fallback;
}


static int intValue(const QJsonObject& object, const char* key, int fallback, int minimum, int maximum)
{
	const QJsonValue value = object.value(QString::fromLatin1(key));
	return value.isDouble() ? std::max(minimum, std::min(maximum, value.toInt())) : fallback;
}


static float floatValue(const QJsonObject& object, const char* key, float fallback, float minimum, float maximum)
{
	const QJsonValue value = object.value(QString::fromLatin1(key));
	return value.isDouble() ? std::max(minimum, std::min(maximum, (float)value.toDouble())) : fallback;
}


static void putString(QJsonObject& object, const char* key, const std::string& value)
{
	object.insert(QString::fromLatin1(key), toQString(value));
}


static bool validateLength(const std::string& value, std::size_t max_bytes, const char* field, std::string* error_out)
{
	if(value.size() <= max_bytes)
		return true;
	if(error_out)
		*error_out = std::string("Document field '") + field + "' exceeds its bounded UTF-8 size.";
	return false;
}
}


DocumentObjectSettings::DocumentObjectSettings()
:	schema_version(1),
	language("ru"),
	document_format("plain_text"),
	source_mode("manual"),
	encoding("UTF-8"),
	display_mode("virtual_screen"),
	open_on_click(true),
	pinned_to_object(false),
	allow_text_selection(true),
	show_toolbar(true),
	initial_page(0),
	zoom(1.f),
	screen_width(1.6f),
	screen_height(0.9f),
	rotation_degrees(0.f)
{}


const char* DocumentObjectSettings::contentMarker()
{
	return "metasiberia_document_object_v1\n";
}


bool DocumentObjectSettings::isDocumentObjectContent(const std::string& content)
{
	const std::string marker(contentMarker());
	return content.size() >= marker.size() && content.compare(0, marker.size(), marker) == 0;
}


DocumentObjectSettings DocumentObjectSettings::defaultObject()
{
	return DocumentObjectSettings();
}


DocumentObjectSettings DocumentObjectSettings::fromContent(const std::string& content, std::string* parse_error_out)
{
	DocumentObjectSettings settings;
	if(parse_error_out)
		parse_error_out->clear();

	if(content.size() > MAX_CONTENT_BYTES)
	{
		if(parse_error_out)
			*parse_error_out = "Document descriptor exceeds the 10000 byte WorldObject content limit.";
		return settings;
	}
	if(!isDocumentObjectContent(content))
	{
		if(parse_error_out)
			*parse_error_out = "Not a Metasiberia document object v1 descriptor.";
		return settings;
	}

	const std::size_t marker_size = std::string(contentMarker()).size();
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(
		QByteArray(content.data() + marker_size, (int)(content.size() - marker_size)), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
	{
		if(parse_error_out)
			*parse_error_out = toStdString(parse_error.errorString());
		return settings;
	}

	const QJsonObject object = document.object();
	const QByteArray preserved = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if(preserved.size() <= 4096)
		settings.preserved_json_fields = std::string(preserved.constData(), (std::size_t)preserved.size());

	settings.schema_version = intValue(object, "schema_version", 1, 1, 1);
#define READ_SMALL(field) settings.field = boundedString(object, #field, settings.field, 512)
#define READ_MEDIUM(field) settings.field = boundedString(object, #field, settings.field, 2048)
	READ_SMALL(uuid); READ_SMALL(title); READ_SMALL(author); READ_MEDIUM(description); READ_SMALL(language); READ_MEDIUM(tags);
	READ_SMALL(document_format); READ_SMALL(source_mode); READ_MEDIUM(local_file_path); READ_MEDIUM(source_url); READ_MEDIUM(resource_url);
	READ_SMALL(encoding); READ_SMALL(checksum); READ_SMALL(linked_object_uid); READ_SMALL(linked_object_uuid);
	READ_MEDIUM(sources); READ_MEDIUM(comments); READ_SMALL(display_mode); READ_SMALL(created_at); READ_SMALL(modified_at);
#undef READ_SMALL
#undef READ_MEDIUM

	settings.open_on_click = boolValue(object, "open_on_click", settings.open_on_click);
	settings.pinned_to_object = boolValue(object, "pinned_to_object", settings.pinned_to_object);
	settings.allow_text_selection = boolValue(object, "allow_text_selection", settings.allow_text_selection);
	settings.show_toolbar = boolValue(object, "show_toolbar", settings.show_toolbar);
	settings.initial_page = intValue(object, "initial_page", settings.initial_page, 0, 1000000);
	settings.zoom = floatValue(object, "zoom", settings.zoom, 0.05f, 32.f);
	settings.screen_width = floatValue(object, "screen_width", settings.screen_width, 0.05f, 100.f);
	settings.screen_height = floatValue(object, "screen_height", settings.screen_height, 0.05f, 100.f);
	settings.rotation_degrees = floatValue(object, "rotation_degrees", settings.rotation_degrees, -36000.f, 36000.f);
	return settings;
}


bool DocumentObjectSettings::trySerialiseToContent(const DocumentObjectSettings& input, std::string& content_out, std::string* error_out)
{
	content_out.clear();
	if(error_out)
		error_out->clear();

	// Do not silently truncate user metadata: a future editor integration can
	// show this exact error and keep the local value intact.
#define VALIDATE_SMALL(field) if(!validateLength(input.field, 512, #field, error_out)) return false
#define VALIDATE_MEDIUM(field) if(!validateLength(input.field, 2048, #field, error_out)) return false
	VALIDATE_SMALL(uuid); VALIDATE_SMALL(title); VALIDATE_SMALL(author); VALIDATE_MEDIUM(description); VALIDATE_SMALL(language); VALIDATE_MEDIUM(tags);
	VALIDATE_SMALL(document_format); VALIDATE_SMALL(source_mode); VALIDATE_MEDIUM(source_url); VALIDATE_MEDIUM(resource_url);
	VALIDATE_SMALL(encoding); VALIDATE_SMALL(checksum); VALIDATE_SMALL(linked_object_uid); VALIDATE_SMALL(linked_object_uuid);
	VALIDATE_MEDIUM(sources); VALIDATE_MEDIUM(comments); VALIDATE_SMALL(display_mode); VALIDATE_SMALL(created_at); VALIDATE_SMALL(modified_at);
#undef VALIDATE_SMALL
#undef VALIDATE_MEDIUM

	QJsonObject object;
	if(!input.preserved_json_fields.empty() && input.preserved_json_fields.size() <= 4096)
	{
		const QJsonDocument preserved = QJsonDocument::fromJson(QByteArray::fromStdString(input.preserved_json_fields));
		if(preserved.isObject())
			object = preserved.object();
	}

	object.insert(QStringLiteral("schema_version"), 1);
	// A local absolute path is client-only state and must never be persisted in
	// world JSON.  Remove it explicitly in case it came from a legacy preserved
	// descriptor; resource_url is the portable reference shared by clients.
	object.remove(QStringLiteral("local_file_path"));
#define PUT_SMALL(field) putString(object, #field, input.field)
#define PUT_MEDIUM(field) putString(object, #field, input.field)
	PUT_SMALL(uuid); PUT_SMALL(title); PUT_SMALL(author); PUT_MEDIUM(description); PUT_SMALL(language); PUT_MEDIUM(tags);
	PUT_SMALL(document_format); PUT_SMALL(source_mode); PUT_MEDIUM(source_url); PUT_MEDIUM(resource_url);
	PUT_SMALL(encoding); PUT_SMALL(checksum); PUT_SMALL(linked_object_uid); PUT_SMALL(linked_object_uuid);
	PUT_MEDIUM(sources); PUT_MEDIUM(comments); PUT_SMALL(display_mode); PUT_SMALL(created_at); PUT_SMALL(modified_at);
#undef PUT_SMALL
#undef PUT_MEDIUM

	object.insert(QStringLiteral("open_on_click"), input.open_on_click);
	object.insert(QStringLiteral("pinned_to_object"), input.pinned_to_object);
	object.insert(QStringLiteral("allow_text_selection"), input.allow_text_selection);
	object.insert(QStringLiteral("show_toolbar"), input.show_toolbar);
	object.insert(QStringLiteral("initial_page"), std::max(0, std::min(1000000, input.initial_page)));
	object.insert(QStringLiteral("zoom"), (double)std::max(0.05f, std::min(32.f, input.zoom)));
	object.insert(QStringLiteral("screen_width"), (double)std::max(0.05f, std::min(100.f, input.screen_width)));
	object.insert(QStringLiteral("screen_height"), (double)std::max(0.05f, std::min(100.f, input.screen_height)));
	object.insert(QStringLiteral("rotation_degrees"), (double)std::max(-36000.f, std::min(36000.f, input.rotation_degrees)));

	const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
	const std::string candidate = std::string(contentMarker()) + std::string(json.constData(), (std::size_t)json.size());
	if(candidate.size() > MAX_CONTENT_BYTES)
	{
		if(error_out)
			*error_out = "Document descriptor exceeds the 10000 byte WorldObject content limit. Move long metadata to a referenced resource.";
		return false;
	}

	content_out = candidate;
	return true;
}


std::string DocumentObjectSettings::serialiseToContent(const DocumentObjectSettings& settings)
{
	std::string content;
	return trySerialiseToContent(settings, content) ? content : std::string();
}
