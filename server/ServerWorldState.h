/*=====================================================================
ServerWorldState.h
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include "../shared/WorldSettings.h"
#include "../shared/WorldDetails.h"
#include "../shared/WorldStateLock.h"
#include "../shared/Protocol.h"
#include "../shared/LODChunk.h"
#include "../shared/SubstrataLuaVM.h"
#include "ServerConfig.h"
#include "NewsPost.h"
#include "SubEvent.h"
#include "User.h"
#include "Order.h"
#include "UserWebSession.h"
#include "ParcelAuction.h"
#include "Screenshot.h"
#include "Photo.h"
#include "ChatBot.h"
#include "GameRaySpatialIndex.h"
#include "../shared/GearItem.h"
#include "../shared/UserID.h"
#include "SubEthTransaction.h"
#include <ThreadSafeRefCounted.h>
#include <Platform.h>
#include <Mutex.h>
#include <Database.h>
#include <CircularBuffer.h>
#include <HashMap.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <deque>
#include <vector>
#include <chrono>
class ServerWorldState;
class ServerAllWorldsState;
class WebDataStore;


struct OpenSeaParcelListing
{
	ParcelID parcel_id;
};


struct TileInfo
{
	ScreenshotRef cur_tile_screenshot;
	ScreenshotRef prev_tile_screenshot;
};


// Short-lived, per-player controls advertised by an explicitly focused Lua game.
// These are server-side authorization records, not persisted world data.
struct GameHUDMenuAction
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
	Type type = Text;
	std::vector<std::string> options;
	bool required = false;
	bool checked = false;
	double minimum = 0.0;
	double maximum = 0.0;
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

struct GameHUDFormValue
{
	std::string id;
	std::string value;
};

struct GameHUDMenu
{
	std::string panel_id = "main";
	std::string title;
	std::string body;
	std::vector<GameHUDMenuAction> actions;
	std::vector<GameHUDFormField> fields;
	std::vector<GameHUDProgressBar> progress_bars;
	std::vector<GameHUDTextWidget> text_widgets;
	float center_x = 0.f;
	float top_y = 0.48f;
	float width = 0.8f;
};

struct GameStateSnapshot
{
	uint32 version = 0;
	std::vector<uint8> serialized_value;
};

struct ScriptInteractionPrompt
{
	// input_mode: 0 = keyboard, 1 = left mouse button, 2 = either. key_code is 0 for no key and E for legacy prompts.
	uint32 input_mode = 0;
	uint32 key_code = Protocol::ScriptKey_CharacterBase + 'e';
	bool visible = false;
	std::string text;
};

struct GameOverlapChangeSet
{
	std::vector<UID> entered;
	std::vector<UID> exited;
};

struct GameInputState
{
	bool gamepad_connected = false;
	float left_x = 0.f, left_y = 0.f, right_x = 0.f, right_y = 0.f;
	float left_trigger = 0.f, right_trigger = 0.f;
	bool pointer_available = false;
	float pointer_x = 0.f, pointer_y = 0.f;
	float wheel_x = 0.f, wheel_y = 0.f; // Pending scroll steps, consumed once by game.getInput().
	bool aim_ray_available = false;
	Vec3d aim_origin = Vec3d(0,0,0), aim_direction = Vec3d(0,0,1);
	std::chrono::steady_clock::time_point last_received_at;
};


struct LastParcelUpdateInfo
{
	LastParcelUpdateInfo() : db_dirty(false) {}

	int last_parcel_sale_update_hour;
	int last_parcel_sale_update_day;
	int last_parcel_sale_update_year;

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct EthInfo
{
	EthInfo() : db_dirty(false) {}

	int min_next_nonce;
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct MapTileInfo
{
	MapTileInfo() : db_dirty(false) {}

	std::map<Vec3<int>, TileInfo> info;
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct PendingMapTileScreenshot
{
	std::string world_name; // Empty string for the main/root world.
	Vec3<int> tile_coords;
};


struct FeatureFlagInfo
{
	FeatureFlagInfo();

	uint64 feature_flags; // A combination of ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG etc.

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


struct MigrationVersionInfo
{
	MigrationVersionInfo() : db_dirty(false) {}

	uint32 migration_version;
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};

struct AdminActionLogEntry
{
	TimeStamp time;
	UserID user_id;
	std::string username;
	std::string action;
};


struct UserScriptLogMessage
{
	TimeStamp time;

	enum MessageType
	{
		MessageType_print,
		MessageType_error
	};
	MessageType msg_type;

	UID script_ob_uid;
	std::string msg;
};

struct UserScriptLog : public ThreadSafeRefCounted
{
	CircularBuffer<UserScriptLogMessage> messages GUARDED_BY(mutex);
	size_t total_message_bytes GUARDED_BY(mutex) = 0;
	Mutex mutex;
};


struct ObjectStorageKey
{
	ObjectStorageKey() {}
	ObjectStorageKey(const UID& ob_uid_, const std::string& key_string_) : ob_uid(ob_uid_), key_string(key_string_) {}

	UID ob_uid;
	std::string key_string;

	inline bool operator == (const ObjectStorageKey& other) const { return ob_uid == other.ob_uid && key_string == other.key_string; }
	inline bool operator < (const ObjectStorageKey& other) const
	{
		if(ob_uid < other.ob_uid)
			return true;
		else if(ob_uid > other.ob_uid)
			return false;
		else
			return key_string < other.key_string;
	}
};


struct ObjectStorageItem : public ThreadSafeRefCounted
{
	ObjectStorageKey key;
	glare::AllocatorVector<unsigned char, 16> data;
	DatabaseKey database_key;
};
typedef Reference<ObjectStorageItem> ObjectStorageItemRef;
	
struct ObjectStorageItemRefHash
{
	size_t operator() (const ObjectStorageItemRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


struct UserSecretKey
{
	UserSecretKey() {}
	UserSecretKey(const UserID& user_id_, const std::string& secret_name_) : user_id(user_id_), secret_name(secret_name_) {}

	UserID user_id;
	std::string secret_name;

	inline bool operator == (const UserSecretKey& other) const { return user_id == other.user_id && secret_name == other.secret_name; }
	inline bool operator < (const UserSecretKey& other) const
	{
		if(user_id < other.user_id)
			return true;
		else if(user_id > other.user_id)
			return false;
		else
			return secret_name < other.secret_name;
	}
};


struct UserSecret : public ThreadSafeRefCounted
{
	UserSecretKey key;
	std::string value;
	DatabaseKey database_key;

	static const size_t MAX_SECRET_NAME_SIZE            = 1000;
	static const size_t MAX_VALUE_SIZE                  = 10000;
};
typedef Reference<UserSecret> UserSecretRef;
	
struct UserSecretRefHash
{
	size_t operator() (const UserSecretRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


/*=====================================================================
ServerWorldState
----------------
State for a particular world.

Due to limitations of the Thread Safety Analysis, which doesn't seem to handle
references in maps, we will enforce that the using thread holds the world state mutex by making
members private and having accessor methods that take a WorldStateLock argument.
=====================================================================*/
class ServerWorldState : public ThreadSafeRefCounted
{
public:
	ServerWorldState() : db_dirty(false) {}

	void addParcelAsDBDirty     (const ParcelRef parcel,  WorldStateLock& /*world_state_lock*/) { db_dirty_parcels.insert(parcel); }
	void addWorldObjectAsDBDirty(const WorldObjectRef ob, WorldStateLock& /*world_state_lock*/)
	{
		db_dirty_world_objects.insert(ob);
		game_ray_spatial_index.updateObject(*ob);
	}
	void addLODChunkAsDBDirty   (const LODChunkRef ob,    WorldStateLock& /*world_state_lock*/) { db_dirty_lod_chunks.insert(ob); }
	void addChatBotAsDBDirty    (const ChatBotRef ob,     WorldStateLock& /*world_state_lock*/) { db_dirty_chatbots.insert(ob); }

	void writeToStream(RandomAccessOutStream& stream) const;

	WorldSettings world_settings;

	WorldDetails details; // owner ID, name, description etc.
	
	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.


	typedef std::map<UID, Reference<Avatar>> AvatarMapType;
	typedef std::map<UID, WorldObjectRef> ObjectMapType;
	typedef std::map<ParcelID, ParcelRef> ParcelMapType;
	typedef std::map<Vec3i, LODChunkRef> LODChunkMapType;
	typedef std::unordered_set<WorldObjectRef, WorldObjectRefHash> DirtyFromRemoteObjectSetType;
	typedef std::map<uint64, ChatBotRef> ChatBotMapType;

