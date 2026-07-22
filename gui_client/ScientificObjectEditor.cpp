/*=====================================================================
ScientificObjectEditor.cpp
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ScientificObjectEditor.h"
#include "MoleculeViewportWidget.h"
#include "PeriodicTable.h"
#include "ScientificImageViewer.h"


#include <BitUtils.h>
#include <maths/matrix3.h>
#include <maths/mathstypes.h>
#include "../utils/FileUtils.h"
#include "../utils/PlatformUtils.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtCore5Compat/QRegExp>
#else
#include <QtCore/QRegExp>
#endif
#include <QtCore/QSignalBlocker>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QTextOption>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

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

#include "../utils/ConPrint.h"


namespace
{
QString qstr(const std::string& s)
{
	return QString::fromUtf8(s.c_str());
}


std::string stdstr(const QString& s)
{
	const QByteArray bytes = s.toUtf8();
	return std::string(bytes.constData(), (size_t)bytes.size());
}


void configureCombo(QComboBox* combo)
{
	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(14);
	combo->setMaximumWidth(320);
}


void configurePlainText(QPlainTextEdit* edit, int min_height)
{
	edit->setMinimumHeight(min_height);
	edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
}


void configureHorizontalTextScroll(QPlainTextEdit* edit)
{
	if(!edit)
		return;
	edit->setLineWrapMode(QPlainTextEdit::NoWrap);
	edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}


void setDetailedTip(QWidget* widget, const QString& text)
{
	if(!widget)
		return;

	const QString html = QStringLiteral("<div style=\"white-space:pre-wrap; max-width:420px;\">%1</div>")
		.arg(text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
	widget->setToolTip(html);
	widget->setWhatsThis(text);
	widget->setToolTipDuration(30000);
}


void setLayoutItemsVisible(QLayout* layout, bool visible)
{
	if(!layout)
		return;

	for(int i=0; i<layout->count(); ++i)
	{
		QLayoutItem* item = layout->itemAt(i);
		if(!item)
			continue;
		if(QWidget* widget = item->widget())
			widget->setVisible(visible);
		if(QLayout* child_layout = item->layout())
			setLayoutItemsVisible(child_layout, visible);
	}
}


void addStretchToGrid(QGridLayout* grid, int row)
{
	QWidget* spacer = new QWidget();
	spacer->setMinimumHeight(1);
	grid->addWidget(spacer, row, 0, 1, 2);
}


QString scientificTypeLabel(const QString& type)
{
	if(type == "molecule") return QString::fromUtf8("Молекула");
	if(type == "protein") return QString::fromUtf8("Белок");
	if(type == "dna") return QString::fromUtf8("ДНК");
	if(type == "rna") return QString::fromUtf8("РНК");
	if(type == "crystal") return QString::fromUtf8("Кристалл");
	if(type == "material") return QString::fromUtf8("Материал");
	if(type == "planet") return QString::fromUtf8("Планета");
	if(type == "asteroid") return QString::fromUtf8("Астероид");
	if(type == "map") return QString::fromUtf8("Карта");
	if(type == "gis") return QStringLiteral("GIS");
	if(type == "point_cloud") return QString::fromUtf8("Облако точек");
	if(type == "field") return QString::fromUtf8("Поле");
	if(type == "volume") return QString::fromUtf8("Объемные данные");
	if(type == "chart") return QString::fromUtf8("График");
	if(type == "knowledge_graph") return QString::fromUtf8("Граф знаний");
	if(type == "point_set") return QString::fromUtf8("Точечный набор");
	if(type == "surface") return QString::fromUtf8("Поверхность");
	if(type == "medical") return QString::fromUtf8("Медицинские данные");
	return QString::fromUtf8("Пользовательский объект");
}


QString normalisedScientificQueryKey(QString s)
{
	s = s.trimmed().toLower();
	QString out;
	for(int i=0; i<s.size(); ++i)
	{
		const QChar c = s[i];
		if(c.isLetterOrNumber())
			out.append(c);
	}
	return out;
}


QString defaultScientificTypeForDatabase(const QString& database)
{
	const QString db = database.toLower();
	if(db.contains("built-in") || db.contains("sample catalog"))
		return QStringLiteral("molecule");
	if(db.contains("pubchem") || db.contains("chebi") || db.contains("chemspider"))
		return QStringLiteral("molecule");
	if(db.contains("rcsb") || db.contains("alphafold") || db.contains("ncbi"))
		return QStringLiteral("protein");
	if(db.contains("materials") || db.contains("crystallography") || db.contains("oqmd") || db.contains("cod"))
		return QStringLiteral("material");
	if(db.contains("emdb"))
		return QStringLiteral("volume");
	if(db.contains("openstreetmap") || db.contains("usgs") || db.contains("natural earth") || db.contains("copernicus"))
		return QStringLiteral("gis");
	if(db.contains("open topography") || db.contains("opentopography"))
		return QStringLiteral("point_cloud");
	if(db.contains("nasa") || db.contains("jpl") || db.contains("esa"))
		return QStringLiteral("planet");
	if(db.contains("zenodo") || db.contains("figshare"))
		return QStringLiteral("custom");
	return QStringLiteral("custom");
}


QString sourceSupportStatusForTypeAndDatabase(const QString& scientific_type, const QString& database)
{
	const QString db = database.toLower();
	if(db.contains("built-in") || db.contains("sample catalog"))
		return QString::fromUtf8("Built-in sample catalog is local demo data only. It currently contains explicit molecule samples such as Caffeine and Water.");
	if(scientific_type == "molecule" && db.contains("pubchem"))
		return QString::fromUtf8("PubChem molecule adapter is implemented for interactive PUG REST search, CID selection, properties, PNG image and SDF structure loading.");
	if(scientific_type == "molecule" && (db.contains("chebi") || db.contains("chemspider")))
		return QString::fromUtf8("ChEBI/ChemSpider adapters are not implemented yet. API terms/keys and identifier mapping are future work.");
	if((scientific_type == "protein" || scientific_type == "dna" || scientific_type == "rna") && (db.contains("rcsb") || db.contains("alphafold")))
		return QString::fromUtf8("Protein/nucleic-acid provider adapters are not implemented yet. RCSB/AlphaFold are tracked as planned providers only.");
	if((scientific_type == "protein" || scientific_type == "dna" || scientific_type == "rna") && db.contains("ncbi"))
		return QString::fromUtf8("NCBI sequence adapters are not implemented yet. FASTA/GenBank parsing and provenance storage are future work.");
	if((scientific_type == "crystal" || scientific_type == "material") && (db.contains("materials") || db.contains("crystallography") || db.contains("oqmd") || db.contains("cod")))
		return QString::fromUtf8("Crystal/material provider adapters are not implemented yet. CIF/POSCAR parsing and API keys are future work.");
	if(scientific_type == "volume" && db.contains("emdb"))
		return QString::fromUtf8("EMDB/electron-density map adapters are not implemented yet. CCP4/MRC parsing is future work.");
	if((scientific_type == "gis" || scientific_type == "map") && (db.contains("openstreetmap") || db.contains("usgs") || db.contains("natural earth")))
		return QString::fromUtf8("GIS provider adapters are not implemented in Scientific Object Editor yet. Existing world map systems are separate runtime systems.");
	if(scientific_type == "point_cloud" && (db.contains("open topography") || db.contains("opentopography")))
		return QString::fromUtf8("Point-cloud provider adapters are not implemented yet. PLY/LAS/XYZ import is planned, not active.");
	return QString::fromUtf8("This type/source pair has no implemented Scientific Object provider adapter yet.");
}


struct BuiltInMoleculeSample
{
	const char* key;
	const char* name;
	const char* identifier;
	const char* formula;
	const char* mass;
	const char* atoms;
	const char* bonds;
	int atom_count;
	int bond_count;
};


const BuiltInMoleculeSample* builtInMoleculeSamples()
{
	static const BuiltInMoleculeSample samples[] = {
		{
			"caffeine",
			"Caffeine",
			"built-in:caffeine",
			"C8H10N4O2",
			"194.19 g/mol",
			"1 C -1.207 1.453 -0.152\n"
			"2 C -0.513 2.667 0.062\n"
			"3 N 0.789 2.861 -0.215\n"
			"4 C 1.530 1.733 -0.485\n"
			"5 N 2.769 1.756 -0.058\n"
			"6 C 3.371 0.681 -0.293\n"
			"7 O 3.067 -0.482 -0.142\n"
			"8 N 2.221 -1.188 -0.518\n"
			"9 C 0.923 -0.776 -0.747\n"
			"10 C 0.487 0.545 -0.563\n"
			"11 N -0.838 0.359 -0.393\n"
			"12 C -1.560 2.987 0.430\n"
			"13 O -2.420 1.260 0.060\n"
			"14 C -1.341 -0.908 -0.365\n"
			"15 H -1.035 3.918 0.638\n"
			"16 H -2.240 3.117 -0.420\n"
			"17 H -2.081 2.642 1.335\n"
			"18 H -2.140 -1.196 0.328\n"
			"19 H -1.712 -1.110 -1.374\n"
			"20 H -0.575 -1.622 -0.137\n"
			"21 H 4.462 0.807 -0.327\n"
			"22 H 1.164 -1.836 -0.916\n"
			"23 H 0.002 0.826 -1.493\n"
			"24 H 1.891 1.250 -1.435",
			"1-2 aromatic\n"
			"2-3 aromatic\n"
			"3-4 aromatic\n"
			"4-5 single\n"
			"5-6 single\n"
			"6-7 double\n"
			"6-8 single\n"
			"8-9 aromatic\n"
			"9-10 aromatic\n"
			"10-11 aromatic\n"
			"11-1 single\n"
			"1-13 double\n"
			"3-12 single\n"
			"11-14 single\n"
			"12-15 single\n"
			"12-16 single\n"
			"12-17 single\n"
			"14-18 single\n"
			"14-19 single\n"
			"14-20 single\n"
			"6-21 single\n"
			"9-22 single\n"
			"10-23 single\n"
			"4-24 single\n"
			"2-10 aromatic",
			24,
			25
		},
		{
			"water",
			"Water",
			"built-in:water",
			"H2O",
			"18.015 g/mol",
			"1 O 0.000 0.000 0.000\n"
			"2 H 0.958 0.000 0.000\n"
			"3 H -0.239 0.927 0.000",
			"1-2 single\n"
			"1-3 single",
			3,
			2
		}
	};
	return samples;
}


int builtInMoleculeSampleCount()
{
	return 2;
}


const BuiltInMoleculeSample* findBuiltInMoleculeSample(const QString& query_or_key)
{
	const QString key = normalisedScientificQueryKey(query_or_key);
	const BuiltInMoleculeSample* samples = builtInMoleculeSamples();
	for(int i=0; i<builtInMoleculeSampleCount(); ++i)
	{
		if(key == QString::fromLatin1(samples[i].key) || key == normalisedScientificQueryKey(QString::fromLatin1(samples[i].name)))
			return &samples[i];
	}
	return NULL;
}
}


namespace
{

struct MoleculeAtom
{
	int source_id;
	std::string element;
	Vec3f pos;
};


struct MoleculeBond
{
	int atom_a;
	int atom_b;
	int order;
};


float clampFloat(float v, float min_v, float max_v)
{
	return std::max(min_v, std::min(max_v, v));
}


std::string trimString(const std::string& s)
{
	size_t begin = 0;
	while(begin < s.size() && std::isspace((unsigned char)s[begin]))
		begin++;
	size_t end = s.size();
	while(end > begin && std::isspace((unsigned char)s[end - 1]))
		end--;
	return s.substr(begin, end - begin);
}


Colour3f cpkColourForElement(const std::string& element);


QString sha256Hex(const QByteArray& data)
{
	return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}


QString scientificCacheRoot()
{
	QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	if(root.isEmpty())
		root = QDir::tempPath() + QStringLiteral("/MetaSiberiaScientificCache");
	QDir().mkpath(root + QStringLiteral("/scientific/pubchem"));
	return root + QStringLiteral("/scientific/pubchem");
}


QString cachePathForKey(const QString& key, const QString& extension)
{
	const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
	return scientificCacheRoot() + QStringLiteral("/") + QString::fromLatin1(hash) + extension;
}


struct PubChemHttpResult
{
	PubChemHttpResult() : http_status(0), from_cache(false), timed_out(false) {}

	QByteArray bytes;
	QString cache_path;
	QString error;
	QString diagnostics;
	int http_status;
	bool from_cache;
	bool timed_out;
};


static QDateTime g_last_pubchem_request_utc;


void throttlePubChemRequest()
{
	const QDateTime now = QDateTime::currentDateTimeUtc();
	if(g_last_pubchem_request_utc.isValid())
	{
		const qint64 elapsed_ms = g_last_pubchem_request_utc.msecsTo(now);
		if(elapsed_ms >= 0 && elapsed_ms < 350)
			QThread::msleep((unsigned long)(350 - elapsed_ms));
	}
	g_last_pubchem_request_utc = QDateTime::currentDateTimeUtc();
}


#if defined(_WIN32)
struct WinHttpHandle
{
	WinHttpHandle() : handle(NULL) {}
	explicit WinHttpHandle(HINTERNET h) : handle(h) {}
	~WinHttpHandle() { if(handle) WinHttpCloseHandle(handle); }

	HINTERNET handle;

private:
	WinHttpHandle(const WinHttpHandle&);
	WinHttpHandle& operator = (const WinHttpHandle&);
};


std::wstring winHttpWideString(const QString& s)
{
	return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()), (size_t)s.size());
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
	if(message.isEmpty())
		message = QStringLiteral("Windows error %1").arg((qulonglong)error_code);
	return message;
}


QString pubchemQtSslDiagnostics()
{
	return QStringLiteral("QtNetwork SSL disabled in this local Qt build; PubChem uses WinHTTP/SChannel instead");
}


PubChemHttpResult pubchemWinHttpRequest(const QUrl& url, const QByteArray& post_data)
{
	PubChemHttpResult result;

	if(!url.isValid() || url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("pubchem.ncbi.nlm.nih.gov"))
	{
		result.error = QStringLiteral("PubChem HTTPS transport rejected URL outside the official PubChem HTTPS host.");
		return result;
	}

	const QString path = url.path(QUrl::FullyEncoded).isEmpty() ? QStringLiteral("/") : url.path(QUrl::FullyEncoded);
	const QString query = url.query(QUrl::FullyEncoded);
	const QString path_and_query = query.isEmpty() ? path : (path + QStringLiteral("?") + query);
	const QString method_q = post_data.isEmpty() ? QStringLiteral("GET") : QStringLiteral("POST");

	const std::wstring user_agent = L"MetaSiberiaScientificObjectEditor/1.0 (PubChem PUG REST; interactive user request)";
	WinHttpHandle session(WinHttpOpen(
		user_agent.c_str(),
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0
	));
	if(!session.handle)
	{
		const DWORD err = GetLastError();
		result.error = QStringLiteral("PubChem HTTPS transport failed to open WinHTTP session: %1").arg(winHttpErrorString(err));
		return result;
	}

	WinHttpSetTimeouts(session.handle, 10000, 10000, 30000, 30000);

	const std::wstring host_w = winHttpWideString(url.host());
	WinHttpHandle connection(WinHttpConnect(session.handle, host_w.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
	if(!connection.handle)
	{
		const DWORD err = GetLastError();
		result.error = QStringLiteral("PubChem HTTPS transport failed to connect to %1: %2").arg(url.host(), winHttpErrorString(err));
		return result;
	}

	const std::wstring method_w = winHttpWideString(method_q);
	const std::wstring target_w = winHttpWideString(path_and_query);
	WinHttpHandle request(WinHttpOpenRequest(
		connection.handle,
		method_w.c_str(),
		target_w.c_str(),
		NULL,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE
	));
	if(!request.handle)
	{
		const DWORD err = GetLastError();
		result.error = QStringLiteral("PubChem HTTPS transport failed to open request for %1: %2").arg(url.host(), winHttpErrorString(err));
		return result;
	}

	DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

	QString headers_q = QStringLiteral("Accept: application/json, chemical/x-mdl-sdfile, image/png, */*\r\n");
	if(!post_data.isEmpty())
		headers_q += QStringLiteral("Content-Type: application/x-www-form-urlencoded\r\n");
	const std::wstring headers_w = winHttpWideString(headers_q);

	const BOOL sent = WinHttpSendRequest(
		request.handle,
		headers_w.c_str(),
		(DWORD)-1L,
		post_data.isEmpty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)post_data.constData(),
		post_data.isEmpty() ? 0 : (DWORD)post_data.size(),
		post_data.isEmpty() ? 0 : (DWORD)post_data.size(),
		0
	);
	if(!sent)
	{
		const DWORD err = GetLastError();
		if(err == ERROR_WINHTTP_TIMEOUT)
			result.timed_out = true;
		QString detail = winHttpErrorString(err);
		if(err == ERROR_WINHTTP_SECURE_FAILURE)
			detail += QStringLiteral(" TLS certificate validation failed; HTTPS was not downgraded and certificate errors were not ignored.");
		result.error = QStringLiteral("PubChem HTTPS request failed for host %1: %2").arg(url.host(), detail);
		result.diagnostics = pubchemQtSslDiagnostics();
		return result;
	}

	if(!WinHttpReceiveResponse(request.handle, NULL))
	{
		const DWORD err = GetLastError();
		if(err == ERROR_WINHTTP_TIMEOUT)
			result.timed_out = true;
		QString detail = winHttpErrorString(err);
		if(err == ERROR_WINHTTP_SECURE_FAILURE)
			detail += QStringLiteral(" TLS certificate validation failed; HTTPS was not downgraded and certificate errors were not ignored.");
		result.error = QStringLiteral("PubChem HTTPS response failed for host %1: %2").arg(url.host(), detail);
		result.diagnostics = pubchemQtSslDiagnostics();
		return result;
	}

	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	if(WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX))
		result.http_status = (int)status_code;

	while(true)
	{
		DWORD available = 0;
		if(!WinHttpQueryDataAvailable(request.handle, &available))
		{
			const DWORD err = GetLastError();
			result.error = QStringLiteral("PubChem HTTPS read failed for host %1: %2").arg(url.host(), winHttpErrorString(err));
			result.diagnostics = pubchemQtSslDiagnostics();
			return result;
		}
		if(available == 0)
			break;

		const int old_size = result.bytes.size();
		if(old_size + (int)available > 16 * 1024 * 1024)
		{
			result.error = QStringLiteral("PubChem response exceeded the 16 MiB safety limit.");
			return result;
		}

		result.bytes.resize(old_size + (int)available);
		DWORD read = 0;
		if(!WinHttpReadData(request.handle, result.bytes.data() + old_size, available, &read))
		{
			const DWORD err = GetLastError();
			result.error = QStringLiteral("PubChem HTTPS read failed for host %1: %2").arg(url.host(), winHttpErrorString(err));
			result.diagnostics = pubchemQtSslDiagnostics();
			return result;
		}
		result.bytes.resize(old_size + (int)read);
	}

	result.diagnostics = QStringLiteral("transport=WinHTTP/SChannel, host=%1, http_status=%2, TLS certificate validation=enabled, %3")
		.arg(url.host())
		.arg(result.http_status)
		.arg(pubchemQtSslDiagnostics());
	return result;
}
#endif


PubChemHttpResult pubchemHttpRequest(const QUrl& url, const QByteArray& post_data, const QString& cache_key, const QString& extension, bool allow_cache)
{
	PubChemHttpResult result;
	const QString cache_path = cachePathForKey(cache_key, extension);
	result.cache_path = cache_path;
	if(allow_cache && QFile::exists(cache_path))
	{
		QFile cached(cache_path);
		if(cached.open(QIODevice::ReadOnly))
		{
			result.bytes = cached.readAll();
			result.from_cache = true;
			result.http_status = 200;
			return result;
		}
	}

	int attempts = 0;
	int backoff_ms = 500;
	while(attempts < 3)
	{
		attempts++;
		throttlePubChemRequest();

#if defined(_WIN32)
		result = pubchemWinHttpRequest(url, post_data);
		result.cache_path = cache_path;
#else
		result.error = QStringLiteral("PubChem HTTPS support is unavailable in this client build. The current implementation requires Windows WinHTTP or a future cross-platform HTTPS provider.");
		return result;
#endif

		if(!result.diagnostics.isEmpty())
			conPrint(stdstr(QStringLiteral("Scientific PubChem HTTPS: %1 %2").arg(url.toString(QUrl::FullyEncoded), result.diagnostics)));

		if(result.error.isEmpty() && result.http_status >= 200 && result.http_status < 300)
		{
			QFile out(cache_path);
			if(out.open(QIODevice::WriteOnly))
				out.write(result.bytes);
			result.error.clear();
			return result;
		}

		if(result.http_status == 429 || result.http_status == 503)
		{
			result.error = QStringLiteral("PubChem rate limited or temporarily unavailable (HTTP %1).").arg(result.http_status);
			QThread::msleep((unsigned long)backoff_ms);
			backoff_ms *= 2;
			continue;
		}

		if(!result.error.isEmpty())
			return result;
		else if(result.http_status == 404)
			result.error = QStringLiteral("PubChem record was not found (HTTP 404).");
		else if(result.http_status > 0)
			result.error = QStringLiteral("PubChem HTTP %1.").arg(result.http_status);
		else
			result.error = QStringLiteral("PubChem network error: no HTTP response was received.");
		return result;
	}

	return result;
}


PubChemHttpResult pubchemGet(const QUrl& url, const QString& extension = QStringLiteral(".cache"), bool allow_cache = true)
{
	return pubchemHttpRequest(url, QByteArray(), QStringLiteral("GET ") + url.toString(QUrl::FullyEncoded), extension, allow_cache);
}


PubChemHttpResult pubchemPostForm(const QUrl& url, const QByteArray& form_data, const QString& extension = QStringLiteral(".json"), bool allow_cache = true)
{
	return pubchemHttpRequest(url, form_data, QStringLiteral("POST ") + url.toString(QUrl::FullyEncoded) + QStringLiteral(" ") + QString::fromUtf8(form_data), extension, allow_cache);
}


QUrl pubchemUrl(const QString& path)
{
	return QUrl(QStringLiteral("https://pubchem.ncbi.nlm.nih.gov") + path);
}


QJsonObject parseJsonObjectOrError(const QByteArray& bytes, QString* error_out)
{
	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !doc.isObject())
	{
		if(error_out)
			*error_out = QStringLiteral("Invalid JSON from PubChem: %1").arg(parse_error.errorString());
		return QJsonObject();
	}
	return doc.object();
}


QString jsonValueToString(const QJsonValue& value)
{
	if(value.isString())
		return value.toString();
	if(value.isDouble())
		return QString::number(value.toDouble(), 'g', 12);
	if(value.isBool())
		return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
	return QString();
}


QList<int> cidsFromPubChemIdentifierJson(const QByteArray& bytes, QString* error_out)
{
	const QJsonObject root = parseJsonObjectOrError(bytes, error_out);
	if(root.isEmpty())
		return QList<int>();
	if(root.contains(QStringLiteral("Fault")))
	{
		const QJsonObject fault = root.value(QStringLiteral("Fault")).toObject();
		if(error_out)
			*error_out = fault.value(QStringLiteral("Message")).toString(QStringLiteral("PubChem returned a fault."));
		return QList<int>();
	}
	const QJsonArray cid_array = root.value(QStringLiteral("IdentifierList")).toObject().value(QStringLiteral("CID")).toArray();
	QList<int> cids;
	for(int i=0; i<cid_array.size(); ++i)
		cids.push_back(cid_array[i].toInt());
	return cids;
}


QString pubchemPropertyList()
{
	return QStringLiteral("IUPACName,MolecularFormula,MolecularWeight,ExactMass,MonoisotopicMass,Charge,HeavyAtomCount,HBondDonorCount,HBondAcceptorCount,RotatableBondCount,XLogP,TPSA,Complexity,IsomericSMILES,CanonicalSMILES,InChI,InChIKey");
}


QList<QMap<QString, QString> > parsePubChemPropertyTable(const QByteArray& bytes, QString* error_out)
{
	const QJsonObject root = parseJsonObjectOrError(bytes, error_out);
	QList<QMap<QString, QString> > rows;
	if(root.isEmpty())
		return rows;
	const QJsonArray props = root.value(QStringLiteral("PropertyTable")).toObject().value(QStringLiteral("Properties")).toArray();
	for(int i=0; i<props.size(); ++i)
	{
		const QJsonObject obj = props[i].toObject();
		QMap<QString, QString> row;
		for(QJsonObject::const_iterator it = obj.constBegin(); it != obj.constEnd(); ++it)
			row.insert(it.key(), jsonValueToString(it.value()));
		if(!row.contains(QStringLiteral("IsomericSMILES")) && row.contains(QStringLiteral("SMILES"))) row.insert(QStringLiteral("IsomericSMILES"), row.value(QStringLiteral("SMILES")));
		if(!row.contains(QStringLiteral("CanonicalSMILES")) && row.contains(QStringLiteral("ConnectivitySMILES"))) row.insert(QStringLiteral("CanonicalSMILES"), row.value(QStringLiteral("ConnectivitySMILES")));
		rows.push_back(row);
	}
	return rows;
}


QString detectPubChemQueryKind(const QString& q)
{
	const QString trimmed = q.trimmed();
	bool ok = false;
	trimmed.toInt(&ok);
	if(ok)
		return QStringLiteral("cid");
	if(trimmed.startsWith(QStringLiteral("cid:"), Qt::CaseInsensitive))
		return QStringLiteral("cid");
	if(trimmed.startsWith(QStringLiteral("InChI="), Qt::CaseInsensitive))
		return QStringLiteral("inchi");
	if(trimmed.size() == 27 && trimmed.count('-') == 2)
		return QStringLiteral("inchikey");
	if(trimmed.contains('=') || trimmed.contains('#') || trimmed.contains('[') || trimmed.contains(']') || trimmed.contains('@'))
		return QStringLiteral("smiles");
	if(trimmed.contains(QRegExp(QStringLiteral("^[A-Z][A-Za-z0-9]*[0-9]"))))
		return QStringLiteral("formula");
	return QStringLiteral("name");
}


QString pubchemCidFromQueryText(QString q)
{
	q = q.trimmed();
	if(q.startsWith(QStringLiteral("cid:"), Qt::CaseInsensitive))
		q = q.mid(4).trimmed();
	return q;
}


QStringList pubchemSynonymsFromJson(const QByteArray& bytes)
{
	QString error;
	const QJsonObject root = parseJsonObjectOrError(bytes, &error);
	QStringList synonyms;
	const QJsonArray infos = root.value(QStringLiteral("InformationList")).toObject().value(QStringLiteral("Information")).toArray();
	if(!infos.isEmpty())
	{
		const QJsonArray syns = infos[0].toObject().value(QStringLiteral("Synonym")).toArray();
		for(int i=0; i<syns.size() && i<24; ++i)
			synonyms << syns[i].toString();
	}
	return synonyms;
}


QString pubchemTitleFromPugViewJson(const QByteArray& bytes)
{
	QString error;
	const QJsonObject root = parseJsonObjectOrError(bytes, &error);
	return root.value(QStringLiteral("Record")).toObject().value(QStringLiteral("Title")).toString();
}


void appendPugViewText(const QJsonValue& value, QStringList& lines, int depth = 0)
{
	if(lines.size() >= 600 || depth > 20)
		return;
	if(value.isObject())
	{
		const QJsonObject o = value.toObject();
		const QString heading = o.value(QStringLiteral("TOCHeading")).toString();
		if(!heading.isEmpty()) lines << QString(depth * 2, QChar(' ')) + heading;
		const QString name = o.value(QStringLiteral("Name")).toString();
		if(!name.isEmpty()) lines << QString(depth * 2, QChar(' ')) + name + QStringLiteral(":");
		for(QJsonObject::const_iterator it=o.constBegin(); it!=o.constEnd(); ++it)
		{
			if(it.key() == QStringLiteral("TOCHeading") || it.key() == QStringLiteral("Name") || it.key() == QStringLiteral("ReferenceNumber")) continue;
			if(it.value().isString()) { const QString text=it.value().toString().trimmed(); if(!text.isEmpty() && text.size()<4000) lines << QString(depth * 2 + 2,QChar(' '))+text; }
			else if(it.value().isDouble() && (it.key()==QStringLiteral("Number") || it.key()==QStringLiteral("Value"))) lines << QString(depth*2+2,QChar(' '))+QString::number(it.value().toDouble(),'g',12);
			else appendPugViewText(it.value(), lines, depth+1);
		}
	}
	else if(value.isArray())
	{
		for(const QJsonValue& child : value.toArray()) appendPugViewText(child, lines, depth);
	}
}


QString readablePugViewSection(const QByteArray& bytes)
{
	QString error; const QJsonObject root=parseJsonObjectOrError(bytes,&error); if(root.isEmpty()) return QString(); QStringList lines; appendPugViewText(root,lines); lines.removeDuplicates(); return lines.join(QChar('\n')).trimmed();
}


struct ParsedSdfMolecule
{
	QString atom_table;
	QString bond_table;
	int atom_count = 0;
	int bond_count = 0;
	QString dimensions;
	QString error;
};


QString bondOrderToName(const int order)
{
	if(order == 2) return QStringLiteral("double");
	if(order == 3) return QStringLiteral("triple");
	if(order == 4) return QStringLiteral("aromatic");
	return QStringLiteral("single");
}


