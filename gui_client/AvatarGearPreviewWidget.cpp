/*=====================================================================
AvatarGearPreviewWidget.cpp
---------------------------
Independent avatar-and-gear preview using the same renderer as the
avatar settings preview.
=====================================================================*/
#include "AvatarGearPreviewWidget.h"


#include "AnimationManager.h"
#include "AvatarGroundingUtils.h"
#include "ModelLoading.h"
#include "../dll/include/IndigoException.h"
#include "../dll/IndigoStringUtils.h"
#include "../indigo/TextureServer.h"
#include "../shared/ResourceManager.h"
#include "../utils/Exception.h"
#include "../utils/FileUtils.h"
#include "../utils/StringUtils.h"
#include "graphics/SRGBUtils.h"
#include <QtGui/QHideEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QShowEvent>
#include <QtGui/QWheelEvent>
#include <cmath>
#include <exception>
#include <utility>


namespace
{
class PreviewContextScope
{
public:
	explicit PreviewContextScope(AvatarGearPreviewWidget& widget_, const AvatarGearPreviewWidget::RestoreMainContextCallback& restore_main_context_)
	: widget(widget_), restore_main_context(restore_main_context_)
	{
		widget.makeCurrent();
	}

	~PreviewContextScope()
	{
		widget.doneCurrent();
		if(restore_main_context)
			restore_main_context();
	}

private:
	AvatarGearPreviewWidget& widget;
	const AvatarGearPreviewWidget::RestoreMainContextCallback& restore_main_context;
};


class RestoreMainContextAfterQtEvent
{
public:
	explicit RestoreMainContextAfterQtEvent(AvatarGearPreviewWidget& widget_, const AvatarGearPreviewWidget::RestoreMainContextCallback& restore_main_context_)
	: widget(widget_), restore_main_context(restore_main_context_)
	{}

	~RestoreMainContextAfterQtEvent()
	{
		if(QGLContext::currentContext() == widget.context())
			widget.doneCurrent();
		if(restore_main_context)
			restore_main_context();
	}

private:
	AvatarGearPreviewWidget& widget;
	const AvatarGearPreviewWidget::RestoreMainContextCallback& restore_main_context;
};


static bool isLoadableTexturePath(const std::string& path)
{
	return !path.empty() && !hasExtension(path, "mp4");
}
}


AvatarGearPreviewWidget::AvatarGearPreviewWidget(QWidget* parent)
: AvatarPreviewWidget(parent),
	animation_manager(nullptr),
	avatar_added_to_engine(false),
	interaction_mode(Interaction_Orbit),
	transform_axis(Axis_All),
	transform_dragging(false),
	initialised(false),
	shutting_down(false),
	content_dirty(false),
	waiting_for_resources(false),
	use_basis_textures(false),
	use_optimised_meshes(false),
	optimised_mesh_version(-1),
	gizmo_visible(true)
{
	render_timer.setInterval(33);
	connect(&render_timer, &QTimer::timeout, this, [this]() { renderTick(); });
	resource_retry_timer.start();
}


AvatarGearPreviewWidget::~AvatarGearPreviewWidget()
{
	shutdown();
}


int AvatarGearPreviewWidget::heightForWidth(int width) const
{
	// Avatar Settings uses an almost portrait-square viewport.  Preserve that
	// vertical field of view in the dock so the feet/head cannot be cropped.
	return myMax(420, (int)(width * 0.86f));
}


QSize AvatarGearPreviewWidget::sizeHint() const
{
	return QSize(520, heightForWidth(520));
}


void AvatarGearPreviewWidget::init(
	const std::string& base_dir_path_,
	QSettings* settings_,
	Reference<ResourceManager> resource_manager_,
	AnimationManager* animation_manager_,
	RestoreMainContextCallback restore_main_context_
)
{
	if(initialised)
		return;

	base_dir_path = base_dir_path_;
	resource_manager = resource_manager_;
	animation_manager = animation_manager_;
	restore_main_context = std::move(restore_main_context_);
	owned_texture_server = new TextureServer(/*use_canonical_path_keys=*/false);

	{
		// AvatarPreviewWidget::init() switches to this widget's context.  Keep
		// restoration exception-safe because the main renderer must never be
		// left with the preview context current.
		PreviewContextScope context(*this, restore_main_context);
		AvatarPreviewWidget::init(base_dir_path, settings_, owned_texture_server);
	}

	initialised = true;
	content_dirty = true;
	if(isVisible())
		render_timer.start();
}


