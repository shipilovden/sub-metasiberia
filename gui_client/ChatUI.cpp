/* ChatUI — Copyright Glare Technologies Limited 2024 - */
#include "ChatUI.h"
#include "ChatOverlayButton.h"
#include "ChatOverlayLayout.h"
#include "GUIClient.h"
#include "EmojiUtils.h"
#include <settings/SettingsStore.h>
#include <utils/UTF8Utils.h>
#include <algorithm>

namespace {
std::string excerpt(const std::string& text,size_t bytes) {
	std::string result=UTF8Utils::sanitiseUTF8String(text);
	for(char& c:result) if(c=='\n'||c=='\r'||c=='\t') c=' ';
	if(result.size()>bytes) {
		while(bytes && (static_cast<unsigned char>(result[bytes])&0xc0)==0x80) --bytes;
		result.resize(bytes); result+="…";
	}
	return result;
}
}
ChatUI::ChatUI() {}
ChatUI::~ChatUI() {}
float ChatUI::px(float value) const { return gl_ui->getUIWidthForDevIndepPixelWidth(value); }
bool ChatUI::ready() const { return gl_ui.nonNull() && chat_line_edit.nonNull() && emoji_window.nonNull() && expand_button.nonNull() && emoji_hint.nonNull() && emoji_search_label.nonNull(); }
float ChatUI::messageWidth() const { return myMax(px(180),myMin(px(460),1.f-px(38))); }
ChatOverlayButtonRef ChatUI::button(const std::string& label,int font,const std::string& tooltip) {
	ChatOverlayButtonRef b=new ChatOverlayButton(*gl_ui,opengl_engine,label,font,tooltip);
	b->handler=this; gl_ui->addWidget(b); return b;
}
void ChatUI::create(Reference<OpenGLEngine>& engine,GUIClient* client,GLUIRef ui) {
	opengl_engine=engine; gui_client=client; gl_ui=ui;
#if EMSCRIPTEN
	const bool default_expanded=false;
#else
	const bool default_expanded=true;
#endif
	expanded=client->getSettingsStore()->getBoolValue("setting/show_chat",default_expanded); visible=true;
	try {
		GLUIGridContainer::CreateArgs bg; bg.background_colour=toLinearSRGB(Colour3f(.14f,.13f,.16f));
		bg.background_alpha=.82f; bg.background_consumes_events=true;
		chat_background=new GLUIGridContainer(*ui,engine,bg);
		chat_background->sizing_type_x=chat_background->sizing_type_y=GLUIWidget::SizingType_FixedSizeUICoords;
		gl_ui->addWidget(chat_background);
		GLUILineEdit::CreateArgs input; input.width=messageWidth(); input.font_size_px=14;
		input.background_colour=toLinearSRGB(Colour3f(.16f,.15f,.18f)); input.background_alpha=1;
		input.text_colour=toLinearSRGB(Colour3f(.94f)); input.rounded_corner_radius_px=8; input.z=-.04f;
		input.tooltip="Сообщение в общий чат · Enter — отправить";
		chat_line_edit=new GLUILineEdit(*ui,engine,Vec2f(0),input);
		chat_line_edit->on_enter_pressed=[this](){submit();}; gl_ui->addWidget(chat_line_edit);
		emoji_button=button(EmojiUtils::pickerButtonLabel(),21,"Эмодзи — вставить в сообщение");
		send_button=button("↑",20,"Отправить сообщение (Enter)");
		collapse_button=button("−",16,"Свернуть чат");
		GLUIButton::CreateArgs expand_args; expand_args.tooltip="Открыть общий чат";
		expand_button=new GLUIButton(*ui,engine,client->resources_dir_path+"/buttons/expand_chat_icon.png",expand_args);
		expand_button->handler=this; gl_ui->addWidget(expand_button);
		new_messages_button=button("К новым ↓",12,"Вернуться к последним сообщениям");
		reply_cancel=button("×",16,"Отменить ответ с цитатой");
		GLUITextView::CreateArgs text; text.font_size_px=12; text.background_alpha=0; text.z=-.04f;
		text.text_colour=toLinearSRGB(Colour3f(.76f,.72f,.9f)); text.max_width=messageWidth()-px(58); text.text_selectable=false;
		reply_hint=new GLUITextView(*ui,engine,"",Vec2f(0),text); gl_ui->addWidget(reply_hint);
		GLUIWindow::CreateArgs window; window.title="Эмодзи"; window.background_colour=toLinearSRGB(Colour3f(.125f,.12f,.15f));
		window.background_alpha=.98f; window.background_consumes_events=true; window.z=-.3f;
		emoji_window=new GLUIWindow(*ui,engine,window); emoji_window->handler=this; gl_ui->addWidget(emoji_window);
		input.width=px(400); input.z=-.34f; input.tooltip="Поиск по названию, категории или эмодзи (русский / English)";
		emoji_search=new GLUILineEdit(*ui,engine,Vec2f(0),input); gl_ui->addWidget(emoji_search);
		text.z=-.341f; text.max_width=px(540); text.text_colour=toLinearSRGB(Colour3f(.65f));
		emoji_search_label=new GLUITextView(*ui,engine,"Поиск эмодзи…",Vec2f(0),text); gl_ui->addWidget(emoji_search_label);
		emoji_hint=new GLUITextView(*ui,engine,"",Vec2f(0),text); gl_ui->addWidget(emoji_hint);
		rebuildEmojiPickerContents(); updateWidgetTransforms();
	} catch(glare::Exception& e) { conPrint("Chat UI: "+e.what()); }
}
void ChatUI::removeMessageWidgets(ChatMessage& msg) {
	checkRemoveAndDeleteWidget(gl_ui,msg.body); checkRemoveAndDeleteWidget(gl_ui,msg.author); checkRemoveAndDeleteWidget(gl_ui,msg.reply);
}
void ChatUI::destroy() {
	if(gl_ui.isNull()) return;
	for(auto& msg:messages) removeMessageWidgets(msg);
	messages.clear();
	for(auto& b:emoji_picker_buttons) checkRemoveAndDeleteWidget(gl_ui,b);
	for(auto& b:emoji_category_buttons) checkRemoveAndDeleteWidget(gl_ui,b);
	emoji_picker_buttons.clear(); emoji_category_buttons.clear();
	checkRemoveAndDeleteWidget(gl_ui,chat_background); checkRemoveAndDeleteWidget(gl_ui,chat_line_edit);
	checkRemoveAndDeleteWidget(gl_ui,emoji_search); checkRemoveAndDeleteWidget(gl_ui,emoji_hint);
	checkRemoveAndDeleteWidget(gl_ui,emoji_search_label);
	checkRemoveAndDeleteWidget(gl_ui,reply_hint); checkRemoveAndDeleteWidget(gl_ui,emoji_window);
	checkRemoveAndDeleteWidget(gl_ui,emoji_button); checkRemoveAndDeleteWidget(gl_ui,send_button);
	checkRemoveAndDeleteWidget(gl_ui,collapse_button); checkRemoveAndDeleteWidget(gl_ui,expand_button);
	checkRemoveAndDeleteWidget(gl_ui,new_messages_button); checkRemoveAndDeleteWidget(gl_ui,reply_cancel);
	reply_author.clear(); reply_text.clear(); last_search.clear(); history_scroll=emoji_scroll=0;
	read_state.viewedLatest();
	emoji_picker_open=focus_composer=false; gl_ui=nullptr; opengl_engine=nullptr;
}
void ChatUI::rebuildEmojiPickerContents() {
	for(auto& b:emoji_category_buttons) checkRemoveAndDeleteWidget(gl_ui,b);
	for(auto& b:emoji_picker_buttons) checkRemoveAndDeleteWidget(gl_ui,b);
	emoji_category_buttons.clear(); emoji_picker_buttons.clear();
	const auto categories=EmojiUtils::buildPickerCategories(gui_client->getRecentEmojiHistory());
	const auto layout=ChatOverlayLayout::picker(2/px(1),2*gl_ui->getViewportMinMaxY()/px(1),int(categories.size()));
	compact_categories=layout.compact;
	const char* labels[]={"Недавние","Лица","Эмоции","Жесты","Эффекты","Животные","Сердца","Еда","Досуг"};
	const char* icons[]={"◷","☺","!","✋","★","🐱","♥","☕","⚽"};
	current_emoji_category=myMin(current_emoji_category,categories.size()-1);
	for(size_t i=0;i<categories.size();++i) {
		auto b=button(i<9?(compact_categories?icons[i]:labels[i]):categories[i].title,compact_categories?17:12,categories[i].title); b->setZ(-.34f);
		b->setToggled(i==current_emoji_category && last_search.empty()); emoji_category_buttons.push_back(b);
	}
	const auto emojis=last_search.empty()?categories[current_emoji_category].emojis:EmojiUtils::searchEmoji(last_search);
	for(const auto& emoji:emojis) {
		auto b=button(emoji,26,std::string(EmojiUtils::emojiDisplayName(emoji))+" · вставить в сообщение");
		b->client_data=emoji; b->setZ(-.34f); emoji_picker_buttons.push_back(b);
	}
	emoji_hint->setText(*gl_ui,emojis.empty()?"Ничего нет. Выберите категорию или измените поиск.":
		std::to_string(emojis.size())+" эмодзи · выбор вставляет в текст · Enter отправляет");
}
void ChatUI::recreateMessage(ChatMessage& msg) {
	removeMessageWidgets(msg);
	GLUITextView::CreateArgs a; a.font_size_px=13; a.padding_px=0; a.background_alpha=0; a.z=-.025f;
	a.max_width=messageWidth()-px(32); a.text_colour=toLinearSRGB(Colour3f(.94f));
	msg.body=new GLUITextView(*gl_ui,opengl_engine,UTF8Utils::sanitiseUTF8String(msg.text),Vec2f(0),a); gl_ui->addWidget(msg.body);
	if(msg.user_message) {
		a.font_size_px=12; a.text_colour=Colour3f(myMax(.35f,msg.colour.r),myMax(.35f,msg.colour.g),myMax(.35f,msg.colour.b)); a.max_width=px(2000);
		msg.author=new GLUITextView(*gl_ui,opengl_engine,excerpt(msg.name,90),Vec2f(0),a); gl_ui->addWidget(msg.author);
		msg.reply=button("Ответ",11,"Ответить с цитатой. Цитата отправляется обычным текстом всем участникам.");
	}
}
void ChatUI::appendMessage(const std::string& name,const Colour3f& colour,const std::string& text,bool user_message) {
	if(!ready()) return;
	read_state.appended(user_message,visible && expanded && !emoji_picker_open && history_scroll==0);
	ChatMessage msg; msg.name=name; msg.colour=colour; msg.user_message=user_message;
	msg.text=user_message?text:name+text; messages.push_back(msg); recreateMessage(messages.back());
	if(history_scroll>0) history_scroll+=messages.back().body->getRect().getWidths().y+px(user_message?42:24);
	if(messages.size()>100) {removeMessageWidgets(messages.front());messages.pop_front();}
	updateWidgetTransforms();
}
void ChatUI::updateWidgetTransforms() {
	if(!ready()) return;
	const float width=messageWidth();
	const float left=-1+px(18),bottom=myMax(-gl_ui->getViewportMinMaxY()+px(12),draw_area_bottom_left_y+px(12));
	const float input_y=bottom+(reply_text.empty()?0:px(44));
	chat_line_edit->setPos(Vec2f(left,input_y)); chat_line_edit->setWidth(myMax(px(80),width-px(94)));
	emoji_button->setPosAndDims(Vec2f(left+width-px(86),input_y),Vec2f(px(38)));
	send_button->setPosAndDims(Vec2f(left+width-px(42),input_y),Vec2f(px(38)));
	reply_hint->setPos(Vec2f(left+px(8),bottom+px(22))); reply_cancel->setPosAndDims(Vec2f(left+width-px(36),bottom),Vec2f(px(32)));
	const float history_bottom=input_y+px(46);
	const float height=myMax(px(45),myMin(px(330),gl_ui->getViewportMinMaxY()-history_bottom-px(46)));
	history_rect=Rect2f(Vec2f(left,history_bottom),Vec2f(left+width,history_bottom+height));
	float total=px(12); for(auto& msg:messages) total+=msg.body->getRect().getWidths().y+px(msg.user_message?42:24);
	history_max_scroll=myMax(0.f,total-height); history_scroll=myClamp(history_scroll,0.f,history_max_scroll);
	float y=history_bottom+px(12)-history_scroll;
	for(auto it=messages.rbegin();it!=messages.rend();++it) {
		auto& msg=*it; const float body_h=msg.body->getRect().getWidths().y;
		msg.body->setPos(Vec2f(0)); const auto bounds=msg.body->getRect();
		msg.body->setPos(Vec2f(left+px(12),y)-bounds.getMin()); msg.body->setClipRegion(history_rect);
		if(msg.author.nonNull()) {
			msg.author->setPos(Vec2f(left+px(12),y+body_h+px(13)));
			msg.author->setClipRegion(Rect2f(history_rect.getMin(),Vec2f(left+width-px(76),history_rect.getMax().y)));
			msg.reply->setPosAndDims(Vec2f(left+width-px(66),y+body_h+px(4)),Vec2f(px(54),px(25))); msg.reply->setClipRegion(history_rect);
		}
		y+=body_h+px(msg.user_message?42:24);
	}
	const float shown_height=myMin(height,total);
	chat_background->setPosAndDims(history_rect.getMin(),Vec2f(width,shown_height));
	collapse_button->setPosAndDims(Vec2f(left,history_bottom+shown_height+px(4)),Vec2f(px(30),px(25)));
	new_messages_button->setPosAndDims(expanded?Vec2f(left+width-px(128),history_bottom+height+px(4)):Vec2f(left+px(44),bottom+px(5)),Vec2f(px(128),px(27)));
	expand_button->setPosAndDims(Vec2f(left,bottom),Vec2f(px(36)));
	const auto picker=ChatOverlayLayout::picker(2/px(1),2*gl_ui->getViewportMinMaxY()/px(1),int(emoji_category_buttons.size()));
	const float win_w=px(picker.width),win_h=px(picker.height);
	const float wx=myClamp(left+width+px(12),-1+px(12),1-px(12)-win_w);
	const float wy=myClamp(input_y+px(50),-gl_ui->getViewportMinMaxY()+px(12),gl_ui->getViewportMinMaxY()-px(12)-win_h);
	emoji_window->setPosAndDims(Vec2f(wx,wy),Vec2f(win_w,win_h));
	const float content_w=win_w-px(24),search_y=wy+win_h-px(72);
	emoji_search->setPos(Vec2f(wx+px(12),search_y)); emoji_search->setWidth(content_w);
	emoji_search_label->setPos(Vec2f(wx+px(22),search_y+px(12)));
	const int cols=picker.tab_columns;
	const float tab_w=(content_w-px(6)*(cols-1))/cols;
	const int rows=(int(emoji_category_buttons.size())+cols-1)/cols;
	for(size_t i=0;i<emoji_category_buttons.size();++i)
		emoji_category_buttons[i]->setPosAndDims(Vec2f(wx+px(12)+(i%cols)*(tab_w+px(6)),search_y-px(38)-int(i/cols)*px(34)),Vec2f(tab_w,px(28)));
	emoji_scroll_rect=Rect2f(Vec2f(wx+px(12),wy+px(38)),Vec2f(wx+win_w-px(12),search_y-px(14)-rows*px(34)));
	const int tile_cols=myMax(1,int(content_w/px(50)));
	const float tile_gap=(content_w-px(44)*tile_cols)/tile_cols;
	const int tile_rows=(int(emoji_picker_buttons.size())+tile_cols-1)/tile_cols;
	emoji_max_scroll=myMax(0.f,tile_rows*px(50)-emoji_scroll_rect.getWidths().y);
	emoji_scroll=myClamp(emoji_scroll,0.f,emoji_max_scroll);
	for(size_t i=0;i<emoji_picker_buttons.size();++i) {
		auto& b=emoji_picker_buttons[i];
		b->setPosAndDims(Vec2f(wx+px(12)+tile_gap*.5f+(i%tile_cols)*(px(44)+tile_gap),emoji_scroll_rect.getMax().y-px(44)-int(i/tile_cols)*px(50)+emoji_scroll),Vec2f(px(44)));
		b->setClipRegion(emoji_scroll_rect);
	}
	emoji_hint->setPos(Vec2f(wx+px(12),wy+px(14)));
	emoji_hint->setClipRegion(Rect2f(Vec2f(wx+px(10),wy+px(4)),Vec2f(wx+win_w-px(10),wy+px(32))));
	updateVisibility();
}
void ChatUI::updateVisibility() {
	if(!ready()) return;
	const bool show=expanded && visible,picker=show && emoji_picker_open;
	if(show && !picker && history_scroll==0) read_state.viewedLatest();
	const std::string unread=read_state.unread>=100?"99+":std::to_string(read_state.unread);
	new_messages_button->setText(read_state.unread?("Новые: "+unread+" ↓"):"К новым ↓");
	const auto focus=gl_ui->getKeyboardFocusWidget();
	if((!show && focus==chat_line_edit) || (!picker && focus==emoji_search)) gl_ui->setKeyboardFocusWidget(nullptr);
	chat_background->setVisible(show && !messages.empty()); chat_line_edit->setVisible(show);
	emoji_button->setVisible(show); send_button->setVisible(show); collapse_button->setVisible(show);
	expand_button->setVisible(visible && !expanded);
	new_messages_button->setVisible(visible && ((expanded && history_scroll>0) || read_state.unread>0));
	reply_hint->setVisible(show && !reply_text.empty()); reply_cancel->setVisible(show && !reply_text.empty());
	for(auto& m:messages) {
		const auto r=m.body->getRect(); const bool in_view=r.getMax().y>=history_rect.getMin().y-px(30) && r.getMin().y<history_rect.getMax().y;
		m.body->setVisible(show && in_view);
		if(m.author.nonNull()) {m.author->setVisible(show && in_view);m.reply->setVisible(show && in_view);}
	}
	emoji_window->setVisible(picker); emoji_search->setVisible(picker); emoji_hint->setVisible(picker);
	emoji_search_label->setVisible(picker && emoji_search->getText().empty());
	for(auto& b:emoji_category_buttons) b->setVisible(picker);
	for(auto& b:emoji_picker_buttons)
		b->setVisible(picker && ChatOverlayLayout::visibleRow(b->getRect().getMin().y,b->getRect().getMax().y,emoji_scroll_rect.getMin().y,emoji_scroll_rect.getMax().y));
}
void ChatUI::setVisible(bool value) {visible=value;updateVisibility();}
void ChatUI::setDrawAreaBottomLeftY(float y) {draw_area_bottom_left_y=y;}
void ChatUI::viewportResized(int,int) {
	if(!ready()) return;
	for(auto& m:messages) recreateMessage(m);
	const auto layout=ChatOverlayLayout::picker(2/px(1),2*gl_ui->getViewportMinMaxY()/px(1),int(emoji_category_buttons.size()));
	if(layout.compact!=compact_categories) rebuildEmojiPickerContents();
	updateWidgetTransforms();
}
void ChatUI::think() {
	if(!ready()) return;
	if(focus_composer) {focus_composer=false;if(visible && expanded) gl_ui->setKeyboardFocusWidget(chat_line_edit);}
	if(emoji_picker_open && emoji_search->getText()!=last_search) {
		last_search=emoji_search->getText(); emoji_scroll=0; rebuildEmojiPickerContents();updateWidgetTransforms();
	}
}
bool ChatUI::dismissPopup() {if(!emoji_picker_open) return false;setEmojiPickerOpen(false);return true;}
void ChatUI::setEmojiPickerOpen(bool open) {
	emoji_picker_open=open;
	if(open) {
		if(current_emoji_category==0 && gui_client->getRecentEmojiHistory().empty()) current_emoji_category=1;
		last_search.clear();emoji_search->clear();emoji_scroll=0;rebuildEmojiPickerContents();
	}
	else if(gl_ui->getKeyboardFocusWidget()==emoji_search) gl_ui->setKeyboardFocusWidget(nullptr);
	updateWidgetTransforms();
}
void ChatUI::handleMousePress(MouseEvent& event) {
	if(!ready() || !emoji_picker_open) return;
	const auto p=gl_ui->UICoordsForOpenGLCoords(event.gl_coords);
	if(!emoji_window->getRect().inClosedRectangle(p) && !emoji_button->getRect().inClosedRectangle(p)) setEmojiPickerOpen(false);
}
void ChatUI::handleMouseMoved(MouseEvent&) {}
void ChatUI::handleMouseWheelEvent(MouseWheelEvent& event) {
	if(!ready() || !visible || !expanded) return;
	const auto p=gl_ui->UICoordsForOpenGLCoords(event.gl_coords);
	if(emoji_picker_open && emoji_window->getRect().inClosedRectangle(p)) {
		if(emoji_scroll_rect.inClosedRectangle(p)) emoji_scroll=myClamp(emoji_scroll-px(event.angle_delta.y*.45f),0.f,emoji_max_scroll);
	} else if(history_rect.inClosedRectangle(p)) history_scroll=myClamp(history_scroll+px(event.angle_delta.y*.45f),0.f,history_max_scroll);
	else return;
	event.accepted=true;updateWidgetTransforms();
}
void ChatUI::insertEmoji(const std::string& emoji) {
	if(!EmojiUtils::isSupportedEmoji(emoji) || chat_line_edit->getText().size()+emoji.size()>8000) return;
	TextInputEvent input; input.text=emoji;
	chat_line_edit->doHandleTextInputEvent(input);
	gui_client->recordRecentEmojiUsage(emoji); focus_composer=true;
}
void ChatUI::submit() {
	const auto text=chat_line_edit->getText();
	if(text.find_first_not_of(" \r\n\t")==std::string::npos) return;
	if(gui_client->connection_state!=GUIClient::ServerConnectionState_Connected) {gui_client->showErrorNotification("Нет соединения. Текст оставлен в поле ввода.");return;}
	if(!gui_client->logged_in_user_id.valid()) {gui_client->showErrorNotification("Войдите в аккаунт, чтобы отправить сообщение. Черновик сохранён.");return;}
	std::string message=reply_text.empty()?text:("↪ "+reply_author+": «"+reply_text+"»\n"+text);
	if(message.size()>9000) {gui_client->showErrorNotification("Сообщение слишком длинное. Сократите текст.");return;}
	if(EmojiUtils::isSupportedEmoji(message)) gui_client->sendEmojiChatMessage(message); else gui_client->sendChatMessage(message);
	chat_line_edit->clear();reply_text.clear();reply_author.clear();history_scroll=0;updateWidgetTransforms();
}
void ChatUI::eventOccurred(GLUICallbackEvent& event) {
	if(!ready()) return;
	event.accepted=true;
	if(event.widget==emoji_button.ptr()) {setEmojiPickerOpen(!emoji_picker_open);return;}
	if(event.widget==send_button.ptr()) {submit();return;}
	if(event.widget==collapse_button.ptr()) {expanded=false;emoji_picker_open=false;}
	else if(event.widget==expand_button.ptr()) {expanded=true;focus_composer=true;}
	else if(event.widget==new_messages_button.ptr()) {expanded=true;history_scroll=0;read_state.viewedLatest();}
	else if(event.widget==reply_cancel.ptr()) {reply_text.clear();reply_author.clear();}
	else {
		for(size_t i=0;i<emoji_category_buttons.size();++i) if(event.widget==emoji_category_buttons[i].ptr()) {
			current_emoji_category=i;last_search.clear();emoji_search->clear();emoji_scroll=0;rebuildEmojiPickerContents();updateWidgetTransforms();return;
		}
		for(auto& b:emoji_picker_buttons) if(event.widget==b.ptr()) {insertEmoji(b->client_data);return;}
		for(auto& m:messages) if(event.widget==m.reply.ptr()) {
			reply_author=excerpt(m.name,90);reply_text=excerpt(m.text,240);
			reply_hint->setText(*gl_ui,"Ответ "+excerpt(reply_author,40)+": "+excerpt(reply_text,55));focus_composer=true;updateWidgetTransforms();return;
		}
	}
	gui_client->getSettingsStore()->setBoolValue("setting/show_chat",expanded);updateWidgetTransforms();
}
void ChatUI::closeWindowEventOccurred(GLUICallbackEvent& event) {
	if(event.widget==emoji_window.ptr()) {setEmojiPickerOpen(false);event.accepted=true;}
}
