/* ChatUI — Copyright Glare Technologies Limited 2024 - */
#pragma once
#include "ClientThread.h"
#include "ChatReadState.h"
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIWindow.h>
#include <opengl/ui/GLUILineEdit.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <list>
#include <vector>

class GUIClient;
class ChatOverlayButton;
typedef Reference<ChatOverlayButton> ChatOverlayButtonRef;
class ChatUI : public GLUICallbackHandler
{
public:
	ChatUI();
	~ChatUI();
	void create(Reference<OpenGLEngine>& engine, GUIClient* client, GLUIRef ui);
	void destroy();
	void setVisible(bool visible);
	void appendMessage(const std::string& name, const Colour3f& colour, const std::string& msg, bool user_message=false);
	void viewportResized(int w,int h);
	void setDrawAreaBottomLeftY(float y);
	void think();
	bool dismissPopup();
	void handleMousePress(MouseEvent& event);
	void handleMouseWheelEvent(MouseWheelEvent& event);
	void handleMouseMoved(MouseEvent& event);
	void eventOccurred(GLUICallbackEvent& event) override;
	void closeWindowEventOccurred(GLUICallbackEvent& event) override;
private:
	struct ChatMessage {
		std::string name,text;
		Colour3f colour;
		bool user_message=false;
		GLUITextViewRef body,author;
		ChatOverlayButtonRef reply;
	};
	bool ready() const;
	float px(float value) const;
	float messageWidth() const;
	void updateWidgetTransforms();
	void updateVisibility();
	void rebuildEmojiPickerContents();
	void recreateMessage(ChatMessage& msg);
	void removeMessageWidgets(ChatMessage& msg);
	void setEmojiPickerOpen(bool open);
	void submit();
	void insertEmoji(const std::string& emoji);
	ChatOverlayButtonRef button(const std::string& label,int font,const std::string& tooltip);
	std::list<ChatMessage> messages;
	GUIClient* gui_client=nullptr;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;
	GLUIGridContainerRef chat_background;
	GLUILineEditRef chat_line_edit,emoji_search;
	GLUIWindowRef emoji_window;
	GLUITextViewRef emoji_hint,reply_hint,emoji_search_label;
	ChatOverlayButtonRef emoji_button,send_button,collapse_button,new_messages_button,reply_cancel;
	GLUIButtonRef expand_button;
	ChatReadState read_state;
	std::vector<ChatOverlayButtonRef> emoji_category_buttons,emoji_picker_buttons;
	std::string last_search,reply_author,reply_text;
	size_t current_emoji_category=0;
	float draw_area_bottom_left_y=-1;
	float emoji_scroll=0,emoji_max_scroll=0,history_scroll=0,history_max_scroll=0;
	Rect2f emoji_scroll_rect,history_rect;
	bool expanded=true,visible=true,emoji_picker_open=false,focus_composer=false;
	bool compact_categories=false;
};
