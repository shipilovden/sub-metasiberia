/*=====================================================================
AvatarPreviewGLUIWidget.cpp
---------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "AvatarPreviewGLUIWidget.h"


#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <opengl/RenderBuffer.h>
#include <opengl/IncludeOpenGL.h>
#include <graphics/SRGBUtils.h>
#include <maths/Matrix4f.h>
#include <maths/GeometrySampling.h>
#include <cmath>


namespace
{
static void blitPreviewFrameBuffer(FrameBuffer& src_framebuffer, FrameBuffer& dest_framebuffer)
{
	assert(src_framebuffer.xRes() == dest_framebuffer.xRes() && src_framebuffer.yRes() == dest_framebuffer.yRes());

	src_framebuffer.bindForReading();
	dest_framebuffer.bindForDrawing();

	glReadBuffer(GL_COLOR_ATTACHMENT0);
	const GLenum draw_buf = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &draw_buf);

	glBlitFramebuffer(
		/*srcX0=*/0, /*srcY0=*/0, /*srcX1=*/(int)src_framebuffer.xRes(), /*srcY1=*/(int)src_framebuffer.yRes(),
		/*dstX0=*/0, /*dstY0=*/0, /*dstX1=*/(int)dest_framebuffer.xRes(), /*dstY1=*/(int)dest_framebuffer.yRes(),
		GL_COLOR_BUFFER_BIT,
		GL_NEAREST
	);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}
}


AvatarPreviewGLUIWidget::AvatarPreviewGLUIWidget(GLUI& glui_)
:	gl_ui(&glui_),
	cam_phi(0.f),
	cam_dist(2.0f),
	cam_theta(1.4f),
	cam_target_pos(0.f, 0.f, 1.0f, 1.f),
	drag_active(false),
	drag_start_x(0.f),
	drag_start_phi(0.f),
	middle_drag_active(false),
	middle_drag_last_cursor_pos(0, 0)
{
	opengl_engine = glui_.opengl_engine;
	m_z = -0.9f;
	rect = Rect2f(Vec2f(0.f), Vec2f(0.1f));

	overlay_ob = new OverlayObject();
	overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
	overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(1000.f, 1000.f, 0.f); // Offscreen until positioned.
	opengl_engine->addOverlayObject(overlay_ob);

	setFixedDimsPx(Vec2f(200.f, 400.f), *gl_ui);
}


AvatarPreviewGLUIWidget::~AvatarPreviewGLUIWidget()
{
	if(overlay_ob.nonNull())
		opengl_engine->removeOverlayObject(overlay_ob);
}


void AvatarPreviewGLUIWidget::setPos(const Vec2f& botleft)
{
	rect = Rect2f(botleft, botleft + getDims());
	updateOverlayTransform();
}


void AvatarPreviewGLUIWidget::setPosAndDims(const Vec2f& botleft, const Vec2f& dims)
{
	rect = Rect2f(botleft, botleft + dims);
	updateOverlayTransform();
}


void AvatarPreviewGLUIWidget::setClipRegion(const Rect2f& clip_rect)
{
	if(overlay_ob.nonNull())
		overlay_ob->clip_region = gl_ui->OpenGLRectCoordsForUICoords(clip_rect);
}


void AvatarPreviewGLUIWidget::updateGLTransform()
{
	Vec2f dims = getDims();
	if(sizing_type_x == SizingType_FixedSizePx)
		dims.x = gl_ui->getUIWidthForDevIndepPixelWidth(fixed_size.x);
	if(sizing_type_y == SizingType_FixedSizePx)
		dims.y = gl_ui->getUIWidthForDevIndepPixelWidth(fixed_size.y);

	rect = Rect2f(rect.getMin(), rect.getMin() + dims);
	updateOverlayTransform();
}


void AvatarPreviewGLUIWidget::setVisible(bool visible)
{
	if(overlay_ob.nonNull())
		overlay_ob->draw = visible;
}


bool AvatarPreviewGLUIWidget::isVisible()
{
	return overlay_ob.nonNull() && overlay_ob->draw;
}


