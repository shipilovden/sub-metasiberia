/*=====================================================================
GlWidget.cpp
------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "GlWidget.h"


#include "CameraController.h"
#include "MainOptionsDialog.h"
#include "PhotoFrameRenderer.h"
#include "PhotoPostProcess.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/ImageMap.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/PlatformUtils.h"
#include "../utils/TaskManager.h"
#include "../qt/QtUtils.h"
#include <QtGui/QMouseEvent>
#include <QtCore/QSettings>
#include <QtWidgets/QShortcut>
#include <QtGamepad/QGamepad>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFramebufferObject>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtCore/QScopedValueRollback>
#include <QtCore/QThread>
#include <QtCore/QPointer>
#if defined(_WIN32)
#include <QtPlatformHeaders/QWGLNativeContext>
#endif
#include <tracy/Tracy.hpp>
#include <set>
#include <stack>
#include <algorithm>
#include <array>
#include <stdexcept>
#include "../opengl/IncludeOpenGL.h"


namespace
{
// Capture may be called outside paintGL, including while another Qt GL view is
// current.  QGLWidget's contextHandle is the QOpenGLContext on Qt 5 as well.
class PhotoCurrentContext
{
public:
	explicit PhotoCurrentContext(GlWidget& widget) : previous(QOpenGLContext::currentContext()), surface(previous ? previous->surface() : nullptr)
	{
		widget.makeCurrent();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		current = widget.context();
#else
		current = widget.context() ? widget.context()->contextHandle() : nullptr;
#endif
		if(!current || current != QOpenGLContext::currentContext())
			throw std::runtime_error("Cannot make the photo OpenGL context current");
	}
	~PhotoCurrentContext()
	{
		if(previous != current)
		{
			if(previous && surface) previous->makeCurrent(surface);
			else current->doneCurrent();
		}
	}
private:
	QOpenGLContext* previous;
	QSurface* surface;
	QOpenGLContext* current = nullptr;
};

// Do not use the engine's program-binding helpers here: they maintain private
// shader caches.  draw() resets those caches on entry; restore actual GL state
// around this independent renderer, including failure/readback paths.
class PhotoGLState
{
public:
	PhotoGLState()
	{
		for(size_t i=0; i<queries.size(); ++i) glGetIntegerv(queries[i], &ints[i]);
		glGetIntegerv(GL_VIEWPORT, viewport);
		glGetIntegerv(GL_SCISSOR_BOX, scissor);
		glGetIntegerv(GL_POLYGON_MODE, polygon);
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_colour);
		glGetFloatv(GL_BLEND_COLOR, blend_colour);
		glGetDoublev(GL_DEPTH_RANGE, depth_range);
		glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
		glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &uniform_buffer);
		for(GLuint i=0; i<uniform_bindings.size(); ++i)
			saveBufferBinding(GL_UNIFORM_BUFFER_BINDING, GL_UNIFORM_BUFFER_START, GL_UNIFORM_BUFFER_SIZE, i, uniform_bindings[i]);
		const QSurfaceFormat format = QOpenGLContext::currentContext()->format();
		storage_buffers = format.majorVersion() > 4 || (format.majorVersion() == 4 && format.minorVersion() >= 3);
		if(storage_buffers)
		{
			glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &storage_buffer);
			glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &indirect_buffer);
			for(GLuint i=0; i<storage_bindings.size(); ++i)
				saveBufferBinding(GL_SHADER_STORAGE_BUFFER_BINDING, GL_SHADER_STORAGE_BUFFER_START, GL_SHADER_STORAGE_BUFFER_SIZE, i, storage_bindings[i]);
		}
		for(size_t i=0; i<caps.size(); ++i) enabled[i] = glIsEnabled(caps[i]);
		GLint count = 0;
		glGetIntegerv(GL_MAX_DRAW_BUFFERS, &count);
		blend.resize(count);
		indexed_blend = QOpenGLContext::currentContext()->format().majorVersion() >= 4 ||
			QOpenGLContext::currentContext()->hasExtension(QByteArrayLiteral("GL_ARB_draw_buffers_blend"));
		for(GLint i=0; i<count; ++i)
		{
			blend[i].enabled = glIsEnabledi(GL_BLEND, i);
			glGetBooleani_v(GL_COLOR_WRITEMASK, i, blend[i].mask);
			for(size_t j=0; j<blend_queries.size(); ++j)
				if(indexed_blend) glGetIntegeri_v(blend_queries[j], i, &blend[i].values[j]);
				else glGetIntegerv(blend_queries[j], &blend[i].values[j]);
		}
		// Engine texture units occupy 0..31. Include all fragment-stage units
		// (and restore the caller's active unit separately).
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &count);
		textures.resize(count);
		for(GLint i=0; i<count; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			for(size_t j=0; j<texture_queries.size(); ++j) glGetIntegerv(texture_queries[j], &textures[i][j]);
		}
		glActiveTexture(ints[6]);
	}
	~PhotoGLState()
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ints[0]);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, ints[1]);
		glBindRenderbuffer(GL_RENDERBUFFER, ints[2]);
		glUseProgram(ints[3]);
		glBindVertexArray(ints[4]);
		glBindBuffer(GL_ARRAY_BUFFER, ints[5]);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, ints[7]);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ints[8]);
		for(GLuint i=0; i<uniform_bindings.size(); ++i) restoreBufferBinding(GL_UNIFORM_BUFFER, i, uniform_bindings[i]);
		glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
		if(storage_buffers)
		{
			for(GLuint i=0; i<storage_bindings.size(); ++i) restoreBufferBinding(GL_SHADER_STORAGE_BUFFER, i, storage_bindings[i]);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, storage_buffer);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
		}
		glPixelStorei(GL_PACK_ALIGNMENT, ints[9]);
		glPixelStorei(GL_PACK_ROW_LENGTH, ints[10]);
		glPixelStorei(GL_PACK_SKIP_ROWS, ints[11]);
		glPixelStorei(GL_PACK_SKIP_PIXELS, ints[12]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, ints[13]);
		glDepthFunc(ints[14]);
		glCullFace(ints[15]);
		glFrontFace(ints[16]);
		glReadBuffer(ints[17]);
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
		glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
		glClearColor(clear_colour[0], clear_colour[1], clear_colour[2], clear_colour[3]);
		glBlendColor(blend_colour[0], blend_colour[1], blend_colour[2], blend_colour[3]);
		glClearDepth(clear_depth);
		glDepthRange(depth_range[0], depth_range[1]);
		glDepthMask(depth_mask);
		for(size_t i=0; i<caps.size(); ++i) { if(enabled[i]) glEnable(caps[i]); else glDisable(caps[i]); }
		for(size_t i=0; i<blend.size(); ++i)
		{
			const Blend& b = blend[i];
			if(b.enabled) glEnablei(GL_BLEND, GLuint(i)); else glDisablei(GL_BLEND, GLuint(i));
			glColorMaski(GLuint(i), b.mask[0], b.mask[1], b.mask[2], b.mask[3]);
			if(indexed_blend)
			{
				glBlendFuncSeparatei(GLuint(i), b.values[0], b.values[1], b.values[2], b.values[3]);
				glBlendEquationSeparatei(GLuint(i), b.values[4], b.values[5]);
			}
			else if(i == 0)
			{
				glBlendFuncSeparate(b.values[0], b.values[1], b.values[2], b.values[3]);
				glBlendEquationSeparate(b.values[4], b.values[5]);
			}
		}
		for(size_t i=0; i<textures.size(); ++i)
		{
			glActiveTexture(GL_TEXTURE0 + GLenum(i));
			for(size_t j=0; j<texture_targets.size(); ++j)
			{
				const GLuint texture = textures[i][j];
				// Engine scratch textures may have been replaced at capture size.
				glBindTexture(texture_targets[j], texture && glIsTexture(texture) ? texture : 0);
			}
		}
		glActiveTexture(ints[6]);
	}
private:
	struct BufferBinding { GLint name; GLint64 start, size; };
	static void saveBufferBinding(GLenum binding, GLenum start, GLenum size, GLuint index, BufferBinding& out)
	{
		glGetIntegeri_v(binding, index, &out.name);
		glGetInteger64i_v(start, index, &out.start);
		glGetInteger64i_v(size, index, &out.size);
	}
	static void restoreBufferBinding(GLenum target, GLuint index, const BufferBinding& binding)
	{
		if(binding.name && glIsBuffer(binding.name) && binding.size > 0)
			glBindBufferRange(target, index, binding.name, GLintptr(binding.start), GLsizeiptr(binding.size));
		else glBindBufferBase(target, index, binding.name && glIsBuffer(binding.name) ? binding.name : 0);
	}
	const std::array<GLenum, 18> queries = {{GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_FRAMEBUFFER_BINDING, GL_RENDERBUFFER_BINDING,
		GL_CURRENT_PROGRAM, GL_VERTEX_ARRAY_BINDING, GL_ARRAY_BUFFER_BINDING, GL_ACTIVE_TEXTURE, GL_PIXEL_PACK_BUFFER_BINDING,
		GL_PIXEL_UNPACK_BUFFER_BINDING, GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
		GL_UNPACK_ALIGNMENT, GL_DEPTH_FUNC, GL_CULL_FACE_MODE, GL_FRONT_FACE, GL_READ_BUFFER}};
	const std::array<GLenum, 10> caps = {{GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_FRAMEBUFFER_SRGB,
		GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_RASTERIZER_DISCARD, GL_POLYGON_OFFSET_FILL, GL_DITHER}};
	const std::array<GLenum, 6> blend_queries = {{GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA}};
	const std::array<GLenum, 5> texture_queries = {{GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_3D, GL_TEXTURE_BINDING_CUBE_MAP, GL_TEXTURE_BINDING_2D_ARRAY, GL_TEXTURE_BINDING_2D_MULTISAMPLE}};
	const std::array<GLenum, 5> texture_targets = {{GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_2D_MULTISAMPLE}};
	struct Blend { GLboolean enabled, mask[4]; GLint values[6]; };
	std::array<GLint, 18> ints;
	std::array<GLboolean, 10> enabled;
	std::vector<Blend> blend;
	std::vector<std::array<GLint, 5>> textures;
	std::array<BufferBinding, 8> uniform_bindings;
	std::array<BufferBinding, 6> storage_bindings;
	GLint uniform_buffer, storage_buffer = 0, indirect_buffer = 0;
	bool storage_buffers;
	GLint viewport[4], scissor[4], polygon[2];
	GLfloat clear_colour[4], blend_colour[4];
	GLdouble depth_range[2], clear_depth;
	GLboolean depth_mask;
	bool indexed_blend;
};

class PhotoEngineState
{
public:
	explicit PhotoEngineState(OpenGLEngine& e) : engine(e), target(e.getTargetFrameBuffer()), viewport(e.getViewportDims()),
		main_width(e.getMainViewPortWidth()), main_height(e.getMainViewPortHeight()), scene(e.getCurrentScene()),
		overlays(scene->draw_overlay_objects), main_target(scene->render_to_main_render_framebuffer), exposure(scene->exposure_factor),
		saturation(scene->saturation_multiplier), bloom(scene->bloom_strength), dof(scene->dof_blur_strength), focus(scene->dof_blur_focus_distance) {}
	~PhotoEngineState()
	{
		engine.setTargetFrameBuffer(target);
		engine.setViewportDims(viewport);
		engine.setMainViewportDims(main_width, main_height);
		if(camera_changed)
		{
			engine.setPerspectiveCameraTransform(world_to_camera, sensor_width, lens_distance, aspect, shift_up, shift_right);
			scene->last_view_matrix = last_view;
		}
		scene->draw_overlay_objects = overlays;
		scene->render_to_main_render_framebuffer = main_target;
		scene->exposure_factor = exposure;
		scene->saturation_multiplier = saturation;
		scene->bloom_strength = bloom;
		scene->dof_blur_strength = dof;
		scene->dof_blur_focus_distance = focus;
	}
	void savePerspectiveCamera()
	{
		// Used only by desktop capture, never map/identity/XR cameras (whose
		// private projection overrides cannot be queried through the engine API).
		if(!scene->getCamToWorld().getInverseForAffine3Matrix(world_to_camera))
			throw std::runtime_error("Cannot save the scene camera transform");
		aspect = scene->render_aspect_ratio;
		sensor_width = qMin(scene->use_sensor_width, scene->use_sensor_height * aspect);
		lens_distance = scene->lens_sensor_dist;
		shift_up = scene->lens_shift_up_distance;
		shift_right = scene->lens_shift_right_distance;
		last_view = scene->last_view_matrix;
		camera_changed = true;
	}
private:
	OpenGLEngine& engine;
	Reference<FrameBuffer> target;
	Vec2i viewport;
	int main_width, main_height;
	OpenGLScene* scene;
	bool overlays, main_target;
	float exposure, saturation, bloom, dof, focus;
	bool camera_changed = false;
	Matrix4f world_to_camera, last_view;
	float sensor_width, lens_distance, aspect, shift_up, shift_right;
};

void checkPhotoGL(const char* stage)
{
	const GLenum error = glGetError();
	if(error != GL_NO_ERROR)
	{
		while(glGetError() != GL_NO_ERROR) {}
		throw std::runtime_error(std::string(stage) + " (OpenGL error " + std::to_string(error) + ")");
	}
}

void checkPhotoSize(const QSize& size)
{
	GLint texture = 0, renderbuffer = 0, viewport[2] = {0, 0};
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture);
	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer);
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport);
	PhotoFrameRenderer::checkSize(size, qMin(qMin(texture, renderbuffer), viewport[0]), qMin(qMin(texture, renderbuffer), viewport[1]));
}

void ensurePhotoFBO(std::unique_ptr<QOpenGLFramebufferObject>& fbo, const QSize& size, bool depth = false)
{
	if(fbo && fbo->size() == size) return;
	checkPhotoSize(size);
	QOpenGLFramebufferObjectFormat format;
	format.setInternalTextureFormat(GL_RGBA8);
	format.setSamples(0); // Engine resolves its own MSAA before final imaging.
	format.setAttachment(depth ? QOpenGLFramebufferObject::CombinedDepthStencil : QOpenGLFramebufferObject::NoAttachment);
	glActiveTexture(GL_TEXTURE0);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	std::unique_ptr<QOpenGLFramebufferObject> next(new QOpenGLFramebufferObject(size, format));
	if(!next->isValid()) throw std::runtime_error("Cannot allocate photo framebuffer; select a smaller resolution");
	checkPhotoGL("Photo framebuffer allocation failed");
	fbo = std::move(next);
}

QImage readPhotoPixels(QOpenGLFramebufferObject& fbo, const QRect& crop)
{
	QImage image(crop.size(), QImage::Format_RGBA8888);
	if(image.isNull()) throw std::runtime_error("Not enough memory for photo readback");
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.handle());
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
	glReadPixels(crop.x(), fbo.height() - crop.y() - crop.height(), crop.width(), crop.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
	checkPhotoGL("Photo readback failed");
	return image.mirrored();
}

QImage photoHistogram(const QImage& image)
{
	unsigned int bins[3][256] = {};
	for(int y=0; y<image.height(); ++y)
	{
		const uchar* row = image.constScanLine(y);
		for(int x=0; x<image.width(); ++x)
			for(int c=0; c<3; ++c) ++bins[c][row[x * 4 + c]];
	}
	unsigned int peak = 1;
	for(const auto& channel : bins) for(unsigned int n : channel) peak = qMax(peak, n);
	QImage chart(256, 96, QImage::Format_ARGB32_Premultiplied);
	chart.fill(QColor(20, 23, 28));
	QPainter painter(&chart);
	const QColor colours[] = {QColor(255, 90, 90, 160), QColor(90, 240, 130, 160), QColor(100, 155, 255, 160)};
	for(int c=0; c<3; ++c)
	{
		QPainterPath path;
		path.moveTo(0, 95);
		for(int x=0; x<256; ++x) path.lineTo(x, 95.0 - bins[c][x] * 91.0 / peak);
		path.lineTo(255, 95);
		painter.fillPath(path, colours[c]);
	}
	return chart;
}
}

class GlWidgetPhotoResources
{
public:
	QPointer<QOpenGLContext> context = QOpenGLContext::currentContext();
	std::unique_ptr<PhotoPostProcess> post{new PhotoPostProcess};
	std::unique_ptr<QOpenGLFramebufferObject> live_scene, live_clean, histogram;
	~GlWidgetPhotoResources() noexcept
	{
		// Qt resources can safely outlive their context. Raw postprocess names
		// must only be released with the owning context current.
		if(context && context == QOpenGLContext::currentContext())
			try { post->release(); } catch(...) {}
	}
};


// Export some symbols to indicate to the system that we want to run on a dedicated GPU if present.
// See https://stackoverflow.com/a/39047129
#if defined(_WIN32)
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif


// https://wiki.qt.io/How_to_use_OpenGL_Core_Profile_with_Qt
// https://developer.apple.com/opengl/capabilities/GLInfo_1085_Core.html
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))

static QSurfaceFormat makeFormat()
{
	// We need to request a 'core' profile.  Otherwise on OS X, we get an OpenGL 2.1 interface, whereas we require a v3+ interface.
	QSurfaceFormat format;
	// We need to request version 3.2 (or above?) on OS X, otherwise we get legacy version 2.
#ifdef OSX
	format.setVersion(3, 2);
#endif
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setSamples(4); // Enable multisampling

	return format;
}

#else

static QGLFormat makeFormat()
{
	// We need to request a 'core' profile.  Otherwise on OS X, we get an OpenGL 2.1 interface, whereas we require a v3+ interface.
	QGLFormat format;
	// We need to request version 3.2 (or above?) on OS X, otherwise we get legacy version 2.
#ifdef OSX
	format.setVersion(3, 2);
#endif
//	format.setVersion(4, 6); // TEMP NEW
	format.setProfile(QGLFormat::CoreProfile);
	format.setSampleBuffers(true); // Enable multisampling

//	format.setSwapInterval(0); // TEMP: turn off vsync

	return format;
}

#endif


GlWidget::GlWidget(QWidget *parent)
:	
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QOpenGLWidget(parent),
#else
	QGLWidget(makeFormat(), parent),
#endif
	cam_controller(NULL),
	cam_rot_on_mouse_move_enabled(true),
	cam_rot_on_right_mouse_move_enabled(false),
	cam_move_on_key_input_enabled(true),
	near_draw_dist(0.22f), // As large as possible as we can get without clipping becoming apparent.
	max_draw_dist(1000.f),
	gamepad(NULL),
	print_output(NULL),
	settings(NULL),
	take_map_screenshot(false),
	screenshot_ortho_sensor_width_m(10),
	external_perspective_camera_transform_enabled(false),
	external_world_to_camera_space_matrix(Matrix4f::identity()),
	external_sensor_width(0.f),
	external_lens_sensor_dist(0.f),
	external_render_aspect_ratio(0.f),
	external_lens_shift_up(0.f),
	external_lens_shift_right(0.f),
	external_projection_matrix_override_valid(false),
	external_projection_matrix_override(Matrix4f::identity()),
	allow_bindless_textures(true),
	allow_multi_draw_indirect(true)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	setFormat(makeFormat());
#endif

	viewport_w = viewport_h = 100;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);

	setMouseTracking(true); // Set this so we get mouse move events even when a mouse button is not down.


	gamepad_init_timer = new QTimer(this);
	gamepad_init_timer->setSingleShot(true);
	connect(gamepad_init_timer, SIGNAL(timeout()), this, SLOT(initGamepadsSlot()));
	gamepad_init_timer->start(/*msec=*/500); 

	//// See if we have any attached gamepads
	//QGamepadManager* manager = QGamepadManager::instance();

	//const QList<int> list = manager->connectedGamepads();

	//if(!list.isEmpty())
	//{
	//	gamepad = new QGamepad(list.at(0));

	//	connect(gamepad, SIGNAL(axisLeftXChanged(double)), this, SLOT(gamepadInputSlot()));
	//	connect(gamepad, SIGNAL(axisLeftYChanged(double)), this, SLOT(gamepadInputSlot()));
	//}


	// Create a CTRL+C shortcut just for this widget, so it doesn't interfere with the global CTRL+C shortcut for copying text from the chat etc.
	QShortcut* copy_shortcut = new QShortcut(QKeySequence(tr("Ctrl+C")), this);
	connect(copy_shortcut, SIGNAL(activated()), this, SIGNAL(copyShortcutActivated()));
	copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut); // We only want CTRL+C to work for the graphics view when it has focus.

	QShortcut* cut_shortcut = new QShortcut(QKeySequence(tr("Ctrl+X")), this);
	connect(cut_shortcut, SIGNAL(activated()), this, SIGNAL(cutShortcutActivated()));
	cut_shortcut->setContext(Qt::WidgetWithChildrenShortcut); // We only want CTRL+X to work for the graphics view when it has focus.

	QShortcut* paste_shortcut = new QShortcut(QKeySequence(tr("Ctrl+V")), this);
	connect(paste_shortcut, SIGNAL(activated()), this, SIGNAL(pasteShortcutActivated()));
	paste_shortcut->setContext(Qt::WidgetWithChildrenShortcut); // We only want CTRL+V to work for the graphics view when it has focus.
}