void AvatarGearPreviewWidget::setPreviewData(
	const AvatarSettings& avatar_settings_,
	const GearItems& equipped_gear_,
	bool use_basis_textures_,
	bool use_optimised_meshes_,
	int optimised_mesh_version_
)
{
	preview_avatar_settings = avatar_settings_;
	preview_equipped_gear = equipped_gear_;
	use_basis_textures = use_basis_textures_;
	use_optimised_meshes = use_optimised_meshes_;
	optimised_mesh_version = optimised_mesh_version_;
	content_dirty = true;
	waiting_for_resources = false;
	last_error.clear();
	missing_resource_urls.clear();
	resource_retry_timer.restart();
	if(initialised && isVisible() && !render_timer.isActive())
		render_timer.start();
}


void AvatarGearPreviewWidget::resourcesChanged()
{
	content_dirty = true;
	waiting_for_resources = false;
	resource_retry_timer.restart();
}


void AvatarGearPreviewWidget::setSelectedGear(const UID& gear_id)
{
	selected_gear_id = gear_id;
}


void AvatarGearPreviewWidget::setInteractionMode(InteractionMode mode, TransformAxis axis)
{
	interaction_mode = mode;
	transform_axis = axis;
	transform_dragging = false;
}


void AvatarGearPreviewWidget::setGizmoVisible(bool visible)
{
	gizmo_visible = visible;
	update();
}


void AvatarGearPreviewWidget::setTransformDragCallback(TransformDragCallback callback)
{
	transform_drag_callback = std::move(callback);
}


void AvatarGearPreviewWidget::setTransformAxisClickCallback(TransformAxisClickCallback callback)
{
	transform_axis_click_callback = std::move(callback);
}


void AvatarGearPreviewWidget::shutdown()
{
	if(!initialised || shutting_down)
		return;

	shutting_down = true;
	render_timer.stop();
	{
		PreviewContextScope context(*this, restore_main_context);
		clearPreviewObjects();
		AvatarPreviewWidget::shutdown();
	}
	owned_texture_server = nullptr;
	resource_manager = nullptr;
	animation_manager = nullptr;
	initialised = false;
	shutting_down = false;
}


void AvatarGearPreviewWidget::paintGL()
{
	if(!initialised || shutting_down)
	{
		AvatarPreviewWidget::paintGL();
		return;
	}

	try
	{
		rebuildSceneIfNeeded();
		updateGearAttachmentTransforms(); // Uses the pose computed on the preceding frame.
	}
	catch(glare::Exception& e)
	{
		last_error = QString::fromStdString(e.what());
		content_dirty = false;
	}
	catch(Indigo::IndigoException& e)
	{
		last_error = QString::fromStdString(toStdString(e.what()));
		content_dirty = false;
	}
	catch(std::exception& e)
	{
		last_error = QString::fromUtf8(e.what());
		content_dirty = false;
	}
	catch(...)
	{
		last_error = tr("Could not rebuild the equipment preview.");
		content_dirty = false;
	}

	AvatarPreviewWidget::paintGL();

	// OpenGLEngine computes the current animation pose during draw().  Updating
	// again here makes the freshly computed bone transforms ready for the next
	// frame, matching the one-frame attachment flow used by AvatarGraphics.
	try
	{
		updateGearAttachmentTransforms();
	}
	catch(glare::Exception& e)
	{
		last_error = QString::fromStdString(e.what());
	}
	catch(...)
	{
		if(last_error.isEmpty())
			last_error = tr("Could not update the equipment preview.");
	}
}


