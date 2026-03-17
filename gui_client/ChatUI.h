/*=====================================================================
ChatUI.h
--------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIWindow.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUILineEdit.h>
#include <opengl/ui/GLUIGridContainer.h>
#include "ClientThread.h"
#include <vector>


class GUIClient;
class Avatar;
class MouseWheelEvent;


/*=====================================================================
ChatUI
------

=====================================================================*/
class ChatUI : public GLUICallbackHandler
{
public:
	ChatUI();
	~ChatUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void setVisible(bool visible);

	void appendMessage(const std::string& avatar_name, const Colour3f& avatar_colour, const std::string& msg);

	void viewportResized(int w, int h);

	void setDrawAreaBottomLeftY(float draw_area_bottom_left_y);

	void handleMousePress(MouseEvent& mouse_event);
	void handleMouseWheelEvent(MouseWheelEvent& mouse_event);
	void handleMouseMoved(MouseEvent& mouse_event);

	virtual void eventOccurred(GLUICallbackEvent& event);
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override;
private:
	bool isInitialisedFully();
	float computeWidgetWidth();
	float computeWidgetHeight();
	void rebuildEmojiPickerContents();
	void sendEmojiMessage(const std::string& emoji);
	void setEmojiPickerOpen(bool open);

	struct ChatMessage
	{
		Colour3f avatar_colour;
		std::string avatar_name, msg;

		//GLUITextViewRef name_text;
		GLUITextViewRef msg_text;
	};

	void setWidgetVisibilityForExpanded();
	void updateWidgetTransforms();
	void recreateMessageTextViews();
	void recreateTextViewsForMessage(ChatMessage& msg, int row_index);


	std::list<ChatMessage> messages;

	bool expanded;
	bool visible;
	bool emoji_picker_open;
	GLUIGridContainerRef grid_container;
	GLUIButtonRef collapse_button;
	GLUIButtonRef expand_button;
	GLUITextButtonRef emoji_button;
	GLUIWindowRef emoji_window;
	GLUIGridContainerRef emoji_grid_container;
	Vec2i last_viewport_dims;
	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;
	
	float draw_area_bottom_left_y;

	GLUILineEditRef chat_line_edit;

	size_t current_emoji_category;
	float emoji_picker_scroll_offset;
	float emoji_picker_max_scroll_offset;
	Rect2f emoji_picker_scroll_rect;
	Rect2f emoji_picker_category_rect;
	std::vector<GLUITextButtonRef> emoji_category_buttons;
	std::vector<GLUITextButtonRef> emoji_picker_buttons;
};
