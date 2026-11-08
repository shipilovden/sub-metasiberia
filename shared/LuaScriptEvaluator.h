/*=====================================================================
LuaScriptEvaluator.h
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include "UID.h"
#include "ParcelID.h"
#include <lua/LuaScript.h>
#include <maths/Vec4f.h>
#include <utils/RefCounted.h>
#include <utils/WeakRefCounted.h>
#include <utils/UniqueRef.h>
#include <memory>
#include <map>
#include <utility>
#include <vector>
class SubstrataLuaVM;
class WorldObject;
class ObjectEventHandlers;
class ServerWorldState;
class WorldStateLock;
class LuaHTTPRequestResult;


/*=====================================================================
LuaScriptEvaluator
------------------
Per-WorldObject
=====================================================================*/
class LuaScriptEvaluator : public WeakRefCounted
{
public:
	LuaScriptEvaluator(const Reference<SubstrataLuaVM>& substrata_lua_vm, LuaScriptOutputHandler* script_output_handler, 
		const std::string& script_src, WorldObject* world_object,
#if SERVER
		ServerWorldState* world_state, // The world that the object belongs to.
#endif
		WorldStateLock& world_state_lock // Since this constructor executes Lua code, we need to hold the world state lock
	);
	~LuaScriptEvaluator();


	void doOnUserTouchedObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept;
	
	void doOnUserUsedObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	void doOnUserGameSessionChanged(int func_ref, UID avatar_uid, UID ob_uid, bool active, WorldStateLock& world_state_lock) noexcept;
	void doOnUserKeyPressed(int func_ref, UID avatar_uid, UID ob_uid, const std::string& key, WorldStateLock& world_state_lock) noexcept;
	void doOnUserKeyReleased(int func_ref, UID avatar_uid, UID ob_uid, const std::string& key, WorldStateLock& world_state_lock) noexcept;
	void doOnUserGameAction(int func_ref, UID avatar_uid, UID ob_uid, const std::string& action_id, const std::vector<std::pair<std::string, std::string>>& values, WorldStateLock& world_state_lock) noexcept;
	void doOnUserGameMessage(int func_ref, UID avatar_uid, UID ob_uid, const std::string& name, const std::vector<uint8>& payload, WorldStateLock& world_state_lock) noexcept;
	void doOnGameStateChanged(int func_ref, UID ob_uid, uint32 version, const std::vector<uint8>& serialized_value, WorldStateLock& world_state_lock) noexcept;
	void doOnGameEvent(int func_ref, UID ob_uid, const std::string& name, uint32 sequence, double server_time, WorldStateLock& world_state_lock) noexcept;

	void doOnUserMovedNearToObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	
	void doOnUserMovedAwayFromObject(int func_ref, UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserEnteredParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserExitedParcel(int func_ref, UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnUserEnteredVehicle(int func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in
	
	void doOnUserExitedVehicle(int func_ref, UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	void doOnChatMessage(int func_ref, UID avatar_uid, const std::string& message, WorldStateLock& world_state_lock) noexcept; // client_user_id may be invalid if user is not logged in

	// The callback receives (this_object, delta_time_seconds). Existing scripts that
	// declare only one parameter remain compatible with the extra argument.
	void doOnTimerEvent(int onTimerEvent_ref, WorldStateLock& world_state_lock, double delta_time_s = 0.0) noexcept;

	void destroyTimer(int timer_index);

	void doOnError(int onError_ref, int error_code, const std::string& error_description, WorldStateLock& world_state_lock) noexcept;
	void doOnDone(int onDone_ref, Reference<LuaHTTPRequestResult> result, WorldStateLock& world_state_lock) noexcept;

//private:
	void pushUserTableOntoStack(UserID client_user_id);
	void pushAvatarTableOntoStack(UID avatar_uid);
	void pushWorldObjectTableOntoStack(UID ob_uid); // OLD: Push a table for this->world_object onto Lua stack.
	void pushParcelTableOntoStack(ParcelID parcel_id);
public:
	Reference<SubstrataLuaVM> substrata_lua_vm;
	UniqueRef<LuaScript> lua_script;
	LuaScriptOutputHandler* script_output_handler;
	bool hit_error;
	std::map<std::string, std::vector<std::string>> game_action_bindings;
	bool game_action_input_binding_registered = false;
	Reference<ObjectEventHandlers> game_action_input_event_handlers;

	WorldObject* world_object;
#if SERVER
	ServerWorldState* world_state; // The world that the object belongs to.
#endif

	WorldStateLock* cur_world_state_lock; // Non-null if the world state lock is currently held by this thread, null otherwise.

	static const int MAX_NUM_TIMERS = 5; // Four ordinary timers plus one fixed-step game loop.
	static const int MAX_NUM_ORDINARY_TIMERS = 4;
	static const int MAX_NUM_GAME_LOOPS = 1;

	struct LuaTimerInfo
	{
		int id; // -1 means no timer.
		int onTimerEvent_ref; // Reference to Lua callback function
		double last_trigger_time; // Monotonic host time used to calculate callback delta time.
		double fixed_step_s; // Zero for a normal timer; positive for a fixed-step game loop.
		double fixed_step_accumulator_s;
	};
	LuaTimerInfo timers[MAX_NUM_TIMERS];

	int next_timer_id;

	int num_obs_event_listening; // Number of objects that this script has added an event listener to.
};