void AvatarPreviewGLUIWidget::handleMousePress(MouseEvent& e)
{
	const Vec2f ui_coords = gl_ui->UICoordsForOpenGLCoords(e.gl_coords);
	if(rect.inClosedRectangle(ui_coords))
	{
		if(press_interceptor && press_interceptor(e))
		{
			e.accepted = true;
			return;
		}

		if(e.button == MouseButton::Left)
		{
			drag_active = true;
			drag_start_x = ui_coords.x;
			drag_start_phi = cam_phi;
			e.accepted = true;
		}
		else if(e.button == MouseButton::Middle)
		{
			middle_drag_active = true;
			middle_drag_last_cursor_pos = e.cursor_pos;
			e.accepted = true;
		}
	}
}


void AvatarPreviewGLUIWidget::handleMouseRelease(MouseEvent& e)
{
	if(release_interceptor)
		release_interceptor(e);

	if(e.button == MouseButton::Left)
		drag_active = false;
	if(e.button == MouseButton::Middle)
		middle_drag_active = false;
}


void AvatarPreviewGLUIWidget::doHandleMouseMoved(MouseEvent& e)
{
	if(move_interceptor && move_interceptor(e))
	{
		return;
	}

	if(drag_active)
	{
		const Vec2f ui_coords = gl_ui->UICoordsForOpenGLCoords(e.gl_coords);
		// One full revolution (2pi) per 1.0 UI-coord of horizontal drag.
		cam_phi = drag_start_phi + (ui_coords.x - drag_start_x) * Maths::get2Pi<float>();
	}

	if(middle_drag_active)
	{
		const Vec2i delta = e.cursor_pos - middle_drag_last_cursor_pos;
		middle_drag_last_cursor_pos = e.cursor_pos;

		const float move_scale = 0.005f;
		const Vec4f forwards = GeometrySampling::dirForSphericalCoords(-cam_phi + Maths::pi_2<float>(), Maths::pi<float>() - cam_theta);
		const Vec4f right = normalise(crossProduct(forwards, Vec4f(0, 0, 1, 0)));
		const Vec4f up = crossProduct(right, forwards);
		cam_target_pos += right * -(float)delta.x * move_scale + up * (float)delta.y * move_scale;
	}
}


void AvatarPreviewGLUIWidget::doHandleMouseWheelEvent(MouseWheelEvent& e)
{
	const Vec2f ui_coords = gl_ui->UICoordsForOpenGLCoords(e.gl_coords);
	if(rect.inClosedRectangle(ui_coords))
	{
		cam_dist = myClamp(cam_dist - cam_dist * e.angle_delta.y * 0.016f, 0.01f, 20.f);
		e.accepted = true;
	}
}


void AvatarPreviewGLUIWidget::recreateFBO(int avatar_preview_w, int avatar_preview_h)
{
	const int msaa_samples = opengl_engine->settings.msaa_samples;

	avatar_preview_color_rb = new RenderBuffer(avatar_preview_w, avatar_preview_h, msaa_samples, Format_RGBA_Linear_Uint8);
	avatar_preview_depth_rb = new RenderBuffer(avatar_preview_w, avatar_preview_h, msaa_samples, Format_Depth_Float);

	avatar_preview_fbo = new FrameBuffer();
	avatar_preview_fbo->attachRenderBuffer(*avatar_preview_color_rb, GL_COLOR_ATTACHMENT0);
	avatar_preview_fbo->attachRenderBuffer(*avatar_preview_depth_rb, GL_DEPTH_ATTACHMENT);
	assert(avatar_preview_fbo->isComplete());

	avatar_preview_tex = new OpenGLTexture(avatar_preview_w, avatar_preview_h, opengl_engine.ptr(),
		ArrayRef<uint8>(),
		Format_SRGBA_Uint8,
		OpenGLTexture::Filtering_Bilinear,
		OpenGLTexture::Wrapping_Clamp,
		/*has_mipmaps=*/false
	);

	avatar_preview_resolve_fbo = new FrameBuffer();
	avatar_preview_resolve_fbo->attachTexture(*avatar_preview_tex, GL_COLOR_ATTACHMENT0);
	assert(avatar_preview_resolve_fbo->isComplete());

	overlay_ob->material.albedo_linear_rgb = Colour3f(1.f);
	overlay_ob->material.overlay_show_just_tex_rgb = true; // Ignore preview texture alpha, show opaque RGB image.
	overlay_ob->material.albedo_texture = avatar_preview_tex;
}


