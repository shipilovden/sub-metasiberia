/*=====================================================================
ChatBot.h
---------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include "../shared/UserID.h"
#include "../shared/WorldMaterial.h"
#include "../shared/Avatar.h"
#include "../shared/WorldStateLock.h"
#include <TimeStamp.h>
#include <ThreadSafeRefCounted.h>
#include <WeakRefCounted.h>
#include <Reference.h>
#include <Timer.h>
#include <OutStream.h>
#include <SocketBufferOutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>
#include <BitUtils.h>
#include <maths/Matrix4f.h>
#include <map>
#include <unordered_map>
#include <vector>


class RandomAccessInStream;
class LLMThread;
class Server;
class ServerWorldState;
class ToolFunctionCall;


// One point in a bot patrol route
struct BotWaypoint
{
	Vec3d pos;
	float heading_override; // -1.f = face direction of travel automatically
	float dwell_time_s;     // Seconds to pause at this waypoint (0 = no pause)
	static const float AUTO_HEADING; // sentinel value -1.f
};

void readBotWaypointFromStream(InStream& stream, BotWaypoint& wp);
void writeBotWaypointToStream(const BotWaypoint& wp, OutStream& stream);

// One action triggered when a player presses E on this bot
struct BotUseAction
{
	uint32      type;
	std::string label; // Optional short label shown in action menu
	std::string param; // URL / text / gesture-slot name / "wx,wy,wz,wh" for teleport
	std::string required_avatar_uid; // If non-empty, only triggers for this avatar UID (decimal string)

	static const uint32 USE_ACTION_LLM         = 0; // Start AI conversation
	static const uint32 USE_ACTION_SAY_TEXT    = 1; // Bot says fixed text
	static const uint32 USE_ACTION_GESTURE     = 2; // Play a gesture slot
	static const uint32 USE_ACTION_OPEN_URL    = 3; // Open URL in browser
	static const uint32 USE_ACTION_TELEPORT_TO = 4; // Teleport player to coords
	static const uint32 USE_ACTION_NONE        = 5;
	static const uint32 USE_ACTION_SET_QUEST   = 6; // param = new quest_state string
	static const uint32 USE_ACTION_REP_UP      = 7; // param = points (default 10)
	static const uint32 USE_ACTION_REP_DOWN    = 8; // param = points (default 10)
	static const int MAX_LABEL_SIZE = 100;
	static const int MAX_PARAM_SIZE = 1000;
	static const int MAX_REQUIRED_UID_SIZE = 64;
};

void readBotUseActionFromStream(InStream& stream, BotUseAction& a);
void writeBotUseActionToStream(const BotUseAction& a, OutStream& stream);


class ChatBotToolFunction : public ThreadSafeRefCounted
{
public:
	void writeToStream(RandomAccessOutStream& stream);

	static const int MAX_FUNCTION_NAME_SIZE = 1'000;
	static const int MAX_DESCRIPTION_NAME_SIZE = 10'000;
	static const int MAX_RESULT_CONTENT_SIZE = 100'000;

	std::string function_name;
	std::string description;
	std::string result_content;
};

void readChatBotToolFunctionFromStream(RandomAccessInStream& stream, ChatBotToolFunction& func);


// Plain data struct for passing tool function data through the UI layer (no ref-counting overhead)
struct BotToolFunctionInfo
{
	std::string function_name;
	std::string description;
	std::string result_content;
};


// One scripted (keyword-triggered) response — replied without calling the LLM
struct BotScriptedResponse
{
	std::string keywords;      // Comma- or newline-separated keywords (OR logic, case-insensitive)
	std::string response_text;

	static const int MAX_KEYWORDS_SIZE  = 500;
	static const int MAX_RESPONSE_SIZE  = 2000;
	static const int MAX_COUNT          = 32;
};

inline void readBotScriptedResponseFromStream(InStream& stream, BotScriptedResponse& r)
{
	r.keywords      = stream.readStringLengthFirst(BotScriptedResponse::MAX_KEYWORDS_SIZE);
	r.response_text = stream.readStringLengthFirst(BotScriptedResponse::MAX_RESPONSE_SIZE);
}
inline void writeBotScriptedResponseToStream(const BotScriptedResponse& r, OutStream& stream)
{
	stream.writeStringLengthFirst(r.keywords);
	stream.writeStringLengthFirst(r.response_text);
}


// One branch choice in a dialog tree node
struct BotDialogChoice
{
	std::string label;        // Button/menu text shown to player (may be empty)
	std::string keywords;     // Trigger keywords, comma-sep, OR logic; empty = always/fallback
	uint32      next_node_id; // Node to go to; END_DIALOG = dialog ends

	static const uint32 END_DIALOG        = 0xFFFFFFFF;
	static const int    MAX_LABEL_SIZE    = 200;
	static const int    MAX_KEYWORDS_SIZE = 300;
	static const int    MAX_CHOICES       = 8;
};

// One node in a bot's scripted dialog tree
struct BotDialogNode
{
	uint32      node_id;      // Unique within this bot (0 = root/start)
	std::string bot_text;     // Text the bot says when this node is reached
	uint32      action_type;  // Optional follow-up action
	std::string action_param; // Parameter for the action
	std::vector<BotDialogChoice> choices;

	static const uint32 ACTION_NONE     = 0;
	static const uint32 ACTION_GESTURE  = 1; // action_param = gesture slot name
	static const uint32 ACTION_OPEN_URL = 2; // action_param = URL
	static const uint32 ACTION_TELEPORT = 3; // action_param = "x,y,z,heading_deg"

	static const int MAX_BOT_TEXT_SIZE = 2000;
	static const int MAX_ACTION_PARAM  = 500;
	static const int MAX_NODES         = 64;
};

inline void readBotDialogChoiceFromStream(InStream& stream, BotDialogChoice& c)
{
	c.label        = stream.readStringLengthFirst(BotDialogChoice::MAX_LABEL_SIZE);
	c.keywords     = stream.readStringLengthFirst(BotDialogChoice::MAX_KEYWORDS_SIZE);
	c.next_node_id = stream.readUInt32();
}
inline void writeBotDialogChoiceToStream(const BotDialogChoice& c, OutStream& stream)
{
	stream.writeStringLengthFirst(c.label);
	stream.writeStringLengthFirst(c.keywords);
	stream.writeUInt32(c.next_node_id);
}
inline void readBotDialogNodeFromStream(InStream& stream, BotDialogNode& n)
{
	n.node_id      = stream.readUInt32();
	n.bot_text     = stream.readStringLengthFirst(BotDialogNode::MAX_BOT_TEXT_SIZE);
	n.action_type  = stream.readUInt32();
	n.action_param = stream.readStringLengthFirst(BotDialogNode::MAX_ACTION_PARAM);
	const uint32 nc = myMin(stream.readUInt32(), (uint32)BotDialogChoice::MAX_CHOICES);
	n.choices.resize(nc);
	for(auto& ch : n.choices) readBotDialogChoiceFromStream(stream, ch);
}
inline void writeBotDialogNodeToStream(const BotDialogNode& n, OutStream& stream)
{
	stream.writeUInt32(n.node_id);
	stream.writeStringLengthFirst(n.bot_text);
	stream.writeUInt32(n.action_type);
	stream.writeStringLengthFirst(n.action_param);
	stream.writeUInt32((uint32)n.choices.size());
	for(const auto& ch : n.choices) writeBotDialogChoiceToStream(ch, stream);
}


/*=====================================================================
ChatBot
-------
A chatbot that uses LLMs for thinking.
=====================================================================*/
class ChatBot : public WeakRefCounted
{
public:
	ChatBot();
	~ChatBot();

	

