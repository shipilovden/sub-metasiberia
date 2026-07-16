/*=====================================================================
GearInventoryPanel.cpp
----------------------
Copyright Metasiberia 2026
=====================================================================*/
#include "GearInventoryPanel.h"


#include "AvatarGearPreviewWidget.h"
#include "GUIClient.h"
#include "LucideIconUtils.h"
#include "AvatarGraphics.h"
#include "../shared/ResourceManager.h"
#include "../opengl/FrameBuffer.h"
#include "../opengl/IncludeOpenGL.h"
#include "../opengl/OpenGLEngine.h"
#include "../opengl/RenderBuffer.h"
#include "../utils/FileUtils.h"
#include <QtCore/QSignalBlocker>
#include <QtCore/QElapsedTimer>
#include <QtGui/QGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <array>
#include <cmath>
#include <utility>
#include <vector>


#if 0
// Retained temporarily for source-history readability.  This was the old
// shared-world FBO implementation that caused the preview/world renderer
// coupling.  The active preview below is AvatarGearPreviewWidget, which owns
// the same independent renderer used by Avatar Settings.
namespace
{
static void removePreviewObject(OpenGLEngine& engine, GLObjectRef& ob)
{
	if(ob)
	{
		engine.removeObject(ob);
		ob = nullptr;
	}
}


static QString conciseURL(const URLString& url)
{
	const QString text = QString::fromStdString(toStdString(url));
	const int slash = qMax(text.lastIndexOf('/'), text.lastIndexOf('\\'));
	return slash >= 0 ? text.mid(slash + 1) : text;
}


static void restoreGLCapability(GLenum capability, GLboolean enabled)
{
	if(enabled)
		glEnable(capability);
	else
		glDisable(capability);
}


static bool containsPreviewGeometry(const float depth)
{
	return depth > 0.000001f && depth < 0.999999f;
}


// Lift only pixels that contain preview geometry.  The depth test works for
// both normal and reverse-Z buffers because clear pixels are at an endpoint
// (zero or one), while rendered geometry lies strictly between them.  Keeping
// this as an image-only operation avoids touching shared world lighting or the
// avatar's live materials.
static void applyPreviewExposureLift(QImage& image, const std::vector<float>& depth_pixels)
{
	if(image.format() != QImage::Format_RGBA8888 || depth_pixels.size() != (size_t)image.width() * image.height())
		return;

	static const std::array<unsigned char, 256> lift_lut = []() {
		std::array<unsigned char, 256> lut;
		for(size_t i=0; i<lut.size(); ++i)
		{
			const float source = (float)i / 255.f;
			const float lifted = myClamp(std::pow(source, 0.58f) * 1.08f + 0.018f, 0.f, 1.f);
			lut[i] = (unsigned char)(lifted * 255.f + 0.5f);
		}
		return lut;
	}();

	for(int y=0; y<image.height(); ++y)
	{
		unsigned char* const row = image.scanLine(y);
		for(int x=0; x<image.width(); ++x)
		{
			const float depth = depth_pixels[(size_t)y * image.width() + x];
			if(containsPreviewGeometry(depth))
			{
				unsigned char* const pixel = row + x * 4;
				pixel[0] = lift_lut[pixel[0]];
				pixel[1] = lift_lut[pixel[1]];
				pixel[2] = lift_lut[pixel[2]];
			}
		}
	}
}


// Avatar Settings uses its own OpenGL engine and can safely render a sky and
// textured ground.  The inventory preview intentionally shares the main GL
// context, so touching the engine's global sky/sun state would also alter the
// visible world.  Compose the equivalent daylight backdrop after readback,
// using depth to keep every avatar/gear pixel intact.
static QImage makeAvatarSettingsBackdrop(const int width, const int height)
{
	QImage backdrop(width, height, QImage::Format_RGBA8888);
	backdrop.fill(QColor(198, 214, 222));

	QPainter painter(&backdrop);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const int horizon_y = myClamp((int)(height * 0.40f), 1, myMax(1, height - 1));

	QLinearGradient sky_gradient(0, 0, 0, horizon_y);
	sky_gradient.setColorAt(0.0, QColor(104, 169, 211));
	sky_gradient.setColorAt(0.72, QColor(158, 198, 220));
	sky_gradient.setColorAt(1.0, QColor(206, 218, 223));
	painter.fillRect(QRect(0, 0, width, horizon_y + 1), sky_gradient);

	QLinearGradient ground_gradient(0, horizon_y, 0, height);
	ground_gradient.setColorAt(0.0, QColor(218, 219, 217));
	ground_gradient.setColorAt(1.0, QColor(174, 178, 180));
	painter.fillRect(QRect(0, horizon_y, width, height - horizon_y), ground_gradient);

	// A few subdued cloud bands keep the preview visually consistent with the
	// existing Avatar Settings sky without requiring an environment-map draw.
	const auto draw_cloud = [&painter](const qreal centre_x, const qreal centre_y, const qreal scale)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(255, 255, 255, 50));
		painter.drawEllipse(QRectF(centre_x - 34 * scale, centre_y - 7 * scale, 68 * scale, 14 * scale));
		painter.drawEllipse(QRectF(centre_x - 12 * scale, centre_y - 12 * scale, 38 * scale, 18 * scale));
	};
	draw_cloud(width * 0.18, horizon_y * 0.43, myMax(0.55, width / 620.0));
	draw_cloud(width * 0.73, horizon_y * 0.25, myMax(0.45, width / 760.0));
	draw_cloud(width * 0.86, horizon_y * 0.67, myMax(0.38, width / 900.0));

	const QPointF vanishing_point(width * 0.5, horizon_y);
	const int vertical_step = myMax(28, width / 12);
	painter.setPen(QPen(QColor(72, 76, 78, 105), 1.0));
	for(int bottom_x = -vertical_step; bottom_x <= width + vertical_step; bottom_x += vertical_step)
		painter.drawLine(vanishing_point, QPointF(bottom_x, height));

	const int horizontal_line_count = 18;
	for(int i=1; i<=horizontal_line_count; ++i)
	{
		const qreal t = (qreal)i / horizontal_line_count;
		const qreal y = horizon_y + (height - horizon_y) * t * t;
		const bool major = (i % 4) == 0;
		painter.setPen(QPen(QColor(65, 69, 72, major ? 125 : 80), major ? 1.25 : 0.8));
		painter.drawLine(QPointF(0, y), QPointF(width, y));
	}
	painter.setPen(QPen(QColor(84, 89, 92, 110), 1.0));
	painter.drawLine(QPointF(0, horizon_y), QPointF(width, horizon_y));

	// Ground the avatar visually, as the preview scene deliberately has no
	// shadow-map pass.
	QRadialGradient contact_shadow(QPointF(width * 0.5, height * 0.89), width * 0.15);
	contact_shadow.setColorAt(0.0, QColor(25, 30, 34, 75));
	contact_shadow.setColorAt(1.0, QColor(25, 30, 34, 0));
	painter.setPen(Qt::NoPen);
	painter.setBrush(contact_shadow);
	painter.drawEllipse(QRectF(width * 0.34, height * 0.855, width * 0.32, height * 0.075));

	return backdrop;
}