void AvatarGearPreviewWidget::paintEvent(QPaintEvent* event)
{
	// QGLWidget can repaint in response to expose/update events outside our
	// timer.  Restore the world widget only after QGLWidget has completed the
	// complete paint (including its buffer swap), never from inside paintGL().
	RestoreMainContextAfterQtEvent restore_context(*this, restore_main_context);
	AvatarPreviewWidget::paintEvent(event);
	// The gizmo is an inspection aid, not a mode-specific overlay.  Keep it
	// visible in orbit mode as well; orbit only changes mouse handling.
	if(gizmo_visible && selected_gear_id.valid())
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		const QPoint origin(width() / 2, height() / 2);
		const int extent = qMax(52, qMin(width(), height()) / 6);
		painter.setPen(QPen(QColor(0, 0, 0, 180), 2));
		painter.setBrush(QColor(20, 24, 30, 150));
		painter.drawEllipse(origin, extent + 12, extent + 12);
		const auto draw_axis = [&painter, origin, extent](const QPoint& end, const QColor& colour, const QString& label)
		{
			painter.setPen(QPen(colour, 3));
			painter.drawLine(origin, end);
			painter.setBrush(colour);
			painter.drawEllipse(end, 4, 4);
			painter.setPen(colour);
			painter.drawText(end + QPoint(6, 5), label);
		};
		draw_axis(origin + QPoint(extent, 0), QColor(220, 70, 70), QStringLiteral("X"));
		draw_axis(origin + QPoint(0, -extent), QColor(75, 190, 90), QStringLiteral("Y"));
		draw_axis(origin + QPoint(-extent / 2, extent / 2), QColor(75, 120, 230), QStringLiteral("Z"));
	}
}


void AvatarGearPreviewWidget::resizeEvent(QResizeEvent* event)
{
	// resizeGL() is another Qt-owned entry point that may make this widget's
	// context current without passing through renderTick().
	RestoreMainContextAfterQtEvent restore_context(*this, restore_main_context);
	AvatarPreviewWidget::resizeEvent(event);
}


void AvatarGearPreviewWidget::mousePressEvent(QMouseEvent* event)
{
	if(interaction_mode == Interaction_Orbit)
	{
		AvatarPreviewWidget::mousePressEvent(event);
		return;
	}

	if(event->button() == Qt::LeftButton && selected_gear_id.valid() && transform_drag_callback)
	{
		if(gizmo_visible && transform_axis_click_callback)
		{
			const QPoint origin(width() / 2, height() / 2);
			const int extent = qMax(36, qMin(width(), height()) / 8);
			const QPoint ends[] = { origin + QPoint(extent, 0), origin + QPoint(0, -extent), origin + QPoint(-extent / 2, extent / 2) };
			const TransformAxis axes[] = { Axis_X, Axis_Y, Axis_Z };
			for(int i=0; i<3; ++i)
				if((event->pos() - ends[i]).manhattanLength() < 18)
				{
					transform_axis_click_callback(axes[i]);
					break;
				}
		}
		transform_dragging = true;
		transform_drag_origin = event->pos();
		// A zero-delta callback marks the beginning of a transform gesture, so
		// the owning panel can snapshot the item's starting transform before any
		// move events apply cumulative deltas.
		transform_drag_callback(interaction_mode, transform_axis, QPoint(0, 0), /*finished=*/false);
		event->accept();
		return;
	}

	AvatarPreviewWidget::mousePressEvent(event);
}


void AvatarGearPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
	if(interaction_mode == Interaction_Orbit)
	{
		AvatarPreviewWidget::mouseMoveEvent(event);
		return;
	}

	if(transform_dragging && (event->buttons() & Qt::LeftButton) && transform_drag_callback)
	{
		transform_drag_callback(interaction_mode, transform_axis, event->pos() - transform_drag_origin, /*finished=*/false);
		event->accept();
		return;
	}

	AvatarPreviewWidget::mouseMoveEvent(event);
}


void AvatarGearPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if(interaction_mode != Interaction_Orbit && event->button() == Qt::LeftButton && transform_dragging)
	{
		if(transform_drag_callback)
			transform_drag_callback(interaction_mode, transform_axis, event->pos() - transform_drag_origin, /*finished=*/true);
		transform_dragging = false;
		event->accept();
		return;
	}

	AvatarPreviewWidget::mouseReleaseEvent(event);
}


void AvatarGearPreviewWidget::wheelEvent(QWheelEvent* event)
{
	// Wheel zoom remains available while a gear transform tool is selected.
	AvatarPreviewWidget::wheelEvent(event);
}


void AvatarGearPreviewWidget::showEvent(QShowEvent* event)
{
	AvatarPreviewWidget::showEvent(event);
	if(initialised && !shutting_down)
		render_timer.start();
}


void AvatarGearPreviewWidget::hideEvent(QHideEvent* event)
{
	render_timer.stop();
	transform_dragging = false;
	AvatarPreviewWidget::hideEvent(event);
}