	struct EventHandlerResults
	{
		Reference<LLMThread> new_llm_thread;
	};

	[[nodiscard]] EventHandlerResults userMovedNearToBotAvatar(AvatarRef other_avatar, Server* server, WorldStateLock& lock);
	[[nodiscard]] EventHandlerResults userMovedAwayFromBotAvatar(AvatarRef other_avatar, Server* server, WorldStateLock& lock);

	// A user/avatar sent a chat message, which this bot is in range of.
	// Threadsafe, will be called from WorkerThreads but only when the world lock is held.
	[[nodiscard]] EventHandlerResults processHeardChatMessage(const std::string& msg, AvatarRef sender_avatar, const std::string& avatar_name, Server* server, uint32 client_capabilities, WorldStateLock& lock);

	// Handle some (partial, streaming) chat data coming back from an LLM cloud server.
	// Called from the main thread only, in server.cpp.
	void handleLLMChatResponse(const std::string& msg, Server* server, WorldStateLock& world_lock);

	// Called from the main thread only, in server.cpp.
	void handleLLMChatResponseDone(Server* server, WorldStateLock& world_lock);

	// Called from the main thread only, in server.cpp.
	void handleLLMToolFunctionCall(const std::vector<Reference<ToolFunctionCall>>& calls, Server* server, WorldStateLock& world_lock);

