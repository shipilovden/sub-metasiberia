/*=====================================================================
AddObjectDialog.cpp
-------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "AddObjectDialog.h"


#include "GaussianSplatCEFConverter.h"
#include "ModelLoading.h"
#include "GaussianSplatRenderer.h"
#include "MeshBuilding.h"
#include "NetDownloadResourcesThread.h"
#include "MCPTextureTools.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../shared/GaussianSplatAsset.h"
#include "../shared/GaussianSplatData.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/include/IndigoException.h"
#include "../simpleraytracer/raymesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/Exception.h"
#include "../utils/FileUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/TaskManager.h"
#include "../utils/MemMappedFile.h"
#include "../indigo/TextureServer.h"
#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtCore/QEvent>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QRunnable>
#include <QtCore/QThreadPool>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDialogButtonBox>
#include <functional>


struct StandardPrimitiveEntry
{
	const char* asset;
	const char* label;
};

static const StandardPrimitiveEntry standard_primitives[] = {
	{"Quad", QT_TRANSLATE_NOOP("AddObjectDialog", "Quad")},
	{"Cube", QT_TRANSLATE_NOOP("AddObjectDialog", "Cube")},
	{"Capsule", QT_TRANSLATE_NOOP("AddObjectDialog", "Capsule")},
	{"Cylinder", QT_TRANSLATE_NOOP("AddObjectDialog", "Cylinder")},
	{"Icosahedron", QT_TRANSLATE_NOOP("AddObjectDialog", "Icosahedron")},
	{"Platonic_Solid", QT_TRANSLATE_NOOP("AddObjectDialog", "Dodecahedron")},
	{"Torus", QT_TRANSLATE_NOOP("AddObjectDialog", "Torus")},
	{"Cone", QT_TRANSLATE_NOOP("AddObjectDialog", "Cone")},
	{"Pyramid", QT_TRANSLATE_NOOP("AddObjectDialog", "Pyramid")},
	{"Octahedron", QT_TRANSLATE_NOOP("AddObjectDialog", "Octahedron")},
	{"Wedge", QT_TRANSLATE_NOOP("AddObjectDialog", "Wedge")},
	{"Triangular_Prism", QT_TRANSLATE_NOOP("AddObjectDialog", "Triangular prism")},
	{"Hexagonal_Prism", QT_TRANSLATE_NOOP("AddObjectDialog", "Hexagonal prism")},
	{"Icosphere", QT_TRANSLATE_NOOP("AddObjectDialog", "Icosphere")}
};


class AddObjectRunnable : public QRunnable
{
public:
	explicit AddObjectRunnable(const std::function<void()>& fn_) : fn(fn_) { setAutoDelete(true); }
	void run() override { fn(); }
private:
	std::function<void()> fn;
};


static QString safePolyHavenRelativePath(QString path)
{
	path.replace('\\', '/');
	const QString cleaned=QDir::cleanPath(path);
	if(path.startsWith('/') || cleaned=="." || cleaned==".." || cleaned.startsWith("../") || QFileInfo(cleaned).isAbsolute())
		throw glare::Exception("Poly Haven model contains an unsafe file path.");
	return cleaned;
}


static QString downloadPolyHavenModel(const QString& asset_id, const QString& destination_root)
{
	if(!QRegularExpression("^[A-Za-z0-9_]{1,100}$").match(asset_id).hasMatch())
		throw glare::Exception("Invalid Poly Haven model id.");
	const QJsonObject files=QJsonDocument::fromJson(MCPTextures::fetch(QUrl("https://api.polyhaven.com/files/"+asset_id))).object();
	QJsonObject asset=files.value("gltf").toObject().value("1k").toObject().value("gltf").toObject();
	if(asset.isEmpty())
		asset=files.value("obj").toObject().value("1k").toObject().value("obj").toObject();
	if(asset.isEmpty() || !asset.value("url").isString())
		throw glare::Exception("This Poly Haven model does not provide a glTF or OBJ download.");
	const QString folder=QDir(destination_root).filePath(asset_id);
	if(!QDir().mkpath(folder)) throw glare::Exception("Could not create the Poly Haven download folder.");
	quint64 downloaded=0;
	auto saveFile=[&](const QString& relative_path,const QJsonObject& entry) {
		const QString safe_path=safePolyHavenRelativePath(relative_path);
		const QUrl url(entry.value("url").toString());
		if(url.host().toLower()!="dl.polyhaven.org" || !entry.value("size").isDouble())
			throw glare::Exception("Poly Haven returned an unsupported model file URL.");
		const qint64 declared_size=(qint64)entry.value("size").toDouble();
		if(declared_size<0 || declared_size>128LL*1024*1024 || downloaded+(quint64)declared_size>256ULL*1024*1024)
			throw glare::Exception("The Poly Haven model exceeds the 256 MiB download limit.");
		const QByteArray data=MCPTextures::fetch(url,128*1024*1024);
		if(declared_size>0 && data.size()!=declared_size)
			throw glare::Exception("A Poly Haven model file had an unexpected size.");
		const QString target=QDir(folder).filePath(safe_path);
		if(!QDir().mkpath(QFileInfo(target).absolutePath())) throw glare::Exception("Could not create a model dependency folder.");
		QFile output(target);
		if(!output.open(QIODevice::WriteOnly) || output.write(data)!=data.size())
			throw glare::Exception("Could not save a downloaded Poly Haven model file.");
		downloaded+=(quint64)data.size();
	};

	const QUrl model_url(asset.value("url").toString());
	const QString model_name=QFileInfo(model_url.path()).fileName();
	if(model_name.isEmpty() || (!model_name.endsWith(".gltf",Qt::CaseInsensitive) && !model_name.endsWith(".obj",Qt::CaseInsensitive)))
		throw glare::Exception("Poly Haven returned an invalid model filename.");
	saveFile(model_name,asset);
	const QJsonObject includes=asset.value("include").toObject();
	for(auto it=includes.begin();it!=includes.end();++it)
		saveFile(it.key(),it.value().toObject());
	return QDir(folder).filePath(model_name);
}


static void deleteAddObjectGaussianTemporaryFile(const std::string& path)
{
	if(path.empty())
		return;
	try
	{
		if(FileUtils::fileExists(path))
			FileUtils::deleteFile(path);
	}
	catch(glare::Exception& e)
	{
		conPrint("Could not remove temporary Gaussian PLY '" + path + "': " + e.what());
	}
}


AddObjectDialog::AddObjectDialog(const std::string& base_dir_path_, QSettings* settings_, Reference<ResourceManager> resource_manager_, IMFDXGIDeviceManager* dev_manager_,
	glare::TaskManager* main_task_manager_, glare::TaskManager* high_priority_task_manager_)
:	settings(settings_),
	gaussian_splat_progress_dialog(NULL),
	resource_manager(resource_manager_),
	base_dir_path(base_dir_path_),
	polyHavenModelList(NULL),
	polyHavenSearchEdit(NULL),
	polyHavenStatusLabel(NULL),
	polyHavenWebTabs(NULL),
	polyHavenSearchButton(NULL),
	polyHavenTempDir(),
	polyHavenRequestGeneration(0),
	dev_manager(dev_manager_),
	loaded_mesh_is_image_cube(false),
	main_task_manager(main_task_manager_),
	high_priority_task_manager(high_priority_task_manager_)
{
	setupUi(this);

	// Keep the original URL workflow and add Poly Haven as a second source inside "From Web".
	QTabWidget* webSources=new QTabWidget(tab_3);
	polyHavenWebTabs=webSources;
	QWidget* urlPage=new QWidget(webSources);
	QVBoxLayout* urlLayout=new QVBoxLayout(urlPage);
	urlLayout->setContentsMargins(0,0,0,0);
	verticalLayout_5->removeWidget(widget);
	widget->setParent(urlPage);
	urlLayout->addWidget(widget);
	urlLayout->addStretch(1);
	webSources->addTab(urlPage,tr("URL"));
	QWidget* polyHavenPage=new QWidget(webSources);
	QVBoxLayout* polyLayout=new QVBoxLayout(polyHavenPage);
	QHBoxLayout* searchLayout=new QHBoxLayout();
	polyHavenSearchEdit=new QLineEdit(polyHavenPage);
	polyHavenSearchEdit->setObjectName("polyHavenSearchEdit");
	polyHavenSearchEdit->setPlaceholderText(tr("Search models: chair, tree, rock…"));
	polyHavenSearchButton=new QPushButton(tr("Search"),polyHavenPage);
	polyHavenSearchButton->setObjectName("polyHavenSearchButton");
	searchLayout->addWidget(polyHavenSearchEdit,1);
	searchLayout->addWidget(polyHavenSearchButton);
	polyLayout->addLayout(searchLayout);
	polyHavenModelList=new QListWidget(polyHavenPage);
	polyHavenModelList->setObjectName("polyHavenModelList");
	polyHavenModelList->setViewMode(QListWidget::IconMode);
	polyHavenModelList->setIconSize(QSize(144,144));
	polyHavenModelList->setGridSize(QSize(170,190));
	polyHavenModelList->setResizeMode(QListWidget::Adjust);
	polyHavenModelList->setMovement(QListWidget::Static);
	polyHavenModelList->setSelectionMode(QAbstractItemView::SingleSelection);
	polyLayout->addWidget(polyHavenModelList,1);
	polyHavenStatusLabel=new QLabel(polyHavenPage);
	polyHavenStatusLabel->setObjectName("polyHavenStatusLabel");
	polyHavenStatusLabel->setOpenExternalLinks(true);
	polyHavenStatusLabel->setTextFormat(Qt::RichText);
	polyHavenStatusLabel->setText(tr("Choose a model to download and preview. <a href=\"https://polyhaven.com\">Powered by Poly Haven</a> · CC0"));
	polyLayout->addWidget(polyHavenStatusLabel);
	webSources->addTab(polyHavenPage,tr("Poly Haven"));
	verticalLayout_5->addWidget(webSources);
	connect(polyHavenSearchButton,SIGNAL(clicked()),this,SLOT(searchPolyHavenModels()));
	connect(polyHavenSearchEdit,SIGNAL(returnPressed()),this,SLOT(searchPolyHavenModels()));
	connect(polyHavenModelList,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(polyHavenModelSelected(QListWidgetItem*)));
	connect(webSources,SIGNAL(currentChanged(int)),this,SLOT(searchPolyHavenModels()));
	polyHavenTempDir.reset(new QTemporaryDir());
	polyHavenTempDir->setAutoRemove(true);

	texture_server = new TextureServer(/*use_canonical_path_keys=*/false); // To cache textures for textureHasAlphaChannel

	this->objectPreviewGLWidget->init(base_dir_path, settings_, texture_server, main_task_manager, high_priority_task_manager);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AddObjectDialog/geometry").toByteArray());

	this->tabWidget->setCurrentIndex(settings->value("AddObjectDialog/tabIndex").toInt());

	this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
	this->avatarSelectWidget->setFilter(
		"Supported files (*.obj *.gltf *.glb *.vox *.stl *.igmesh *.ply *.compressed.ply *.splat *.ksplat *.spz *.sog *.lcc *.lcc2 "
		"meta.json lod-meta.json "
		"*.jpg *.jpeg *.png *.gif *.exr *.ktx *.ktx2 *.basis *.tif *.tiff *.bmp *.tga *.webp);;All files (*.*)"
	);
	//this->avatarSelectWidget->setFilename(settings->value("AddObjectDialogPath").toString());

	connect(this->listWidget, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(modelSelected(QListWidgetItem*)));
	connect(this->listWidget, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(modelDoubleClicked(QListWidgetItem*)));
	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(filenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	connect(this->urlLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(urlChanged(const QString&)));
	connect(this->urlLineEdit, SIGNAL(editingFinished()), this, SLOT(urlEditingFinished()));

	connect(this, SIGNAL(finished(int)), this, SLOT(dialogFinished()));

	startTimer(10);

	thread_manager.addThread(new NetDownloadResourcesThread(&this->msg_queue, resource_manager_, &num_net_resources_downloading));

	
	listWidget->setViewMode(QListWidget::IconMode);
	listWidget->setIconSize(QSize(200, 200));
	listWidget->setResizeMode(QListWidget::Adjust);
	listWidget->setSelectionMode(QAbstractItemView::NoSelection);
	 
	for(const StandardPrimitiveEntry& primitive : standard_primitives)
	{
		std::string preview_model = primitive.asset;
		if(preview_model == "Quad") preview_model = "quad";
		const std::string image_path = base_dir_path + "/data/resources/models/" + preview_model + ".PNG";

		QListWidgetItem* item = new QListWidgetItem(QIcon(QtUtils::toQString(image_path)), tr(primitive.label));
		// Asset paths must never depend on the current display language.
		item->setData(Qt::UserRole, QString::fromLatin1(primitive.asset));
		item->setData(Qt::UserRole + 1, QString::fromLatin1(primitive.label));
		listWidget->addItem(item);
	}
}