void GlWidget::initGamepadsSlot()
{
#if 1 // If use Qt for gamepad input:
	// See if we have any attached gamepads
	QGamepadManager* manager = QGamepadManager::instance();

	const QList<int> list = manager->connectedGamepads();

	if(print_output) print_output->print("Found " + toString(list.size()) + " connected gamepad(s).");

	if(!list.isEmpty())
	{
		gamepad = new QGamepad(list.at(0));
		
		const std::string name = QtUtils::toStdString(gamepad->name());
		if(print_output) print_output->print("Using gamepad '" + name + "'...");

		//connect(gamepad, SIGNAL(axisLeftXChanged(double)), this, SLOT(gamepadInputSlot()));
		//connect(gamepad, SIGNAL(axisLeftYChanged(double)), this, SLOT(gamepadInputSlot()));

		connect(gamepad, SIGNAL(buttonXChanged(bool)), this, SLOT(buttonXChangedSlot(bool)));
		connect(gamepad, SIGNAL(buttonXChanged(bool)), this, SIGNAL(gamepadButtonXChangedSignal(bool)));
		connect(gamepad, SIGNAL(buttonAChanged(bool)), this, SIGNAL(gamepadButtonAChangedSignal(bool)));
	}
#endif
}


void GlWidget::buttonXChangedSlot(bool pressed)
{
	// printVar(pressed);

}