static void applyAvatarSettingsBackdrop(QImage& display_image, const std::vector<float>& unmirrored_depth_pixels)
{
	if(display_image.format() != QImage::Format_RGBA8888 ||
		unmirrored_depth_pixels.size() != (size_t)display_image.width() * display_image.height())
		return;

	const QImage backdrop = makeAvatarSettingsBackdrop(display_image.width(), display_image.height());
	for(int y=0; y<display_image.height(); ++y)
	{
		unsigned char* const destination_row = display_image.scanLine(y);
		const unsigned char* const backdrop_row = backdrop.constScanLine(y);
		const int depth_y = display_image.height() - 1 - y; // GL readback is bottom-up.
		for(int x=0; x<display_image.width(); ++x)
		{
			const float depth = unmirrored_depth_pixels[(size_t)depth_y * display_image.width() + x];
			if(!containsPreviewGeometry(depth))
			{
				unsigned char* const destination = destination_row + x * 4;
				const unsigned char* const source = backdrop_row + x * 4;
				destination[0] = source[0];
				destination[1] = source[1];
				destination[2] = source[2];
				destination[3] = source[3];
			}
		}
	}
}


// OpenGLEngine keeps part of its render state outside OpenGL itself.  Preview
// rendering shares the main widget's context, so both sets of state must be
// restored even if preview setup or drawing throws.
class PreviewRenderStateGuard
{
public:
	explicit PreviewRenderStateGuard(OpenGLEngine& engine_)
	: engine(engine_), old_scene(engine_.getCurrentScene()), old_target_fbo(engine_.getTargetFrameBuffer()),
	old_engine_viewport(engine_.getViewportDims()), old_main_w(engine_.getMainViewPortWidth()), old_main_h(engine_.getMainViewPortHeight())
	{
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_fbo);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fbo);
		glGetIntegerv(GL_VIEWPORT, old_gl_viewport);
		glGetIntegerv(GL_PACK_ALIGNMENT, &old_pack_alignment);
		glGetIntegerv(GL_PACK_ROW_LENGTH, &old_pack_row_length);
		glGetIntegerv(GL_PACK_SKIP_ROWS, &old_pack_skip_rows);
		glGetIntegerv(GL_PACK_SKIP_PIXELS, &old_pack_skip_pixels);
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pixel_pack_buffer);
		old_framebuffer_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
		old_depth_test = glIsEnabled(GL_DEPTH_TEST);
		old_blend = glIsEnabled(GL_BLEND);
		old_cull_face = glIsEnabled(GL_CULL_FACE);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &old_depth_write_mask);
		glGetBooleanv(GL_COLOR_WRITEMASK, old_colour_write_mask);
		glGetIntegerv(GL_DEPTH_FUNC, &old_depth_func);
		glGetIntegerv(GL_CULL_FACE_MODE, &old_cull_face_mode);
		glGetIntegerv(GL_FRONT_FACE, &old_front_face);
		glGetIntegerv(GL_BLEND_SRC_RGB, &old_blend_src_rgb);
		glGetIntegerv(GL_BLEND_DST_RGB, &old_blend_dst_rgb);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &old_blend_src_alpha);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &old_blend_dst_alpha);
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &old_blend_equation_rgb);
		glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &old_blend_equation_alpha);
	}

	~PreviewRenderStateGuard()
	{
		engine.setCurrentScene(old_scene);
		engine.setTargetFrameBuffer(old_target_fbo);
		engine.setViewportDims(old_engine_viewport.x, old_engine_viewport.y);
		engine.setMainViewportDims(old_main_w, old_main_h);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_fbo);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_fbo);
		glViewport(old_gl_viewport[0], old_gl_viewport[1], old_gl_viewport[2], old_gl_viewport[3]);
		glPixelStorei(GL_PACK_ALIGNMENT, old_pack_alignment);
		glPixelStorei(GL_PACK_ROW_LENGTH, old_pack_row_length);
		glPixelStorei(GL_PACK_SKIP_ROWS, old_pack_skip_rows);
		glPixelStorei(GL_PACK_SKIP_PIXELS, old_pack_skip_pixels);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old_pixel_pack_buffer);
		restoreGLCapability(GL_FRAMEBUFFER_SRGB, old_framebuffer_srgb);
		restoreGLCapability(GL_DEPTH_TEST, old_depth_test);
		restoreGLCapability(GL_BLEND, old_blend);
		restoreGLCapability(GL_CULL_FACE, old_cull_face);
		glDepthMask(old_depth_write_mask);
		glColorMask(old_colour_write_mask[0], old_colour_write_mask[1], old_colour_write_mask[2], old_colour_write_mask[3]);
		glDepthFunc((GLenum)old_depth_func);
		glCullFace((GLenum)old_cull_face_mode);
		glFrontFace((GLenum)old_front_face);
		glBlendFuncSeparate((GLenum)old_blend_src_rgb, (GLenum)old_blend_dst_rgb, (GLenum)old_blend_src_alpha, (GLenum)old_blend_dst_alpha);
		glBlendEquationSeparate((GLenum)old_blend_equation_rgb, (GLenum)old_blend_equation_alpha);
	}

private:
	OpenGLEngine& engine;
	OpenGLSceneRef old_scene;
	FrameBufferRef old_target_fbo;
	Vec2i old_engine_viewport;
	int old_main_w;
	int old_main_h;
	GLint old_draw_fbo;
	GLint old_read_fbo;
	GLint old_gl_viewport[4];
	GLint old_pack_alignment;
	GLint old_pack_row_length;
	GLint old_pack_skip_rows;
	GLint old_pack_skip_pixels;
	GLint old_pixel_pack_buffer;
	GLboolean old_framebuffer_srgb;
	GLboolean old_depth_test;
	GLboolean old_blend;
	GLboolean old_cull_face;
	GLboolean old_depth_write_mask;
	GLboolean old_colour_write_mask[4];
	GLint old_depth_func;
	GLint old_cull_face_mode;
	GLint old_front_face;
	GLint old_blend_src_rgb;
	GLint old_blend_dst_rgb;
	GLint old_blend_src_alpha;
	GLint old_blend_dst_alpha;
	GLint old_blend_equation_rgb;
	GLint old_blend_equation_alpha;
};
}