void AvatarGearPreviewWidget::renderTick()
{
	if(!initialised || shutting_down || !isVisible())
		return;

	if(waiting_for_resources && resource_retry_timer.elapsed() >= 500)
	{
		if(anyMissingResourceIsAvailable())
		{
			content_dirty = true;
			waiting_for_resources = false;
		}
		resource_retry_timer.restart();
	}

	// Always queue a QWidget paint event.  QGLWidget::updateGL() calls paintGL()
	// directly in Qt 5 and would bypass our post-paint context restoration.
	update();
}


bool AvatarGearPreviewWidget::anyMissingResourceIsAvailable() const
{
	if(resource_manager.isNull())
		return false;

	for(size_t i=0; i<missing_resource_urls.size(); ++i)
	{
		const URLString& url = missing_resource_urls[i];
		const std::string url_string = toStdString(url);
		if(FileUtils::fileExists(url_string))
			return true;
		try
		{
			if(resource_manager->isFileForURLPresent(url))
				return true;
		}
		catch(glare::Exception&)
		{
		}
	}
	return false;
}


void AvatarGearPreviewWidget::rebuildSceneIfNeeded()
{
	if(!content_dirty || opengl_engine.isNull() || !opengl_engine->initSucceeded())
		return;

	last_error.clear();
	missing_resource_urls.clear();
	waiting_for_resources = false;

	std::string avatar_model_path;
	if(preview_avatar_settings.model_url.empty())
		avatar_model_path = base_dir_path + "/data/resources/xbot_glb_3242545562312850498.bmesh";
	else
	{
		URLString desired_avatar_url;
		avatar_model_path = resolveModelPath(preview_avatar_settings.model_url, desired_avatar_url);
		if(avatar_model_path.empty())
		{
			clearPreviewObjects();
			last_error = tr("Waiting for the avatar model resource.");
			content_dirty = false;
			waiting_for_resources = true;
			resource_retry_timer.restart();
			return;
		}
	}

	std::vector<std::string> gear_model_paths(preview_equipped_gear.items.size());
	for(size_t i=0; i<preview_equipped_gear.items.size(); ++i)
	{
		if(preview_equipped_gear.items[i].isNull())
			continue;
		URLString desired_gear_url;
		gear_model_paths[i] = resolveModelPath(preview_equipped_gear.items[i]->model_url, desired_gear_url);
		if(gear_model_paths[i].empty())
			waiting_for_resources = true;
	}

	clearPreviewObjects();

	ModelLoading::MakeGLObjectResults avatar_results;
	ModelLoading::makeGLObjectForModelFile(
		*opengl_engine,
		*opengl_engine->vert_buf_allocator,
		/*allocator=*/nullptr,
		avatar_model_path,
		/*do_opengl_stuff=*/true,
		avatar_results
	);
	avatar_gl_ob = avatar_results.gl_ob;
	if(avatar_gl_ob.isNull() || avatar_gl_ob->mesh_data.isNull())
		throw glare::Exception("Loading the avatar preview returned no renderable mesh.");

	if(preview_avatar_settings.model_url.empty())
	{
		avatar_gl_ob->ob_to_world_matrix = Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());
		if(avatar_gl_ob->materials.size() >= 2)
		{
			avatar_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Avatar::defaultMat0Col());
			avatar_gl_ob->materials[0].metallic_frac = Avatar::default_mat0_metallic_frac;
			avatar_gl_ob->materials[0].roughness = Avatar::default_mat0_roughness;
			avatar_gl_ob->materials[0].albedo_texture = nullptr;
			avatar_gl_ob->materials[0].tex_path.clear();
			avatar_gl_ob->materials[1].albedo_linear_rgb = toLinearSRGB(Avatar::defaultMat1Col());
			avatar_gl_ob->materials[1].metallic_frac = Avatar::default_mat1_metallic_frac;
			avatar_gl_ob->materials[1].roughness = Avatar::default_mat0_roughness;
			avatar_gl_ob->materials[1].albedo_texture = nullptr;
			avatar_gl_ob->materials[1].tex_path.clear();
		}
	}

	if(!preview_avatar_settings.materials.empty())
		applyWorldMaterials(*avatar_gl_ob, preview_avatar_settings.materials);
	loadObjectTextures(*avatar_gl_ob);

	AnimationData& animation_data = avatar_gl_ob->mesh_data->animation_data;
	AvatarGrounding::GroundingInfo grounding = AvatarGrounding::computeGroundingInfo(
		animation_data,
		/*use_retarget_adjustment=*/false,
		AvatarGrounding::kDefaultToeBottomOffsetM
	);
	float foot_bottom_height = grounding.foot_bottom_height;
	float avatar_anchor_height = grounding.anchor_height_from_origin;

	if(animation_manager && resource_manager.nonNull())
	{
		if(!animation_data.retarget_adjustments_set)
			animation_data.loadAndRetargetAnim(*animation_manager->getAnimation("Idle.subanim", *resource_manager));
		else if(animation_data.getAnimationIndex("Idle") < 0)
			animation_data.appendAnimationData(*animation_manager->getAnimation("Idle.subanim", *resource_manager));

		grounding = AvatarGrounding::computeGroundingInfo(
			animation_data,
			/*use_retarget_adjustment=*/true,
			AvatarGrounding::kRetargetedToeBottomOffsetM
		);
		foot_bottom_height = grounding.foot_bottom_height;
		avatar_anchor_height = grounding.anchor_height_from_origin;
	}

	avatar_gl_ob->current_anim_i = myMax(0, animation_data.getAnimationIndex("Idle"));
	// Avatar Settings stores T(-anchor) * imported_model_transform.  Recover its
	// exact visual preview transform T(-foot) * imported_model_transform from
	// the server setting.  This also preserves scale/up-axis data when an
	// optimised bmesh itself loads with an identity transform.
	if(!preview_avatar_settings.model_url.empty() &&
		!(preview_avatar_settings.pre_ob_to_world_matrix == Matrix4f::identity()))
	{
		avatar_gl_ob->ob_to_world_matrix =
			Matrix4f::translationMatrix(0, 0, avatar_anchor_height - foot_bottom_height) *
			preview_avatar_settings.pre_ob_to_world_matrix;
	}
	else
	{
		avatar_gl_ob->ob_to_world_matrix =
			Matrix4f::translationMatrix(0, 0, -foot_bottom_height) * avatar_gl_ob->ob_to_world_matrix;
	}
	opengl_engine->addObject(avatar_gl_ob);
	avatar_added_to_engine = true;

	preview_gear.reserve(preview_equipped_gear.items.size());
	for(size_t i=0; i<preview_equipped_gear.items.size(); ++i)
	{
		const GearItemRef& item = preview_equipped_gear.items[i];
		if(item.isNull() || gear_model_paths[i].empty())
			continue;

		try
		{
			ModelLoading::MakeGLObjectResults gear_results;
			ModelLoading::makeGLObjectForModelFile(
				*opengl_engine,
				*opengl_engine->vert_buf_allocator,
				/*allocator=*/nullptr,
				gear_model_paths[i],
				/*do_opengl_stuff=*/true,
				gear_results
			);
			if(gear_results.gl_ob.isNull() || gear_results.gl_ob->mesh_data.isNull())
				throw glare::Exception("Loading the gear preview returned no renderable mesh.");

			PreviewGear gear;
			gear.item = item;
			gear.gl_ob = gear_results.gl_ob;
			gear.bone_name = item->bone_name;
			gear.bone_node_i = animation_data.getNodeIndex(gear.bone_name);
			if(!item->materials.empty())
				applyWorldMaterials(*gear.gl_ob, item->materials);
			loadObjectTextures(*gear.gl_ob);

			// Keep it out of view until OpenGLEngine has computed the first
			// animated bone pose for the independent avatar object.  Add an
			// item even when its current bone name is invalid: the editor can
			// then switch it to a valid bone without reloading its GPU resources.
			gear.gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(100000.f, 100000.f, 100000.f);
			opengl_engine->addObject(gear.gl_ob);
			gear.added_to_engine = true;
			if(gear.bone_node_i < 0)
			{
				const QString bone_error = tr("Gear item '%1' references missing avatar bone '%2'.")
					.arg(QString::fromStdString(item->name), QString::fromStdString(item->bone_name));
				if(last_error.isEmpty())
					last_error = bone_error;
			}
			preview_gear.push_back(gear);
		}
		catch(glare::Exception& e)
		{
			if(last_error.isEmpty())
				last_error = tr("Could not load gear item '%1': %2")
					.arg(QString::fromStdString(item->name), QString::fromStdString(e.what()));
		}
		catch(Indigo::IndigoException& e)
		{
			if(last_error.isEmpty())
				last_error = tr("Could not load gear item '%1': %2")
					.arg(QString::fromStdString(item->name), QString::fromStdString(toStdString(e.what())));
		}
		catch(std::exception& e)
		{
			if(last_error.isEmpty())
				last_error = tr("Could not load gear item '%1': %2")
					.arg(QString::fromStdString(item->name), QString::fromUtf8(e.what()));
		}
	}

	content_dirty = false;
	if(waiting_for_resources)
		resource_retry_timer.restart();
}