	// Called from the main thread only, in server.cpp.
	struct ThinkResults
	{
		Reference<LLMThread> llm_thread_being_killed;
		Reference<LLMThread> new_llm_thread;
	};
	ThinkResults think(Server* server, WorldStateLock& world_lock);

	// Player pressed E on this bot avatar.
	[[nodiscard]] EventHandlerResults processUserUsedBot(AvatarRef user_avatar, Server* server, WorldStateLock& lock);

	// Queue a test gesture playback request. Executed in think() on the main server loop.
	void queueManualGesturePlayback(const std::string& gesture_name, const URLString& gesture_URL, uint32 gesture_flags);
	void clampAnimationSettings();

	void writeToStream(RandomAccessOutStream& stream);





	uint64 id;

	UserID owner_id;
	TimeStamp created_time;

	static const int MAX_NAME_SIZE = 200;
	std::string name;

	AvatarSettings avatar_settings;

	uint32 flags;

	Vec3d pos;
	float heading;

	static const int MAX_CUSTOM_PROMPT_PART_SIZE = 10000;
	std::string custom_prompt_part;

	// Configurable animation profile for server-driven chatbot gestures.
	static const int MAX_GESTURE_NAME_SIZE = 200;
	static const int MAX_GESTURE_URL_SIZE = 10'000;
	std::string greeting_gesture_name;
	URLString greeting_gesture_URL;
	uint32 greeting_gesture_flags;
	float greeting_gesture_cooldown_s;

	std::string idle_gesture_name;
	URLString idle_gesture_URL;
	uint32 idle_gesture_flags;
	float idle_gesture_interval_s;

	std::string reactive_gesture_name;
	URLString reactive_gesture_URL;
	uint32 reactive_gesture_flags;
	float reactive_gesture_cooldown_s;

	static const int MAX_EXTRA_GESTURE_NAME_SIZE = 200;
	std::string surprise_gesture_name;
	URLString   surprise_gesture_URL;
	uint32      surprise_gesture_flags;
	float       surprise_gesture_cooldown_s;

	std::string acknowledge_gesture_name;
	URLString   acknowledge_gesture_URL;
	uint32      acknowledge_gesture_flags;
	float       acknowledge_gesture_cooldown_s;

	static const int MAX_FALLBACK_MSG_SIZE = 500;
	std::string fallback_message;

	// Use action (triggered when player presses E on bot avatar and TRIGGER_USE_ACTION_FLAG is set)
	static const uint32 USE_ACTION_LLM         = 0;
	static const uint32 USE_ACTION_SAY_TEXT    = 1;
	static const uint32 USE_ACTION_GESTURE     = 2;
	static const uint32 USE_ACTION_NONE        = 3;
	static const uint32 USE_ACTION_OPEN_URL    = 3;
	static const uint32 USE_ACTION_TELEPORT_TO = 4;
	static const uint32 USE_ACTION_SET_QUEST   = 6;
	static const uint32 USE_ACTION_REP_UP      = 7;
	static const uint32 USE_ACTION_REP_DOWN    = 8;
	static const int MAX_USE_ACTION_PARAM_SIZE = 500;
	uint32      use_action_type;
	std::string use_action_param;