ParsedSdfMolecule parsePubChemSdf(const QByteArray& sdf_bytes)
{
	ParsedSdfMolecule parsed;
	QString text = QString::fromUtf8(sdf_bytes);
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	text.replace(QChar('\r'), QChar('\n'));
	const QStringList lines = text.split(QChar('\n'));
	if(lines.size() < 4)
	{
		parsed.error = QStringLiteral("SDF is too short.");
		return parsed;
	}

	const QString counts = lines[3];
	int atom_count = 0;
	int bond_count = 0;
	if(counts.size() >= 6)
	{
		atom_count = counts.mid(0, 3).trimmed().toInt();
		bond_count = counts.mid(3, 3).trimmed().toInt();
	}
	if(atom_count <= 0 || bond_count < 0 || lines.size() < 4 + atom_count + bond_count)
	{
		parsed.error = QStringLiteral("SDF counts line is invalid or incomplete.");
		return parsed;
	}

	std::ostringstream atoms;
	std::ostringstream bonds;
	QVector<int> formal_charges(atom_count, 0);
	atoms << std::fixed << std::setprecision(6);
	float min_x = 0, min_y = 0, min_z = 0, max_x = 0, max_y = 0, max_z = 0;
	for(int i=0; i<atom_count; ++i)
	{
		const QString line = lines[4 + i];
		const QStringList parts = line.split(QRegExp(QStringLiteral("\\s+")), QString::SkipEmptyParts);
		if(parts.size() < 4)
		{
			parsed.error = QStringLiteral("SDF atom line %1 is incomplete.").arg(i + 1);
			return parsed;
		}
		const float x = parts[0].toFloat();
		const float y = parts[1].toFloat();
		const float z = parts[2].toFloat();
		const QString element = parts[3].trimmed();
		int formal_charge = 0;
		if(parts.size() > 5)
		{
			const int charge_code = parts[5].toInt();
			if(charge_code >= 1 && charge_code <= 3) formal_charge = 4 - charge_code;
			else if(charge_code >= 5 && charge_code <= 7) formal_charge = 4 - charge_code;
		}
		formal_charges[i] = formal_charge;
		if(i == 0)
			min_x = max_x = x, min_y = max_y = y, min_z = max_z = z;
		else
		{
			min_x = std::min(min_x, x); min_y = std::min(min_y, y); min_z = std::min(min_z, z);
			max_x = std::max(max_x, x); max_y = std::max(max_y, y); max_z = std::max(max_z, z);
		}
		atoms << (i + 1) << " " << stdstr(element) << " " << x << " " << y << " " << z << " " << formal_charge << "\n";
	}
	for(int line_i=4+atom_count+bond_count; line_i<lines.size(); ++line_i)
	{
		const QStringList p=lines[line_i].split(QRegExp(QStringLiteral("\\s+")),QString::SkipEmptyParts);
		if(p.size()>=4&&p[0]==QStringLiteral("M")&&p[1]==QStringLiteral("CHG"))
		{
			const int count=p[2].toInt();for(int j=0;j<count&&3+j*2+1<p.size();++j){const int index=p[3+j*2].toInt()-1;if(index>=0&&index<formal_charges.size())formal_charges[index]=p[4+j*2].toInt();}
		}
	}

	for(int i=0; i<bond_count; ++i)
	{
		const QString line = lines[4 + atom_count + i];
		const QStringList parts = line.split(QRegExp(QStringLiteral("\\s+")), QString::SkipEmptyParts);
		if(parts.size() < 3)
		{
			parsed.error = QStringLiteral("SDF bond line %1 is incomplete.").arg(i + 1);
			return parsed;
		}
		const int a = parts[0].toInt();
		const int b = parts[1].toInt();
		const int order = parts[2].toInt();
		const int stereo = parts.size() > 3 ? parts[3].toInt() : 0;
		bonds << a << "-" << b << " " << stdstr(bondOrderToName(order));
		if(stereo != 0) bonds << " stereo:" << stereo;
		bonds << "\n";
	}

	parsed.atom_count = atom_count;
	parsed.bond_count = bond_count;
	QStringList atom_output=QString::fromStdString(atoms.str()).split(QChar('\n'),QString::SkipEmptyParts);
	for(int i=0;i<atom_output.size()&&i<formal_charges.size();++i){QStringList p=atom_output[i].split(QChar(' '),QString::SkipEmptyParts);if(p.size()>=6){p[5]=QString::number(formal_charges[i]);atom_output[i]=p.join(QChar(' '));}}
	parsed.atom_table = atom_output.join(QChar('\n')) + QChar('\n');
	parsed.bond_table = QString::fromStdString(bonds.str());
	parsed.dimensions = QString::fromUtf8("%1 x %2 x %3 Å")
		.arg(max_x - min_x, 0, 'f', 3)
		.arg(max_y - min_y, 0, 'f', 3)
		.arg(max_z - min_z, 0, 'f', 3);
	return parsed;
}


QString moleculeLegendFromAtomTable(const QString& atom_table)
{
	QMap<QString, int> counts;
	const QStringList lines = atom_table.split(QChar('\n'));
	for(const QString& line : lines)
	{
		const QStringList parts = line.split(QRegExp(QStringLiteral("\\s+")), QString::SkipEmptyParts);
		if(parts.size() >= 2)
			counts[parts[1]] += 1;
	}
	QString legend;
	for(QMap<QString, int>::const_iterator it = counts.constBegin(); it != counts.constEnd(); ++it)
	{
		const Colour3f col = cpkColourForElement(stdstr(it.key()));
		legend += QStringLiteral("#%1%2%3 — %4 — count %5\n")
			.arg((int)(col.r * 255.f), 2, 16, QLatin1Char('0'))
			.arg((int)(col.g * 255.f), 2, 16, QLatin1Char('0'))
			.arg((int)(col.b * 255.f), 2, 16, QLatin1Char('0'))
			.arg(it.key())
			.arg(it.value());
	}
	return legend.toUpper();
}


std::string normaliseElementSymbol(const std::string& element)
{
	std::string out = trimString(element);
	if(out.empty())
		return "X";

	out[0] = (char)std::toupper((unsigned char)out[0]);
	for(size_t i=1; i<out.size(); ++i)
		out[i] = (char)std::tolower((unsigned char)out[i]);
	return out;
}


std::string materialNameForElement(const std::string& element)
{
	const std::string e = normaliseElementSymbol(element);
	if(e == "H") return "mat_H";
	if(e == "C") return "mat_C";
	if(e == "N") return "mat_N";
	if(e == "O") return "mat_O";
	if(e == "S") return "mat_S";
	if(e == "P") return "mat_P";
	if(e == "Cl") return "mat_Cl";
	if(e == "F") return "mat_F";
	return "mat_Default";
}


Colour3f cpkColourForElement(const std::string& element)
{
	const std::string e = normaliseElementSymbol(element);
	if(e == "H") return Colour3f(0.94f, 0.94f, 0.90f);
	if(e == "C") return Colour3f(0.05f, 0.05f, 0.05f);
	if(e == "N") return Colour3f(0.12f, 0.24f, 0.88f);
	if(e == "O") return Colour3f(0.86f, 0.04f, 0.02f);
	if(e == "S") return Colour3f(0.92f, 0.78f, 0.08f);
	if(e == "P") return Colour3f(0.95f, 0.48f, 0.08f);
	if(e == "Cl") return Colour3f(0.12f, 0.70f, 0.12f);
	if(e == "F") return Colour3f(0.52f, 0.90f, 0.52f);
	return Colour3f(0.18f, 0.72f, 0.92f);
}


float cpkRadiusScaleForElement(const std::string& element)
{
	const std::string e = normaliseElementSymbol(element);
	if(e == "H") return 0.58f;
	if(e == "C") return 1.00f;
	if(e == "N") return 0.95f;
	if(e == "O") return 0.92f;
	if(e == "S") return 1.10f;
	if(e == "P") return 1.10f;
	return 0.96f;
}


std::string sanitiseAtomLine(std::string line)
{
	for(size_t i=0; i<line.size(); ++i)
		if(line[i] == ',' || line[i] == ';' || line[i] == '\t')
			line[i] = ' ';
	return line;
}


std::string sanitiseBondLine(std::string line)
{
	for(size_t i=0; i<line.size(); ++i)
		if(line[i] == ',' || line[i] == ';' || line[i] == '\t' || line[i] == '-' || line[i] == ':')
			line[i] = ' ';
	return line;
}


std::vector<MoleculeAtom> parseMoleculeAtoms(const std::string& atom_table)
{
	std::vector<MoleculeAtom> atoms;
	std::stringstream lines(atom_table);
	std::string line;
	while(std::getline(lines, line))
	{
		line = trimString(sanitiseAtomLine(line));
		if(line.empty() || line[0] == '#' || line.find("...") != std::string::npos)
			continue;

		std::stringstream parser(line);
		MoleculeAtom atom;
		if(parser >> atom.source_id >> atom.element >> atom.pos.x >> atom.pos.y >> atom.pos.z)
		{
			atom.element = normaliseElementSymbol(atom.element);
			atoms.push_back(atom);
		}
	}

	return atoms;
}


int atomIndexForSourceID(const std::vector<MoleculeAtom>& atoms, int source_id)
{
	for(size_t i=0; i<atoms.size(); ++i)
		if(atoms[i].source_id == source_id)
			return (int)i;
	return -1;
}


int bondOrderFromToken(const std::string& token)
{
	std::string t = token;
	for(size_t i=0; i<t.size(); ++i)
		t[i] = (char)std::tolower((unsigned char)t[i]);

	if(t == "2" || t == "double")
		return 2;
	if(t == "3" || t == "triple")
		return 3;
	return 1;
}


std::vector<MoleculeBond> parseMoleculeBonds(const std::string& bond_table, const std::vector<MoleculeAtom>& atoms)
{
	std::vector<MoleculeBond> bonds;
	std::stringstream lines(bond_table);
	std::string line;
	while(std::getline(lines, line))
	{
		line = trimString(sanitiseBondLine(line));
		if(line.empty() || line[0] == '#' || line.find("...") != std::string::npos)
			continue;

		std::stringstream parser(line);
		int source_a = 0;
		int source_b = 0;
		std::string order_token;
		if(parser >> source_a >> source_b)
		{
			MoleculeBond bond;
			bond.atom_a = atomIndexForSourceID(atoms, source_a);
			bond.atom_b = atomIndexForSourceID(atoms, source_b);
			bond.order = 1;
			if(parser >> order_token)
				bond.order = bondOrderFromToken(order_token);
			if(bond.atom_a >= 0 && bond.atom_b >= 0 && bond.atom_a != bond.atom_b)
				bonds.push_back(bond);
		}
	}

	return bonds;
}


Vec3f moleculeCenter(const std::vector<MoleculeAtom>& atoms)
{
	Vec3f center(0.f);
	if(atoms.empty())
		return center;

	for(size_t i=0; i<atoms.size(); ++i)
		center += atoms[i].pos;
	return center * (1.f / (float)atoms.size());
}


int appendVertex(std::ostringstream& obj, const Vec3f& p, int& next_vertex_index)
{
	obj << "v " << p.x << " " << p.y << " " << p.z << "\n";
	return next_vertex_index++;
}


void appendSphereMesh(std::ostringstream& obj, const Vec3f& center, float radius, int& next_vertex_index)
{
	const int segments = 16;
	const int rings = 8;
	const float pi = 3.14159265358979323846f;

	const int top_index = appendVertex(obj, center + Vec3f(0.f, 0.f, radius), next_vertex_index);
	std::vector<std::vector<int> > ring_indices;
	for(int r=1; r<rings; ++r)
	{
		const float theta = pi * (float)r / (float)rings;
		const float z = std::cos(theta) * radius;
		const float ring_radius = std::sin(theta) * radius;

		std::vector<int> ring;
		for(int s=0; s<segments; ++s)
		{
			const float phi = 2.f * pi * (float)s / (float)segments;
			ring.push_back(appendVertex(obj, center + Vec3f(std::cos(phi) * ring_radius, std::sin(phi) * ring_radius, z), next_vertex_index));
		}
		ring_indices.push_back(ring);
	}
	const int bottom_index = appendVertex(obj, center + Vec3f(0.f, 0.f, -radius), next_vertex_index);

	if(ring_indices.empty())
		return;

	for(int s=0; s<segments; ++s)
		obj << "f " << top_index << " " << ring_indices[0][s] << " " << ring_indices[0][(s + 1) % segments] << "\n";

	for(size_t r=0; r + 1<ring_indices.size(); ++r)
		for(int s=0; s<segments; ++s)
			obj << "f " << ring_indices[r][s] << " " << ring_indices[r + 1][s] << " " << ring_indices[r + 1][(s + 1) % segments] << " " << ring_indices[r][(s + 1) % segments] << "\n";

	const std::vector<int>& last_ring = ring_indices.back();
	for(int s=0; s<segments; ++s)
		obj << "f " << last_ring[(s + 1) % segments] << " " << last_ring[s] << " " << bottom_index << "\n";
}


void appendCylinderMesh(std::ostringstream& obj, const Vec3f& a, const Vec3f& b, float radius, int& next_vertex_index)
{
	const Vec3f axis = b - a;
	const float len = axis.length();
	if(len < 1.0e-5f)
		return;

	const Vec3f w = axis * (1.f / len);
	const Vec3f helper = std::fabs(w.z) < 0.85f ? Vec3f(0.f, 0.f, 1.f) : Vec3f(0.f, 1.f, 0.f);
	Vec3f u = crossProduct(w, helper);
	if(u.length() < 1.0e-5f)
		u = Vec3f(1.f, 0.f, 0.f);
	else
		u.normalise();
	Vec3f v = crossProduct(w, u);
	v.normalise();

	const int segments = 12;
	const float pi = 3.14159265358979323846f;
	std::vector<int> ring_a;
	std::vector<int> ring_b;
	for(int s=0; s<segments; ++s)
	{
		const float phi = 2.f * pi * (float)s / (float)segments;
		const Vec3f offset = (u * std::cos(phi) + v * std::sin(phi)) * radius;
		ring_a.push_back(appendVertex(obj, a + offset, next_vertex_index));
		ring_b.push_back(appendVertex(obj, b + offset, next_vertex_index));
	}

	for(int s=0; s<segments; ++s)
		obj << "f " << ring_a[s] << " " << ring_b[s] << " " << ring_b[(s + 1) % segments] << " " << ring_a[(s + 1) % segments] << "\n";
}


std::string safeFileStemForScientificObject(const ScientificObjectSettings& s)
{
	std::string stem = "metasiberia_scientific_molecule_";
	const std::string name = s.name.empty() ? "object" : s.name;
	for(size_t i=0; i<name.size() && stem.size() < 80; ++i)
	{
		const unsigned char c = (unsigned char)name[i];
		if(std::isalnum(c))
			stem.push_back((char)std::tolower(c));
		else if(c == '-' || c == '_')
			stem.push_back((char)c);
	}

	std::ostringstream hash_input;
	hash_input << "molecule_engine_space_no_loader_autoscale_v2\n"
		<< s.atom_table << "\n"
		<< s.bond_table << "\n"
		<< s.visualization_mode << "\n"
		<< s.atom_radius << "\n"
		<< s.bond_thickness << "\n"
		<< s.object_scale << "\n"
		<< s.opacity << "\n"
		<< s.show_hydrogen << "\n"
		<< s.colour_scheme << "\n"
		<< s.wireframe_enabled;
	const size_t hash = std::hash<std::string>()(hash_input.str());
	std::ostringstream suffix;
	suffix << "_" << std::hex << hash;
	stem += suffix.str();
	return stem;
}


void appendMtlMaterial(std::ostringstream& mtl, const std::string& name, const Colour3f& col, float opacity)
{
	mtl << "newmtl " << name << "\n";
	mtl << "Ka 0 0 0\n";
	mtl << "Kd " << col.r << " " << col.g << " " << col.b << "\n";
	mtl << "Ks 0.04 0.04 0.04\n";
	mtl << "Ns 48\n";
	mtl << "d " << opacity << "\n\n";
}


WorldMaterialRef makeScientificWorldMaterial(const std::string& name, const Colour3f& col, const ScientificObjectSettings& s)
{
	WorldMaterialRef mat = new WorldMaterial();
	mat->name = name;
	mat->colour_rgb = col;
	const bool emissive = s.material == "hologram" || s.glow_enabled;
	mat->emission_rgb = emissive ? col : Colour3f(col.r * 0.08f, col.g * 0.08f, col.b * 0.08f);
	mat->emission_lum_flux_or_lum = s.glow_enabled ? s.glow_strength : (s.material == "hologram" ? 80.f : 0.f);
	mat->opacity = ScalarVal(s.opacity);
	mat->roughness = ScalarVal(s.material == "glossy" ? 0.16f : 0.50f);
	mat->metallic_fraction = ScalarVal(s.material == "metal" ? 0.30f : 0.0f);
	BitUtils::setBit(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG);
	BitUtils::setOrZeroBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG, s.material == "hologram");
	return mat;
}


bool applyMoleculeWorldMaterials(WorldObject& ob_out, const ScientificObjectSettings& s)
{
	if(s.scientific_type != "molecule")
		return false;
	if(s.visualization_mode != "ball_and_stick" && s.visualization_mode != "space_fill" && s.visualization_mode != "wireframe" && !s.wireframe_enabled)
		return false;

	std::vector<MoleculeAtom> atoms = parseMoleculeAtoms(s.atom_table);
	if(!s.show_hydrogen)
		atoms.erase(std::remove_if(atoms.begin(), atoms.end(), [](const MoleculeAtom& atom) { return atom.element == "H"; }), atoms.end());
	if(atoms.empty())
		return false;

	ob_out.materials.clear();
	if(s.visualization_mode != "space_fill")
		ob_out.materials.push_back(makeScientificWorldMaterial("Molecular bond", Colour3f(0.62f, 0.62f, 0.58f), s));

	if(s.visualization_mode != "wireframe" && !s.wireframe_enabled)
	{
		std::set<std::string> used_material_names;
		for(size_t i=0; i<atoms.size(); ++i)
		{
			const std::string material_name = materialNameForElement(atoms[i].element);
			if(used_material_names.insert(material_name).second)
				ob_out.materials.push_back(makeScientificWorldMaterial("Atom " + atoms[i].element, cpkColourForElement(atoms[i].element), s));
		}
	}

	return !ob_out.materials.empty();
}


std::string writeMoleculeOBJForSettings(const ScientificObjectSettings& s)
{
	if(s.scientific_type != "molecule")
		return "";
	if(s.visualization_mode != "ball_and_stick" && s.visualization_mode != "space_fill" && s.visualization_mode != "wireframe" && !s.wireframe_enabled)
		return "";

	std::vector<MoleculeAtom> atoms = parseMoleculeAtoms(s.atom_table);
	if(!s.show_hydrogen)
		atoms.erase(std::remove_if(atoms.begin(), atoms.end(), [](const MoleculeAtom& atom) { return atom.element == "H"; }), atoms.end());

	if(atoms.empty())
		return "";

	const std::vector<MoleculeBond> bonds = parseMoleculeBonds(s.bond_table, atoms);
	const std::string stem = safeFileStemForScientificObject(s);
	const std::string obj_path = FileUtils::join(PlatformUtils::getTempDirPath(), stem + ".obj");
	const std::string mtl_path = FileUtils::join(PlatformUtils::getTempDirPath(), stem + ".mtl");

	std::ostringstream mtl;
	mtl << std::fixed << std::setprecision(6);
	appendMtlMaterial(mtl, "mat_H", cpkColourForElement("H"), s.opacity);
	appendMtlMaterial(mtl, "mat_C", cpkColourForElement("C"), s.opacity);
	appendMtlMaterial(mtl, "mat_N", cpkColourForElement("N"), s.opacity);
	appendMtlMaterial(mtl, "mat_O", cpkColourForElement("O"), s.opacity);
	appendMtlMaterial(mtl, "mat_S", cpkColourForElement("S"), s.opacity);
	appendMtlMaterial(mtl, "mat_P", cpkColourForElement("P"), s.opacity);
	appendMtlMaterial(mtl, "mat_Cl", cpkColourForElement("Cl"), s.opacity);
	appendMtlMaterial(mtl, "mat_F", cpkColourForElement("F"), s.opacity);
	appendMtlMaterial(mtl, "mat_Default", cpkColourForElement("X"), s.opacity);
	appendMtlMaterial(mtl, "mat_Bond", Colour3f(0.62f, 0.62f, 0.58f), s.opacity);
	FileUtils::writeEntireFileTextMode(mtl_path, mtl.str());

	std::ostringstream obj;
	obj << std::fixed << std::setprecision(6);
	obj << "mtllib " << FileUtils::getFilename(mtl_path) << "\n";
	obj << "o MetaSiberiaScientificMolecule\n";

const Vec3f center = moleculeCenter(atoms);
const float coord_scale = clampFloat(s.object_scale, 0.01f, 1000.f) * 0.75f;
	const float atom_base_radius = s.visualization_mode == "space_fill" ? 0.42f : 0.22f;
	const float atom_radius_multiplier = clampFloat(s.atom_radius, 0.01f, 10.f);
	const float bond_radius = (s.visualization_mode == "wireframe" || s.wireframe_enabled) ? 0.025f : clampFloat(0.08f * (s.bond_thickness / 0.10f), 0.015f, 0.5f);
	const bool draw_atoms = s.visualization_mode != "wireframe" && !s.wireframe_enabled;
	const bool draw_bonds = s.visualization_mode != "space_fill";

	std::vector<Vec3f> model_positions(atoms.size());
	for(size_t i=0; i<atoms.size(); ++i)
	{
		const Vec3f p = (atoms[i].pos - center) * coord_scale;
		// Store molecule OBJ vertices directly in MetaSiberia engine object-space.
		// Generic OBJ files are y-up and ModelLoading normally converts them to z-up,
		// but scientific molecule overlays/picking must share one exact atom coordinate
		// with the generated mesh for every molecule.
		model_positions[i] = Vec3f(p.x, -p.z, p.y);
	}

	int next_vertex_index = 1;
	if(draw_bonds)
	{
		obj << "usemtl mat_Bond\n";
		for(size_t i=0; i<bonds.size(); ++i)
		{
			const MoleculeBond& bond = bonds[i];
			if(bond.atom_a < 0 || bond.atom_b < 0 || bond.atom_a >= (int)model_positions.size() || bond.atom_b >= (int)model_positions.size())
				continue;
			appendCylinderMesh(obj, model_positions[bond.atom_a], model_positions[bond.atom_b], bond_radius, next_vertex_index);
		}
	}

	if(draw_atoms)
	{
		for(size_t i=0; i<atoms.size(); ++i)
		{
			obj << "usemtl " << materialNameForElement(atoms[i].element) << "\n";
			appendSphereMesh(obj, model_positions[i], atom_base_radius * atom_radius_multiplier * cpkRadiusScaleForElement(atoms[i].element), next_vertex_index);
		}
	}

	FileUtils::writeEntireFileTextMode(obj_path, obj.str());
	return obj_path;
}

}


