#include "AvatarPreviewWidget.h" // IncludeWindows must precede GUIClient/Qt headers.
#include "AnimationPreviewWidget.h"
#include "AvatarGroundingUtils.h"
#include "GUIClient.h"
#include "ModelLoading.h"
#include "GestureAnimationRetarget.h"
#include "../shared/ResourceManager.h"
#include <indigo/TextureServer.h>
#include <utils/FileInStream.h>
#include <utils/FileUtils.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QHideEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace
{
// Covers explicit GL work as well as Qt-initiated paint/resize/show events.
struct RestoreContext
{
	QPointer<AvatarPreviewWidget> preview;
	std::function<void()> restore;
	bool enabled = true;
	~RestoreContext() noexcept
	{
		if(!enabled) return;
		try { if(preview) preview->doneCurrent(); } catch(...) {}
		try { if(restore) restore(); } catch(...) {}
	}
};

QString currentError()
{
	try { throw; }
	catch(const glare::Exception& e) { return QString::fromStdString(e.what()); }
	catch(const std::exception& e) { return QString::fromUtf8(e.what()); }
	catch(...) { return QStringLiteral("Unknown preview error"); }
}

class AnimationPreviewRenderer final : public AvatarPreviewWidget
{
public:
	AnimationPreviewRenderer(QWidget* parent, const std::function<void()>& restore_, std::function<void(QString)> failed_)
	: AvatarPreviewWidget(parent), restore(restore_), failed(std::move(failed_)) {}
	bool failed_render = false;
protected:
	bool event(QEvent* event) override
	{
		// Nested events inside an explicit GL operation leave its context intact;
		// that operation's outer guard performs the final restoration.
		RestoreContext context_restore{this, restore, QGLContext::currentContext() != context()};
		try { return AvatarPreviewWidget::event(event); }
		catch(...) { failed_render = true; failed(currentError()); return true; }
	}
	void paintGL() override
	{
		if(isVisible() && !failed_render && opengl_engine.nonNull() && opengl_engine->initSucceeded())
			AvatarPreviewWidget::paintGL();
	}
private:
	std::function<void()> restore;
	std::function<void(QString)> failed;
};

// Only the rest skeleton is retained, never a model's embedded clip collection.
void copyRestSkeleton(const AnimationData& source, AnimationData& dest)
{
	dest.nodes = source.nodes;
	dest.sorted_nodes = source.sorted_nodes;
	dest.joint_nodes = source.joint_nodes;
	dest.vrm_data = source.vrm_data;
	dest.retarget_adjustments_set = false;
}
}

struct AnimationPreviewWidget::Impl
{
	Impl(AnimationPreviewWidget* owner_, const std::string& base_, QSettings* settings_, GUIClient* client_, std::function<void()> restore_)
	: owner(owner_), base(base_), settings(settings_), client(client_), restore(std::move(restore_))
	{
		layout = new QVBoxLayout(owner);
		layout->setContentsMargins(0, 0, 0, 0);
		label = new QLabel(AnimationPreviewWidget::tr("Выберите анимацию для предпросмотра."), owner);
		label->setWordWrap(true);
		label->setTextFormat(Qt::PlainText);
		layout->addWidget(label);
		tick_timer.setTimerType(Qt::PreciseTimer);
		// Round upwards: never schedule more than 30 redraws per second.
		tick_timer.setInterval(34);
		QObject::connect(&tick_timer, &QTimer::timeout, owner, [this]() { tick(); });
	}

	void status(const QString& text)
	{
		label->setText(text);
		label->setVisible(!renderer || failed_preview); // Normal status is already below the preview in the panel.
		emit owner->statusChanged(text);
	}
	void playback(bool value)
	{
		if(playing != value) { playing = value; emit owner->playbackChanged(playing); }
		clock.restart();
	}
	void fail(const QString& error)
	{
		tick_timer.stop();
		failed_preview = true;
		if(renderer) { renderer->failed_render = true; renderer->hide(); }
		playback(false);
		status(AnimationPreviewWidget::tr("Предпросмотр недоступен: %1").arg(error));
	}

	// Does not insert missing URLs into ResourceManager or request downloads.
	std::string localPath(const URLString& url) const
	{
		if(!client || client->resource_manager.isNull() || url.empty()) return std::string();
		ResourceRef resource = client->resource_manager->getExistingResourceForURL(url);
		if(resource.isNull()) return std::string();
		const std::string path = client->resource_manager->getLocalAbsPathForResource(*resource);
		return FileUtils::fileExists(path) ? path : std::string();
	}

