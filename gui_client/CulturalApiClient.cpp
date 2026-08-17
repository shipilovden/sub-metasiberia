/*=====================================================================
CulturalApiClient.cpp
---------------------
=====================================================================*/
#include "CulturalApiClient.h"


#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

#include <algorithm>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif


CulturalApiSearchOptions::CulturalApiSearchOptions()
: public_domain_only(false),
	has_images_only(false),
	audio_only(false),
	video_only(false),
	title_only(false),
	artist_or_culture(false),
	on_view_only(false),
	highlights_only(false),
	date_begin(0),
	date_end(0),
	page(1),
	limit(12)
{}


namespace
{

struct CulturalHttpResponse
{
	QByteArray bytes;
	QString error;
	int http_status;

	CulturalHttpResponse() : http_status(0) {}
};


QString fieldText(const QJsonObject& object, const QStringList& keys);


QString valueText(const QJsonValue& value)
{
	if(value.isString())
		return value.toString().trimmed();
	if(value.isDouble())
		return QString::number(value.toDouble(), 'g', 15);
	if(value.isBool())
		return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
	if(value.isArray())
	{
		QStringList values;
		for(const QJsonValue& item : value.toArray())
		{
			const QString text = valueText(item);
			if(!text.isEmpty())
				values.push_back(text);
		}
		return values.join(QStringLiteral("; "));
	}
	if(value.isObject())
	{
		const QJsonObject object = value.toObject();
		return fieldText(object, { QStringLiteral("title"), QStringLiteral("name"), QStringLiteral("term"), QStringLiteral("displayName"), QStringLiteral("value") });
	}
	return QString();
}


QString fieldText(const QJsonObject& object, const QStringList& keys)
{
	for(const QString& key : keys)
	{
		const QString value = valueText(object.value(key));
		if(!value.isEmpty())
			return value;
	}
	return QString();
}


bool containsText(const QString& value, const QString& term)
{
	const QString trimmed_term = term.trimmed();
	if(trimmed_term.isEmpty())
		return true;
	if(value.contains(trimmed_term, Qt::CaseInsensitive))
		return true;
	// The museum may use a full legal or historical name, for example
	// "Rembrandt van Rijn" while the catalogue choice says "Рембрандт".
	// Treat all words in a typed/search label as required, rather than requiring
	// them to be consecutive in the source metadata.
	const QStringList words = trimmed_term.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for(const QString& word : words)
		if(!value.contains(word, Qt::CaseInsensitive))
			return false;
	return !words.isEmpty();
}


bool anyFieldContains(const QJsonObject& object, const QStringList& fields, const QString& term)
{
	return term.trimmed().isEmpty() || containsText(fieldText(object, fields), term);
}


int integerField(const QJsonObject& object, const QString& field)
{
	const QJsonValue value = object.value(field);
	if(value.isDouble())
		return value.toInt();
	bool ok = false;
	const int parsed = value.toString().toInt(&ok);
	return ok ? parsed : 0;
}


bool matchesLocalFilters(const CulturalApiRecord& record, const CulturalApiSearchOptions& options)
{
	if(options.public_domain_only && !record.public_domain)
		return false;
	if(options.has_images_only && record.preview_url.isEmpty())
		return false;

	const QJsonObject& raw = record.raw;
	if(options.audio_only && raw.value(QStringLiteral("sound_ids")).toArray().isEmpty())
		return false;
	if(options.video_only && raw.value(QStringLiteral("video_ids")).toArray().isEmpty())
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("artist_display"), QStringLiteral("artist_title"), QStringLiteral("artistDisplayName"), QStringLiteral("artistAlphaSort"), QStringLiteral("culture") }, options.artist))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("style_titles"), QStringLiteral("style_title"), QStringLiteral("classification_title"), QStringLiteral("classification") }, options.style))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("theme_titles"), QStringLiteral("subject_titles"), QStringLiteral("term_titles"), QStringLiteral("tags") }, options.theme))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("place_of_origin"), QStringLiteral("culture"), QStringLiteral("country"), QStringLiteral("region"), QStringLiteral("geographyType") }, options.region))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("medium_display"), QStringLiteral("material_titles"), QStringLiteral("medium"), QStringLiteral("technique_titles") }, options.material))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("date_display"), QStringLiteral("period"), QStringLiteral("objectDate") }, options.period))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("department_title"), QStringLiteral("department"), QStringLiteral("repository") }, options.department))
		return false;
	if(!anyFieldContains(raw, { QStringLiteral("classification_title"), QStringLiteral("classification_titles"), QStringLiteral("classification"), QStringLiteral("objectName"), QStringLiteral("artwork_type_title") }, options.object_type))
		return false;
	if(options.date_begin != 0 || options.date_end != 0)
	{
		const int date_start = integerField(raw, QStringLiteral("date_start"));
		const int date_end = integerField(raw, QStringLiteral("date_end"));
		const int first_year = date_start != 0 ? date_start : date_end;
		const int last_year = date_end != 0 ? date_end : date_start;
		if((first_year == 0 && last_year == 0) ||
			(options.date_begin != 0 && last_year < options.date_begin) ||
			(options.date_end != 0 && first_year > options.date_end))
			return false;
	}
	return true;
}


