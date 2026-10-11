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


static const int PREVIEW_SIZE = 160;

namespace {
class MaterialBrowserTask : public QRunnable
{
public:
	explicit MaterialBrowserTask(std::function<void()> work_) : work(std::move(work_)) {}
	void run() override { work(); }
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
		if(index == 1) QTimer::singleShot(0, this, [this]() { loadVisibleThumbnails(); });
	});
	connect(refresh, &QPushButton::clicked, this, [this]() {
		failed_thumbnails.clear();
		loadPolyHavenCatalog();
	});
	connect(search, &QLineEdit::textChanged, this, [this]() { filterPolyHavenMaterials(); });
	connect(sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { filterPolyHavenMaterials(); });
	connect(categories, &QTreeWidget::currentItemChanged, this, [this]() { filterPolyHavenMaterials(); });
	connect(polyhaven_materials, &QListWidget::itemClicked, this, &MaterialBrowser::selectPolyHavenMaterial);
	connect(polyhaven_materials->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { loadVisibleThumbnails(); });

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
		QTimer::singleShot(0, this, [this]() { loadVisibleThumbnails(); });
	return QWidget::eventFilter(watched, event);
}

void MaterialBrowser::loadPolyHavenCatalog()
{
	if(catalog_loading) return;
	catalog_loading = true;
	refresh->setEnabled(false);
	status->setText(tr("Loading Poly Haven textures…"));
	const QString cache_path = QtUtils::toQString(appdata_path) + "/polyhaven_materials/catalog.json";
	const QPointer<MaterialBrowser> guard(this);
	const auto alive_guard = alive;
	QThreadPool::globalInstance()->start(new MaterialBrowserTask([guard, alive_guard, cache_path]() {
		if(!alive_guard->load()) return;
		QJsonObject result;
		QString error;
		try
		{
			result = MCPTextures::textureCatalog();
			try { PolyHavenMaterialCache::save(cache_path, QJsonDocument(result).toJson(QJsonDocument::Compact)); }
			catch(const std::exception&) {} // A read-only cache must not prevent browsing online.
		}
		catch(const std::exception& e)
		{
			error = QString::fromUtf8(e.what());
			QFile file(cache_path);
			if(file.open(QIODevice::ReadOnly) && file.size() <= 24 * 1024 * 1024)
				result = QJsonDocument::fromJson(file.readAll()).object();
		}
		QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, result, error]() {
			if(!guard) return;
			guard->catalog_loading = false;
			guard->refresh->setEnabled(true);
			if(!result.isEmpty())
			{
				guard->catalog = result;
				guard->rebuildCategories();
				guard->filterPolyHavenMaterials();
			}
			if(!error.isEmpty()) guard->status->setText(result.isEmpty() ?
				guard->tr("Could not load Poly Haven: %1").arg(error) : guard->tr("Offline catalogue. Downloaded materials remain available."));
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
	if(!material_loading) status->setText(matches.isEmpty() ? tr("No matching textures found.") : tr("%1 textures · Click a sphere to apply its 1K material.").arg(matches.size()));
	QTimer::singleShot(0, this, [this]() { loadVisibleThumbnails(); });
}

void MaterialBrowser::loadVisibleThumbnails()
{
	if(!polyhaven_materials->isVisible() || pending_thumbnails.size() >= 4) return;
	const QRect visible = polyhaven_materials->viewport()->rect().adjusted(0, -202, 0, 202);
	for(int i = 0; i < polyhaven_materials->count() && pending_thumbnails.size() < 4; ++i)
	{
		QListWidgetItem* item = polyhaven_materials->item(i);
		if(!polyhaven_materials->visualItemRect(item).intersects(visible)) continue;
		const QString id = item->data(Qt::UserRole).toString();
		const QString url = catalog.value(id).toObject().value("thumbnail_url").toString();
		if(url.isEmpty() || thumbnail_icons.contains(url) || pending_thumbnails.contains(url) || failed_thumbnails.contains(url)) continue;
		pending_thumbnails.insert(url);
		const QString cache_path = QtUtils::toQString(appdata_path) + "/polyhaven_materials/thumbs/" + PolyHavenMaterialCache::key(url) + ".png";
		const QPointer<MaterialBrowser> guard(this);
		const auto alive_guard = alive;
		QThreadPool::globalInstance()->start(new MaterialBrowserTask([guard, alive_guard, id, url, cache_path]() {
			if(!alive_guard->load()) return;
			QImage image;
			try
			{
				QFile file(cache_path);
				if(file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024)
					image = MCPTextures::decode(file.readAll());
			}
			catch(const std::exception&) {}
			if(image.isNull()) try
			{
				const QByteArray bytes = MCPTextures::fetch(QUrl(url), 4 * 1024 * 1024);
				image = MCPTextures::decode(bytes);
				try { PolyHavenMaterialCache::save(cache_path, bytes); } catch(const std::exception&) {}
			}
			catch(const std::exception&) {}
			QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, id, url, image]() {
				if(!guard) return;
				guard->pending_thumbnails.remove(url);
				if(image.isNull()) guard->failed_thumbnails.insert(url);
				else
				{
					const QIcon icon(QPixmap::fromImage(image.scaled(PREVIEW_SIZE, PREVIEW_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
					guard->thumbnail_icons.insert(url, icon);
					for(int i = 0; i < guard->polyhaven_materials->count(); ++i)
						if(guard->polyhaven_materials->item(i)->data(Qt::UserRole).toString() == id)
							guard->polyhaven_materials->item(i)->setIcon(icon);
				}
				guard->loadVisibleThumbnails();
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