	AvatarMapType&     getAvatars(WorldStateLock& /*world_state_lock*/) { return avatars; }
	ObjectMapType&     getObjects(WorldStateLock& /*world_state_lock*/) { return objects; }
	void ensureGameRaySpatialIndex(WorldStateLock& /*world_state_lock*/)
	{
		if(!game_ray_spatial_index.isBuilt()) game_ray_spatial_index.rebuild(objects);
	}
	void refreshGameRaySpatialIndexObject(const WorldObject& object, WorldStateLock& /*world_state_lock*/)
	{
		game_ray_spatial_index.updateObject(object);
	}
	void removeGameRaySpatialIndexObject(const UID& uid, WorldStateLock& /*world_state_lock*/)
	{
		game_ray_spatial_index.removeObject(uid);
		script_interaction_prompts.erase(uid);
	}
	bool getGameRaySpatialCandidates(const Vec3d& origin, const Vec3d& direction, double max_distance,
		std::vector<UID>& candidates_out, WorldStateLock& world_state_lock)
	{
		ensureGameRaySpatialIndex(world_state_lock);
		return game_ray_spatial_index.candidatesForSegment(origin, direction, max_distance, candidates_out);
	}
	bool getGameRaySpatialCandidatesForBounds(const Vec3d& bounds_min, const Vec3d& bounds_max,
		std::vector<UID>& candidates_out, WorldStateLock& world_state_lock)
	{
		ensureGameRaySpatialIndex(world_state_lock);
		return game_ray_spatial_index.candidatesForBounds(bounds_min, bounds_max, candidates_out);
	}
	ParcelMapType&     getParcels(WorldStateLock& /*world_state_lock*/) { return parcels; }
	LODChunkMapType& getLODChunks(WorldStateLock& /*world_state_lock*/) { return lod_chunks; }
	ChatBotMapType&    getChatBots(WorldStateLock& /*world_state_lock*/) { return chatbots; }
	void setGameSessionParticipant(const UID& game_object_uid, const UID& avatar_uid, bool active, WorldStateLock& /*world_state_lock*/, const UserID& user_id = UserID::invalidUserID())
	{
		if(!game_object_uid.valid() || !avatar_uid.valid())
			return;
		if(active)
		{
			// A new E-session always begins with a clean input state, even if a
			// duplicate or delayed focus transition reaches the server.
			auto held_keys_it = game_session_held_keys.find(game_object_uid);
			if(held_keys_it != game_session_held_keys.end())
			{
				held_keys_it->second.erase(avatar_uid);
				if(held_keys_it->second.empty())
					game_session_held_keys.erase(held_keys_it);
			}
			game_session_participants[game_object_uid].insert(avatar_uid);
			game_session_user_ids[game_object_uid][avatar_uid] = user_id;
			auto input_it = game_session_input_state.find(game_object_uid);
			if(input_it != game_session_input_state.end()) input_it->second.erase(avatar_uid);
		}
		else
		{
			auto menu_it = game_hud_menus.find(game_object_uid);
			if(menu_it != game_hud_menus.end())
			{
				menu_it->second.erase(avatar_uid);
				if(menu_it->second.empty())
					game_hud_menus.erase(menu_it);
			}
			auto session_it = game_session_participants.find(game_object_uid);
			if(session_it != game_session_participants.end())
			{
				session_it->second.erase(avatar_uid);
				if(session_it->second.empty())
					game_session_participants.erase(session_it);
			}
			auto user_it = game_session_user_ids.find(game_object_uid);
			if(user_it != game_session_user_ids.end())
			{
				user_it->second.erase(avatar_uid);
				if(user_it->second.empty())
					game_session_user_ids.erase(user_it);
			}
			auto key_game_it = game_session_held_keys.find(game_object_uid);
			if(key_game_it != game_session_held_keys.end())
			{
				key_game_it->second.erase(avatar_uid);
				if(key_game_it->second.empty())
					game_session_held_keys.erase(key_game_it);
			}
			auto input_it = game_session_input_state.find(game_object_uid);
			if(input_it != game_session_input_state.end())
			{
				input_it->second.erase(avatar_uid);
				if(input_it->second.empty()) game_session_input_state.erase(input_it);
			}
			auto rpc_game_it = game_rpc_update_windows.find(game_object_uid);
			if(rpc_game_it != game_rpc_update_windows.end())
			{
				rpc_game_it->second.erase(avatar_uid);
				if(rpc_game_it->second.empty()) game_rpc_update_windows.erase(rpc_game_it);
			}
			auto input_rate_game_it = game_input_update_windows.find(game_object_uid);
			if(input_rate_game_it != game_input_update_windows.end())
			{
				input_rate_game_it->second.erase(avatar_uid);
				if(input_rate_game_it->second.empty()) game_input_update_windows.erase(input_rate_game_it);
			}
		}
	}

	// Tracks verified per-avatar proximity transitions for script events.
	bool transitionScriptProximity(const UID& avatar_uid, const UID& object_uid, bool is_near, WorldStateLock& /*world_state_lock*/)
	{
		if(!avatar_uid.valid() || !object_uid.valid())
			return false;
		auto it = script_proximity_objects.find(avatar_uid);
		if(is_near)
		{
			if(it == script_proximity_objects.end())
				it = script_proximity_objects.emplace(avatar_uid, std::set<UID>()).first;
			return it->second.insert(object_uid).second;
		}
		if(it == script_proximity_objects.end())
			return false;
		const bool was_near = it->second.erase(object_uid) != 0;
		if(it->second.empty())
			script_proximity_objects.erase(it);
		return was_near;
	}