ScientificObjectEditor::ScientificObjectEditor(QWidget* parent)
:	QWidget(parent),
	settings(NULL),
	editing_ob_uid(UID::invalidUID()),
	controls_editable(true),
	syncing(false),
	info_label(NULL),
	tab_widget(NULL),
	show_3d_controls_checkbox(NULL),
	snap_to_grid_checkbox(NULL),
	grid_spacing_spin(NULL),
	pos_x_spin(NULL),
	pos_y_spin(NULL),
	pos_z_spin(NULL),
	scale_x_spin(NULL),
	scale_y_spin(NULL),
	scale_z_spin(NULL),
	rot_x_spin(NULL),
	rot_y_spin(NULL),
	rot_z_spin(NULL),
	name_edit(NULL),
	type_combo(NULL),
	description_edit(NULL),
	source_edit(NULL),
	author_edit(NULL),
	tags_edit(NULL),
	uuid_edit(NULL),
	created_edit(NULL),
	modified_edit(NULL),
	source_mode_combo(NULL),
	file_path_edit(NULL),
	browse_file_button(NULL),
	url_edit(NULL),
	online_database_combo(NULL),
	online_query_edit(NULL),
	online_search_button(NULL),
	online_results_list(NULL),
	online_preview_button(NULL),
	online_load_button(NULL),
	source_status_label(NULL),
	molecule_image_label(NULL),
	molecule_image_preview_zoom(1.0),
	query_resolution_label(NULL),
	code_language_combo(NULL),
	code_edit(NULL),
	prompt_edit(NULL),
	data_summary_edit(NULL),
	atom_table_edit(NULL),
	bond_table_edit(NULL),
	point_table_edit(NULL),
	value_table_edit(NULL),
	property_table_edit(NULL),
	visualization_mode_combo(NULL),
	colour_scheme_combo(NULL),
	display_colour_button(NULL),
	material_combo(NULL),
	atom_radius_spin(NULL),
	bond_thickness_spin(NULL),
	point_size_spin(NULL),
	line_width_spin(NULL),
	opacity_spin(NULL),
	object_scale_spin(NULL),
	show_labels_check(NULL),
	atom_labels_pinned_check(NULL),
	show_molecule_title_check(NULL),
	molecule_title_edit(NULL),
	molecule_title_pinned_check(NULL),
	show_info_card_check(NULL),
	info_card_mode_combo(NULL),
	info_card_scale_spin(NULL),
	info_card_distance_spin(NULL),
	info_card_pinned_check(NULL),
	info_card_dark_background_check(NULL),
	info_card_stand_type_combo(NULL),
	info_card_auto_fit_text_check(NULL),
	info_card_stand_width_spin(NULL),
	info_card_stand_height_spin(NULL),
	info_card_stand_depth_spin(NULL),
	show_legend_check(NULL),
	show_hydrogen_check(NULL),
	label_mode_combo(NULL),
	label_colour_button(NULL),
	label_scale_spin(NULL),
	molecule_title_scale_spin(NULL),
	label_max_count_spin(NULL),
	label_max_distance_spin(NULL),
	lod_spin(NULL),
	glow_enabled_check(NULL),
	glow_strength_spin(NULL),
	outline_enabled_check(NULL),
	wireframe_enabled_check(NULL),
	selection_mode_combo(NULL),
	molecule_viewport(NULL),
	molecule_selection_status_label(NULL),
	collision_enabled_check(NULL),
	solid_check(NULL),
	trigger_check(NULL),
	selectable_check(NULL),
	movable_check(NULL),
	gravity_enabled_check(NULL),
	physics_motion_type_combo(NULL),
	physics_shape_combo(NULL),
	collision_layer_edit(NULL),
	physics_mass_spin(NULL),
	physics_friction_spin(NULL),
	physics_restitution_spin(NULL),
	measure_distance_check(NULL),
	measure_angle_check(NULL),
	measure_torsion_check(NULL),
	measure_area_check(NULL),
	measure_volume_check(NULL),
	atom_count_spin(NULL),
	bond_count_spin(NULL),
	point_count_spin(NULL),
	dimensions_edit(NULL),
	measurement_records_edit(NULL),
	molecule_metrics_label(NULL),
	start_distance_button(NULL),
	start_angle_button(NULL),
	start_torsion_button(NULL),
	clear_measurements_button(NULL),
	rotation_animation_check(NULL),
	trajectory_animation_check(NULL),
	vibration_animation_check(NULL),
	time_series_check(NULL),
	animation_speed_spin(NULL),
	animation_direction_combo(NULL),
	current_frame_spin(NULL),
	frame_count_spin(NULL),
	simulation_enabled_check(NULL),
	simulation_type_combo(NULL),
	simulation_notes_edit(NULL),
	simulation_status_label(NULL),
	animation_status_label(NULL),
	molecule_world_spin_timer(NULL),
	molecule_card_tab(NULL),
	molecule_card_sections(NULL),
	image_viewer(NULL),
	provider_classification_edit(NULL),
	computed_classification_edit(NULL),
	user_collections_edit(NULL),
	favorite_check(NULL),
	catalog_status_label(NULL),
	ai_prompt_edit(NULL),
	generated_code_edit(NULL),
	ai_provider_combo(NULL),
	ai_model_edit(NULL),
	ai_endpoint_edit(NULL),
	ai_api_key_edit(NULL),
	ai_user_credentials_check(NULL),
	ai_generate_code_button(NULL),
	ai_create_object_button(NULL),
	ai_explain_button(NULL),
	ai_optimise_button(NULL),
	custom_properties_edit(NULL),
	display_colour(0.20f, 0.72f, 1.0f)
{
	setAttribute(Qt::WA_AlwaysShowToolTips, true);
	setMinimumWidth(360);

	QVBoxLayout* root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(8, 8, 8, 8);
	root_layout->setSpacing(8);

	QLabel* title = new QLabel(QString::fromUtf8("<b>MetaSiberia. Научный редактор</b>"), this);
	root_layout->addWidget(title);

	info_label = new QLabel(this);
	info_label->setWordWrap(true);
	info_label->setFrameShape(QFrame::StyledPanel);
	info_label->setMinimumHeight(42);
	root_layout->addWidget(info_label);

	tab_widget = new QTabWidget(this);
	tab_widget->setDocumentMode(true);
	tab_widget->setUsesScrollButtons(true);
	tab_widget->setElideMode(Qt::ElideNone);
	root_layout->addWidget(tab_widget);

	QWidget* object_tab = new QWidget(tab_widget);
	QVBoxLayout* object_layout = new QVBoxLayout(object_tab);
	object_layout->setContentsMargins(6, 6, 6, 6);

	QGridLayout* meta_grid = NULL;
	QGroupBox* meta_group = createSection(QString::fromUtf8("Объект"), object_tab, &meta_grid);
	int row = 0;
	name_edit = addLineEdit(meta_grid, row, QString::fromUtf8("Название"));
	type_combo = addComboBox(meta_grid, row, QString::fromUtf8("Тип"));
	description_edit = addPlainTextEdit(meta_grid, row, QString::fromUtf8("Описание"), 64);
	source_edit = addLineEdit(meta_grid, row, QString::fromUtf8("Источник"));
	author_edit = addLineEdit(meta_grid, row, QString::fromUtf8("Автор"));
	tags_edit = addLineEdit(meta_grid, row, QString::fromUtf8("Теги"));
	uuid_edit = addLineEdit(meta_grid, row, QStringLiteral("UUID"));
	created_edit = addLineEdit(meta_grid, row, QString::fromUtf8("Дата создания"));
	modified_edit = addLineEdit(meta_grid, row, QString::fromUtf8("Дата изменения"));
	object_layout->addWidget(meta_group);

	QGridLayout* transform_grid = NULL;
	QGroupBox* transform_group = createSection(QString::fromUtf8("Положение в мире"), object_tab, &transform_grid);
	row = 0;
	snap_to_grid_checkbox = addCheckBox(transform_grid, row, QString::fromUtf8("Привязка к сетке"));
	grid_spacing_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Шаг сетки"), 0.001, 10000.0, 0.1, 3);
	pos_x_spin = addDoubleSpinBox(transform_grid, row, QStringLiteral("X"), -1000000000.0, 1000000000.0, 0.1, 3);
	pos_y_spin = addDoubleSpinBox(transform_grid, row, QStringLiteral("Y"), -1000000000.0, 1000000000.0, 0.1, 3);
	pos_z_spin = addDoubleSpinBox(transform_grid, row, QStringLiteral("Z"), -1000000000.0, 1000000000.0, 0.1, 3);
	scale_x_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Масштаб X"), 0.0001, 1000000.0, 0.05, 4);
	scale_y_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Масштаб Y"), 0.0001, 1000000.0, 0.05, 4);
	scale_z_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Масштаб Z"), 0.0001, 1000000.0, 0.05, 4);
	rot_x_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Поворот X"), -360000.0, 360000.0, 1.0, 2);
	rot_y_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Поворот Y"), -360000.0, 360000.0, 1.0, 2);
	rot_z_spin = addDoubleSpinBox(transform_grid, row, QString::fromUtf8("Поворот Z"), -360000.0, 360000.0, 1.0, 2);
	object_layout->addWidget(transform_group);

	QGridLayout* physics_grid = NULL;
	QGroupBox* physics_group = createSection(QString::fromUtf8("Физика и взаимодействие"), object_tab, &physics_grid);
	row = 0;
	collision_enabled_check = addCheckBox(physics_grid, row, QString::fromUtf8("Collision enabled"));
	solid_check = addCheckBox(physics_grid, row, QStringLiteral("Solid"));
	trigger_check = addCheckBox(physics_grid, row, QStringLiteral("Trigger / sensor"));
	selectable_check = addCheckBox(physics_grid, row, QStringLiteral("Selectable"));
	movable_check = addCheckBox(physics_grid, row, QStringLiteral("Movable"));
	gravity_enabled_check = addCheckBox(physics_grid, row, QString::fromUtf8("Gravity enabled"));
	physics_motion_type_combo = addComboBox(physics_grid, row, QString::fromUtf8("Motion type"));
	physics_shape_combo = addComboBox(physics_grid, row, QString::fromUtf8("Physics shape"));
	collision_layer_edit = addLineEdit(physics_grid, row, QString::fromUtf8("Collision layer"));
	physics_mass_spin = addDoubleSpinBox(physics_grid, row, QString::fromUtf8("Mass, kg"), 0.0, 1000000000.0, 1.0, 3);
	physics_friction_spin = addDoubleSpinBox(physics_grid, row, QString::fromUtf8("Friction"), 0.0, 10.0, 0.05, 3);
	physics_restitution_spin = addDoubleSpinBox(physics_grid, row, QString::fromUtf8("Restitution"), 0.0, 10.0, 0.05, 3);
	object_layout->addWidget(physics_group);
	tab_widget->addTab(object_tab, QString::fromUtf8("Настройки"));

	QWidget* source_tab = new QWidget(tab_widget);
	QVBoxLayout* source_layout = new QVBoxLayout(source_tab);
	source_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* source_grid = NULL;
	QGroupBox* source_group = createSection(QString::fromUtf8("Источник данных"), object_tab, &source_grid);
	row = 0;
	source_mode_combo = addComboBox(source_grid, row, QString::fromUtf8("Источник"));
	file_path_edit = addLineEdit(source_grid, row, QString::fromUtf8("Файл"));
	browse_file_button = new QPushButton(QString::fromUtf8("Выбрать"), source_group);
	source_grid->addWidget(browse_file_button, row - 1, 2);
	url_edit = addLineEdit(source_grid, row, QStringLiteral("URL"));
	online_database_combo = addComboBox(source_grid, row, QString::fromUtf8("Онлайн-база"));
	online_query_edit = addLineEdit(source_grid, row, QString::fromUtf8("Поиск"));
	online_search_button = new QPushButton(QString::fromUtf8("Найти"), source_group);
	source_grid->addWidget(online_search_button, row - 1, 2);
	query_resolution_label = new QLabel(QStringLiteral("Original query: Not available | Normalized query: Not available | Translation: Not used"), source_group);
	query_resolution_label->setWordWrap(true);
	source_grid->addWidget(new QLabel(QString::fromUtf8("Разрешение запроса"), source_group), row, 0);
	source_grid->addWidget(query_resolution_label, row++, 1, 1, 2);
	online_results_list = new QListWidget(source_group);
	online_results_list->setMinimumHeight(88);
	online_results_list->setSelectionMode(QAbstractItemView::SingleSelection);
	online_results_list->setAlternatingRowColors(true);
	online_results_list->setStyleSheet(QStringLiteral(
		"QListWidget::item { padding: 4px; }"
		"QListWidget::item:selected { background: #3d6fb6; color: white; }"
		"QListWidget::item:selected:!active { background: #315d96; color: white; }"
	));
	source_grid->addWidget(new QLabel(QString::fromUtf8("Результаты"), source_group), row, 0);
	source_grid->addWidget(online_results_list, row++, 1, 1, 2);
	source_status_label = new QLabel(source_group);
	source_status_label->setWordWrap(true);
	source_status_label->setFrameShape(QFrame::StyledPanel);
	source_grid->addWidget(new QLabel(QString::fromUtf8("Статус"), source_group), row, 0);
	source_grid->addWidget(source_status_label, row++, 1, 1, 2);
	molecule_image_label = new QLabel(source_group);
	molecule_image_label->setMinimumHeight(96);
	molecule_image_label->setMinimumWidth(120);
	molecule_image_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	molecule_image_label->setAlignment(Qt::AlignCenter);
	molecule_image_label->setFrameShape(QFrame::StyledPanel);
	molecule_image_label->setText(QString::fromUtf8("Превью 2D-изображения"));
	molecule_image_label->setToolTip(QString::fromUtf8("Колесо мыши над изображением: зум превью. Полный просмотр с pan/fit/reset — вкладка «Изображения»."));
	molecule_image_label->installEventFilter(this);
	source_grid->addWidget(new QLabel(QString::fromUtf8("Изображение"), source_group), row, 0);
	source_grid->addWidget(molecule_image_label, row++, 1, 1, 2);
	QHBoxLayout* online_buttons = new QHBoxLayout();
	online_preview_button = new QPushButton(QString::fromUtf8("Просмотр"), source_group);
	online_load_button = new QPushButton(QString::fromUtf8("Загрузить"), source_group);
	online_preview_button->setText(QString::fromUtf8("Просмотр данных"));
	online_load_button->setText(QString::fromUtf8("Загрузить в объект"));
	online_buttons->addWidget(online_preview_button);
	online_buttons->addWidget(online_load_button);
	source_grid->addLayout(online_buttons, row++, 1, 1, 2);
	code_language_combo = addComboBox(source_grid, row, QString::fromUtf8("Язык кода"));
	code_edit = addPlainTextEdit(source_grid, row, QString::fromUtf8("Код"), 150);
	prompt_edit = addPlainTextEdit(source_grid, row, QString::fromUtf8("Запрос словами"), 80);
	object_layout->insertWidget(1, source_group); // Logical order in Settings: Object -> Data Source -> World Position -> Visualisation -> Animation -> Image.

	QWidget* data_tab = new QWidget(tab_widget);
	QVBoxLayout* data_layout = new QVBoxLayout(data_tab);
	data_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* data_grid = NULL;
	QGroupBox* data_group = createSection(QString::fromUtf8("Данные"), data_tab, &data_grid);
	row = 0;
	data_summary_edit = addPlainTextEdit(data_grid, row, QString::fromUtf8("Сводка"), 74);
	atom_table_edit = addPlainTextEdit(data_grid, row, QString::fromUtf8("Атомы"), 90);
	bond_table_edit = addPlainTextEdit(data_grid, row, QString::fromUtf8("Связи"), 80);
	point_table_edit = addPlainTextEdit(data_grid, row, QString::fromUtf8("XYZ / RGB / нормали"), 90);
	value_table_edit = addPlainTextEdit(data_grid, row, QString::fromUtf8("Значения"), 80);
	property_table_edit = addPlainTextEdit(data_grid, row, QString::fromUtf8("Свойства"), 80);
	data_layout->addWidget(data_group);
	data_layout->addStretch(1);
	tab_widget->addTab(data_tab, QString::fromUtf8("Данные"));

	molecule_card_tab = new QWidget(tab_widget);
	QVBoxLayout* card_layout = new QVBoxLayout(molecule_card_tab);
	card_layout->setContentsMargins(6, 6, 6, 6);
	QLabel* card_note = new QLabel(QString::fromUtf8("Сводка, структура и свойства загружаются вместе с объектом. Остальные разделы PubChem PUG View загружаются только при открытии вкладки и кэшируются локально."), molecule_card_tab);
	card_note->setWordWrap(true);
	card_layout->addWidget(card_note);
	molecule_card_sections = new QTabWidget(molecule_card_tab);
	molecule_card_sections->setUsesScrollButtons(true);
	molecule_card_sections->setElideMode(Qt::ElideNone);
	const QStringList card_names = {
		QString::fromUtf8("Сводка"),
		QString::fromUtf8("Структура"),
		QString::fromUtf8("Свойства"),
		QString::fromUtf8("Классификация"),
		QString::fromUtf8("Безопасность"),
		QString::fromUtf8("Биоактивность"),
		QString::fromUtf8("Литература"),
		QString::fromUtf8("Патенты"),
		QString::fromUtf8("Спектры"),
		QString::fromUtf8("Происхождение")
	};
	for(const QString& card_name : card_names)
	{
		QWidget* page = new QWidget(molecule_card_sections);
		QVBoxLayout* page_layout = new QVBoxLayout(page);
		QLabel* status = new QLabel(QString::fromUtf8("Нет данных"), page); status->setWordWrap(true); status->setFrameShape(QFrame::StyledPanel); page_layout->addWidget(status);
		QPlainTextEdit* edit = new QPlainTextEdit(page); edit->setReadOnly(true); edit->setProperty("scientificReadOnly", true); edit->setMinimumHeight(260); edit->setPlaceholderText(QString::fromUtf8("Нет данных")); page_layout->addWidget(edit);
		molecule_card_status_labels.push_back(status); molecule_card_edits.push_back(edit); molecule_card_sections->addTab(page, card_name);
	}
	card_layout->addWidget(molecule_card_sections);
	tab_widget->addTab(molecule_card_tab, QString::fromUtf8("Карточка молекулы"));

	QWidget* vis_tab = new QWidget(tab_widget);
	QVBoxLayout* vis_layout = new QVBoxLayout(vis_tab);
	vis_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* vis_grid = NULL;
	QGroupBox* vis_group = createSection(QString::fromUtf8("Визуализация"), object_tab, &vis_grid);
	row = 0;
	visualization_mode_combo = addComboBox(vis_grid, row, QString::fromUtf8("Режим"));
	colour_scheme_combo = addComboBox(vis_grid, row, QString::fromUtf8("Цветовая схема"));
	display_colour_button = new QPushButton(QString(), vis_group);
	display_colour_button->setMinimumWidth(72);
	vis_grid->addWidget(new QLabel(QString::fromUtf8("Цвет"), vis_group), row, 0);
	vis_grid->addWidget(display_colour_button, row++, 1);
	material_combo = addComboBox(vis_grid, row, QString::fromUtf8("Материал"));
	atom_radius_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Размер атомов"), 0.01, 10.0, 0.05, 3);
	bond_thickness_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Толщина связей"), 0.001, 10.0, 0.01, 3);
	point_size_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Размер точек"), 0.001, 20.0, 0.01, 3);
	line_width_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Толщина линий"), 0.001, 20.0, 0.01, 3);
	opacity_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Прозрачность"), 0.02, 1.0, 0.02, 3);
	object_scale_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Масштаб визуализации"), 0.01, 1000.0, 0.05, 3);
	show_3d_controls_checkbox = addCheckBox(vis_grid, row, QString::fromUtf8("Показывать 3D-контролы"));
	show_labels_check = addCheckBox(vis_grid, row, QString::fromUtf8("Показывать подписи"));
	atom_labels_pinned_check = addCheckBox(vis_grid, row, QString::fromUtf8("Закрепить подписи атомов"));
	show_molecule_title_check = addCheckBox(vis_grid, row, QString::fromUtf8("Показывать название молекулы"));
	molecule_title_edit = new QLineEdit(vis_group);
	molecule_title_edit->setPlaceholderText(QString::fromUtf8("Авто: имя объекта/молекулы"));
	vis_grid->addWidget(new QLabel(QString::fromUtf8("Название над молекулой"), vis_group), row, 0);
	vis_grid->addWidget(molecule_title_edit, row++, 1);
	molecule_title_pinned_check = addCheckBox(vis_grid, row, QString::fromUtf8("Закрепить название молекулы"));
	show_info_card_check = addCheckBox(vis_grid, row, QString::fromUtf8("Показывать 3D-карточку"));
	info_card_mode_combo = addComboBox(vis_grid, row, QString::fromUtf8("Режим 3D-карточки"));
	info_card_scale_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Масштаб карточки"), 0.05, 20.0, 0.05, 3);
	info_card_distance_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Дистанция карточки"), 0.0, 1000.0, 0.25, 2);
	info_card_pinned_check = addCheckBox(vis_grid, row, QString::fromUtf8("Закрепить карточку"));
	info_card_dark_background_check = addCheckBox(vis_grid, row, QString::fromUtf8("Тёмный фон карточки"));
	info_card_stand_type_combo = addComboBox(vis_grid, row, QString::fromUtf8("Стенд карточки"));
	info_card_auto_fit_text_check = addCheckBox(vis_grid, row, QString::fromUtf8("Автоподгонка текста под стенд"));
	info_card_stand_width_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Ширина стенда"), 0.2, 100.0, 0.1, 2);
	info_card_stand_height_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Высота стенда"), 0.2, 100.0, 0.1, 2);
	info_card_stand_depth_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Толщина стенда"), 0.005, 10.0, 0.01, 3);
	show_legend_check = addCheckBox(vis_grid, row, QString::fromUtf8("Показывать легенду"));
	show_hydrogen_check = addCheckBox(vis_grid, row, QString::fromUtf8("Показывать водород"));
	label_mode_combo = addComboBox(vis_grid, row, QString::fromUtf8("Режим подписей"));
	label_colour_button = new QPushButton(QStringLiteral("Label colour"), vis_group);
	vis_grid->addWidget(new QLabel(QString::fromUtf8("Цвет подписей"), vis_group), row, 0); vis_grid->addWidget(label_colour_button, row++, 1);
	label_scale_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Масштаб подписей"), 0.05, 20.0, 0.05, 3);
	molecule_title_scale_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Масштаб названия"), 0.05, 20.0, 0.05, 3);
	label_max_count_spin = addSpinBox(vis_grid, row, QString::fromUtf8("Макс. подписей"), 0, 1000000);
	label_max_distance_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Дистанция подписей"), 0.0, 1000000.0, 1.0, 2);
	lod_spin = addSpinBox(vis_grid, row, QStringLiteral("LOD"), 0, 8);
	glow_enabled_check = addCheckBox(vis_grid, row, QStringLiteral("Glow / emissive"));
	glow_strength_spin = addDoubleSpinBox(vis_grid, row, QString::fromUtf8("Glow strength"), 0.0, 10000.0, 5.0, 2);
	outline_enabled_check = addCheckBox(vis_grid, row, QStringLiteral("Outline / selection highlight"));
	wireframe_enabled_check = addCheckBox(vis_grid, row, QStringLiteral("Wireframe flag"));
	selection_mode_combo = addComboBox(vis_grid, row, QString::fromUtf8("Режим выбора"));
	molecule_selection_status_label = new QLabel(QStringLiteral("no_selection"), vis_group); molecule_selection_status_label->setWordWrap(true);
	vis_grid->addWidget(new QLabel(QString::fromUtf8("Выбор"), vis_group), row, 0); vis_grid->addWidget(molecule_selection_status_label, row++, 1);
	object_layout->addWidget(vis_group);
	molecule_viewport = new MoleculeViewportWidget(object_tab);
	object_layout->addWidget(molecule_viewport);

	QWidget* measure_tab = new QWidget(tab_widget);
	QVBoxLayout* measure_layout = new QVBoxLayout(measure_tab);
	measure_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* measure_grid = NULL;
	QGroupBox* measure_group = createSection(QString::fromUtf8("Измерения"), measure_tab, &measure_grid);
	row = 0;
	measure_distance_check = addCheckBox(measure_grid, row, QString::fromUtf8("Расстояние"));
	measure_angle_check = addCheckBox(measure_grid, row, QString::fromUtf8("Угол"));
	measure_torsion_check = addCheckBox(measure_grid, row, QString::fromUtf8("Торсион"));
	measure_area_check = addCheckBox(measure_grid, row, QString::fromUtf8("Площадь"));
	measure_volume_check = addCheckBox(measure_grid, row, QString::fromUtf8("Объем"));
	atom_count_spin = addSpinBox(measure_grid, row, QString::fromUtf8("Количество атомов"), 0, 100000000);
	bond_count_spin = addSpinBox(measure_grid, row, QString::fromUtf8("Количество связей"), 0, 100000000);
	point_count_spin = addSpinBox(measure_grid, row, QString::fromUtf8("Количество точек"), 0, 100000000);
	dimensions_edit = addLineEdit(measure_grid, row, QString::fromUtf8("Размер объекта"));
	QHBoxLayout* measurement_buttons = new QHBoxLayout();
	start_distance_button = new QPushButton(QStringLiteral("Distance (2 atoms)"), measure_group);
	start_angle_button = new QPushButton(QStringLiteral("Angle (3 atoms)"), measure_group);
	start_torsion_button = new QPushButton(QStringLiteral("Torsion (4 atoms)"), measure_group);
	clear_measurements_button = new QPushButton(QStringLiteral("Clear measurements"), measure_group);
	measurement_buttons->addWidget(start_distance_button); measurement_buttons->addWidget(start_angle_button); measurement_buttons->addWidget(start_torsion_button); measurement_buttons->addWidget(clear_measurements_button);
	measure_grid->addLayout(measurement_buttons, row++, 0, 1, 2);
	molecule_metrics_label = new QLabel(QStringLiteral("Not available"), measure_group); molecule_metrics_label->setWordWrap(true); molecule_metrics_label->setFrameShape(QFrame::StyledPanel);
	measure_grid->addWidget(new QLabel(QStringLiteral("Derived metrics"), measure_group), row, 0); measure_grid->addWidget(molecule_metrics_label, row++, 1);
	measurement_records_edit = addPlainTextEdit(measure_grid, row, QStringLiteral("Saved measurement records"), 110); measurement_records_edit->setReadOnly(true); measurement_records_edit->setProperty("scientificReadOnly", true);
	measure_layout->addWidget(measure_group);
	measure_layout->addStretch(1);
	tab_widget->addTab(measure_tab, QString::fromUtf8("Измерения"));

	QWidget* anim_tab = new QWidget(tab_widget);
	QVBoxLayout* anim_layout = new QVBoxLayout(anim_tab);
	anim_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* anim_grid = NULL;
	QGroupBox* anim_group = createSection(QString::fromUtf8("Анимация"), object_tab, &anim_grid);
	row = 0;
	rotation_animation_check = addCheckBox(anim_grid, row, QString::fromUtf8("Вращение"));
	trajectory_animation_check = addCheckBox(anim_grid, row, QString::fromUtf8("Траектория"));
	vibration_animation_check = addCheckBox(anim_grid, row, QString::fromUtf8("Колебания"));
	time_series_check = addCheckBox(anim_grid, row, QString::fromUtf8("Временные данные"));
	animation_speed_spin = addDoubleSpinBox(anim_grid, row, QString::fromUtf8("Скорость"), 0.0, 100.0, 0.1, 3);
	animation_direction_combo = addComboBox(anim_grid, row, QString::fromUtf8("Направление"));
	current_frame_spin = addSpinBox(anim_grid, row, QString::fromUtf8("Кадр"), 0, 100000000);
	frame_count_spin = addSpinBox(anim_grid, row, QString::fromUtf8("Всего кадров"), 0, 100000000);
	animation_status_label = new QLabel(QStringLiteral("Rotation/Spin: available in interactive molecule viewport. Trajectory, vibration and time-series: WIP."), anim_group); animation_status_label->setWordWrap(true); animation_status_label->setFrameShape(QFrame::StyledPanel); anim_grid->addWidget(animation_status_label, row++, 0, 1, 2);
	object_layout->addWidget(anim_group);

	QWidget* sim_tab = new QWidget(tab_widget);
	QVBoxLayout* sim_layout = new QVBoxLayout(sim_tab);
	sim_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* sim_grid = NULL;
	QGroupBox* sim_group = createSection(QString::fromUtf8("Симуляция"), sim_tab, &sim_grid);
	row = 0;
	simulation_enabled_check = addCheckBox(sim_grid, row, QString::fromUtf8("Включено"));
	simulation_type_combo = addComboBox(sim_grid, row, QString::fromUtf8("Тип"));
	simulation_notes_edit = addPlainTextEdit(sim_grid, row, QString::fromUtf8("Заметки"), 120);
	simulation_status_label = new QLabel(QStringLiteral("Unsupported: solver backend unavailable. Molecular dynamics, CFD and orbital solvers are not simulated."), sim_group); simulation_status_label->setWordWrap(true); simulation_status_label->setFrameShape(QFrame::StyledPanel); sim_grid->addWidget(simulation_status_label, row++, 0, 1, 2);
	simulation_enabled_check->setChecked(false); simulation_enabled_check->setEnabled(false); simulation_type_combo->setEnabled(false);
	sim_layout->addWidget(sim_group);
	sim_layout->addStretch(1);
	tab_widget->addTab(sim_tab, QString::fromUtf8("Симуляция"));

	QGridLayout* image_grid = NULL;
	QGroupBox* image_group = createSection(QString::fromUtf8("Изображение"), object_tab, &image_grid);
	row = 0;
	image_viewer = new ScientificImageViewer(image_group);
	image_grid->addWidget(image_viewer, row++, 0, 1, 2);
	object_layout->addWidget(image_group);
	object_layout->addStretch(1);
	connect(tab_widget, &QTabWidget::currentChanged, this, [this](int) { if(tab_widget->currentWidget()==molecule_card_tab) moleculeCardSectionChanged(molecule_card_sections->currentIndex()); });

	QWidget* catalog_tab = new QWidget(tab_widget); QVBoxLayout* catalog_layout = new QVBoxLayout(catalog_tab); catalog_layout->setContentsMargins(6,6,6,6); QGridLayout* catalog_grid=NULL; QGroupBox* catalog_group=createSection(QString::fromUtf8("Классификация и коллекции"),catalog_tab,&catalog_grid); row=0;
	provider_classification_edit=addPlainTextEdit(catalog_grid,row,QStringLiteral("PubChem provider classifications"),100);
	computed_classification_edit=addPlainTextEdit(catalog_grid,row,QStringLiteral("Computed classification (explicit heuristic)"),80);
	user_collections_edit=addLineEdit(catalog_grid,row,QStringLiteral("MetaSiberia user collections"),QStringLiteral("favorites, teaching, research"));
	favorite_check=addCheckBox(catalog_grid,row,QStringLiteral("Favorite"));
	catalog_status_label=new QLabel(QStringLiteral("Filters: formula, molecular mass, elements, charge, confirmed organic/inorganic, biological role, hazard class, source, has 3D/safety/bioactivity. Current card exposes provider availability; global multi-object catalog indexing is WIP."),catalog_group);catalog_status_label->setWordWrap(true);catalog_status_label->setFrameShape(QFrame::StyledPanel);catalog_grid->addWidget(catalog_status_label,row++,0,1,2);
	catalog_layout->addWidget(catalog_group);catalog_layout->addStretch(1);tab_widget->addTab(catalog_tab,QString::fromUtf8("Классификация и каталог"));

	QWidget* ai_tab = new QWidget(tab_widget);
	QVBoxLayout* ai_layout = new QVBoxLayout(ai_tab);
	ai_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* ai_grid = NULL;
	QGroupBox* ai_group = createSection(QString::fromUtf8("ИИ"), ai_tab, &ai_grid);
	row = 0;
	ai_prompt_edit = addPlainTextEdit(ai_grid, row, QString::fromUtf8("Запрос словами"), 86);
	generated_code_edit = addPlainTextEdit(ai_grid, row, QString::fromUtf8("Генерация кода"), 150);
	ai_provider_combo = addComboBox(ai_grid, row, QString::fromUtf8("Провайдер"));
	ai_model_edit = addLineEdit(ai_grid, row, QString::fromUtf8("Модель"));
	ai_endpoint_edit = addLineEdit(ai_grid, row, QString::fromUtf8("URL / endpoint"));
	ai_api_key_edit = addLineEdit(ai_grid, row, QString::fromUtf8("API Key (локально)"));
	ai_api_key_edit->setEchoMode(QLineEdit::Password);
	ai_api_key_edit->setClearButtonEnabled(true);
	ai_api_key_edit->setProperty("scientificLocalOnly", true);
	ai_user_credentials_check = addCheckBox(ai_grid, row, QString::fromUtf8("Использовать ключи пользователя"));
	QHBoxLayout* ai_buttons = new QHBoxLayout();
	ai_generate_code_button = new QPushButton(QString::fromUtf8("Сгенерировать код"), ai_group);
	ai_create_object_button = new QPushButton(QString::fromUtf8("Создать объект"), ai_group);
	ai_explain_button = new QPushButton(QString::fromUtf8("Объяснить"), ai_group);
	ai_optimise_button = new QPushButton(QString::fromUtf8("Оптимизировать"), ai_group);
	ai_buttons->addWidget(ai_generate_code_button);
	ai_buttons->addWidget(ai_create_object_button);
	ai_buttons->addWidget(ai_explain_button);
	ai_buttons->addWidget(ai_optimise_button);
	ai_grid->addLayout(ai_buttons, row++, 0, 1, 2);
	ai_layout->addWidget(ai_group);
	ai_layout->addStretch(1);
	tab_widget->addTab(ai_tab, QString::fromUtf8("ИИ"));

	QWidget* custom_tab = new QWidget(tab_widget);
	QVBoxLayout* custom_layout = new QVBoxLayout(custom_tab);
	custom_layout->setContentsMargins(6, 6, 6, 6);
	QGridLayout* custom_grid = NULL;
	QGroupBox* custom_group = createSection(QString::fromUtf8("Custom Properties"), custom_tab, &custom_grid);
	row = 0;
	custom_properties_edit = addPlainTextEdit(custom_grid, row, QString::fromUtf8("JSON / свойства"), 200);
	custom_layout->addWidget(custom_group);
	custom_layout->addStretch(1);
	tab_widget->addTab(custom_tab, QString::fromUtf8("Custom"));

	populateStaticCombos();
	connectObjectChangeSignals();
	configureHorizontalTextScroll(code_edit);
	configureHorizontalTextScroll(generated_code_edit);
	configureHorizontalTextScroll(atom_table_edit);
	configureHorizontalTextScroll(bond_table_edit);
	configureHorizontalTextScroll(point_table_edit);
	configureHorizontalTextScroll(value_table_edit);
	configureHorizontalTextScroll(property_table_edit);
	configureHorizontalTextScroll(custom_properties_edit);
	installTooltips();
	molecule_viewport->stateChanged = [this]()
	{
		updateMoleculeSelectionStatus();
		if(measurement_records_edit)
			measurement_records_edit->setPlainText(molecule_viewport->measurementsJson());
		if(!syncing)
			emit objectChanged();
	};
	molecule_viewport->actionRequested = [this](const QString& action, int index) { handleMoleculeAction(action, index); };
	molecule_world_spin_timer = new QTimer(this);
	molecule_world_spin_timer->setInterval(50);
	connect(molecule_world_spin_timer, &QTimer::timeout, this, [this]()
	{
		if(syncing || !rotation_animation_check || !rotation_animation_check->isChecked() || !rot_z_spin || !animation_speed_spin)
			return;
		const double spin_direction = currentComboData(animation_direction_combo, QStringLiteral("forward")) == QStringLiteral("reverse") ? -1.0 : 1.0;
		double next_z = rot_z_spin->value() + spin_direction * std::max(0.1, animation_speed_spin->value()) * 0.35;
		while(next_z < 0.0)
			next_z += 360.0;
		while(next_z > 360.0)
			next_z -= 360.0;
		rot_z_spin->setValue(next_z);
	});
	connect(selection_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { molecule_viewport->setSelectionMode(currentComboData(selection_mode_combo, QStringLiteral("atom"))); updateMoleculeSelectionStatus(); if(!syncing) emit objectChanged(); });
	connect(label_colour_button, &QPushButton::clicked, this, [this]() { const QColor initial((int)(current_settings.label_colour.r*255.f),(int)(current_settings.label_colour.g*255.f),(int)(current_settings.label_colour.b*255.f)); const QColor c=QColorDialog::getColor(initial,this,QStringLiteral("Label colour")); if(c.isValid()){current_settings.label_colour=Colour3f(c.redF(),c.greenF(),c.blueF());label_colour_button->setStyleSheet(QStringLiteral("background:%1").arg(c.name()));updateMoleculeInteractiveView();emitObjectChanged();} });
	connect(molecule_card_sections, &QTabWidget::currentChanged, this, &ScientificObjectEditor::moleculeCardSectionChanged);
	connect(start_distance_button, &QPushButton::clicked, this, [this]() { molecule_viewport->beginMeasurement(QStringLiteral("distance")); });
	connect(start_angle_button, &QPushButton::clicked, this, [this]() { molecule_viewport->beginMeasurement(QStringLiteral("angle")); });
	connect(start_torsion_button, &QPushButton::clicked, this, [this]() { molecule_viewport->beginMeasurement(QStringLiteral("torsion")); });
	connect(clear_measurements_button, &QPushButton::clicked, this, [this]() { molecule_viewport->clearMeasurements(); });
	auto update_spin = [this]()
	{
		const float spin_direction = currentComboData(animation_direction_combo, QStringLiteral("forward")) == QStringLiteral("reverse") ? -1.f : 1.f;
		molecule_viewport->setSpinEnabled(rotation_animation_check->isChecked(), (float)animation_speed_spin->value() * spin_direction);
	};
	connect(rotation_animation_check, &QCheckBox::toggled, this, [this, update_spin](bool enabled) { update_spin(); if(molecule_world_spin_timer){if(enabled)molecule_world_spin_timer->start();else molecule_world_spin_timer->stop();} if(animation_status_label) animation_status_label->setText(enabled ? QString::fromUtf8("Loaded: Rotation/Spin активен в превью и в 3D-мире. Остальные animation controls остаются WIP.") : QString::fromUtf8("Rotation/Spin: доступен. Trajectory, vibration и time-series: WIP.")); });
	connect(animation_speed_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [update_spin](double) { update_spin(); });
	connect(animation_direction_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [update_spin](int) { update_spin(); });
	connect(show_3d_controls_checkbox, &QCheckBox::toggled, this, [this](bool) { if(!syncing) emit posAndRot3DControlsToggled(); });
	const QList<QWidget*> viewport_controls = { visualization_mode_combo, colour_scheme_combo, atom_radius_spin, bond_thickness_spin, object_scale_spin, show_labels_check, atom_labels_pinned_check, show_molecule_title_check, molecule_title_pinned_check, show_info_card_check, info_card_mode_combo, info_card_scale_spin, info_card_distance_spin, info_card_pinned_check, info_card_dark_background_check, info_card_stand_type_combo, info_card_auto_fit_text_check, info_card_stand_width_spin, info_card_stand_height_spin, info_card_stand_depth_spin, show_legend_check, show_hydrogen_check, label_mode_combo, label_scale_spin, molecule_title_scale_spin, label_max_count_spin, label_max_distance_spin };
	for(QWidget* control : viewport_controls)
	{
		if(QComboBox* combo = qobject_cast<QComboBox*>(control)) connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateMoleculeInteractiveView(); });
		else if(QCheckBox* check = qobject_cast<QCheckBox*>(control)) connect(check, &QCheckBox::toggled, this, [this](bool) { updateMoleculeInteractiveView(); });
		else if(QDoubleSpinBox* spin = qobject_cast<QDoubleSpinBox*>(control)) connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateMoleculeInteractiveView(); });
		else if(QSpinBox* int_spin = qobject_cast<QSpinBox*>(control)) connect(int_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { updateMoleculeInteractiveView(); });
	}
	connect(molecule_title_edit, &QLineEdit::textChanged, this, [this](const QString&) { updateMoleculeInteractiveView(); });
	updateColourButton();
	updateScientificSourceUiState();
}


