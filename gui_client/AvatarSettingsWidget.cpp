/*=====================================================================
AvatarSettingsWidget.cpp
------------------------
Dock-friendly Avatar settings UI (Qt).
=====================================================================*/
#include "AvatarSettingsWidget.h"

#include "AddObjectDialog.h"
#include "AnimationManager.h"
#include "GUIClient.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include "../indigo/TextureServer.h"
#include "../qt/QtUtils.h"
#include "../qt/SignalBlocker.h"
#include "../utils/ConPrint.h"
#include "graphics/SRGBUtils.h"

#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QPushButton>


static const char* anim_names[] = {
	"Walking",
	"Idle",
	"Walking Backward",
	"Running",
	"Running Backward",
	"Floating",
	"Flying",
	"Left Turn",
	"Right Turn",
};


AvatarSettingsWidget::AvatarSettingsWidget(
	QWidget* parent,
	const std::string& base_dir_path_,
	QSettings* settings_,
	Reference<ResourceManager> resource_manager_,
	AnimationManager* anim_manager_,
	GUIClient* gui_client_,
	std::function<void()> make_main_gl_context_current_
)
:	QWidget(parent),
	base_dir_path(base_dir_path_),
	settings(settings_),
	resource_manager(resource_manager_),
	anim_manager(anim_manager_),
	gui_client(gui_client_),
	make_main_gl_context_current(std::move(make_main_gl_context_current_)),
	done_initial_load(false),
	pre_ob_to_world_matrix(Matrix4f::identity())
{
	setupUi(this);

	texture_server = new TextureServer(/*use_canonical_path_keys=*/false);

	std::string display_str;
	display_str += "<br/><a href=\"https://substrata.readyplayer.me/\">Create a ReadyPlayerMe avatar</a>.  After creating, download and select in file browser above.";
	display_str += "<br/><br/><a href=\"https://avaturn.me/\">Create a AvaturnMe avatar</a>.  After creating, download and select in file browser above.";
	this->createReadyPlayerMeLabel->setText(QtUtils::toQString(display_str));
	this->createReadyPlayerMeLabel->setOpenExternalLinks(true);

	this->avatarPreviewGLWidget->init(base_dir_path, settings_, texture_server);

	{
		SignalBlocker b(this->avatarSelectWidget);
		this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
		this->avatarSelectWidget->setFilename(settings->value("avatarPath").toString());
	}

	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(avatarFilenameChanged(QString&)));
	connect(this->animationComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(animationComboBoxIndexChanged(int)));

	if(QPushButton* apply_btn = this->buttonBox->button(QDialogButtonBox::Apply))
		connect(apply_btn, SIGNAL(clicked()), this, SLOT(onApplyClicked()));

	if(QPushButton* close_btn = this->buttonBox->button(QDialogButtonBox::Close))
		connect(close_btn, SIGNAL(clicked()), this, SLOT(onCloseClicked()));

	// Drive preview redraw and initial load.
	connect(&tick_timer, SIGNAL(timeout()), this, SLOT(onTick()));
	tick_timer.start(10);
}


AvatarSettingsWidget::~AvatarSettingsWidget()
{
	shutdownGL();
}


void AvatarSettingsWidget::shutdownGL()
{
	// Make sure we have set the widget gl context to current as we destroy OpenGL stuff.
	if(this->avatarPreviewGLWidget)
		this->avatarPreviewGLWidget->makeCurrent();

	preview_gl_ob = NULL;

	if(this->avatarPreviewGLWidget)
		this->avatarPreviewGLWidget->shutdown();
}


void AvatarSettingsWidget::avatarFilenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);
	const bool changed = this->result_path != path;
	this->result_path = path;

	if(changed)
		loadModelIntoPreview(path, /*show_error_dialogs=*/true);
}


void AvatarSettingsWidget::animationComboBoxIndexChanged(int index)
{
	if(preview_gl_ob.isNull())
		return;

	const std::string anim_name = QtUtils::toStdString(this->animationComboBox->itemText(index));
	preview_gl_ob->current_anim_i = myMax(0, preview_gl_ob->mesh_data->animation_data.getAnimationIndex(anim_name));
}