class GearPreviewCanvas final : public QWidget
{
public:
	explicit GearPreviewCanvas(GearInventoryPanel* owner_)
	:	QWidget(owner_), owner(owner_), scene_dirty(true), transform_dirty(false), frame_dirty(true), waiting_for_avatar(false), resize_pending(false),
		fbo_w(0), fbo_h(0), cam_phi(0.f), cam_theta(1.4f), cam_dist(2.5f), dragging(false)
	{
		setMinimumHeight(300);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);
		render_timer.start();
		avatar_retry_timer.start();
		resize_settle_timer.start();
	}

	void markSceneDirty()
	{
		scene_dirty = true;
		frame_dirty = true;
		waiting_for_avatar = false; // New model/topology data should be tried immediately.
	}

	void markTransformDirty()
	{
		transform_dirty = true;
		frame_dirty = true;
	}

	void requestFrame() { frame_dirty = true; }

	void shutdown()
	{
		if(!owner->gui_client || owner->gui_client->opengl_engine.isNull() || preview_scene.isNull())
			return;

		OpenGLEngine& engine = *owner->gui_client->opengl_engine;
		const OpenGLSceneRef old_scene = engine.getCurrentScene();
		engine.setCurrentScene(preview_scene);
		removePreviewObject(engine, avatar_ob);
		for(size_t i=0; i<gear_graphics.size(); ++i)
			removePreviewObject(engine, gear_graphics[i].gear_gl_ob);
		gear_graphics.clear();
		engine.setCurrentScene(old_scene);
		engine.removeScene(preview_scene);
		preview_scene = nullptr;
		preview_fbo = nullptr;
		preview_colour = nullptr;
		preview_depth = nullptr;
		depth_pixels.clear();
		last_image = QImage();
	}

	void renderPreview()
	{
		if(!isVisible() || !owner->gui_client || owner->gui_client->opengl_engine.isNull())
			return;
		if(!frame_dirty && !scene_dirty && !transform_dirty)
			return;
		// Coalesce high-frequency mouse events.  Unlike the old path this does not
		// render continuously when the preview is unchanged.
		if(render_timer.elapsed() < 33)
			return;
		// Keep the previous image visible while the dock is being resized, then
		// allocate one bounded FBO after the geometry settles.
		if(resize_pending && preview_fbo && resize_settle_timer.elapsed() < 120)
			return;
		resize_pending = false;
		if(scene_dirty && waiting_for_avatar && avatar_retry_timer.elapsed() < 250)
			return;

		OpenGLEngine& engine = *owner->gui_client->opengl_engine;

		try
		{
			PreviewRenderStateGuard restore_state(engine);
			ensureScene(engine);

			if(scene_dirty)
			{
				if(!syncAvatar(engine))
				{
					waiting_for_avatar = true;
					avatar_retry_timer.restart();
					return; // Preserve the last good image while reconnecting/reloading.
				}
				scene_dirty = false;
				transform_dirty = false;
				waiting_for_avatar = false;
			}
			else if(transform_dirty)
			{
				syncSelectedItemTransform();
				transform_dirty = false;
			}

			ensureFBO(engine);
			if(preview_fbo.isNull())
				return;

			engine.setCurrentScene(preview_scene);
			engine.setTargetFrameBufferAndViewport(preview_fbo);
			preview_fbo->bindForDrawing();
			const GLenum preview_draw_buffer = GL_COLOR_ATTACHMENT0;
			glDrawBuffers(1, &preview_draw_buffer);
			engine.setNearDrawDistance(0.05f);
			engine.setMaxDrawDistance(30.f);

			const Matrix4f T = Matrix4f::translationMatrix(0.f, cam_dist, 0.f);
			const Matrix4f z_rot = Matrix4f::rotationAroundZAxis(cam_phi);
			const Matrix4f x_rot = Matrix4f::rotationAroundXAxis(-(cam_theta - Maths::pi_2<float>()));
			const Matrix4f world_to_cam = T * x_rot * z_rot * Matrix4f::translationMatrix(-Vec4f(0.f, 0.f, 0.95f, 1.f));
			engine.setPerspectiveCameraTransform(world_to_cam, 0.035f, 0.05f, (float)fbo_w / (float)fbo_h, 0.f, 0.f);

			if(avatar_ob)
			{
				for(size_t i=0; i<gear_graphics.size(); ++i)
				{
					EquippedGearGraphics& gear = gear_graphics[i];
					if(gear.gear_gl_ob && gear.bone_node_i >= 0 && gear.bone_node_i < (int)avatar_ob->anim_node_data.size())
					{
						gear.gear_gl_ob->ob_to_world_matrix =
							(avatar_ob->ob_to_world_matrix * avatar_ob->anim_node_data[gear.bone_node_i].node_hierarchical_to_object) * gear.transform;
						engine.updateObjectTransformData(*gear.gear_gl_ob);
					}
				}
			}

			engine.draw();
			preview_fbo->bindForReading();
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			QImage image(fbo_w, fbo_h, QImage::Format_RGBA8888);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
			glPixelStorei(GL_PACK_SKIP_ROWS, 0);
			glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); // image.bits() is a CPU pointer, never a PBO offset.
			glReadPixels(0, 0, fbo_w, fbo_h, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
			depth_pixels.resize((size_t)fbo_w * fbo_h);
			glReadPixels(0, 0, fbo_w, fbo_h, GL_DEPTH_COMPONENT, GL_FLOAT, depth_pixels.data());
			applyPreviewExposureLift(image, depth_pixels);
			last_image = image.mirrored();
			applyAvatarSettingsBackdrop(last_image, depth_pixels);
			frame_dirty = false;
			render_timer.restart();
			update();
		}
		catch(...)
		{
			// State is restored by PreviewRenderStateGuard.  Keep the last valid
			// image and wait for an explicit content/camera change before retrying.
			frame_dirty = false;
			transform_dirty = false;
			scene_dirty = false;
		}
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.fillRect(rect(), palette().color(QPalette::AlternateBase));
		if(!last_image.isNull())
			p.drawImage(rect(), last_image);
		else
		{
			p.setPen(palette().color(QPalette::Text));
			p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
				tr("Загрузка аватара и предметов…\nПревью использует отдельную сцену и не показывает игровой мир."));
		}

		p.setPen(QPen(QColor(0, 0, 0, 110), 7, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(18, height() - 22, 58, height() - 22);
		p.setPen(QPen(QColor(231, 76, 60), 3, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(18, height() - 22, 58, height() - 22);
		p.setPen(QPen(QColor(46, 204, 113), 3, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(18, height() - 22, 18, height() - 62);
		p.setPen(QPen(QColor(52, 152, 219), 3, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(18, height() - 22, 42, height() - 48);

		const QString hint = owner->transform_mode == GearInventoryPanel::Mode_Orbit ? tr("ЛКМ: вращать • Колесо: масштаб") :
			(owner->selected_item.nonNull() ? tr("Перетащите мышью — изменения сохранятся при отпускании") : tr("Выберите экипированный предмет"));
		const QRect hint_rect(70, height() - 42, qMax(0, width() - 82), 28);
		if(hint_rect.width() > 0)
		{
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0, 0, 0, 115));
			p.drawRoundedRect(hint_rect.adjusted(-6, 2, 4, -2), 5, 5);
			p.setPen(QColor(255, 255, 255, 235));
			p.drawText(hint_rect, Qt::AlignVCenter | Qt::AlignRight, hint);
		}
	}

	void mousePressEvent(QMouseEvent* event) override
	{
		if(event->button() != Qt::LeftButton)
			return;
		dragging = true;
		drag_origin = event->pos();
		if(owner->selected_item)
		{
			owner->drag_start_translation = owner->selected_item->translation;
			owner->drag_start_scale = owner->selected_item->scale;
			owner->drag_start_axis = owner->selected_item->axis;
			owner->drag_start_angle = owner->selected_item->angle;
		}
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		if(!dragging || !(event->buttons() & Qt::LeftButton))
			return;
		const QPoint delta = event->pos() - drag_origin;
		if(owner->transform_mode == GearInventoryPanel::Mode_Orbit)
		{
			cam_phi += delta.x() * 0.006f;
			cam_theta = myClamp(cam_theta - delta.y() * 0.004f, 0.45f, 2.65f);
			drag_origin = event->pos();
			requestFrame();
		}
		else if(owner->selectedItemIsEquipped())
			owner->applyPreviewDrag(delta, false);
	}

	void mouseReleaseEvent(QMouseEvent* event) override
	{
		if(event->button() == Qt::LeftButton && dragging)
		{
			if(owner->transform_mode != GearInventoryPanel::Mode_Orbit && owner->selectedItemIsEquipped())
				owner->applyPreviewDrag(event->pos() - drag_origin, true);
			dragging = false;
		}
	}

	void wheelEvent(QWheelEvent* event) override
	{
		cam_dist = myClamp(cam_dist * std::exp(-(float)event->angleDelta().y() * 0.0012f), 0.65f, 8.f);
		requestFrame();
		event->accept();
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		resize_pending = true;
		resize_settle_timer.restart();
		requestFrame();
	}

private:
	void ensureScene(OpenGLEngine& engine)
	{
		if(preview_scene)
			return;
		preview_scene = new OpenGLScene(engine);
		preview_scene->draw_water = false;
		preview_scene->water_level_z = -10000.0;
		preview_scene->background_colour = Colour3f(0.105f, 0.12f, 0.15f);
		preview_scene->collect_stats = false;
		preview_scene->cloud_shadows = false;
		preview_scene->shadow_mapping = false;
		preview_scene->draw_overlay_objects = false;
		preview_scene->render_to_main_render_framebuffer = false;
		preview_scene->exposure_factor = 1.f;
		// Environment loading and sun state are global to OpenGLEngine, not per
		// scene.  A null env object gives this preview a solid background and,
		// critically, prevents it from consuming a pending main-world sky reload.
		preview_scene->env_ob = nullptr;
		engine.addScene(preview_scene);
		engine.setCurrentScene(preview_scene);
	}

	void ensureFBO(OpenGLEngine& engine)
	{
		const int physical_w = myMax(64, (int)(width() * devicePixelRatioF()));
		const int physical_h = myMax(64, (int)(height() * devicePixelRatioF()));
		const float downscale = myMin(1.f, 512.f / (float)myMax(physical_w, physical_h));
		const int wanted_w = myClamp((int)(physical_w * downscale), 64, 512);
		const int wanted_h = myClamp((int)(physical_h * downscale), 64, 512);
		if(preview_fbo && wanted_w == fbo_w && wanted_h == fbo_h)
			return;
		fbo_w = wanted_w;
		fbo_h = wanted_h;
		preview_colour = new RenderBuffer(fbo_w, fbo_h, 1, Format_RGBA_Linear_Uint8);
		preview_depth = new RenderBuffer(fbo_w, fbo_h, 1, Format_Depth_Float);
		preview_fbo = new FrameBuffer();
		preview_fbo->attachRenderBuffer(*preview_colour, GL_COLOR_ATTACHMENT0);
		preview_fbo->attachRenderBuffer(*preview_depth, GL_DEPTH_ATTACHMENT);
		if(!preview_fbo->isComplete())
			preview_fbo = nullptr;
	}

	bool syncAvatar(OpenGLEngine& engine)
	{
		if(!owner->gui_client->world_state)
			return false;

		GLObjectRef source_avatar;
		std::vector<EquippedGearGraphics> source_gear_graphics;
		Matrix4f avatar_pre_ob_to_world = Matrix4f::identity();
		{
			// Only snapshot references and small value data while holding the world
			// mutex.  GL object allocation/removal below can be comparatively slow.
			WorldStateLock lock(owner->gui_client->world_state->mutex);
			Avatar* avatar = owner->gui_client->getOurAvatar(lock);
			if(!avatar || avatar->graphics.skinned_gl_ob.isNull())
				return false;
			source_avatar = avatar->graphics.skinned_gl_ob;
			source_gear_graphics = avatar->graphics.equipped_gear_graphics;
			avatar_pre_ob_to_world = avatar->avatar_settings.pre_ob_to_world_matrix;
		}

		engine.setCurrentScene(preview_scene);
		removePreviewObject(engine, avatar_ob);
		for(size_t i=0; i<gear_graphics.size(); ++i)
			removePreviewObject(engine, gear_graphics[i].gear_gl_ob);
		gear_graphics.clear();

		avatar_ob = engine.allocateObject();
		avatar_ob->mesh_data = source_avatar->mesh_data;
		avatar_ob->materials = source_avatar->materials;
		avatar_ob->current_anim_i = source_avatar->current_anim_i;
		avatar_ob->next_anim_i = source_avatar->next_anim_i;
		avatar_ob->transition_start_time = source_avatar->transition_start_time;
		avatar_ob->transition_end_time = source_avatar->transition_end_time;
		avatar_ob->use_time_offset = source_avatar->use_time_offset;
		avatar_ob->anim_node_data = source_avatar->anim_node_data; // Stable first-frame attachment pose.
		avatar_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0.f, 0.f, 1.67f) * avatar_pre_ob_to_world;
		engine.addObject(avatar_ob);

		for(size_t i=0; i<source_gear_graphics.size(); ++i)
		{
			EquippedGearGraphics preview_gear = source_gear_graphics[i];
			if(preview_gear.gear_gl_ob)
			{
				const GLObjectRef source_gear = preview_gear.gear_gl_ob;
				preview_gear.gear_gl_ob = engine.allocateObject();
				preview_gear.gear_gl_ob->mesh_data = source_gear->mesh_data;
				preview_gear.gear_gl_ob->materials = source_gear->materials;
				preview_gear.gear_gl_ob->ob_to_world_matrix = Matrix4f::identity();
				engine.addObject(preview_gear.gear_gl_ob);
			}
			gear_graphics.push_back(preview_gear);
		}
		syncSelectedItemTransform();
		return true;
	}

	void syncSelectedItemTransform()
	{
		if(!owner->selected_item || avatar_ob.isNull() || avatar_ob->mesh_data.isNull())
			return;

		for(size_t i=0; i<gear_graphics.size(); ++i)
		{
			EquippedGearGraphics& gear = gear_graphics[i];
			if(gear.gear_id == owner->selected_item->id)
			{
				gear.transform = owner->selected_item->gearObToBoneSpaceMatrix();
				gear.bone_name = owner->selected_item->bone_name;
				gear.bone_node_i = avatar_ob->mesh_data->animation_data.getNodeIndex(gear.bone_name);
				break;
			}
		}
	}

	GearInventoryPanel* owner;
	OpenGLSceneRef preview_scene;
	GLObjectRef avatar_ob;
	std::vector<EquippedGearGraphics> gear_graphics;
	FrameBufferRef preview_fbo;
	RenderBufferRef preview_colour;
	RenderBufferRef preview_depth;
	std::vector<float> depth_pixels;
	QImage last_image;
	bool scene_dirty;
	bool transform_dirty;
	bool frame_dirty;
	bool waiting_for_avatar;
	bool resize_pending;
	int fbo_w;
	int fbo_h;
	float cam_phi;
	float cam_theta;
	float cam_dist;
	bool dragging;
	QPoint drag_origin;
	QElapsedTimer render_timer;
	QElapsedTimer avatar_retry_timer;
	QElapsedTimer resize_settle_timer;
};
#endif


namespace
{
static QString conciseURL(const URLString& url)
{
	const QString text = QString::fromStdString(toStdString(url));
	const int slash = qMax(text.lastIndexOf('/'), text.lastIndexOf('\\'));
	return slash >= 0 ? text.mid(slash + 1) : text;
}
}


GearInventoryPanel::GearInventoryPanel(QWidget* parent)
:	QWidget(parent), gui_client(nullptr), transform_mode(Mode_Orbit), transform_axis(Axis_All), drag_start_angle(0.f), updating_editor(false)
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);

	QHBoxLayout* heading = new QHBoxLayout();
	QLabel* title = new QLabel(tr("<b>Инвентарь и экипировка</b>"), this);
	status_label = new QLabel(tr("Не подключено"), this);
	status_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	refresh_button = new QToolButton(this);
	refresh_button->setToolTip(tr("Обновить инвентарь с сервера"));
	heading->addWidget(title, 1);
	heading->addWidget(status_label);
	heading->addWidget(refresh_button);
	root->addLayout(heading);

	QGroupBox* preview_group = new QGroupBox(tr("Превью аватара с экипировкой"), this);
	QVBoxLayout* preview_layout = new QVBoxLayout(preview_group);
	preview = new AvatarGearPreviewWidget(this);
	preview->setMinimumHeight(420);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	preview_layout->addWidget(preview, 1);

	QHBoxLayout* tools = new QHBoxLayout();
	orbit_button = new QToolButton(this); orbit_button->setText(tr("Обзор")); orbit_button->setCheckable(true);
	move_button = new QToolButton(this); move_button->setText(tr("Перемещение")); move_button->setCheckable(true);
	rotate_button = new QToolButton(this); rotate_button->setText(tr("Вращение")); rotate_button->setCheckable(true);
	scale_button = new QToolButton(this); scale_button->setText(tr("Масштаб")); scale_button->setCheckable(true);
	tools->addWidget(orbit_button);
	tools->addWidget(move_button);
	tools->addWidget(rotate_button);
	tools->addWidget(scale_button);
	tools->addStretch(1);
	gizmo_button = new QCheckBox(tr("Гизмо"), this);
	gizmo_button->setChecked(true);
	gizmo_button->setToolTip(tr("Показывать gizmo и использовать его для точной настройки предмета"));
	tools->addWidget(gizmo_button);
	const char* axis_names[] = { "XYZ", "X", "Y", "Z" };
	for(int i=0; i<4; ++i)
	{
		axis_buttons[i] = new QToolButton(this);
		axis_buttons[i]->setText(axis_names[i]);
		axis_buttons[i]->setCheckable(true);
		axis_buttons[i]->setToolTip(tr("Ось преобразования"));
		tools->addWidget(axis_buttons[i]);
	}
	preview_layout->addLayout(tools);
	root->addWidget(preview_group);

	selection_label = new QLabel(tr("Выберите карточку предмета"), this);
	selection_label->setWordWrap(true);
	root->addWidget(selection_label);

	QGroupBox* attachment_group = new QGroupBox(tr("Закрепление и точная настройка"), this);
	QGridLayout* grid = new QGridLayout(attachment_group);
	bone_combo = new QComboBox(this);
	const std::pair<const char*, const char*> bones[] = {
		{"Head", "Голова"}, {"Neck", "Шея"}, {"Spine2", "Грудь"}, {"Hips", "Таз"},
		{"LeftShoulder", "Левое плечо"}, {"RightShoulder", "Правое плечо"}, {"LeftArm", "Левая рука"}, {"RightArm", "Правая рука"},
		{"LeftForeArm", "Левое предплечье"}, {"RightForeArm", "Правое предплечье"}, {"LeftHand", "Левая кисть"}, {"RightHand", "Правая кисть"},
		{"LeftUpLeg", "Левое бедро"}, {"RightUpLeg", "Правое бедро"}, {"LeftLeg", "Левая голень"}, {"RightLeg", "Правая голень"},
		{"LeftFoot", "Левая стопа"}, {"RightFoot", "Правая стопа"}
	};
	for(const auto& bone : bones)
		bone_combo->addItem(QString::fromUtf8(bone.second), QString::fromUtf8(bone.first));
	bone_combo->setToolTip(tr("Кость аватара, к которой прикреплён предмет"));
	grid->addWidget(new QLabel(tr("Кость"), this), 0, 0);
	grid->addWidget(bone_combo, 0, 1, 1, 3);
	name_edit = new QLineEdit(this);
	name_edit->setMaxLength((int)GearItem::MAX_NAME_SIZE - 1);
	name_edit->setPlaceholderText(tr("Название предмета"));
	name_edit->setToolTip(tr("Название предмета в инвентаре"));
	grid->addWidget(new QLabel(tr("Название"), this), 1, 0);
	grid->addWidget(name_edit, 1, 1, 1, 3);

	const QString axes[] = { "X", "Y", "Z" };
	for(int i=0; i<3; ++i)
	{
		translation_spins[i] = new QDoubleSpinBox(this);
		translation_spins[i]->setRange(-100.0, 100.0); translation_spins[i]->setDecimals(4); translation_spins[i]->setSingleStep(0.01); translation_spins[i]->setSuffix(" m");
		translation_spins[i]->setToolTip(tr("Позиция по оси %1. Изменение применяется сразу.").arg(axes[i]));
		rotation_axis_spins[i] = new QDoubleSpinBox(this);
		rotation_axis_spins[i]->setRange(-1.0, 1.0); rotation_axis_spins[i]->setDecimals(4); rotation_axis_spins[i]->setSingleStep(0.05);
		rotation_axis_spins[i]->setToolTip(tr("Ось вращения по компоненте %1.").arg(axes[i]));
		scale_spins[i] = new QDoubleSpinBox(this);
		scale_spins[i]->setRange(0.001, 100.0); scale_spins[i]->setDecimals(4); scale_spins[i]->setSingleStep(0.01);
		scale_spins[i]->setToolTip(tr("Масштаб по оси %1. Изменение применяется сразу.").arg(axes[i]));
		grid->addWidget(new QLabel(axes[i], this), 2, i + 1);
		grid->addWidget(translation_spins[i], 3, i + 1);
		grid->addWidget(rotation_axis_spins[i], 4, i + 1);
		grid->addWidget(scale_spins[i], 6, i + 1);
	}
	grid->addWidget(new QLabel(tr("Позиция"), this), 3, 0);
	grid->addWidget(new QLabel(tr("Ось вращения"), this), 4, 0);
	rotation_angle_spin = new QDoubleSpinBox(this);
	rotation_angle_spin->setRange(-3600.0, 3600.0); rotation_angle_spin->setDecimals(2); rotation_angle_spin->setSingleStep(1.0); rotation_angle_spin->setSuffix("°");
	rotation_angle_spin->setToolTip(tr("Угол вращения. Изменение применяется сразу."));
	grid->addWidget(new QLabel(tr("Угол"), this), 5, 0);
	grid->addWidget(rotation_angle_spin, 5, 1, 1, 3);
	grid->addWidget(new QLabel(tr("Масштаб"), this), 6, 0);
	apply_button = new QPushButton(tr("Сохранить положение"), this);
	delete_button = new QPushButton(tr("Удалить предмет"), this);
	delete_button->setToolTip(tr("Удалить выбранный предмет из инвентаря"));
	grid->addWidget(apply_button, 7, 0, 1, 2);
	grid->addWidget(delete_button, 7, 2, 1, 2);
	root->addWidget(attachment_group);

	tabs = new QTabWidget(this);
	QWidget* equipped_page = new QWidget(tabs);
	equipped_cards_layout = new QVBoxLayout(equipped_page);
	equipped_cards_layout->setContentsMargins(4, 8, 4, 8);
	QWidget* all_page = new QWidget(tabs);
	all_cards_layout = new QVBoxLayout(all_page);
	all_cards_layout->setContentsMargins(4, 8, 4, 8);
	tabs->addTab(equipped_page, tr("Экипировано"));
	tabs->addTab(all_page, tr("Все предметы"));
	root->addWidget(tabs);
	root->addStretch(1);

	connect(refresh_button, &QToolButton::clicked, this, [this]() { if(gui_client) gui_client->requestGearInventory(); });
	connect(orbit_button, &QToolButton::clicked, this, [this]() { setTransformMode(Mode_Orbit); });
	connect(move_button, &QToolButton::clicked, this, [this]() { setTransformMode(Mode_Move); });
	connect(rotate_button, &QToolButton::clicked, this, [this]() { setTransformMode(Mode_Rotate); });
	connect(scale_button, &QToolButton::clicked, this, [this]() { setTransformMode(Mode_Scale); });
	for(int i=0; i<4; ++i)
		connect(axis_buttons[i], &QToolButton::clicked, this, [this, i]() { setTransformAxis((TransformAxis)i); });
	connect(apply_button, &QPushButton::clicked, this, [this]() { updateSelectionFromEditor(true); });
	connect(gizmo_button, &QCheckBox::toggled, this, [this](bool on) { preview->setGizmoVisible(on); });
	connect(name_edit, &QLineEdit::editingFinished, this, [this]() { updateSelectionFromEditor(true); });
	connect(bone_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateSelectionFromEditor(true); });
	connect(rotation_angle_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateSelectionFromEditor(true); });
	for(int i=0; i<3; ++i)
	{
		connect(translation_spins[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateSelectionFromEditor(true); });
		connect(rotation_axis_spins[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateSelectionFromEditor(true); });
		connect(scale_spins[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateSelectionFromEditor(true); });
	}
	connect(delete_button, &QPushButton::clicked, this, [this]() {
		if(gui_client && selected_item && canSynchroniseInventory())
			gui_client->deleteGearItem(selected_item);
	});
	setTransformMode(Mode_Orbit);
	setTransformAxis(Axis_All);
	updateEditorFromSelection();
}


