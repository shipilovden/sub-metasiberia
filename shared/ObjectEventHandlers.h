/*=====================================================================
ObjectEventHandlers.h
---------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "LuaScriptEvaluator.h"
#include <utils/ThreadSafeRefCounted.h>
#include <utils/WeakReference.h>
#include <vector>
#include <string>
#include <utility>
class WorldStateLock;


struct HandlerFunc
{
	WeakReference<LuaScriptEvaluator> script;
	int handler_func_ref;
	const void* function_ptr; // Just used for uniquely identifying functions, not dereferenced.

	bool operator == (const HandlerFunc& other) const { return script.ob == other.script.ob && function_ptr == other.function_ptr; }
};

struct HandlerList
{
	bool addHandler(const HandlerFunc& handler); // Returns if added or not
	void removeHandler(const HandlerFunc& handler);
	void removeHandlerAtIndex(size_t i);

	bool nonEmpty() { return !handler_funcs.empty(); }

	std::vector<HandlerFunc> handler_funcs;
};


/*=====================================================================
ObjectEventHandlers
-------------------
An object script can listen for events that are triggered on other objects, for example onUserUsedObject.

onUserUsedObject_handlers is a list of all scripts handler functions that are listening for the user-used-object event
on the object that owns the ObjectEventHandlers instance.
=====================================================================*/
class ObjectEventHandlers : public ThreadSafeRefCounted
{
public:
	void executeOnUserUsedObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserGameSessionChangedHandlers(UID avatar_uid, UID ob_uid, bool active, WorldStateLock& world_state_lock);
	void executeOnUserKeyPressedHandlers(UID avatar_uid, UID ob_uid, const std::string& key, WorldStateLock& world_state_lock);
	void executeOnUserKeyReleasedHandlers(UID avatar_uid, UID ob_uid, const std::string& key, WorldStateLock& world_state_lock);
	void executeOnUserGameActionHandlers(UID avatar_uid, UID ob_uid, const std::string& action_id, const std::vector<std::pair<std::string, std::string>>& values, WorldStateLock& world_state_lock);
	void executeOnUserGameMessageHandlers(UID avatar_uid, UID ob_uid, const std::string& name, const std::vector<uint8>& payload, WorldStateLock& world_state_lock);
	void executeOnGameStateChangedHandlers(UID ob_uid, uint32 version, const std::vector<uint8>& serialized_value, WorldStateLock& world_state_lock);
	void executeOnGameEventHandlers(UID ob_uid, const std::string& name, uint32 sequence, double server_time, WorldStateLock& world_state_lock);
	void executeOnUserTouchedObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserMovedNearToObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserMovedAwayFromObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserEnteredParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock);
	void executeOnUserExitedParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock);
	void executeOnUserEnteredVehicleHandlers(UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock);
	void executeOnUserExitedVehicleHandlers(UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock);
	void executeOnChatMessageHandlers(UID avatar_uid, const std::string& message, WorldStateLock& world_state_lock);

	HandlerList onUserUsedObject_handlers;
	HandlerList onUserGameSessionChanged_handlers;
	HandlerList onUserKeyPressed_handlers;
	HandlerList onUserKeyReleased_handlers;
	HandlerList onUserGameAction_handlers;
	HandlerList onUserGameMessage_handlers;
	size_t game_action_input_binding_count = 0;
	HandlerList onGameStateChanged_handlers;
	HandlerList onGameEvent_handlers;
	HandlerList onUserTouchedObject_handlers;
	HandlerList onUserMovedNearToObject_handlers;
	HandlerList onUserMovedAwayFromObject_handlers;
	HandlerList onUserEnteredParcel_handlers;
	HandlerList onUserExitedParcel_handlers;
	HandlerList onUserEnteredVehicle_handlers;
	HandlerList onUserExitedVehicle_handlers;
	HandlerList onChatMessage_handlers;
};