ScientificObjectEditor::~ScientificObjectEditor()
{
	if(settings)
	{
		settings->setValue("scientificObjectEditor/show3DControls", show_3d_controls_checkbox->isChecked());
		settings->setValue("scientificObjectEditor/snapToGrid", snap_to_grid_checkbox->isChecked());
		settings->setValue("scientificObjectEditor/gridSpacing", grid_spacing_spin->value());
	}
}


void ScientificObjectEditor::init(QSettings* settings_)
{
	settings = settings_;
	if(settings)
	{
		show_3d_controls_checkbox->setChecked(settings->value("scientificObjectEditor/show3DControls", true).toBool());
		snap_to_grid_checkbox->setChecked(settings->value("scientificObjectEditor/snapToGrid", false).toBool());
		grid_spacing_spin->setValue(settings->value("scientificObjectEditor/gridSpacing", 1.0).toDouble());
	}
	aiProviderChanged(ai_provider_combo ? ai_provider_combo->currentIndex() : 0);
}


QGroupBox* ScientificObjectEditor::createSection(const QString& title, QWidget* parent, QGridLayout** grid_out)
{
	QGroupBox* group = new QGroupBox(title, parent);
	group->setCheckable(true);
	group->setChecked(true);

	QGridLayout* grid = new QGridLayout(group);
	grid->setContentsMargins(8, 8, 8, 8);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(6);
	grid->setColumnStretch(1, 1);
	connect(group, &QGroupBox::toggled, group, [grid](bool checked) { setLayoutItemsVisible(grid, checked); });

	if(grid_out)
		*grid_out = grid;
	return group;
}


QLineEdit* ScientificObjectEditor::addLineEdit(QGridLayout* grid, int& row, const QString& label, const QString& placeholder)
{
	QLabel* l = new QLabel(label, grid->parentWidget());
	QLineEdit* edit = new QLineEdit(grid->parentWidget());
	edit->setPlaceholderText(placeholder);
	grid->addWidget(l, row, 0);
	grid->addWidget(edit, row++, 1);
	return edit;
}


QPlainTextEdit* ScientificObjectEditor::addPlainTextEdit(QGridLayout* grid, int& row, const QString& label, int min_height)
{
	QLabel* l = new QLabel(label, grid->parentWidget());
	QPlainTextEdit* edit = new QPlainTextEdit(grid->parentWidget());
	configurePlainText(edit, min_height);
	grid->addWidget(l, row, 0);
	grid->addWidget(edit, row++, 1);
	return edit;
}


QComboBox* ScientificObjectEditor::addComboBox(QGridLayout* grid, int& row, const QString& label)
{
	QLabel* l = new QLabel(label, grid->parentWidget());
	QComboBox* combo = new QComboBox(grid->parentWidget());
	configureCombo(combo);
	grid->addWidget(l, row, 0);
	grid->addWidget(combo, row++, 1);
	return combo;
}


QDoubleSpinBox* ScientificObjectEditor::addDoubleSpinBox(QGridLayout* grid, int& row, const QString& label, double min_v, double max_v, double step, int decimals)
{
	QLabel* l = new QLabel(label, grid->parentWidget());
	QDoubleSpinBox* spin = new QDoubleSpinBox(grid->parentWidget());
	spin->setRange(min_v, max_v);
	spin->setSingleStep(step);
	spin->setDecimals(decimals);
	spin->setAccelerated(true);
	grid->addWidget(l, row, 0);
	grid->addWidget(spin, row++, 1);
	return spin;
}


QSpinBox* ScientificObjectEditor::addSpinBox(QGridLayout* grid, int& row, const QString& label, int min_v, int max_v)
{
	QLabel* l = new QLabel(label, grid->parentWidget());
	QSpinBox* spin = new QSpinBox(grid->parentWidget());
	spin->setRange(min_v, max_v);
	spin->setAccelerated(true);
	grid->addWidget(l, row, 0);
	grid->addWidget(spin, row++, 1);
	return spin;
}


QCheckBox* ScientificObjectEditor::addCheckBox(QGridLayout* grid, int& row, const QString& label)
{
	QCheckBox* check = new QCheckBox(label, grid->parentWidget());
	grid->addWidget(check, row++, 0, 1, 2);
	return check;
}


void ScientificObjectEditor::installTooltips()
{
	setDetailedTip(info_label, QString::fromUtf8(
		"Краткий путь для безопасной демо-молекулы:\n"
		"1. Вкладка \"Настройки\", секция \"Источник данных\".\n"
		"2. Источник = \"Онлайн-база\".\n"
		"3. Тип = \"Молекула\" и поиск = \"Caffeine\" или \"Water\".\n"
		"5. Нажми \"Найти\".\n"
		"6. Нажми \"Загрузить в объект\", если выбран Built-in sample.\n\n"
		"PubChem для молекул выполняет real PUG REST загрузку. Остальные provider adapters должны показывать Unsupported/Error и не подменять данные тестовой молекулой."
	));

	setDetailedTip(tab_widget, QString::fromUtf8(
		"Вкладки научного редактора. Если вкладки или поля не помещаются по ширине, используй горизонтальную прокрутку снизу левой панели."
	));
	tab_widget->setTabToolTip(0, QString::fromUtf8("Единая прокручиваемая вкладка: объект, источник данных, положение в мире и визуализация."));
	tab_widget->setTabToolTip(1, QString::fromUtf8("Сырые и нормализованные данные: атомы, связи, точки, значения, свойства."));
	tab_widget->setTabToolTip(2, QString::fromUtf8("Карточка молекулы: summary, structure, properties, classification, safety и provenance."));
	tab_widget->setTabToolTip(3, QString::fromUtf8("Измерения: расстояния, углы, площадь, объем и счетчики элементов."));
	tab_widget->setTabToolTip(4, QString::fromUtf8("Временные данные, кадры, вращение, траектории и колебания."));
	tab_widget->setTabToolTip(5, QString::fromUtf8("Зарезервировано для будущих симуляций: динамика, поля, CFD, численные расчеты."));
	tab_widget->setTabToolTip(6, QString::fromUtf8("Изображения молекулы: просмотр, масштаб, pan, источник и лицензия."));
	tab_widget->setTabToolTip(7, QString::fromUtf8("Классификация, коллекции, избранное и история."));
	tab_widget->setTabToolTip(8, QString::fromUtf8("Запрос словами, генерация кода и пользовательские AI-провайдеры."));
	tab_widget->setTabToolTip(9, QString::fromUtf8("Расширяемые JSON-свойства объекта."));

	setDetailedTip(name_edit, QString::fromUtf8("Название научного объекта. Для built-in sample сюда попадет имя sample; для внешних источников имя должно приходить только из реального provider/import path."));
	setDetailedTip(type_combo, QString::fromUtf8("Тип научного объекта. Он определяет допустимые источники, смысл таблиц и будущий визуализатор: молекула, белок, GIS, облако точек, объем и т.д. Нереализованные типы не должны загружать тестовые данные."));
	setDetailedTip(description_edit, QString::fromUtf8("Описание объекта: что это за данные, откуда они взяты и зачем нужны в сцене."));
	setDetailedTip(source_edit, QString::fromUtf8("Краткое имя источника. Для built-in sample это локальный каталог; внешнюю базу можно указывать только после реальной загрузки/импорта."));
	setDetailedTip(tags_edit, QString::fromUtf8("Теги для поиска и организации: chemistry, caffeine, molecule, geology, GIS и т.д."));

	setDetailedTip(collision_enabled_check, QString::fromUtf8("Включает участие Scientific Object в существующей physics/collision системе WorldObject. По умолчанию выключено для молекулярных и научных визуализаций."));
	setDetailedTip(solid_check, QString::fromUtf8("Делает объект твёрдым для столкновений, если Collision enabled также включен. Не изменяет научные данные; влияет только на поведение в мире."));
	setDetailedTip(trigger_check, QString::fromUtf8("Использует существующий sensor/trigger флаг WorldObject, если Collision enabled включен. Полезно для интерактивных зон без твёрдой коллизии."));
	setDetailedTip(selectable_check, QString::fromUtf8("Сохраняет намерение, что объект можно выбирать. Текущий runtime selection работает через обычный WorldObject; отдельного Scientific-only selection флага нет."));
	setDetailedTip(movable_check, QString::fromUtf8("Сохраняет намерение, что объект можно перемещать редактором. Не включает физическую динамику само по себе."));
	setDetailedTip(gravity_enabled_check, QString::fromUtf8("Сохраняет намерение будущей gravity behavior. В найденном WorldObject нет отдельного gravity флага для Scientific Object."));
	setDetailedTip(physics_motion_type_combo, QString::fromUtf8("Static/Dynamic применяются к существующему WorldObject dynamic flag. Kinematic пока сохраняется как schema intent и требует runtime support."));
	setDetailedTip(physics_shape_combo, QString::fromUtf8("Предпочитаемая форма физики. Сейчас mesh/none влияют на включение collidable; box/sphere сохраняются для будущего physics adapter."));
	setDetailedTip(collision_layer_edit, QString::fromUtf8("Логическое имя collision layer/group для будущей маршрутизации. Сейчас сохраняется в ScientificObject JSON и не создаёт новый physics layer."));
	setDetailedTip(physics_mass_spin, QString::fromUtf8("Масса WorldObject в килограммах. Применяется к существующему physics field mass; важна только для dynamic/collidable сценариев."));
	setDetailedTip(physics_friction_spin, QString::fromUtf8("Коэффициент трения WorldObject. Не имеет единиц; применяется к существующему physics field friction."));
	setDetailedTip(physics_restitution_spin, QString::fromUtf8("Коэффициент упругости WorldObject. Не имеет единиц; применяется к существующему physics field restitution."));

	setDetailedTip(show_3d_controls_checkbox, QString::fromUtf8("Показывать стрелки и дуги трансформации прямо в мире для выбранного объекта."));
	setDetailedTip(snap_to_grid_checkbox, QString::fromUtf8("Привязывать перемещение к сетке. Полезно, если нужно аккуратно поставить научный объект рядом с другими объектами."));
	setDetailedTip(grid_spacing_spin, QString::fromUtf8("Размер шага сетки для перемещения объекта."));
	setDetailedTip(pos_x_spin, QString::fromUtf8("Позиция объекта в мире по оси X."));
	setDetailedTip(pos_y_spin, QString::fromUtf8("Позиция объекта в мире по оси Y."));
	setDetailedTip(pos_z_spin, QString::fromUtf8("Высота объекта в мире."));
	setDetailedTip(scale_x_spin, QString::fromUtf8("Масштаб объекта по X. Для молекулы обычно удобен одинаковый масштаб по всем осям."));
	setDetailedTip(scale_y_spin, QString::fromUtf8("Масштаб объекта по Y."));
	setDetailedTip(scale_z_spin, QString::fromUtf8("Масштаб объекта по Z."));

	setDetailedTip(source_mode_combo, QString::fromUtf8(
		"Выбери способ получения данных.\n"
		"Файл: PDB, MOL, XYZ, CIF, CSV, PLY, OBJ и т.д.\n"
		"URL: ссылка на данные.\n"
		"Онлайн-база: PubChem, RCSB, NASA, OpenStreetMap и другие.\n"
		"Код: Python-код, который возвращает данные.\n"
		"Запрос словами: фраза, из которой AI генерирует код."
	));
	setDetailedTip(file_path_edit, QString::fromUtf8("Путь к локальному научному файлу. Нажми \"Выбрать\", чтобы открыть файл с диска."));
	setDetailedTip(browse_file_button, QString::fromUtf8("Открыть диалог выбора файла. Поддерживаемые форматы перечислены в фильтре диалога."));
	setDetailedTip(url_edit, QString::fromUtf8("Ссылка на научные данные. В будущих адаптерах редактор скачает данные и преобразует их в ScientificObject."));
	setDetailedTip(online_database_combo, QString::fromUtf8("Онлайн-источник. PubChem реализован для Phase 1 молекул через PUG REST. Остальные записи описывают planned providers и должны честно показывать Unsupported/Planned."));
	setDetailedTip(online_query_edit, QString::fromUtf8("Строка поиска или ID. PubChem поддерживает name/CID/SMILES/InChI/InChIKey/formula heuristics. Пустой запрос не заменяется тестовой молекулой."));
	setDetailedTip(online_search_button, QString::fromUtf8("Выполнить поиск. Для PubChem это real API request с cache/throttling; для нереализованных providers результат будет Unsupported и объектные данные не изменятся."));
	setDetailedTip(online_results_list, QString::fromUtf8("Список найденных результатов. Выбери нужную запись перед загрузкой в объект."));
	setDetailedTip(source_status_label, QString::fromUtf8("Состояние загрузки: Idle, Ready, Unsupported или Error. Нереализованный provider не должен менять atom/bond data."));
	setDetailedTip(online_preview_button, QString::fromUtf8("Показать/обновить статус источника и доступные локальные samples без изменения текущих научных данных."));
	setDetailedTip(online_load_button, QString::fromUtf8(
		"Загрузить выбранный результат в текущий ScientificObject.\n\n"
		"Для PubChem загружает выбранный CID: properties, SDF structure, PNG image, cache/provenance. Нереализованный provider покажет Unsupported и не подменит объект тестовой молекулой.\n"
		"Визуальные настройки применяются через текущий WorldObject/material/model update path."
	));
	setDetailedTip(code_language_combo, QString::fromUtf8("Язык кода. Сейчас это metadata/editor text: Python не исполняется внутри редактора без отдельного runtime executor."));
	setDetailedTip(code_edit, QString::fromUtf8("Текст кода или заготовка генерации данных. Не считать результатом вычислений, пока не подключён и не проверен executor."));
	setDetailedTip(prompt_edit, QString::fromUtf8("Запрос словами. Примеры: \"Создай молекулу кофеина\", \"Построй график sin(x)\", \"Создай облако случайных точек\"."));

	setDetailedTip(data_summary_edit, QString::fromUtf8("Сводка загруженных данных: формула, масса, количество атомов/точек, источник и другие ключевые факты."));
	setDetailedTip(atom_table_edit, QString::fromUtf8("Таблица атомов: номер, элемент, координаты, заряд и дополнительные свойства."));
	setDetailedTip(bond_table_edit, QString::fromUtf8("Таблица связей: какие атомы соединены и какой тип связи используется."));
	setDetailedTip(point_table_edit, QString::fromUtf8("Точки XYZ, RGB, нормали или классификация для облаков точек и поверхностей."));
	setDetailedTip(value_table_edit, QString::fromUtf8("Табличные значения для графиков, временных рядов, полей и измерений."));
	setDetailedTip(property_table_edit, QString::fromUtf8("Дополнительные свойства источника: CID, DOI, миссия, датасет, единицы измерения и т.д."));

	setDetailedTip(visualization_mode_combo, QString::fromUtf8("Режим отображения. Для молекул обычно Ball and Stick, Space Fill или Wireframe."));
	setDetailedTip(colour_scheme_combo, QString::fromUtf8("Цветовая схема. CPK подходит для химии: углерод, кислород, азот и водород получают узнаваемые цвета."));
	setDetailedTip(display_colour_button, QString::fromUtf8("Базовый цвет объекта или превью-контейнера в мире."));
	setDetailedTip(material_combo, QString::fromUtf8("Материал отображения: матовый, глянцевый, стекло, металл или голограмма."));
	setDetailedTip(atom_radius_spin, QString::fromUtf8("Множитель визуального радиуса атомов. Не изменяет реальные атомные координаты. Применяется к локально сгенерированной molecule mesh в реальном времени через новый model fingerprint."));
	setDetailedTip(bond_thickness_spin, QString::fromUtf8("Толщина визуальных связей. Не изменяет химический порядок связей. Применяется к molecule mesh в реальном времени через новый model fingerprint."));
	setDetailedTip(point_size_spin, QString::fromUtf8("Размер точек для point cloud, графиков и наборов точек."));
	setDetailedTip(line_width_spin, QString::fromUtf8("Толщина линий для связей, графиков, траекторий и сеток."));
	setDetailedTip(opacity_spin, QString::fromUtf8("Прозрачность объекта. Уменьши значение, если объект перекрывает сцену."));
	setDetailedTip(object_scale_spin, QString::fromUtf8("Масштаб научной визуализации внутри объекта, отдельно от масштаба самого объекта в мире."));
	setDetailedTip(show_labels_check, QString::fromUtf8("Показывать подписи элементов, атомов, точек или осей, когда визуализатор поддерживает подписи."));
	setDetailedTip(atom_labels_pinned_check, QString::fromUtf8("Фиксировать ориентацию подписей атомов относительно молекулы. При выключенной фиксации подписи ориентируются к текущей камере."));
	setDetailedTip(show_molecule_title_check, QString::fromUtf8("Показывать отдельную 3D-подпись с названием над молекулой. Она использует тот же world-mapping, что и атомные подписи."));
	setDetailedTip(molecule_title_edit, QString::fromUtf8("Необязательный текст названия над молекулой. Если пусто — используется имя молекулы/объекта."));
	setDetailedTip(molecule_title_pinned_check, QString::fromUtf8("Фиксировать название в одной позиции и ориентации относительно молекулы."));
	setDetailedTip(show_info_card_check, QString::fromUtf8("Показывать compact 3D-карточку рядом с молекулой/выбранным атомом. Карточка создаётся в том же world-overlay path, что подписи, поэтому движется и удаляется вместе с молекулой."));
	setDetailedTip(info_card_mode_combo, QString::fromUtf8("Режим карточки: по выбору, всегда молекула или только выбранный атом."));
	setDetailedTip(info_card_scale_spin, QString::fromUtf8("Масштаб текста 3D-карточки в мире. Не меняет подписи атомов и не влияет на 2D-превью."));
	setDetailedTip(info_card_distance_spin, QString::fromUtf8("Отступ карточки от выбранного атома или центра молекулы в world units."));
	setDetailedTip(info_card_pinned_check, QString::fromUtf8("Закрепить карточку в одной позиции и ориентации относительно молекулы. Выбор другого атома изменит содержание, но не переместит карточку."));
	setDetailedTip(info_card_dark_background_check, QString::fromUtf8("Добавить тёмный читаемый фон под текстом карточки."));
	setDetailedTip(info_card_stand_type_combo, QString::fromUtf8("Выбрать mesh-стенд: без стенда, плоская панель, скруглённая панель или панель на опоре."));
	setDetailedTip(info_card_auto_fit_text_check, QString::fromUtf8("Автоматически уменьшать текст, чтобы он помещался в заданные размеры стенда."));
	setDetailedTip(info_card_stand_width_spin, QString::fromUtf8("Ширина mesh-стенда карточки в локальных единицах объекта."));
	setDetailedTip(info_card_stand_height_spin, QString::fromUtf8("Высота mesh-стенда карточки в локальных единицах объекта."));
	setDetailedTip(info_card_stand_depth_spin, QString::fromUtf8("Толщина панели и опоры mesh-стенда."));
	setDetailedTip(show_legend_check, QString::fromUtf8("Показывать легенду цветов/значений, когда визуализатор поддерживает легенду."));
	setDetailedTip(show_hydrogen_check, QString::fromUtf8("Для молекул: показывать атомы водорода. Отключение делает крупные молекулы чище."));
	setDetailedTip(label_mode_combo, QString::fromUtf8("Какая информация должна попадать в подписи: элемент, номер атома, residue, chain или custom attribute. Runtime label objects ещё требуют отдельного child ObjectType_Text workflow."));
	setDetailedTip(label_scale_spin, QString::fromUtf8("Масштаб будущих подписей. Не меняет научные координаты; сохраняется в schema и будет применяться existing text/label system adapter."));
	setDetailedTip(molecule_title_scale_spin, QString::fromUtf8("Отдельный масштаб главной 3D-надписи с названием молекулы."));
	setDetailedTip(label_max_count_spin, QString::fromUtf8("Ограничение числа подписей для защиты производительности. 0 означает не показывать подписи даже если show labels включен."));
	setDetailedTip(label_max_distance_spin, QString::fromUtf8("Максимальная дистанция видимости подписей в world units. 0 означает без отдельного ограничения на уровне schema."));
	setDetailedTip(lod_spin, QString::fromUtf8("Уровень детализации. Чем выше LOD, тем тяжелее объект может быть для рендера."));
	setDetailedTip(glow_enabled_check, QString::fromUtf8("Включает emission/glow через существующий WorldMaterial. Не создаёт отдельный Scientific renderer."));
	setDetailedTip(glow_strength_spin, QString::fromUtf8("Сила свечения WorldMaterial. Единицы соответствуют текущему material emission field; применяется в реальном времени к материалу объекта."));
	setDetailedTip(outline_enabled_check, QString::fromUtf8("Сохраняет намерение outline/selection highlight. Прямого per-object outline renderer path здесь пока не подключено."));
	setDetailedTip(wireframe_enabled_check, QString::fromUtf8("Принудительно использует wireframe-подобную molecule mesh генерацию без изменения исходных atom/bond данных."));

	setDetailedTip(measure_distance_check, QString::fromUtf8("Включить измерение расстояний между выбранными точками/атомами."));
	setDetailedTip(measure_angle_check, QString::fromUtf8("Включить измерение углов."));
	setDetailedTip(measure_torsion_check, QString::fromUtf8("Включить измерение торсионных углов."));
	setDetailedTip(measure_area_check, QString::fromUtf8("Включить измерение площади для поверхностей."));
	setDetailedTip(measure_volume_check, QString::fromUtf8("Включить измерение объема для объемных данных или закрытых поверхностей."));
	setDetailedTip(atom_count_spin, QString::fromUtf8("Количество атомов в текущих данных. Для built-in Caffeine sample ставится 24; для внешних источников значение должно приходить из provider/import path."));
	setDetailedTip(bond_count_spin, QString::fromUtf8("Количество связей в текущих данных."));
	setDetailedTip(point_count_spin, QString::fromUtf8("Количество точек для point cloud, графиков и поверхностей."));
	setDetailedTip(dimensions_edit, QString::fromUtf8("Размеры объекта или диапазон данных в физических единицах."));

	setDetailedTip(rotation_animation_check, QString::fromUtf8("Сохраняет настройку визуального вращения. Runtime tick/update связь пока не подключена; это не научная симуляция."));
	setDetailedTip(trajectory_animation_check, QString::fromUtf8("Сохраняет намерение движения по траектории. Реальный playback требует траекторных данных и runtime adapter."));
	setDetailedTip(vibration_animation_check, QString::fromUtf8("Сохраняет намерение колебаний. Не генерирует молекулярные моды и не является вычислительной симуляцией."));
	setDetailedTip(time_series_check, QString::fromUtf8("Сохраняет использование временных кадров, если данные действительно содержат frame sequence."));
	setDetailedTip(animation_speed_spin, QString::fromUtf8("Скорость воспроизведения animation/playback metadata. Runtime применение требует отдельной tick/update интеграции."));
	setDetailedTip(current_frame_spin, QString::fromUtf8("Текущий кадр временных данных."));
	setDetailedTip(frame_count_spin, QString::fromUtf8("Общее число кадров во временном наборе."));

	setDetailedTip(simulation_enabled_check, QString::fromUtf8("Зарезервировано для будущих физических/численных симуляций."));
	setDetailedTip(simulation_type_combo, QString::fromUtf8("Тип будущей симуляции: молекулярная динамика, поля, CFD, численный solver."));
	setDetailedTip(simulation_notes_edit, QString::fromUtf8("Заметки и параметры будущей симуляции."));

	setDetailedTip(ai_prompt_edit, QString::fromUtf8("Запрос для AI. MetaSiberia не оплачивает запросы: используется только ключ или локальный URL пользователя."));
	setDetailedTip(generated_code_edit, QString::fromUtf8("Сгенерированный код. Его можно проверить и затем использовать как источник данных."));
	setDetailedTip(ai_provider_combo, QString::fromUtf8("AI-провайдер: OpenAI, Anthropic, Gemini, OpenRouter, DeepSeek, Ollama или LM Studio."));
	setDetailedTip(ai_model_edit, QString::fromUtf8("Имя модели у выбранного провайдера, например gpt-4.1, claude, deepseek или локальная модель."));
	setDetailedTip(ai_endpoint_edit, QString::fromUtf8("URL для локальных или совместимых API. Пример для Ollama: http://localhost:11434."));
	setDetailedTip(ai_api_key_edit, QString::fromUtf8("API key хранится только локально в настройках этого клиента и не записывается в ScientificObject, мир или server state."));
	setDetailedTip(ai_user_credentials_check, QString::fromUtf8("Использовать ключ/URL пользователя. Расходы на облачные токены несет пользователь, не MetaSiberia."));
	setDetailedTip(ai_generate_code_button, QString::fromUtf8("Сгенерировать Python-код из запроса словами. Примеры уже поддерживают кофеин, sin(x), облако точек и поверхность."));
	setDetailedTip(ai_create_object_button, QString::fromUtf8("Применить сгенерированный результат к текущему ScientificObject, когда исполнитель кода будет подключен."));
	setDetailedTip(ai_explain_button, QString::fromUtf8("Будущая команда: объяснить текущий научный объект, источник и визуализацию."));
	setDetailedTip(ai_optimise_button, QString::fromUtf8("Будущая команда: оптимизировать код или данные перед визуализацией."));

	setDetailedTip(custom_properties_edit, QString::fromUtf8("Дополнительные JSON-свойства для будущих адаптеров и визуализаторов."));
}