GearInventoryPanel::~GearInventoryPanel()
{
	if(preview)
		preview->shutdown();
}


void GearInventoryPanel::initPreview(const std::string& base_dir_path, QSettings* settings, Reference<ResourceManager> resource_manager,
	AnimationManager* animation_manager, std::function<void()> restore_main_context)
{
	preview->init(base_dir_path, settings, resource_manager, animation_manager, std::move(restore_main_context));
	preview->setTransformDragCallback([this](AvatarGearPreviewWidget::InteractionMode, AvatarGearPreviewWidget::TransformAxis, const QPoint& delta, bool finished) {
		applyPreviewDrag(delta, finished);
	});
	preview->setTransformAxisClickCallback([this](AvatarGearPreviewWidget::TransformAxis axis) {
		setTransformAxis(static_cast<TransformAxis>(axis));
	});
}


void GearInventoryPanel::setClient(GUIClient* client)
{
	gui_client = client;
	refreshFromClient();
}


void GearInventoryPanel::setIconDirectory(const QString& directory)
{
	icon_directory = directory;
	const QColor fg = palette().color(QPalette::ButtonText);
	LucideIconUtils::setButtonIcon(refresh_button, directory, "refresh-cw", fg);
	LucideIconUtils::setButtonIcon(orbit_button, directory, "user-round", fg);
	LucideIconUtils::setButtonIcon(move_button, directory, "move-3d", fg);
	LucideIconUtils::setButtonIcon(rotate_button, directory, "refresh-cw", fg);
	LucideIconUtils::setButtonIcon(scale_button, directory, "move", fg);
	LucideIconUtils::setButtonIcon(apply_button, directory, "save", fg);
	LucideIconUtils::setButtonIcon(delete_button, directory, "trash-2", fg);
	LucideIconUtils::setButtonIcon(gizmo_button, directory, "move-3d", fg);
	const QString hover_style = QStringLiteral("QToolButton:hover, QPushButton:hover, QCheckBox:hover { background: palette(highlight); color: palette(highlighted-text); } QFrame#gearInventoryCard:hover { border: 1px solid palette(highlight); }");
	setStyleSheet(styleSheet() + hover_style);
}