AddObjectDialog::~AddObjectDialog()
{
	cancelGaussianSplatConversion(/*delete_temporary_output=*/true);

	settings->setValue("AddObjectDialog/geometry", saveGeometry());

	settings->setValue("AddObjectDialog/tabIndex", this->tabWidget->currentIndex());
}


void AddObjectDialog::shutdownGL()
{
	// Keep a completed temporary PLY alive until MainWindow has synchronously
	// copied it in GUIClient::createObject(). The destructor removes it.
	cancelGaussianSplatConversion(/*delete_temporary_output=*/false);

	// Make sure we have set the gl context to current as we destroy objectPreviewGLWidget.
	this->objectPreviewGLWidget->makeCurrent();

	preview_gl_ob = NULL;
	gaussian_splat_preview_render_object = NULL;
	gaussian_splat_preview_program = NULL;
	objectPreviewGLWidget->shutdown();
}


// Called when user presses ESC key, or clicks OK or cancel button.
void AddObjectDialog::dialogFinished()
{
	++polyHavenRequestGeneration; // Ignore any queued Poly Haven result after the GL preview is shut down.
	shutdownGL();
}


void AddObjectDialog::accepted()
{
	this->settings->setValue("AddObjectDialogPath", this->avatarSelectWidget->filename());
}


