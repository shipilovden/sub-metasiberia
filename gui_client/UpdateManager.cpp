/*=====================================================================
UpdateManager.cpp
-----------------
Checks for a new Windows installer published on GitHub Releases.
=====================================================================*/

#include "UpdateManager.h"

#include "../shared/Version.h"

#include "HTTPClient.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QMetaObject>
#include <QtCore/QByteArray>
#include <thread>
#include <string>


static const char* const GITHUB_RELEASES_URL =
	"https://api.github.com/repos/shipilovden/sub-metasiberia/releases?per_page=10";


UpdateManager::UpdateManager(QObject* parent)
:	QObject(parent)
{
}


QString UpdateManager::currentVersionString() const
{
	// From substrata/shared/Version.h, e.g. "0.0.1"
	return QString::fromStdString(::cyberspace_version);
}


UpdateManager::ParsedVersion UpdateManager::parseVersionLike(const QString& s)
{
	// Accept: "v0.0.2", "0.0.2", "0.0.2-beta", "Metasiberia Beta v0.0.2"
	ParsedVersion out;

	QString t = s.trimmed();
	if(t.isEmpty())
		return out;

	// Find the first occurrence of something like v?\d+\.\d+\.\d+
	int start = -1;
	for(int i = 0; i < t.size(); ++i)
	{
		const QChar c = t[i];
		if(c.isDigit() || c == 'v' || c == 'V')
		{
			start = i;
			break;
		}
	}
	if(start < 0)
		return out;

	t = t.mid(start);
	if(t.startsWith('v', Qt::CaseInsensitive))
		t = t.mid(1);

	// Strip suffix after first whitespace or '-' (e.g. beta)
	int cut = t.indexOf(' ');
	if(cut >= 0) t = t.left(cut);
	cut = t.indexOf('-');
	if(cut >= 0) t = t.left(cut);

	const QStringList parts = t.split('.', Qt::SkipEmptyParts);
	if(parts.size() < 2)
		return out;

	bool ok0 = false, ok1 = false, ok2 = false;
	const int maj = parts.value(0).toInt(&ok0);
	const int min = parts.value(1).toInt(&ok1);
	const int pat = parts.value(2, "0").toInt(&ok2);
	if(!ok0 || !ok1 || !ok2)
		return out;

	out.major = maj;
	out.minor = min;
	out.patch = pat;
	out.valid = true;
	return out;
}


int UpdateManager::compareVersions(const ParsedVersion& a, const ParsedVersion& b)
{
	if(!a.valid && !b.valid) return 0;
	if(!a.valid) return -1;
	if(!b.valid) return 1;
	if(a.major != b.major) return (a.major < b.major) ? -1 : 1;
	if(a.minor != b.minor) return (a.minor < b.minor) ? -1 : 1;
	if(a.patch != b.patch) return (a.patch < b.patch) ? -1 : 1;
	return 0;
}


void UpdateManager::checkForUpdatesAsync(bool force)
{
	if(in_progress)
		return;

	if(!force && has_result)
		return;

	startRequest();
}


void UpdateManager::startRequest()
{
	in_progress = true;
	last_error.clear();

	const uint32_t request_id = ++request_serial;
	runRequestInBackground(request_id);
}

void UpdateManager::runRequestInBackground(uint32_t request_id)
{
	// Run network + parsing on a background thread so we don't block the UI.
	QPointer<UpdateManager> self(this);

	std::thread([self, request_id]() {
		if(!self)
			return;

		QString error;
		UpdateInfo info;
		bool available = false;

		try
		{
			Reference<HTTPClient> client = new HTTPClient();
			// GitHub API requires a User-Agent for GET requests with our HTTPClient implementation.
			client->additional_headers.push_back("User-Agent: metasiberia-client");
			client->additional_headers.push_back("Accept: application/vnd.github+json");

			std::string body;
			HTTPClient::ResponseInfo resp = client->downloadFile(GITHUB_RELEASES_URL, body);
			if(resp.response_code != 200)
			{
				QString extra;
				// Try to extract GitHub's error message from JSON if possible.
				QJsonParseError parse_err;
				const QJsonDocument err_doc = QJsonDocument::fromJson(QByteArray(body.data(), (int)body.size()), &parse_err);
				if(parse_err.error == QJsonParseError::NoError && err_doc.isObject())
				{
					const QString msg = err_doc.object().value("message").toString();
					if(!msg.isEmpty())
						extra = " (" + msg + ")";
				}

				const std::string extra_utf8 = extra.isEmpty() ? std::string() : std::string(extra.toUtf8().constData());
				throw glare::Exception("GitHub API returned HTTP " + std::to_string(resp.response_code) + ": " + resp.response_message + extra_utf8);
			}

			QJsonParseError json_err;
			const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(body.data(), (int)body.size()), &json_err);
			if(json_err.error != QJsonParseError::NoError || !doc.isArray())
				throw glare::Exception("Failed to parse GitHub release JSON.");

			const ParsedVersion cur_v = parseVersionLike(self->currentVersionString());

			ParsedVersion best_v;
			QJsonObject best_release;
			QString best_tag;

			const QJsonArray arr = doc.array();
			for(const QJsonValue& v : arr)
			{
				if(!v.isObject())
					continue;

				const QJsonObject rel = v.toObject();
				if(rel.value("draft").toBool())
					continue;

				const QString tag = rel.value("tag_name").toString();
				const ParsedVersion pv = parseVersionLike(tag);
				if(!pv.valid)
					continue;

				if(!best_v.valid || (compareVersions(pv, best_v) > 0))
				{
					best_v = pv;
					best_release = rel;
					best_tag = tag;
				}
			}

			if(!best_v.valid)
				throw glare::Exception("No usable releases found.");

			InstallerAsset installer;
			const QJsonArray assets = best_release.value("assets").toArray();
			for(const QJsonValue& a : assets)
			{
				if(!a.isObject())
					continue;
				const QJsonObject ao = a.toObject();
				const QString name = ao.value("name").toString();
				if(!name.endsWith(".exe", Qt::CaseInsensitive))
					continue;
				if(!name.contains("MetasiberiaBeta-Setup", Qt::CaseInsensitive))
					continue;

				installer.name = name;
				installer.url = QUrl(ao.value("browser_download_url").toString());
				installer.size_B = (qint64)ao.value("size").toDouble(-1);
				break;
			}

			if(!installer.url.isValid())
				throw glare::Exception("Latest release has no installer asset.");

			info.tag = best_tag;
			info.title = best_release.value("name").toString();
			info.notes = best_release.value("body").toString();
			info.prerelease = best_release.value("prerelease").toBool();
			info.installer = installer;

			available = compareVersions(best_v, cur_v) > 0;
		}
		catch(glare::Exception& e)
		{
			error = QString::fromStdString(e.what());
		}

		if(!self)
			return;

		QMetaObject::invokeMethod(self, [self, request_id, error, info, available]() {
			if(!self)
				return;
			if(request_id != self->request_serial.load())
				return;

			self->in_progress = false;
			self->has_result = true;

			const bool prev_available = self->update_available;
			self->update_available = false;
			self->latest_info = UpdateInfo();
			self->last_error.clear();

			if(!error.isEmpty())
			{
				self->last_error = error;
			}
			else
			{
				self->latest_info = info;
				self->update_available = available;
			}

			emit self->updateCheckFinished();
			if(prev_available != self->update_available)
				emit self->updateAvailabilityChanged(self->update_available);
		}, Qt::QueuedConnection);
	}).detach();
}