void AvatarGearPreviewWidget::clearPreviewObjects()
{
	if(opengl_engine.isNull())
	{
		avatar_gl_ob = nullptr;
		avatar_added_to_engine = false;
		preview_gear.clear();
		return;
	}

	for(size_t i=0; i<preview_gear.size(); ++i)
		if(preview_gear[i].added_to_engine && preview_gear[i].gl_ob)
			opengl_engine->removeObject(preview_gear[i].gl_ob);
	preview_gear.clear();

	if(avatar_gl_ob)
	{
		if(avatar_added_to_engine)
			opengl_engine->removeObject(avatar_gl_ob);
		avatar_gl_ob = nullptr;
		avatar_added_to_engine = false;
	}
}


void AvatarGearPreviewWidget::updateGearAttachmentTransforms()
{
	if(opengl_engine.isNull() || avatar_gl_ob.isNull() || avatar_gl_ob->mesh_data.isNull())
		return;

	const AnimationData& animation_data = avatar_gl_ob->mesh_data->animation_data;
	for(size_t i=0; i<preview_gear.size(); ++i)
	{
		PreviewGear& gear = preview_gear[i];
		if(!gear.added_to_engine || gear.gl_ob.isNull() || gear.item.isNull())
			continue;

		if(gear.bone_name != gear.item->bone_name)
		{
			gear.bone_name = gear.item->bone_name;
			gear.bone_node_i = animation_data.getNodeIndex(gear.bone_name);
		}

		if(gear.bone_node_i >= 0 && gear.bone_node_i < (int)avatar_gl_ob->anim_node_data.size())
		{
			gear.gl_ob->ob_to_world_matrix =
				(avatar_gl_ob->ob_to_world_matrix * avatar_gl_ob->anim_node_data[gear.bone_node_i].node_hierarchical_to_object) *
				gear.item->gearObToBoneSpaceMatrix();
			opengl_engine->updateObjectTransformData(*gear.gl_ob);
		}
		else
		{
			// Invalid or not-yet-available bones must never leave an item at its
			// preceding attachment transform.
			gear.gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(100000.f, 100000.f, 100000.f);
			opengl_engine->updateObjectTransformData(*gear.gl_ob);
		}
	}
}


