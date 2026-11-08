/*=====================================================================
ThreadMessages.h
----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <ThreadMessage.h>
#include "../shared/UID.h"
#include <string>
#include <vector>


enum GuiClientThreadMessages
{
	Msg_ModelLoadedThreadMessage,
	Msg_GaussianSplatLoadedThreadMessage,
	Msg_TextureLoadedThreadMessage,
	Msg_AudioLoadedThreadMessage,
	Msg_ScriptLoadedThreadMessage,
	Msg_ClientConnectingToServerMessage,
	Msg_ClientConnectedToServerMessage,
	Msg_AudioStreamToServerStartedMessage,
	Msg_AudioStreamToServerEndedMessage,
	Msg_RemoteClientAudioStreamToServerStarted,
	Msg_RemoteClientAudioStreamToServerEnded,
	Msg_ClientProtocolTooOldMessage,
	Msg_ClientDisconnectedFromServerMessage,
	Msg_AvatarIsHereMessage,
	Msg_AvatarCreatedMessage,
	Msg_OurAvatarFullUpdateMessage,
	Msg_AvatarPerformGestureMessage,
	Msg_AvatarStopGestureMessage,
	Msg_ChatMessage,
	Msg_InfoMessage,
	Msg_ErrorMessage,
	Msg_LogMessage,
	Msg_LoggedInMessage,
	Msg_LoggedOutMessage,
	Msg_GuestModeMessage,
	Msg_SignedUpMessage,
	Msg_ServerAdminMessage,
	Msg_WorldSettingsReceivedMessage,
	Msg_WorldDetailsReceivedMessage,
	Msg_MapTilesResultReceivedMessage,
	Msg_UserSelectedObjectMessage,
	Msg_UserDeselectedObjectMessage,
	Msg_GetFileMessage,
	Msg_NewResourceOnServerMessage,
	Msg_ResourceDownloadedMessage,
	Msg_ResourceDownloadFailedMessage,
	Msg_TerrainChunkGeneratedMsg,
	Msg_WindNoiseLoaded,
	Msg_UserGearListMessage,
	Msg_ChatBotCreatedMessage,
	Msg_UserBotListMessage,
	Msg_BotConversationLogMessage,
	Msg_BotPlayerMemoryListMessage,
	Msg_AdminWorldEntryMessage,
	Msg_GameHUDUpdateMessage,
	Msg_GameHUDMenuUpdateMessage,
	Msg_ChatBotDialogChoicesMessage,
	Msg_GameStateUpdateMessage,
	Msg_GameEventMessage,
	Msg_ScriptInteractionPromptMessage,
	Msg_AvatarMaterialiseEffectMessage,
	Msg_TextureUploadedMessage = 1000, // Should match the values from <opengl/OpenGLUploadThread.h>
	Msg_AnimatedTextureUpdated = 1001,
	Msg_GeometryUploadedMessage = 1002,
	Msg_OpenGLUploadErrorMessage = 1003,
};

class ScriptInteractionPromptMessage : public ThreadMessage
{
public:
	ScriptInteractionPromptMessage() : ThreadMessage(Msg_ScriptInteractionPromptMessage) {}
	UID object_uid;
	uint32 input_mode = 0;
	uint32 key_code = 0;
	bool visible = false;
	std::string text;
};

class AvatarMaterialiseEffectMessage : public ThreadMessage
{
public:
	AvatarMaterialiseEffectMessage() : ThreadMessage(Msg_AvatarMaterialiseEffectMessage) {}
	UID avatar_uid;
};

struct ChatBotDialogOptionUI
{
	uint32 choice_index = 0;
	std::string label;
};

class ChatBotDialogChoicesMessage : public ThreadMessage
{
public:
	ChatBotDialogChoicesMessage() : ThreadMessage(Msg_ChatBotDialogChoicesMessage), node_id(0) {}
	UID bot_avatar_uid;
	std::string bot_name;
	uint32 node_id;
	std::vector<ChatBotDialogOptionUI> options;
};


class LogMessage : public ThreadMessage
{
public:
	LogMessage(const std::string& msg_) : ThreadMessage(Msg_LogMessage), msg(msg_) {}
	std::string msg;
};


class InfoMessage : public ThreadMessage
{
public:
	InfoMessage(const std::string& msg_) : ThreadMessage(Msg_InfoMessage), msg(msg_) {}
	std::string msg;
};

class GameHUDUpdateMessage : public ThreadMessage
{
public:
	GameHUDUpdateMessage(const UID& game_object_uid_, bool visible_, const std::string& text_, bool has_layout_ = false, float center_x_ = 0.f, float top_y_ = 0.55f)
	: ThreadMessage(Msg_GameHUDUpdateMessage), game_object_uid(game_object_uid_), visible(visible_), text(text_), has_layout(has_layout_), center_x(center_x_), top_y(top_y_) {}
	UID game_object_uid;
	bool visible;
	std::string text;
	bool has_layout;
	float center_x;
	float top_y;
};

struct GameHUDAction
{
	std::string id;
	std::string label;
	bool has_layout = false;
	float center_x = 0.f;
	float top_y = 0.f;
	float width = 0.f;
	float height = 0.f;
};

struct GameHUDFormField
{
	enum Type { Text, Select, Number, Checkbox };
	std::string id;
	std::string label;
	bool required = false;
	Type type = Text;
	std::vector<std::string> options;
	double minimum = 0.0;
	double maximum = 0.0;
	bool checked = false;
};

struct GameHUDProgressBar
{
	std::string id;
	std::string label;
	float value = 0.f;
	float maximum = 1.f;
};

struct GameHUDTextWidget
{
	std::string id;
	std::string text;
	float center_x = 0.f;
	float top_y = 0.f;
	float width = 0.f;
	float text_r = 0.92f, text_g = 0.92f, text_b = 0.95f;
	float background_r = 0.f, background_g = 0.f, background_b = 0.f, background_alpha = 0.f;
};

class GameHUDMenuUpdateMessage : public ThreadMessage
{
public:
	GameHUDMenuUpdateMessage(const UID& game_object_uid_, const std::string& panel_id_, bool visible_, const std::string& title_, const std::string& body_, const std::vector<GameHUDAction>& actions_, const std::vector<GameHUDFormField>& fields_, const std::vector<GameHUDProgressBar>& progress_bars_, const std::vector<GameHUDTextWidget>& text_widgets_, float center_x_, float top_y_, float width_)
	: ThreadMessage(Msg_GameHUDMenuUpdateMessage), game_object_uid(game_object_uid_), panel_id(panel_id_), visible(visible_), title(title_), body(body_), actions(actions_), fields(fields_), progress_bars(progress_bars_), text_widgets(text_widgets_), center_x(center_x_), top_y(top_y_), width(width_) {}
	UID game_object_uid;
	std::string panel_id;
	bool visible;
	std::string title;
	std::string body;
	std::vector<GameHUDAction> actions;
	std::vector<GameHUDFormField> fields;
	std::vector<GameHUDProgressBar> progress_bars;
	std::vector<GameHUDTextWidget> text_widgets;
	float center_x;
	float top_y;
	float width;
};

class GameStateUpdateMessage : public ThreadMessage
{
public:
	GameStateUpdateMessage() : ThreadMessage(Msg_GameStateUpdateMessage), version(0) {}
	UID game_object_uid;
	uint32 version;
	bool is_delta = false;
	uint32 base_version = 0;
	uint32 result_size = 0;
	uint32 prefix_size = 0;
	uint32 suffix_size = 0;
	std::vector<uint8> serialized_value;
};

class GameEventMessage : public ThreadMessage
{
public:
	GameEventMessage() : ThreadMessage(Msg_GameEventMessage), sequence(0), server_time(0) {}
	UID game_object_uid;
	uint32 sequence;
	double server_time;
	std::string name;
};


class ErrorMessage : public ThreadMessage
{
public:
	ErrorMessage(const std::string& msg_) : ThreadMessage(Msg_ErrorMessage), msg(msg_) {}
	std::string msg;
};