	void open()
	{
		if(shutdown || renderer || !owner->isVisible() || selected.empty()) return;
		try
		{
			if(!settings) throw glare::Exception("Preview settings are unavailable.");
			// Report an incomplete installation in the UI, not just in the render log.
			if(!FileUtils::fileExists(base + "/data/resources/obstacle.png"))
				throw glare::Exception("Preview ground texture is not installed.");
			renderer = new AnimationPreviewRenderer(owner, restore, [this](QString error) { fail(error); });
			renderer->hide();
			RestoreContext context{renderer, restore};
			textures = new TextureServer(/*use_canonical_path_keys=*/false);
			renderer->init(base, settings, textures);
			renderer->setPreviewTimeOverride(0.0);
			layout->insertWidget(0, renderer, 1);
			renderer->show();
			pending = true;
			failed_preview = renderer->failed_render;
			clock.restart();
			if(!renderer->failed_render) tick_timer.start();
		}
		catch(...) { fail(currentError()); }
	}

	void release()
	{
		tick_timer.stop();
		clock.invalidate();
		if(!renderer) return;
		// Delete the child/context inside this scope; restore main context AFTER deletion.
		RestoreContext context{nullptr, restore};
		renderer->makeCurrent();
		object = nullptr;
		rest = nullptr;
		inserted = false;
		renderer->shutdown();
		delete renderer;
		renderer = nullptr;
		textures = nullptr;
		pending = true;
	}

	void loadTextures(OpenGLMaterial& mat)
	{
		auto load = [this](OpenGLTextureKey& key, Reference<OpenGLTexture>& texture, bool linear)
		{
			const std::string path(key);
			if(path.empty()) return;
			try
			{
				if(!FileUtils::fileExists(path) || hasExtension(path, "mp4"))
					throw glare::Exception("Texture is unavailable locally or unsupported in preview.");
				TextureParams params;
				params.use_sRGB = !linear;
				texture = renderer->opengl_engine->getTexture(path, params);
			}
			catch(...) { incomplete_textures = true; key.clear(); texture = nullptr; }
		};
		load(mat.tex_path, mat.albedo_texture, false);
		load(mat.metallic_roughness_tex_path, mat.metallic_roughness_texture, true);
		load(mat.emission_tex_path, mat.emission_texture, false);
		load(mat.normal_map_path, mat.normal_map, true);
	}

	void loadModel()
	{
		const bool server_avatar = client && client->isLoggedIn();
		const AvatarSettings avatar = server_avatar ? client->logged_in_avatar_settings : AvatarSettings();
		std::string path = localPath(avatar.model_url);
		const bool use_default = path.empty();
		const bool active_default = server_avatar && avatar.model_url.empty();
		if(use_default)
		{
			path = base + "/data/resources/xbot_glb_3242545562312850498.bmesh";
			model_status = active_default ? AnimationPreviewWidget::tr("Текущий аватар: стандартный Xbot.") :
				AnimationPreviewWidget::tr("Модель текущего аватара ещё не загружена. Показан стандартный Xbot.");
		}
		else model_status = AnimationPreviewWidget::tr("Предпросмотр вашего текущего аватара.");

		ModelLoading::MakeGLObjectResults result;
		ModelLoading::makeGLObjectForModelFile(*renderer->opengl_engine, *renderer->opengl_engine->vert_buf_allocator,
			nullptr, path, true, result);
		object = result.gl_ob;
		if(object.isNull() || object->mesh_data.isNull() || object->mesh_data->animation_data.joint_nodes.empty())
			throw glare::Exception("Avatar model has no skinned skeleton.");
		model_transform = use_default ? Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()) : avatar.pre_ob_to_world_matrix;
		// Server transforms are eye-relative; the preview is grounded independently.
		model_transform.setColumn(3, Vec4f(0, 0, 0, 1));
		rest = new AnimationData();
		copyRestSkeleton(object->mesh_data->animation_data, *rest);
		object->mesh_data->animation_data = *rest;
		object->mesh_data->animation_data.retarget_adjustments_set = false;