std::string AvatarGearPreviewWidget::resolveModelPath(const URLString& base_url, URLString& desired_url_out)
{
	if(base_url.empty())
		return std::string();

	const std::string base_string = toStdString(base_url);
	if(FileUtils::fileExists(base_string))
	{
		desired_url_out = base_url;
		return base_string;
	}

	desired_url_out = Avatar::getLODModelURLForLevel(
		base_url,
		/*level=*/0,
		Avatar::GetLODModelURLOptions(use_optimised_meshes, optimised_mesh_version)
	);

	const std::string preferred_path = resolveResourcePath(desired_url_out, base_url);
	if(preferred_path.empty())
	{
		addMissingResourceURL(desired_url_out);
		addMissingResourceURL(base_url);
	}
	return preferred_path;
}


std::string AvatarGearPreviewWidget::resolveResourcePath(const URLString& preferred_url, const URLString& fallback_url)
{
	if(resource_manager.isNull())
		return std::string();

	const URLString candidates[] = { preferred_url, fallback_url };
	for(size_t i=0; i<2; ++i)
	{
		const URLString& url = candidates[i];
		if(url.empty() || (i == 1 && url == candidates[0]))
			continue;
		const std::string url_string = toStdString(url);
		if(FileUtils::fileExists(url_string))
			return url_string;
		try
		{
			if(resource_manager->isFileForURLPresent(url))
			{
				const std::string path = resource_manager->pathForURL(url);
				if(FileUtils::fileExists(path))
					return path;
			}
		}
		catch(glare::Exception&)
		{
		}
	}
	return std::string();
}


void AvatarGearPreviewWidget::addMissingResourceURL(const URLString& url)
{
	if(url.empty())
		return;
	for(size_t i=0; i<missing_resource_urls.size(); ++i)
		if(missing_resource_urls[i] == url)
			return;
	missing_resource_urls.push_back(url);
}