int boundedPage(const CulturalApiSearchOptions& options)
{
	return std::max(1, options.page);
}


int boundedLimit(const CulturalApiSearchOptions& options)
{
	return std::max(1, std::min(24, options.limit));
}


CulturalApiRecord makeArtInstituteRecord(const QJsonObject& raw)
{
	CulturalApiRecord record;
	record.provider_id = QStringLiteral("artic");
	record.record_id = fieldText(raw, { QStringLiteral("id") });
	record.title = fieldText(raw, { QStringLiteral("title") });
	record.public_domain = raw.value(QStringLiteral("is_public_domain")).toBool(false);
	record.source_url = fieldText(raw, { QStringLiteral("api_link") });
	if(record.source_url.isEmpty() && !record.record_id.isEmpty())
		record.source_url = QStringLiteral("https://api.artic.edu/api/v1/artworks/") + record.record_id;
	const QString image_id = fieldText(raw, { QStringLiteral("image_id") });
	if(record.public_domain && !image_id.isEmpty())
	{
		record.preview_url = QStringLiteral("https://www.artic.edu/iiif/2/%1/full/200,/0/default.jpg").arg(image_id);
		record.iiif_manifest_url = QStringLiteral("https://api.artic.edu/api/v1/artworks/%1/manifest.json").arg(record.record_id);
	}
	record.display_text = record.title + QStringLiteral(" | ") + fieldText(raw, { QStringLiteral("artist_display"), QStringLiteral("date_display"), QStringLiteral("department_title") });
	record.raw = raw;
	return record;
}


CulturalApiRecord makeMetRecord(const QJsonObject& raw)
{
	CulturalApiRecord record;
	record.provider_id = QStringLiteral("met");
	record.record_id = fieldText(raw, { QStringLiteral("objectID") });
	record.title = fieldText(raw, { QStringLiteral("title"), QStringLiteral("objectName") });
	record.public_domain = raw.value(QStringLiteral("isPublicDomain")).toBool(false);
	record.source_url = fieldText(raw, { QStringLiteral("objectURL"), QStringLiteral("linkResource") });
	if(record.source_url.isEmpty() && !record.record_id.isEmpty())
		record.source_url = QStringLiteral("https://collectionapi.metmuseum.org/public/collection/v1/objects/") + record.record_id;
	if(record.public_domain)
		record.preview_url = fieldText(raw, { QStringLiteral("primaryImageSmall"), QStringLiteral("primaryImage") });
	record.display_text = record.title + QStringLiteral(" | ") + fieldText(raw, { QStringLiteral("artistDisplayName"), QStringLiteral("objectDate"), QStringLiteral("department") });
	record.raw = raw;
	return record;
}


#if defined(_WIN32)
class WinHttpHandle
{
public:
	WinHttpHandle() : handle(NULL) {}
	explicit WinHttpHandle(HINTERNET handle_) : handle(handle_) {}
	~WinHttpHandle() { if(handle) WinHttpCloseHandle(handle); }

	HINTERNET handle;

private:
	WinHttpHandle(const WinHttpHandle&);
	WinHttpHandle& operator = (const WinHttpHandle&);
};


std::wstring toWideString(const QString& value)
{
	return std::wstring(reinterpret_cast<const wchar_t*>(value.utf16()), (size_t)value.size());
}