void GearInventoryPanel::refreshFromClient()
{
	if(!gui_client)
		return;
	const bool connected = gui_client->connection_state == GUIClient::ServerConnectionState_Connected;
	const bool server_supports_inventory = connected && gui_client->serverSupportsGearInventory();
	if(!connected)
		status_label->setText(tr("Не подключено"));
	else if(!gui_client->logged_in_user_id.valid())
		status_label->setText(tr("Войдите в аккаунт"));
	else if(!server_supports_inventory)
		status_label->setText(tr("Нужно обновить сервер"));
	else
		status_label->setText(tr("%1 предметов").arg(gui_client->logged_in_all_gear.items.size()));
	refresh_button->setEnabled(server_supports_inventory && gui_client->logged_in_user_id.valid());
	gui_client->requestGearPreviewResources();
	preview->setPreviewData(
		gui_client->logged_in_avatar_settings,
		gui_client->logged_in_equipped_gear,
		gui_client->server_has_basis_textures,
		gui_client->server_has_optimised_meshes,
		gui_client->server_opt_mesh_version
	);
	rebuildCards();
}


void GearInventoryPanel::shutdownPreview()
{
	preview->shutdown();
}


void GearInventoryPanel::clearCardLayout(QVBoxLayout* layout)
{
	while(QLayoutItem* child = layout->takeAt(0))
	{
		delete child->widget();
		delete child;
	}
}