	// Per-bot API key/endpoint (override server credentials for this bot)
	static const int MAX_API_KEY_SIZE      = 200;
	static const int MAX_API_ENDPOINT_SIZE = 300;
	std::string api_key;      // Empty = use server credentials
	std::string api_endpoint; // Empty = use model default

	float greeting_distance;
	float farewell_distance;
	float chat_radius;

	Vec3f model_scale;

	static const int MAX_AI_MODEL_ID_SIZE = 200;
	static const int MAX_AI_PRESET_SIZE = 200;
	static const int MAX_AI_KNOWLEDGE_SIZE = 10000;
	std::string ai_model_id; // Empty means use the server default model.
	std::string ai_personality_preset;
	std::string ai_knowledge;
	float ai_temperature;
	uint32 ai_max_tokens; // 0 means provider/server default.

	static const int MAX_AUDIO_URL_SIZE = 10000;
	URLString audio_source_url;
	float audio_volume;
	float audio_radius;
	float audio_activation_distance;
	float audio_cooldown_s;

	static const int MAX_TRIGGER_KEYWORDS_SIZE = 4000;
	uint32 trigger_flags;
	std::string trigger_keywords;
	float trigger_cooldown_s;

	std::map<std::string, Reference<ChatBotToolFunction>> info_tool_functions; // Map from function name to ChatBotToolFunction ref.  Tool functions that the LLM can call.

	static const uint32 DISABLED_FLAG = 1; // If set, chatbot logic is disabled.
	static const uint32 ALWAYS_FACE_NEAREST_USER_FLAG = 2;
	static const uint32 STATIONARY_FLAG = 4;
	static const uint32 AUDIO_LOOP_FLAG = 8;
	static const uint32 AUDIO_SPATIAL_FLAG = 16;
	static const uint32 AUDIO_AUTOPLAY_FLAG = 32;
	static const uint32 ACTIVE_HOURS_ENABLED_FLAG = 64;
	static const uint32 LISTENS_GLOBAL_CHAT_FLAG  = 128;  // React to world chat even from far away
	static const uint32 REACT_TO_MENTION_FLAG     = 256;  // Only respond when @name is mentioned in message
	static const uint32 REACT_TO_BOTS_FLAG        = 512;  // Also respond to messages from other bots
	static const uint32 USE_DIALOG_FLAG           = 1024; // Use scripted dialog tree instead of LLM
	static const uint32 STREAM_LLM_FLAG           = 2048; // Stream LLM response token-by-token to chat

	static const uint32 TRIGGER_PROXIMITY_FLAG = 1;
	static const uint32 TRIGGER_CHAT_FLAG = 2;
	static const uint32 TRIGGER_KEYWORDS_FLAG = 4;
	static const uint32 TRIGGER_GESTURE_FLAG = 8;
	static const uint32 TRIGGER_USE_ACTION_FLAG = 16;

	// Movement types
	static const uint32 MOVEMENT_TYPE_STATIONARY = 0;
	static const uint32 MOVEMENT_TYPE_PATROL      = 1; // Walk between waypoints in order
	static const uint32 MOVEMENT_TYPE_WANDER      = 2; // Random wander within wander_radius

	// Block 5 — waypoints, movement, multiple use-actions
	uint32 movement_type;
	float  walk_speed;        // m/s
	float  wander_radius;     // For MOVEMENT_WANDER (m)
	static const int MAX_WAYPOINTS = 64;
	std::vector<BotWaypoint> waypoints;

	static const int MAX_USE_ACTIONS = 16;
	std::vector<BotUseAction> use_actions; // Replaces single use_action_type/param in UI

