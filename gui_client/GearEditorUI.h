/*=====================================================================
GearEditorUI.h
--------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/GearItem.h"
#include <ThreadSafeRefCounted.h>
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUILineEdit.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIWindow.h>


class GUIClient;
struct GLObject;


/*=====================================================================
GearEditorUI
------------
Branch-compatible gear editor based on the GLUI controls already present
in our current glare-core checkout.
=====================================================================*/
class GearEditorUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	GearEditorUI(GUIClient* gui_client_, GLUIRef gl_ui_, GearItemRef gear_item_);
	~GearEditorUI();

	void think();
	void viewportResized(int w, int h);
	void setAvatarGLObject(const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix);
	virtual void eventOccurred(GLUICallbackEvent& event) override;
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override;
	void updateWidgetPositions();
	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex);

	bool close_soon;

private:
	void addLabel(int row, const std::string& text);
	void addLineEdit(int row, const std::string& label, const std::string& value, GLUILineEditRef& edit_out);
	void refreshFieldsFromGearItem();
	void gearItemChanged();

	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;
	GearItemRef gear_item;

	GLUIWindowRef window;
	GLUIGridContainerRef controls_grid;

	GLUILineEditRef name_line_edit;
	GLUILineEditRef desc_line_edit;
	GLUILineEditRef bone_line_edit;
	GLUILineEditRef pos_x_line_edit;
	GLUILineEditRef pos_y_line_edit;
	GLUILineEditRef pos_z_line_edit;
	GLUILineEditRef rot_x_line_edit;
	GLUILineEditRef rot_y_line_edit;
	GLUILineEditRef rot_z_line_edit;
	GLUILineEditRef scale_x_line_edit;
	GLUILineEditRef scale_y_line_edit;
	GLUILineEditRef scale_z_line_edit;
	GLUITextButtonRef apply_button;
};