void GearInventoryPanel::rebuildCards()
{
	const UID selected_id = selected_item ? selected_item->id : UID::invalidUID();
	clearCardLayout(equipped_cards_layout);
	clearCardLayout(all_cards_layout);
	GearItemRef restored_selection;

	for(size_t i=0; i<gui_client->logged_in_equipped_gear.items.size(); ++i)
	{
		const GearItemRef& item = gui_client->logged_in_equipped_gear.items[i];
		equipped_cards_layout->addWidget(makeCard(item, true));
		if(item->id == selected_id) restored_selection = item;
	}
	if(gui_client->logged_in_equipped_gear.items.empty())
		equipped_cards_layout->addWidget(new QLabel(tr("Пока ничего не экипировано. Выберите предмет во вкладке «Все предметы»."), this));
	equipped_cards_layout->addStretch(1);

	for(size_t i=0; i<gui_client->logged_in_all_gear.items.size(); ++i)
	{
		const GearItemRef& item = gui_client->logged_in_all_gear.items[i];
		bool equipped = false;
		for(size_t z=0; z<gui_client->logged_in_equipped_gear.items.size(); ++z)
			if(gui_client->logged_in_equipped_gear.items[z]->id == item->id) { equipped = true; break; }
		all_cards_layout->addWidget(makeCard(item, equipped));
		if(item->id == selected_id) restored_selection = item;
	}
	if(gui_client->logged_in_all_gear.items.empty())
	{
		const bool connected = gui_client->connection_state == GUIClient::ServerConnectionState_Connected;
		const QString empty_text = (connected && !gui_client->serverSupportsGearInventory())
			? tr("Этот сервер ещё не поддерживает инвентарь. Клиент не будет отправлять несовместимые команды и не потеряет соединение.")
			: tr("Инвентарь пуст. Выберите объект в мире и нажмите «Преобразовать выбранный объект в предмет снаряжения».");
		QLabel* empty_label = new QLabel(empty_text, this);
		empty_label->setWordWrap(true);
		all_cards_layout->addWidget(empty_label);
	}
	all_cards_layout->addStretch(1);
	selected_item = restored_selection;
	preview->setSelectedGear(selected_item ? selected_item->id : UID::invalidUID());
	updateEditorFromSelection();
}