	// Block 6 — extra animation slots
	std::string farewell_gesture_name;
	URLString   farewell_gesture_URL;
	uint32      farewell_gesture_flags;
	float       farewell_gesture_cooldown_s;

	std::string walk_gesture_name;   // Animation played while moving along waypoints
	URLString   walk_gesture_URL;
	uint32      walk_gesture_flags;

	std::string talk_gesture_name;   // Animation played while bot is responding
	URLString   talk_gesture_URL;
	uint32      talk_gesture_flags;

	std::string interaction_gesture_name; // Animation played on E-key interaction
	URLString   interaction_gesture_URL;
	uint32      interaction_gesture_flags;
	float       interaction_gesture_cooldown_s;

	// Block 7 — extended sound settings
	float     audio_min_distance;       // Inner radius at full volume (m)
	float     audio_start_delay_s;      // Delay before first playback (s)
	URLString greeting_audio_url;       // Played when greeting a player
	URLString farewell_audio_url;       // Played when saying farewell
	URLString interaction_audio_url;    // Played on E-key interaction

	// Block 10 — advanced settings
	float    conversation_timeout_s;    // Seconds of silence before LLM context is reset; 0 = use default (120 s)
	uint32   max_llm_calls_per_hour;    // Maximum LLM requests per hour; 0 = unlimited
	static const int MAX_WEBHOOK_URL_SIZE = 500;
	std::string webhook_url;            // If non-empty, POST JSON events to this URL
	uint32   active_hours_start_utc;    // Hour (0–23 UTC) when bot becomes active (when ACTIVE_HOURS_ENABLED_FLAG set)
	uint32   active_hours_end_utc;      // Hour (0–23 UTC) when bot becomes inactive
	std::vector<BotScriptedResponse> scripted_responses; // Keyword→text replies without LLM
	static const int MAX_PLAYER_LIST_ENTRY_SIZE = 64;
	static const int MAX_PLAYER_LIST_SIZE = 64;
	std::vector<std::string> player_whitelist;  // Only these avatar UIDs may interact; empty = allow all
	std::vector<std::string> player_blacklist;  // These avatar UIDs are always blocked

	// Block 11 — extended AI parameters + dialog scenario
	static const uint32 AI_PROVIDER_SERVER_DEFAULT = 0;
	static const uint32 AI_PROVIDER_OPENAI         = 1;
	static const uint32 AI_PROVIDER_ANTHROPIC       = 2;
	static const uint32 AI_PROVIDER_XAI             = 3;
	static const uint32 AI_PROVIDER_GEMINI          = 4;
	static const uint32 AI_PROVIDER_MISTRAL         = 5;
	static const uint32 AI_PROVIDER_OLLAMA          = 6;

	uint32 ai_provider;           // AI_PROVIDER_* constant; 0 = server default
	float  top_p;                 // Nucleus sampling; 0.0 = server default; [0.01..1.0] = override
	uint32 top_k;                 // Top-K sampling; 0 = disabled
	float  frequency_penalty;     // [-2..2] penalise repeated tokens by frequency; default 0
	float  presence_penalty;      // [-2..2] penalise any repeated token; default 0
	uint32 max_context_messages;  // Max chat history messages sent to LLM; 0 = server default

	std::vector<BotDialogNode> dialog_nodes;
	uint32 dialog_start_node_id;  // node_id to start from on first interaction; usually 0

	// Block 12 — player memory + content safety
	struct PlayerMemory
	{
		std::string history;           // Last N message-exchange pairs as raw text
		TimeStamp   last_seen;
		uint32      visit_count = 0;
		int32_t     reputation  = 0;   // -100..100; injected as {reputation_level} in prompt
		std::string quest_state;       // e.g. "none" / "active:task1" / "done:task1"

		static const int MAX_HISTORY_SIZE    = 2000;
		static const int MAX_QUEST_STATE_SIZE = 200;
	};