void GlWidget::gamepadInputSlot()
{
	// TODO: have to handle interactions with click and drag to rotate camera code.
	// hideCursor();
}


GlWidget::~GlWidget()
{
	shutdown();
}


void GlWidget::shutdown()
{
	releasePhotoResources();
	opengl_engine = NULL;
}


void GlWidget::releasePhotoResources()
{
	if(!photo_resources) return;
	// closeEvent already makes this current; also cover standalone destruction
	// and Qt's native context teardown before the widget is destroyed.
	try
	{
		if(photo_resources->context && photo_resources->context->isValid())
		{
			PhotoCurrentContext current(*this);
			photo_resources.reset();
		}
		else photo_resources.reset();
	}
	catch(...) { photo_resources.reset(); } // Destruction/context teardown cannot throw.
}


void GlWidget::setPhotoSettings(const QVariantMap& values, bool enabled)
{
	photo_settings = values;
	photo_enabled = enabled;
	last_photo_error.clear();
	if(!enabled) last_photo_histogram_time = -1.0;
	update();
}


bool GlWidget::renderPhotoPreview()
{
	QString error;
	QImage histogram_image;
	try
	{
		QScopedValueRollback<bool> rendering(photo_frame_in_progress, true);
		PhotoGLState gl_state;
		PhotoEngineState engine_state(*opengl_engine);
		const double frame_time = photo_frame_timer.elapsed(); // One time for all passes in this frame.
		const QVariantMap values = photo_settings;
		const QSize size(viewport_w, viewport_h);
		const Reference<FrameBuffer> original_target = opengl_engine->getTargetFrameBuffer();
		const GLuint target = original_target ? original_target->buffer_name : QOpenGLContext::currentContext()->defaultFramebufferObject();
		while(glGetError() != GL_NO_ERROR) {} // Do not attribute earlier engine errors to this pass.
		if(!photo_resources) photo_resources.reset(new GlWidgetPhotoResources);
		ensurePhotoFBO(photo_resources->live_scene, size, true);
		Reference<FrameBuffer> scene_target = new FrameBuffer(photo_resources->live_scene->handle());
		scene_target->xres = size.width();
		scene_target->yres = size.height();
		opengl_engine->setTargetFrameBuffer(scene_target);
		opengl_engine->setMainViewportDims(size.width(), size.height());
		opengl_engine->getCurrentScene()->render_to_main_render_framebuffer = true;
		glDisable(GL_SCISSOR_TEST);
		opengl_engine->draw();
		checkPhotoGL("Photo scene render failed");
		photo_resources->post->render(photo_resources->live_scene->texture(), size, target, values, true, frame_time);
		checkPhotoGL("Photo preview failed");
		if(values.value(QStringLiteral("show_histogram"), false).toBool() &&
			(last_photo_histogram_time < 0 || frame_time - last_photo_histogram_time >= 0.25))
		{
			last_photo_histogram_time = frame_time;
			// Grade at the real frame size (identical grain/sharpening), without
			// guides/diagnostics, then downsample on GPU before the small readback.
			ensurePhotoFBO(photo_resources->live_clean, size);
			photo_resources->post->render(photo_resources->live_scene->texture(), size, photo_resources->live_clean->handle(), values, false, frame_time);
			const QRect crop = PhotoFrameRenderer::cropRect(size, values.value(QStringLiteral("aspect_ratio"), 0.0).toDouble());
			const QSize sample_size = crop.size().scaled(QSize(256, 144), Qt::KeepAspectRatio);
			ensurePhotoFBO(photo_resources->histogram, sample_size);
			glDisable(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, photo_resources->live_clean->handle());
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, photo_resources->histogram->handle());
			const int bottom = size.height() - crop.y() - crop.height();
			glBlitFramebuffer(crop.x(), bottom, crop.x() + crop.width(), bottom + crop.height(),
				0, 0, sample_size.width(), sample_size.height(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
			checkPhotoGL("Photo histogram downsample failed");
			histogram_image = photoHistogram(readPhotoPixels(*photo_resources->histogram, QRect(QPoint(), sample_size)));
		}
	}
	catch(const glare::Exception& e) { error = QString::fromStdString(e.what()); }
	catch(const std::exception& e) { error = QString::fromUtf8(e.what()); }
	catch(...) { error = tr("Unknown photo preview error"); }
	if(!error.isEmpty())
	{
		if(last_photo_error != error) { last_photo_error = error; emit photoRenderError(error); }
		return false; // Caller redraws the ordinary scene, after all restoration.
	}
	last_photo_error.clear();
	if(!histogram_image.isNull()) emit photoHistogramReady(histogram_image);
	return true;
}


QImage GlWidget::capturePhotoFrame(const QVariantMap& values, const QSize& requested_size)
{
	if(QThread::currentThread() != thread()) throw std::runtime_error("Photo capture must run on the widget thread");
	if(photo_frame_in_progress) throw std::runtime_error("Photo capture is already in progress");
	if(opengl_engine.isNull() || !opengl_engine->initSucceeded() || !cam_controller)
		throw std::runtime_error("The scene is not ready for photo capture");
	if(take_map_screenshot || external_perspective_camera_transform_enabled)
		throw std::runtime_error("Photo capture is unavailable during map or XR rendering");
	const PhotoFrameRenderer::Layout layout = PhotoFrameRenderer::layout(QSize(viewport_w, viewport_h), values, requested_size);
	QScopedValueRollback<bool> rendering(photo_frame_in_progress, true);
	PhotoCurrentContext current(*this);
	PhotoGLState gl_state;
	PhotoEngineState engine_state(*opengl_engine);
	engine_state.savePerspectiveCamera();
	const double frame_time = photo_frame_timer.elapsed();
	while(glGetError() != GL_NO_ERROR) {}
	checkPhotoSize(layout.scene_size);
	if(!photo_resources) photo_resources.reset(new GlWidgetPhotoResources);
	std::unique_ptr<QOpenGLFramebufferObject> source, output;
	ensurePhotoFBO(source, layout.scene_size, true);
	ensurePhotoFBO(output, layout.scene_size);
	Reference<FrameBuffer> target = new FrameBuffer(source->handle());
	target->xres = layout.scene_size.width();
	target->yres = layout.scene_size.height();
	opengl_engine->setTargetFrameBuffer(target);
	Matrix4f world_to_camera;
	cam_controller->getWorldToCameraMatrix(world_to_camera);
	// Set up the fresh camera against the original view aspect first. Capture
	// timers may move it after the last paint. Allocation rounding must not
	// alter its sensor dimensions; only then switch to the capture viewport.
	opengl_engine->setViewportDims(viewport_w, viewport_h);
	opengl_engine->setPerspectiveCameraTransform(world_to_camera, defaultSensorWidth(), float(cam_controller->lens_sensor_dist),
		float(viewport_w) / viewport_h, photo_shift_y, photo_shift_x);
	opengl_engine->setViewportDims(layout.scene_size.width(), layout.scene_size.height());
	opengl_engine->setMainViewportDims(layout.scene_size.width(), layout.scene_size.height());
	OpenGLScene* scene = opengl_engine->getCurrentScene();
	scene->render_to_main_render_framebuffer = true;
	scene->draw_overlay_objects = !values.value(QStringLiteral("hide_world_ui"), true).toBool() && scene->draw_overlay_objects;
	// Main may have temporarily neutralised these for the before/after button.
	// Capture always uses the selected grade and restores the neutral preview.
	const auto number = [&values](const char* key, float fallback) {
		const double value = values.value(QLatin1String(key), fallback).toDouble();
		if(!std::isfinite(value)) throw std::runtime_error("Invalid photo effect value");
		return value;
	};
	const double strength = qBound(0.0, number("filter_strength", 1.f), 1.0);
	scene->exposure_factor = float(std::exp2(qBound(-20.0, number("ev", 0.f), 20.0) * strength));
	scene->saturation_multiplier = float(1.0 + (qBound(0.0, number("saturation", 1.f), 10.0) - 1.0) * strength);
	scene->bloom_strength = float(qBound(0.0, number("bloom", scene->bloom_strength), 1.0) * strength);
	scene->dof_blur_strength = float(qBound(0.0, number("dof_blur", scene->dof_blur_strength), 1.0));
	if(values.value(QStringLiteral("autofocus_mode"), QStringLiteral("off")).toString() == QStringLiteral("off"))
		scene->dof_blur_focus_distance = float(qMax(0.001, number("focus_distance", scene->dof_blur_focus_distance)));
	glDisable(GL_SCISSOR_TEST);
	opengl_engine->draw();
	checkPhotoGL("Photo scene render failed");
	QVariantMap export_values = values;
	export_values.insert(QStringLiteral("compare_original"), false);
	export_values.insert(QStringLiteral("photo_pixel_scale"), double(layout.scene_size.width()) / viewport_w);
	photo_resources->post->render(source->texture(), layout.scene_size, output->handle(), export_values, false, frame_time);
	checkPhotoGL("Photo postprocessing failed");
	QImage image = readPhotoPixels(*output, layout.crop);
	// At most a rounding/downsampling adjustment. Native scene detail was
	// rendered above; there is never enlargement of a captured window image.
	if(image.size() != layout.output_size)
		image = image.scaled(layout.output_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	if(image.isNull()) throw std::runtime_error("Not enough memory for photo output");
	return image;
}


void GlWidget::setCameraController(CameraController* cam_controller_)
{
	cam_controller = cam_controller_;
}


void GlWidget::setExternalPerspectiveCameraTransform(const Matrix4f& world_to_camera_space_matrix, float sensor_width, float lens_sensor_dist, float render_aspect_ratio,
	float lens_shift_up, float lens_shift_right, bool projection_matrix_override_valid, const Matrix4f& projection_matrix_override)
{
	external_perspective_camera_transform_enabled = true;
	external_world_to_camera_space_matrix = world_to_camera_space_matrix;
	external_sensor_width = sensor_width;
	external_lens_sensor_dist = lens_sensor_dist;
	external_render_aspect_ratio = render_aspect_ratio;
	external_lens_shift_up = lens_shift_up;
	external_lens_shift_right = lens_shift_right;
	external_projection_matrix_override_valid = projection_matrix_override_valid;
	external_projection_matrix_override = projection_matrix_override;
}


void GlWidget::clearExternalPerspectiveCameraTransform()
{
	external_perspective_camera_transform_enabled = false;
	external_projection_matrix_override_valid = false;
}


void GlWidget::resizeGL(int width_, int height_)
{
	assert(QGLContext::currentContext() == this->context());

	viewport_w = width_;
	viewport_h = height_;

	if(this->opengl_engine)
	{
		this->opengl_engine->setViewportDims(viewport_w, viewport_h);

		this->opengl_engine->setMainViewportDims(viewport_w, viewport_h);

#if QT_VERSION_MAJOR >= 6
		// In Qt6, the GL widget uses a custom framebuffer (defaultFramebufferObject).  We want to make sure we draw to this.
		this->opengl_engine->setTargetFrameBuffer(new FrameBuffer(this->defaultFramebufferObject()));
#endif
	}

	emit viewportResizedSignal(width_, height_);
}


void GlWidget::initializeGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)
	connect(QOpenGLContext::currentContext(), &QOpenGLContext::aboutToBeDestroyed, this, [this]() { releasePhotoResources(); }, Qt::DirectConnection);

	// Initialise the engine's GL entry points before using gl3w-backed symbols.
	// On some Qt/driver startup paths initializeGL runs before OpenGLEngine::initialise;
	// calling glGetString earlier would dereference a null gl3w function pointer.
	const bool gl_entry_points_ready = gl3wInit() == 0;
	const GLubyte* vendor_string = gl_entry_points_ready ? glGetString(GL_VENDOR) : nullptr;
	const std::string opengl_vendor = vendor_string ? std::string((const char*)vendor_string) : std::string();
	const bool is_AMD    = StringUtils::containsString(toLowerCase(opengl_vendor), "ati") || StringUtils::containsString(toLowerCase(opengl_vendor), "amd");
	const bool is_Nvidia = StringUtils::containsString(toLowerCase(opengl_vendor), "nvidia");

	// Only enable SSAO/SSGI by default on AMD and Nvidia GPUs, which are likely to be more powerful than stuff like (integrated) Intel GPUs, which
	// struggle with SSAO/SSGI.
	const bool default_use_SSAO = is_AMD || is_Nvidia;

	bool shadows = true;
	bool use_MSAA = true;
	bool bloom = true;
	bool use_SSAO = false;
	if(settings)
	{
		shadows  = settings->value(MainOptionsDialog::shadowsKey(),	/*default val=*/true).toBool();
		use_MSAA = settings->value(MainOptionsDialog::MSAAKey(),	/*default val=*/true).toBool();
		bloom    = settings->value(MainOptionsDialog::BloomKey(),	/*default val=*/true).toBool();
		use_SSAO = settings->value(MainOptionsDialog::SSAOKey(),    /*default val=*/default_use_SSAO).toBool();
	}

	// Enable debug output (glDebugMessageCallback) in Debug and RelWithDebugInfo mode, e.g. when BUILD_TESTS is 1.
	// Don't enable in Release mode, in case it has a performance cost.
#if BUILD_TESTS
	const bool enable_debug_outout = true;
#else
	const bool enable_debug_outout = false;
#endif

	OpenGLEngineSettings engine_settings;
	engine_settings.enable_debug_output = enable_debug_outout;
	engine_settings.shadow_mapping = shadows;
	engine_settings.compress_textures = true;
	engine_settings.depth_fog = true;
	// Build the environment volumetric-cloud shader path.  It is runtime
	// toggled from EnvironmentOptionsWidget, so enabling support here does not
	// force clouds to be drawn when the user turns them off.
	engine_settings.volumetric_clouds_support = true;
	//engine_settings.use_final_image_buffer = bloom;
	engine_settings.msaa_samples = use_MSAA ? 4 : -1;
	engine_settings.max_tex_CPU_mem_usage = 1536 * 1024 * 1024ull; // Should be large enough that we have some spare room for the LRU texture cache.
	engine_settings.max_tex_GPU_mem_usage = 1536 * 1024 * 1024ull; // Should be large enough that we have some spare room for the LRU texture cache.
	engine_settings.allow_multi_draw_indirect = this->allow_multi_draw_indirect;
	engine_settings.allow_bindless_textures = this->allow_bindless_textures;
	engine_settings.ssao = use_SSAO;

#ifdef OSX
	// Force SSAO to false for now on Mac, as when it's enabled, the number of texture units exceeds the max (16) for the terrain shader.
	engine_settings.ssao_support = false;
	engine_settings.ssao = false; 
#endif


	opengl_engine = new OpenGLEngine(engine_settings);

	std::string data_dir = base_dir_path + "/data";
#if BUILD_TESTS
	try
	{
		// For development, allow loading opengl engine data, particularly shaders, straight from the repo dir.
		data_dir = PlatformUtils::getEnvironmentVariable("SUBSTRATA_USE_OPENGL_DATA_DIR"); // SUBSTRATA_USE_OPENGL_DATA_DIR can be set to e.g. n:/glare-core/opengl
		conPrint("Using OpenGL data dir from Env var: '" + data_dir + "'");
	}
	catch(glare::Exception&)
	{}
	
	try
	{
		const std::string substrata_dir = PlatformUtils::getEnvironmentVariable("SUBSTRATA_TRUNK_DIR");
		opengl_engine->additional_shader_dirs.push_back(substrata_dir);
	}
	catch(glare::Exception&)
	{}
#endif
		
	opengl_engine->initialise(
		data_dir, // data dir (should contain 'shaders' and 'gl_data')
		NULL, // texture_server
		this->print_output,
		main_task_manager,
		high_priority_task_manager,
		main_mem_allocator
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
		initialisation_error_msg = opengl_engine->getInitialisationErrorMsg();
	}

	if(opengl_engine->initSucceeded())
	{
		try
		{
			opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir_path + "/data/resources/cirrus.exr"));
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}
		
		try
		{
			// opengl_engine->setSnowIceTexture(opengl_engine->getTexture(base_dir_path + "/resources/snow-ice-01-normal.png"));
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}

		if(bloom)
			opengl_engine->getCurrentScene()->bloom_strength = 0.3f;

		// Aurora setting will be controlled by Environment panel
	}
}