void AddObjectDialog::modelSelected(QListWidgetItem* selected_item)
{
	if(this->listWidget->currentItem())
	{
		cancelGaussianSplatConversion(/*delete_temporary_output=*/true);

		const std::string model = QtUtils::toStdString(this->listWidget->currentItem()->data(Qt::UserRole).toString());

		this->listWidget->setCurrentItem(NULL);

		const std::string model_path = base_dir_path + "/data/resources/models/" + model + ".obj";

		this->result_path = model_path;

		loadModelIntoPreview(model_path);
	}
}


void AddObjectDialog::modelDoubleClicked(QListWidgetItem* selected_item)
{
	cancelGaussianSplatConversion(/*delete_temporary_output=*/true);

	const std::string model = QtUtils::toStdString(selected_item->data(Qt::UserRole).toString());
	const std::string model_path = base_dir_path + "/data/resources/models/" + model + ".obj";
	this->result_path = model_path;
	accept();
}


void AddObjectDialog::filenameChanged(QString& filename)
{
	cancelGaussianSplatConversion(/*delete_temporary_output=*/true);

	const std::string path = QtUtils::toIndString(filename);
	this->result_path = path;

	conPrint("AddObjectDialog::filenameChanged: filename = " + path);

	if(filename.isEmpty())
		return;

	loadModelIntoPreview(path);
}