		incomplete_textures = false;
		for(size_t i = 0; i < object->materials.size(); ++i)
		{
			OpenGLMaterial& gl_mat = object->materials[i];
			const size_t material_i = i < avatar.materials.size() ? i : 0;
			if((!use_default || active_default) && !avatar.materials.empty() && avatar.materials[material_i].nonNull())
			{
				WorldMaterialRef material = avatar.materials[material_i]->clone(); // Preserve server state and refcounts.
				auto resolve = [this](URLString& url)
				{
					if(url.empty()) return;
					const std::string path = localPath(url);
					if(path.empty()) incomplete_textures = true;
					url = toURLString(path);
				};
				resolve(material->colour_texture_url);
				resolve(material->roughness.texture_url);
				resolve(material->emission_texture_url);
				resolve(material->normal_map_url);
				ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(*material, gl_mat);
			}
			else if(use_default)
			{
				WorldMaterial material;
				material.colour_rgb = i == 0 ? Avatar::defaultMat0Col() : Avatar::defaultMat1Col();
				material.metallic_fraction.val = i == 0 ? Avatar::default_mat0_metallic_frac : Avatar::default_mat1_metallic_frac;
				if(i == 0) material.roughness.val = Avatar::default_mat0_roughness;
				ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(material, gl_mat);
			}
			loadTextures(gl_mat);
		}
		if(incomplete_textures)
			model_status += AnimationPreviewWidget::tr(" Часть текстур ещё не загружена; показаны цвета материалов.");
	}

	void loadAnimation()
	{
		pending = false;
		if(object.isNull()) loadModel();
		OpenGLEngine& engine = *renderer->opengl_engine;
		if(inserted) { engine.removeObject(object); inserted = false; }
		AnimationData& data = object->mesh_data->animation_data;
		data = *rest;
		data.retarget_adjustments_set = false; // AnimationData::operator= does not copy this flag.
		const std::string path = localPath(selected);
		if(path.empty()) throw glare::Exception("Selected animation is not available locally. Download it, then select it again.");
		if(!hasExtension(path, "subanim")) throw glare::Exception("Preview requires a .subanim resource.");

		// Do not use the shared AnimationManager: browsing must not grow its clip cache.
		FileInStream file(path);
		char magic[4];
		file.readData(magic, sizeof(magic));
		if(std::memcmp(magic, "SUBA", 4) != 0) throw glare::Exception("Invalid animation file header.");
		AnimationData clip;
		clip.readFromStream(file);
		if(clip.animations.size() != 1) throw glare::Exception("Animation must contain exactly one clip.");
		const double length = clip.animations[0]->anim_len;
		if(!std::isfinite(length) || length <= 0.0) throw glare::Exception("Animation has no finite positive duration.");
		FileInStream driver_file(base + "/data/resources/animations/Idle.subanim");
		driver_file.readData(magic, sizeof(magic));
		if(std::memcmp(magic, "SUBA", 4) != 0) throw glare::Exception("Invalid installed Idle animation.");
		AnimationData driver;
		driver.readFromStream(driver_file);
		const auto normalised = normaliseGestureAnimation(clip, driver);
		AnimationData& use_clip = normalised.nonNull() ? *normalised : clip;
		use_clip.prepareForMultipleUse();
		driver.prepareForMultipleUse();
		// Match world playback: retarget the avatar to Idle, then append the clip.
		data.loadAndRetargetAnim(driver);
		data.appendAnimationData(use_clip);
		data.checkDataIsValid();
		data.checkPerAnimNodeDataIsValid();
		if(data.animations.size() != 2) throw glare::Exception("Expected Idle driver and selected preview clip.");

		const auto grounding = AvatarGrounding::computeGroundingInfo(data, true, AvatarGrounding::kRetargetedToeBottomOffsetM);
		float foot_height = grounding.foot_bottom_height;
		const int toe = data.getNodeIndex("LeftToe_End");
		if(toe >= 0)
			foot_height = (model_transform * data.getNodePositionModelSpace(toe, true))[2] - AvatarGrounding::kRetargetedToeBottomOffsetM;
		object->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -foot_height) * model_transform;
		object->current_anim_i = 1;
		object->next_anim_i = -1;
		object->use_time_offset = 0;
		engine.addObject(object);
		inserted = true;
		duration = length;
		clock.restart(); // Loading time must not advance the timeline.
	}

	void tick()
	{
		if(shutdown || !owner->isVisible() || !renderer) return;
		bool loaded = false, advanced = false, stopped = false;
		QString error;
		{
			RestoreContext context{renderer, restore};
			try
			{
				renderer->makeCurrent();
				// First update lets QGLWidget initialise its renderer lazily.
				if(!renderer->opengl_engine->initSucceeded()) renderer->updateGL();
				if(renderer->failed_render || !renderer->opengl_engine->initSucceeded())
					throw glare::Exception("OpenGL preview initialisation failed.");
				loaded = pending;
				if(pending) loadAnimation();
				const double elapsed = clock.isValid() ? clock.nsecsElapsed() * 1.0e-9 : 0.0;
				clock.restart();
				if(playing && duration > 0.0)
				{
					advanced = true;
					position += elapsed * speed / duration;
					if(position >= 1.0)
					{
						if(loop) position = std::fmod(position, 1.0);
						else { position = 1.0; playing = false; stopped = true; }
					}
				}
				// The engine loops exact anim_len to zero. Keep a seek-to-end on the last pose.
				const float end = std::nextafter(static_cast<float>(duration), 0.0f);
				renderer->setPreviewTimeOverride(std::min(position * duration, static_cast<double>(end)));
				renderer->updateGL();
			}
			catch(...)
			{
				error = currentError();
				// Never leave the previously selected clip visible after a failed selection.
				if(inserted && object.nonNull()) { renderer->opengl_engine->removeObject(object); inserted = false; }
				duration = 0.0;
			}
		}
		// Notify editor controls only after restoring the main context.
		if(!error.isEmpty())
		{
			fail(error);
			emit owner->durationChanged(0.0);
			return;
		}
		if(loaded) { emit owner->durationChanged(duration); status(model_status); }
		if(loaded || advanced) emit owner->positionChanged(position);
		if(stopped) emit owner->playbackChanged(false);
	}

	AnimationPreviewWidget* owner;
	std::string base;
	QSettings* settings;
	GUIClient* client;
	std::function<void()> restore;
	QVBoxLayout* layout;
	QLabel* label;
	AnimationPreviewRenderer* renderer = nullptr;
	Reference<TextureServer> textures;
	GLObjectRef object;
	Reference<AnimationData> rest;
	Matrix4f model_transform;
	QString model_status;
	URLString selected;
	QTimer tick_timer;
	QElapsedTimer clock;
	double position = 0.0, duration = 0.0, speed = 1.0;
	bool playing = false, loop = true, pending = true, inserted = false, shutdown = false, incomplete_textures = false, failed_preview = false;
};