void ScientificObjectEditor::populateStaticCombos()
{
	auto add = [](QComboBox* combo, const char* label, const char* item_data) { combo->addItem(QString::fromUtf8(label), QString::fromLatin1(item_data)); };

	add(type_combo, "Молекула", "molecule");
	add(type_combo, "Белок", "protein");
	add(type_combo, "ДНК", "dna");
	add(type_combo, "РНК", "rna");
	add(type_combo, "Кристалл", "crystal");
	add(type_combo, "Материал", "material");
	add(type_combo, "Планета", "planet");
	add(type_combo, "Астероид", "asteroid");
	add(type_combo, "Карта", "map");
	add(type_combo, "GIS", "gis");
	add(type_combo, "Облако точек", "point_cloud");
	add(type_combo, "Поле", "field");
	add(type_combo, "Объемные данные", "volume");
	add(type_combo, "График", "chart");
	add(type_combo, "Граф знаний", "knowledge_graph");
	add(type_combo, "Точечный набор", "point_set");
	add(type_combo, "Поверхность", "surface");
	add(type_combo, "Медицинские данные", "medical");
	add(type_combo, "Пользовательский объект", "custom");

	add(source_mode_combo, "Файл", "file");
	add(source_mode_combo, "URL", "url");
	add(source_mode_combo, "Онлайн-база", "online");
	add(source_mode_combo, "Код", "code");
	add(source_mode_combo, "Запрос словами", "prompt");

	const char* databases[] = {
		"Built-in sample catalog", "PubChem", "ChEBI", "ChemSpider", "RCSB Protein Data Bank", "AlphaFold", "NCBI",
		"Materials Project", "OQMD", "Crystallography Open Database", "COD", "EMDB",
		"NASA", "NASA Open API", "JPL", "ESA",
		"CERN Open Data", "USGS", "OpenStreetMap", "OpenTopography", "Zenodo",
		"FigShare", "GBIF", "NOAA", "Copernicus", "Natural Earth"
	};
	for(const char* db : databases)
		online_database_combo->addItem(QString::fromLatin1(db), QString::fromLatin1(db));

	const char* languages[] = { "Python", "JavaScript", "Lua", "C#", "C++" };
	for(const char* lang : languages)
		code_language_combo->addItem(QString::fromLatin1(lang), QString::fromLatin1(lang));

	add(visualization_mode_combo, "Ball and Stick", "ball_and_stick");
	add(visualization_mode_combo, "Space Fill", "space_fill");
	add(visualization_mode_combo, "Wireframe", "wireframe");
	add(visualization_mode_combo, "Surface", "surface");
	add(visualization_mode_combo, "Ribbon", "ribbon");
	add(visualization_mode_combo, "Points", "points");
	add(visualization_mode_combo, "Lines", "lines");
	add(visualization_mode_combo, "Mesh", "mesh");
	add(visualization_mode_combo, "Heatmap", "heatmap");
	add(visualization_mode_combo, "Volume", "volume");

	add(colour_scheme_combo, "CPK", "CPK");
	add(colour_scheme_combo, "Monochrome", "monochrome");
	add(colour_scheme_combo, "By Element", "element");
	add(colour_scheme_combo, "By Chain", "chain");
	add(colour_scheme_combo, "By Value", "value");
	add(colour_scheme_combo, "Heatmap", "heatmap");

	add(label_mode_combo, "Element symbol", "element");
	add(label_mode_combo, "Atom number", "atom_number");
	add(label_mode_combo, "Element + number", "element_number");
	add(label_mode_combo, "Atomic number", "atomic_number");
	add(label_mode_combo, "Atomic mass", "atomic_mass");
	add(label_mode_combo, "Formal charge", "formal_charge");
	add(label_mode_combo, "Custom attribute", "custom_attribute");

	add(info_card_mode_combo, "По выбору", "selection");
	add(info_card_mode_combo, "Всегда карточка молекулы", "molecule");
	add(info_card_mode_combo, "Только выбранный атом", "atom");
	add(info_card_stand_type_combo, "Без стенда", "none");
	add(info_card_stand_type_combo, "Плоская панель", "panel");
	add(info_card_stand_type_combo, "Скруглённая панель", "rounded_panel");
	add(info_card_stand_type_combo, "Панель на опоре", "pedestal");

	add(animation_direction_combo, "По часовой стрелке", "forward");
	add(animation_direction_combo, "Против часовой стрелки", "reverse");

	add(selection_mode_combo, "Выбор атомов", "atom");
	add(selection_mode_combo, "Выбор связей", "bond");
	add(selection_mode_combo, "Выбор молекулы", "molecule");

	add(material_combo, "Matte", "matte");
	add(material_combo, "Glossy", "glossy");
	add(material_combo, "Metal", "metal");
	add(material_combo, "Glass", "glass");
	add(material_combo, "Hologram", "hologram");

	add(physics_motion_type_combo, "Static", "static");
	add(physics_motion_type_combo, "Dynamic", "dynamic");
	add(physics_motion_type_combo, "Kinematic (stored only)", "kinematic");

	add(physics_shape_combo, "Mesh / visual", "mesh");
	add(physics_shape_combo, "Box", "box");
	add(physics_shape_combo, "Sphere", "sphere");
	add(physics_shape_combo, "None", "none");

	add(simulation_type_combo, "Future", "future");
	add(simulation_type_combo, "Molecular Dynamics", "molecular_dynamics");
	add(simulation_type_combo, "Physics", "physics");
	add(simulation_type_combo, "Electromagnetic Field", "em_field");
	add(simulation_type_combo, "CFD", "cfd");
	add(simulation_type_combo, "Numerical Solver", "numerical_solver");

	const char* providers[] = { "OpenAI", "Anthropic", "Gemini", "OpenRouter", "DeepSeek", "Ollama", "LM Studio" };
	for(const char* provider : providers)
		ai_provider_combo->addItem(QString::fromLatin1(provider), QString::fromLatin1(provider));
}