// Sets preview_gl_ob and loaded_object
void AddObjectDialog::makeMeshForWidthAndHeight(const std::string& local_image_or_vid_path, int w, int h)
{
	this->preview_gl_ob = ModelLoading::makeImageCube(*objectPreviewGLWidget->opengl_engine, *objectPreviewGLWidget->opengl_engine->vert_buf_allocator, local_image_or_vid_path, w, h,
		this->loaded_mesh, 
		this->loaded_materials, // world_materials_out
		this->scale // scale_out
	);
}


void AddObjectDialog::loadModelIntoPreview(const std::string& local_path)
{
	this->objectPreviewGLWidget->makeCurrent();

	this->loaded_mesh_is_image_cube = false;

	this->ob_cam_right_translation = 0;
	this->ob_cam_up_translation = 0;

	this->loaded_materials.clear();
	this->loaded_mesh = NULL;
	this->loaded_voxels.clear();
	this->gaussian_splat_preview_render_object = NULL;
	this->scale = Vec3f(1.f);
	this->axis = Vec3f(0, 0, 1);
	this->angle = 0;

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
		{
			// Remove previous object from engine.
			objectPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);
		}

		if(GaussianSplatAsset::hasSupportedExtension(local_path))
		{
			try
			{
				MemMappedFile file(local_path);
				const GaussianSplatDataRef splat_data = GaussianSplatDecoder::decode(
					local_path,
					ArrayRef<uint8>((const uint8*)file.fileData(), file.fileSize())
				);
				setGaussianSplatPreviewObject(splat_data);
			}
			catch(glare::Exception& e)
			{
				const GaussianSplatAsset::Format format = GaussianSplatAsset::detectFormat(local_path);
				const bool can_use_converter =
					// Plain .ply is decoded natively.  If that decoder reports a
					// problem, preserve its useful error instead of routing a valid
					// 3DGS PLY through the fragile browser conversion fallback.
					format == GaussianSplatAsset::Format::CompressedPly ||
					format == GaussianSplatAsset::Format::KSplat ||
					format == GaussianSplatAsset::Format::SPZ || // Native v4 first; converter covers other supported SPZ versions.
					format == GaussianSplatAsset::Format::SOG ||
					format == GaussianSplatAsset::Format::LCC ||
					format == GaussianSplatAsset::Format::LCC2;
				if(can_use_converter)
				{
					startGaussianSplatConversion(local_path, e.what());
					return;
				}
				throw;
			}
		}
		else if(ImageDecoding::hasSupportedImageExtension(local_path))
		{
			// Load image to get aspect ratio of image.
			// We will scale our model so it has the same aspect ratio.
			Reference<Map2D> im = ImageDecoding::decodeImage(base_dir_path, local_path);

			makeMeshForWidthAndHeight(local_path, (int)im->getMapWidth(), (int)im->getMapHeight());
			
			BitUtils::setOrZeroBit(loaded_materials[0]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, LODGeneration::textureHasAlphaChannel(local_path, im)); // Set COLOUR_TEX_HAS_ALPHA_FLAG flag

			this->loaded_mesh_is_image_cube = true;
		}
		else if(ModelLoading::hasSupportedModelExtension(local_path))
		{
			ModelLoading::MakeGLObjectResults results;
			ModelLoading::makeGLObjectForModelFile(*objectPreviewGLWidget->opengl_engine, *objectPreviewGLWidget->opengl_engine->vert_buf_allocator, /*allocator=*/nullptr, local_path, /*do_opengl_stuff=*/true,
				results
			);

			if(results.materials.size() > WorldObject::maxNumMaterials())
				throw glare::Exception("Model had too many materials (" + toString(results.materials.size()) + "), max number allowed is " + toString(WorldObject::maxNumMaterials()) + ".");

			this->preview_gl_ob = results.gl_ob;

			this->loaded_mesh = results.batched_mesh;
			this->loaded_voxels = results.voxels.voxels;

			this->loaded_materials = results.materials;
			this->scale = results.scale;
			this->axis = results.axis;
			this->angle = results.angle;
		}
		else
			throw glare::Exception("file did not have a supported image or model extension: '" + getExtension(local_path) + "'");

		finishPreviewObject();
	}
	catch(Indigo::IndigoException& e)
	{
		this->loaded_materials.clear();

		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
	catch(glare::Exception& e)
	{
		this->loaded_materials.clear();

		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
}


void AddObjectDialog::setGaussianSplatPreviewObject(const GaussianSplatDataRef& splat_data)
{
	if(gaussian_splat_preview_program.isNull())
		gaussian_splat_preview_program = GaussianSplatRenderer::makeProgram(
			*objectPreviewGLWidget->opengl_engine,
			base_dir_path
		);
	const OpenGLTextureRef data_texture =
		GaussianSplatRenderer::makeDataTexture(*objectPreviewGLWidget->opengl_engine, *splat_data);
	gaussian_splat_preview_render_object = GaussianSplatRenderer::makeObject(
		*objectPreviewGLWidget->opengl_engine,
		splat_data,
		data_texture,
		gaussian_splat_preview_program.ptr(),
		Matrix4f::identity()
	);
	preview_gl_ob = gaussian_splat_preview_render_object->gl_object;
	gaussian_splat_preview_render_object->updateDepthSort(
		objectPreviewGLWidget->cameraForwardsWS(),
		preview_gl_ob->ob_to_world_matrix,
		/*force=*/true
	);
	loaded_materials.push_back(new WorldMaterial());
}


void AddObjectDialog::finishPreviewObject()
{
	// Try and load textures
	tryLoadTexturesForPreviewOb(preview_gl_ob, this->loaded_materials, objectPreviewGLWidget->opengl_engine.ptr(), *texture_server, this);

	// Offset object vertically so it rests on the ground plane.
	const js::AABBox cur_aabb_ws = preview_gl_ob->mesh_data->aabb_os.transformedAABBFast(preview_gl_ob->ob_to_world_matrix);
	const float z_trans = -cur_aabb_ws.min_[2];
	preview_gl_ob->ob_to_world_matrix = ::leftTranslateAffine3(Vec4f(0, 0, z_trans, 0), preview_gl_ob->ob_to_world_matrix);

	objectPreviewGLWidget->opengl_engine->addObject(preview_gl_ob);
}


void AddObjectDialog::startGaussianSplatConversion(const std::string& local_path, const std::string& native_error)
{
	cancelGaussianSplatConversion(/*delete_temporary_output=*/true);

	GaussianSplatCEFConverter::Config config;
	config.input_path = local_path;
	config.converter_script_path =
		GaussianSplatCEFConverter::packagedConverterScriptPath(base_dir_path + "/data/resources");
	config.webp_wasm_path =
		GaussianSplatCEFConverter::packagedWebPWasmPath(base_dir_path + "/data/resources");
	config.output_path = GaussianSplatCEFConverter::makeTemporaryOutputPath();

	try
	{
		gaussian_splat_converter = new GaussianSplatCEFConverter();
		gaussian_splat_converter->start(config);
		gaussian_splat_temporary_output_path = config.output_path;

		gaussian_splat_progress_dialog = new QProgressDialog(
			tr("Converting Gaussian splat to a native PLY..."),
			tr("Cancel"),
			0,
			1000,
			this
		);
		gaussian_splat_progress_dialog->setWindowTitle(tr("Gaussian splat conversion"));
		gaussian_splat_progress_dialog->setWindowModality(Qt::WindowModal);
		gaussian_splat_progress_dialog->setAutoClose(false);
		gaussian_splat_progress_dialog->setAutoReset(false);
		gaussian_splat_progress_dialog->setMinimumDuration(0);
		gaussian_splat_progress_dialog->show();
		if(buttonBox->button(QDialogButtonBox::Ok))
			buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

		conPrint(
			"Native Gaussian decoder requested the hidden converter for '" +
			local_path + "': " + native_error
		);
	}
	catch(glare::Exception& e)
	{
		gaussian_splat_converter = NULL;
		deleteAddObjectGaussianTemporaryFile(config.output_path);
		gaussian_splat_temporary_output_path.clear();
		throw glare::Exception(
			"Native Gaussian decode failed: " + native_error +
			"\nHidden converter could not start: " + e.what()
		);
	}
}


void AddObjectDialog::processGaussianSplatConversion()
{
	if(gaussian_splat_converter.isNull())
		return;

	gaussian_splat_converter->think();

	if(gaussian_splat_progress_dialog)
	{
		if(gaussian_splat_progress_dialog->wasCanceled())
		{
			cancelGaussianSplatConversion(/*delete_temporary_output=*/true);
			loaded_materials.clear();
			return;
		}

		const std::string stage = gaussian_splat_converter->progressStage();
		gaussian_splat_progress_dialog->setLabelText(
			tr("Converting Gaussian splat: %1").arg(QtUtils::toQString(stage.empty() ? "working" : stage))
		);
		gaussian_splat_progress_dialog->setValue(
			(int)(myClamp(gaussian_splat_converter->progressValue(), 0.0, 1.0) * 1000.0)
		);
	}

	if(!gaussian_splat_converter->isFinished())
		return;

	const Reference<GaussianSplatCEFConverter> finished_converter = gaussian_splat_converter;
	gaussian_splat_converter = NULL;
	closeGaussianSplatProgressDialog();

	if(finished_converter->state() == GaussianSplatCEFConverter::State_Failed)
	{
		const std::string error = finished_converter->errorMessage();
		deleteAddObjectGaussianTemporaryFile(gaussian_splat_temporary_output_path);
		gaussian_splat_temporary_output_path.clear();
		loaded_materials.clear();
		QtUtils::showErrorMessageDialog(QtUtils::toQString(error), this);
		return;
	}

	try
	{
		objectPreviewGLWidget->makeCurrent();
		MemMappedFile file(finished_converter->outputPath());
		const GaussianSplatDataRef splat_data = GaussianSplatDecoder::decode(
			"metasiberia-runtime.ply",
			ArrayRef<uint8>((const uint8*)file.fileData(), file.fileSize())
		);
		setGaussianSplatPreviewObject(splat_data);
		finishPreviewObject();

		gaussian_splat_temporary_output_path = finished_converter->outputPath();
		result_path = gaussian_splat_temporary_output_path;
	}
	catch(glare::Exception& e)
	{
		loaded_materials.clear();
		deleteAddObjectGaussianTemporaryFile(gaussian_splat_temporary_output_path);
		gaussian_splat_temporary_output_path.clear();
		result_path.clear();
		QtUtils::showErrorMessageDialog(
			QtUtils::toQString("Converted Gaussian PLY could not be loaded: " + e.what()),
			this
		);
	}
}


void AddObjectDialog::cancelGaussianSplatConversion(bool delete_temporary_output)
{
	if(gaussian_splat_converter.nonNull())
	{
		gaussian_splat_converter->cancel();
		gaussian_splat_converter = NULL;
	}
	closeGaussianSplatProgressDialog();

	if(delete_temporary_output && !gaussian_splat_temporary_output_path.empty())
	{
		deleteAddObjectGaussianTemporaryFile(gaussian_splat_temporary_output_path);
		gaussian_splat_temporary_output_path.clear();
	}
}


void AddObjectDialog::closeGaussianSplatProgressDialog()
{
	if(gaussian_splat_progress_dialog)
	{
		gaussian_splat_progress_dialog->hide();
		delete gaussian_splat_progress_dialog;
		gaussian_splat_progress_dialog = NULL;
	}
	if(buttonBox->button(QDialogButtonBox::Ok))
		buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
}


// static
// Used by both AddObjectDialog and AvatarSettingsDialog
void AddObjectDialog::tryLoadTexturesForPreviewOb(Reference<GLObject> preview_gl_ob, std::vector<WorldMaterialRef>& world_materials/*WorldObjectRef loaded_object*/, OpenGLEngine* opengl_engine, 
	TextureServer& texture_server, QWidget* parent_widget)
{
	// Try and load textures.  Report any errors but continue with the loading.
	for(size_t i=0; i<preview_gl_ob->materials.size(); ++i)
	{
		const std::string albedo_tex_path = std::string(preview_gl_ob->materials[i].tex_path);
		if(!albedo_tex_path.empty() && !hasExtension(albedo_tex_path, "mp4"))
		{
			try
			{
				preview_gl_ob->materials[i].albedo_texture = opengl_engine->getTexture(albedo_tex_path); // Load texture

				Reference<Map2D> map = texture_server.getTexForPath(".", albedo_tex_path); // Hopefully is already loaded

				const bool has_alpha = LODGeneration::textureHasAlphaChannel(albedo_tex_path, map);// preview_gl_ob->materials[i].albedo_texture->hasAlpha();// && !preview_gl_ob->materials[i].albedo_texture->isAlphaChannelAllWhite();
				BitUtils::setOrZeroBit(world_materials[i]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, has_alpha);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + albedo_tex_path + "': " + e.what() + 
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->colour_texture_url = ""; // Clear texture in WorldMaterial, so we don't insert an invalid texture into the world.
			}
		}

		const std::string metallic_roughness_tex_path = std::string(preview_gl_ob->materials[i].metallic_roughness_tex_path);
		if(!metallic_roughness_tex_path.empty() && !hasExtension(metallic_roughness_tex_path, "mp4"))
		{
			try
			{
				preview_gl_ob->materials[i].metallic_roughness_texture = opengl_engine->getTexture(metallic_roughness_tex_path);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + metallic_roughness_tex_path + "': " + e.what() +
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->roughness.texture_url = ""; // Clear texture in WorldMaterial
			}
		}

		const std::string emission_tex_path = std::string(preview_gl_ob->materials[i].emission_tex_path);
		if(!emission_tex_path.empty() && !hasExtension(emission_tex_path, "mp4"))
		{
			try
			{
				preview_gl_ob->materials[i].emission_texture = opengl_engine->getTexture(emission_tex_path);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + emission_tex_path + "': " + e.what() +
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->emission_texture_url = ""; // Clear texture in WorldMaterial
			}
		}

		const std::string normal_map_path = std::string(preview_gl_ob->materials[i].normal_map_path);
		if(!normal_map_path.empty() && !hasExtension(normal_map_path, "mp4"))
		{
			try
			{
				TextureParams params;
				params.use_sRGB = false;
				preview_gl_ob->materials[i].normal_map = opengl_engine->getTexture(normal_map_path, params);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + normal_map_path + "': " + e.what() +
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->normal_map_url = ""; // Clear texture in WorldMaterial
			}
		}
	}
}


void AddObjectDialog::searchPolyHavenModels()
{
	if(!polyHavenModelList || !polyHavenStatusLabel) return;
	if(QPushButton* ok_button=buttonBox->button(QDialogButtonBox::Ok)) ok_button->setEnabled(true);
	const int generation=++polyHavenRequestGeneration;
	const QString query=polyHavenSearchEdit ? polyHavenSearchEdit->text() : QString();
	polyHavenStatusLabel->setText(tr("Searching Poly Haven…"));
	polyHavenModelList->clear();
	QPointer<AddObjectDialog> dialog(this);
	QThreadPool::globalInstance()->start(new AddObjectRunnable([dialog,generation,query]() {
		QJsonArray results;
		QString error;
		try
		{
			results=MCPTextures::searchModels(query,24);
			for(int i=0;i<results.size();++i)
			{
				QJsonObject item=results[i].toObject();
				const QUrl thumbnail_url(item.value("thumbnail_url").toString());
				if(!thumbnail_url.isEmpty())
				{
					try { item.insert("thumbnail_data",QString::fromLatin1(MCPTextures::fetch(thumbnail_url,4*1024*1024).toBase64())); }
					catch(const glare::Exception&) {} // A missing thumbnail should not hide an otherwise usable model.
				}
				results[i]=item;
			}
		}
		catch(const glare::Exception& e) { error=QString::fromStdString(e.what()); }
		if(dialog)
			QMetaObject::invokeMethod(dialog.data(),[dialog,generation,results,error]() {
				if(!dialog || dialog->polyHavenRequestGeneration!=generation) return;
				if(!error.isEmpty()) { dialog->polyHavenStatusLabel->setText(dialog->tr("Poly Haven search failed: %1").arg(error.toHtmlEscaped())); return; }
				for(const QJsonValue& value : results)
				{
					const QJsonObject asset=value.toObject();
					QListWidgetItem* item=new QListWidgetItem(asset.value("name").toString());
					item->setData(Qt::UserRole,asset.value("id").toString());
					item->setToolTip(asset.value("description").toString()+"\n\nPoly Haven · CC0");
					const QByteArray image_bytes=QByteArray::fromBase64(asset.value("thumbnail_data").toString().toLatin1());
					if(!image_bytes.isEmpty())
					{
						QPixmap pixmap; if(pixmap.loadFromData(image_bytes)) item->setIcon(QIcon(pixmap));
					}
					dialog->polyHavenModelList->addItem(item);
				}
				dialog->polyHavenStatusLabel->setText(dialog->tr("%1 models · choose one to download and preview. <a href=\"https://polyhaven.com\">Powered by Poly Haven</a> · CC0").arg(results.size()));
			},Qt::QueuedConnection);
	}));
}


void AddObjectDialog::polyHavenModelSelected(QListWidgetItem* item)
{
	if(!item || polyHavenTempDir.isNull() || !polyHavenTempDir->isValid()) return;
	last_url=URLString(); // Any in-flight download from the URL page is now stale.
	const QString id=item->data(Qt::UserRole).toString();
	const int generation=++polyHavenRequestGeneration;
	polyHavenStatusLabel->setText(tr("Downloading model and textures…"));
	if(QPushButton* ok_button=buttonBox->button(QDialogButtonBox::Ok)) ok_button->setEnabled(false);
	QPointer<AddObjectDialog> dialog(this);
	const QSharedPointer<QTemporaryDir> temp_dir=polyHavenTempDir;
	const QString destination=temp_dir->path();
	QThreadPool::globalInstance()->start(new AddObjectRunnable([dialog,generation,id,destination,temp_dir]() {
		QString model_path,error;
		try { model_path=downloadPolyHavenModel(id,destination); }
		catch(const glare::Exception& e) { error=QString::fromStdString(e.what()); }
		if(dialog)
			QMetaObject::invokeMethod(dialog.data(),[dialog,generation,model_path,error]() {
				if(!dialog || dialog->polyHavenRequestGeneration!=generation) return;
				if(QPushButton* ok_button=dialog->buttonBox->button(QDialogButtonBox::Ok)) ok_button->setEnabled(true);
				if(!error.isEmpty()) { dialog->polyHavenStatusLabel->setText(dialog->tr("Could not download model: %1").arg(error.toHtmlEscaped())); return; }
				dialog->cancelGaussianSplatConversion(/*delete_temporary_output=*/true);
				dialog->result_path=QtUtils::toStdString(model_path);
				try
				{
					dialog->loadModelIntoPreview(dialog->result_path);
					if(dialog->loaded_materials.empty())
						dialog->polyHavenStatusLabel->setText(dialog->tr("Could not preview model: %1").arg(dialog->tr("The model could not be loaded.")));
					else
						dialog->polyHavenStatusLabel->setText(dialog->tr("Preview ready · <a href=\"https://polyhaven.com\">Poly Haven</a> · CC0"));
				}
				catch(const glare::Exception& e) { dialog->polyHavenStatusLabel->setText(dialog->tr("Could not preview model: %1").arg(QString::fromStdString(e.what()).toHtmlEscaped())); }
			},Qt::QueuedConnection);
	}));
}


void AddObjectDialog::urlChanged(const QString& filename)
{
	const URLString url = toURLString(QtUtils::toStdString(urlLineEdit->text()));
	if(url != last_url)
	{
		cancelGaussianSplatConversion(/*delete_temporary_output=*/true);
		last_url = url;
		

		// Process new URL:

		if(resource_manager->isFileForURLPresent(url))
		{
			try
			{
				loadModelIntoPreview(resource_manager->pathForURL(url));
			}
			catch(glare::Exception& e)
			{
				conPrint(e.what());
			}
		}
		else
		{
			// Download the model:
			thread_manager.enqueueMessage(new DownloadResourceMessage(url));
		}
	}
}


void AddObjectDialog::urlEditingFinished()
{}


// Will be called when the user clicks the 'X' button.
void AddObjectDialog::closeEvent(QCloseEvent* event)
{
	++polyHavenRequestGeneration;
	shutdownGL();
}


void AddObjectDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if(event->type() == QEvent::LanguageChange)
	{
		Ui_AddObjectDialog::retranslateUi(this);
		for(int i=0; i<listWidget->count(); ++i)
		{
			QListWidgetItem* item = listWidget->item(i);
			item->setText(tr(item->data(Qt::UserRole + 1).toString().toLatin1().constData()));
		}
		if(polyHavenWebTabs)
		{
			polyHavenWebTabs->setTabText(0,tr("URL"));
			polyHavenWebTabs->setTabText(1,tr("Poly Haven"));
		}
		if(polyHavenSearchEdit) polyHavenSearchEdit->setPlaceholderText(tr("Search models: chair, tree, rock…"));
		if(polyHavenSearchButton) polyHavenSearchButton->setText(tr("Search"));
		if(polyHavenModelList && polyHavenModelList->count()==0 && polyHavenStatusLabel)
			polyHavenStatusLabel->setText(tr("Choose a model to download and preview. <a href=\"https://polyhaven.com\">Powered by Poly Haven</a> · CC0"));
	}
}