void AvatarGearPreviewWidget::applyWorldMaterials(GLObject& ob, const std::vector<WorldMaterialRef>& materials)
{
	const WorldMaterial::GetURLOptions url_options(use_basis_textures, /*arena allocator=*/nullptr);
	const size_t count = myMin(ob.materials.size(), materials.size());
	for(size_t i=0; i<count; ++i)
	{
		if(materials[i].isNull())
			continue;

		const WorldMaterial& world_mat = *materials[i];
		OpenGLMaterial& gl_mat = ob.materials[i];
		// This applies every scalar/flag/matrix property from the authoritative
		// server material.  Texture URL fields are replaced with local resource
		// paths immediately below.
		ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(world_mat, gl_mat);

		if(!world_mat.colour_texture_url.empty())
		{
			const URLString preferred_url = world_mat.getLODTextureURLForLevel(url_options, world_mat.colour_texture_url, 0, world_mat.colourTexHasAlpha());
			const std::string path = resolveResourcePath(preferred_url, world_mat.colour_texture_url);
			gl_mat.tex_path = path;
			if(path.empty()) { addMissingResourceURL(preferred_url); addMissingResourceURL(world_mat.colour_texture_url); }
		}
		else
			gl_mat.tex_path.clear();

		if(!world_mat.emission_texture_url.empty())
		{
			const URLString preferred_url = world_mat.getLODTextureURLForLevel(url_options, world_mat.emission_texture_url, 0, /*has alpha=*/false);
			const std::string path = resolveResourcePath(preferred_url, world_mat.emission_texture_url);
			gl_mat.emission_tex_path = path;
			if(path.empty()) { addMissingResourceURL(preferred_url); addMissingResourceURL(world_mat.emission_texture_url); }
		}
		else
			gl_mat.emission_tex_path.clear();

		if(!world_mat.roughness.texture_url.empty())
		{
			const URLString preferred_url = world_mat.getLODTextureURLForLevel(url_options, world_mat.roughness.texture_url, 0, /*has alpha=*/false);
			const std::string path = resolveResourcePath(preferred_url, world_mat.roughness.texture_url);
			gl_mat.metallic_roughness_tex_path = path;
			if(path.empty()) { addMissingResourceURL(preferred_url); addMissingResourceURL(world_mat.roughness.texture_url); }
		}
		else
			gl_mat.metallic_roughness_tex_path.clear();

		if(!world_mat.normal_map_url.empty())
		{
			const URLString preferred_url = world_mat.getLODTextureURLForLevel(url_options, world_mat.normal_map_url, 0, /*has alpha=*/false);
			const std::string path = resolveResourcePath(preferred_url, world_mat.normal_map_url);
			gl_mat.normal_map_path = path;
			if(path.empty()) { addMissingResourceURL(preferred_url); addMissingResourceURL(world_mat.normal_map_url); }
		}
		else
			gl_mat.normal_map_path.clear();
	}

	if(!missing_resource_urls.empty())
		waiting_for_resources = true;
}


void AvatarGearPreviewWidget::loadObjectTextures(GLObject& ob)
{
	for(size_t i=0; i<ob.materials.size(); ++i)
	{
		OpenGLMaterial& material = ob.materials[i];
		try
		{
			const std::string path = std::string(material.tex_path);
			if(isLoadableTexturePath(path))
				material.albedo_texture = opengl_engine->getTexture(path);
		}
		catch(glare::Exception&)
		{
			material.albedo_texture = nullptr;
		}

		try
		{
			const std::string path = std::string(material.emission_tex_path);
			if(isLoadableTexturePath(path))
				material.emission_texture = opengl_engine->getTexture(path);
		}
		catch(glare::Exception&)
		{
			material.emission_texture = nullptr;
		}

		try
		{
			const std::string path = std::string(material.metallic_roughness_tex_path);
			if(isLoadableTexturePath(path))
			{
				TextureParams params;
				params.use_sRGB = false;
				material.metallic_roughness_texture = opengl_engine->getTexture(path, params);
			}
		}
		catch(glare::Exception&)
		{
			material.metallic_roughness_texture = nullptr;
		}

		try
		{
			const std::string path = std::string(material.normal_map_path);
			if(isLoadableTexturePath(path))
			{
				TextureParams params;
				params.use_sRGB = false;
				material.normal_map = opengl_engine->getTexture(path, params);
			}
		}
		catch(glare::Exception&)
		{
			material.normal_map = nullptr;
		}
	}
}