	void clearAvatarScriptProximity(const UID& avatar_uid, WorldStateLock& /*world_state_lock*/)
	{
		script_proximity_objects.erase(avatar_uid);
	}
	bool isGameSessionParticipant(const UID& game_object_uid, const UID& avatar_uid, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_session_participants.find(game_object_uid);
		return game_it != game_session_participants.end() && game_it->second.count(avatar_uid) > 0;
	}
	void setGameSessionKeyDown(const UID& game_object_uid, const UID& avatar_uid, const std::string& key, bool down, WorldStateLock& world_state_lock)
	{
		if(key.empty() || !isGameSessionParticipant(game_object_uid, avatar_uid, world_state_lock))
			return;
		auto& keys = game_session_held_keys[game_object_uid][avatar_uid];
		if(down)
			keys.insert(key);
		else
		{
			keys.erase(key);
			if(keys.empty())
			{
				auto game_it = game_session_held_keys.find(game_object_uid);
				if(game_it != game_session_held_keys.end())
				{
					game_it->second.erase(avatar_uid);
					if(game_it->second.empty())
						game_session_held_keys.erase(game_it);
				}
			}
		}
	}
	bool isGameSessionKeyDown(const UID& game_object_uid, const UID& avatar_uid, const std::string& key, WorldStateLock& /*world_state_lock*/) const
	{
		const auto input_game_it = game_session_input_state.find(game_object_uid);
		if(input_game_it != game_session_input_state.end())
		{
			const auto input_avatar_it = input_game_it->second.find(avatar_uid);
			// Older clients can send digital key events without the newer analog
			// snapshot capability. Apply stale-state expiry only after a snapshot
			// has actually been received for this participant.
			if(input_avatar_it != input_game_it->second.end() && input_avatar_it->second.last_received_at.time_since_epoch().count() != 0 &&
				!isFreshGameSessionInput(input_avatar_it->second, std::chrono::steady_clock::now())) return false;
		}
		const auto game_it = game_session_held_keys.find(game_object_uid);
		if(game_it == game_session_held_keys.end()) return false;
		const auto avatar_it = game_it->second.find(avatar_uid);
		return avatar_it != game_it->second.end() && avatar_it->second.count(key) > 0;
	}
	void setGameSessionInputState(const UID& game_object_uid, const UID& avatar_uid, const GameInputState& input, WorldStateLock& world_state_lock)
	{
		if(isGameSessionParticipant(game_object_uid, avatar_uid, world_state_lock))
		{
			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			GameInputState& stored = game_session_input_state[game_object_uid][avatar_uid];
			const bool had_stale_input = stored.last_received_at.time_since_epoch().count() != 0 &&
				!isFreshGameSessionInput(stored, now);
			const float pending_wheel_x = had_stale_input ? 0.f : stored.wheel_x;
			const float pending_wheel_y = had_stale_input ? 0.f : stored.wheel_y;
			if(had_stale_input)
			{
				auto held_game_it = game_session_held_keys.find(game_object_uid);
				if(held_game_it != game_session_held_keys.end())
				{
					held_game_it->second.erase(avatar_uid);
					if(held_game_it->second.empty()) game_session_held_keys.erase(held_game_it);
				}
			}
			stored = input;
			stored.wheel_x = std::max(-100.f, std::min(100.f, pending_wheel_x + input.wheel_x));
			stored.wheel_y = std::max(-100.f, std::min(100.f, pending_wheel_y + input.wheel_y));
			stored.last_received_at = now;
		}
	}
	bool getGameSessionInputState(const UID& game_object_uid, const UID& avatar_uid, GameInputState& input_out, WorldStateLock& /*world_state_lock*/)
	{
		auto game_it = game_session_input_state.find(game_object_uid);
		if(game_it == game_session_input_state.end()) return false;
		auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end()) return false;
		if(!isFreshGameSessionInput(avatar_it->second, std::chrono::steady_clock::now()))
		{
			input_out = GameInputState();
			avatar_it->second.wheel_x = 0.f;
			avatar_it->second.wheel_y = 0.f;
			return true;
		}
		input_out = avatar_it->second;
		avatar_it->second.wheel_x = 0.f;
		avatar_it->second.wheel_y = 0.f;
		return true;
	}
	bool getGameSessionAimRay(const UID& game_object_uid, const UID& avatar_uid, Vec3d& origin_out, Vec3d& direction_out, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_session_input_state.find(game_object_uid);
		if(game_it == game_session_input_state.end()) return false;
		const auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end() || !avatar_it->second.aim_ray_available ||
			!isFreshGameSessionInput(avatar_it->second, std::chrono::steady_clock::now())) return false;
		origin_out = avatar_it->second.aim_origin;
		direction_out = avatar_it->second.aim_direction;
		return true;
	}
	static bool isFreshGameSessionInput(const GameInputState& input, const std::chrono::steady_clock::time_point& now)
	{
		return input.last_received_at.time_since_epoch().count() != 0 &&
			now - input.last_received_at <= std::chrono::milliseconds(500);
	}
	bool getGameSessionUserID(const UID& game_object_uid, const UID& avatar_uid, UserID& user_id_out, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_session_user_ids.find(game_object_uid);
		if(game_it == game_session_user_ids.end())
			return false;
		const auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end())
			return false;
		user_id_out = avatar_it->second;
		return true;
	}
	std::vector<UID> getGameSessionParticipants(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/) const
	{
		std::vector<UID> participants;
		const auto session_it = game_session_participants.find(game_object_uid);
		if(session_it != game_session_participants.end())
			participants.assign(session_it->second.begin(), session_it->second.end());
		return participants;
	}
	void clearGameSessionParticipants(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		game_session_participants.erase(game_object_uid);
		game_session_user_ids.erase(game_object_uid);
		game_hud_menus.erase(game_object_uid);
		game_hud_update_windows.erase(game_object_uid);
		game_session_held_keys.erase(game_object_uid);
		game_session_input_state.erase(game_object_uid);
		game_input_update_windows.erase(game_object_uid);
		game_input_rejected_counts.erase(game_object_uid);
		game_rpc_update_windows.erase(game_object_uid);
		game_rpc_rejected_counts.erase(game_object_uid);
		game_spatial_query_windows.erase(game_object_uid);
		game_aim_query_windows.erase(game_object_uid);
		game_collision_query_windows.erase(game_object_uid);
		game_transform_update_windows.erase(game_object_uid);
		game_property_update_windows.erase(game_object_uid);
		game_material_update_windows.erase(game_object_uid);
		game_overlap_snapshots.erase(game_object_uid);
	}
	bool allowGameRPCMessage(const UID& game_object_uid, const UID& avatar_uid, const std::chrono::steady_clock::time_point& now, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_RPC_MESSAGES_PER_PLAYER_PER_SECOND = 20;
		static const size_t MAX_GAME_RPC_MESSAGES_PER_WORLD_PER_SECOND = 1200;
		if(!game_object_uid.valid() || !avatar_uid.valid()) return false;
		if(game_rpc_global_window.first == std::chrono::steady_clock::time_point() ||
			now - game_rpc_global_window.first >= std::chrono::seconds(1))
			game_rpc_global_window = std::make_pair(now, size_t(0));
		if(game_rpc_global_window.second >= MAX_GAME_RPC_MESSAGES_PER_WORLD_PER_SECOND)
		{
			recordRejectedGameRPCMessage(game_object_uid);
			return false;
		}
		auto& window = game_rpc_update_windows[game_object_uid][avatar_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
			window = std::make_pair(now, 0);
		if(window.second >= MAX_GAME_RPC_MESSAGES_PER_PLAYER_PER_SECOND)
		{
			recordRejectedGameRPCMessage(game_object_uid);
			return false;
		}
		window.second++;
		game_rpc_global_window.second++;
		return true;
	}
	size_t getRejectedGameRPCMessageCount(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/) const
	{
		const auto it = game_rpc_rejected_counts.find(game_object_uid);
		return it == game_rpc_rejected_counts.end() ? 0 : it->second;
	}
	bool allowGameInputUpdate(const UID& game_object_uid, const UID& avatar_uid, const std::chrono::steady_clock::time_point& now, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_INPUT_UPDATES_PER_PLAYER_PER_SECOND = 60;
		static const size_t MAX_GAME_INPUT_UPDATES_PER_WORLD_PER_SECOND = 3600;
		if(!game_object_uid.valid() || !avatar_uid.valid()) return false;
		if(game_input_global_window.first == std::chrono::steady_clock::time_point() ||
			now - game_input_global_window.first >= std::chrono::seconds(1))
			game_input_global_window = std::make_pair(now, size_t(0));
		if(game_input_global_window.second >= MAX_GAME_INPUT_UPDATES_PER_WORLD_PER_SECOND)
		{
			recordRejectedGameInputUpdate(game_object_uid);
			return false;
		}
		auto& window = game_input_update_windows[game_object_uid][avatar_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
			window = std::make_pair(now, 0);
		if(window.second >= MAX_GAME_INPUT_UPDATES_PER_PLAYER_PER_SECOND)
		{
			recordRejectedGameInputUpdate(game_object_uid);
			return false;
		}
		window.second++;
		game_input_global_window.second++;
		return true;
	}
	size_t getRejectedGameInputUpdateCount(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/) const
	{
		const auto it = game_input_rejected_counts.find(game_object_uid);
		return it == game_input_rejected_counts.end() ? 0 : it->second;
	}
	bool allowGameStateUpdate(const UID& game_object_uid, const std::chrono::steady_clock::time_point& now, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_STATE_UPDATES_PER_GAME_PER_SECOND = 10;
		static const size_t MAX_GAME_STATE_UPDATES_PER_WORLD_PER_SECOND = 600;
		if(!game_object_uid.valid()) return false;
		if(game_state_update_global_window.first == std::chrono::steady_clock::time_point() ||
			now - game_state_update_global_window.first >= std::chrono::seconds(1))
		{
			game_state_update_global_window = std::make_pair(now, 0u);
		}
		if(game_state_update_global_window.second >= MAX_GAME_STATE_UPDATES_PER_WORLD_PER_SECOND) return false;
		auto& window = game_state_update_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_GAME_STATE_UPDATES_PER_GAME_PER_SECOND) return false;
		window.second++;
		game_state_update_global_window.second++;
		return true;
	}
	bool allowGameHUDUpdate(const UID& game_object_uid, const std::chrono::steady_clock::time_point& now, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_HUD_UPDATES_PER_GAME_PER_SECOND = 60;
		static const size_t MAX_GAME_HUD_UPDATES_PER_WORLD_PER_SECOND = 1200;
		if(!game_object_uid.valid()) return false;
		auto& window = game_hud_update_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_GAME_HUD_UPDATES_PER_GAME_PER_SECOND) return false;
		if(game_hud_update_global_window.first == std::chrono::steady_clock::time_point() || now - game_hud_update_global_window.first >= std::chrono::seconds(1))
		{
			game_hud_update_global_window.first = now;
			game_hud_update_global_window.second = 0;
		}
		if(game_hud_update_global_window.second >= MAX_GAME_HUD_UPDATES_PER_WORLD_PER_SECOND) return false;
		++window.second;
		++game_hud_update_global_window.second;
		return true;
	}
	bool allocateGameEventSequence(const UID& game_object_uid, size_t recipient_count, const std::chrono::steady_clock::time_point& now, uint32& sequence_out, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_EVENTS_PER_WORLD_PER_SECOND = 1200;
		static const size_t MAX_GAME_EVENT_RECIPIENTS_PER_WORLD_PER_SECOND = 12000;
		if(!game_object_uid.valid()) return false;
		if(recipient_count > 256)
		{
			recordRejectedGameEvent(game_object_uid);
			return false;
		}
		const auto sequence_it = game_event_sequences.find(game_object_uid);
		if(sequence_it != game_event_sequences.end() && sequence_it->second == 0xFFFFFFFFu)
		{
			recordRejectedGameEvent(game_object_uid);
			return false;
		}
		auto window_it = game_event_update_windows.find(game_object_uid);
		if(game_event_global_window.first == std::chrono::steady_clock::time_point() ||
			now - game_event_global_window.first >= std::chrono::seconds(1))
		{
			game_event_global_window = std::make_pair(now, size_t(0));
			game_event_recipients_global_window = std::make_pair(now, size_t(0));
		}
		if(game_event_global_window.second >= MAX_GAME_EVENTS_PER_WORLD_PER_SECOND ||
			recipient_count > MAX_GAME_EVENT_RECIPIENTS_PER_WORLD_PER_SECOND - game_event_recipients_global_window.second)
		{
			recordRejectedGameEvent(game_object_uid);
			return false;
		}
		if(window_it == game_event_update_windows.end() || now - window_it->second.first >= std::chrono::seconds(1))
			game_event_update_windows[game_object_uid] = std::make_pair(now, 0u);
		else if(window_it->second.second >= 20)
		{
			recordRejectedGameEvent(game_object_uid);
			return false;
		}
		auto& game_window = game_event_update_windows[game_object_uid];
		++game_window.second;
		++game_event_global_window.second;
		game_event_recipients_global_window.second += recipient_count;
		uint32& sequence = game_event_sequences[game_object_uid];
		sequence_out = ++sequence;
		return true;
	}
	size_t getRejectedGameEventCount(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/) const
	{
		const auto it = game_event_rejected_counts.find(game_object_uid);
		return it == game_event_rejected_counts.end() ? 0 : it->second;
	}
	bool setGameStateSnapshot(const UID& game_object_uid, uint32 version, const std::vector<uint8>& serialized_value, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_STATE_SNAPSHOT_BYTES_PER_WORLD = 8 * 1024 * 1024;
		if(!game_object_uid.valid())
			return false;
		const auto existing_it = game_state_snapshots.find(game_object_uid);
		const size_t existing_size = existing_it == game_state_snapshots.end() ? 0 : existing_it->second.serialized_value.size();
		if(existing_size > game_state_snapshot_bytes_in_world ||
			serialized_value.size() > MAX_GAME_STATE_SNAPSHOT_BYTES_PER_WORLD ||
			game_state_snapshot_bytes_in_world - existing_size > MAX_GAME_STATE_SNAPSHOT_BYTES_PER_WORLD - serialized_value.size())
			return false;
		GameStateSnapshot snapshot;
		snapshot.version = version;
		snapshot.serialized_value = serialized_value;
		game_state_snapshots[game_object_uid] = snapshot;
		game_state_snapshot_bytes_in_world = game_state_snapshot_bytes_in_world - existing_size + serialized_value.size();
		return true;
	}
	bool getGameStateSnapshot(const UID& game_object_uid, GameStateSnapshot& snapshot_out, WorldStateLock& /*world_state_lock*/) const
	{
		const auto it = game_state_snapshots.find(game_object_uid);
		if(it == game_state_snapshots.end()) return false;
		snapshot_out = it->second;
		return true;
	}
	bool setScriptInteractionPrompt(const UID& object_uid, const ScriptInteractionPrompt& prompt, WorldStateLock& /*world_state_lock*/)
	{
		if(!object_uid.valid() || prompt.text.size() > 128 || prompt.input_mode > 2) return false;
		if(!prompt.visible && prompt.text.empty())
		{
			script_interaction_prompts.erase(object_uid);
			return true;
		}
		static const size_t MAX_PROMPTS_PER_WORLD = 4096;
		if(script_interaction_prompts.count(object_uid) == 0 && script_interaction_prompts.size() >= MAX_PROMPTS_PER_WORLD) return false;
		script_interaction_prompts[object_uid] = prompt;
		return true;
	}
	bool getScriptInteractionPrompt(const UID& object_uid, ScriptInteractionPrompt& prompt_out, WorldStateLock& /*world_state_lock*/) const
	{
		const auto it = script_interaction_prompts.find(object_uid);
		if(it == script_interaction_prompts.end()) return false;
		prompt_out = it->second;
		return true;
	}
	const std::map<UID, ScriptInteractionPrompt>& getScriptInteractionPrompts(WorldStateLock& /*world_state_lock*/) const
	{
		return script_interaction_prompts;
	}
	void clearGameStateSnapshot(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		const auto snapshot_it = game_state_snapshots.find(game_object_uid);
		if(snapshot_it != game_state_snapshots.end())
		{
			game_state_snapshot_bytes_in_world -= snapshot_it->second.serialized_value.size();
			game_state_snapshots.erase(snapshot_it);
		}
		game_state_update_windows.erase(game_object_uid);
		game_event_update_windows.erase(game_object_uid);
		game_event_sequences.erase(game_object_uid);
		game_event_rejected_counts.erase(game_object_uid);
	}
	void setGameHUDMenu(const UID& game_object_uid, const UID& avatar_uid, const GameHUDMenu& menu, WorldStateLock& /*world_state_lock*/)
	{
		if(!game_object_uid.valid() || !avatar_uid.valid() || menu.panel_id.empty())
			return;
		auto& panels = game_hud_menus[game_object_uid][avatar_uid];
		if(menu.actions.empty() && menu.progress_bars.empty() && menu.text_widgets.empty())
		{
			panels.erase(menu.panel_id);
			if(panels.empty())
			{
				game_hud_menus[game_object_uid].erase(avatar_uid);
				if(game_hud_menus[game_object_uid].empty()) game_hud_menus.erase(game_object_uid);
			}
		}
		else
		{
			static const size_t MAX_PANELS_PER_PLAYER = 4;
			if(panels.count(menu.panel_id) == 0 && panels.size() >= MAX_PANELS_PER_PLAYER)
				return;
			panels[menu.panel_id] = menu;
		}
	}
	bool getGameHUDMenu(const UID& game_object_uid, const UID& avatar_uid, const std::string& panel_id, GameHUDMenu& menu_out, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_hud_menus.find(game_object_uid);
		if(game_it == game_hud_menus.end())
			return false;
		const auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end())
			return false;
		const auto panel_it = avatar_it->second.find(panel_id);
		if(panel_it == avatar_it->second.end()) return false;
		menu_out = panel_it->second;
		return true;
	}
	void getGameHUDMenus(const UID& game_object_uid, const UID& avatar_uid, std::vector<GameHUDMenu>& menus_out, WorldStateLock& /*world_state_lock*/) const
	{
		menus_out.clear();
		const auto game_it = game_hud_menus.find(game_object_uid);
		if(game_it == game_hud_menus.end()) return;
		const auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end()) return;
		for(const auto& panel : avatar_it->second) menus_out.push_back(panel.second);
	}
	void clearGameHUDMenus(const UID& game_object_uid, const UID& avatar_uid, WorldStateLock& /*world_state_lock*/)
	{
		auto game_it = game_hud_menus.find(game_object_uid);
		if(game_it == game_hud_menus.end()) return;
		game_it->second.erase(avatar_uid);
		if(game_it->second.empty()) game_hud_menus.erase(game_it);
	}
	bool isGameHUDActionAllowed(const UID& game_object_uid, const UID& avatar_uid, const std::string& action_id, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_hud_menus.find(game_object_uid);
		if(game_it == game_hud_menus.end())
			return false;
		const auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end())
			return false;
		for(const auto& panel : avatar_it->second)
			for(const GameHUDMenuAction& action : panel.second.actions)
				if(action.id == action_id && panel.second.fields.empty())
					return true;
		return false;
	}
	bool isGameHUDFormSubmitAllowed(const UID& game_object_uid, const UID& avatar_uid, const std::string& action_id,
		const std::vector<GameHUDFormValue>& values, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_hud_menus.find(game_object_uid);
		if(game_it == game_hud_menus.end()) return false;
		const auto avatar_it = game_it->second.find(avatar_uid);
		if(avatar_it == game_it->second.end()) return false;
		for(const auto& panel : avatar_it->second)
		{
			bool has_action = false;
			for(const GameHUDMenuAction& action : panel.second.actions)
				has_action = has_action || action.id == action_id;
			if(!has_action || panel.second.fields.empty() || panel.second.fields.size() != values.size()) continue;
			bool matches = true;
			for(const GameHUDFormField& field : panel.second.fields)
			{
				const GameHUDFormValue* matched_value = NULL;
				for(const GameHUDFormValue& value : values)
					if(value.id == field.id)
					{
						if(matched_value) { matches = false; break; }
						matched_value = &value;
					}
				if(!matched_value || matched_value->value.size() > 256 || (field.required && matched_value->value.empty()))
					matches = false;
				if(matched_value && field.type == GameHUDFormField::Select &&
					std::find(field.options.begin(), field.options.end(), matched_value->value) == field.options.end())
					matches = false;
				if(matched_value && field.type == GameHUDFormField::Number && !matched_value->value.empty())
				{
					char* end = NULL;
					const double number = std::strtod(matched_value->value.c_str(), &end);
					if(end == matched_value->value.c_str() || end != matched_value->value.c_str() + matched_value->value.size() ||
						!std::isfinite(number) || number < field.minimum || number > field.maximum)
						matches = false;
				}
				if(matched_value && field.type == GameHUDFormField::Checkbox &&
					((matched_value->value != "true" && matched_value->value != "false") || (field.required && matched_value->value != "true")))
					matches = false;
			}
			if(matches) return true;
		}
		return false;
	}
	bool addGameSpawnedObject(const UID& game_object_uid, const UID& object_uid, WorldStateLock& world_state_lock)
	{
		return addGameSpawnedObjects(game_object_uid, std::vector<UID>(1, object_uid), world_state_lock);
	}
	bool canAddGameSpawnedObjects(const UID& game_object_uid, size_t count, WorldStateLock& /*world_state_lock*/) const
	{
		static const size_t MAX_OBJECTS_PER_GAME = 1024;
		static const size_t MAX_OBJECTS_PER_WORLD = 32768;
		static const size_t MAX_OBJECTS_PER_GAME_PER_SECOND = 128;
		static const size_t MAX_OBJECTS_PER_WORLD_PER_SECOND = 1024;
		if(!game_object_uid.valid() || count == 0 || count > MAX_OBJECTS_PER_GAME_PER_SECOND)
			return false;
		if(game_spawned_object_count_in_world > MAX_OBJECTS_PER_WORLD ||
			count > MAX_OBJECTS_PER_WORLD - game_spawned_object_count_in_world)
			return false;
		const auto game_it = game_spawned_objects.find(game_object_uid);
		const size_t current_game_count = game_it == game_spawned_objects.end() ? 0 : game_it->second.size();
		if(count > MAX_OBJECTS_PER_GAME - current_game_count)
			return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const auto rate_it = game_spawn_rate_windows.find(game_object_uid);
		if(rate_it != game_spawn_rate_windows.end() &&
			now - rate_it->second.first < std::chrono::seconds(1) &&
			rate_it->second.second + count > MAX_OBJECTS_PER_GAME_PER_SECOND)
			return false;
		if(game_spawn_world_rate_window.first != std::chrono::steady_clock::time_point() &&
			now - game_spawn_world_rate_window.first < std::chrono::seconds(1) &&
			count > MAX_OBJECTS_PER_WORLD_PER_SECOND - game_spawn_world_rate_window.second)
			return false;
		return true;
	}
	bool addGameSpawnedObjects(const UID& game_object_uid, const std::vector<UID>& object_uids, WorldStateLock& world_state_lock)
	{
		static const size_t MAX_OBJECTS_PER_GAME_PER_SECOND = 128;
		if(!game_object_uid.valid() || object_uids.empty())
			return false;
		std::set<UID> unique_ids;
		for(const UID& object_uid : object_uids)
			if(!object_uid.valid() || !unique_ids.insert(object_uid).second)
				return false;
		if(!canAddGameSpawnedObjects(game_object_uid, object_uids.size(), world_state_lock))
			return false;
		auto game_it = game_spawned_objects.find(game_object_uid);
		if(game_it != game_spawned_objects.end())
		{
			for(const UID& object_uid : object_uids)
				if(game_it->second.find(object_uid) != game_it->second.end())
					return false;
		}
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& rate_window = game_spawn_rate_windows[game_object_uid];
		if(rate_window.first == std::chrono::steady_clock::time_point() || now - rate_window.first >= std::chrono::seconds(1))
		{
			rate_window.first = now;
			rate_window.second = 0;
		}
		if(rate_window.second + object_uids.size() > MAX_OBJECTS_PER_GAME_PER_SECOND)
			return false;
		static const size_t MAX_OBJECTS_PER_WORLD_PER_SECOND = 1024;
		auto& world_rate_window = game_spawn_world_rate_window;
		if(world_rate_window.first == std::chrono::steady_clock::time_point() || now - world_rate_window.first >= std::chrono::seconds(1))
		{
			world_rate_window.first = now;
			world_rate_window.second = 0;
		}
		if(object_uids.size() > MAX_OBJECTS_PER_WORLD_PER_SECOND - world_rate_window.second)
			return false;
		auto& objects_for_game = game_spawned_objects[game_object_uid];
		objects_for_game.insert(unique_ids.begin(), unique_ids.end());
		game_spawned_object_count_in_world += unique_ids.size();
		for(const UID& object_uid : unique_ids)
		{
			const auto object_it = objects.find(object_uid);
			if(object_it != objects.end()) game_ray_spatial_index.updateObject(*object_it->second);
		}
		rate_window.second += object_uids.size();
		world_rate_window.second += object_uids.size();
		return true;
	}
	bool canAddGameSpawnedVoxelCells(const UID& game_object_uid, size_t cell_count, WorldStateLock& /*world_state_lock*/) const
	{
		static const size_t MAX_VOXEL_CELLS_PER_GAME = 65536;
		static const size_t MAX_VOXEL_CELLS_PER_WORLD = 1048576;
		if(!game_object_uid.valid() || cell_count == 0 ||
			game_spawned_voxel_cells_in_world + cell_count > MAX_VOXEL_CELLS_PER_WORLD)
			return false;
		const auto game_it = game_spawned_voxel_cells.find(game_object_uid);
		const size_t current = game_it == game_spawned_voxel_cells.end() ? 0 : game_it->second;
		return current + cell_count <= MAX_VOXEL_CELLS_PER_GAME;
	}
	bool addGameSpawnedVoxelObjects(const UID& game_object_uid, const std::vector<std::pair<UID, size_t>>& object_cell_counts, WorldStateLock& world_state_lock)
	{
		if(!game_object_uid.valid() || object_cell_counts.empty())
			return false;
		std::vector<UID> object_uids;
		std::set<UID> unique_ids;
		size_t total_cells = 0;
		for(const auto& entry : object_cell_counts)
		{
			if(!entry.first.valid() || !unique_ids.insert(entry.first).second)
				return false;
			if(total_cells > std::numeric_limits<size_t>::max() - entry.second)
				return false;
			total_cells += entry.second;
			object_uids.push_back(entry.first);
		}
		if((total_cells > 0 && !canAddGameSpawnedVoxelCells(game_object_uid, total_cells, world_state_lock)) ||
			!addGameSpawnedObjects(game_object_uid, object_uids, world_state_lock))
			return false;
		for(const auto& entry : object_cell_counts)
		{
			if(entry.second == 0) continue;
			game_spawned_voxel_cells[game_object_uid] += entry.second;
			game_spawned_voxel_object_cells[entry.first] = std::make_pair(game_object_uid, entry.second);
		}
		game_spawned_voxel_cells_in_world += total_cells;
		return true;
	}
	bool addGameSpawnedVoxelObject(const UID& game_object_uid, const UID& object_uid, size_t cell_count, WorldStateLock& world_state_lock)
	{
		return addGameSpawnedVoxelObjects(game_object_uid, std::vector<std::pair<UID, size_t>>(1, std::make_pair(object_uid, cell_count)), world_state_lock);
	}
	void removeGameSpawnedObject(const UID& game_object_uid, const UID& object_uid, WorldStateLock& /*world_state_lock*/)
	{
		auto game_it = game_spawned_objects.find(game_object_uid);
		if(game_it == game_spawned_objects.end() || game_it->second.find(object_uid) == game_it->second.end())
			return;
		game_ray_spatial_index.removeObject(object_uid);
		const auto voxel_it = game_spawned_voxel_object_cells.find(object_uid);
		if(voxel_it != game_spawned_voxel_object_cells.end() && voxel_it->second.first == game_object_uid)
		{
			const size_t cell_count = voxel_it->second.second;
			auto cells_it = game_spawned_voxel_cells.find(game_object_uid);
			if(cells_it != game_spawned_voxel_cells.end())
			{
				cells_it->second = cells_it->second >= cell_count ? cells_it->second - cell_count : 0;
				if(cells_it->second == 0) game_spawned_voxel_cells.erase(cells_it);
			}
			game_spawned_voxel_cells_in_world = game_spawned_voxel_cells_in_world >= cell_count ? game_spawned_voxel_cells_in_world - cell_count : 0;
			game_spawned_voxel_object_cells.erase(voxel_it);
		}
		if(game_it->second.erase(object_uid) > 0 && game_spawned_object_count_in_world > 0)
			game_spawned_object_count_in_world--;
		if(game_it->second.empty())
			game_spawned_objects.erase(game_it);
		auto snapshots_it = game_overlap_snapshots.find(game_object_uid);
		if(snapshots_it != game_overlap_snapshots.end())
		{
			snapshots_it->second.erase(object_uid);
			for(auto snapshot_it = snapshots_it->second.begin(); snapshot_it != snapshots_it->second.end(); )
			{
				snapshot_it->second.erase(object_uid);
				if(snapshot_it->second.empty()) snapshot_it = snapshots_it->second.erase(snapshot_it);
				else ++snapshot_it;
			}
			if(snapshots_it->second.empty()) game_overlap_snapshots.erase(snapshots_it);
		}
	}
	std::vector<UID> takeGameSpawnedObjects(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		std::vector<UID> object_uids;
		auto game_it = game_spawned_objects.find(game_object_uid);
		if(game_it != game_spawned_objects.end())
		{
			object_uids.assign(game_it->second.begin(), game_it->second.end());
			for(const UID& object_uid : object_uids) game_ray_spatial_index.removeObject(object_uid);
			game_spawned_object_count_in_world = game_spawned_object_count_in_world >= game_it->second.size() ?
				game_spawned_object_count_in_world - game_it->second.size() : 0;
			game_spawned_objects.erase(game_it);
		}
		for(auto it = game_spawned_voxel_object_cells.begin(); it != game_spawned_voxel_object_cells.end(); )
			if(it->second.first == game_object_uid)
			{
				game_spawned_voxel_cells_in_world = game_spawned_voxel_cells_in_world >= it->second.second ?
					game_spawned_voxel_cells_in_world - it->second.second : 0;
				it = game_spawned_voxel_object_cells.erase(it);
			}
			else ++it;
		game_spawned_voxel_cells.erase(game_object_uid);
		game_spawn_rate_windows.erase(game_object_uid);
		game_spatial_query_windows.erase(game_object_uid);
		game_aim_query_windows.erase(game_object_uid);
		game_collision_query_windows.erase(game_object_uid);
		game_transform_update_windows.erase(game_object_uid);
		game_property_update_windows.erase(game_object_uid);
		game_material_update_windows.erase(game_object_uid);
		game_overlap_snapshots.erase(game_object_uid);
		return object_uids;
	}
	size_t getGameSpawnedObjectCount(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/) const
	{
		const auto game_it = game_spawned_objects.find(game_object_uid);
		return game_it == game_spawned_objects.end() ? 0 : game_it->second.size();
	}
	std::vector<UID> getGameSpawnedObjectUIDs(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/) const
	{
		std::vector<UID> object_uids;
		const auto game_it = game_spawned_objects.find(game_object_uid);
		if(game_it != game_spawned_objects.end())
			object_uids.assign(game_it->second.begin(), game_it->second.end());
		return object_uids;
	}
	bool allowGameAimQuery(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_AIM_QUERIES_PER_GAME_PER_SECOND = 10;
		static const size_t MAX_WORLD_OBJECT_SCANS_PER_SECOND = 200;
		if(!game_object_uid.valid()) return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& window = game_aim_query_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_AIM_QUERIES_PER_GAME_PER_SECOND) return false;
		if(game_world_scan_global_window.first == std::chrono::steady_clock::time_point() || now - game_world_scan_global_window.first >= std::chrono::seconds(1))
		{
			game_world_scan_global_window.first = now;
			game_world_scan_global_window.second = 0;
		}
		if(game_world_scan_global_window.second >= MAX_WORLD_OBJECT_SCANS_PER_SECOND) return false;
		++window.second;
		++game_world_scan_global_window.second;
		return true;
	}
	bool allowGameCollisionQuery(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		// Collision sweeps run in the bounded fixed-step game loop, so permit
		// one query per maximum-rate tick while retaining the shared world scan cap.
		static const size_t MAX_COLLISION_QUERIES_PER_GAME_PER_SECOND = 30;
		static const size_t MAX_WORLD_OBJECT_SCANS_PER_SECOND = 200;
		if(!game_object_uid.valid()) return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& window = game_collision_query_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_COLLISION_QUERIES_PER_GAME_PER_SECOND) return false;
		if(game_world_scan_global_window.first == std::chrono::steady_clock::time_point() || now - game_world_scan_global_window.first >= std::chrono::seconds(1))
		{
			game_world_scan_global_window.first = now;
			game_world_scan_global_window.second = 0;
		}
		if(game_world_scan_global_window.second >= MAX_WORLD_OBJECT_SCANS_PER_SECOND) return false;
		++window.second;
		++game_world_scan_global_window.second;
		return true;
	}
	bool allowGameObjectTransformUpdate(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		// Supports roughly twenty moving objects at 30 Hz for a game while
		// bounding the number of transform broadcasts a script can generate.
		static const size_t MAX_GAME_TRANSFORM_UPDATES_PER_GAME_PER_SECOND = 600;
		static const size_t MAX_GAME_TRANSFORM_UPDATES_PER_WORLD_PER_SECOND = 12000;
		if(!game_object_uid.valid()) return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& window = game_transform_update_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_GAME_TRANSFORM_UPDATES_PER_GAME_PER_SECOND) return false;
		if(game_transform_update_global_window.first == std::chrono::steady_clock::time_point() || now - game_transform_update_global_window.first >= std::chrono::seconds(1))
		{
			game_transform_update_global_window.first = now;
			game_transform_update_global_window.second = 0;
		}
		if(game_transform_update_global_window.second >= MAX_GAME_TRANSFORM_UPDATES_PER_WORLD_PER_SECOND) return false;
		++window.second;
		++game_transform_update_global_window.second;
		return true;
	}
	bool allowGameObjectMaterialUpdate(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_MATERIAL_UPDATES_PER_GAME_PER_SECOND = 600;
		static const size_t MAX_GAME_MATERIAL_UPDATES_PER_WORLD_PER_SECOND = 12000;
		if(!game_object_uid.valid()) return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& window = game_material_update_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_GAME_MATERIAL_UPDATES_PER_GAME_PER_SECOND) return false;
		if(game_material_update_global_window.first == std::chrono::steady_clock::time_point() || now - game_material_update_global_window.first >= std::chrono::seconds(1))
		{
			game_material_update_global_window.first = now;
			game_material_update_global_window.second = 0;
		}
		if(game_material_update_global_window.second >= MAX_GAME_MATERIAL_UPDATES_PER_WORLD_PER_SECOND) return false;
		++window.second;
		++game_material_update_global_window.second;
		return true;
	}
	bool allowGameObjectPropertyUpdate(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_GAME_PROPERTY_UPDATES_PER_GAME_PER_SECOND = 600;
		static const size_t MAX_GAME_PROPERTY_UPDATES_PER_WORLD_PER_SECOND = 12000;
		if(!game_object_uid.valid()) return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& window = game_property_update_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_GAME_PROPERTY_UPDATES_PER_GAME_PER_SECOND) return false;
		if(game_property_update_global_window.first == std::chrono::steady_clock::time_point() || now - game_property_update_global_window.first >= std::chrono::seconds(1))
		{
			game_property_update_global_window.first = now;
			game_property_update_global_window.second = 0;
		}
		if(game_property_update_global_window.second >= MAX_GAME_PROPERTY_UPDATES_PER_WORLD_PER_SECOND) return false;
		++window.second;
		++game_property_update_global_window.second;
		return true;
	}
	bool allowGameSpatialQuery(const UID& game_object_uid, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_SPATIAL_QUERIES_PER_GAME_PER_SECOND = 60;
		static const size_t MAX_SPATIAL_QUERIES_PER_WORLD_PER_SECOND = 600;
		if(!game_object_uid.valid()) return false;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		auto& window = game_spatial_query_windows[game_object_uid];
		if(window.first == std::chrono::steady_clock::time_point() || now - window.first >= std::chrono::seconds(1))
		{
			window.first = now;
			window.second = 0;
		}
		if(window.second >= MAX_SPATIAL_QUERIES_PER_GAME_PER_SECOND) return false;
		if(game_spatial_query_global_window.first == std::chrono::steady_clock::time_point() || now - game_spatial_query_global_window.first >= std::chrono::seconds(1))
		{
			game_spatial_query_global_window.first = now;
			game_spatial_query_global_window.second = 0;
		}
		if(game_spatial_query_global_window.second >= MAX_SPATIAL_QUERIES_PER_WORLD_PER_SECOND) return false;
		++window.second;
		++game_spatial_query_global_window.second;
		return true;
	}
	bool updateGameOverlapSnapshot(const UID& game_object_uid, const UID& target_uid,
		const std::vector<UID>& current_overlaps, GameOverlapChangeSet& changes_out, WorldStateLock& /*world_state_lock*/)
	{
		static const size_t MAX_OVERLAP_SNAPSHOT_PAIRS_PER_GAME = 16384;
		std::set<UID> current(current_overlaps.begin(), current_overlaps.end());
		auto game_it = game_overlap_snapshots.find(game_object_uid);
		size_t previous_size = 0;
		size_t total_pairs = 0;
		if(game_it != game_overlap_snapshots.end())
		{
			for(const auto& snapshot : game_it->second) total_pairs += snapshot.second.size();
			const auto target_it = game_it->second.find(target_uid);
			if(target_it != game_it->second.end()) previous_size = target_it->second.size();
		}
		if(total_pairs - previous_size + current.size() > MAX_OVERLAP_SNAPSHOT_PAIRS_PER_GAME)
			return false;
		std::set<UID>& previous = game_overlap_snapshots[game_object_uid][target_uid];
		for(const UID& uid : current)
			if(previous.find(uid) == previous.end()) changes_out.entered.push_back(uid);
		for(const UID& uid : previous)
			if(current.find(uid) == current.end()) changes_out.exited.push_back(uid);
		previous.swap(current);
		return true;
	}
	
	ParcelMapType parcels; // TODO: make private.  Lots of compile errors to fix when doing so.

	DirtyFromRemoteObjectSetType&                           getDirtyFromRemoteObjects(WorldStateLock& /*world_state_lock*/) { return dirty_from_remote_objects; }
	std::unordered_set<WorldObjectRef, WorldObjectRefHash>& getDBDirtyWorldObjects(WorldStateLock& /*world_state_lock*/) { return db_dirty_world_objects; }
	std::unordered_set<ParcelRef, ParcelRefHash>&           getDBDirtyParcels(WorldStateLock& /*world_state_lock*/) { return db_dirty_parcels; }
	std::unordered_set<LODChunkRef, LODChunkRefHash>&       getDBDirtyLODChunks(WorldStateLock& /*world_state_lock*/) { return db_dirty_lod_chunks; }
	std::unordered_set<ChatBotRef, ChatBotRefHash>&         getDBDirtyChatBots(WorldStateLock& /*world_state_lock*/) { return db_dirty_chatbots; }

	AvatarRef createAndInsertAvatarForChatBot(ServerAllWorldsState* all_world_state, const ChatBot* chatbot, WorldStateLock& /*world_state_lock*/);