void* GlWidget::makeNewSharedGLContext()
{
#if defined(_WIN32)
	QOpenGLContext* window_context = this->context()->contextHandle();
	
	QOpenGLContext* new_window_context = new QOpenGLContext();
	new_window_context->setFormat(window_context->format());
	new_window_context->setShareContext(window_context);
	new_window_context->create();
	assert(new_window_context->isValid());


	QVariant nativeHandle = new_window_context->nativeHandle();
	assert(!nativeHandle.isNull() && nativeHandle.canConvert<QWGLNativeContext>());
	
	QWGLNativeContext nativeContext = nativeHandle.value<QWGLNativeContext>();
	HGLRC hglrc = nativeContext.context();

	return hglrc;
#else
	return nullptr;
#endif
}


void GlWidget::paintGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)
	if(opengl_engine.isNull())
		return;

	if(take_map_screenshot)
	{
		const Vec3d cam_pos =  cam_controller->getPosition();

		const Matrix4f world_to_camera_space_matrix = Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()) * Matrix4f::translationMatrix(-(cam_pos.toVec4fVector()));

		opengl_engine->setViewportDims(viewport_w, viewport_h);
		opengl_engine->setNearDrawDistance(near_draw_dist);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setDiagonalOrthoCameraTransform(world_to_camera_space_matrix, /*sensor_width*/screenshot_ortho_sensor_width_m, /*render_aspect_ratio=*/1.f);
		//opengl_engine->setOrthoCameraTransform(world_to_camera_space_matrix, /*sensor_width*/screenshot_ortho_sensor_width_m, /*render_aspect_ratio=*/1.f, 0, 0);
		opengl_engine->draw();
		return;
	}


	if(external_perspective_camera_transform_enabled)
	{
		opengl_engine->setViewportDims(viewport_w, viewport_h);
		opengl_engine->setNearDrawDistance(near_draw_dist);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setPerspectiveCameraTransform(
			external_world_to_camera_space_matrix,
			external_sensor_width,
			external_lens_sensor_dist,
			external_render_aspect_ratio,
			external_lens_shift_up,
			external_lens_shift_right
		);
		if(external_projection_matrix_override_valid)
			opengl_engine->setProjectionMatrixOverride(external_projection_matrix_override);
		opengl_engine->draw();
		opengl_engine->clearProjectionMatrixOverride();
		return;
	}


	if(cam_controller)
	{
		// Work out current camera transform
		Matrix4f world_to_camera_space_matrix;
		cam_controller->getWorldToCameraMatrix(world_to_camera_space_matrix);

		const float sensor_width = defaultSensorWidth();
		const float lens_sensor_dist = (float)cam_controller->lens_sensor_dist;
		const float render_aspect_ratio = (float)viewport_w / (float)viewport_h;
		opengl_engine->setViewportDims(viewport_w, viewport_h);
		opengl_engine->setNearDrawDistance(near_draw_dist);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, photo_shift_y, photo_shift_x);
		//opengl_engine->setOrthoCameraTransform(world_to_camera_space_matrix, 1000.f, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
		if(!photo_enabled || photo_frame_in_progress || !renderPhotoPreview())
			opengl_engine->draw();
	}

	//conPrint("FPS: " + doubleToStringNSigFigs(1 / fps_timer.elapsed(), 1));
	//fps_timer.reset();

	FrameMark; // Tracy profiler
}


