/*=====================================================================
ClientThread.h
--------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "ThreadMessages.h"
#include "../shared/WorldSettings.h"
#include "../shared/UID.h"
#include "../shared/UserID.h"
#include "../shared/Avatar.h"
#include "../shared/WorldDetails.h"
#include "../shared/GestureSettings.h"
#include "../server/ChatBot.h"
#include <networking/IPAddress.h>
#include <maths/vec3.h>
#include <utils/MessageableThread.h>
#include <utils/Platform.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/EventFD.h>
#include <utils/ThreadManager.h>
#include <utils/Vector.h>
#include <utils/BufferInStream.h>
#include <utils/ArrayRef.h>
#include <string>
class ClientSenderThread;
class WorldState;
class WorldObject;
class SocketInterface;
struct tls_config;
namespace glare { class FastPoolAllocator; }
struct ZSTD_DCtx_s;


class ChatMessage : public ThreadMessage
{
public:
	ChatMessage(const std::string& name_, const std::string& msg_, UID sender_avatar_uid_) : ThreadMessage(Msg_ChatMessage), name(name_), msg(msg_), sender_avatar_uid(sender_avatar_uid_), private_message(false), outgoing_private_message(false) {}
	ChatMessage(const std::string& name_, const std::string& recipient_name_, const std::string& msg_, UID sender_avatar_uid_, bool outgoing_private_message_) : ThreadMessage(Msg_ChatMessage), name(name_), recipient_name(recipient_name_), msg(msg_), sender_avatar_uid(sender_avatar_uid_), private_message(true), outgoing_private_message(outgoing_private_message_) {}
	std::string name, recipient_name, msg;
	UID sender_avatar_uid;
	bool private_message;
	bool outgoing_private_message;
};


class AvatarPerformGestureMessage : public ThreadMessage
{
public:
	AvatarPerformGestureMessage(const UID avatar_uid_, const std::string& gesture_name_) : ThreadMessage(Msg_AvatarPerformGestureMessage), avatar_uid(avatar_uid_), gesture_name(gesture_name_) {}
	UID avatar_uid;
	std::string gesture_name;
	URLString gesture_URL;
	uint32 flags;
	double start_global_time;
};


class AvatarStopGestureMessage : public ThreadMessage
{
public:
	AvatarStopGestureMessage(const UID avatar_uid_) : ThreadMessage(Msg_AvatarStopGestureMessage), avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


// When the server wants a file from the client, it will send the client a GetFile protocol message.  The ClientThread will send this 'GetFileMessage' back to MainWindow.
class GetFileMessage : public ThreadMessage
{
public:
	GetFileMessage(const URLString& URL_) : ThreadMessage(Msg_GetFileMessage), URL(URL_) {}
	URLString URL;
};


// When the server has file uploaded to it, it will send a message to clients, so they can download it.
class NewResourceOnServerMessage : public ThreadMessage
{
public:
	NewResourceOnServerMessage(const URLString& URL_) : ThreadMessage(Msg_NewResourceOnServerMessage), URL(URL_) {}
	URLString URL;
};


class AvatarCreatedMessage : public ThreadMessage
{
public:
	AvatarCreatedMessage(const UID& avatar_uid_) : ThreadMessage(Msg_AvatarCreatedMessage), avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


class AvatarIsHereMessage : public ThreadMessage
{
public:
	AvatarIsHereMessage(const UID& avatar_uid_) : ThreadMessage(Msg_AvatarIsHereMessage), avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


class RemoteClientAudioStreamToServerStarted : public ThreadMessage
{
public:
	RemoteClientAudioStreamToServerStarted(UID avatar_uid_, const uint32 sampling_rate_, uint32 flags_, uint32 stream_id_) : ThreadMessage(Msg_RemoteClientAudioStreamToServerStarted), avatar_uid(avatar_uid_), sampling_rate(sampling_rate_), flags(flags_), stream_id(stream_id_) {}
	UID avatar_uid;
	uint32 sampling_rate;
	uint32 flags;
	uint32 stream_id;
};


class RemoteClientAudioStreamToServerEnded : public ThreadMessage
{
public:
	RemoteClientAudioStreamToServerEnded(UID avatar_uid_) : ThreadMessage(Msg_RemoteClientAudioStreamToServerEnded), avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


class UserSelectedObjectMessage : public ThreadMessage
{
public:
	UserSelectedObjectMessage(const UID& avatar_uid_, const UID& object_uid_) : ThreadMessage(Msg_UserSelectedObjectMessage), avatar_uid(avatar_uid_), object_uid(object_uid_) {}
	UID avatar_uid, object_uid;
};


class UserDeselectedObjectMessage : public ThreadMessage
{
public:
	UserDeselectedObjectMessage(const UID& avatar_uid_, const UID& object_uid_) : ThreadMessage(Msg_UserDeselectedObjectMessage), avatar_uid(avatar_uid_), object_uid(object_uid_) {}
	UID avatar_uid, object_uid;
};


class ClientConnectedToServerMessage : public ThreadMessage
{
public:
	ClientConnectedToServerMessage(const UID client_avatar_uid_, uint32 server_protocol_version_, uint32 server_capabilities_, int server_mesh_optimisation_version_) : 
		ThreadMessage(Msg_ClientConnectedToServerMessage), client_avatar_uid(client_avatar_uid_), server_protocol_version(server_protocol_version_), server_capabilities(server_capabilities_), server_mesh_optimisation_version(server_mesh_optimisation_version_) {}
	UID client_avatar_uid;
	uint32 server_protocol_version;
	uint32 server_capabilities;
	int server_mesh_optimisation_version;
};


class ClientConnectingToServerMessage : public ThreadMessage
{
public:
	ClientConnectingToServerMessage(const IPAddress& server_ip_) : ThreadMessage(Msg_ClientConnectingToServerMessage), server_ip(server_ip_) {}
	IPAddress server_ip;
};


class ClientProtocolTooOldMessage : public ThreadMessage
{
public:
	ClientProtocolTooOldMessage() : ThreadMessage(Msg_ClientProtocolTooOldMessage) {}
};


class ClientDisconnectedFromServerMessage : public ThreadMessage
{
public:
	ClientDisconnectedFromServerMessage() : ThreadMessage(Msg_ClientDisconnectedFromServerMessage), closed_gracefully(true) {}
	ClientDisconnectedFromServerMessage(const std::string& error_message_, bool closed_gracefully_) : ThreadMessage(Msg_ClientDisconnectedFromServerMessage), error_message(error_message_), closed_gracefully(closed_gracefully_) {}
	std::string error_message;
	bool closed_gracefully;
};


class LoggedInMessage : public ThreadMessage
{
public:
	LoggedInMessage(UserID user_id_, const std::string& username_) : ThreadMessage(Msg_LoggedInMessage), user_id(user_id_), username(username_), user_flags(0) {}
	UserID user_id;
	std::string username;
	AvatarSettings avatar_settings;
	uint32 user_flags;
	GestureSettings gesture_settings;
	GearItems equipped_gear;
};


class LoggedOutMessage : public ThreadMessage
{
public:
	LoggedOutMessage() : ThreadMessage(Msg_LoggedOutMessage) {}
};


class UserGearListMessage : public ThreadMessage
{
public:
	UserGearListMessage() : ThreadMessage(Msg_UserGearListMessage) {}
	GearItems all_gear;
};


class ChatBotCreatedMessage : public ThreadMessage
{
public:
	ChatBotCreatedMessage() : ThreadMessage(Msg_ChatBotCreatedMessage), bot_id(0) {}
	uint64 bot_id;
	UID    avatar_uid;
	std::string name;
	std::string prompt;
	AvatarSettings avatar_settings;
	Vec3d pos;
	float heading = 0.f;
	std::string greeting_name;
	std::string greeting_url;
	float greeting_cooldown = 0.f;
	std::string idle_name;
	std::string idle_url;
	float idle_interval = 0.f;
	std::string reactive_name;
	std::string reactive_url;
	float reactive_cooldown = 0.f;
	uint32 flags = 0;
	float greeting_distance = 6.f;
	float farewell_distance = 10.f;
	float chat_radius = 8.f;
	Vec3f model_scale = Vec3f(1.f);
	std::string ai_model_id;
	std::string ai_personality_preset = "assistant";
	std::string ai_knowledge;
	float ai_temperature = 0.7f;
	uint32 ai_max_tokens = 0;
	std::string audio_url;
	float audio_volume = 1.f;
	float audio_radius = 10.f;
	float audio_activation_distance = 12.f;
	float audio_cooldown = 0.f;
	uint32 trigger_flags = 3;
	std::string trigger_keywords;
	float trigger_cooldown = 3.f;
	uint32 greeting_gesture_flags = 0;
	uint32 idle_gesture_flags = 0;
	uint32 reactive_gesture_flags = 0;
	std::string fallback_message;
	std::string surprise_name;
	std::string surprise_url;
	uint32 surprise_flags = 0;
	float  surprise_cooldown = 15.f;
	std::string acknowledge_name;
	std::string acknowledge_url;
	uint32 acknowledge_flags = 0;
	float  acknowledge_cooldown = 10.f;
	uint32 use_action_type = 0;
	std::string use_action_param;
	std::string api_key;
	std::string api_endpoint;
	// Block 5
	uint32 movement_type = 0;
	float  walk_speed    = 1.4f;
	float  wander_radius = 5.f;
	std::vector<BotWaypoint>  waypoints;
	std::vector<BotUseAction> use_actions;
	// Block 6
	std::string farewell_gesture_name;
	std::string farewell_gesture_url;
	uint32      farewell_gesture_flags    = 0;
	float       farewell_gesture_cooldown = 8.f;
	std::string walk_gesture_name;
	std::string walk_gesture_url;
	uint32      walk_gesture_flags        = 0;
	std::string talk_gesture_name;
	std::string talk_gesture_url;
	uint32      talk_gesture_flags        = 0;
	std::string interaction_gesture_name;
	std::string interaction_gesture_url;
	uint32      interaction_gesture_flags    = 0;
	float       interaction_gesture_cooldown = 3.f;
	// Block 7
	float       audio_min_distance   = 1.f;
	float       audio_start_delay    = 0.f;
	std::string greeting_audio_url;
	std::string farewell_audio_url2;
	std::string interaction_audio_url;
	// Block 9
	float       conversation_timeout_s  = 0.f;
	uint32      max_llm_calls_per_hour  = 0;
	std::string webhook_url;
	uint32      active_hours_start_utc  = 8;
	uint32      active_hours_end_utc    = 22;
	std::vector<BotScriptedResponse>  scripted_responses;
	std::vector<std::string>          player_whitelist;
	std::vector<std::string>          player_blacklist;
	std::vector<BotToolFunctionInfo>  tool_functions;
	// Block 10
	uint32 ai_provider           = 0;
	float  top_p                 = 0.f;
	uint32 top_k                 = 0;
	float  frequency_penalty     = 0.f;
	float  presence_penalty      = 0.f;
	uint32 max_context_messages  = 0;
	uint32 dialog_start_node_id  = 0;
	std::vector<BotDialogNode>   dialog_nodes;
	// Block 11
	bool     enable_player_memory  = false;
	uint32   memory_summary_tokens = 150;
	std::string content_filter_patterns;
	bool     jailbreak_guard       = true;
	// Block 12
	uint32   max_llm_calls_per_player_per_hour = 0;
	bool     response_cache_enabled = false;
	uint32   response_cache_ttl_s   = 300;
	std::string fallback_model_id;
	std::string fallback_api_key;
	std::string fallback_api_endpoint;
	uint32   llm_max_retries        = 0;
	uint32   stats_conversations_24h = 0;
	uint32   stats_llm_calls_total   = 0;
};
class UserBotListMessage : public ThreadMessage
{
public:
	UserBotListMessage() : ThreadMessage(Msg_UserBotListMessage) {}
	struct BotInfo
	{
		uint64 bot_id = 0;
		UID avatar_uid;
		std::string name;
		std::string prompt;
		AvatarSettings avatar_settings;
		Vec3d pos;
		float heading = 0.f;
		std::string greeting_name;
		std::string greeting_url;
		float greeting_cooldown = 0.f;
		std::string idle_name;
		std::string idle_url;
		float idle_interval = 0.f;
		std::string reactive_name;
		std::string reactive_url;
		float reactive_cooldown = 0.f;
		uint32 flags = 0;
		float greeting_distance = 6.f;
		float farewell_distance = 10.f;
		float chat_radius = 8.f;
		Vec3f model_scale = Vec3f(1.f);
		std::string ai_model_id;
		std::string ai_personality_preset = "assistant";
		std::string ai_knowledge;
		float ai_temperature = 0.7f;
		uint32 ai_max_tokens = 0;
		std::string audio_url;
		float audio_volume = 1.f;
		float audio_radius = 10.f;
		float audio_activation_distance = 12.f;
		float audio_cooldown = 0.f;
		uint32 trigger_flags = 3;
		std::string trigger_keywords;
		float trigger_cooldown = 3.f;
		uint32 greeting_gesture_flags = 0;
		uint32 idle_gesture_flags = 0;
		uint32 reactive_gesture_flags = 0;
		std::string fallback_message;
		std::string surprise_name;
		std::string surprise_url;
		uint32 surprise_flags = 0;
		float  surprise_cooldown = 15.f;
		std::string acknowledge_name;
		std::string acknowledge_url;
		uint32 acknowledge_flags = 0;
		float  acknowledge_cooldown = 10.f;
		uint32 use_action_type = 0;
		std::string use_action_param;
		std::string api_key;
		std::string api_endpoint;
		// Block 5
		uint32 movement_type = 0;
		float  walk_speed    = 1.4f;
		float  wander_radius = 5.f;
		std::vector<BotWaypoint>  waypoints;
		std::vector<BotUseAction> use_actions;
		// Block 6
		std::string farewell_gesture_name;
		std::string farewell_gesture_url;
		uint32      farewell_gesture_flags    = 0;
		float       farewell_gesture_cooldown = 8.f;
		std::string walk_gesture_name;
		std::string walk_gesture_url;
		uint32      walk_gesture_flags        = 0;
		std::string talk_gesture_name;
		std::string talk_gesture_url;
		uint32      talk_gesture_flags        = 0;
		std::string interaction_gesture_name;
		std::string interaction_gesture_url;
		uint32      interaction_gesture_flags    = 0;
		float       interaction_gesture_cooldown = 3.f;
		// Block 7
		float       audio_min_distance   = 1.f;
		float       audio_start_delay    = 0.f;
		std::string greeting_audio_url;
		std::string farewell_audio_url2;
		std::string interaction_audio_url;
		// Block 9
		float       conversation_timeout_s  = 0.f;
		uint32      max_llm_calls_per_hour  = 0;
		std::string webhook_url;
		uint32      active_hours_start_utc  = 8;
		uint32      active_hours_end_utc    = 22;
		std::vector<BotScriptedResponse>  scripted_responses;
		std::vector<std::string>          player_whitelist;
		std::vector<std::string>          player_blacklist;
		std::vector<BotToolFunctionInfo>  tool_functions;
		// Block 10
		uint32 ai_provider           = 0;
		float  top_p                 = 0.f;
		uint32 top_k                 = 0;
		float  frequency_penalty     = 0.f;
		float  presence_penalty      = 0.f;
		uint32 max_context_messages  = 0;
		uint32 dialog_start_node_id  = 0;
		std::vector<BotDialogNode>   dialog_nodes;
		// Block 11
		bool     enable_player_memory  = false;
		uint32   memory_summary_tokens = 150;
		std::string content_filter_patterns;
		bool     jailbreak_guard       = true;
		// Block 12
		uint32   max_llm_calls_per_player_per_hour = 0;
		bool     response_cache_enabled = false;
		uint32   response_cache_ttl_s   = 300;
		std::string fallback_model_id;
		std::string fallback_api_key;
		std::string fallback_api_endpoint;
		uint32   llm_max_retries        = 0;
		uint32   stats_conversations_24h = 0;
		uint32   stats_llm_calls_total   = 0;
	};
	std::vector<BotInfo> bots;
};


class BotPlayerMemoryListMessage : public ThreadMessage
{
public:
	BotPlayerMemoryListMessage() : ThreadMessage(Msg_BotPlayerMemoryListMessage), bot_id(0) {}
	struct PlayerEntry {
		std::string uid_str;
		uint32_t    visit_count = 0;
		int32_t     reputation  = 0;
		std::string quest_state;
		uint64_t    last_seen_unix = 0;
		std::string history_preview;
	};
	uint64_t bot_id;
	std::vector<PlayerEntry> entries;
};


class BotConversationLogMessage : public ThreadMessage
{
public:
	BotConversationLogMessage() : ThreadMessage(Msg_BotConversationLogMessage), bot_id(0) {}
	struct Entry {
		uint64_t    timestamp_unix = 0;
		std::string player_name;
		std::string player_uid_str;
		std::string player_message;
		std::string bot_response;
	};
	uint64_t bot_id;
	std::vector<Entry> entries;
};


class SignedUpMessage : public ThreadMessage
{
public:
	SignedUpMessage(UserID user_id_, const std::string& username_) : ThreadMessage(Msg_SignedUpMessage), user_id(user_id_), username(username_) {}
	UserID user_id;
	std::string username;
};


class ServerAdminMessage : public ThreadMessage
{
public:
	ServerAdminMessage(const std::string& msg_) : ThreadMessage(Msg_ServerAdminMessage), msg(msg_) {}
	std::string msg;
};


class WorldSettingsReceivedMessage : public ThreadMessage
{
public:
	WorldSettingsReceivedMessage(bool is_initial_send_) : ThreadMessage(Msg_WorldSettingsReceivedMessage), is_initial_send(is_initial_send_), sender_avatar_UID(UID::invalidUID()) {}
	WorldSettings world_settings;
	bool is_initial_send;
	UID sender_avatar_UID;
};


class WorldDetailsReceivedMessage : public ThreadMessage
{
public:
	WorldDetailsReceivedMessage() : ThreadMessage(Msg_WorldDetailsReceivedMessage) {}
	WorldDetails world_details;
};


class MapTilesResultReceivedMessage : public ThreadMessage
{
public:
	MapTilesResultReceivedMessage() : ThreadMessage(Msg_MapTilesResultReceivedMessage) {}
	std::vector<Vec3i> tile_indices;
	std::vector<URLString> tile_URLS;
};


/*=====================================================================
ClientThread
------------
Maintains network connection to server.
=====================================================================*/
class ClientThread : public MessageableThread
{
public:
	ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, const std::string& hostname, int port,
		const std::string& initial_world_name, struct tls_config* config, const Reference<glare::FastPoolAllocator>& world_ob_pool_allocator, Reference<WorldState> world_state);
	virtual ~ClientThread();

	virtual void doRun() override;

	void enqueueDataToSend(const ArrayRef<uint8> data); // threadsafe

	virtual void kill() override;

	void killConnection();
private:
	void readAndHandleMessage(uint32 peer_protocol_version);
	void handleObjectInitialSend(RandomAccessInStream& msg_stream);

	Reference<WorldState> world_state;

	UID client_avatar_uid;

	Reference<WorldObject> allocWorldObject();

	glare::AtomicInt should_die;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	EventFD event_fd;
	std::string hostname;
	int port;
public:
	Reference<SocketInterface> socket;
private:
	std::string initial_world_name;
	struct tls_config* config;

	Mutex data_to_send_mutex;
	js::Vector<uint8, 16> data_to_send						GUARDED_BY(data_to_send_mutex);
	bool send_data_to_socket;

	BufferInStream msg_buffer;

	Reference<glare::FastPoolAllocator> world_ob_pool_allocator;

	ThreadManager client_sender_thread_manager;
	Reference<ClientSenderThread> client_sender_thread		GUARDED_BY(data_to_send_mutex);

	ZSTD_DCtx_s* dstream;
};