void AvatarSettingsWidget::onApplyClicked()
{
	this->settings->setValue("avatarPath", this->avatarSelectWidget->filename());

	if(loaded_mesh.isNull())
		return;

	// Ensure main GL context is current before creating GPU resources for the main scene.
	if(make_main_gl_context_current)
		make_main_gl_context_current();

	try
	{
		gui_client->updateOurAvatarModel(loaded_mesh, result_path, pre_ob_to_world_matrix, loaded_materials);
	}
	catch(glare::Exception& e)
	{
		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
}


void AvatarSettingsWidget::onCloseClicked()
{
	emit requestClose();
}


void AvatarSettingsWidget::onTick()
{
	avatarPreviewGLWidget->makeCurrent();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	avatarPreviewGLWidget->update();
#else
	avatarPreviewGLWidget->updateGL();
#endif

	// Once the OpenGL widget has initialised, we can add the model.
	if(avatarPreviewGLWidget->opengl_engine.nonNull() && avatarPreviewGLWidget->opengl_engine->initSucceeded() && !done_initial_load)
	{
		const QString path = settings->value("avatarPath").toString();
		this->result_path = QtUtils::toStdString(path);
		loadModelIntoPreview(QtUtils::toStdString(path), /*show_error_dialogs=*/false);
		done_initial_load = true;
	}
}


void AvatarSettingsWidget::loadModelIntoPreview(const std::string& local_path, bool show_error_dialogs)
{
	const std::string use_local_path = local_path.empty() ?
		(base_dir_path + "/data/resources/xbot_glb_3242545562312850498.bmesh") :
		local_path;

	this->avatarPreviewGLWidget->makeCurrent();
	this->pre_ob_to_world_matrix = Matrix4f::identity();

	try
	{
		if(preview_gl_ob.nonNull())
			avatarPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);

		ModelLoading::MakeGLObjectResults results;
		ModelLoading::makeGLObjectForModelFile(
			*avatarPreviewGLWidget->opengl_engine,
			*avatarPreviewGLWidget->opengl_engine->vert_buf_allocator,
			/*allocator=*/nullptr,
			use_local_path,
			/*do_opengl_stuff=*/true,
			results
		);

		this->preview_gl_ob = results.gl_ob;
		this->loaded_mesh = results.batched_mesh;
		this->loaded_materials = results.materials;

		if(local_path.empty()) // If we used xbot_glb_3242545562312850498.bmesh we need to rotate it upright
		{
			this->preview_gl_ob->ob_to_world_matrix = Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());

			this->preview_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Avatar::defaultMat0Col());
			this->preview_gl_ob->materials[0].metallic_frac = Avatar::default_mat0_metallic_frac;
			this->preview_gl_ob->materials[0].roughness = Avatar::default_mat0_roughness;
			this->preview_gl_ob->materials[0].albedo_texture = NULL;
			this->preview_gl_ob->materials[0].tex_path.clear();

			this->preview_gl_ob->materials[1].albedo_linear_rgb = toLinearSRGB(Avatar::defaultMat1Col());
			this->preview_gl_ob->materials[1].metallic_frac = Avatar::default_mat1_metallic_frac;
			this->preview_gl_ob->materials[1].albedo_texture = NULL;
			this->preview_gl_ob->materials[1].tex_path.clear();

			loaded_materials.resize(2);
			loaded_materials[0] = new WorldMaterial();
			loaded_materials[0]->colour_rgb = Avatar::defaultMat0Col();
			loaded_materials[0]->metallic_fraction.val = Avatar::default_mat0_metallic_frac;
			loaded_materials[0]->roughness.val = Avatar::default_mat0_roughness;

			loaded_materials[1] = new WorldMaterial();
			loaded_materials[1]->colour_rgb = Avatar::defaultMat1Col();
			loaded_materials[1]->metallic_fraction.val = Avatar::default_mat1_metallic_frac;
		}

		Vec4f original_toe_pos = preview_gl_ob->mesh_data->animation_data.getNodePositionModelSpace("LeftToe_End", /*use_retarget_adjustment=*/false);
		float foot_bottom_height = original_toe_pos[1] - 0.0362269469f;

		// Append/retarget animations.
		preview_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(*anim_manager->getAnimation("Idle.subanim", *resource_manager));
		for(size_t i = 0; i < staticArrayNumElems(anim_names); ++i)
			preview_gl_ob->mesh_data->animation_data.appendAnimationData(*anim_manager->getAnimation(URLString(anim_names[i]) + ".subanim", *resource_manager));

		Vec4f new_toe_pos = preview_gl_ob->mesh_data->animation_data.getNodePositionModelSpace("LeftToe_End", /*use_retarget_adjustment=*/true);
		foot_bottom_height = new_toe_pos[1] - 0.03f;

		// Populate animation combobox.
		animationComboBox->clear();
		for(size_t i = 0; i < preview_gl_ob->mesh_data->animation_data.animations.size(); ++i)
			animationComboBox->addItem(QtUtils::toQString(preview_gl_ob->mesh_data->animation_data.animations[i]->name));
		animationComboBox->setMaxVisibleItems(50);

		preview_gl_ob->current_anim_i = myMax(0, preview_gl_ob->mesh_data->animation_data.getAnimationIndex("Idle"));
		SignalBlocker::setCurrentIndex(animationComboBox, preview_gl_ob->current_anim_i);

		// Construct transformation to bring avatars to z-up and standing on the ground.
		this->pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -1.67 - foot_bottom_height) * preview_gl_ob->ob_to_world_matrix;
		preview_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -foot_bottom_height) * preview_gl_ob->ob_to_world_matrix;

		// Load textures for preview.
		AddObjectDialog::tryLoadTexturesForPreviewOb(preview_gl_ob, this->loaded_materials, avatarPreviewGLWidget->opengl_engine.ptr(), *texture_server, this);

		avatarPreviewGLWidget->opengl_engine->addObject(preview_gl_ob);
	}
	catch(glare::Exception& e)
	{
		this->loaded_mesh = NULL;
		if(show_error_dialogs)
			QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
}
