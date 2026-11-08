/*=====================================================================
WorkerThread.h
--------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include "../shared/UserID.h"
#include "../shared/UID.h"
#include <RequestInfo.h>
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <MySocket.h>
#include <SocketBufferOutStream.h>
#include <Vector.h>
#include <BufferInStream.h>
#include <AtomicInt.h>
#include <string>
#include <chrono>
#include <set>
#include <map>
class Server;
class ServerAllWorldsState;
class ServerWorldState;
class WorldObject;
class WorldStateLock;

// Shared with server-side Lua game object transactions so they use the same
// placement authorization and LOD invalidation rules as normal client actions.
bool userHasObjectCreationPermissions(const WorldObject& object, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock);
void markLODChunkAsNeedsRebuildForChangedObject(ServerWorldState* world_state, const WorldObject* object, WorldStateLock& lock);


/*=====================================================================
WorkerThread
------------
This thread runs on the server, and handles communication with a single client.
=====================================================================*/
class WorkerThread : public MessageableThread
{
public:
	// May throw glare::Exception from constructor if EventFD init fails.
	WorkerThread(const Reference<SocketInterface>& socket, Server* server, bool is_websocket_connection);
	virtual ~WorkerThread();

	virtual void doRun() override;

	virtual void kill() override;

	Reference<ServerWorldState> cur_world_state; // World the client is connected to.

	void enqueueDataToSend(const SocketBufferOutStream& packet); // threadsafe
	void enqueueDataToSend(const ArrayRef<uint8> data); // threadsafe
	void enqueueAdminWorldEntry(const SocketBufferOutStream& packet); // Checks authenticated superadmin and capability.
	std::string getRoutingUserName() const;
	UserID getRoutingUserID() const;
	UID getRoutingAvatarUID() const;
	bool supportsScriptedGameHUD() const;
	bool supportsScriptedGameHUDLayout() const;
	bool supportsScriptedGameHUDPanels() const;
	bool supportsScriptedGameHUDForms() const;
	bool supportsScriptedGameHUDChoices() const;
	bool supportsScriptedGameHUDNumeric() const;
	bool supportsScriptedGameHUDCheckbox() const;
	bool supportsScriptedGameHUDActionLayout() const;
	bool supportsScriptedGameHUDProgress() const;
	bool supportsScriptedGameHUDTextWidgets() const;
	bool supportsScriptedGameHUDTextStyle() const;
	bool supportsScriptedGameHUDTextLayout() const;
	bool supportsScriptedGameTriggerInput() const;
	bool supportsScriptedGameWheelInput() const;
	bool supportsScriptedGameStateDelta() const;
	bool supportsScriptedGameEvents() const;
	bool supportsScriptedGameEventTime() const;
	bool supportsScriptedGameAimRay() const;
	bool supportsScriptedInteractionPrompts() const;
	bool supportsScriptedInteractionKeys() const;
	bool supportsScriptedObjectClicks() const;
	bool supportsScriptedObjectClickRay() const;
	bool supportsAvatarMaterialiseEffect() const;
	UID getActiveScriptKeyboardObjectUID() const;

	web::RequestInfo websocket_request_info; // If the client connected via a websocket, this the HTTP request data.  Is used for accessing the login cookie.

private:
	void sendGetFileMessageIfNeeded(const URLString& resource_URL);
	void handleResourceUploadConnection();
	void handleResourceDownloadConnection();
	void handleScreenshotBotConnection();
	void handleEthBotConnection();
	void conPrintIfNotFuzzing(const std::string& msg);
	void sendPerWorldInitialDataToClient(ServerAllWorldsState* world_state, uint32 client_protocol_version);
	void enqueuePacketToBroadcast(const SocketBufferOutStream& packet_buffer);
	void setRoutingClientInfo(const UserID& user_id, const std::string& user_name, const UID& avatar_uid);
	void setActiveScriptKeyboardObjectUID(const UID& object_uid);
	void notifyWorldEntry(UserID user_id, const std::string& user_name, UID avatar_uid);
	void releaseScriptKeyboardFocus(UID client_avatar_uid);

	Reference<SocketInterface> socket;
	Server* server;
	EventFD event_fd;	

	Mutex data_to_send_mutex;
	js::Vector<uint8, 16> data_to_send			GUARDED_BY(data_to_send_mutex);
	js::Vector<uint8, 16> temp_data_to_send;

	js::Vector<uint8, 16> m_temp_buf;

	SocketBufferOutStream scratch_packet;

	BufferInStream msg_buffer;
public:
	bool fuzzing; // Are we currently doing fuzz-testing?
private:
	mutable Mutex routing_client_info_mutex;
	UserID routing_client_user_id;
	std::string routing_client_user_name;
	UID routing_client_avatar_uid;
	bool supports_admin_world_entries GUARDED_BY(routing_client_info_mutex) = false;
	uint32 client_capabilities GUARDED_BY(routing_client_info_mutex) = 0;
	UID active_script_keyboard_object_uid GUARDED_BY(routing_client_info_mutex) = UID::invalidUID();
	js::Vector<uint8, 16> admin_entry_data GUARDED_BY(routing_client_info_mutex);
	// Accessed only by this connection's worker. Suppresses repeated CreateAvatar packets.
	bool world_entry_reported = false;
	UserID reported_entry_user = UserID::invalidUserID();
	std::string entry_world_name;
	std::chrono::steady_clock::time_point object_key_event_window_start{};
	uint32 object_key_events_in_window = 0;
	std::chrono::steady_clock::time_point game_action_event_window_start{};
	uint32 game_action_events_in_window = 0;
	std::chrono::steady_clock::time_point game_input_state_window_start{};
	uint32 game_input_state_events_in_window = 0;
	std::chrono::steady_clock::time_point game_rpc_events_window_start{};
	uint32 game_rpc_events_in_window = 0;
	std::set<uint32> active_script_keyboard_keys;
	std::chrono::steady_clock::time_point last_scripted_object_click{};
	std::map<UID, std::chrono::steady_clock::time_point> scripted_object_click_times;

	bool write_trace; // Should we write a record of network traffic to disk for fuzz seeding?

	glare::AtomicInt should_quit;

	bool is_websocket_connection;
};