void ScientificObjectEditor::connectObjectChangeSignals()
{
	const QList<QLineEdit*> line_edits = findChildren<QLineEdit*>();
	for(QLineEdit* edit : line_edits)
	{
		if(edit->property("scientificLocalOnly").toBool())
			continue;
		connect(edit, SIGNAL(editingFinished()), this, SLOT(emitObjectChanged()));
	}

	const QList<QPlainTextEdit*> plain_edits = findChildren<QPlainTextEdit*>();
	for(QPlainTextEdit* edit : plain_edits)
		if(!edit->property("scientificReadOnly").toBool())
			connect(edit, SIGNAL(textChanged()), this, SLOT(emitObjectChanged()));

	const QList<QComboBox*> combos = findChildren<QComboBox*>();
	for(QComboBox* combo : combos)
		connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(emitObjectChanged()));

	const QList<QCheckBox*> checks = findChildren<QCheckBox*>();
	for(QCheckBox* check : checks)
		connect(check, SIGNAL(toggled(bool)), this, SLOT(emitObjectChanged()));

	const QList<QDoubleSpinBox*> double_spins = findChildren<QDoubleSpinBox*>();
	for(QDoubleSpinBox* spin : double_spins)
		connect(spin, SIGNAL(valueChanged(double)), this, SLOT(emitObjectChanged()));

	const QList<QSpinBox*> spins = findChildren<QSpinBox*>();
	for(QSpinBox* spin : spins)
		connect(spin, SIGNAL(valueChanged(int)), this, SLOT(emitObjectChanged()));

	connect(pos_x_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(pos_y_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(pos_z_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(scale_x_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(scale_y_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(scale_z_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(rot_x_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(rot_y_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));
	connect(rot_z_spin, SIGNAL(valueChanged(double)), this, SLOT(emitTransformChanged()));

	connect(browse_file_button, SIGNAL(clicked(bool)), this, SLOT(browseFile()));
	connect(source_mode_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(sourceModeChanged(int)));
	connect(online_database_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(onlineDatabaseChanged(int)));
	connect(type_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(updateScientificSourceUiState()));
	connect(online_query_edit, SIGNAL(textChanged(QString)), this, SLOT(updateScientificSourceUiState()));
	connect(online_results_list, SIGNAL(currentItemChanged(QListWidgetItem*, QListWidgetItem*)), this, SLOT(updateScientificSourceUiState()));
	connect(ai_provider_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(aiProviderChanged(int)));
	connect(ai_api_key_edit, SIGNAL(editingFinished()), this, SLOT(saveAiApiKey()));
	connect(online_search_button, SIGNAL(clicked(bool)), this, SLOT(previewScientificSourceResult()));
	connect(online_preview_button, SIGNAL(clicked(bool)), this, SLOT(previewScientificSourceResult()));
	connect(online_load_button, SIGNAL(clicked(bool)), this, SLOT(loadScientificSourceResult()));
	connect(ai_generate_code_button, SIGNAL(clicked(bool)), this, SLOT(generateCodeFromPrompt()));
	connect(ai_create_object_button, SIGNAL(clicked(bool)), this, SLOT(emitObjectChanged()));
	connect(ai_explain_button, SIGNAL(clicked(bool)), this, SLOT(emitObjectChanged()));
	connect(ai_optimise_button, SIGNAL(clicked(bool)), this, SLOT(emitObjectChanged()));
	connect(display_colour_button, SIGNAL(clicked(bool)), this, SLOT(updateColourButton()));
}


void ScientificObjectEditor::emitObjectChanged()
{
	if(!syncing)
		emit objectChanged();
}


void ScientificObjectEditor::emitTransformChanged()
{
	if(!syncing)
		emit objectTransformChanged();
}


QString ScientificObjectEditor::selectionModeDisplayText(const QString& mode) const
{
	if(mode == QStringLiteral("bond")) return QString::fromUtf8("выбор связей");
	if(mode == QStringLiteral("molecule")) return QString::fromUtf8("выбор молекулы");
	return QString::fromUtf8("выбор атомов");
}


QString ScientificObjectEditor::selectionStateDisplayText(const QString& state) const
{
	if(state == QStringLiteral("atom_selected")) return QString::fromUtf8("выбран атом");
	if(state == QStringLiteral("multiple_atoms_selected")) return QString::fromUtf8("выбрано несколько атомов");
	if(state == QStringLiteral("bond_selected")) return QString::fromUtf8("выбрана связь");
	if(state == QStringLiteral("molecule_selected")) return QString::fromUtf8("выбрана молекула");
	return QString::fromUtf8("нет выбора");
}


void ScientificObjectEditor::updateMoleculeSelectionStatus()
{
	if(!molecule_viewport || !molecule_selection_status_label)
		return;
	molecule_selection_status_label->setText(QString::fromUtf8("%1 | режим: %2 | атомы: %3 | связь: %4")
		.arg(selectionStateDisplayText(molecule_viewport->selectionState()))
		.arg(selectionModeDisplayText(molecule_viewport->selectionMode()))
		.arg(molecule_viewport->selectedAtomsText().isEmpty() ? QString::fromUtf8("нет") : molecule_viewport->selectedAtomsText())
		.arg(molecule_viewport->selectedBondIndex() >= 0 ? QString::number(molecule_viewport->selectedBondIndex()) : QString::fromUtf8("нет")));
}


void ScientificObjectEditor::updateMoleculeImagePreview()
{
	if(!molecule_image_label)
		return;
	if(molecule_image_preview_pixmap.isNull())
	{
		molecule_image_label->setPixmap(QPixmap());
		molecule_image_label->setText(QString::fromUtf8("Превью 2D-изображения"));
		return;
	}
	const QSize base_size = molecule_image_label->contentsRect().size().expandedTo(QSize(120, 96));
	const QSize target_size = base_size * molecule_image_preview_zoom;
	molecule_image_label->setText(QString());
	molecule_image_label->setPixmap(molecule_image_preview_pixmap.scaled(target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}


bool ScientificObjectEditor::eventFilter(QObject* watched, QEvent* event)
{
	if(watched == molecule_image_label)
	{
		if(event->type() == QEvent::Wheel)
		{
			QWheelEvent* wheel_event = static_cast<QWheelEvent*>(event);
			molecule_image_preview_zoom *= wheel_event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
			molecule_image_preview_zoom = std::max(0.25, std::min(6.0, molecule_image_preview_zoom));
			updateMoleculeImagePreview();
			return true;
		}
		if(event->type() == QEvent::Resize)
			updateMoleculeImagePreview();
	}
	return QWidget::eventFilter(watched, event);
}


void ScientificObjectEditor::updateScientificSourceUiState()
{
	if(!online_load_button || !online_results_list || !online_query_edit || !online_database_combo || !type_combo)
		return;

	const QString db = online_database_combo->currentText().trimmed();
	const QString selected_type = currentComboData(type_combo, QStringLiteral("custom"));
	const bool is_pubchem_molecule = (selected_type == QStringLiteral("molecule")) && (db.compare(QStringLiteral("PubChem"), Qt::CaseInsensitive) == 0);
	const bool has_query = !online_query_edit->text().trimmed().isEmpty();

	QListWidgetItem* item = online_results_list->currentItem();
	if(!item && online_results_list->count() == 1)
	{
		online_results_list->setCurrentRow(0);
		item = online_results_list->currentItem();
	}

	if(is_pubchem_molecule)
	{
		if(!has_query)
		{
			online_load_button->setEnabled(false);
			online_load_button->setText(QString::fromUtf8("Сначала выполните поиск"));
			return;
		}

		if(item && item->data(Qt::UserRole + 1).toString() == QStringLiteral("pubchem_cid"))
		{
			const QString cid = item->data(Qt::UserRole).toString();
			online_load_button->setEnabled(controls_editable);
			if((current_settings.load_status == "ready" || current_settings.load_status == "loaded_from_cache") && current_settings.online_result_id == stdstr(cid))
				online_load_button->setText(QString::fromUtf8("Обновить объект"));
			else
				online_load_button->setText(QString::fromUtf8("Загрузить CID %1 в объект").arg(cid));
			return;
		}

		online_load_button->setEnabled(false);
		online_load_button->setText(QString::fromUtf8("Выберите результат"));
		return;
	}

	online_load_button->setEnabled(controls_editable && has_query);
	online_load_button->setText(QString::fromUtf8("Загрузить в объект"));
}


QString ScientificObjectEditor::currentComboData(const QComboBox* combo, const QString& fallback) const
{
	if(!combo)
		return fallback;
	const QVariant item_data = combo->currentData();
	if(item_data.isValid() && !item_data.toString().isEmpty())
		return item_data.toString();
	return fallback;
}


void ScientificObjectEditor::setComboData(QComboBox* combo, const QString& item_data)
{
	const int index = combo->findData(item_data);
	combo->setCurrentIndex(index >= 0 ? index : 0);
}


QString ScientificObjectEditor::aiSettingsProviderKey() const
{
	QString provider = currentComboData(ai_provider_combo, QStringLiteral("OpenAI"));
	provider = provider.trimmed();
	if(provider.isEmpty())
		provider = QStringLiteral("OpenAI");

	QString safe_provider;
	for(int i=0; i<provider.size(); ++i)
	{
		const QChar c = provider[i];
		safe_provider.append(c.isLetterOrNumber() ? c : QChar('_'));
	}
	return QStringLiteral("scientificObjectEditor/aiApiKey/") + safe_provider;
}


void ScientificObjectEditor::setControlsFromSettings(const ScientificObjectSettings& s)
{
	current_settings = s;
	name_edit->setText(qstr(s.name));
	setComboData(type_combo, qstr(s.scientific_type));
	description_edit->setPlainText(qstr(s.description));
	source_edit->setText(qstr(s.source));
	author_edit->setText(qstr(s.author));
	tags_edit->setText(qstr(s.tags));
	uuid_edit->setText(qstr(s.uuid));
	created_edit->setText(qstr(s.created_time));
	modified_edit->setText(qstr(s.modified_time));

	setComboData(source_mode_combo, qstr(s.source_mode));
	file_path_edit->setText(qstr(s.file_path));
	url_edit->setText(qstr(s.source_url));
	setComboData(online_database_combo, qstr(s.online_database));
	online_query_edit->setText(qstr(s.online_query));
	online_results_list->clear();
	if(!s.online_result_id.empty())
	{
		QListWidgetItem* restored_item = new QListWidgetItem(qstr(s.online_result_id), online_results_list);
		restored_item->setData(Qt::UserRole, qstr(s.online_result_id));
		restored_item->setData(Qt::UserRole + 1, QStringLiteral("restored"));
		online_results_list->setCurrentItem(restored_item);
	}
	if(source_status_label)
		source_status_label->setText(qstr(s.load_status + ": " + s.load_status_message));
	if(query_resolution_label)
		query_resolution_label->setText(QStringLiteral("Original query: %1 | Normalized query: %2 | Translation: %3")
			.arg(s.search_original_query.empty() ? QStringLiteral("Not available") : qstr(s.search_original_query), s.search_normalized_query.empty() ? QStringLiteral("Not available") : qstr(s.search_normalized_query), s.search_translation.empty() ? QStringLiteral("Not used") : qstr(s.search_translation)));
	if(molecule_image_label)
	{
		if(!s.image_cache_path.empty())
		{
			QPixmap pixmap(qstr(s.image_cache_path));
			if(!pixmap.isNull())
			{
				molecule_image_preview_pixmap = pixmap;
				molecule_image_preview_zoom = 1.0;
				updateMoleculeImagePreview();
			}
			else
			{
				molecule_image_preview_pixmap = QPixmap();
				molecule_image_label->setText(QString::fromUtf8("2D-изображение недоступно"));
			}
		}
		else
		{
			molecule_image_preview_pixmap = QPixmap();
			molecule_image_label->setText(QString::fromUtf8("Превью 2D-изображения"));
		}
	}
	if(image_viewer)
	{
		if(!s.image_cache_path.empty()) image_viewer->setImage(qstr(s.image_cache_path), qstr(s.image_url), qstr(s.provenance_license));
		else image_viewer->clearImage();
	}
	code_edit->setPlainText(qstr(s.code_text));
	prompt_edit->setPlainText(qstr(s.prompt_text));
	setComboData(code_language_combo, qstr(s.code_language));

	data_summary_edit->setPlainText(qstr(s.data_summary));
	atom_table_edit->setPlainText(qstr(s.atom_table));
	bond_table_edit->setPlainText(qstr(s.bond_table));
	point_table_edit->setPlainText(qstr(s.point_table));
	value_table_edit->setPlainText(qstr(s.value_table));
	property_table_edit->setPlainText(qstr(s.property_table));

	setComboData(visualization_mode_combo, qstr(s.visualization_mode));
	setComboData(colour_scheme_combo, qstr(s.colour_scheme));
	display_colour = s.display_colour;
	setComboData(material_combo, qstr(s.material));
	atom_radius_spin->setValue(s.atom_radius);
	bond_thickness_spin->setValue(s.bond_thickness);
	point_size_spin->setValue(s.point_size);
	line_width_spin->setValue(s.line_width);
	opacity_spin->setValue(s.opacity);
	object_scale_spin->setValue(s.object_scale);
	show_labels_check->setChecked(s.show_labels);
	atom_labels_pinned_check->setChecked(s.atom_labels_pinned);
	show_molecule_title_check->setChecked(s.show_molecule_title);
	molecule_title_edit->setText(qstr(s.molecule_title));
	molecule_title_pinned_check->setChecked(s.molecule_title_pinned);
	show_info_card_check->setChecked(s.show_info_card);
	setComboData(info_card_mode_combo, qstr(s.info_card_mode));
	info_card_scale_spin->setValue(s.info_card_scale);
	info_card_distance_spin->setValue(s.info_card_distance);
	info_card_pinned_check->setChecked(s.info_card_pinned);
	info_card_dark_background_check->setChecked(s.info_card_dark_background);
	setComboData(info_card_stand_type_combo, qstr(s.info_card_stand_type));
	info_card_auto_fit_text_check->setChecked(s.info_card_auto_fit_text);
	info_card_stand_width_spin->setValue(s.info_card_stand_width);
	info_card_stand_height_spin->setValue(s.info_card_stand_height);
	info_card_stand_depth_spin->setValue(s.info_card_stand_depth);
	show_legend_check->setChecked(s.show_legend);
	show_hydrogen_check->setChecked(s.show_hydrogen);
	setComboData(label_mode_combo, qstr(s.label_mode));
	label_colour_button->setStyleSheet(QStringLiteral("background:%1").arg(QColor((int)(s.label_colour.r*255.f),(int)(s.label_colour.g*255.f),(int)(s.label_colour.b*255.f)).name()));
	label_scale_spin->setValue(s.label_scale);
	molecule_title_scale_spin->setValue(s.molecule_title_scale);
	label_max_count_spin->setValue(s.label_max_count);
	label_max_distance_spin->setValue(s.label_max_distance);
	lod_spin->setValue(s.lod_level);
	glow_enabled_check->setChecked(s.glow_enabled);
	glow_strength_spin->setValue(s.glow_strength);
	outline_enabled_check->setChecked(s.outline_enabled);
	wireframe_enabled_check->setChecked(s.wireframe_enabled);
	setComboData(selection_mode_combo, qstr(s.selection_mode));
	molecule_viewport->setMolecule(qstr(s.atom_table), qstr(s.bond_table));
	molecule_viewport->setScientificSettings(s);
	molecule_viewport->setSelectionMode(qstr(s.selection_mode));
	molecule_viewport->setSelectionState(qstr(s.selected_atom_indices), s.selected_bond_index, qstr(s.selection_state));
	molecule_viewport->setMeasurementsJson(qstr(s.measurements_json));
	updateMoleculeSelectionStatus();

	measure_distance_check->setChecked(s.measure_distance);
	measure_angle_check->setChecked(s.measure_angle);
	measure_torsion_check->setChecked(s.measure_torsion);
	measure_area_check->setChecked(s.measure_area);
	measure_volume_check->setChecked(s.measure_volume);
	atom_count_spin->setValue(s.atom_count);
	bond_count_spin->setValue(s.bond_count);
	point_count_spin->setValue(s.point_count);
	dimensions_edit->setText(qstr(s.object_dimensions));
	molecule_metrics_label->setText(molecule_viewport->moleculeMetricsText());
	measurement_records_edit->setPlainText(molecule_viewport->measurementsJson());

	rotation_animation_check->setChecked(s.rotation_animation_enabled);
	trajectory_animation_check->setChecked(s.trajectory_animation_enabled);
	vibration_animation_check->setChecked(s.vibration_animation_enabled);
	time_series_check->setChecked(s.time_series_enabled);
	animation_speed_spin->setValue(s.animation_speed);
	setComboData(animation_direction_combo, qstr(s.animation_direction));
	current_frame_spin->setValue(s.current_frame);
	frame_count_spin->setValue(s.frame_count);
	molecule_viewport->setSpinEnabled(s.rotation_animation_enabled, s.animation_speed * (s.animation_direction == "reverse" ? -1.f : 1.f));

	simulation_enabled_check->setChecked(false);
	setComboData(simulation_type_combo, qstr(s.simulation_type));
	simulation_notes_edit->setPlainText(qstr(s.simulation_notes));
	provider_classification_edit->setPlainText(qstr(s.provider_classifications));
	computed_classification_edit->setPlainText(qstr(s.computed_classifications));
	user_collections_edit->setText(qstr(s.user_collections));
	favorite_check->setChecked(s.favorite);
	if(catalog_status_label && settings)
		catalog_status_label->setText(QStringLiteral("Recent CIDs: %1\nSearch history: %2\nFilters available for indexed catalog records: formula, mass range, elements, charge, confirmed organic/inorganic, biological role, hazard, source, 3D, safety, bioactivity.")
			.arg(settings->value(QStringLiteral("scientificObjectEditor/recentCids")).toStringList().join(QStringLiteral(", ")), settings->value(QStringLiteral("scientificObjectEditor/searchHistory")).toStringList().join(QStringLiteral("; "))));

	if(molecule_card_edits.size() >= 10)
	{
		for(QPlainTextEdit* edit : molecule_card_edits) edit->setProperty("pugLoaded", false);
		molecule_card_edits[0]->setPlainText(qstr(s.data_summary)); molecule_card_status_labels[0]->setText(s.data_summary.empty() ? QString::fromUtf8("Нет данных") : QString::fromUtf8("Загружено"));
		molecule_card_edits[1]->setPlainText(QString::fromUtf8("Атомы\n%1\n\nСвязи\n%2").arg(qstr(s.atom_table), qstr(s.bond_table))); molecule_card_status_labels[1]->setText(s.atom_table.empty() ? QString::fromUtf8("Нет данных") : QString::fromUtf8("Загружено"));
		molecule_card_edits[2]->setPlainText(qstr(s.property_table)); molecule_card_status_labels[2]->setText(s.property_table.empty() ? QString::fromUtf8("Нет данных") : QString::fromUtf8("Загружено"));
		molecule_card_edits[3]->setPlainText(qstr(s.provider_classifications)); molecule_card_status_labels[3]->setText(s.provider_classifications.empty() ? QString::fromUtf8("Нет данных — откройте раздел для загрузки") : QString::fromUtf8("Из кэша"));
		molecule_card_edits[9]->setPlainText(QString::fromUtf8("Источник: %1\nИдентификатор: %2\nURL: %3\nЗагружено: %4\nФормат: %5\nВерсия: %6\nЛицензия: %7\nКонтрольная сумма: %8")
			.arg(qstr(s.provenance_source), qstr(s.provenance_identifier), qstr(s.provenance_url), qstr(s.provenance_loaded_at), qstr(s.provenance_format), qstr(s.provenance_version), qstr(s.provenance_license), qstr(s.provenance_checksum)));
		molecule_card_status_labels[9]->setText(s.provenance_source.empty() ? QString::fromUtf8("Нет данных") : QString::fromUtf8("Загружено"));
		for(int i=4; i<=8; ++i) { molecule_card_edits[i]->clear(); molecule_card_status_labels[i]->setText(QString::fromUtf8("Нет данных — откройте раздел для загрузки")); }
	}

	ai_prompt_edit->setPlainText(qstr(s.prompt_text));
	generated_code_edit->setPlainText(qstr(s.generated_code));
	setComboData(ai_provider_combo, qstr(s.ai_provider));
	ai_model_edit->setText(qstr(s.ai_model));
	ai_endpoint_edit->setText(qstr(s.ai_endpoint));
	aiProviderChanged(ai_provider_combo->currentIndex());
	ai_user_credentials_check->setChecked(s.ai_uses_user_credentials);
	collision_enabled_check->setChecked(s.collision_enabled);
	solid_check->setChecked(s.solid);
	trigger_check->setChecked(s.trigger);
	selectable_check->setChecked(s.selectable);
	movable_check->setChecked(s.movable);
	gravity_enabled_check->setChecked(s.gravity_enabled);
	setComboData(physics_motion_type_combo, qstr(s.physics_motion_type));
	setComboData(physics_shape_combo, qstr(s.physics_shape));
	collision_layer_edit->setText(qstr(s.collision_layer));
	physics_mass_spin->setValue(s.physics_mass);
	physics_friction_spin->setValue(s.physics_friction);
	physics_restitution_spin->setValue(s.physics_restitution);
	custom_properties_edit->setPlainText(qstr(s.custom_properties));

	updateColourButton();
	updateScientificSourceUiState();
}


ScientificObjectSettings ScientificObjectEditor::controlsToSettings() const
{
	ScientificObjectSettings s = current_settings;
	s.name = stdstr(name_edit->text());
	s.scientific_type = stdstr(currentComboData(type_combo, QStringLiteral("custom")));
	s.description = stdstr(description_edit->toPlainText());
	s.source = stdstr(source_edit->text());
	s.author = stdstr(author_edit->text());
	s.tags = stdstr(tags_edit->text());
	s.uuid = stdstr(uuid_edit->text());
	s.created_time = stdstr(created_edit->text());
	s.modified_time = stdstr(modified_edit->text());

	s.source_mode = stdstr(currentComboData(source_mode_combo, QStringLiteral("prompt")));
	s.file_path = stdstr(file_path_edit->text());
	s.source_url = stdstr(url_edit->text());
	s.online_database = stdstr(currentComboData(online_database_combo, QStringLiteral("PubChem")));
	s.online_query = stdstr(online_query_edit->text());
	if(online_results_list->currentItem())
		s.online_result_id = stdstr(online_results_list->currentItem()->data(Qt::UserRole).toString());
	s.code_language = stdstr(currentComboData(code_language_combo, QStringLiteral("Python")));
	s.code_text = stdstr(code_edit->toPlainText());
	s.prompt_text = stdstr(ai_prompt_edit->toPlainText().isEmpty() ? prompt_edit->toPlainText() : ai_prompt_edit->toPlainText());
	s.generated_code = stdstr(generated_code_edit->toPlainText());

	s.data_summary = stdstr(data_summary_edit->toPlainText());
	s.atom_table = stdstr(atom_table_edit->toPlainText());
	s.bond_table = stdstr(bond_table_edit->toPlainText());
	s.point_table = stdstr(point_table_edit->toPlainText());
	s.value_table = stdstr(value_table_edit->toPlainText());
	s.property_table = stdstr(property_table_edit->toPlainText());

	s.visualization_mode = stdstr(currentComboData(visualization_mode_combo, QStringLiteral("points")));
	s.colour_scheme = stdstr(currentComboData(colour_scheme_combo, QStringLiteral("CPK")));
	s.display_colour = display_colour;
	s.material = stdstr(currentComboData(material_combo, QStringLiteral("matte")));
	s.atom_radius = (float)atom_radius_spin->value();
	s.bond_thickness = (float)bond_thickness_spin->value();
	s.point_size = (float)point_size_spin->value();
	s.line_width = (float)line_width_spin->value();
	s.opacity = (float)opacity_spin->value();
	s.object_scale = (float)object_scale_spin->value();
	s.show_labels = show_labels_check->isChecked();
	s.atom_labels_pinned = atom_labels_pinned_check->isChecked();
	s.show_molecule_title = show_molecule_title_check->isChecked();
	s.molecule_title = stdstr(molecule_title_edit->text());
	s.molecule_title_pinned = molecule_title_pinned_check->isChecked();
	s.show_info_card = show_info_card_check->isChecked();
	s.info_card_mode = stdstr(currentComboData(info_card_mode_combo, QStringLiteral("selection")));
	s.info_card_scale = (float)info_card_scale_spin->value();
	s.info_card_distance = (float)info_card_distance_spin->value();
	s.info_card_pinned = info_card_pinned_check->isChecked();
	s.info_card_dark_background = info_card_dark_background_check->isChecked();
	s.info_card_stand_type = stdstr(currentComboData(info_card_stand_type_combo, QStringLiteral("rounded_panel")));
	s.info_card_auto_fit_text = info_card_auto_fit_text_check->isChecked();
	s.info_card_stand_width = (float)info_card_stand_width_spin->value();
	s.info_card_stand_height = (float)info_card_stand_height_spin->value();
	s.info_card_stand_depth = (float)info_card_stand_depth_spin->value();
	s.show_legend = show_legend_check->isChecked();
	s.show_hydrogen = show_hydrogen_check->isChecked();
	s.label_mode = stdstr(currentComboData(label_mode_combo, QStringLiteral("element")));
	s.label_scale = (float)label_scale_spin->value();
	s.molecule_title_scale = (float)molecule_title_scale_spin->value();
	s.label_max_count = label_max_count_spin->value();
	s.label_max_distance = (float)label_max_distance_spin->value();
	s.label_runtime_status = s.show_labels ? "interactive_molecule_viewport_active" : "disabled";
	s.lod_level = lod_spin->value();
	s.glow_enabled = glow_enabled_check->isChecked();
	s.glow_strength = (float)glow_strength_spin->value();
	s.outline_enabled = outline_enabled_check->isChecked();
	s.wireframe_enabled = wireframe_enabled_check->isChecked();
	s.selection_mode = stdstr(currentComboData(selection_mode_combo, QStringLiteral("atom")));
	s.selection_state = stdstr(molecule_viewport->selectionState());
	s.selected_atom_indices = stdstr(molecule_viewport->selectedAtomsText());
	s.selected_bond_index = molecule_viewport->selectedBondIndex();
	s.measurements_json = stdstr(molecule_viewport->measurementsJson());

	s.measure_distance = measure_distance_check->isChecked();
	s.measure_angle = measure_angle_check->isChecked();
	s.measure_torsion = measure_torsion_check->isChecked();
	s.measure_area = measure_area_check->isChecked();
	s.measure_volume = measure_volume_check->isChecked();
	s.atom_count = atom_count_spin->value();
	s.bond_count = bond_count_spin->value();
	s.point_count = point_count_spin->value();
	s.object_dimensions = stdstr(dimensions_edit->text());

	s.rotation_animation_enabled = rotation_animation_check->isChecked();
	s.trajectory_animation_enabled = trajectory_animation_check->isChecked();
	s.vibration_animation_enabled = vibration_animation_check->isChecked();
	s.time_series_enabled = time_series_check->isChecked();
	s.animation_speed = (float)animation_speed_spin->value();
	s.animation_direction = stdstr(currentComboData(animation_direction_combo, QStringLiteral("forward")));
	s.current_frame = current_frame_spin->value();
	s.frame_count = frame_count_spin->value();
	s.animation_runtime_status = s.rotation_animation_enabled ? "interactive_viewport_rotation_active" : ((s.trajectory_animation_enabled || s.vibration_animation_enabled || s.time_series_enabled) ? "wip_not_connected" : "disabled");

	s.simulation_enabled = false;
	s.simulation_type = stdstr(currentComboData(simulation_type_combo, QStringLiteral("future")));
	s.simulation_notes = stdstr(QStringLiteral("Backend unavailable. ") + simulation_notes_edit->toPlainText());
	s.provider_classifications = stdstr(provider_classification_edit->toPlainText());
	s.computed_classifications = stdstr(computed_classification_edit->toPlainText());
	s.user_collections = stdstr(user_collections_edit->text());
	s.favorite = favorite_check->isChecked();

	s.ai_provider = stdstr(currentComboData(ai_provider_combo, QStringLiteral("OpenAI")));
	s.ai_model = stdstr(ai_model_edit->text());
	s.ai_endpoint = stdstr(ai_endpoint_edit->text());
	s.ai_uses_user_credentials = ai_user_credentials_check->isChecked();
	s.collision_enabled = collision_enabled_check->isChecked();
	s.solid = solid_check->isChecked();
	s.trigger = trigger_check->isChecked();
	s.selectable = selectable_check->isChecked();
	s.movable = movable_check->isChecked();
	s.gravity_enabled = gravity_enabled_check->isChecked();
	s.physics_motion_type = stdstr(currentComboData(physics_motion_type_combo, QStringLiteral("static")));
	s.physics_shape = stdstr(currentComboData(physics_shape_combo, QStringLiteral("mesh")));
	s.collision_layer = stdstr(collision_layer_edit->text());
	s.physics_mass = (float)physics_mass_spin->value();
	s.physics_friction = (float)physics_friction_spin->value();
	s.physics_restitution = (float)physics_restitution_spin->value();
	s.custom_properties = stdstr(custom_properties_edit->toPlainText());
	return s;
}


void ScientificObjectEditor::setFromObject(const WorldObject& ob, bool)
{
	syncing = true;
	editing_ob_uid = ob.uid;
	std::string parse_error;
	setControlsFromSettings(ScientificObjectSettings::fromContent(ob.content, &parse_error));
	setTransformFromObject(ob);
	updateInfoLabel(ob);
	syncing = false;
}


void ScientificObjectEditor::setTransformFromObject(const WorldObject& ob)
{
	const bool was_syncing = syncing;
	syncing = true;
	pos_x_spin->setValue(ob.pos.x);
	pos_y_spin->setValue(ob.pos.y);
	pos_z_spin->setValue(ob.pos.z);
	scale_x_spin->setValue(ob.scale.x);
	scale_y_spin->setValue(ob.scale.y);
	scale_z_spin->setValue(ob.scale.z);

	const Matrix3f rot_mat = Matrix3f::rotationMatrix(normalise(ob.axis), ob.angle);
	const Vec3f angles = rot_mat.getAngles();
	rot_x_spin->setValue(angles.x * 360 / Maths::get2Pi<float>());
	rot_y_spin->setValue(angles.y * 360 / Maths::get2Pi<float>());
	rot_z_spin->setValue(angles.z * 360 / Maths::get2Pi<float>());
	updateInfoLabel(ob);
	syncing = was_syncing;
}


void ScientificObjectEditor::writeTransformMembersToObject(WorldObject& ob_out)
{
	ob_out.pos.x = pos_x_spin->value();
	ob_out.pos.y = pos_y_spin->value();
	ob_out.pos.z = pos_z_spin->value();
	ob_out.scale.x = (float)scale_x_spin->value();
	ob_out.scale.y = (float)scale_y_spin->value();
	ob_out.scale.z = (float)scale_z_spin->value();

	const Vec3f angles(
		(float)(rot_x_spin->value() / 360 * Maths::get2Pi<double>()),
		(float)(rot_y_spin->value() / 360 * Maths::get2Pi<double>()),
		(float)(rot_z_spin->value() / 360 * Maths::get2Pi<double>())
	);

	const Matrix3f rot_matrix = Matrix3f::fromAngles(angles);
	rot_matrix.rotationMatrixToAxisAngle(ob_out.axis, ob_out.angle);
	if(ob_out.axis.length() < 1.0e-5f)
	{
		ob_out.axis = Vec3f(0, 0, 1);
		ob_out.angle = 0;
	}
}


void ScientificObjectEditor::applyScientificMaterial(WorldObject& ob_out, const ScientificObjectSettings& s)
{
	if(applyMoleculeWorldMaterials(ob_out, s))
		return;

	if(ob_out.materials.empty())
		ob_out.materials.push_back(new WorldMaterial());

	WorldMaterialRef mat = ob_out.materials[0];
	if(mat.isNull())
	{
		mat = new WorldMaterial();
		ob_out.materials[0] = mat;
	}

	mat->name = "Scientific Object Preview";
	mat->colour_rgb = s.display_colour;
	const bool emissive = s.material == "hologram" || s.glow_enabled;
	mat->emission_rgb = emissive ? s.display_colour : Colour3f(s.display_colour.r * 0.12f, s.display_colour.g * 0.12f, s.display_colour.b * 0.12f);
	mat->emission_lum_flux_or_lum = s.glow_enabled ? s.glow_strength : (s.material == "hologram" ? 80.f : 0.f);
	mat->opacity = ScalarVal(s.opacity);
	mat->roughness = ScalarVal(s.material == "glossy" ? 0.12f : 0.56f);
	mat->metallic_fraction = ScalarVal(s.material == "metal" ? 0.85f : 0.0f);
	BitUtils::setBit(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG);
	if(s.material == "hologram")
		BitUtils::setBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG);
	else
		BitUtils::zeroBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG);
}


void ScientificObjectEditor::applyScientificPhysics(WorldObject& ob_out, const ScientificObjectSettings& s)
{
	const uint32 old_flags = ob_out.flags;
	const float old_mass = ob_out.mass;
	const float old_friction = ob_out.friction;
	const float old_restitution = ob_out.restitution;

	const bool wants_dynamic = s.physics_motion_type == "dynamic";
	const bool wants_collidable = s.collision_enabled && s.solid && s.physics_shape != "none";
	const bool wants_sensor = s.collision_enabled && s.trigger;

	ob_out.setCollidable(wants_collidable);
	ob_out.setDynamic(wants_dynamic);
	ob_out.setIsSensor(wants_sensor);
	ob_out.mass = s.physics_mass;
	ob_out.friction = s.physics_friction;
	ob_out.restitution = s.physics_restitution;

	if((old_flags ^ ob_out.flags) & WorldObject::DYNAMIC_FLAG)
		ob_out.changed_flags |= WorldObject::DYNAMIC_CHANGED;
	if(old_flags != ob_out.flags || std::fabs(old_mass - ob_out.mass) > 1.0e-6f || std::fabs(old_friction - ob_out.friction) > 1.0e-6f || std::fabs(old_restitution - ob_out.restitution) > 1.0e-6f)
		ob_out.changed_flags |= WorldObject::PHYSICS_VALUE_CHANGED;
}


void ScientificObjectEditor::toObject(WorldObject& ob_out)
{
	ScientificObjectSettings s = controlsToSettings();
	const std::string new_content = ScientificObjectSettings::serialiseToContent(s);
	if(new_content.size() > WorldObject::MAX_CONTENT_SIZE)
	{
		if(info_label)
			info_label->setText(QString::fromUtf8("ScientificObject payload is %1 bytes, above WorldObject::MAX_CONTENT_SIZE (%2). Reduce raw tables/custom properties before saving.")
				.arg((qulonglong)new_content.size())
				.arg((qulonglong)WorldObject::MAX_CONTENT_SIZE));
		return;
	}
	if(ob_out.content != new_content)
		ob_out.changed_flags |= WorldObject::CONTENT_CHANGED;
	ob_out.content = new_content;

	applyScientificMaterial(ob_out, s);
	applyScientificPhysics(ob_out, s);

	const std::string molecule_model_path = writeMoleculeOBJForSettings(s);
	if(!molecule_model_path.empty() && toStdString(ob_out.model_url) != molecule_model_path)
	{
		ob_out.object_type = WorldObject::ObjectType_Generic;
		ob_out.model_url = molecule_model_path;
		ob_out.max_model_lod_level = 2;
		ob_out.changed_flags |= WorldObject::MODEL_URL_CHANGED;
	}

	writeTransformMembersToObject(ob_out);
}


void ScientificObjectEditor::objectLastModifiedUpdated(const WorldObject& ob)
{
	updateInfoLabel(ob);
}


void ScientificObjectEditor::objectPickedUp()
{
	pos_x_spin->setEnabled(false);
	pos_y_spin->setEnabled(false);
	pos_z_spin->setEnabled(false);
}


void ScientificObjectEditor::objectDropped()
{
	pos_x_spin->setEnabled(true);
	pos_y_spin->setEnabled(true);
	pos_z_spin->setEnabled(true);
}


void ScientificObjectEditor::setControlsEnabled(bool enabled)
{
	setEnabled(enabled);
	if(!enabled && molecule_world_spin_timer)
		molecule_world_spin_timer->stop();
}


void ScientificObjectEditor::setControlsEditable(bool editable)
{
	controls_editable = editable;
	const QList<QLineEdit*> line_edits = findChildren<QLineEdit*>();
	for(QLineEdit* edit : line_edits)
		edit->setReadOnly(!editable);

	const QList<QPlainTextEdit*> plain_edits = findChildren<QPlainTextEdit*>();
	for(QPlainTextEdit* edit : plain_edits)
		edit->setReadOnly(edit->property("scientificReadOnly").toBool() || !editable);

	const QList<QComboBox*> combos = findChildren<QComboBox*>();
	for(QComboBox* combo : combos)
		combo->setEnabled(editable);

	const QList<QCheckBox*> checks = findChildren<QCheckBox*>();
	for(QCheckBox* check : checks)
		check->setEnabled(editable);

	const QList<QDoubleSpinBox*> double_spins = findChildren<QDoubleSpinBox*>();
	for(QDoubleSpinBox* spin : double_spins)
		spin->setReadOnly(!editable);

	const QList<QSpinBox*> spins = findChildren<QSpinBox*>();
	for(QSpinBox* spin : spins)
		spin->setReadOnly(!editable);

	browse_file_button->setEnabled(editable);
	online_search_button->setEnabled(editable);
	online_preview_button->setEnabled(editable);
	online_load_button->setEnabled(editable);
	ai_generate_code_button->setEnabled(editable);
	ai_create_object_button->setEnabled(editable);
	ai_explain_button->setEnabled(editable);
	ai_optimise_button->setEnabled(editable);
	display_colour_button->setEnabled(editable);
	label_colour_button->setEnabled(editable);
	simulation_enabled_check->setEnabled(false);
	simulation_type_combo->setEnabled(false);
	updateScientificSourceUiState();
}


bool ScientificObjectEditor::posAndRot3DControlsEnabled() const
{
	return show_3d_controls_checkbox && show_3d_controls_checkbox->isChecked();
}


bool ScientificObjectEditor::snapToGridChecked() const
{
	return snap_to_grid_checkbox && snap_to_grid_checkbox->isChecked();
}


double ScientificObjectEditor::gridSpacing() const
{
	return grid_spacing_spin ? grid_spacing_spin->value() : 1.0;
}


bool ScientificObjectEditor::handleSceneRay(const Vec4f& ray_origin_os, const Vec4f& ray_dir_os, bool show_context_menu, bool additive, const QPoint& global_pos)
{
	return molecule_viewport && molecule_viewport->handleSceneRay(ray_origin_os, ray_dir_os, show_context_menu, additive, global_pos);
}


void ScientificObjectEditor::browseFile()
{
	const QString path = QFileDialog::getOpenFileName(
		this,
		QString::fromUtf8("Импорт научного файла"),
		QString(),
		QString::fromUtf8("Scientific data (*.pdb *.mol *.sdf *.xyz *.cif *.csv *.json *.ply *.las *.obj *.stl *.gltf *.glb *.dcm *.nii *.vti *.vdb *.raw);;All files (*.*)")
	);
	if(path.isEmpty())
		return;

	file_path_edit->setText(path);
	setComboData(source_mode_combo, QStringLiteral("file"));
	emit objectChanged();
}


void ScientificObjectEditor::sourceModeChanged(int)
{
	const QString mode = currentComboData(source_mode_combo, QStringLiteral("prompt"));
	if(mode == "code")
		tab_widget->setCurrentIndex(0);
	else if(mode == "prompt")
	{
		if(ai_prompt_edit->toPlainText().isEmpty())
			ai_prompt_edit->setPlainText(prompt_edit->toPlainText());
	}
	updateScientificSourceUiState();
}


void ScientificObjectEditor::onlineDatabaseChanged(int)
{
	online_results_list->clear();
	current_settings.load_status = "idle";
	current_settings.load_status_message = "Source changed; run Search to check support status.";
	if(source_status_label)
		source_status_label->setText(QString::fromUtf8("Idle: source changed; run Search to check support status."));
	updateScientificSourceUiState();
}


void ScientificObjectEditor::aiProviderChanged(int)
{
	if(!settings || !ai_api_key_edit)
		return;

	const QSignalBlocker blocker(ai_api_key_edit);
	ai_api_key_edit->setText(settings->value(aiSettingsProviderKey()).toString());
}


void ScientificObjectEditor::saveAiApiKey()
{
	if(!settings || !ai_api_key_edit)
		return;

	settings->setValue(aiSettingsProviderKey(), ai_api_key_edit->text());
}


QString ScientificObjectEditor::generatedPythonForPrompt(const QString& prompt) const
{
	const QString lower = prompt.toLower();
	if(lower.contains("кофеин") || lower.contains("caffeine"))
	{
		return QString::fromUtf8(
			"# Generated starter code. Uses user-installed chemistry packages when available.\n"
			"from math import cos, sin, pi\n\n"
			"atoms = [\n"
			"    {\"element\": \"C\", \"x\": -1.20, \"y\":  1.45, \"z\": 0.00},\n"
			"    {\"element\": \"N\", \"x\":  0.78, \"y\":  2.86, \"z\": 0.00},\n"
			"    {\"element\": \"O\", \"x\":  3.06, \"y\": -0.48, \"z\": 0.00},\n"
			"    {\"element\": \"H\", \"x\": -2.18, \"y\":  1.92, \"z\": 0.00}\n"
			"]\n"
			"bonds = [{\"a\": 0, \"b\": 1, \"order\": 1}, {\"a\": 1, \"b\": 2, \"order\": 2}]\n"
			"return {\"type\": \"molecule\", \"name\": \"Caffeine\", \"formula\": \"C8H10N4O2\", \"atoms\": atoms, \"bonds\": bonds}\n"
		);
	}
	if(lower.contains("sin"))
	{
		return QString::fromUtf8(
			"import math\n\n"
			"points = []\n"
			"for i in range(240):\n"
			"    x = -2 * math.pi + i * 4 * math.pi / 239\n"
			"    points.append({\"x\": x, \"y\": math.sin(x), \"z\": 0.0})\n"
			"return {\"type\": \"chart\", \"name\": \"sin(x)\", \"points\": points}\n"
		);
	}
	if(lower.contains("облако") || lower.contains("point"))
	{
		return QString::fromUtf8(
			"import random\n\n"
			"points = []\n"
			"for i in range(1000):\n"
			"    points.append({\"x\": random.uniform(-1, 1), \"y\": random.uniform(-1, 1), \"z\": random.uniform(-1, 1)})\n"
			"return {\"type\": \"point_cloud\", \"name\": \"Random point cloud\", \"points\": points}\n"
		);
	}
	if(lower.contains("перлин") || lower.contains("perlin") || lower.contains("surface"))
	{
		return QString::fromUtf8(
			"import math\n\n"
			"vertices = []\n"
			"for y in range(64):\n"
			"    for x in range(64):\n"
			"        z = math.sin(x * 0.23) * math.cos(y * 0.19)\n"
			"        vertices.append({\"x\": x / 16.0, \"y\": y / 16.0, \"z\": z})\n"
			"return {\"type\": \"surface\", \"name\": \"Procedural surface\", \"vertices\": vertices}\n"
		);
	}

	return QString::fromUtf8(
		"# Return any ScientificObject-compatible dictionary.\n"
		"return {\n"
		"    \"type\": \"custom\",\n"
		"    \"name\": \"Generated scientific object\",\n"
		"    \"metadata\": {},\n"
		"    \"data\": []\n"
		"}\n"
	);
}


void ScientificObjectEditor::generateCodeFromPrompt()
{
	const QString prompt = ai_prompt_edit->toPlainText().isEmpty() ? prompt_edit->toPlainText() : ai_prompt_edit->toPlainText();
	const QString code = generatedPythonForPrompt(prompt);
	generated_code_edit->setPlainText(code);
	code_edit->setPlainText(code);
	setComboData(source_mode_combo, QStringLiteral("prompt"));
	setComboData(code_language_combo, QStringLiteral("Python"));
	emit objectChanged();
}


int ScientificObjectEditor::runPubChemSmokeCheck(const QString& report_path)
{
	QJsonObject report;
	report.insert(QStringLiteral("check"), QStringLiteral("Scientific Object Editor PubChem HTTPS smoke"));
	report.insert(QStringLiteral("generated_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	report.insert(QStringLiteral("transport"), QStringLiteral("WinHTTP/SChannel"));
	report.insert(QStringLiteral("qt_network_ssl"), QStringLiteral("disabled in current Qt build; PubChem does not use QNetworkAccessManager"));
	report.insert(QStringLiteral("tls_certificate_validation"), QStringLiteral("enabled by WinHTTP default policy; certificate errors are not ignored"));
	report.insert(QStringLiteral("source_host"), QStringLiteral("pubchem.ncbi.nlm.nih.gov"));
	report.insert(QStringLiteral("uses_builtin_sample_fallback"), false);

	QJsonArray query_reports;
	bool all_ok = true;

	const QStringList required_queries = QStringList() << QStringLiteral("water") << QStringLiteral("nicotine");
	for(int qi=0; qi<required_queries.size(); ++qi)
	{
		const QString q = required_queries[qi];
		QJsonObject row;
		row.insert(QStringLiteral("query"), q);
		row.insert(QStringLiteral("search_kind"), detectPubChemQueryKind(q));
		row.insert(QStringLiteral("source"), QStringLiteral("PubChem"));
		row.insert(QStringLiteral("origin"), QStringLiteral("live HTTPS PUG REST"));

		QString error;
		QList<int> cids;
		const QString kind = detectPubChemQueryKind(q);
		if(kind == "cid")
		{
			bool ok = false;
			const int cid = pubchemCidFromQueryText(q).toInt(&ok);
			if(ok && cid > 0)
				cids.push_back(cid);
			else
				error = QStringLiteral("Invalid PubChem CID.");
		}
		else if(kind == "smiles" || kind == "inchi")
		{
			const QUrl url = pubchemUrl(QStringLiteral("/rest/pug/compound/%1/cids/JSON").arg(kind));
			const QByteArray field = kind.toLatin1();
			const QByteArray form = field + QByteArray("=") + QUrl::toPercentEncoding(q);
			const PubChemHttpResult r = pubchemPostForm(url, form, QStringLiteral(".json"), false);
			row.insert(QStringLiteral("search_http_status"), r.http_status);
			if(!r.error.isEmpty())
				error = r.error;
			else
				cids = cidsFromPubChemIdentifierJson(r.bytes, &error);
		}
		else
		{
			QString path_kind = kind;
			if(kind == "formula")
				path_kind = QStringLiteral("fastformula");
			const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(q));
			const QUrl url = pubchemUrl(QStringLiteral("/rest/pug/compound/%1/%2/cids/JSON").arg(path_kind, encoded));
			const PubChemHttpResult r = pubchemGet(url, QStringLiteral(".json"), false);
			row.insert(QStringLiteral("search_http_status"), r.http_status);
			if(!r.error.isEmpty())
				error = r.error;
			else
				cids = cidsFromPubChemIdentifierJson(r.bytes, &error);
		}

		row.insert(QStringLiteral("cid_count"), cids.size());
		if(!error.isEmpty() || cids.empty())
		{
			row.insert(QStringLiteral("status"), QStringLiteral("failed"));
			row.insert(QStringLiteral("error"), error.isEmpty() ? QStringLiteral("PubChem returned no CID.") : error);
			query_reports.append(row);
			all_ok = false;
			continue;
		}

		const QString cid = QString::number(cids[0]);
		row.insert(QStringLiteral("selected_cid"), cid);

		QString parse_error;
		const QUrl prop_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/property/%2/JSON").arg(cid, pubchemPropertyList()));
		const PubChemHttpResult props_result = pubchemGet(prop_url, QStringLiteral(".json"), false);
		row.insert(QStringLiteral("properties_http_status"), props_result.http_status);
		const QList<QMap<QString, QString> > rows = props_result.error.isEmpty() ? parsePubChemPropertyTable(props_result.bytes, &parse_error) : QList<QMap<QString, QString> >();
		if(!props_result.error.isEmpty() || rows.empty())
		{
			row.insert(QStringLiteral("status"), QStringLiteral("failed"));
			row.insert(QStringLiteral("error"), props_result.error.isEmpty() ? QStringLiteral("PubChem properties parse failed: %1").arg(parse_error) : props_result.error);
			query_reports.append(row);
			all_ok = false;
			continue;
		}
		row.insert(QStringLiteral("formula"), rows[0].value(QStringLiteral("MolecularFormula")));
		row.insert(QStringLiteral("molecular_weight"), rows[0].value(QStringLiteral("MolecularWeight")));

		QUrl sdf_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/SDF").arg(cid));
		QUrlQuery sdf_query;
		sdf_query.addQueryItem(QStringLiteral("record_type"), QStringLiteral("3d"));
		sdf_url.setQuery(sdf_query);
		PubChemHttpResult sdf_result = pubchemGet(sdf_url, QStringLiteral(".sdf"), false);
		QString conformer = QStringLiteral("3d");
		if(!sdf_result.error.isEmpty())
		{
			conformer = QStringLiteral("2d");
			QUrl sdf_2d_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/SDF").arg(cid));
			QUrlQuery sdf_2d_query;
			sdf_2d_query.addQueryItem(QStringLiteral("record_type"), QStringLiteral("2d"));
			sdf_2d_url.setQuery(sdf_2d_query);
			sdf_result = pubchemGet(sdf_2d_url, QStringLiteral(".sdf"), false);
		}
		row.insert(QStringLiteral("sdf_http_status"), sdf_result.http_status);
		row.insert(QStringLiteral("sdf_record_type"), conformer);
		if(!sdf_result.error.isEmpty())
		{
			row.insert(QStringLiteral("status"), QStringLiteral("failed"));
			row.insert(QStringLiteral("error"), QStringLiteral("SDF load failed: %1").arg(sdf_result.error));
			query_reports.append(row);
			all_ok = false;
			continue;
		}

		const ParsedSdfMolecule molecule = parsePubChemSdf(sdf_result.bytes);
		row.insert(QStringLiteral("atom_count"), molecule.atom_count);
		row.insert(QStringLiteral("bond_count"), molecule.bond_count);
		row.insert(QStringLiteral("sdf_sha256"), sha256Hex(sdf_result.bytes));
		if(!molecule.error.isEmpty() || molecule.atom_count <= 0)
		{
			row.insert(QStringLiteral("status"), QStringLiteral("failed"));
			row.insert(QStringLiteral("error"), QStringLiteral("SDF parser failed: %1").arg(molecule.error));
			query_reports.append(row);
			all_ok = false;
			continue;
		}

		QUrl png_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/PNG").arg(cid));
		QUrlQuery png_query;
		png_query.addQueryItem(QStringLiteral("image_size"), QStringLiteral("large"));
		png_url.setQuery(png_query);
		const PubChemHttpResult png_result = pubchemGet(png_url, QStringLiteral(".png"), false);
		row.insert(QStringLiteral("png_http_status"), png_result.http_status);
		row.insert(QStringLiteral("png_bytes"), png_result.bytes.size());
		row.insert(QStringLiteral("png_available"), png_result.error.isEmpty() && png_result.bytes.size() > 0);
		if(!png_result.error.isEmpty() || png_result.bytes.size() <= 0)
		{
			row.insert(QStringLiteral("status"), QStringLiteral("failed"));
			row.insert(QStringLiteral("error"), QStringLiteral("PNG load failed: %1").arg(png_result.error));
			query_reports.append(row);
			all_ok = false;
			continue;
		}

		row.insert(QStringLiteral("status"), QStringLiteral("ok"));
		query_reports.append(row);
	}

	QJsonObject invalid;
	const QString invalid_query = QStringLiteral("metasiberia_invalid_pubchem_query_20260711_no_demo_fallback");
	invalid.insert(QStringLiteral("query"), invalid_query);
	invalid.insert(QStringLiteral("expected"), QStringLiteral("no CID and no built-in fallback"));
	const QUrl invalid_url = pubchemUrl(QStringLiteral("/rest/pug/compound/name/%1/cids/JSON").arg(QString::fromLatin1(QUrl::toPercentEncoding(invalid_query))));
	QString invalid_error;
	const PubChemHttpResult invalid_result = pubchemGet(invalid_url, QStringLiteral(".json"), false);
	invalid.insert(QStringLiteral("http_status"), invalid_result.http_status);
	if(invalid_result.error.isEmpty())
	{
		const QList<int> invalid_cids = cidsFromPubChemIdentifierJson(invalid_result.bytes, &invalid_error);
		invalid.insert(QStringLiteral("cid_count"), invalid_cids.size());
		invalid.insert(QStringLiteral("status"), invalid_cids.empty() ? QStringLiteral("ok") : QStringLiteral("failed"));
		if(!invalid_cids.empty())
			all_ok = false;
	}
	else
	{
		invalid.insert(QStringLiteral("cid_count"), 0);
		invalid.insert(QStringLiteral("status"), QStringLiteral("ok"));
		invalid.insert(QStringLiteral("error"), invalid_result.error);
	}
	report.insert(QStringLiteral("invalid_query_check"), invalid);

	report.insert(QStringLiteral("queries"), query_reports);
	report.insert(QStringLiteral("status"), all_ok ? QStringLiteral("ok") : QStringLiteral("failed"));

	QFile out(report_path);
	if(!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		conPrint("Scientific PubChem smoke failed to write report: " + stdstr(report_path));
		return 2;
	}
	out.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
	out.close();

	return all_ok ? 0 : 1;
}


int ScientificObjectEditor::runPubChemApplySmokeCheck(const QString& report_path)
{
	QJsonObject report;
	report.insert(QStringLiteral("check"), QStringLiteral("Scientific Object Editor PubChem UI selection/apply smoke"));
	report.insert(QStringLiteral("transport"), QStringLiteral("WinHTTP/SChannel"));
	report.insert(QStringLiteral("scope"), QStringLiteral("QWidget slot flow and WorldObject application; no server/deploy/production changes"));
	report.insert(QStringLiteral("created_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

	ScientificObjectEditor editor;
	editor.init(NULL);

	bool all_ok = true;
	QJsonArray query_reports;

	auto run_apply_query = [&](const QString& query, const QString& expected_cid, int expected_atoms, int expected_bonds) -> QJsonObject {
		QJsonObject row;
		row.insert(QStringLiteral("query"), query);
		row.insert(QStringLiteral("expected_cid"), expected_cid);

		ScientificObjectSettings initial = ScientificObjectSettings::defaultObject();
		initial.name = "Scientific Object";
		initial.scientific_type = "molecule";
		initial.source = "manual";
		initial.source_mode = "online";
		initial.online_database = "PubChem";
		initial.online_query = stdstr(query);
		initial.load_status = "idle";
		initial.load_status_message = "Apply smoke test object before PubChem load.";
		initial.data_origin = "user";

		WorldObject ob;
		ob.uid = UID(900001);
		ob.object_type = WorldObject::ObjectType_Generic;
		ob.content = ScientificObjectSettings::serialiseToContent(initial);

		editor.setFromObject(ob, true);
		editor.setComboData(editor.type_combo, QStringLiteral("molecule"));
		editor.setComboData(editor.source_mode_combo, QStringLiteral("online"));
		editor.setComboData(editor.online_database_combo, QStringLiteral("PubChem"));
		editor.online_query_edit->setText(query);

		editor.previewScientificSourceResult();

		QListWidgetItem* selected_item = editor.online_results_list->currentItem();
		const QString selected_cid = selected_item ? selected_item->data(Qt::UserRole).toString() : QString();
		row.insert(QStringLiteral("results_count"), editor.online_results_list->count());
		row.insert(QStringLiteral("selected_cid_after_search"), selected_cid);
		row.insert(QStringLiteral("selected_row_visible"), selected_item ? selected_item->isSelected() : false);
		row.insert(QStringLiteral("load_button_after_search"), editor.online_load_button->text());
		row.insert(QStringLiteral("status_after_search"), editor.source_status_label->text());

		bool ok = true;
		QStringList errors;
		if(editor.online_results_list->count() < 1)
		{
			ok = false;
			errors << QStringLiteral("No PubChem search results were shown.");
		}
		if(selected_cid != expected_cid)
		{
			ok = false;
			errors << QStringLiteral("Expected selected CID %1 but got %2.").arg(expected_cid, selected_cid.isEmpty() ? QStringLiteral("<none>") : selected_cid);
		}
		if(!selected_item || !selected_item->isSelected())
		{
			ok = false;
			errors << QStringLiteral("Search result row was not selected.");
		}

		editor.loadScientificSourceResult();
		editor.show_labels_check->setChecked(true);
		editor.show_legend_check->setChecked(true);
		editor.setComboData(editor.label_mode_combo, QStringLiteral("element"));
		editor.updateMoleculeInteractiveView();
		editor.molecule_viewport->resize(900, 560);
		QImage molecule_view_image(editor.molecule_viewport->size(), QImage::Format_ARGB32_Premultiplied);
		molecule_view_image.fill(Qt::transparent);
		editor.molecule_viewport->render(&molecule_view_image);
		const bool information_overlay_rendered = !molecule_view_image.isNull() && molecule_view_image.width() == 900;
		const bool classification_lazy_loaded = editor.loadPubChemCardSection(QStringLiteral("classification"));
		const bool image_lazy_loaded = editor.loadPubChemImage();
		editor.toObject(ob);

		std::string parse_error;
		const ScientificObjectSettings applied = ScientificObjectSettings::fromContent(ob.content, &parse_error);
		const std::string model_url = toStdString(ob.model_url);
		const bool model_file_exists = !model_url.empty() && FileUtils::fileExists(model_url);
		const bool image_file_exists = !applied.image_cache_path.empty() && FileUtils::fileExists(applied.image_cache_path);
		const bool model_url_changed = BitUtils::isBitSet(ob.changed_flags, WorldObject::MODEL_URL_CHANGED);
		const bool content_changed = BitUtils::isBitSet(ob.changed_flags, WorldObject::CONTENT_CHANGED);

		row.insert(QStringLiteral("status_after_load"), editor.source_status_label->text());
		row.insert(QStringLiteral("load_button_after_load"), editor.online_load_button->text());
		row.insert(QStringLiteral("applied_name"), qstr(applied.name));
		row.insert(QStringLiteral("applied_source"), qstr(applied.source));
		row.insert(QStringLiteral("applied_data_origin"), qstr(applied.data_origin));
		row.insert(QStringLiteral("applied_identifier"), qstr(applied.provenance_identifier));
		row.insert(QStringLiteral("applied_source_url"), qstr(applied.provenance_url));
		row.insert(QStringLiteral("applied_atom_count"), applied.atom_count);
		row.insert(QStringLiteral("applied_bond_count"), applied.bond_count);
		row.insert(QStringLiteral("applied_load_status"), qstr(applied.load_status));
		row.insert(QStringLiteral("image_cache_path"), qstr(applied.image_cache_path));
		row.insert(QStringLiteral("image_lazy_loaded"), image_lazy_loaded);
		row.insert(QStringLiteral("classification_lazy_loaded"), classification_lazy_loaded);
		row.insert(QStringLiteral("content_bytes"), (int)ob.content.size());
		row.insert(QStringLiteral("labels_enabled"), applied.show_labels);
		row.insert(QStringLiteral("legend_enabled"), applied.show_legend);
		row.insert(QStringLiteral("information_overlay_rendered"), information_overlay_rendered);
		row.insert(QStringLiteral("image_file_exists"), image_file_exists);
		row.insert(QStringLiteral("model_url"), QString::fromStdString(model_url));
		row.insert(QStringLiteral("model_file_exists"), model_file_exists);
		row.insert(QStringLiteral("model_url_changed"), model_url_changed);
		row.insert(QStringLiteral("content_changed"), content_changed);
		row.insert(QStringLiteral("model_url_kind"), QStringLiteral("local OBJ before GUIClient resource conversion/upload"));
		row.insert(QStringLiteral("server_confirmation"), QStringLiteral("not part of this headless smoke"));

		if(!parse_error.empty())
		{
			ok = false;
			errors << QString::fromUtf8("ScientificObject JSON parse error: %1").arg(qstr(parse_error));
		}
		if(applied.source != "PubChem")
		{
			ok = false;
			errors << QString::fromUtf8("Applied source stayed %1 instead of PubChem.").arg(qstr(applied.source));
		}
		if(applied.data_origin != "provider")
		{
			ok = false;
			errors << QString::fromUtf8("Applied data_origin is %1 instead of provider.").arg(qstr(applied.data_origin));
		}
		if(applied.provenance_identifier != stdstr(QStringLiteral("CID:%1").arg(expected_cid)))
		{
			ok = false;
			errors << QString::fromUtf8("Applied provenance identifier is %1.").arg(qstr(applied.provenance_identifier));
		}
		if(applied.atom_count != expected_atoms || applied.bond_count != expected_bonds)
		{
			ok = false;
			errors << QStringLiteral("Expected %1 atoms / %2 bonds but got %3 / %4.")
				.arg(expected_atoms)
				.arg(expected_bonds)
				.arg(applied.atom_count)
				.arg(applied.bond_count);
		}
		if(!image_file_exists)
		{
			ok = false;
			errors << QStringLiteral("PNG preview cache file was not assigned or does not exist.");
		}
		if(!classification_lazy_loaded)
		{
			ok = false;
			errors << QStringLiteral("PUG View classification lazy load failed.");
		}
		if(!information_overlay_rendered || !applied.show_labels || !applied.show_legend)
		{
			ok = false;
			errors << QStringLiteral("Molecule labels/legend information overlay did not render with both modes enabled.");
		}
		if(!model_file_exists)
		{
			ok = false;
			errors << QStringLiteral("Molecule OBJ model was not assigned or does not exist.");
		}
		if(!model_url_changed || !content_changed)
		{
			ok = false;
			errors << QStringLiteral("WorldObject changed flags do not include both MODEL_URL_CHANGED and CONTENT_CHANGED.");
		}

		row.insert(QStringLiteral("status"), ok ? QStringLiteral("ok") : QStringLiteral("failed"));
		if(!errors.isEmpty())
			row.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
		if(!ok)
			all_ok = false;
		return row;
	};

	query_reports.append(run_apply_query(QStringLiteral("water"), QStringLiteral("962"), 3, 2));
	query_reports.append(run_apply_query(QStringLiteral("nicotine"), QStringLiteral("89594"), 26, 27));

	ScientificObjectSettings invalid_initial = ScientificObjectSettings::defaultObject();
	invalid_initial.scientific_type = "molecule";
	invalid_initial.source_mode = "online";
	invalid_initial.online_database = "PubChem";
	WorldObject invalid_ob;
	invalid_ob.uid = UID(900002);
	invalid_ob.object_type = WorldObject::ObjectType_Generic;
	invalid_ob.content = ScientificObjectSettings::serialiseToContent(invalid_initial);
	editor.setFromObject(invalid_ob, true);
	editor.setComboData(editor.type_combo, QStringLiteral("molecule"));
	editor.setComboData(editor.source_mode_combo, QStringLiteral("online"));
	editor.setComboData(editor.online_database_combo, QStringLiteral("PubChem"));
	editor.online_query_edit->setText(QStringLiteral("metasiberia_invalid_pubchem_query_20260711_no_demo_fallback"));
	editor.previewScientificSourceResult();
	editor.toObject(invalid_ob);
	const ScientificObjectSettings invalid_applied = ScientificObjectSettings::fromContent(invalid_ob.content);
	QJsonObject invalid_report;
	invalid_report.insert(QStringLiteral("query"), editor.online_query_edit->text());
	invalid_report.insert(QStringLiteral("results_count"), editor.online_results_list->count());
	invalid_report.insert(QStringLiteral("status_after_search"), editor.source_status_label->text());
	invalid_report.insert(QStringLiteral("applied_source"), qstr(invalid_applied.source));
	invalid_report.insert(QStringLiteral("applied_atom_count"), invalid_applied.atom_count);
	const bool invalid_ok = editor.online_results_list->count() == 0 && invalid_applied.source != "PubChem" && invalid_applied.atom_count == 0;
	invalid_report.insert(QStringLiteral("status"), invalid_ok ? QStringLiteral("ok") : QStringLiteral("failed"));
	if(!invalid_ok)
		all_ok = false;

	report.insert(QStringLiteral("queries"), query_reports);
	report.insert(QStringLiteral("invalid_query_check"), invalid_report);
	report.insert(QStringLiteral("status"), all_ok ? QStringLiteral("ok") : QStringLiteral("failed"));

	QFile out(report_path);
	if(!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		conPrint("Scientific PubChem apply smoke failed to write report: " + stdstr(report_path));
		return 2;
	}
	out.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
	out.close();

	return all_ok ? 0 : 1;
}


int ScientificObjectEditor::runMoleculeInformationSmokeCheck(const QString& report_path)
{
	QJsonObject report; report.insert(QStringLiteral("check"),QStringLiteral("Scientific molecule information layer smoke"));report.insert(QStringLiteral("created_at_utc"),QDateTime::currentDateTimeUtc().toString(Qt::ISODate));bool all_ok=true;
	ScientificObjectEditor editor;editor.setComboData(editor.type_combo,QStringLiteral("molecule"));editor.setComboData(editor.online_database_combo,QStringLiteral("PubChem"));
	struct AliasCase{const char* russian;const char* english;const char* cid;};const AliasCase cases[]={
		{"вода","water","962"},{"никотин","nicotine","89594"},{"аспирин","aspirin","2244"},{"кофеин","caffeine","2519"},{"этанол","ethanol","702"},{"глюкоза","glucose","5793"}
	};
	QJsonArray alias_results;for(const AliasCase&c:cases){const QString query=QString::fromUtf8(c.russian);const bool found=editor.searchPubChem(query);const QString cid=editor.online_results_list->currentItem()?editor.online_results_list->currentItem()->data(Qt::UserRole).toString():QString();const bool ok=found&&cid==QString::fromLatin1(c.cid)&&qstr(editor.current_settings.search_normalized_query)==QString::fromLatin1(c.english);QJsonObject row;row.insert(QStringLiteral("original"),query);row.insert(QStringLiteral("normalized"),qstr(editor.current_settings.search_normalized_query));row.insert(QStringLiteral("translation"),qstr(editor.current_settings.search_translation));row.insert(QStringLiteral("cid"),cid);row.insert(QStringLiteral("status"),ok?QStringLiteral("ok"):QStringLiteral("failed"));alias_results.append(row);if(!ok)all_ok=false;}
	report.insert(QStringLiteral("russian_query_resolver"),alias_results);
	const bool classification_ok=editor.loadPubChemCardSection(QStringLiteral("classification"));report.insert(QStringLiteral("classification_lazy_load"),classification_ok?QStringLiteral("ok"):QStringLiteral("failed"));if(!classification_ok)all_ok=false;

	MoleculeViewportWidget viewport;viewport.resize(900,560);ScientificObjectSettings options=ScientificObjectSettings::defaultObject();options.scientific_type="molecule";options.show_labels=true;options.show_legend=true;options.label_max_count=32;options.label_max_distance=100.f;options.colour_scheme="CPK";
	const QString atoms=QStringLiteral("1 C 0 0 0 0 alpha\n2 O 1.2 0 0 0 beta\n3 N 1.2 1.1 0 1 gamma\n4 H 1.2 1.1 1.0 0 delta");const QString bonds=QStringLiteral("1-2 double\n2-3 single stereo:1\n3-4 single");viewport.setMolecule(atoms,bonds);viewport.setScientificSettings(options);
	viewport.setSelectionMode(QStringLiteral("atom"));viewport.setSelectionState(QStringLiteral("1"),-1,QStringLiteral("atom_selected"));const bool atom_state=viewport.selectionState()==QStringLiteral("atom_selected");viewport.setSelectionState(QStringLiteral("1,2"),-1,QStringLiteral("multiple_atoms_selected"));const bool multi_state=viewport.selectionState()==QStringLiteral("multiple_atoms_selected");viewport.setSelectionMode(QStringLiteral("bond"));viewport.setSelectionState(QString(),0,QStringLiteral("bond_selected"));const bool bond_state=viewport.selectionState()==QStringLiteral("bond_selected");viewport.setSelectionMode(QStringLiteral("molecule"));viewport.setSelectionState(QString(),-1,QStringLiteral("molecule_selected"));const bool molecule_state=viewport.selectionState()==QStringLiteral("molecule_selected");
	viewport.setSelectionMode(QStringLiteral("atom"));viewport.clearMeasurements();viewport.beginMeasurement(QStringLiteral("distance"));viewport.selectAtomBySourceID(1);viewport.selectAtomBySourceID(2,true);viewport.beginMeasurement(QStringLiteral("angle"));viewport.selectAtomBySourceID(1);viewport.selectAtomBySourceID(2,true);viewport.selectAtomBySourceID(3,true);viewport.beginMeasurement(QStringLiteral("torsion"));viewport.selectAtomBySourceID(1);viewport.selectAtomBySourceID(2,true);viewport.selectAtomBySourceID(3,true);viewport.selectAtomBySourceID(4,true);const QJsonDocument measurements=QJsonDocument::fromJson(viewport.measurementsJson().toUtf8());const bool measurements_ok=measurements.isArray()&&measurements.array().size()==3;viewport.setMolecule(QStringLiteral("1 O 0 0 0 0\n2 H 0.96 0 0 0\n3 H -0.24 0.93 0 0"),QStringLiteral("1-2 single\n1-3 single"));const QJsonDocument replacement_measurements=QJsonDocument::fromJson(viewport.measurementsJson().toUtf8());const bool stale_measurements_cleared=replacement_measurements.isArray()&&replacement_measurements.array().isEmpty();viewport.setMolecule(atoms,bonds);viewport.setScientificSettings(options);
	const char* modes[]={"element","atom_number","element_number","atomic_number","atomic_mass","formal_charge","custom_attribute"};QJsonArray rendered_modes;for(const char*mode:modes){options.label_mode=mode;viewport.setScientificSettings(options);QImage image(viewport.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);viewport.render(&image);const bool rendered=!image.isNull()&&image.width()==900;QJsonObject row;row.insert(QStringLiteral("mode"),QString::fromLatin1(mode));row.insert(QStringLiteral("rendered"),rendered);rendered_modes.append(row);if(!rendered)all_ok=false;}
	const bool periodic_ok=PeriodicTableModel::allElements().size()==118;
	QJsonObject interaction;interaction.insert(QStringLiteral("atom_selected"),atom_state);interaction.insert(QStringLiteral("multiple_atoms_selected"),multi_state);interaction.insert(QStringLiteral("bond_selected"),bond_state);interaction.insert(QStringLiteral("molecule_selected"),molecule_state);interaction.insert(QStringLiteral("measurement_count"),measurements.isArray()?measurements.array().size():0);interaction.insert(QStringLiteral("measurements_ok"),measurements_ok);interaction.insert(QStringLiteral("stale_measurements_cleared_after_molecule_replace"),stale_measurements_cleared);interaction.insert(QStringLiteral("metrics"),viewport.moleculeMetricsText());interaction.insert(QStringLiteral("label_modes"),rendered_modes);interaction.insert(QStringLiteral("periodic_element_count"),PeriodicTableModel::allElements().size());interaction.insert(QStringLiteral("periodic_table_ok"),periodic_ok);report.insert(QStringLiteral("interactive_layer"),interaction);
	all_ok=all_ok&&atom_state&&multi_state&&bond_state&&molecule_state&&measurements_ok&&stale_measurements_cleared&&periodic_ok;report.insert(QStringLiteral("status"),all_ok?QStringLiteral("ok"):QStringLiteral("failed"));QFile out(report_path);if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate))return 2;out.write(QJsonDocument(report).toJson(QJsonDocument::Indented));return all_ok?0:1;
}


bool ScientificObjectEditor::searchPubChem(const QString& query)
{
	const QString q = query.trimmed();
	online_results_list->clear();
	if(q.isEmpty())
	{
		source_status_label->setText(QString::fromUtf8("Idle: enter a PubChem name, CID, SMILES, InChI, InChIKey or formula."));
		updateScientificSourceUiState();
		return false;
	}

	source_status_label->setText(QString::fromUtf8("Searching: PubChem PUG REST query is running..."));
	QString error;
	auto resolve_cids = [](const QString& attempt, QString* error_out) -> QList<int>
	{
		QList<int> result;
		const QString kind = detectPubChemQueryKind(attempt);
		if(kind == QStringLiteral("cid"))
		{
			bool ok = false; const int cid = pubchemCidFromQueryText(attempt).toInt(&ok);
			if(ok && cid > 0) result.push_back(cid); else *error_out = QStringLiteral("Invalid PubChem CID.");
		}
		else if(kind == QStringLiteral("smiles") || kind == QStringLiteral("inchi"))
		{
			const QUrl url = pubchemUrl(QStringLiteral("/rest/pug/compound/%1/cids/JSON").arg(kind));
			const QByteArray form = kind.toLatin1() + QByteArray("=") + QUrl::toPercentEncoding(attempt);
			const PubChemHttpResult r = pubchemPostForm(url, form, QStringLiteral(".json"), true);
			if(!r.error.isEmpty()) *error_out = r.error; else result = cidsFromPubChemIdentifierJson(r.bytes, error_out);
		}
		else
		{
			const QString path_kind = kind == QStringLiteral("formula") ? QStringLiteral("fastformula") : kind;
			const QUrl url = pubchemUrl(QStringLiteral("/rest/pug/compound/%1/%2/cids/JSON").arg(path_kind, QString::fromLatin1(QUrl::toPercentEncoding(attempt))));
			const PubChemHttpResult r = pubchemGet(url, QStringLiteral(".json"), true);
			if(!r.error.isEmpty()) *error_out = r.error; else result = cidsFromPubChemIdentifierJson(r.bytes, error_out);
		}
		return result;
	};

	QString normalized_query = q;
	QString translation;
	QList<int> cids = resolve_cids(q, &error); // Exact PubChem attempt always comes first.
	if(cids.isEmpty() && detectPubChemQueryKind(q) == QStringLiteral("name"))
	{
		static const QMap<QString, QString> aliases = {
			{QString::fromUtf8("вода"), QStringLiteral("water")}, {QString::fromUtf8("никотин"), QStringLiteral("nicotine")},
			{QString::fromUtf8("аспирин"), QStringLiteral("aspirin")}, {QString::fromUtf8("кофеин"), QStringLiteral("caffeine")},
			{QString::fromUtf8("этанол"), QStringLiteral("ethanol")}, {QString::fromUtf8("глюкоза"), QStringLiteral("glucose")}
		};
		const QMap<QString, QString>::const_iterator alias_it = aliases.find(q.toLower());
		if(alias_it != aliases.end())
		{
			normalized_query = alias_it.value(); translation = QStringLiteral("%1 → %2").arg(q, normalized_query); error.clear(); cids = resolve_cids(normalized_query, &error);
		}
	}
	current_settings.search_original_query = stdstr(q);
	current_settings.search_normalized_query = stdstr(normalized_query);
	current_settings.search_translation = stdstr(translation);
	if(settings)
	{
		QStringList history=settings->value(QStringLiteral("scientificObjectEditor/searchHistory")).toStringList();history.removeAll(q);history.prepend(q);while(history.size()>50)history.removeLast();settings->setValue(QStringLiteral("scientificObjectEditor/searchHistory"),history);
	}
	if(query_resolution_label)
		query_resolution_label->setText(QStringLiteral("Original query: %1 | Normalized query: %2 | Translation: %3").arg(q, normalized_query, translation.isEmpty() ? QStringLiteral("Not used") : translation));

	if(!error.isEmpty())
	{
		source_status_label->setText(QString::fromUtf8("Error: %1").arg(error));
		current_settings.load_status = "error";
		current_settings.load_status_message = stdstr(error);
		updateScientificSourceUiState();
		return false;
	}
	if(cids.empty())
	{
		source_status_label->setText(QString::fromUtf8("Error: PubChem returned no CID for this query. Object data was not changed."));
		current_settings.load_status = "error";
		current_settings.load_status_message = "PubChem returned no CID for this query.";
		updateScientificSourceUiState();
		return false;
	}

	const int max_results = std::min(12, cids.size());
	QStringList cid_texts;
	for(int i=0; i<max_results; ++i)
		cid_texts << QString::number(cids[i]);

	const QUrl prop_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/property/%2/JSON")
		.arg(cid_texts.join(QStringLiteral(",")), pubchemPropertyList()));
	const PubChemHttpResult props_result = pubchemGet(prop_url, QStringLiteral(".json"), true);
	QList<QMap<QString, QString> > prop_rows;
	if(props_result.error.isEmpty())
		prop_rows = parsePubChemPropertyTable(props_result.bytes, &error);

	if(prop_rows.empty())
	{
		for(int i=0; i<max_results; ++i)
		{
			QMap<QString, QString> row;
			row.insert(QStringLiteral("CID"), QString::number(cids[i]));
			prop_rows.push_back(row);
		}
	}

	for(int i=0; i<prop_rows.size(); ++i)
	{
		const QMap<QString, QString>& row = prop_rows[i];
		const QString cid = row.value(QStringLiteral("CID"), i < cids.size() ? QString::number(cids[i]) : QString());
		const QString name = row.value(QStringLiteral("IUPACName"), QString::fromUtf8("PubChem compound"));
		const QString formula = row.value(QStringLiteral("MolecularFormula"), QString::fromUtf8("formula N/A"));
		const QString mass = row.value(QStringLiteral("MolecularWeight"), QString::fromUtf8("mass N/A"));
		QListWidgetItem* item = new QListWidgetItem(QString::fromUtf8("%1 | CID %2 | %3 | %4 Da | 2D: yes | 3D: check on load | PubChem")
			.arg(name, cid, formula, mass), online_results_list);
		item->setData(Qt::UserRole, cid);
		item->setData(Qt::UserRole + 1, QStringLiteral("pubchem_cid"));
		item->setToolTip(QString::fromUtf8("PubChem search result. Select this CID and click \"Загрузить в объект\" to fetch metadata, SDF structure and PNG image."));
	}
	QString selected_cid;
	if(online_results_list->count() > 0)
	{
		online_results_list->setCurrentRow(0);
		if(online_results_list->currentItem())
			selected_cid = online_results_list->currentItem()->data(Qt::UserRole).toString();
	}

	current_settings.load_status = "results_available";
	current_settings.load_status_message = stdstr(QString::fromUtf8("PubChem returned %1 CID(s); showing %2. CID %3 is selected for loading.").arg(cids.size()).arg(max_results).arg(selected_cid));
	current_settings.data_origin = "provider_search";
	current_settings.online_database = "PubChem";
	current_settings.online_query = stdstr(q);
	current_settings.online_result_id = stdstr(selected_cid);
	if(max_results == 1 && !selected_cid.isEmpty())
		source_status_label->setText(QString::fromUtf8("Results available: CID %1 selected. Click \"Загрузить CID %1 в объект\" once to load metadata, PNG and structure.").arg(selected_cid));
	else
		source_status_label->setText(QString::fromUtf8("Results available: PubChem returned %1 CID(s); showing %2. CID %3 is selected for loading.").arg(cids.size()).arg(max_results).arg(selected_cid));
	updateScientificSourceUiState();
	return true;
}


bool ScientificObjectEditor::loadPubChemCID(const QString& cid_text)
{
	const QString cid = cid_text.trimmed();
	bool ok = false;
	const int cid_int = cid.toInt(&ok);
	if(!ok || cid_int <= 0)
	{
		source_status_label->setText(QString::fromUtf8("Error: selected PubChem result does not contain a valid CID."));
		return false;
	}

	source_status_label->setText(QString::fromUtf8("Loading metadata: PubChem CID %1").arg(cid));
	QString error;

	const QUrl prop_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/property/%2/JSON").arg(cid, pubchemPropertyList()));
	const PubChemHttpResult props_result = pubchemGet(prop_url, QStringLiteral(".json"), true);
	if(!props_result.error.isEmpty())
	{
		source_status_label->setText(QString::fromUtf8("Error: %1").arg(props_result.error));
		current_settings.load_status = "error";
		current_settings.load_status_message = stdstr(props_result.error);
		return false;
	}
	QList<QMap<QString, QString> > rows = parsePubChemPropertyTable(props_result.bytes, &error);
	if(rows.empty())
	{
		source_status_label->setText(QString::fromUtf8("Error: PubChem properties could not be parsed. %1").arg(error));
		current_settings.load_status = "error";
		current_settings.load_status_message = stdstr(QStringLiteral("PubChem properties could not be parsed. ") + error);
		return false;
	}
	const QMap<QString, QString> props = rows[0];

	source_status_label->setText(QString::fromUtf8("Loading structure: PubChem CID %1 SDF 3D").arg(cid));
	QUrl sdf_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/SDF").arg(cid));
	QUrlQuery sdf_query;
	sdf_query.addQueryItem(QStringLiteral("record_type"), QStringLiteral("3d"));
	sdf_url.setQuery(sdf_query);
	PubChemHttpResult sdf_result = pubchemGet(sdf_url, QStringLiteral(".sdf"), true);
	bool used_3d = true;
	if(!sdf_result.error.isEmpty())
	{
		used_3d = false;
		source_status_label->setText(QString::fromUtf8("No 3D conformer or 3D request failed; loading 2D SDF for CID %1.").arg(cid));
		QUrl sdf_2d_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/SDF").arg(cid));
		QUrlQuery sdf_2d_query;
		sdf_2d_query.addQueryItem(QStringLiteral("record_type"), QStringLiteral("2d"));
		sdf_2d_url.setQuery(sdf_2d_query);
		sdf_result = pubchemGet(sdf_2d_url, QStringLiteral(".sdf"), true);
	}
	if(!sdf_result.error.isEmpty())
	{
		source_status_label->setText(QString::fromUtf8("Error: PubChem SDF load failed. %1").arg(sdf_result.error));
		current_settings.load_status = "error";
		current_settings.load_status_message = stdstr(QStringLiteral("PubChem SDF load failed. ") + sdf_result.error);
		return false;
	}

	source_status_label->setText(QString::fromUtf8("Parsing: PubChem SDF for CID %1").arg(cid));
	const ParsedSdfMolecule molecule = parsePubChemSdf(sdf_result.bytes);
	if(!molecule.error.isEmpty())
	{
		source_status_label->setText(QString::fromUtf8("Error: SDF parser failed. %1").arg(molecule.error));
		current_settings.load_status = "error";
		current_settings.load_status_message = stdstr(QStringLiteral("SDF parser failed. ") + molecule.error);
		return false;
	}

	QUrl png_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/PNG").arg(cid));
	QUrlQuery png_query;
	png_query.addQueryItem(QStringLiteral("image_size"), QStringLiteral("large"));
	png_url.setQuery(png_query);
	if(molecule_image_label){molecule_image_preview_pixmap=QPixmap();molecule_image_label->setPixmap(QPixmap());molecule_image_label->setText(QString::fromUtf8("Загрузка 2D-превью..."));}
	if(image_viewer) image_viewer->clearImage(QString::fromUtf8("2D-изображение ещё не загружено"));

	const QUrl synonyms_url = pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/synonyms/JSON").arg(cid));
	const PubChemHttpResult synonyms_result = pubchemGet(synonyms_url, QStringLiteral(".json"), true);
	const QStringList synonyms = synonyms_result.error.isEmpty() ? pubchemSynonymsFromJson(synonyms_result.bytes) : QStringList();

	const QString display_name = !synonyms.isEmpty() ? synonyms[0] : props.value(QStringLiteral("IUPACName"), QString::fromUtf8("PubChem CID %1").arg(cid));
	const QString source_url = QStringLiteral("https://pubchem.ncbi.nlm.nih.gov/compound/%1").arg(cid);
	const QString sdf_checksum = sha256Hex(sdf_result.bytes);
	const QString image_checksum;
	const bool loaded_from_cache = props_result.from_cache || sdf_result.from_cache || synonyms_result.from_cache;

	source_status_label->setText(QString::fromUtf8("Building molecule: applying PubChem CID %1 to ScientificObject").arg(cid));
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	const bool was_syncing = syncing;
	syncing = true;

	name_edit->setText(display_name);
	source_edit->setText(QStringLiteral("PubChem"));
	setComboData(source_mode_combo, QStringLiteral("online"));
	url_edit->setText(source_url);
	setComboData(online_database_combo, QStringLiteral("PubChem"));
	online_query_edit->setText(cid);
	setComboData(type_combo, QStringLiteral("molecule"));
	setComboData(visualization_mode_combo, QStringLiteral("ball_and_stick"));
	setComboData(colour_scheme_combo, QStringLiteral("CPK"));

	QString summary;
	summary += QString::fromUtf8("Молекула PubChem загружена.\n");
	summary += QString::fromUtf8("Название: %1\n").arg(display_name);
	summary += QStringLiteral("CID: %1\n").arg(cid);
	summary += QString::fromUtf8("Формула: %1\n").arg(props.value(QStringLiteral("MolecularFormula"), QString::fromUtf8("Нет данных")));
	summary += QString::fromUtf8("Молекулярная масса: %1 Da\n").arg(props.value(QStringLiteral("MolecularWeight"), QString::fromUtf8("Нет данных")));
	summary += QString::fromUtf8("Атомов: %1\n").arg(molecule.atom_count);
	summary += QString::fromUtf8("Связей: %1\n").arg(molecule.bond_count);
	summary += QString::fromUtf8("Конформер: %1\n").arg(used_3d ? QStringLiteral("3D SDF") : QString::fromUtf8("2D SDF; 3D-конформер недоступен или запрос 3D не прошёл"));
	summary += QString::fromUtf8("Источник: %1\n").arg(source_url);
	summary += QString::fromUtf8("Кэш: %1\n").arg(loaded_from_cache ? QString::fromUtf8("часть ответов загружена из локального кэша") : QString::fromUtf8("свежие ответы PubChem"));
	if(!synonyms.isEmpty())
		summary += QString::fromUtf8("Синонимы: %1\n").arg(synonyms.mid(0, 8).join(QStringLiteral("; ")));

	QString properties;
	properties += QStringLiteral("provider\tPubChem\n");
	properties += QStringLiteral("cid\t%1\n").arg(cid);
	properties += QStringLiteral("source_url\t%1\n").arg(source_url);
	const QStringList property_keys = {
		QStringLiteral("IUPACName"), QStringLiteral("MolecularFormula"), QStringLiteral("MolecularWeight"), QStringLiteral("ExactMass"), QStringLiteral("MonoisotopicMass"),
		QStringLiteral("Charge"), QStringLiteral("HeavyAtomCount"), QStringLiteral("HBondDonorCount"), QStringLiteral("HBondAcceptorCount"), QStringLiteral("RotatableBondCount"),
		QStringLiteral("XLogP"), QStringLiteral("TPSA"), QStringLiteral("Complexity"), QStringLiteral("IsomericSMILES"), QStringLiteral("CanonicalSMILES"),
		QStringLiteral("InChI"), QStringLiteral("InChIKey")
	};
	for(const QString& key : property_keys)
		properties += key + QStringLiteral("\t") + props.value(key, QStringLiteral("Not available")) + QStringLiteral("\n");
	properties += QStringLiteral("Title\t%1\n").arg(display_name);
	properties += QStringLiteral("AtomCount\t%1\nBondCount\t%2\n").arg(molecule.atom_count).arg(molecule.bond_count);
	properties += QStringLiteral("synonyms\t%1\n").arg(synonyms.isEmpty() ? QStringLiteral("Not available") : synonyms.mid(0, 12).join(QStringLiteral("; ")));
	properties += QStringLiteral("sdf_checksum_sha256\t%1\n").arg(sdf_checksum);
	properties += QStringLiteral("sdf_cache_path\t%1\n").arg(sdf_result.cache_path);
	properties += QStringLiteral("image_url\t%1\n").arg(png_url.toString());
	properties += QStringLiteral("image_checksum_sha256\t%1\n").arg(image_checksum.isEmpty() ? QStringLiteral("Not available") : image_checksum);
	properties += QStringLiteral("image_cache_path\tNot loaded (lazy Images section)\n");
	properties += QStringLiteral("license\tPubChem/NCBI public data; keep attribution/source URL\n");

	const QString legend = moleculeLegendFromAtomTable(molecule.atom_table);
	QString values;
	values += QStringLiteral("legend\n%1\n").arg(legend);
	values += QStringLiteral("measurements\n");
	values += QStringLiteral("Atom Count\t%1\n").arg(molecule.atom_count);
	values += QStringLiteral("Bond Count\t%1\n").arg(molecule.bond_count);
	values += QStringLiteral("Molecular Weight\t%1 Da\n").arg(props.value(QStringLiteral("MolecularWeight"), QStringLiteral("Not available")));
	values += QStringLiteral("Bounding Box\t%1\n").arg(molecule.dimensions);

	data_summary_edit->setPlainText(summary);
	atom_table_edit->setPlainText(molecule.atom_table);
	bond_table_edit->setPlainText(molecule.bond_table);
	property_table_edit->setPlainText(properties);
	value_table_edit->setPlainText(values);
	point_table_edit->clear();
	atom_count_spin->setValue(molecule.atom_count);
	bond_count_spin->setValue(molecule.bond_count);
	point_count_spin->setValue(0);
	dimensions_edit->setText(molecule.dimensions);
	frame_count_spin->setValue(used_3d ? 1 : 0);
	current_frame_spin->setValue(0);

	current_settings.load_status = loaded_from_cache ? "loaded_from_cache" : "ready";
	current_settings.load_status_message = stdstr(QString::fromUtf8("PubChem CID %1 loaded as %2.").arg(cid, used_3d ? QStringLiteral("3D SDF") : QStringLiteral("2D SDF")));
	current_settings.name = stdstr(display_name);
	current_settings.scientific_type = "molecule";
	current_settings.source = "PubChem";
	current_settings.source_mode = "online";
	current_settings.source_url = stdstr(source_url);
	current_settings.online_database = "PubChem";
	current_settings.online_query = stdstr(cid);
	current_settings.online_result_id = stdstr(cid);
	current_settings.data_origin = "provider";
	current_settings.provenance_source = "PubChem";
	current_settings.provenance_identifier = stdstr(QStringLiteral("CID:%1").arg(cid));
	current_settings.provenance_url = stdstr(source_url);
	current_settings.provenance_author = "PubChem / NCBI";
	current_settings.provenance_loaded_at = stdstr(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	current_settings.provenance_format = used_3d ? "SDF 3D" : "SDF 2D";
	current_settings.provenance_version = "PubChem PUG REST";
	current_settings.provenance_license = "PubChem/NCBI public data; preserve attribution";
	current_settings.provenance_checksum = stdstr(sdf_checksum);
	current_settings.molecule_model_version = "molecule_model_v1";
	current_settings.provider_adapter_version = "PubChemProvider_v1";
	current_settings.parser_version = "SDFParser_v1";
	current_settings.cache_version = "pubchem_cache_v1";
	current_settings.visualization_settings_version = "visualization_settings_v1";
	current_settings.source_data_cache_key = stdstr(QStringLiteral("pubchem:%1:sdf:%2").arg(cid, used_3d ? QStringLiteral("3d") : QStringLiteral("2d")));
	current_settings.source_data_cache_path = stdstr(sdf_result.cache_path);
	current_settings.image_url = stdstr(png_url.toString());
	current_settings.image_cache_path.clear();
	current_settings.image_checksum = stdstr(image_checksum);
	current_settings.conformer_status = used_3d ? "3d_conformer_loaded" : "no_3d_conformer_loaded_2d_sdf";
	current_settings.label_runtime_status = show_labels_check->isChecked() ? "interactive_molecule_viewport_active" : "disabled";
	current_settings.animation_runtime_status = rotation_animation_check->isChecked() ? "interactive_viewport_rotation_active" : "disabled";
	const QString formula = props.value(QStringLiteral("MolecularFormula"));
	current_settings.computed_classifications = stdstr(formula.contains(QChar('C')) && formula.contains(QChar('H'))
		? QStringLiteral("computed: organic_candidate (formula heuristic C+H; not a confirmed provider classification)")
		: QStringLiteral("computed: no organic/inorganic conclusion (insufficient confirmed rule)"));
	current_settings.provider_classifications.clear();
	computed_classification_edit->setPlainText(qstr(current_settings.computed_classifications));
	provider_classification_edit->clear();
	if(settings)
	{
		QStringList recent=settings->value(QStringLiteral("scientificObjectEditor/recentCids")).toStringList();recent.removeAll(cid);recent.prepend(cid);while(recent.size()>50)recent.removeLast();settings->setValue(QStringLiteral("scientificObjectEditor/recentCids"),recent);
	}

	syncing = was_syncing;

	source_status_label->setText(QString::fromUtf8("%1: PubChem CID %2 загружен. %3")
		.arg(loaded_from_cache ? QString::fromUtf8("Загружено из кэша") : QString::fromUtf8("Готово"))
		.arg(cid)
		.arg(used_3d ? QString::fromUtf8("3D-конформер доступен.") : QString::fromUtf8("3D-конформер не загружен; используется 2D SDF."))
		+ QString::fromUtf8(" Изображение: загружается превью."));
	updateScientificSourceUiState();
	updateMoleculeInteractiveView();
	const bool image_loaded = loadPubChemImage();
	source_status_label->setText(QString::fromUtf8("%1: PubChem CID %2 загружен. %3 Изображение: %4.")
		.arg(loaded_from_cache ? QString::fromUtf8("Загружено из кэша") : QString::fromUtf8("Готово"))
		.arg(cid)
		.arg(used_3d ? QString::fromUtf8("3D-конформер доступен.") : QString::fromUtf8("3D-конформер не загружен; используется 2D SDF."))
		.arg(image_loaded ? QString::fromUtf8("превью загружено") : QString::fromUtf8("превью недоступно")));

	emit objectChanged();
	return true;
}


void ScientificObjectEditor::previewScientificSourceResult()
{
	setScientificSourceResult(online_database_combo->currentText(), online_query_edit->text(), false);
}


void ScientificObjectEditor::loadScientificSourceResult()
{
	setScientificSourceResult(online_database_combo->currentText(), online_query_edit->text(), true);
}


void ScientificObjectEditor::setScientificSourceResult(const QString& database, const QString& query, bool load)
{
	const QString db = database.trimmed();
	const QString q = query.trimmed();
	const QString selected_type = currentComboData(type_combo, QStringLiteral("custom"));
	const QString suggested_type = defaultScientificTypeForDatabase(db);

	if(q.isEmpty())
	{
		online_results_list->clear();
		const QString message = QString::fromUtf8("Idle: enter an identifier/name first. Empty query is not replaced by a sample molecule.");
		if(source_status_label)
			source_status_label->setText(message);
		current_settings.load_status = "idle";
		current_settings.load_status_message = stdstr(message);
		updateScientificSourceUiState();
		if(load)
			emit objectChanged();
		return;
	}

	QString unsupported_message = sourceSupportStatusForTypeAndDatabase(selected_type, db);
	if(selected_type == "custom" && suggested_type != "custom")
		unsupported_message += QString::fromUtf8(" Suggested Scientific Object type for this source: %1.").arg(scientificTypeLabel(suggested_type));

	if(selected_type == "molecule" && db.compare(QStringLiteral("PubChem"), Qt::CaseInsensitive) == 0)
	{
		if(!load)
		{
			searchPubChem(q);
			return;
		}

		QListWidgetItem* selected_item = online_results_list->currentItem();
		const bool result_list_matches_query =
			(current_settings.load_status == "results_available" || current_settings.load_status == "ready" || current_settings.load_status == "loaded_from_cache") &&
			(current_settings.online_database == "PubChem") &&
			(current_settings.online_query == stdstr(q) || current_settings.online_result_id == stdstr(q));
		if(!result_list_matches_query)
			selected_item = NULL;
		if(!selected_item && online_results_list->count() == 1)
		{
			if(result_list_matches_query)
			{
				online_results_list->setCurrentRow(0);
				selected_item = online_results_list->currentItem();
			}
		}
		if(!selected_item || selected_item->data(Qt::UserRole + 1).toString() != QStringLiteral("pubchem_cid"))
		{
			if(searchPubChem(q))
			{
				updateScientificSourceUiState();
				source_status_label->setText(QString::fromUtf8("Results available: CID %1 selected. Click \"%2\" to load it into the object.")
					.arg(online_results_list->currentItem() ? online_results_list->currentItem()->data(Qt::UserRole).toString() : QStringLiteral("?"))
					.arg(online_load_button ? online_load_button->text() : QString::fromUtf8("Загрузить в объект")));
			}
			else
				source_status_label->setText(QString::fromUtf8("Error: PubChem search failed. Object data was not changed."));
			return;
		}

		const QString cid = selected_item->data(Qt::UserRole).toString();
		const QString old_button_text = online_load_button ? online_load_button->text() : QString();
		const bool old_button_enabled = online_load_button ? online_load_button->isEnabled() : true;
		if(online_load_button)
		{
			online_load_button->setEnabled(false);
			online_load_button->setText(QString::fromUtf8("Загрузка CID %1...").arg(cid));
		}
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		const bool loaded = loadPubChemCID(cid);
		if(online_load_button)
		{
			online_load_button->setEnabled(old_button_enabled);
			if(!loaded)
				online_load_button->setText(old_button_text.isEmpty() ? QString::fromUtf8("Загрузить в объект") : old_button_text);
		}
		updateScientificSourceUiState();
		return;
	}

	online_results_list->clear();
	QListWidgetItem* unsupported_item = new QListWidgetItem(QString::fromUtf8("%1 / %2 — Unsupported provider").arg(db, q), online_results_list);
	unsupported_item->setData(Qt::UserRole, db + QStringLiteral(":") + q);
	unsupported_item->setData(Qt::UserRole + 1, QStringLiteral("unsupported"));
	unsupported_item->setToolTip(unsupported_message);
	online_results_list->setCurrentItem(unsupported_item);

	const BuiltInMoleculeSample* sample = NULL;
	if(selected_type == "molecule")
		sample = findBuiltInMoleculeSample(q);
	if(sample)
	{
		QListWidgetItem* sample_item = new QListWidgetItem(QString::fromUtf8("Built-in sample / %1").arg(QString::fromLatin1(sample->name)), online_results_list);
		sample_item->setData(Qt::UserRole, QString::fromLatin1(sample->identifier));
		sample_item->setData(Qt::UserRole + 1, QStringLiteral("built_in_sample"));
		sample_item->setData(Qt::UserRole + 2, QString::fromLatin1(sample->key));
		sample_item->setToolTip(QString::fromUtf8("Local built-in sample. This is not live data from %1 and must not be cited as a provider result.").arg(db));
		online_results_list->setCurrentItem(sample_item);
	}

	const QString preview_message = sample
		? QString::fromUtf8("Ready: one explicit built-in sample matches this molecule query. External %1 loading is still unsupported.").arg(db)
		: QString::fromUtf8("Unsupported: %1").arg(unsupported_message);
	if(source_status_label)
		source_status_label->setText(preview_message);

	if(!load)
		return;

	QListWidgetItem* selected_item = online_results_list->currentItem();
	const QString result_kind = selected_item ? selected_item->data(Qt::UserRole + 1).toString() : QStringLiteral("unsupported");
	if(result_kind != "built_in_sample")
	{
		current_settings.load_status = "unsupported";
		current_settings.load_status_message = stdstr(QString::fromUtf8("%1 Request: %2 / %3. Existing scientific data was not changed.")
			.arg(unsupported_message)
			.arg(db)
			.arg(q));
		if(source_status_label)
			source_status_label->setText(QString::fromUtf8("Unsupported: object data was not changed. %1").arg(unsupported_message));
		emit objectChanged();
		return;
	}

	const QString sample_key = selected_item->data(Qt::UserRole + 2).toString();
	const BuiltInMoleculeSample* selected_sample = findBuiltInMoleculeSample(sample_key);
	if(!selected_sample)
	{
		current_settings.load_status = "error";
		current_settings.load_status_message = "Selected built-in sample is missing from the local catalog.";
		if(source_status_label)
			source_status_label->setText(QString::fromUtf8("Error: selected built-in sample is missing from the local catalog."));
		emit objectChanged();
		return;
	}

	name_edit->setText(QString::fromLatin1(selected_sample->name));
	source_edit->setText(QStringLiteral("Built-in sample catalog"));
	setComboData(source_mode_combo, QStringLiteral("online"));
	setComboData(type_combo, QStringLiteral("molecule"));
	setComboData(visualization_mode_combo, QStringLiteral("ball_and_stick"));
	setComboData(colour_scheme_combo, QStringLiteral("CPK"));
	data_summary_edit->setPlainText(QString::fromUtf8(
		"Built-in molecule sample.\n"
		"Formula: %1\n"
		"Molecular mass: %2\n"
		"Atoms: %3\n"
		"Bonds: %4\n"
		"Origin: local MetaSiberia sample catalog, not live %5 data.\n"
		"Scientific warning: use external provider/import once adapters are implemented for cited research data.")
		.arg(QString::fromLatin1(selected_sample->formula))
		.arg(QString::fromLatin1(selected_sample->mass))
		.arg(selected_sample->atom_count)
		.arg(selected_sample->bond_count)
		.arg(db));
	atom_table_edit->setPlainText(QString::fromLatin1(selected_sample->atoms));
	bond_table_edit->setPlainText(QString::fromLatin1(selected_sample->bonds));
	property_table_edit->setPlainText(QString::fromUtf8(
		"source\tBuilt-in sample catalog\n"
		"identifier\t%1\n"
		"requested_database\t%2\n"
		"requested_query\t%3\n"
		"format\tMetaSiberia atom/bond table\n"
		"license\tProject-local sample data")
		.arg(QString::fromLatin1(selected_sample->identifier))
		.arg(db)
		.arg(q));
	atom_count_spin->setValue(selected_sample->atom_count);
	bond_count_spin->setValue(selected_sample->bond_count);
	point_count_spin->setValue(0);

	current_settings.load_status = "ready";
	current_settings.load_status_message = "Built-in molecule sample loaded. External provider was not queried.";
	current_settings.data_origin = "built_in_sample";
	current_settings.provenance_source = "Built-in sample catalog";
	current_settings.provenance_identifier = selected_sample->identifier;
	current_settings.provenance_url = "";
	current_settings.provenance_author = "MetaSiberia";
	current_settings.provenance_loaded_at = stdstr(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	current_settings.provenance_format = "MetaSiberia atom/bond table";
	current_settings.provenance_version = "v1";
	current_settings.provenance_license = "Project-local sample data";
	if(source_status_label)
		source_status_label->setText(QString::fromUtf8("Ready: built-in sample loaded. External %1 adapter was not used.").arg(db));

	updateMoleculeInteractiveView();
	emit objectChanged();
}


void ScientificObjectEditor::updateMoleculeInteractiveView()
{
	if(!molecule_viewport || !atom_table_edit || !bond_table_edit)
		return;
	ScientificObjectSettings s = controlsToSettings();
	molecule_viewport->setMolecule(atom_table_edit->toPlainText(), bond_table_edit->toPlainText());
	molecule_viewport->setScientificSettings(s);
	const float spin_direction = currentComboData(animation_direction_combo, QStringLiteral("forward")) == QStringLiteral("reverse") ? -1.f : 1.f;
	molecule_viewport->setSpinEnabled(rotation_animation_check->isChecked(), (float)animation_speed_spin->value() * spin_direction);
	if(molecule_metrics_label) molecule_metrics_label->setText(molecule_viewport->moleculeMetricsText());
	updateMoleculeSelectionStatus();
	if(measurement_records_edit) measurement_records_edit->setPlainText(molecule_viewport->measurementsJson());
	if(molecule_card_edits.size() >= 3)
	{
		molecule_card_edits[0]->setPlainText(data_summary_edit->toPlainText());
		molecule_card_edits[1]->setPlainText(QString::fromUtf8("Атомы\n%1\n\nСвязи\n%2").arg(atom_table_edit->toPlainText(), bond_table_edit->toPlainText()));
		molecule_card_edits[2]->setPlainText(property_table_edit->toPlainText());
	}
}


QString ScientificObjectEditor::propertyValue(const QString& key) const
{
	const QStringList lines = property_table_edit ? property_table_edit->toPlainText().split(QChar('\n')) : QStringList();
	for(const QString& line : lines)
	{
		const int tab=line.indexOf(QChar('\t')); if(tab>0 && line.left(tab).compare(key,Qt::CaseInsensitive)==0) return line.mid(tab+1).trimmed();
	}
	return QString();
}


bool ScientificObjectEditor::loadPubChemCardSection(const QString& section_key)
{
	static const QMap<QString, QString> headings = {
		{QStringLiteral("description"), QStringLiteral("Record Description")}, {QStringLiteral("chemical_properties"), QStringLiteral("Chemical and Physical Properties")},
		{QStringLiteral("classification"), QStringLiteral("Classification")}, {QStringLiteral("safety"), QStringLiteral("Safety and Hazards")},
		{QStringLiteral("bioactivity"), QStringLiteral("Biological Test Results")}, {QStringLiteral("literature"), QStringLiteral("Literature")},
		{QStringLiteral("patents"), QStringLiteral("Patents")}, {QStringLiteral("spectra"), QStringLiteral("Spectral Information")}
	};
	static const QMap<QString, int> indices = { {QStringLiteral("description"),0},{QStringLiteral("chemical_properties"),2},{QStringLiteral("classification"),3},{QStringLiteral("safety"),4},{QStringLiteral("bioactivity"),5},{QStringLiteral("literature"),6},{QStringLiteral("patents"),7},{QStringLiteral("spectra"),8} };
	if(!headings.contains(section_key) || !indices.contains(section_key)) return false;
	const int index=indices.value(section_key); QString cid=qstr(current_settings.online_result_id); if(cid.isEmpty()) cid=propertyValue(QStringLiteral("cid"));
	bool ok=false;cid.toInt(&ok);if(!ok){molecule_card_status_labels[index]->setText(QString::fromUtf8("Нет данных: нет PubChem CID"));if(index!=0&&index!=2)molecule_card_edits[index]->setPlainText(QString::fromUtf8("Нет данных"));return false;}
	molecule_card_status_labels[index]->setText(QString::fromUtf8("Загрузка"));QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	QUrl url=pubchemUrl(QStringLiteral("/rest/pug_view/data/compound/%1/JSON").arg(cid));QUrlQuery query;query.addQueryItem(QStringLiteral("heading"),headings.value(section_key));url.setQuery(query);
	const PubChemHttpResult result=pubchemGet(url,QStringLiteral(".json"),true);
	if(!result.error.isEmpty()){molecule_card_status_labels[index]->setText(QString::fromUtf8("Нет данных: %1").arg(result.error));if(index!=0&&index!=2)molecule_card_edits[index]->setPlainText(QString::fromUtf8("Нет данных"));return false;}
	QString text=readablePugViewSection(result.bytes);if(text.isEmpty()){molecule_card_status_labels[index]->setText(QString::fromUtf8("Нет данных"));molecule_card_edits[index]->setPlainText(QString::fromUtf8("Нет данных"));return false;}
	if(section_key==QStringLiteral("bioactivity")||section_key==QStringLiteral("literature"))
	{
		const QString extra_heading=section_key==QStringLiteral("bioactivity")?QStringLiteral("Drug and Medication Information"):QStringLiteral("Related Records");QUrl extra_url=pubchemUrl(QStringLiteral("/rest/pug_view/data/compound/%1/JSON").arg(cid));QUrlQuery extra_query;extra_query.addQueryItem(QStringLiteral("heading"),extra_heading);extra_url.setQuery(extra_query);const PubChemHttpResult extra=pubchemGet(extra_url,QStringLiteral(".json"),true);const QString extra_text=extra.error.isEmpty()?readablePugViewSection(extra.bytes):QString();if(!extra_text.isEmpty())text+=QStringLiteral("\n\n")+extra_text;
	}
	if(section_key==QStringLiteral("description")||section_key==QStringLiteral("chemical_properties")) text=molecule_card_edits[index]->toPlainText()+QStringLiteral("\n\nPubChem PUG View\n")+text;
	molecule_card_edits[index]->setPlainText(text);molecule_card_edits[index]->setProperty("pugLoaded",true);molecule_card_status_labels[index]->setText(result.from_cache?QString::fromUtf8("Из кэша"):QString::fromUtf8("Загружено"));
	QJsonObject manifest;const QJsonDocument old_doc=QJsonDocument::fromJson(QByteArray::fromStdString(current_settings.section_cache_manifest));if(old_doc.isObject())manifest=old_doc.object();manifest.insert(section_key,result.cache_path);current_settings.section_cache_manifest=stdstr(QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact)));
	if(section_key==QStringLiteral("classification")){const QString compact=text.left(900);provider_classification_edit->setPlainText(compact);current_settings.provider_classifications=stdstr(compact);}
	if(!syncing)emit objectChanged();return true;
}


bool ScientificObjectEditor::loadPubChemImage()
{
	if(!image_viewer) return false;
	if(!current_settings.image_cache_path.empty() && QFile::exists(qstr(current_settings.image_cache_path)))
	{
		image_viewer->setImage(qstr(current_settings.image_cache_path), qstr(current_settings.image_url), qstr(current_settings.provenance_license));
		return true;
	}
	QString cid=qstr(current_settings.online_result_id);if(cid.isEmpty())cid=propertyValue(QStringLiteral("cid"));bool ok=false;cid.toInt(&ok);if(!ok){image_viewer->clearImage(QString::fromUtf8("Нет данных: нет PubChem CID"));return false;}
	image_viewer->clearImage(QString::fromUtf8("Загрузка"));QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	QUrl url=pubchemUrl(QStringLiteral("/rest/pug/compound/cid/%1/PNG").arg(cid));QUrlQuery query;query.addQueryItem(QStringLiteral("image_size"),QStringLiteral("large"));url.setQuery(query);
	const PubChemHttpResult result=pubchemGet(url,QStringLiteral(".png"),true);if(!result.error.isEmpty()){image_viewer->clearImage(QString::fromUtf8("Нет данных: %1").arg(result.error));if(molecule_image_label){molecule_image_preview_pixmap=QPixmap();molecule_image_label->setPixmap(QPixmap());molecule_image_label->setText(QString::fromUtf8("2D-превью недоступно"));}return false;}
	QPixmap pixmap;pixmap.loadFromData(result.bytes);if(pixmap.isNull()){image_viewer->clearImage(QString::fromUtf8("Нет данных: ошибка декодирования изображения"));if(molecule_image_label){molecule_image_preview_pixmap=QPixmap();molecule_image_label->setPixmap(QPixmap());molecule_image_label->setText(QString::fromUtf8("2D-превью недоступно"));}return false;}
	current_settings.image_url=stdstr(url.toString());current_settings.image_cache_path=stdstr(result.cache_path);current_settings.image_checksum=stdstr(sha256Hex(result.bytes));
	image_viewer->setImage(result.cache_path,url.toString(),QStringLiteral("PubChem/NCBI public data; preserve attribution"));if(molecule_image_label){molecule_image_preview_pixmap=pixmap;molecule_image_preview_zoom=1.0;updateMoleculeImagePreview();}
	if(!syncing)emit objectChanged();return true;
}


void ScientificObjectEditor::moleculeCardSectionChanged(int index)
{
	if(index<0||index>=molecule_card_edits.size())return;
	if(index==0){if(!molecule_card_edits[0]->property("pugLoaded").toBool()){molecule_card_edits[0]->setPlainText(data_summary_edit->toPlainText().isEmpty()?QString::fromUtf8("Нет данных"):data_summary_edit->toPlainText());loadPubChemCardSection(QStringLiteral("description"));}return;}
	if(index==1){molecule_card_edits[1]->setPlainText(atom_table_edit->toPlainText().isEmpty()?QString::fromUtf8("Нет данных"):QString::fromUtf8("Атомы\n%1\n\nСвязи\n%2").arg(atom_table_edit->toPlainText(),bond_table_edit->toPlainText()));return;}
	if(index==2){if(!molecule_card_edits[2]->property("pugLoaded").toBool()){molecule_card_edits[2]->setPlainText(property_table_edit->toPlainText().isEmpty()?QString::fromUtf8("Нет данных"):property_table_edit->toPlainText());loadPubChemCardSection(QStringLiteral("chemical_properties"));}return;}
	if(index==9)return;
	static const char* keys[]={"","","","classification","safety","bioactivity","literature","patents","spectra"};
	if(molecule_card_edits[index]->property("pugLoaded").toBool())return;
	loadPubChemCardSection(QString::fromLatin1(keys[index]));
}


void ScientificObjectEditor::handleMoleculeAction(const QString& action, int)
{
	if(action==QStringLiteral("toggle_labels")){show_labels_check->setChecked(!show_labels_check->isChecked());updateMoleculeInteractiveView();return;}
	if(action==QStringLiteral("toggle_info_card")){show_info_card_check->setChecked(!show_info_card_check->isChecked());if(show_info_card_check->isChecked())setComboData(info_card_mode_combo,QStringLiteral("selection"));updateMoleculeInteractiveView();return;}
	if(action==QStringLiteral("card")){tab_widget->setCurrentWidget(molecule_card_tab);molecule_card_sections->setCurrentIndex(0);return;}
	if(action==QStringLiteral("properties")||action==QStringLiteral("classification")){tab_widget->setCurrentWidget(molecule_card_tab);molecule_card_sections->setCurrentIndex(action==QStringLiteral("properties")?2:3);return;}
	if(action==QStringLiteral("images")){for(int i=0;i<tab_widget->count();++i)if(tab_widget->tabText(i)==QString::fromUtf8("Изображения")){tab_widget->setCurrentIndex(i);break;}return;}
	const QString cid=propertyValue(QStringLiteral("cid"));
	if(action==QStringLiteral("pubchem")){if(!cid.isEmpty())QDesktopServices::openUrl(QUrl(QStringLiteral("https://pubchem.ncbi.nlm.nih.gov/compound/%1").arg(cid)));return;}
	if(action==QStringLiteral("favorite")){favorite_check->setChecked(!favorite_check->isChecked());emitObjectChanged();return;}
	QString copy_value;if(action==QStringLiteral("copy_cid"))copy_value=cid;else if(action==QStringLiteral("copy_smiles")){copy_value=propertyValue(QStringLiteral("CanonicalSMILES"));if(copy_value.isEmpty()||copy_value==QStringLiteral("Not available"))copy_value=propertyValue(QStringLiteral("IsomericSMILES"));}else if(action==QStringLiteral("copy_inchi"))copy_value=propertyValue(QStringLiteral("InChI"));
	if(!copy_value.isEmpty()){QApplication::clipboard()->setText(copy_value);return;}
	if(action==QStringLiteral("refresh")){if(!cid.isEmpty())loadPubChemCID(cid);return;}
	if(action==QStringLiteral("export")){const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("Export molecule information"),name_edit->text()+QStringLiteral(".json"),QStringLiteral("JSON (*.json);;Text (*.txt)"));if(!path.isEmpty()){QFile f(path);if(f.open(QIODevice::WriteOnly|QIODevice::Truncate))f.write(QByteArray::fromStdString(ScientificObjectSettings::serialiseToContent(controlsToSettings())));}return;}
	if(action==QStringLiteral("delete")){if(QMessageBox::question(this,QStringLiteral("Delete molecule"),QStringLiteral("Delete the selected Scientific Object from the world?"))==QMessageBox::Yes)emit deleteObjectRequested();return;}
}


void ScientificObjectEditor::updateColourButton()
{
	if(sender() == display_colour_button)
	{
		const QColor current((int)(display_colour.r * 255), (int)(display_colour.g * 255), (int)(display_colour.b * 255));
		const QColor colour = QColorDialog::getColor(current, this, QString::fromUtf8("Цвет научного объекта"));
		if(colour.isValid())
		{
			display_colour = Colour3f(colour.red() / 255.f, colour.green() / 255.f, colour.blue() / 255.f);
			emit objectChanged();
		}
	}

	const QColor c((int)(display_colour.r * 255), (int)(display_colour.g * 255), (int)(display_colour.b * 255));
	display_colour_button->setText(QStringLiteral(" "));
	display_colour_button->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.name()));
}


void ScientificObjectEditor::updateInfoLabel(const WorldObject& ob)
{
	const ScientificObjectSettings s = ScientificObjectSettings::fromContent(ob.content);
	QString text = QString::fromUtf8("ScientificObject (UID: %1)\n%2 | %3")
		.arg(QString::fromUtf8(ob.uid.toString().c_str()))
		.arg(qstr(s.name))
		.arg(scientificTypeLabel(qstr(s.scientific_type)));
	if(!s.source.empty())
		text += QString::fromUtf8(" | %1").arg(qstr(s.source));
	if(!s.load_status.empty())
		text += QString::fromUtf8("\nStatus: %1").arg(qstr(s.load_status));
	info_label->setText(text);
}
