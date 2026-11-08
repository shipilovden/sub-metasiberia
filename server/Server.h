/*=====================================================================
Server.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"
#include "ThreadManager.h"
#include "../shared/ResourceManager.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ScriptTimerQueue.h"
#include <IPAddress.h>
#include <utils/UniqueRef.h>
#include <utils/Timer.h>
class WorkerThread;
class SubstrataLuaVM;
class LuaHTTPRequestManager;
class LuaHTTPRequest;
class SocketBufferOutStream;


struct ServerConnectedClientInfo
{
	IPAddress ip_addr;
	UID client_avatar_id;
	int client_UDP_port; // UDP port on client end
};


class UserUsedObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserPressedObjectKeyThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	Reference<ServerWorldState> world;
	UID object_uid;
	uint32 key_code;
};

class UserReleasedObjectKeyThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	Reference<ServerWorldState> world;
	UID object_uid;
	uint32 key_code;
};

class UserGameSessionThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	UserID user_id;
	Reference<ServerWorldState> world;
	UID object_uid;
	bool active;
};

class ScriptMessageThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	Reference<ServerWorldState> world;
	std::string msg;
};

class ScriptGameHUDThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	UID game_object_uid;
	Reference<ServerWorldState> world;
	bool visible;
	std::string text;
	bool has_layout = false;
	float center_x = 0.f;
	float top_y = 0.55f;
};

class ScriptGameHUDMenuThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	UID game_object_uid;
	Reference<ServerWorldState> world;
	GameHUDMenu menu;
};

class ScriptGameStateThreadMessage : public ThreadMessage
{
public:
	UID game_object_uid;
	Reference<ServerWorldState> world;
	uint32 version = 0;
	uint32 previous_version = 0;
	std::vector<uint8> previous_serialized_value;
	std::vector<uint8> serialized_value;
};

class ScriptGameEventThreadMessage : public ThreadMessage
{
public:
	UID game_object_uid;
	Reference<ServerWorldState> world;
	uint32 sequence = 0;
	double server_time = 0;
	std::string name;
	std::vector<UID> recipients;
};

class ScriptInteractionPromptThreadMessage : public ThreadMessage
{
public:
	Reference<ServerWorldState> world;
	UID object_uid;
	ScriptInteractionPrompt prompt;
};

class ScriptAvatarMaterialiseEffectThreadMessage : public ThreadMessage
{
public:
	Reference<ServerWorldState> world;
	UID avatar_uid;
};

struct BotDialogOption
{
	uint32 choice_index = 0;
	std::string label;
};

class BotDialogOptionsThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	UID bot_avatar_uid;
	Reference<ServerWorldState> world;
	std::string bot_name;
	uint32 node_id = 0;
	std::vector<BotDialogOption> options;
};

class UserGameActionThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	Reference<ServerWorldState> world;
	UID object_uid;
	std::string action_id;
	std::vector<GameHUDFormValue> values;
};

class UserGameInputStateThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	Reference<ServerWorldState> world;
	UID object_uid;
	GameInputState input;
};

class UserGameRPCThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid;
	Reference<ServerWorldState> world;
	UID object_uid;
	std::string name;
	std::vector<uint8> serialized_payload;
};

class UserUsedBotThreadMessage : public ThreadMessage
{
public:
	UID user_avatar_uid;  // UID of the user's avatar
	UID bot_avatar_uid;   // UID of the bot avatar that was used
	Reference<ServerWorldState> world;
};

class UserTouchedObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserMovedNearToObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserMovedAwayFromObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserEnteredParcelThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in
	UserID client_user_id;  // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
	ParcelID parcel_id;
};

class UserExitedParcelThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
	ParcelID parcel_id;
};


/*=====================================================================
Server
--------------------
=====================================================================*/
class Server : public LuaScriptOutputHandler
{
public:
	Server();
	~Server();