private:
	void recordRejectedGameRPCMessage(const UID& game_object_uid)
	{
		size_t& count = game_rpc_rejected_counts[game_object_uid];
		if(count < std::numeric_limits<size_t>::max()) count++;
	}
	void recordRejectedGameInputUpdate(const UID& game_object_uid)
	{
		size_t& count = game_input_rejected_counts[game_object_uid];
		if(count < std::numeric_limits<size_t>::max()) count++;
	}
	void recordRejectedGameEvent(const UID& game_object_uid)
	{
		size_t& count = game_event_rejected_counts[game_object_uid];
		if(count < std::numeric_limits<size_t>::max()) count++;
	}
	ObjectMapType objects;
	GameRaySpatialIndex game_ray_spatial_index;
	DirtyFromRemoteObjectSetType dirty_from_remote_objects; // TODO: could just use vector for this, and avoid duplicates by checking object dirty flag.
	AvatarMapType avatars;
	LODChunkMapType lod_chunks;
	ChatBotMapType chatbots;
	std::map<UID, std::set<UID>> game_session_participants; // Ephemeral active Lua game-session membership, guarded by ServerAllWorldsState::mutex.
	std::map<UID, std::map<UID, UserID>> game_session_user_ids; // Authenticated account identity for each active player; avatar UIDs are session-scoped.
	std::map<UID, std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>>> game_input_update_windows; // Per-player/game input snapshot ingress rate limits.
	std::map<UID, size_t> game_input_rejected_counts; // Saturating per-game count exposed through authorised script diagnostics.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_input_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::map<UID, std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>>> game_rpc_update_windows; // Per-session-participant RPC ingress rate limits.
	std::map<UID, size_t> game_rpc_rejected_counts; // Saturating per-game count exposed through authorised script diagnostics.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_rpc_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::map<UID, std::map<UID, std::set<std::string>>> game_session_held_keys; // Server-authoritative pressed-key state, cleared on player/session teardown.
	std::map<UID, std::map<UID, GameInputState>> game_session_input_state; // Latest validated pointer/analog state plus bounded one-shot wheel deltas.
	std::map<UID, std::map<UID, std::map<std::string, GameHUDMenu>>> game_hud_menus; // Per-game/player/panel action allowlist, guarded by ServerAllWorldsState::mutex.
	std::map<UID, ScriptInteractionPrompt> script_interaction_prompts; // Ephemeral prompts; scripts initialize and update them as world state changes.
	std::map<UID, GameStateSnapshot> game_state_snapshots; // Latest transient authoritative snapshot for active games and late joiners.
	size_t game_state_snapshot_bytes_in_world = 0; // Aggregate snapshot budget, updated under the world mutex.
	std::map<UID, std::set<UID>> script_proximity_objects; // Verified per-avatar near state, guarded by world mutex.
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, uint32>> game_state_update_windows; // Per-game state broadcast limit.
	std::pair<std::chrono::steady_clock::time_point, uint32> game_state_update_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0u);
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_hud_update_windows; // Bound per-game HUD/menu broadcasts.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_hud_update_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, uint32>> game_event_update_windows; // Bounded per-game reliable event budget.
	std::map<UID, uint32> game_event_sequences; // Monotonic event sequence for each live game object.
	std::map<UID, size_t> game_event_rejected_counts; // Saturating per-game count exposed through authorised script diagnostics.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_event_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::pair<std::chrono::steady_clock::time_point, size_t> game_event_recipients_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::map<UID, std::set<UID>> game_spawned_objects; // Transient, non-persistent objects owned by each Lua game object.
	std::map<UID, size_t> game_spawned_voxel_cells; // Per-game transient voxel geometry budget.
	std::map<UID, std::pair<UID, size_t>> game_spawned_voxel_object_cells; // Cell count for each transient voxel object, used during cleanup.
	size_t game_spawned_voxel_cells_in_world = 0;
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_spawn_rate_windows; // Per-game create budget, including objects destroyed within the window.
	size_t game_spawned_object_count_in_world = 0; // Aggregate transient game-object count, updated with the ownership sets.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_spawn_world_rate_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_spatial_query_windows; // Shared per-game spatial query budget.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_spatial_query_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0); // Aggregate spatial-query cap across games in a world.
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_aim_query_windows; // More restrictive budget for world-wide occlusion rays.
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_collision_query_windows; // Fixed-step collision sweep budget.
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_transform_update_windows; // Bound per-game transform broadcasts.
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_property_update_windows; // Bound per-game property broadcasts.
	std::map<UID, std::pair<std::chrono::steady_clock::time_point, size_t>> game_material_update_windows; // Bound per-game material broadcasts.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_world_scan_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0); // Shared by aim and collision scans.
	std::pair<std::chrono::steady_clock::time_point, size_t> game_transform_update_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::pair<std::chrono::steady_clock::time_point, size_t> game_property_update_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::pair<std::chrono::steady_clock::time_point, size_t> game_material_update_global_window = std::make_pair(std::chrono::steady_clock::time_point(), 0);
	std::map<UID, std::map<UID, std::set<UID>>> game_overlap_snapshots; // Per-game-object overlap state for server-side enter/exit changes.

	std::unordered_set<WorldObjectRef, WorldObjectRefHash>	db_dirty_world_objects;
	std::unordered_set<ParcelRef, ParcelRefHash>			db_dirty_parcels;
	std::unordered_set<LODChunkRef, LODChunkRefHash>		db_dirty_lod_chunks;
	std::unordered_set<ChatBotRef, ChatBotRefHash>			db_dirty_chatbots;
};