void AvatarPreviewGLUIWidget::renderAvatarPreview()
{
	if(avatar_preview_fbo.isNull())
		return;

	opengl_engine->setTargetFrameBufferAndViewport(avatar_preview_fbo);
	opengl_engine->setNearDrawDistance(0.1f);
	opengl_engine->setMaxDrawDistance(100.f);

	const Matrix4f T = Matrix4f::translationMatrix(0.f, cam_dist, 0.f);
	const Matrix4f z_rot = Matrix4f::rotationAroundZAxis(cam_phi);
	const Matrix4f x_rot = Matrix4f::rotationAroundXAxis(-(cam_theta - Maths::pi_2<float>()));
	const Matrix4f world_to_cam = T * x_rot * z_rot * Matrix4f::translationMatrix(-cam_target_pos);

	const float sensor_width = 0.035f;
	const float lens_sensor_dist = 0.05f;
	const float render_aspect_ratio = (float)avatar_preview_fbo->xRes() / (float)avatar_preview_fbo->yRes();
	opengl_engine->setPerspectiveCameraTransform(world_to_cam, sensor_width, lens_sensor_dist, render_aspect_ratio, 0.f, 0.f);

	if(pre_draw_func)
		pre_draw_func();

	opengl_engine->draw();

	if(avatar_preview_resolve_fbo.nonNull())
		blitPreviewFrameBuffer(*avatar_preview_fbo, *avatar_preview_resolve_fbo);
}


void AvatarPreviewGLUIWidget::updateOverlayTransform()
{
	if(overlay_ob.isNull())
		return;

	const Vec2f botleft = rect.getMin();
	const Vec2f dims = rect.getWidths();
	const float y_scale = opengl_engine->getViewPortAspectRatio();
	overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(botleft.x, botleft.y * y_scale, m_z) * Matrix4f::scaleMatrix(dims.x, dims.y * y_scale, 1.f);
}


void AvatarPreviewGLUIWidget::resetCameraFromAvatarTransform(const Matrix4f& avatar_ob_to_world_matrix)
{
	const Vec4f ob_translation = avatar_ob_to_world_matrix.getColumn(3);

	// Stable default framing: keep the avatar centred even before precise AABB fit is available.
	cam_target_pos = Vec4f(ob_translation[0], ob_translation[1], ob_translation[2] + 0.95f, 1.f);
	cam_dist = 2.0f;
	cam_phi = Maths::pi<float>();
	cam_theta = 1.35f;
}


void AvatarPreviewGLUIWidget::fitCameraToAABB(const js::AABBox& aabb_ws)
{
	if(aabb_ws.isEmpty() || !aabb_ws.min_.isFinite() || !aabb_ws.max_.isFinite())
		return;

	const Vec4f aabb_min = aabb_ws.min_;
	const Vec4f aabb_max = aabb_ws.max_;
	const Vec4f centre = (aabb_min + aabb_max) * 0.5f;
	const Vec4f extents = aabb_max - aabb_min;

	const float height = myMax(0.2f, extents[2]);
	const float half_width_xy = myMax(0.12f, myMax(extents[0], extents[1]) * 0.5f);
	const float half_height = myMax(0.18f, height * 0.5f);

	cam_target_pos = Vec4f(centre[0], centre[1], aabb_min[2] + height * 0.56f, 1.f);
	cam_phi = Maths::pi<float>();
	cam_theta = 1.35f;

	const float sensor_width = 0.035f;
	const float lens_sensor_dist = 0.05f;
	const float aspect = (avatar_preview_fbo.nonNull() && avatar_preview_fbo->yRes() > 0) ? ((float)avatar_preview_fbo->xRes() / (float)avatar_preview_fbo->yRes()) : 1.0f;
	const float fov_x = 2.f * std::atan((sensor_width * 0.5f) / lens_sensor_dist);
	const float fov_y = 2.f * std::atan(std::tan(fov_x * 0.5f) / myMax(0.1f, aspect));
	const float safe_half_fov_x = myMax(0.15f, fov_x * 0.5f);
	const float safe_half_fov_y = myMax(0.15f, fov_y * 0.5f);

	const float dist_from_height = half_height / std::tan(safe_half_fov_y);
	const float dist_from_width = half_width_xy / std::tan(safe_half_fov_x);
	const float fit_dist = myMax(dist_from_height, dist_from_width);

	cam_dist = myClamp(fit_dist * 1.18f, 0.5f, 20.f);
}
