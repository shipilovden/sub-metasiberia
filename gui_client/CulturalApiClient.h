/*=====================================================================
CulturalApiClient.h
-------------------
Read-only adapters for the Cultural Object Editor's online sources.
=====================================================================*/
#pragma once


#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <vector>


struct CulturalApiRecord
{
	QString provider_id;
	QString record_id;
	QString title;
	QString display_text;
	QString source_url;
	// A small public preview URL when the provider has one.  ArtIC additionally
	// supplies an embedded LQIP thumbnail in raw["thumbnail"], so its gallery
	// can render without making one network request per card.
	QString preview_url;
	QString iiif_manifest_url;
	bool public_domain;
	QJsonObject raw;

	CulturalApiRecord() : public_domain(false) {}
};


// These are deliberately provider-neutral labels for the catalogue UI.  Each
// adapter maps only the filters supported by its public API and applies the
// rest locally to the returned museum data.  This avoids pretending that two
// different museum catalogues expose identical search semantics.
struct CulturalApiSearchOptions
{
	QString query;
	QString artist;
	QString style;
	QString theme;
	QString region;
	QString material;
	QString period;
	QString department;
	QString object_type;
	bool public_domain_only;
	bool has_images_only;
	bool audio_only;
	bool video_only;
	bool title_only;
	bool artist_or_culture;
	bool on_view_only;
	bool highlights_only;
	int date_begin;
	int date_end;
	int page;
	int limit;

	CulturalApiSearchOptions();
};


struct CulturalApiSearchResult
{
	std::vector<CulturalApiRecord> records;
	int total;
	int page;
	int total_pages;

	CulturalApiSearchResult() : total(0), page(1), total_pages(0) {}
};


class CulturalApiClient
{
public:
	// Search a single source using its supported public filters.  Results are
	// paged for ArtIC.  The Met API returns an ID list, so the adapter resolves a
	// bounded page of detail records before returning it to the UI.
	static CulturalApiSearchResult search(const QString& provider_id, const CulturalApiSearchOptions& options, QString& error_out, int timeout_ms = 8000);

	// provider_id can be "all", "artic" or "met".  "all" queries every
	// provider that has a real adapter in this build.  The returned records keep
	// the provider response for provenance and field mapping in the editor.
	static std::vector<CulturalApiRecord> search(const QString& provider_id, const QString& query, QString& error_out, int timeout_ms = 8000);

	// Retrieves the complete public metadata record for one item.  The result is
	// intentionally kept out of WorldObject::content: that descriptor remains
	// small and stores the canonical source URL/record ID instead.
	static bool fetchRecord(const QString& provider_id, const QString& record_id, CulturalApiRecord& record_out, QString& error_out, int timeout_ms = 8000);

	// Downloads only a public-domain image from one of the catalogue image
	// hosts explicitly supported by this client.  The caller validates the data
	// and adds it to the MetaSiberia resource store.
	static bool downloadPublicImage(const QString& source_url, QByteArray& image_bytes_out, QString& error_out, int timeout_ms = 20000);
};