typedef Reference<ServerWorldState> ServerWorldStateRef;

struct ServerWorldStateRefHash
{
	size_t operator() (const ServerWorldStateRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};

void readServerWorldStateFromStream(RandomAccessInStream& stream, ServerWorldState& world);


/*=====================================================================
ServerAllWorldsState
--------------------

=====================================================================*/
class ServerAllWorldsState : public ThreadSafeRefCounted
{
public:
	ServerAllWorldsState();
	~ServerAllWorldsState();

	void readFromDisk(const std::string& path);
	void createNewDatabase(const std::string& path);
	void registerLuaVMForUser(const UserID& user_id, const Reference<SubstrataLuaVM>& vm) REQUIRES(mutex);
	void serialiseToDisk(WorldStateLock& lock) REQUIRES(mutex); // Write any changed data (objects in dirty set) to disk.  Mutex should be held already.
	void denormaliseData(); // Build/update cached/denormalised fields like creator_name.
	void doMigrations(WorldStateLock& lock) REQUIRES(mutex);
	void ensureMetasiberiaServiceWorlds(WorldStateLock& lock) REQUIRES(mutex);
	size_t reconcileResourcePresenceWithDisk() REQUIRES(mutex);

	// Removes sensitive information from the database, such as user passwords, email addresses, billing information, web sessions etc.
	// Then saves the updates to disk.
	void saveSanitisedDatabase();
	static void test();