QWidget* GearInventoryPanel::makeCard(const GearItemRef& item, bool equipped)
{
	QFrame* card = new QFrame(this);
	card->setFrameShape(QFrame::StyledPanel);
	card->setObjectName("gearInventoryCard");
	card->setToolTip(tr("Карточка предмета. Нажмите «Настроить», чтобы выбрать его и увидеть gizmo."));
	QHBoxLayout* row = new QHBoxLayout(card);
	row->setContentsMargins(8, 8, 8, 8);

	QLabel* thumb = new QLabel(card);
	thumb->setFixedSize(72, 72);
	thumb->setAlignment(Qt::AlignCenter);
	thumb->setFrameShape(QFrame::StyledPanel);
	const QString preview_path = previewPathForItem(*item);
	if(!preview_path.isEmpty())
	{
		const QPixmap pix(preview_path);
		if(!pix.isNull()) thumb->setPixmap(pix.scaled(68, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	}
	if(thumb->pixmap() == nullptr || thumb->pixmap()->isNull())
		thumb->setPixmap(LucideIconUtils::tintedIcon(icon_directory, "package", palette().color(QPalette::Text), 42).pixmap(42, 42));
	thumb->setToolTip(item->preview_image_URL.empty() ? tr("Мини-превью пока не загружено") : tr("Мини-превью предмета"));
	row->addWidget(thumb);

	QVBoxLayout* text = new QVBoxLayout();
	QLabel* name = new QLabel(QString("<b>%1</b>").arg(QString::fromStdString(item->name).toHtmlEscaped()), card);
	name->setWordWrap(true);
	QLabel* info = new QLabel(tr("Кость: %1\nМодель: %2\nID: %3").arg(boneDisplayName(QString::fromStdString(item->bone_name)), conciseURL(item->model_url), QString::number(item->id.value())), card);
	info->setWordWrap(true);
	info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	text->addWidget(name);
	text->addWidget(info);
	if(!item->description.empty())
	{
		QLabel* description = new QLabel(QString::fromStdString(item->description), card);
		description->setWordWrap(true);
		text->addWidget(description);
	}
	row->addLayout(text, 1);

	QVBoxLayout* actions = new QVBoxLayout();
	QPushButton* select = new QPushButton(tr("Настроить"), card);
	QPushButton* toggle = new QPushButton(equipped ? tr("Снять") : tr("Надеть"), card);
	toggle->setEnabled(canSynchroniseInventory());
	LucideIconUtils::setButtonIcon(select, icon_directory, "move-3d", palette().color(QPalette::ButtonText));
	LucideIconUtils::setButtonIcon(toggle, icon_directory, equipped ? "package-open" : "backpack", palette().color(QPalette::ButtonText));
	connect(select, &QPushButton::clicked, this, [this, item]() { selectItem(item); });
	connect(toggle, &QPushButton::clicked, this, [this, item, equipped]() {
		if(equipped) gui_client->equippedGearItemClicked(item); else gui_client->gearItemClicked(item);
		selected_item = item;
	});
	actions->addWidget(select);
	actions->addWidget(toggle);
	actions->addStretch(1);
	row->addLayout(actions);
	return card;
}


void GearInventoryPanel::selectItem(const GearItemRef& item)
{
	selected_item = item;
	preview->setSelectedGear(item ? item->id : UID::invalidUID());
	updateEditorFromSelection();
	setTransformMode(selectedItemIsEquipped() && canSynchroniseInventory() ? Mode_Move : Mode_Orbit);
}


void GearInventoryPanel::updateEditorFromSelection()
{
	updating_editor = true;
	const bool have_item = selected_item.nonNull();
	const bool editable = have_item && selectedItemIsEquipped() && canSynchroniseInventory();
	if(have_item)
	{
		const QString edit_hint = editable ? QString() :
			(selectedItemIsEquipped() ? tr(" — синхронизация с сервером недоступна") : tr(" — сначала наденьте предмет"));
		selection_label->setText(tr("<b>%1</b><br>Закрепление: %2%3").arg(QString::fromStdString(selected_item->name).toHtmlEscaped(), boneDisplayName(QString::fromStdString(selected_item->bone_name)), edit_hint));
		int bone_index = bone_combo->findData(QString::fromStdString(selected_item->bone_name));
		if(bone_index < 0)
		{
			bone_combo->addItem(boneDisplayName(QString::fromStdString(selected_item->bone_name)), QString::fromStdString(selected_item->bone_name));
			bone_index = bone_combo->count() - 1;
		}
		bone_combo->setCurrentIndex(bone_index);
		name_edit->setText(QString::fromStdString(selected_item->name));
		for(int i=0; i<3; ++i)
		{
			translation_spins[i]->setValue(selected_item->translation[i]);
			rotation_axis_spins[i]->setValue(selected_item->axis[i]);
			scale_spins[i]->setValue(selected_item->scale[i]);
		}
		rotation_angle_spin->setValue(radToDegree(selected_item->angle));
	}
	else
		selection_label->setText(tr("Выберите карточку предмета"));
	name_edit->setEnabled(have_item && canSynchroniseInventory());
	bone_combo->setEnabled(editable);
	for(int i=0; i<3; ++i) { translation_spins[i]->setEnabled(editable); rotation_axis_spins[i]->setEnabled(editable); scale_spins[i]->setEnabled(editable); }
	rotation_angle_spin->setEnabled(editable);
	apply_button->setEnabled(editable);
	delete_button->setEnabled(have_item && canSynchroniseInventory());
	updating_editor = false;
	updateToolButtonStates();
}


void GearInventoryPanel::updateSelectionFromEditor(bool send_to_server)
{
	if(updating_editor || !selected_item || !canSynchroniseInventory())
		return;
	selected_item->bone_name = boneDataName().toStdString();
	selected_item->name = name_edit->text().trimmed().toStdString();
	if(selectedItemIsEquipped())
	{
		for(int i=0; i<3; ++i)
		{
			selected_item->translation[i] = (float)translation_spins[i]->value();
			selected_item->axis[i] = (float)rotation_axis_spins[i]->value();
			selected_item->scale[i] = (float)scale_spins[i]->value();
		}
		if(selected_item->axis.length2() < 1.0e-8f)
			selected_item->axis = Vec3f(0, 0, 1);
		selected_item->angle = degreeToRad((float)rotation_angle_spin->value());
	}
	if(send_to_server)
		gui_client->gearItemChangedOnOurAvatar(selected_item.ptr());
	preview->update();
}


void GearInventoryPanel::applyPreviewDrag(const QPoint& total_delta, bool finished)
{
	if(!selected_item || !selectedItemIsEquipped() || !canSynchroniseInventory())
		return;
	if(total_delta.isNull() && !finished)
	{
		drag_start_translation = selected_item->translation;
		drag_start_scale = selected_item->scale;
		drag_start_axis = selected_item->axis;
		drag_start_angle = selected_item->angle;
		return;
	}
	const float horizontal = (float)total_delta.x();
	const float vertical = (float)-total_delta.y();
	if(transform_mode == Mode_Move)
	{
		selected_item->translation = drag_start_translation;
		const float amount = (horizontal + vertical) * 0.0025f;
		if(transform_axis == Axis_X) selected_item->translation.x += amount;
		else if(transform_axis == Axis_Y) selected_item->translation.y += amount;
		else if(transform_axis == Axis_Z) selected_item->translation.z += amount;
		else { selected_item->translation.x += horizontal * 0.0025f; selected_item->translation.z += vertical * 0.0025f; }
	}
	else if(transform_mode == Mode_Rotate)
	{
		selected_item->axis = drag_start_axis;
		if(transform_axis == Axis_X) selected_item->axis = Vec3f(1, 0, 0);
		else if(transform_axis == Axis_Y) selected_item->axis = Vec3f(0, 1, 0);
		else if(transform_axis == Axis_Z) selected_item->axis = Vec3f(0, 0, 1);
		selected_item->angle = drag_start_angle + horizontal * 0.01f + vertical * 0.004f;
	}
	else if(transform_mode == Mode_Scale)
	{
		selected_item->scale = drag_start_scale;
		const float factor = std::exp((horizontal + vertical) * 0.004f);
		if(transform_axis == Axis_X) selected_item->scale.x = myMax(0.001f, drag_start_scale.x * factor);
		else if(transform_axis == Axis_Y) selected_item->scale.y = myMax(0.001f, drag_start_scale.y * factor);
		else if(transform_axis == Axis_Z) selected_item->scale.z = myMax(0.001f, drag_start_scale.z * factor);
		else selected_item->scale = drag_start_scale * factor;
	}
	updateEditorFromSelection();
	preview->update();
	if(finished || !total_delta.isNull())
		gui_client->gearItemChangedOnOurAvatar(selected_item.ptr());
}


bool GearInventoryPanel::selectedItemIsEquipped() const
{
	if(!gui_client || !selected_item)
		return false;
	for(size_t i=0; i<gui_client->logged_in_equipped_gear.items.size(); ++i)
		if(gui_client->logged_in_equipped_gear.items[i]->id == selected_item->id)
			return true;
	return false;
}


bool GearInventoryPanel::canSynchroniseInventory() const
{
	return gui_client &&
		gui_client->connection_state == GUIClient::ServerConnectionState_Connected &&
		gui_client->logged_in_user_id.valid() &&
		gui_client->serverSupportsGearInventory();
}


void GearInventoryPanel::setTransformMode(TransformMode mode)
{
	transform_mode = mode;
	preview->setInteractionMode(
		static_cast<AvatarGearPreviewWidget::InteractionMode>(transform_mode),
		static_cast<AvatarGearPreviewWidget::TransformAxis>(transform_axis)
	);
	updateToolButtonStates();
	preview->update();
}


void GearInventoryPanel::setTransformAxis(TransformAxis axis)
{
	transform_axis = axis;
	preview->setInteractionMode(
		static_cast<AvatarGearPreviewWidget::InteractionMode>(transform_mode),
		static_cast<AvatarGearPreviewWidget::TransformAxis>(transform_axis)
	);
	updateToolButtonStates();
}


void GearInventoryPanel::updateToolButtonStates()
{
	orbit_button->setChecked(transform_mode == Mode_Orbit);
	move_button->setChecked(transform_mode == Mode_Move);
	rotate_button->setChecked(transform_mode == Mode_Rotate);
	scale_button->setChecked(transform_mode == Mode_Scale);
	const bool enable_transform = selectedItemIsEquipped() && canSynchroniseInventory();
	move_button->setEnabled(enable_transform);
	rotate_button->setEnabled(enable_transform);
	scale_button->setEnabled(enable_transform);
	for(int i=0; i<4; ++i)
	{
		axis_buttons[i]->setChecked(transform_axis == (TransformAxis)i);
		axis_buttons[i]->setEnabled(enable_transform && transform_mode != Mode_Orbit);
	}
}


QString GearInventoryPanel::previewPathForItem(const GearItem& item) const
{
	if(!gui_client || item.preview_image_URL.empty() || !gui_client->resource_manager->isFileForURLPresent(item.preview_image_URL))
		return QString();
	try
	{
		const std::string path = gui_client->resource_manager->pathForURL(item.preview_image_URL);
		return FileUtils::fileExists(path) ? QString::fromStdString(path) : QString();
	}
	catch(glare::Exception&)
	{
		return QString();
	}
}


QString GearInventoryPanel::boneDisplayName(const QString& bone_name) const
{
	const int index = bone_combo->findData(bone_name);
	return index >= 0 ? bone_combo->itemText(index) : bone_name;
}


QString GearInventoryPanel::boneDataName() const
{
	const QVariant data = bone_combo->currentData();
	return data.isValid() ? data.toString() : bone_combo->currentText().trimmed();
}


void GearInventoryPanel::setHoverHelp(QWidget* widget, const QString& text)
{
	if(widget)
		widget->setToolTip(text);
}