	bool     enable_player_memory  = false;
	uint32   memory_summary_tokens = 150;
	static const int MAX_CONTENT_FILTER_SIZE = 2000;
	std::string content_filter_patterns;
	bool     jailbreak_guard = true;

	// Block 13 — per-player rate limit, LLM cache, fallback provider, retry, stats
	uint32 max_llm_calls_per_player_per_hour = 0; // 0 = unlimited
	bool   response_cache_enabled = false;
	uint32 response_cache_ttl_s   = 300;
	static const int MAX_FALLBACK_MODEL_SIZE    = 200;
	static const int MAX_FALLBACK_ENDPOINT_SIZE = 300;
	std::string fallback_model_id;
	std::string fallback_api_key;
	std::string fallback_api_endpoint;
	uint32 llm_max_retries = 0; // 0 = disabled; max 3

	// Runtime stats (not serialised)
	uint32 stats_conversations_24h = 0;
	uint32 stats_llm_calls_total   = 0;

	// Conversation log (runtime, not persisted)
	struct ConversationLogEntry
	{
		TimeStamp   timestamp;
		std::string player_name;
		std::string player_uid_str;
		std::string player_message;
		std::string bot_response;
		static const int MAX_ENTRIES = 200;
	};
	std::vector<ConversationLogEntry> conversation_log;

	std::map<UID, PlayerMemory> player_memories; // Keyed by player avatar UID

	void addToConversationLog(const std::string& player_name, const std::string& uid_str, const std::string& player_msg, const std::string& bot_resp);

	bool isDisabled() const { return BitUtils::isBitSet(flags, DISABLED_FLAG); }

	DatabaseKey database_key;

	Reference<LLMThread> llm_thread; // Thread that does the communication with the LLM cloud server.

	ServerWorldState* world; // world the chatbot is in.

	UID avatar_uid; // UID of the avatar of the chatbot.
	Reference<Avatar> avatar; // avatar of the chatbot.

	// Information about an avatar that is or was nearby the chatbot.
	struct OtherAvatarInfo
	{
		OtherAvatarInfo() : conversing(false) {}
		Timer attention_timer; // How long the other avatar has been staring at the chatbot avatar.
		Timer time_since_last_greeted_other_av;
		Timer time_since_farewelled_other_av;
		bool conversing; // Are we in a conversation with this other avatar.
	};
	std::map<Reference<Avatar>, OtherAvatarInfo> other_avatar_info;
	Reference<const Avatar> look_target_avatar; // Avatar we should look at, may be null if not chatting with anyone.

private:
	void sendChatMessage(const string_view message, Server* server, WorldStateLock& world_lock);
	Reference<LLMThread> createLLMThread(Server* server, const std::string& player_name = "", const UID& player_uid = UID());

	bool isActiveNow() const;                        // Returns true if current UTC hour is within active hours window
	bool isPlayerAllowed(AvatarRef user_avatar) const; // Checks whitelist/blacklist
	bool canMakeLLMCall();                           // Checks hourly quota; resets counter if window elapsed
	void incrementLLMCallCount();                    // Call after enqueuing a SendAIChatPostContent message

	// Dialog tree helpers
	const BotDialogNode* findDialogNode(uint32 node_id) const;
	[[nodiscard]] EventHandlerResults processDialogMessage(const std::string& msg, AvatarRef sender_avatar, Server* server, WorldStateLock& lock);
	bool botMessageMatchesMention(const std::string& msg) const;

	// Prompt variables, memory, content safety
	bool filterMessage(const std::string& msg) const;
	void recordPlayerMessage(const UID& uid, const std::string& player_name, const std::string& msg, const std::string& bot_reply);

	// Per-player rate limit helpers
	bool canPlayerMakeLLMCall(const UID& uid);
	void recordPlayerLLMCall(const UID& uid);

	// Runtime state — not serialized
	uint32 llm_calls_this_hour;
	Timer  llm_hour_window_timer;
	std::map<UID, uint32> player_dialog_node_map;