	bool credentialExists(const std::string& key);
	std::string getCredential(const std::string& key); // Throws glare::Exception if not found

	UID getNextObjectUID(); // Gets and then increments next_object_uid.  Locks mutex.
	UID getNextAvatarUID(); // Gets and then increments next_avatar_uid.  Locks mutex.
	uint64 getNextOrderUID(); // Gets and then increments next_order_uid.  Locks mutex.
	uint64 getNextSubEthTransactionUID();
	uint64 getNextScreenshotUID();
	uint64 getNextScreenshotUIDUnlocked(WorldStateLock& /*lock*/) REQUIRES(mutex); // For callers already holding mutex.
	uint64 getNextNewsPostUID();
	uint64 getNextEventUID();
	uint64 getNextPhotoUID();
	uint64 getNextChatBotUID();
	UID getNextGearItemUID();

	ObjectStorageItemRef getOrCreateObjectStorageItem(const ObjectStorageKey& key) REQUIRES(mutex); // Throws exception if too many items for object already created.
	void clearObjectStorageItems() REQUIRES(mutex); // Just for tests
	bool allowGameStorageWrite(const UID& game_object_uid, const UserID& user_id, std::chrono::steady_clock::time_point now) REQUIRES(mutex)
	{
		static const uint32 MAX_WRITES_PER_SECOND = 10;
		GameStorageWriteWindow& window = game_storage_write_windows[std::make_pair(game_object_uid, user_id)];
		if(window.window_start == std::chrono::steady_clock::time_point{} || now - window.window_start >= std::chrono::seconds(1))
		{
			window.window_start = now;
			window.num_writes = 0;
		}
		if(window.num_writes >= MAX_WRITES_PER_SECOND)
			return false;
		window.num_writes++;
		return true;
	}
	void clearGameStorageWriteWindow(const UID& game_object_uid, const UserID& user_id) REQUIRES(mutex)
	{
		game_storage_write_windows.erase(std::make_pair(game_object_uid, user_id));
	}
	void clearGameStorageWriteWindowsForObject(const UID& game_object_uid) REQUIRES(mutex)
	{
		for(auto it = game_storage_write_windows.begin(); it != game_storage_write_windows.end(); )
			if(it->first.first == game_object_uid)
				it = game_storage_write_windows.erase(it);
			else
				++it;
	}