AnimationPreviewWidget::AnimationPreviewWidget(const std::string& base_dir_path, QSettings* settings, GUIClient* gui_client,
	std::function<void()> restoreMainContext, QWidget* parent)
: QWidget(parent), impl(new Impl(this, base_dir_path, settings, gui_client, std::move(restoreMainContext)))
{}

AnimationPreviewWidget::~AnimationPreviewWidget() { shutdownGL(); }

void AnimationPreviewWidget::setAnimation(URLString url)
{
	if(impl->shutdown) return;
	impl->selected = std::move(url);
	impl->position = impl->duration = 0.0;
	impl->pending = true;
	impl->clock.restart();
	emit durationChanged(0.0);
	emit positionChanged(0.0);
	if(impl->selected.empty())
	{
		impl->playback(false);
		impl->release();
		impl->status(tr("Выберите анимацию для предпросмотра."));
	}
	else if(isVisible())
	{
		if(impl->renderer && impl->renderer->failed_render) impl->release();
		impl->open();
		if(impl->renderer && !impl->renderer->failed_render) impl->tick_timer.start();
	}
}

void AnimationPreviewWidget::setPlaying(bool playing)
{
	if(impl->shutdown) return;
	if(playing && (impl->selected.empty() || impl->failed_preview)) return;
	if(playing && impl->position >= 1.0) seek(0.0);
	impl->playback(playing);
}

void AnimationPreviewWidget::seek(double normalized)
{
	if(impl->shutdown || !std::isfinite(normalized)) return;
	impl->position = std::max(0.0, std::min(1.0, normalized));
	impl->clock.restart();
	emit positionChanged(impl->position);
}

void AnimationPreviewWidget::step(int frames)
{
	if(impl->shutdown) return;
	impl->playback(false);
	if(impl->duration > 0.0) seek(impl->position + frames / (30.0 * impl->duration));
}

void AnimationPreviewWidget::setSpeed(double speed)
{
	if(impl->shutdown || !std::isfinite(speed) || speed <= 0.0) return;
	impl->speed = std::max(0.05, std::min(8.0, speed));
	impl->clock.restart();
}

void AnimationPreviewWidget::setLoop(bool loop) { if(!impl->shutdown) impl->loop = loop; }

void AnimationPreviewWidget::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	impl->open();
}

void AnimationPreviewWidget::hideEvent(QHideEvent* event)
{
	impl->release(); // No hidden timer, renderer, or model/texture cache.
	QWidget::hideEvent(event);
}

void AnimationPreviewWidget::shutdownGL()
{
	if(impl->shutdown) return;
	impl->shutdown = true;
	impl->playback(false);
	impl->release();
	impl->restore = {};
	impl->client = nullptr;
}