void GlWidget::keyPressEvent(QKeyEvent* e)
{
	emit keyPressed(e);
}


void GlWidget::keyReleaseEvent(QKeyEvent* e)
{
	emit keyReleased(e);
}


// If this widget loses focus, just consider all keys up.
// Otherwise we might miss the key-up event, leading to our keys appearing to be stuck down.
void GlWidget::focusOutEvent(QFocusEvent* e)
{
	emit focusOutSignal();
}


void GlWidget::hideCursor()
{
	// Hide cursor when moving view.
	this->setCursor(QCursor(Qt::BlankCursor));
}


bool GlWidget::isCursorHidden()
{
	return this->cursor().shape() == Qt::BlankCursor;
}


void GlWidget::setCursorIfNotHidden(Qt::CursorShape new_shape)
{
	if(this->cursor().shape() != Qt::BlankCursor)
		this->setCursor(new_shape);
}


void GlWidget::mousePressEvent(QMouseEvent* e)
{
	//conPrint("mousePressEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
	mouse_move_origin = QCursor::pos();
	last_mouse_press_pos = QCursor::pos();

	// Hide cursor when moving view.
	//this->setCursor(QCursor(Qt::BlankCursor));

	emit mousePressed(e);
}


void GlWidget::mouseReleaseEvent(QMouseEvent* e)
{
	// Unhide cursor.
	this->unsetCursor();

	//conPrint("mouseReleaseEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));

	//if((QCursor::pos() - last_mouse_press_pos).manhattanLength() < 4)
	{
		//conPrint("Click at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
		//conPrint("Click at " + toString(e->pos().x()) + ", " + toString(e->pos().y()));

		emit mouseReleased(e);
	}
}


void GlWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void GlWidget::mouseMoveEvent(QMouseEvent* e)
{
	Qt::MouseButtons mb = e->buttons();
	const bool rotate_with_left_button = cam_rot_on_mouse_move_enabled && (mb & Qt::LeftButton);
	const bool rotate_with_right_button = cam_rot_on_right_mouse_move_enabled && (mb & Qt::RightButton);
	if((rotate_with_left_button || rotate_with_right_button) && (cam_controller != NULL))// && (e->modifiers() & Qt::AltModifier))
	{
		/*switch(e->source())
		{
			case Qt::MouseEventNotSynthesized: conPrint("Qt::MouseEventNotSynthesized");
			case Qt::MouseEventSynthesizedBySystem: conPrint("Qt::MouseEventSynthesizedBySystem");
			case Qt::MouseEventSynthesizedByQt: conPrint("Qt::MouseEventSynthesizedByQt");
			case Qt::MouseEventSynthesizedByApplication: conPrint("Qt::MouseEventSynthesizedByApplication");
		}*/

		//double shift_scale = ((e->modifiers() & Qt::ShiftModifier) == 0) ? 1.0 : 0.35; // If shift is held, movement speed is roughly 1/3

		// Get new mouse position, movement vector and set previous mouse position to new.
		QPoint new_pos = QCursor::pos();
		QPoint delta(new_pos.x() - mouse_move_origin.x(),
					 mouse_move_origin.y() - new_pos.y()); // Y+ is down in screenspace, not up as desired.

		// QCursor::setPos() seems to generate mouse move events, which we don't want to affect the camera.  Only rotate camera based on actual mouse movements.
		if(e->source() == Qt::MouseEventNotSynthesized)
		{
			if(rotate_with_left_button || rotate_with_right_button)
				cam_controller->updateRotation(/*pitch_delta=*/delta.y(), /*heading_delta=*/delta.x()/* * shift_scale*/);
			//if(mb & Qt::MidButton)   cam_controller->update(Vec3d(delta.x(), 0, delta.y()) * shift_scale, Vec2d(0, 0));
			//if(mb & Qt::RightButton) cam_controller->update(Vec3d(0, delta.y(), 0) * shift_scale, Vec2d(0, 0));
		}

		// On Windows/linux, reset the cursor position to where we started, so we never run out of space to move.
		// QCursor::setPos() does not work on mac, and also gives a message about Substrata trying to control the computer, which we want to avoid.
		// So don't use setPos() on Mac.
#if defined(OSX)
		mouse_move_origin = QCursor::pos();
#else
		QCursor::setPos(mouse_move_origin);
		mouse_move_origin = QCursor::pos();
#endif

		//conPrint("mouseMoveEvent FPS: " + doubleToStringNSigFigs(1 / fps_timer.elapsed(), 1));
		//fps_timer.reset();
	}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QOpenGLWidget::mouseMoveEvent(e);
#else
	QGLWidget::mouseMoveEvent(e);
#endif

	emit mouseMoved(e);

	//conPrint("mouseMoveEvent time since last event: " + doubleToStringNSigFigs(fps_timer.elapsed(), 5));
	//fps_timer.reset();
}


void GlWidget::wheelEvent(QWheelEvent* e)
{
	emit mouseWheelSignal(e);
}


void GlWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
	emit mouseDoubleClickedSignal(e);
}