	void markAsChanged() { changed = 1; }
	void clearChangedFlag() { changed = 0; }
	bool hasChanged() const { return changed != 0; }

	void setUserWebMessage(const UserID& user_id, const std::string& s);
	std::string getAndRemoveUserWebMessage(const UserID& user_id); // returns empty string if no message or user

	inline Reference<ServerWorldState> getRootWorldState(); // Guaranteed to return a non-null reference


	void addResourceAsDBDirty(const ResourceRef resource)					REQUIRES(mutex) { db_dirty_resources.insert(resource); changed = 1; }
	void addSubEthTransactionAsDBDirty(const SubEthTransactionRef trans)	REQUIRES(mutex) { db_dirty_sub_eth_transactions.insert(trans); changed = 1; }
	void addOrderAsDBDirty(const OrderRef order)							REQUIRES(mutex) { db_dirty_orders.insert(order); changed = 1; }
	void addParcelAuctionAsDBDirty(const ParcelAuctionRef parcel_auction)	REQUIRES(mutex) { db_dirty_parcel_auctions.insert(parcel_auction); changed = 1; }
	void addUserWebSessionAsDBDirty(const UserWebSessionRef screenshot)		REQUIRES(mutex) { db_dirty_userwebsessions.insert(screenshot); changed = 1; }
	void addScreenshotAsDBDirty(const ScreenshotRef screenshot)				REQUIRES(mutex) { db_dirty_screenshots.insert(screenshot); changed = 1; }
	void addPhotoAsDBDirty(const PhotoRef photo)							REQUIRES(mutex) { db_dirty_photos.insert(photo); changed = 1; }
	void addUserAsDBDirty(const UserRef user)								REQUIRES(mutex) { db_dirty_users.insert(user); changed = 1; }
	void addNewsPostAsDBDirty(const NewsPostRef post)						REQUIRES(mutex) { db_dirty_news_posts.insert(post); changed = 1; }
	void addEventAsDBDirty(const SubEventRef event)							REQUIRES(mutex) { db_dirty_events.insert(event); changed = 1; }
	void addGearItemAsDBDirty(const GearItemRef item)						REQUIRES(mutex) { db_dirty_gear_items.insert(item); changed = 1; }

	void addEverythingToDirtySets();

	bool isInReadOnlyMode();

	void addAdminAuditLogEntry(const UserID& user_id, const std::string& username, const std::string& action) REQUIRES(mutex);

	void clearAndReset(); // Just for fuzzing

	void addPersonalWorldForUser(const UserRef user, WorldStateLock& lock) REQUIRES(mutex);

