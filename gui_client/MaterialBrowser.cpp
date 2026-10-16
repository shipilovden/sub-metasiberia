/*=====================================================================
MaterialBrowser.cpp
-------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "MaterialBrowser.h"
#include "PolyHavenMaterialCache.h"


#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QApplication>
#include <QtCore/QPointer>
#include <QtCore/QThreadPool>
#include <QtCore/QRunnable>
#include <QtCore/QTimer>
#include <QtCore/QSignalBlocker>
#include <QtCore/QEvent>
#include <QtCore/QDateTime>
#include "../qt/FlowLayout.h"
#include "../shared/WorldMaterial.h"
#include "ModelLoading.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <graphics/PNGDecoder.h>
#include <graphics/jpegdecoder.h>
#include <graphics/SRGBUtils.h>
#include <qt/QtUtils.h>
#include <FileUtils.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <IncludeXXHash.h>
#include <Exception.h>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif


static const int PREVIEW_SIZE = 160;

namespace {
#if defined(_WIN32)
class PolyHavenPreviewHttpHandle
{
public:
	explicit PolyHavenPreviewHttpHandle(HINTERNET handle_) : handle(handle_)
	{
		if(!handle) MCPTextures::fail(QStringLiteral("Poly Haven HTTPS handle error %1").arg(GetLastError()));
	}
	~PolyHavenPreviewHttpHandle() { WinHttpCloseHandle(handle); }
	PolyHavenPreviewHttpHandle(const PolyHavenPreviewHttpHandle&) = delete;
	PolyHavenPreviewHttpHandle& operator=(const PolyHavenPreviewHttpHandle&) = delete;
	HINTERNET handle;
};
#endif

QByteArray fetchPolyHavenPreview(const QUrl& url)
{
#if defined(_WIN32)
	const QString host = url.host().toLower();
	if(!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(443) != 443 ||
		(host != "cdn.polyhaven.com" && host != "dl.polyhaven.org" && host != "api.polyhaven.com"))
		MCPTextures::fail("Unsupported Poly Haven preview URL.");

	// The bundled Qt has no SSL backend. WinHTTP provides HTTPS, system proxy
	// support and connection fallback across the CDN's resolved addresses.
	// Reuse each worker's session so the full catalogue can share connections.
	static thread_local PolyHavenPreviewHttpHandle session(WinHttpOpen(L"Metasiberia (Poly Haven previews)",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if(!WinHttpSetTimeouts(session.handle, 5000, 5000, 15000, 15000))
		MCPTextures::fail(QStringLiteral("Poly Haven HTTPS timeout setup error %1").arg(GetLastError()));
	const std::wstring host_w = host.toStdWString();
	QString target = url.path(QUrl::FullyEncoded);
	if(target.isEmpty()) target = "/";
	if(url.hasQuery()) target += '?' + url.query(QUrl::FullyEncoded);
	const std::wstring target_w = target.toStdWString();
	PolyHavenPreviewHttpHandle connection(WinHttpConnect(session.handle, host_w.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
	PolyHavenPreviewHttpHandle request(WinHttpOpenRequest(connection.handle, L"GET", target_w.c_str(), NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	if(!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy)))
		MCPTextures::fail(QStringLiteral("Poly Haven HTTPS redirect setup error %1").arg(GetLastError()));
	if(!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(request.handle, NULL))
		MCPTextures::fail(QStringLiteral("Poly Haven HTTPS request error %1").arg(GetLastError()));
	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	if(!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX))
		MCPTextures::fail(QStringLiteral("Poly Haven HTTPS status error %1").arg(GetLastError()));
	if(status_code != 200)
		MCPTextures::fail(QStringLiteral("Poly Haven preview returned HTTP %1").arg(status_code));
	QByteArray bytes;
	char buffer[16384];
	for(;;)
	{
		DWORD received = 0;
		if(!WinHttpReadData(request.handle, buffer, sizeof(buffer), &received))
			MCPTextures::fail(QStringLiteral("Poly Haven HTTPS read error %1").arg(GetLastError()));
		if(received == 0) break;
		if(bytes.size() + received > 4 * 1024 * 1024)
			MCPTextures::fail("Poly Haven preview is too large.");
		bytes.append(buffer, (int)received);
	}
	return bytes;
#else
	return MCPTextures::fetch(url, 4 * 1024 * 1024);
#endif
}

class MaterialBrowserTask : public QRunnable
{
public:
	explicit MaterialBrowserTask(std::function<void()> work_) : work(std::move(work_)) {}
	void run() override
	{
		try { work(); }
		catch(const glare::Exception& e) { conPrint("MaterialBrowser background task failed: " + e.what()); }
		catch(const std::exception& e) { conPrint(std::string("MaterialBrowser background task failed: ") + e.what()); }
		catch(...) { conPrint("MaterialBrowser background task failed with an unknown exception."); }
	}
private:
	std::function<void()> work;
};

QString categoryLabel(const QString& name)
{
	return QCoreApplication::translate("MaterialBrowser", name.toUtf8().constData());
}
}


MaterialBrowser::MaterialBrowser()
:	alive(new std::atomic<bool>(true)), print_output(NULL)
{
}


MaterialBrowser::~MaterialBrowser()
{
	alive->store(false);
}


void MaterialBrowser::init(QWidget* parent, const std::string& basedir_path_, const std::string& appdata_path_, PrintOutput* print_output_)
{
	basedir_path = basedir_path_;
	appdata_path = appdata_path_;
	print_output = print_output_;

	setupUi(this);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	source_tabs = new QTabWidget(this);
	source_tabs->setObjectName("materialSourceTabs");
	source_tabs->setDocumentMode(true);
	root->addWidget(source_tabs);
	QScrollArea* local_scroll = new QScrollArea(source_tabs);
	local_scroll->setWidgetResizable(true);
	local_scroll->setFrameShape(QFrame::NoFrame);
	QWidget* local_materials = new QWidget(local_scroll);
	flow_layout = new FlowLayout(local_materials);
	local_scroll->setWidget(local_materials);
	source_tabs->addTab(local_scroll, tr("Materials"));
	QWidget* polyhaven_page = new QWidget(source_tabs);
	QVBoxLayout* polyhaven_layout = new QVBoxLayout(polyhaven_page);
	polyhaven_layout->setContentsMargins(6, 6, 6, 6);
	QHBoxLayout* toolbar = new QHBoxLayout();
	search = new QLineEdit(polyhaven_page);
	search->setClearButtonEnabled(true);
	sort = new QComboBox(polyhaven_page);
	sort->addItems({QString(), QString(), QString()});
	refresh = new QPushButton(polyhaven_page);
	toolbar->addWidget(search, 1);
	toolbar->addWidget(sort);
	toolbar->addWidget(new QLabel("1K", polyhaven_page));
	toolbar->addWidget(refresh);
	polyhaven_layout->addLayout(toolbar);
	QSplitter* split = new QSplitter(Qt::Horizontal, polyhaven_page);
	categories = new QTreeWidget(split);
	categories->setObjectName("polyHavenTextureCategories");
	categories->setHeaderHidden(true);
	categories->setMinimumWidth(150);
	categories->setIndentation(12);
	polyhaven_materials = new QListWidget(split);
	polyhaven_materials->setObjectName("polyHavenTextureGrid");
	polyhaven_materials->setViewMode(QListView::IconMode);
	polyhaven_materials->setResizeMode(QListView::Adjust);
	polyhaven_materials->setMovement(QListView::Static);
	polyhaven_materials->setIconSize(QSize(PREVIEW_SIZE, PREVIEW_SIZE));
	polyhaven_materials->setGridSize(QSize(184, 202));
	polyhaven_materials->setUniformItemSizes(true);
	polyhaven_materials->setWordWrap(true);
	polyhaven_materials->setSelectionMode(QAbstractItemView::SingleSelection);
	polyhaven_materials->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	polyhaven_materials->viewport()->installEventFilter(this);
	split->setChildrenCollapsible(false);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	split->setSizes(QList<int>() << 215 << 800);
	polyhaven_layout->addWidget(split, 1);
	status = new QLabel(polyhaven_page);
	status->setTextFormat(Qt::PlainText);
	status->setWordWrap(true);
	polyhaven_layout->addWidget(status);
	attribution = new QLabel(polyhaven_page);
	attribution->setOpenExternalLinks(true);
	polyhaven_layout->addWidget(attribution);
	source_tabs->addTab(polyhaven_page, "Poly Haven");
	retranslateBrowser();
	connect(source_tabs, &QTabWidget::currentChanged, this, [this](int index) {
		if(index == 1 && catalog.isEmpty()) loadPolyHavenCatalog();
		if(index == 1)
		{
			thumbnail_preload_started = true;
			QTimer::singleShot(0, this, [this]() { loadCatalogThumbnails(); });
		}
	});
	connect(refresh, &QPushButton::clicked, this, [this]() {
		thumbnail_attempts.clear();
		thumbnail_retry_after.clear();
		loadCatalogThumbnails();
		loadPolyHavenCatalog();
	});
	connect(search, &QLineEdit::textChanged, this, [this]() { filterPolyHavenMaterials(); });
	connect(sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { filterPolyHavenMaterials(); });
	connect(categories, &QTreeWidget::currentItemChanged, this, [this]() { filterPolyHavenMaterials(); });
	connect(polyhaven_materials, &QListWidget::itemClicked, this, &MaterialBrowser::selectPolyHavenMaterial);
	connect(polyhaven_materials->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { loadCatalogThumbnails(); });

	// Scan for all materials on disk, make preview buttons for them.

	// NOTE: disabled rendering of previews for now, just use pre-computed JPGs instead.
	// Rendering was slow, seldom tested, and prone to crashing.

	try
	{
		const std::vector<std::string> filepaths = FileUtils::getFilesInDirWithExtensionFullPaths(basedir_path + "/data/resources/materials", "submat");

		for(size_t i=0; i<filepaths.size(); ++i)
		{
			const std::string mat_name = ::removeDotAndExtension(FileUtils::getFilename(filepaths[i]));

			const std::string mat_dir = ::removeDotAndExtension(filepaths[i]);
			const std::string preview_path = mat_dir + "/preview.jpg";
		
			QImage image;
			image.load(QtUtils::toQString(preview_path));

			QPushButton* button = new QPushButton();

			button->setFixedWidth(PREVIEW_SIZE);
			button->setFixedHeight(PREVIEW_SIZE);
			button->setIconSize(QSize(PREVIEW_SIZE, PREVIEW_SIZE));

			button->setIcon(QPixmap::fromImage(image));

			button->setToolTip(QtUtils::toQString(mat_name));

			flow_layout->addWidget(button);

			connect(button, SIGNAL(clicked()), this, SLOT(buttonClicked()));

			browser_buttons.push_back(button);
			mat_paths.push_back(filepaths[i]);
		}
	}
	catch(glare::Exception& e)
	{
		if(print_output)
			print_output->print("MaterialBrowser: " + e.what());
		conPrint("Error: " + e.what());
	}
}


void MaterialBrowser::buttonClicked()
{
	++selection_generation;
	pending_material_id.clear(); // A local material click supersedes an in-flight online selection.
	status->clear();
	assert(mat_paths.size() == browser_buttons.size());

	QPushButton* button = static_cast<QPushButton*>(QObject::sender());
	for(size_t z=0; z<browser_buttons.size(); ++z)
		if(browser_buttons[z] == button)
		{
			emit materialSelected(mat_paths[z]);
		}
}


void MaterialBrowser::retranslateBrowser()
{
	if(!source_tabs) return;
	source_tabs->setTabText(0, tr("Materials"));
	search->setPlaceholderText(tr("Search Poly Haven textures…"));
	refresh->setText(tr("Refresh"));
	const QSignalBlocker blocker(sort);
	sort->setItemText(0, tr("Popular"));
	sort->setItemText(1, tr("Newest"));
	sort->setItemText(2, tr("Name"));
	attribution->setText(tr("<a href=\"https://polyhaven.com/ru/textures\">Powered by Poly Haven</a> · CC0 · 1K"));
	if(!catalog.isEmpty())
	{
		rebuildCategories();
		filterPolyHavenMaterials();
	}
}

void MaterialBrowser::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if(event->type() == QEvent::LanguageChange) retranslateBrowser();
}

bool MaterialBrowser::eventFilter(QObject* watched, QEvent* event)
{
	if(polyhaven_materials && watched == polyhaven_materials->viewport() &&
		(event->type() == QEvent::Resize || event->type() == QEvent::Show))
		QTimer::singleShot(0, this, [this]() { loadCatalogThumbnails(); });
	return QWidget::eventFilter(watched, event);
}

void MaterialBrowser::loadPolyHavenCatalog()
{
	if(catalog_loading) return;
	catalog_loading = true;
	refresh->setEnabled(false);
	status->setText(tr("Loading Poly Haven textures…"));
	const QString cache_path = QtUtils::toQString(appdata_path) + "/polyhaven_materials/catalog.json";
	QJsonObject cached_catalog;
	QFile cache_file(cache_path);
	if(cache_file.open(QIODevice::ReadOnly) && cache_file.size() <= 24 * 1024 * 1024)
		cached_catalog = QJsonDocument::fromJson(cache_file.readAll()).object();
	if(cached_catalog.isEmpty())
	{
		// A fresh installation can browse the shipped previews without waiting for the API.
		QFile bundled_catalog(QtUtils::toQString(basedir_path) + "/data/resources/polyhaven_previews/catalog.json");
		if(bundled_catalog.open(QIODevice::ReadOnly) && bundled_catalog.size() <= 24 * 1024 * 1024)
			cached_catalog = QJsonDocument::fromJson(bundled_catalog.readAll()).object();
	}
	if(!cached_catalog.isEmpty())
	{
		catalog = cached_catalog;
		rebuildCategories();
		filterPolyHavenMaterials();
		status->setText(tr("Using the saved Poly Haven catalogue while checking for updates…"));
	}
	const QPointer<MaterialBrowser> guard(this);
	const auto alive_guard = alive;
	QThreadPool::globalInstance()->start(new MaterialBrowserTask([guard, alive_guard, cache_path, has_cached_catalog = !cached_catalog.isEmpty()]() {
		if(!alive_guard->load()) return;
		QJsonObject result;
		QString error;
		try
		{
			result = MCPTextures::textureCatalog();
			try { PolyHavenMaterialCache::save(cache_path, QJsonDocument(result).toJson(QJsonDocument::Compact)); }
			catch(const glare::Exception&) {} // Cache write failures must not prevent browsing online.
			catch(const std::exception&) {} // A read-only cache must not prevent browsing online.
		}
		catch(const glare::Exception& e)
		{
			error = QString::fromStdString(e.what());
		}
		catch(const std::exception& e)
		{
			error = QString::fromUtf8(e.what());
		}
		QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, result, error, has_cached_catalog]() {
			if(!guard) return;
			guard->catalog_loading = false;
			guard->refresh->setEnabled(true);
			if(!result.isEmpty())
			{
				guard->catalog = result;
				guard->rebuildCategories();
				guard->filterPolyHavenMaterials();
			}
			if(!error.isEmpty()) guard->status->setText(has_cached_catalog ?
				guard->tr("Offline catalogue. Showing saved Poly Haven textures; updates are unavailable.") :
				guard->tr("Could not load Poly Haven: %1").arg(error));
		}, Qt::QueuedConnection);
	}));
}

void MaterialBrowser::rebuildCategories()
{
	const QString selected = categories->currentItem() ? categories->currentItem()->data(0, Qt::UserRole).toString() : QString();
	const QSignalBlocker blocker(categories);
	categories->clear();
	QMap<QString, int> counts;
	int total = 0;
	for(auto it = catalog.begin(); it != catalog.end(); ++it)
	{
		const QJsonObject asset = it.value().toObject();
		if(asset.value("type").toInt(-1) != 1) continue;
		++total;
		QString path;
		for(const QString& part : asset.value("category").toString().split('/', Qt::SkipEmptyParts))
		{
			path += (path.isEmpty() ? "" : "/") + part;
			++counts[path];
		}
	}
	QTreeWidgetItem* all = new QTreeWidgetItem(categories, {tr("All textures") + QString(" (%1)").arg(total)});
	all->setData(0, Qt::UserRole, QString());
	QMap<QString, QTreeWidgetItem*> nodes;
	QTreeWidgetItem* selected_node = all;
	for(auto it = counts.begin(); it != counts.end(); ++it)
	{
		const QString parent_path = it.key().section('/', 0, -2);
		QTreeWidgetItem* item = nodes.contains(parent_path) ? new QTreeWidgetItem(nodes[parent_path]) : new QTreeWidgetItem(categories);
		item->setText(0, categoryLabel(it.key().section('/', -1)) + QString(" (%1)").arg(it.value()));
		item->setData(0, Qt::UserRole, it.key());
		item->setToolTip(0, it.key());
		nodes.insert(it.key(), item);
		if(it.key() == selected) selected_node = item;
	}
	categories->setCurrentItem(selected_node);
	for(QTreeWidgetItem* parent = selected_node->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
}

void MaterialBrowser::filterPolyHavenMaterials()
{
	const QString selected_id = polyhaven_materials->currentItem() ? polyhaven_materials->currentItem()->data(Qt::UserRole).toString() : QString();
	const QString category = categories->currentItem() ? categories->currentItem()->data(0, Qt::UserRole).toString() : QString();
	const QStringList terms = search->text().trimmed().split(' ', Qt::SkipEmptyParts);
	QStringList matches;
	for(auto it = catalog.begin(); it != catalog.end(); ++it)
	{
		const QJsonObject asset = it.value().toObject();
		const QString path = asset.value("category").toString();
		if(asset.value("type").toInt(-1) != 1 || (!category.isEmpty() && path != category && !path.startsWith(category + '/'))) continue;
		QString text = it.key() + ' ' + asset.value("name").toString() + ' ' + path + ' ' + asset.value("description").toString();
		for(const QString& part : path.split('/')) text += ' ' + categoryLabel(part);
		for(const QJsonValue& tag : asset.value("tags").toArray()) text += ' ' + tag.toString();
		bool match = true;
		for(const QString& term : terms) if(!text.contains(term, Qt::CaseInsensitive)) { match = false; break; }
		if(match) matches.append(it.key());
	}
	const int sorting = sort->currentIndex();
	std::sort(matches.begin(), matches.end(), [this, sorting](const QString& a, const QString& b) {
		const QJsonObject first = catalog.value(a).toObject(), second = catalog.value(b).toObject();
		if(sorting != 2)
		{
			const char* field = sorting == 1 ? "date_published" : "download_count";
			const double x = first.value(field).toDouble(), y = second.value(field).toDouble();
			if(x != y) return x > y;
		}
		return QString::compare(first.value("name").toString(), second.value("name").toString(), Qt::CaseInsensitive) < 0;
	});
	polyhaven_materials->clear();
	for(const QString& id : matches)
	{
		const QJsonObject asset = catalog.value(id).toObject();
		QListWidgetItem* item = new QListWidgetItem(asset.value("name").toString(), polyhaven_materials);
		item->setSizeHint(QSize(184, 202));
		item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
		item->setData(Qt::UserRole, id);
		item->setToolTip(asset.value("name").toString() + '\n' + asset.value("category").toString() + "\nPoly Haven · CC0 · 1K");
		const QString url = asset.value("thumbnail_url").toString();
		if(thumbnail_icons.contains(url)) item->setIcon(thumbnail_icons.value(url));
		if(id == selected_id) polyhaven_materials->setCurrentItem(item);
	}
	if(!material_loading) updateCatalogThumbnailStatus();
	QTimer::singleShot(0, this, [this]() { loadCatalogThumbnails(); });
}

void MaterialBrowser::applyCatalogThumbnail(const QString& url, const QIcon& icon)
{
	for(int i = 0; i < polyhaven_materials->count(); ++i)
	{
		QListWidgetItem* item = polyhaven_materials->item(i);
		const QJsonObject asset = catalog.value(item->data(Qt::UserRole).toString()).toObject();
		if(asset.value("thumbnail_url").toString() == url)
			item->setIcon(icon);
	}
}

void MaterialBrowser::updateCatalogThumbnailStatus()
{
	if(material_loading) return;
	int total = 0;
	int loaded = 0;
	for(auto it = catalog.begin(); it != catalog.end(); ++it)
	{
		const QJsonObject asset = it.value().toObject();
		if(asset.value("type").toInt(-1) != 1) continue;
		const QString url = asset.value("thumbnail_url").toString();
		if(url.isEmpty()) continue;
		++total;
		if(thumbnail_icons.contains(url)) ++loaded;
	}
	if(total == 0)
		status->setText(tr("Textures are listed, but Poly Haven provided no previews."));
	else if(loaded == total)
		status->setText(tr("All %1 texture previews loaded. Click a sphere to apply its 1K material.").arg(total));
	else
		status->setText(tr("Loading all texture previews: %1/%2 · Missing previews retry automatically.").arg(loaded).arg(total));
}

void MaterialBrowser::loadCatalogThumbnails()
{
	if(!thumbnail_preload_started || catalog.isEmpty()) return;
	updateCatalogThumbnailStatus();
	if(pending_thumbnails.size() >= 4) return;
	const QStringList ids = catalog.keys();
	// Continue where the previous pass stopped, so failures near the start of
	// the catalogue cannot repeatedly take every download slot.
	for(int scanned = 0; scanned < ids.size() && pending_thumbnails.size() < 4; ++scanned)
	{
		thumbnail_scan_offset %= ids.size();
		const QString id = ids.at(thumbnail_scan_offset++);
		const QJsonObject asset = catalog.value(id).toObject();
		if(asset.value("type").toInt(-1) != 1) continue;
		const QString url = asset.value("thumbnail_url").toString();
		if(url.isEmpty() || thumbnail_icons.contains(url) || pending_thumbnails.contains(url) || thumbnail_retry_after.contains(url)) continue;
		pending_thumbnails.insert(url);
		const QString cache_path = QtUtils::toQString(appdata_path) + "/polyhaven_materials/thumbs/" + PolyHavenMaterialCache::key(url) + ".png";
		const QString bundled_path = QRegularExpression("^[a-z0-9_]{1,100}$").match(id).hasMatch() ?
			QtUtils::toQString(basedir_path) + "/data/resources/polyhaven_previews/" + id + ".webp" : QString();
		const QPointer<MaterialBrowser> guard(this);
		const auto alive_guard = alive;
		QThreadPool::globalInstance()->start(new MaterialBrowserTask([guard, alive_guard, id, url, cache_path, bundled_path]() {
			if(!alive_guard->load()) return;
			QImage image;
			QString error;
			try
			{
				QFile file(cache_path);
				if(file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024)
					image = MCPTextures::decode(file.readAll());
			}
			catch(const glare::Exception&) {} // A damaged cached preview must be downloaded again.
			catch(const std::exception&) {}
			if(image.isNull() && !bundled_path.isEmpty()) try
			{
				// Keep the complete shipped catalogue usable even when the CDN is unavailable.
				QFile file(bundled_path);
				if(file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024)
					image = MCPTextures::decode(file.readAll());
			}
			catch(const glare::Exception&) {} // Fall back to downloading if a resource is damaged.
			catch(const std::exception&) {}
			if(image.isNull()) try
			{
				const QByteArray bytes = fetchPolyHavenPreview(QUrl(url));
				image = MCPTextures::decode(bytes);
				try { PolyHavenMaterialCache::save(cache_path, bytes); }
				catch(const glare::Exception&) {}
				catch(const std::exception&) {}
			}
			catch(const glare::Exception& e) { error = QString::fromStdString(e.what()); }
			catch(const std::exception& e) { error = QString::fromUtf8(e.what()); }
			if(!image.isNull())
				image = image.scaled(PREVIEW_SIZE, PREVIEW_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, id, url, image, error]() {
				if(!guard) return;
				guard->pending_thumbnails.remove(url);
				if(image.isNull())
				{
					const int attempts = qMin(guard->thumbnail_attempts.value(url) + 1, 10);
					guard->thumbnail_attempts.insert(url, attempts);
					const int delay_ms = attempts >= 6 ? 60000 : 1000 * (1 << attempts);
					const qint64 retry_after = QDateTime::currentMSecsSinceEpoch() + delay_ms;
					guard->thumbnail_retry_after.insert(url, retry_after);
					if(attempts == 1 || attempts == 10)
						conPrint("Retrying Poly Haven thumbnail " + id.toStdString() + ": " + error.toStdString());
					QTimer::singleShot(delay_ms, guard, [guard, url, retry_after]() {
						if(!guard || guard->thumbnail_retry_after.value(url) != retry_after) return;
						guard->thumbnail_retry_after.remove(url);
						guard->loadCatalogThumbnails();
					});
				}
				else
				{
					guard->thumbnail_attempts.remove(url);
					guard->thumbnail_retry_after.remove(url);
					const QIcon icon(QPixmap::fromImage(image));
					guard->thumbnail_icons.insert(url, icon);
					guard->applyCatalogThumbnail(url, icon);
				}
				guard->updateCatalogThumbnailStatus();
				guard->loadCatalogThumbnails();
			}, Qt::QueuedConnection);
		}));
	}
}

void MaterialBrowser::selectPolyHavenMaterial(QListWidgetItem* item)
{
	if(!item) return;
	++selection_generation;
	pending_material_id = item->data(Qt::UserRole).toString();
	emit polyHavenMaterialRequested();
	status->setText(tr("Loading %1 · 1K…").arg(item->text()));
	startMaterialDownload();
}

void MaterialBrowser::startMaterialDownload()
{
	if(material_loading || pending_material_id.isEmpty()) return;
	material_loading = true;
	const int generation = selection_generation;
	const QString id = pending_material_id;
	const QJsonObject asset = catalog.value(id).toObject();
	const QString cache_root = QtUtils::toQString(appdata_path) + "/polyhaven_materials";
	const QPointer<MaterialBrowser> guard(this);
	const auto alive_guard = alive;
	QThreadPool::globalInstance()->start(new MaterialBrowserTask([guard, alive_guard, generation, id, asset, cache_root]() {
		if(!alive_guard->load()) return;
		QString path, error;
		try { path = PolyHavenMaterialCache::download(cache_root, id, asset); }
		catch(const glare::Exception& e) { error = QString::fromStdString(e.what()); }
		catch(const std::exception& e) { error = QString::fromUtf8(e.what()); }
		QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, generation, path, error]() {
			if(!guard) return;
			guard->material_loading = false;
			if(generation != guard->selection_generation) { guard->startMaterialDownload(); return; }
			guard->pending_material_id.clear();
			if(!error.isEmpty()) { guard->status->setText(guard->tr("Could not load the material: %1").arg(error)); return; }
			guard->status->setText(guard->tr("1K material ready. Click another sphere to change it."));
			emit guard->polyHavenMaterialReady(path.toStdString());
		}, Qt::QueuedConnection);
	}));
}


void MaterialBrowser::renderThumbnails(Reference<OpenGLEngine> opengl_engine)
{
	OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();

	try
	{
		const std::string materials_dir = "C:/code/substrata/resources/materials";
		const std::vector<std::string> file_paths = FileUtils::getFilesInDirWithExtensionFullPaths(materials_dir, "submat");

		for(size_t i=0; i<file_paths.size(); ++i)
		{
			const std::string mat_name = ::removeDotAndExtension(FileUtils::getFilename(file_paths[i]));
			const std::string mat_dir  = ::removeDotAndExtension(file_paths[i]);
			const std::string preview_png_path = mat_dir + "/preview.png";
			const std::string preview_jpeg_path = mat_dir + "/preview.jpg";

			OpenGLSceneRef scene = new OpenGLScene(*opengl_engine);
			opengl_engine->addScene(scene);
			opengl_engine->setCurrentScene(scene);

			// Add cube object with the material applied
			{
				GLObjectRef cube_ob = opengl_engine->allocateObject();
				cube_ob->materials.resize(1);
			
				WorldMaterialRef mat = WorldMaterial::loadFromXMLOnDisk(file_paths[i], /*convert_rel_paths_to_abs_disk_paths=*/true);
			
				ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(*mat, cube_ob->materials[0]);
			
				const float cube_w = 0.5f;
				cube_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, cube_w/2) * Matrix4f::rotationAroundZAxis(Maths::pi_4<float>()) *
					Matrix4f::uniformScaleMatrix(cube_w) * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
				cube_ob->mesh_data = opengl_engine->getCubeMeshData();
			
				// Load any textures
				for(size_t z=0; z<cube_ob->materials.size(); ++z)
					if(!cube_ob->materials[z].tex_path.empty())
						cube_ob->materials[z].albedo_texture = opengl_engine->getTexture(toStdString(cube_ob->materials[z].tex_path));
			
				opengl_engine->addObject(cube_ob);
			}

			// Add env mat
			{
				OpenGLMaterial env_mat;
				opengl_engine->setEnvMat(env_mat);

				opengl_engine->setSunDir(normalise(Vec4f(0.5f, 1.f, 1.f, 0)));
			}

			// Add a ground plane
			{
				const float W = 200;
			
				GLObjectRef ob = opengl_engine->allocateObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.9f));
				ob->materials[0].albedo_texture = opengl_engine->getTexture(basedir_path + "/data/resources/obstacle.png");
				ob->materials[0].roughness = 0.8f;
				ob->materials[0].fresnel_scale = 0.5f;
				ob->materials[0].tex_matrix = Matrix2f(W, 0, 0, W);
			
				ob->ob_to_world_matrix = Matrix4f::scaleMatrix(W, W, 1) * Matrix4f::translationMatrix(-0.5f, -0.5f, 0);
				ob->mesh_data = opengl_engine->getUnitQuadMeshData();
			
				opengl_engine->addObject(ob);
			}

			const Matrix4f world_to_camera_space_matrix = Matrix4f::rotationAroundXAxis(0.5f) * Matrix4f::translationMatrix(0, 0.8, -0.6) * Matrix4f::rotationAroundZAxis(2.5);

			const float sensor_width = 0.035f;
			const float lens_sensor_dist = 0.03f;
			const float render_aspect_ratio = 1.0;

			opengl_engine->setMainViewportDims(PREVIEW_SIZE, PREVIEW_SIZE);
			opengl_engine->setViewportDims(PREVIEW_SIZE, PREVIEW_SIZE);
			opengl_engine->setNearDrawDistance(0.1f);
			opengl_engine->setMaxDrawDistance(100.f);
			opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);


			OpenGLTextureRef target_tex = new OpenGLTexture(PREVIEW_SIZE, PREVIEW_SIZE, opengl_engine.ptr(),
				ArrayRef<uint8>(),
				OpenGLTextureFormat::Format_RGBA_Linear_Uint8,
				OpenGLTexture::Filtering_Nearest,
				OpenGLTexture::Wrapping_Clamp
			);

			opengl_engine->waitForAllBuildingProgramsToBuild();

			ImageMapUInt8Ref im = opengl_engine->drawToBufferAndReturnImageMap();
			
			FileUtils::createDirIfDoesNotExist(FileUtils::getDirectory(preview_jpeg_path));
			JPEGDecoder::save(im->extract3ChannelImage(), preview_jpeg_path, JPEGDecoder::SaveOptions());

			// PNGDecoder::write(*im, preview_png_path);

			conPrint("Wrote to " + preview_jpeg_path);

			opengl_engine->removeScene(scene);
		}
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}
	
	// Restore old scene
	opengl_engine->setCurrentScene(old_scene);
}
