/*=====================================================================
GearInventoryUI.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "AvatarGraphics.h"
#include "GearEditorUI.h"
#include "../shared/GearItem.h"
#include <ThreadSafeRefCounted.h>
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIWindow.h>
#include <ui/UIEvents.h>


class GUIClient;
struct GLObject;
class AvatarPreviewGLUIWidget;
class OpenGLScene;


/*=====================================================================
GearInventoryUI
---------------
Shows the gear inventory with a large avatar preview panel, an equipped
gear grid, and an all-gear grid.
=====================================================================*/
class GearInventoryUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	GearInventoryUI(GUIClient* gui_client_, GLUIRef gl_ui_);
	~GearInventoryUI();

	void think();
	void viewportResized(int w, int h);
	void keyPressed(KeyEvent& e);
	void setEquippedGear(const GearItems& equipped_gear_);
	void setAllGear(const GearItems& all_gear_);
	void setAvatarGLObject(const AvatarGraphics& graphics, const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix);
	virtual void eventOccurred(GLUICallbackEvent& event) override;
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override;
	void updateWidgetPositions();
	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex);

	bool close_soon;

private:
	struct GearItemUI
	{
		GLUIButtonRef thumbnail;
		GearItemRef gear_item;
	};

	void rebuildEquippedGrid();
	void rebuildAllGearGrid();
	void openEditorForItem(const GearItemRef& item);
	void renderAvatarPreview();
	void recreateAvatarPreviewFBO();

	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;

	GearItems equipped_gear;
	GearItems all_gear;

	std::vector<GearItemUI> equipped_gear_ui;
	std::vector<GearItemUI> all_gear_ui;

	Reference<AvatarPreviewGLUIWidget> avatar_preview_widget;
	GLUIGridContainerRef outer_grid;
	GLUIGridContainerRef equipped_grid;
	GLUIGridContainerRef all_gear_grid;
	Reference<GearEditorUI> gear_editor_ui;

	Reference<OpenGLScene> avatar_preview_scene;
	Reference<OpenGLScene> main_world_scene;
	GLObjectRef avatar_preview_gl_ob;
	Matrix4f avatar_pre_ob_to_world_matrix;

	GLUIWindowRef window;
	bool need_rebuild_equipped;
	bool need_rebuild_all_gear;
};