	// LuaScriptOutputHandler interface:
	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len) override;
	virtual void errorOccurredFromLuaScript(LuaScript* script, const std::string& msg) override;

	void logLuaMessage(const std::string& msg, UserScriptLogMessage::MessageType message_type, UID world_ob_uid, UserID script_creator_user_id); // Thread-safe
	void logLuaError(const std::string& msg, UID world_ob_uid, UserID script_creator_user_id); // Thread-safe

	double getCurrentGlobalTime() const;

	void enqueueMsg(ThreadMessageRef msg);
	void enqueueMsgForLodGenThread(ThreadMessageRef msg) { mesh_lod_gen_thread_manager.enqueueMessage(msg); }

	void enqueueLuaHTTPRequest(Reference<LuaHTTPRequest> request);


	// Called from off main thread
	void clientUDPPortOpen(WorkerThread* worker_thread, const IPAddress& ip_addr, UID client_avatar_id/*, int client_UDP_port*/);
	void clientDisconnected(WorkerThread* worker_thread);

	// Called when we receive a UDP packet from a client, which allows the client remote UDP port to be known.
	void clientUDPPortBecameKnown(UID client_avatar_uid, const IPAddress& ip_addr, int client_UDP_port);

	// Enqueues packet to all WorkerThreads connected to the given world.
	// Thread-safe, can be called from any thread.
	void enqueuePacketToBroadcastForWorld(const SocketBufferOutStream& packet_buffer, ServerWorldState* world);
	// Queues a short game-script notification for delivery after the current world lock is released.
	void enqueueScriptMessageForAvatar(const UID& avatar_uid, const std::string& msg, ServerWorldState* world);
	void enqueueGameHUDForAvatar(const UID& avatar_uid, const UID& game_object_uid, bool visible, const std::string& text, ServerWorldState* world, bool has_layout = false, float center_x = 0.f, float top_y = 0.55f);
	void enqueueGameHUDMenuForAvatar(const UID& avatar_uid, const UID& game_object_uid, const GameHUDMenu& menu, ServerWorldState* world);
	void enqueueGameStateUpdate(const UID& game_object_uid, uint32 version, uint32 previous_version,
		const std::vector<uint8>& previous_serialized_value, const std::vector<uint8>& serialized_value, ServerWorldState* world);
	void enqueueGameEvent(const UID& game_object_uid, uint32 sequence, double server_time, const std::string& name,
		const std::vector<UID>& recipients, ServerWorldState* world);
	void enqueueScriptInteractionPromptUpdate(const UID& object_uid, const ScriptInteractionPrompt& prompt, ServerWorldState* world);
	void enqueueAvatarMaterialiseEffect(const UID& avatar_uid, ServerWorldState* world);
	void enqueueBotDialogOptionsForAvatar(const UID& avatar_uid, const UID& bot_avatar_uid, const std::string& bot_name, uint32 node_id, const std::vector<BotDialogOption>& options, ServerWorldState* world);
	// Server-thread-only routing stage for a queued player notification.
	void sendScriptMessageToAvatar(const UID& avatar_uid, const std::string& msg, ServerWorldState* world);
	void sendGameHUDToAvatar(const UID& avatar_uid, const UID& game_object_uid, bool visible, const std::string& text, ServerWorldState* world, bool has_layout = false, float center_x = 0.f, float top_y = 0.55f);
	void sendGameHUDMenuToAvatar(const UID& avatar_uid, const UID& game_object_uid, const GameHUDMenu& menu, ServerWorldState* world);
	void sendGameStateToAvatar(const UID& avatar_uid, const UID& game_object_uid, uint32 version,
		const std::vector<uint8>& serialized_value, ServerWorldState* world,
		uint32 previous_version = 0, const std::vector<uint8>& previous_serialized_value = std::vector<uint8>());
	void sendGameEventToAvatar(const UID& avatar_uid, const UID& game_object_uid, uint32 sequence, double server_time, const std::string& name, ServerWorldState* world);
	void broadcastScriptInteractionPrompt(const UID& object_uid, const ScriptInteractionPrompt& prompt, ServerWorldState* world);
	void broadcastAvatarMaterialiseEffect(const UID& avatar_uid, ServerWorldState* world);
	void sendBotDialogOptionsToAvatar(const UID& avatar_uid, const UID& bot_avatar_uid, const std::string& bot_name, uint32 node_id, const std::vector<BotDialogOption>& options, ServerWorldState* world);
	bool enqueuePrivateChatPacketForWorld(const SocketBufferOutStream& recipient_packet, const SocketBufferOutStream& sender_packet, ServerWorldState* world, WorkerThread* sender, const std::string& recipient_name, const UID& recipient_avatar_uid);

	// Enqueues packet to all WorkerThreads.
	// Thread-safe, can be called from any thread.
	void enqueuePacketToBroadcastForAllWorlds(const SocketBufferOutStream& packet_buffer);
	void enqueueAdminWorldEntryForAllWorlds(const SocketBufferOutStream& packet_buffer);


	Reference<ServerAllWorldsState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;

	ThreadManager mesh_lod_gen_thread_manager;

	ThreadManager udp_handler_thread_manager;

	ThreadManager dyn_tex_updater_thread_manager;

	ThreadManager llm_thread_manager;

	ThreadSafeQueue<Reference<ThreadMessage> > message_queue; // Contains messages from worker threads to the main server thread.

	std::string screenshot_dir;
	std::string photo_dir;

	ServerConfig config;

	Mutex connected_clients_mutex;
	std::map<WorkerThread*, ServerConnectedClientInfo> connected_clients;
	glare::AtomicInt connected_clients_changed;

	Timer total_timer;
	ScriptTimerQueue timer_queue;
	std::vector<ScriptTimerQueueTimer> temp_triggered_timers;

	Reference<LuaHTTPRequestManager> lua_http_manager;
};