	// Per-player rate limiting
	std::map<UID, uint32> player_call_counts; // calls this hour per player
	Timer  player_rate_hour_timer;            // resets all player_call_counts hourly

	// LLM response cache
	struct CacheEntry { std::string response; time_t cached_at = 0; };
	std::unordered_map<size_t, CacheEntry> response_cache;
	static size_t hashMessage(const std::string& msg, const std::string& model_id);

	// Retry state
	uint32 current_llm_retry_count = 0;
	std::string current_player_message; // Message being processed (for retry/cache)
	Timer stats_24h_reset_timer; // Resets stats_conversations_24h every 24h

	// Conversation isolation: prevent context mixing when multiple players talk simultaneously
	UID         active_conversation_uid;          // UID of player we're currently responding to
	std::string active_conversation_player_name;  // Their display name (needed to recreate LLMThread)
	struct QueuedMsg { std::string player_name; std::string message; };
	std::map<UID, std::vector<QueuedMsg>> pending_msgs; // Messages queued while busy with another player
	Reference<LLMThread> queued_new_thread;       // Created in handleLLMChatResponseDone, picked up by think()

	// The response from the LLM is streamed back from the cloud server, however we only want to chat in complete sentences, not in fragments of sentences.  So we will scan the accumulated response for sentence ends.
	size_t body_start_index; // Index into total_llm_response.  Index at which the first of the current sentences' body starts.  Will be > 0 if after e.g. a [SPEAK] prefix.
	size_t next_sentence_start_index; // Index into total_llm_response.  Index at which the next sentence starts, e.g. 1 place past end of last sentence.
	size_t next_sentence_search_pos; // Current search position in total_llm_response for a sentence end.
	std::string total_llm_response;
	bool response_has_speak_prefix; // Did the chatbot want to speak this message by emitting the "[SPEAK]" prefix? (as opposed to staying silent for this message)?
	bool processed_first_response_data; // Have we received at least strlen("[SPEAK]") chars in the response, and looked for "[SPEAK]"?

	Timer sentences_received_timer; // Once we have received a complete sentence from the LLM server, start this timer.  When it has completed, send any queued sentences.

	Timer repeating_gesture_timer; // This will be unpaused and reset when a repeating gesture such as waving starts.  When it hits some elapsed time, we will stop the gesture.

	Timer time_since_last_LLM_activity; // Time since we sent a message or received a message rom the LLM server.  After this hits some threshold, kill the LLM thread.

	Timer time_since_last_reactive_gesture;
	Timer time_since_last_idle_gesture;
	Timer time_since_last_greeting_gesture;
	Timer time_since_last_farewell_gesture;
	Timer time_since_last_interaction_gesture;

	bool pending_manual_gesture;
	std::string pending_manual_gesture_name;
	URLString pending_manual_gesture_URL;
	uint32 pending_manual_gesture_flags;

	// Runtime movement state (not serialized)
	size_t waypoint_current_idx;       // Index of the waypoint we are heading towards
	double waypoint_dwell_remaining_s; // Seconds remaining to pause at current waypoint
	bool   waypoint_walking;           // True while walking (for animation switching)
	Timer  movement_tick_timer;        // Used to compute dt between think() calls
	Vec3d  wander_center;              // Centre of wander area (set on first wander think tick)
	Vec3d  wander_target;              // Current random walk target (MOVEMENT_TYPE_WANDER)
	bool   wander_has_target;          // False until first target is chosen
	uint32 wander_rng_state;           // LCG state for wander target selection

	void playGestureNow(const std::string& gesture_name, const URLString& gesture_URL, uint32 gesture_flags, Server* server);
	bool canTriggerTimer(const Timer& timer, float min_interval_s) const;

	SocketBufferOutStream scratch_packet;
};


typedef Reference<ChatBot> ChatBotRef;


void readChatBotFromStream(RandomAccessInStream& stream, ChatBot& chatbot);


struct ChatBotRefHash
{
	size_t operator() (const ChatBotRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