QString winHttpErrorString(DWORD error_code)
{
	wchar_t* message_buf = NULL;
	const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	const DWORD chars = FormatMessageW(flags, NULL, error_code, 0, reinterpret_cast<LPWSTR>(&message_buf), 0, NULL);
	QString message;
	if(chars > 0 && message_buf)
		message = QString::fromWCharArray(message_buf).trimmed();
	if(message_buf)
		LocalFree(message_buf);
	return message.isEmpty() ? QStringLiteral("Windows error %1").arg((qulonglong)error_code) : message;
}


bool allowedCulturalApiHost(const QString& host)
{
	return host == QStringLiteral("api.artic.edu") || host == QStringLiteral("collectionapi.metmuseum.org");
}


bool allowedCulturalImageHost(const QString& host)
{
	return host == QStringLiteral("www.artic.edu") || host == QStringLiteral("images.metmuseum.org");
}


CulturalHttpResponse getHttpsResponse(const QUrl& url, int timeout_ms, bool image_response)
{
	CulturalHttpResponse result;
	const QString host = url.host().toLower();
	const bool allowed_host = image_response ? allowedCulturalImageHost(host) : allowedCulturalApiHost(host);
	if(!url.isValid() || url.scheme().toLower() != QStringLiteral("https") || !allowed_host)
	{
		result.error = QStringLiteral("Cultural API отклонил URL вне разрешённых официальных HTTPS-источников.");
		return result;
	}

	const QString path = url.path(QUrl::FullyEncoded).isEmpty() ? QStringLiteral("/") : url.path(QUrl::FullyEncoded);
	const QString query = url.query(QUrl::FullyEncoded);
	const QString target = query.isEmpty() ? path : path + QStringLiteral("?") + query;
	const std::wstring user_agent = L"MetaSiberia-CulturalObjectEditor/0.0.21 (interactive catalogue search)";

	WinHttpHandle session(WinHttpOpen(user_agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if(!session.handle)
	{
		result.error = QStringLiteral("Не удалось открыть HTTPS-сеанс Cultural API: %1").arg(winHttpErrorString(GetLastError()));
		return result;
	}
	WinHttpSetTimeouts(session.handle, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

	const std::wstring host_w = toWideString(host);
	WinHttpHandle connection(WinHttpConnect(session.handle, host_w.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
	if(!connection.handle)
	{
		result.error = QStringLiteral("Не удалось подключиться к %1: %2").arg(host, winHttpErrorString(GetLastError()));
		return result;
	}

	const std::wstring target_w = toWideString(target);
	WinHttpHandle request(WinHttpOpenRequest(connection.handle, L"GET", target_w.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if(!request.handle)
	{
		result.error = QStringLiteral("Не удалось создать HTTPS-запрос к %1: %2").arg(host, winHttpErrorString(GetLastError()));
		return result;
	}

	// TLS certificate validation remains enabled.  Never permit an HTTPS request
	// to silently fall back to plain HTTP.
	DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

	// The Art Institute IIIF endpoint rejects direct image requests with HTTP 403
	// unless they identify an Art Institute page as the referrer.  The value is a
	// fixed first-party HTTPS origin (not a user-supplied URL), so this does not
	// widen the set of hosts this client can contact.
	// Ask for formats the desktop image pipeline is guaranteed to decode.  In
	// particular, advertising AVIF/WebP here can make an IIIF server return a
	// valid image that Qt 5 on an end-user machine cannot load, leaving only the
	// tiny embedded LQIP visible in catalogue cards.
	const QString image_accept_header = QStringLiteral("Accept: image/jpeg,image/png,image/*;q=0.8,*/*;q=0.5\r\n");
	const QString headers = image_response ? (host == QStringLiteral("www.artic.edu")
		? image_accept_header + QStringLiteral("Referer: https://www.artic.edu/\r\n")
		: image_accept_header) : (host == QStringLiteral("api.artic.edu")
		? QStringLiteral("Accept: application/json\r\nAIC-User-Agent: MetaSiberia-CulturalObjectEditor/0.0.21\r\n")
		: QStringLiteral("Accept: application/json\r\n"));
	const std::wstring headers_w = toWideString(headers);
	if(!WinHttpSendRequest(request.handle, headers_w.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
	{
		const DWORD error_code = GetLastError();
		QString detail = winHttpErrorString(error_code);
		if(error_code == ERROR_WINHTTP_SECURE_FAILURE)
			detail += QStringLiteral(" (TLS certificate validation failed; HTTP fallback was refused)");
		result.error = QStringLiteral("Cultural API request failed for %1: %2").arg(host, detail);
		return result;
	}
	if(!WinHttpReceiveResponse(request.handle, NULL))
	{
		const DWORD error_code = GetLastError();
		QString detail = winHttpErrorString(error_code);
		if(error_code == ERROR_WINHTTP_SECURE_FAILURE)
			detail += QStringLiteral(" (TLS certificate validation failed; HTTP fallback was refused)");
		result.error = QStringLiteral("Cultural API response failed for %1: %2").arg(host, detail);
		return result;
	}

	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	if(WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX))
		result.http_status = (int)status_code;
	if(result.http_status < 200 || result.http_status >= 300)
	{
		result.error = QStringLiteral("Cultural API %1 returned HTTP %2.").arg(host).arg(result.http_status);
		return result;
	}

	while(true)
	{
		DWORD available = 0;
		if(!WinHttpQueryDataAvailable(request.handle, &available))
		{
			result.error = QStringLiteral("Ошибка чтения ответа Cultural API: %1").arg(winHttpErrorString(GetLastError()));
			return result;
		}
		if(available == 0)
			break;
		const int max_response_bytes = image_response ? 20 * 1024 * 1024 : 4 * 1024 * 1024;
		if(result.bytes.size() + (int)available > max_response_bytes)
		{
			result.error = QStringLiteral("Ответ Cultural API превышает лимит %1 MiB.").arg(image_response ? 20 : 4);
			return result;
		}

		const int old_size = result.bytes.size();
		result.bytes.resize(old_size + (int)available);
		DWORD read = 0;
		if(!WinHttpReadData(request.handle, result.bytes.data() + old_size, available, &read))
		{
			result.error = QStringLiteral("Ошибка чтения данных Cultural API: %1").arg(winHttpErrorString(GetLastError()));
			return result;
		}
		result.bytes.resize(old_size + (int)read);
	}
	return result;
}


CulturalHttpResponse getJson(const QUrl& url, int timeout_ms)
{
	return getHttpsResponse(url, timeout_ms, /*image_response=*/false);
}


CulturalHttpResponse getImage(const QUrl& url, int timeout_ms)
{
	return getHttpsResponse(url, timeout_ms, /*image_response=*/true);
}
#else
CulturalHttpResponse getJson(const QUrl&, int)
{
	CulturalHttpResponse result;
	result.error = QStringLiteral("Онлайн-поиск объектов культуры сейчас поддержан в нативном Windows-клиенте через WinHTTP/SChannel.");
	return result;
}


CulturalHttpResponse getImage(const QUrl&, int)
{
	CulturalHttpResponse result;
	result.error = QStringLiteral("Загрузка изображения из Cultural API сейчас поддержана в нативном Windows-клиенте через WinHTTP/SChannel.");
	return result;
}
#endif


bool parseJsonObject(const CulturalHttpResponse& response, const QString& provider_name, QJsonObject& object_out, QString& error_out)
{
	if(!response.error.isEmpty())
	{
		error_out = response.error;
		return false;
	}
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(response.bytes, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
	{
		error_out = QStringLiteral("%1 вернул некорректный JSON.").arg(provider_name);
		return false;
	}
	object_out = document.object();
	return true;
}


CulturalApiSearchResult searchArtInstitute(const CulturalApiSearchOptions& options, int timeout_ms, QString& error_out)
{
	CulturalApiSearchResult result;
	result.page = boundedPage(options);

	QUrl url(QStringLiteral("https://api.artic.edu/api/v1/artworks/search"));
	QUrlQuery arguments;
	// ArtIC recommends the URL-encoded `params` JSON form for complex GET
	// searches.  Unlike a client-side filter over one arbitrary page, these
	// clauses are evaluated by the museum search index before pagination.
	QJsonObject params;
	params.insert(QStringLiteral("page"), result.page);
	params.insert(QStringLiteral("limit"), boundedLimit(options));
	params.insert(QStringLiteral("fields"), QStringLiteral(
		"id,title,alt_titles,artist_display,artist_title,date_display,date_start,date_end,place_of_origin,medium_display,classification_title,classification_titles,department_title,credit_line,provenance_text,exhibition_history,publication_history,api_link,image_id,is_public_domain,license_text,copyright_notice,thumbnail,dimensions,style_titles,theme_titles,material_titles,term_titles,subject_titles,category_titles,artwork_type_title,sound_ids,video_ids"));

	const QString full_text_query = !options.query.trimmed().isEmpty() ? options.query.trimmed() : options.theme.trimmed();
	if(!full_text_query.isEmpty())
		params.insert(QStringLiteral("q"), full_text_query);

	QJsonArray filter_clauses;
	const auto add_term = [&filter_clauses](const QString& field, const QJsonValue& value) {
		if(value.isString() && value.toString().trimmed().isEmpty())
			return;
		QJsonObject term;
		term.insert(field, value);
		QJsonObject clause;
		clause.insert(QStringLiteral("term"), term);
		filter_clauses.append(clause);
	};
	const auto add_exists = [&filter_clauses](const QString& field) {
		QJsonObject exists;
		exists.insert(QStringLiteral("field"), field);
		QJsonObject clause;
		clause.insert(QStringLiteral("exists"), exists);
		filter_clauses.append(clause);
	};
	const auto add_match = [&filter_clauses](const QString& field, const QString& value) {
		if(value.trimmed().isEmpty())
			return;
		QJsonObject match_options;
		match_options.insert(QStringLiteral("query"), value.trimmed());
		match_options.insert(QStringLiteral("operator"), QStringLiteral("and"));
		QJsonObject match;
		match.insert(field, match_options);
		QJsonObject clause;
		clause.insert(QStringLiteral("match"), match);
		filter_clauses.append(clause);
	};
	if(options.public_domain_only)
		add_term(QStringLiteral("is_public_domain"), true);
	if(options.has_images_only)
		add_exists(QStringLiteral("image_id"));
	if(options.audio_only)
		add_exists(QStringLiteral("sound_ids"));
	if(options.video_only)
		add_exists(QStringLiteral("video_ids"));
	// `match` is deliberately used for museum terms rather than exact `.keyword`
	// equality.  API labels frequently contain a fuller name (for example
	// "Rembrandt van Rijn") or different case/wording than a human-facing filter.
	add_match(QStringLiteral("artist_title"), options.artist);
	add_match(QStringLiteral("style_titles"), options.style);
	add_match(QStringLiteral("place_of_origin"), options.region);
	add_match(QStringLiteral("material_titles"), options.material);
	add_match(QStringLiteral("department_title"), options.department);
	add_match(QStringLiteral("classification_titles"), options.object_type);
	if(options.date_begin != 0 || options.date_end != 0)
	{
		QJsonObject range_values;
		if(options.date_begin != 0)
			range_values.insert(QStringLiteral("gte"), options.date_begin);
		if(options.date_end != 0)
			range_values.insert(QStringLiteral("lte"), options.date_end);
		QJsonObject range;
		range.insert(QStringLiteral("date_start"), range_values);
		QJsonObject clause;
		clause.insert(QStringLiteral("range"), range);
		filter_clauses.append(clause);
	}
	if(!filter_clauses.isEmpty())
	{
		QJsonObject bool_query;
		bool_query.insert(QStringLiteral("filter"), filter_clauses);
		QJsonObject query;
		query.insert(QStringLiteral("bool"), bool_query);
		params.insert(QStringLiteral("query"), query);
	}
	arguments.addQueryItem(QStringLiteral("params"), QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)));
	url.setQuery(arguments);

	QJsonObject response;
	if(!parseJsonObject(getJson(url, timeout_ms), QStringLiteral("Art Institute of Chicago"), response, error_out))
		return result;

	const QJsonObject pagination = response.value(QStringLiteral("pagination")).toObject();
	result.total = pagination.value(QStringLiteral("total")).toInt(0);
	result.total_pages = pagination.value(QStringLiteral("total_pages")).toInt(0);
	for(const QJsonValue& value : response.value(QStringLiteral("data")).toArray())
	{
		if(!value.isObject())
			continue;
		const CulturalApiRecord record = makeArtInstituteRecord(value.toObject());
		if(record.record_id.isEmpty() || record.title.isEmpty() || !matchesLocalFilters(record, options))
			continue;
		result.records.push_back(record);
	}
	return result;
}


bool fetchArtInstituteRecord(const QString& record_id, int timeout_ms, CulturalApiRecord& record_out, QString& error_out)
{
	QJsonObject response;
	if(!parseJsonObject(getJson(QUrl(QStringLiteral("https://api.artic.edu/api/v1/artworks/") + record_id), timeout_ms), QStringLiteral("Art Institute of Chicago"), response, error_out))
		return false;
	record_out = makeArtInstituteRecord(response.value(QStringLiteral("data")).toObject());
	if(record_out.record_id.isEmpty())
	{
		error_out = QString::fromUtf8("Art Institute of Chicago не вернул корректную запись.");
		return false;
	}
	return true;
}


bool fetchMetRecord(const QString& record_id, int timeout_ms, CulturalApiRecord& record_out, QString& error_out)
{
	QJsonObject raw;
	if(!parseJsonObject(getJson(QUrl(QStringLiteral("https://collectionapi.metmuseum.org/public/collection/v1/objects/") + record_id), timeout_ms), QStringLiteral("The Met"), raw, error_out))
		return false;
	record_out = makeMetRecord(raw);
	if(record_out.record_id.isEmpty())
	{
		error_out = QString::fromUtf8("The Met не вернул корректную запись.");
		return false;
	}
	return true;
}


CulturalApiSearchResult searchMet(const CulturalApiSearchOptions& options, int timeout_ms, QString& error_out)
{
	CulturalApiSearchResult result;
	result.page = boundedPage(options);
	const QString query = !options.query.trimmed().isEmpty() ? options.query.trimmed() : options.artist.trimmed();
	if(query.isEmpty())
	{
		error_out = QString::fromUtf8("Для поиска в The Met укажите название, автора или ключевое слово. Полная выгрузка коллекции не выполняется.");
		return result;
	}

	QUrl search_url(QStringLiteral("https://collectionapi.metmuseum.org/public/collection/v1/search"));
	QUrlQuery arguments;
	arguments.addQueryItem(QStringLiteral("q"), query);
	if(options.has_images_only)
		arguments.addQueryItem(QStringLiteral("hasImages"), QStringLiteral("true"));
	if(options.title_only)
		arguments.addQueryItem(QStringLiteral("title"), QStringLiteral("true"));
	if(options.artist_or_culture)
		arguments.addQueryItem(QStringLiteral("artistOrCulture"), QStringLiteral("true"));
	if(options.on_view_only)
		arguments.addQueryItem(QStringLiteral("isOnView"), QStringLiteral("true"));
	if(options.highlights_only)
		arguments.addQueryItem(QStringLiteral("isHighlight"), QStringLiteral("true"));
	if(!options.material.trimmed().isEmpty())
		arguments.addQueryItem(QStringLiteral("medium"), options.material.trimmed());
	if(!options.region.trimmed().isEmpty())
		arguments.addQueryItem(QStringLiteral("geoLocation"), options.region.trimmed());
	bool department_is_id = false;
	const int department_id = options.department.trimmed().toInt(&department_is_id);
	if(department_is_id && department_id > 0)
		arguments.addQueryItem(QStringLiteral("departmentId"), QString::number(department_id));
	if(options.date_begin != 0)
		arguments.addQueryItem(QStringLiteral("dateBegin"), QString::number(options.date_begin));
	if(options.date_end != 0)
		arguments.addQueryItem(QStringLiteral("dateEnd"), QString::number(options.date_end));
	search_url.setQuery(arguments);

	QJsonObject search_response;
	if(!parseJsonObject(getJson(search_url, timeout_ms), QStringLiteral("The Met"), search_response, error_out))
		return result;

	result.total = search_response.value(QStringLiteral("total")).toInt(0);
	const int limit = boundedLimit(options);
	result.total_pages = result.total > 0 ? (result.total + limit - 1) / limit : 0;
	const QJsonArray ids = search_response.value(QStringLiteral("objectIDs")).toArray();
	const int first_index = std::min(ids.size(), (result.page - 1) * limit);
	const int last_index = std::min(ids.size(), first_index + limit * 4);
	QString first_detail_error;
	for(int i = first_index; i < last_index && (int)result.records.size() < limit; ++i)
	{
		const QString id = valueText(ids[i]);
		if(id.isEmpty())
			continue;
		CulturalApiRecord record;
		QString detail_error;
		if(!fetchMetRecord(id, std::min(timeout_ms, 5000), record, detail_error))
		{
			if(first_detail_error.isEmpty())
				first_detail_error = detail_error;
			continue;
		}
		if(record.title.isEmpty() || !matchesLocalFilters(record, options))
			continue;
		result.records.push_back(record);
	}
	if(result.records.empty() && !first_detail_error.isEmpty())
		error_out = first_detail_error;
	return result;
}

}


CulturalApiSearchResult CulturalApiClient::search(const QString& provider_id, const CulturalApiSearchOptions& options, QString& error_out, int timeout_ms)
{
	error_out.clear();
	const QString provider = provider_id.trimmed().toLower();
	if(provider == QStringLiteral("artic"))
		return searchArtInstitute(options, timeout_ms, error_out);
	if(provider == QStringLiteral("met"))
		return searchMet(options, timeout_ms, error_out);
	if(provider != QStringLiteral("all") && provider != QStringLiteral("manual"))
	{
		error_out = QString::fromUtf8("Для этого источника нет рабочего адаптера. Сейчас подключены Art Institute of Chicago и The Met.");
		return CulturalApiSearchResult();
	}

	QString artic_error;
	QString met_error;
	CulturalApiSearchResult artic_result = searchArtInstitute(options, timeout_ms, artic_error);
	CulturalApiSearchResult met_result;
	if(!options.query.trimmed().isEmpty() || !options.artist.trimmed().isEmpty())
		met_result = searchMet(options, timeout_ms, met_error);
	artic_result.records.insert(artic_result.records.end(), met_result.records.begin(), met_result.records.end());
	artic_result.total += met_result.total;
	artic_result.total_pages = std::max(artic_result.total_pages, met_result.total_pages);
	if(artic_result.records.empty())
	{
		if(!artic_error.isEmpty() && !met_error.isEmpty())
			error_out = QString::fromUtf8("Art Institute of Chicago: %1\nThe Met: %2").arg(artic_error, met_error);
		else
			error_out = !artic_error.isEmpty() ? artic_error : met_error;
	}
	return artic_result;
}


std::vector<CulturalApiRecord> CulturalApiClient::search(const QString& provider_id, const QString& query, QString& error_out, int timeout_ms)
{
	CulturalApiSearchOptions options;
	options.query = query.trimmed();
	if(options.query.isEmpty())
	{
		error_out = QString::fromUtf8("Введите название, автора или ключевое слово объекта культуры.");
		return std::vector<CulturalApiRecord>();
	}
	return search(provider_id, options, error_out, timeout_ms).records;
}


bool CulturalApiClient::fetchRecord(const QString& provider_id, const QString& record_id, CulturalApiRecord& record_out, QString& error_out, int timeout_ms)
{
	error_out.clear();
	record_out = CulturalApiRecord();
	const QString provider = provider_id.trimmed().toLower();
	const QString id = record_id.trimmed();
	if(id.isEmpty())
	{
		error_out = QString::fromUtf8("Не указан ID музейной записи.");
		return false;
	}
	if(provider == QStringLiteral("artic"))
		return fetchArtInstituteRecord(id, timeout_ms, record_out, error_out);
	if(provider == QStringLiteral("met"))
		return fetchMetRecord(id, timeout_ms, record_out, error_out);
	error_out = QString::fromUtf8("Нужно выбрать Art Institute of Chicago или The Met.");
	return false;
}


bool CulturalApiClient::downloadPublicImage(const QString& source_url, QByteArray& image_bytes_out, QString& error_out, int timeout_ms)
{
	image_bytes_out.clear();
	error_out.clear();

	const CulturalHttpResponse response = getImage(QUrl(source_url), timeout_ms);
	if(!response.error.isEmpty())
	{
		error_out = response.error;
		return false;
	}
	if(response.bytes.isEmpty())
	{
		error_out = QString::fromUtf8("Источник вернул пустое изображение.");
		return false;
	}

	image_bytes_out = response.bytes;
	return true;
}