	Reference<ResourceManager> resource_manager;

	std::map<UserID, Reference<User>> user_id_to_users GUARDED_BY(mutex);  // User id to user
	std::map<std::string, Reference<User>> name_to_users GUARDED_BY(mutex); // Username to user

	std::map<uint64, OrderRef> orders GUARDED_BY(mutex); // Order ID to order

	std::map<std::string, Reference<ServerWorldState> > world_states GUARDED_BY(mutex); // ServerWorldState contains WorldObjects and Parcels
	Reference<ServerWorldState> root_world_state; // = world_states[""].  Not guarded by mutex as should be set only once during load from disk, init, or while mutex is held.

	std::map<std::string, UserWebSessionRef> user_web_sessions GUARDED_BY(mutex); // Map from key to UserWebSession
	
	std::map<uint32, ParcelAuctionRef> parcel_auctions GUARDED_BY(mutex); // ParcelAuction id to ParcelAuction

	std::map<uint64, ScreenshotRef> screenshots GUARDED_BY(mutex); // Screenshot id to ScreenshotRef

	std::map<uint64, PhotoRef> photos GUARDED_BY(mutex); // Photo id to PhotoRef

	std::map<UID, GearItemRef> gear_items GUARDED_BY(mutex); // GearItem id to GearItem

	std::map<uint64, SubEthTransactionRef> sub_eth_transactions GUARDED_BY(mutex); // SubEthTransaction id to SubEthTransaction

	std::map<uint64, NewsPostRef> news_posts GUARDED_BY(mutex); // NewsPost id to NewsPost

	std::map<ObjectStorageKey, ObjectStorageItemRef> object_storage_items GUARDED_BY(mutex);
	std::unordered_map<UID, uint64, UIDHasher> object_num_storage_items GUARDED_BY(mutex); // Map from object UID to number of storage items for object.
	struct GameStorageWriteWindow
	{
		std::chrono::steady_clock::time_point window_start{};
		uint32 num_writes = 0;
	};
	std::map<std::pair<UID, UserID>, GameStorageWriteWindow> game_storage_write_windows GUARDED_BY(mutex);

	std::map<UserSecretKey, UserSecretRef> user_secrets GUARDED_BY(mutex);

	std::map<uint64, SubEventRef> events GUARDED_BY(mutex); // SubEvent id to SubEvent

	HashMap<UserID, Reference<SubstrataLuaVM>, UserIDHasher> lua_vms GUARDED_BY(mutex);

	// For the map:
	MapTileInfo& getMapTileInfoForWorld(const std::string& world_name, WorldStateLock& /*world_state_lock*/) REQUIRES(mutex);
	const MapTileInfo& getMapTileInfoForWorld(const std::string& world_name, WorldStateLock& /*world_state_lock*/) const REQUIRES(mutex);

	std::map<std::string, MapTileInfo> map_tile_info_for_world GUARDED_BY(mutex); // Key: world name, empty string for root.
	std::deque<PendingMapTileScreenshot> pending_map_tile_screenshots GUARDED_BY(mutex);

	LastParcelUpdateInfo last_parcel_update_info;

	EthInfo eth_info;

	MigrationVersionInfo migration_version_info;

	static const uint64 SERVER_SCRIPT_EXEC_FEATURE_FLAG                   = 1; // Is server-side script execution enabled?
	static const uint64 LUA_HTTP_REQUESTS_FEATURE_FLAG                    = 2; // Are Lua-initiated HTTP requests enabled?
	static const uint64 DO_WORLD_MAINTENANCE_FEATURE_FLAG                 = 4; // Should world maintenance tasks run? (e.g. WorldMaintenance::removeOldVehicles())
	static const uint64 CHATBOTS_FEATURE_FLAG                             = 8; // Should ChatBots run?
	static const uint64 ALLOW_PERSONAL_WORLD_PARCEL_CREATION_FEATURE_FLAG = 16; // Allow owners to create parcels in personal worlds.
	FeatureFlagInfo feature_flag_info GUARDED_BY(mutex);


	// Ephemeral state that is not serialised to disk.  Set by CoinbasePollerThread.
	double BTC_per_EUR;
	double ETH_per_EUR;

	// Ephemeral state that is not serialised to disk.  Set by OpenSeaPollerThread.
	std::vector<OpenSeaParcelListing> opensea_parcel_listings GUARDED_BY(mutex);

	// Ephemeral state
	TimeStamp last_screenshot_bot_contact_time GUARDED_BY(mutex);
	TimeStamp last_lightmapper_bot_contact_time GUARDED_BY(mutex);
	TimeStamp last_eth_bot_contact_time GUARDED_BY(mutex);

	// Ephemeral state - a message that is shown to clients
	std::string server_admin_message GUARDED_BY(mutex);
	bool server_admin_message_changed GUARDED_BY(mutex);

	// Ephemeral state - is the server in read-only mode?  When true, clients can't make changes to objects etc.
	bool read_only_mode GUARDED_BY(mutex);

	// Ephemeral state - do we want to force the DynamicTextureUpdaterThread to do a run?
	bool force_dyn_tex_update GUARDED_BY(mutex);

	// Ephemeral state:
	std::map<UserID, Reference<UserScriptLog> > user_script_log GUARDED_BY(mutex);

	std::map<UserID, std::string> user_web_messages GUARDED_BY(mutex); // For displaying an informational or error message on the next webpage served to a user.

	std::deque<AdminActionLogEntry> admin_action_log GUARDED_BY(mutex);

	// Sets of objects that should be written to (updated) in the database.
	std::unordered_set<ResourceRef, ResourceRefHash>					db_dirty_resources				GUARDED_BY(mutex);
	std::unordered_set<SubEthTransactionRef, SubEthTransactionRefHash>	db_dirty_sub_eth_transactions	GUARDED_BY(mutex);
	std::unordered_set<NewsPostRef, NewsPostRefHash>					db_dirty_news_posts				GUARDED_BY(mutex);
	std::unordered_set<OrderRef, OrderRefHash>							db_dirty_orders					GUARDED_BY(mutex);
	std::unordered_set<ParcelAuctionRef, ParcelAuctionRefHash>			db_dirty_parcel_auctions		GUARDED_BY(mutex);
	std::unordered_set<UserWebSessionRef, UserWebSessionRefHash>		db_dirty_userwebsessions		GUARDED_BY(mutex);
	std::unordered_set<ScreenshotRef, ScreenshotRefHash>				db_dirty_screenshots			GUARDED_BY(mutex);
	std::unordered_set<PhotoRef, PhotoRefHash>							db_dirty_photos					GUARDED_BY(mutex);
	std::unordered_set<UserRef, UserRefHash>							db_dirty_users					GUARDED_BY(mutex);
	std::unordered_set<ObjectStorageItemRef, ObjectStorageItemRefHash>	db_dirty_object_storage_items	GUARDED_BY(mutex);
	std::unordered_set<UserSecretRef, UserSecretRefHash>				db_dirty_user_secrets			GUARDED_BY(mutex);
	std::unordered_set<SubEventRef, SubEventRefHash>					db_dirty_events					GUARDED_BY(mutex);
	std::unordered_set<GearItemRef, GearItemRefHash>					db_dirty_gear_items				GUARDED_BY(mutex);

	std::unordered_set<DatabaseKey, DatabaseKeyHash>					db_records_to_delete			GUARDED_BY(mutex);

	WebDataStore* web_data_store; // Since we pass around ServerAllWorldsState for all the web request handlers, just store a pointer to web_data_store so we can access it.

	ServerCredentials server_credentials;
	ServerConfig server_config;

	mutable ::WorldStateMutex mutex;
private:
	void setWorldState(const std::string& world_name, Reference<ServerWorldState> world) REQUIRES(mutex);

	GLARE_DISABLE_COPY(ServerAllWorldsState);

	glare::AtomicInt changed;

	UID next_object_uid GUARDED_BY(mutex);
	UID next_avatar_uid GUARDED_BY(mutex);
	uint64 next_order_uid GUARDED_BY(mutex);
	uint64 next_sub_eth_transaction_uid GUARDED_BY(mutex);
	uint64 next_screenshot_uid GUARDED_BY(mutex);
	uint64 next_chatbot_uid GUARDED_BY(mutex);
	UID next_gear_item_uid GUARDED_BY(mutex);

	Database database GUARDED_BY(mutex);
};


inline Reference<ServerWorldState> ServerAllWorldsState::getRootWorldState() 
{
#ifndef NDEBUG
	{
		Lock lock(mutex);
		assert(world_states[""] == root_world_state);
	}
#endif
	return root_world_state;
}


inline MapTileInfo& ServerAllWorldsState::getMapTileInfoForWorld(const std::string& world_name, WorldStateLock& /*world_state_lock*/)
{
	// Mutex is expected to be held (REQUIRES(mutex)), WorldStateLock is used for API consistency.
	return map_tile_info_for_world[world_name];
}


inline const MapTileInfo& ServerAllWorldsState::getMapTileInfoForWorld(const std::string& world_name, WorldStateLock& /*world_state_lock*/) const
{
	auto it = map_tile_info_for_world.find(world_name);
	if(it != map_tile_info_for_world.end())
		return it->second;

	// Fallback to root world tiles if no entry exists for this world yet.
	auto it_root = map_tile_info_for_world.find("");
	if(it_root != map_tile_info_for_world.end())
		return it_root->second;

	static MapTileInfo empty;
	return empty;
}