void AddObjectDialog::timerEvent(QTimerEvent* event)
{
	processGaussianSplatConversion();
	if(gaussian_splat_preview_render_object.nonNull() && preview_gl_ob.nonNull())
		gaussian_splat_preview_render_object->updateDepthSort(
			objectPreviewGLWidget->cameraForwardsWS(),
			preview_gl_ob->ob_to_world_matrix
		);

	// Once the OpenGL widget has initialised, we can add the model.
	//if(objectPreviewGLWidget->opengl_engine->initSucceeded() && !loaded_model)
	//{
	//	//QString path = settings->value("AddObjectDialogPath").toString();
	//	//filenameChanged(path);
	//	//loaded_model = true;
	//}

	objectPreviewGLWidget->makeCurrent();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	objectPreviewGLWidget->update();
#else
	objectPreviewGLWidget->updateGL();
#endif

	// Check msg queue
	Lock lock(this->msg_queue.getMutex());
	while(!msg_queue.unlockedEmpty())
	{
		Reference<ThreadMessage> msg;
		this->msg_queue.unlockedDequeue(msg);

		if(dynamic_cast<const ResourceDownloadedMessage*>(msg.getPointer()))
		{
			const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg.getPointer());
			if(m->URL != last_url)
				continue; // Ignore a stale URL download if the user switched to another web model.

			//conPrint("ResourceDownloadedMessage, URL: " + m->URL);
			try
			{
				// Now that the model is downloaded, set the result to be the local path where it was downloaded to.
				this->result_path = resource_manager->pathForURL(m->URL); // TODO: catch

				loadModelIntoPreview(resource_manager->pathForURL(m->URL));
			}
			catch(glare::Exception& e)
			{
				conPrint(e.what());
			}
		}
	}
}
