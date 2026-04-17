/*=====================================================================
AvatarPreviewGLUIWidget.h
-------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIWidget.h>
#include <physics/jscol_aabbox.h>
#include <functional>


class FrameBuffer;
class RenderBuffer;
class OpenGLTexture;


/*=====================================================================
AvatarPreviewGLUIWidget
-----------------------
GLUI widget that displays a render-to-texture avatar preview and
supports simple orbit / pan / zoom camera controls.
=====================================================================*/
class AvatarPreviewGLUIWidget final : public GLUIWidget
{
public:
	AvatarPreviewGLUIWidget(GLUI& glui_);
	~AvatarPreviewGLUIWidget();

	virtual void setPos(const Vec2f& botleft) override;
	virtual void setPosAndDims(const Vec2f& botleft, const Vec2f& dims) override;
	virtual void setClipRegion(const Rect2f& clip_rect) override;
	virtual void updateGLTransform() override;
	virtual void setVisible(bool visible) override;
	virtual bool isVisible() override;
	virtual void handleMousePress(MouseEvent& e) override;
	virtual void handleMouseRelease(MouseEvent& e) override;
	virtual void doHandleMouseMoved(MouseEvent& e) override;
	virtual void doHandleMouseWheelEvent(MouseWheelEvent& e) override;

	void recreateFBO(int avatar_preview_w, int avatar_preview_h);
	void renderAvatarPreview();
	void resetCameraFromAvatarTransform(const Matrix4f& avatar_ob_to_world_matrix);
	void fitCameraToAABB(const js::AABBox& aabb_ws);

	std::function<void()> pre_draw_func;
	std::function<bool(MouseEvent&)> press_interceptor;
	std::function<bool(MouseEvent&)> move_interceptor;
	std::function<void(MouseEvent&)> release_interceptor;

private:
	void updateOverlayTransform();

	GLUI* gl_ui;
	Reference<OpenGLEngine> opengl_engine;
	OverlayObjectRef overlay_ob;
	Reference<FrameBuffer> avatar_preview_fbo;
	Reference<FrameBuffer> avatar_preview_resolve_fbo;
	Reference<RenderBuffer> avatar_preview_color_rb;
	Reference<RenderBuffer> avatar_preview_depth_rb;
	Reference<OpenGLTexture> avatar_preview_tex;

	float cam_phi;
	float cam_dist;
	float cam_theta;
	Vec4f cam_target_pos;
	bool drag_active;
	float drag_start_x;
	float drag_start_phi;
	bool middle_drag_active;
	Vec2i middle_drag_last_cursor_pos;
};
