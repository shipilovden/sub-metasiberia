/*=====================================================================
CulturalObjectSettings.cpp
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "CulturalObjectSettings.h"


#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDir>
#include <QtCore/QString>
#include <algorithm>


namespace
{
static QString qs(const std::string& s) { return QString::fromUtf8(s.data(), (int)s.size()); }
static std::string ss(const QString& s) { const QByteArray bytes = s.toUtf8(); return std::string(bytes.constData(), (size_t)bytes.size()); }

static std::string stringValue(const QJsonObject& ob, const char* key, const std::string& fallback = std::string())
{
	const QJsonValue value = ob.value(QString::fromLatin1(key));
	return value.isString() ? ss(value.toString()) : fallback;
}

static bool boolValue(const QJsonObject& ob, const char* key, bool fallback)
{
	const QJsonValue value = ob.value(QString::fromLatin1(key));
	return value.isBool() ? value.toBool() : fallback;
}

static float floatValue(const QJsonObject& ob, const char* key, float fallback)
{
	const QJsonValue value = ob.value(QString::fromLatin1(key));
	return value.isDouble() ? (float)value.toDouble() : fallback;
}

static void putString(QJsonObject& ob, const char* key, const std::string& value)
{
	// Always overwrite known fields, including with an empty string.  The object
	// may have been initialised from preserved forward-compatible JSON; skipping
	// an empty value here would silently resurrect a value cleared in the UI.
	ob.insert(QString::fromLatin1(key), qs(value));
}

static void putPortableReference(QJsonObject& ob, const char* key, const std::string& value)
{
	const QString text = qs(value);
	if(text.isEmpty() || QDir::isAbsolutePath(text))
		ob.remove(QString::fromLatin1(key));
	else
		ob.insert(QString::fromLatin1(key), text);
}

static void putBool(QJsonObject& ob, const char* key, bool value) { ob.insert(QString::fromLatin1(key), value); }
static void putFloat(QJsonObject& ob, const char* key, float value) { ob.insert(QString::fromLatin1(key), (double)value); }
}


CulturalObjectSettings::CulturalObjectSettings()
:	schema_version(1),
	object_type("custom"),
	cultural_category("user_cultural_object"),
	source_mode("manual"),
	provider_id("manual"),
	retrieval_status("idle"),
	card_theme("dark"),
	card_language("ru"),
	card_auto_open(false),
	card_open_on_click(true),
	card_pinned(true),
	card_scale(1.f),
	lazy_media_loading(true),
	license_status("unknown_license"),
	allow_display(false),
	allow_download(false),
	allow_modify(false),
	allow_commercial_use(false),
	placement("free"),
	spotlight(false),
	light_intensity(1.f),
	shadows(true),
	interactive(true),
	activation_distance(3.f)
{}


const char* CulturalObjectSettings::contentMarker() { return "metasiberia_cultural_object_v1\n"; }


bool CulturalObjectSettings::isCulturalObjectContent(const std::string& content)
{
	const std::string marker(contentMarker());
	return content.size() >= marker.size() && content.compare(0, marker.size(), marker) == 0;
}


CulturalObjectSettings CulturalObjectSettings::defaultObject() { return CulturalObjectSettings(); }


CulturalObjectSettings CulturalObjectSettings::fromContent(const std::string& content, std::string* parse_error_out)
{
	CulturalObjectSettings s;
	if(parse_error_out) parse_error_out->clear();
	if(!isCulturalObjectContent(content))
	{
		if(parse_error_out) *parse_error_out = "Not a CulturalObject v1 descriptor.";
		return s;
	}

	const size_t marker_size = std::string(contentMarker()).size();
	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(content.data() + marker_size, (int)(content.size() - marker_size)), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !doc.isObject())
	{
		if(parse_error_out) *parse_error_out = ss(parse_error.errorString());
		return s;
	}

	const QJsonObject o = doc.object();
	s.preserved_json_fields = ss(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
	s.schema_version = std::max(1, o.value(QStringLiteral("schema_version")).toInt(1));
#define READ_STRING(field) s.field = stringValue(o, #field, s.field)
	READ_STRING(uuid); READ_STRING(title); READ_STRING(object_type); READ_STRING(cultural_category); READ_STRING(description);
	READ_STRING(source_mode); READ_STRING(local_file); READ_STRING(source_url); READ_STRING(provider_id); READ_STRING(provider_record_id); READ_STRING(retrieval_status);
	READ_STRING(alternative_titles); READ_STRING(creators); READ_STRING(creation_date); READ_STRING(country); READ_STRING(place_of_creation); READ_STRING(current_location);
	READ_STRING(collection); READ_STRING(inventory_number); READ_STRING(art_forms); READ_STRING(museum_classifications); READ_STRING(disciplines); READ_STRING(cultures);
	READ_STRING(periods); READ_STRING(materials); READ_STRING(techniques); READ_STRING(styles); READ_STRING(genres); READ_STRING(functions); READ_STRING(subjects); READ_STRING(keywords);
	READ_STRING(wikidata_id); READ_STRING(iiif_id); READ_STRING(museum_id); READ_STRING(europeana_id);
	READ_STRING(card_title); READ_STRING(card_subtitle); READ_STRING(card_summary); READ_STRING(card_theme); READ_STRING(card_language); READ_STRING(card_visible_fields); READ_STRING(plaque_text);
	READ_STRING(primary_image_url); READ_STRING(high_resolution_image_url); READ_STRING(iiif_manifest_url); READ_STRING(model_3d_url); READ_STRING(audio_url); READ_STRING(video_url);
	READ_STRING(documents); READ_STRING(media_cache_key); READ_STRING(license_status); READ_STRING(rights_holder); READ_STRING(license_url); READ_STRING(attribution_text);
	READ_STRING(provenance); READ_STRING(exhibitions); READ_STRING(restorations); READ_STRING(condition); READ_STRING(publications); READ_STRING(related_objects);
	READ_STRING(exhibition_scene); READ_STRING(exhibition_room); READ_STRING(exhibition_zone); READ_STRING(placement); READ_STRING(frame_style); READ_STRING(pedestal_style); READ_STRING(case_style);
	READ_STRING(route_id); READ_STRING(route_stop); READ_STRING(next_object_uuid); READ_STRING(curator_note); READ_STRING(source_records_ref); READ_STRING(raw_source_ref); READ_STRING(modified_at);
#undef READ_STRING
	s.card_auto_open = boolValue(o, "card_auto_open", s.card_auto_open);
	s.card_open_on_click = boolValue(o, "card_open_on_click", s.card_open_on_click);
	s.card_pinned = boolValue(o, "card_pinned", s.card_pinned);
	s.card_scale = std::max(0.05f, std::min(20.f, floatValue(o, "card_scale", s.card_scale)));
	s.lazy_media_loading = boolValue(o, "lazy_media_loading", s.lazy_media_loading);
	s.allow_display = boolValue(o, "allow_display", s.allow_display);
	s.allow_download = boolValue(o, "allow_download", s.allow_download);
	s.allow_modify = boolValue(o, "allow_modify", s.allow_modify);
	s.allow_commercial_use = boolValue(o, "allow_commercial_use", s.allow_commercial_use);
	s.spotlight = boolValue(o, "spotlight", s.spotlight);
	s.light_intensity = std::max(0.f, std::min(100.f, floatValue(o, "light_intensity", s.light_intensity)));
	s.shadows = boolValue(o, "shadows", s.shadows);
	s.interactive = boolValue(o, "interactive", s.interactive);
	s.activation_distance = std::max(0.f, std::min(10000.f, floatValue(o, "activation_distance", s.activation_distance)));
	return s;
}


std::string CulturalObjectSettings::serialiseToContent(const CulturalObjectSettings& s)
{
	QJsonObject o;
	if(!s.preserved_json_fields.empty())
	{
		const QJsonDocument preserved = QJsonDocument::fromJson(QByteArray::fromStdString(s.preserved_json_fields));
		if(preserved.isObject()) o = preserved.object();
	}
	o.insert(QStringLiteral("schema_version"), std::max(1, s.schema_version));
#define PUT_STRING(field) putString(o, #field, s.field)
	PUT_STRING(uuid); PUT_STRING(title); PUT_STRING(object_type); PUT_STRING(cultural_category); PUT_STRING(description);
	PUT_STRING(source_mode); o.remove(QStringLiteral("local_file")); PUT_STRING(source_url); PUT_STRING(provider_id); PUT_STRING(provider_record_id); PUT_STRING(retrieval_status);
	PUT_STRING(alternative_titles); PUT_STRING(creators); PUT_STRING(creation_date); PUT_STRING(country); PUT_STRING(place_of_creation); PUT_STRING(current_location);
	PUT_STRING(collection); PUT_STRING(inventory_number); PUT_STRING(art_forms); PUT_STRING(museum_classifications); PUT_STRING(disciplines); PUT_STRING(cultures);
	PUT_STRING(periods); PUT_STRING(materials); PUT_STRING(techniques); PUT_STRING(styles); PUT_STRING(genres); PUT_STRING(functions); PUT_STRING(subjects); PUT_STRING(keywords);
	PUT_STRING(wikidata_id); PUT_STRING(iiif_id); PUT_STRING(museum_id); PUT_STRING(europeana_id);
	PUT_STRING(card_title); PUT_STRING(card_subtitle); PUT_STRING(card_summary); PUT_STRING(card_theme); PUT_STRING(card_language); PUT_STRING(card_visible_fields); PUT_STRING(plaque_text);
	PUT_STRING(primary_image_url); PUT_STRING(high_resolution_image_url); PUT_STRING(iiif_manifest_url); PUT_STRING(model_3d_url); PUT_STRING(audio_url); PUT_STRING(video_url);
	PUT_STRING(documents); PUT_STRING(media_cache_key); PUT_STRING(license_status); PUT_STRING(rights_holder); PUT_STRING(license_url); PUT_STRING(attribution_text);
	PUT_STRING(provenance); PUT_STRING(exhibitions); PUT_STRING(restorations); PUT_STRING(condition); PUT_STRING(publications); PUT_STRING(related_objects);
	PUT_STRING(exhibition_scene); PUT_STRING(exhibition_room); PUT_STRING(exhibition_zone); PUT_STRING(placement); PUT_STRING(frame_style); PUT_STRING(pedestal_style); PUT_STRING(case_style);
	PUT_STRING(route_id); PUT_STRING(route_stop); PUT_STRING(next_object_uuid); PUT_STRING(curator_note); PUT_STRING(source_records_ref); putPortableReference(o, "raw_source_ref", s.raw_source_ref); PUT_STRING(modified_at);
#undef PUT_STRING
	putBool(o, "card_auto_open", s.card_auto_open); putBool(o, "card_open_on_click", s.card_open_on_click); putBool(o, "card_pinned", s.card_pinned); putFloat(o, "card_scale", s.card_scale);
	putBool(o, "lazy_media_loading", s.lazy_media_loading); putBool(o, "allow_display", s.allow_display); putBool(o, "allow_download", s.allow_download);
	putBool(o, "allow_modify", s.allow_modify); putBool(o, "allow_commercial_use", s.allow_commercial_use); putBool(o, "spotlight", s.spotlight);
	putFloat(o, "light_intensity", s.light_intensity); putBool(o, "shadows", s.shadows); putBool(o, "interactive", s.interactive); putFloat(o, "activation_distance", s.activation_distance);
	const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
	return std::string(contentMarker()) + std::string(json.constData(), (size_t)json.size());
}
