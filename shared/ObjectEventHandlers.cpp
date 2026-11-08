/*=====================================================================
ObjectEventHandlers.cpp
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ObjectEventHandlers.h"


#include <utils/ConPrint.h>


 // Returns if added or not
bool HandlerList::addHandler(const HandlerFunc& handler)
{
	// See if it exists already
	for(size_t i=0; i<handler_funcs.size(); ++i)
		if(handler_funcs[i] == handler)
		{
			// conPrint("event handler already in list");
			return false;
		}

	// Add
	// conPrint("Adding event handler");
	handler_funcs.push_back(handler);
	return true;
}


void HandlerList::removeHandler(const HandlerFunc& handler)
{
	for(size_t i=0; i<handler_funcs.size(); ++i)
		if(handler_funcs[i] == handler)
		{
			removeHandlerAtIndex(i);
			return;
		}
}


void HandlerList::removeHandlerAtIndex(size_t i)
{
	// Swap with last element, if this is not the last element.
	if(i + 1 < handler_funcs.size())
		mySwap(handler_funcs[i], handler_funcs[handler_funcs.size() - 1]);

	handler_funcs.pop_back(); // Now remove it
}


void ObjectEventHandlers::executeOnUserUsedObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock)
{
	// Execute doOnUserUsedObject event handler in any other scripts that are listening for doOnUserUsedObject for this object
	for(size_t z=0; z<onUserUsedObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserUsedObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserUsedObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, world_state_lock);
			z++;
		}
		else
			onUserUsedObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	} // The loop is guaranteed to terminate because we either increment z or decrease the size of handler_funcs (in removeHandlerAtIndex) at each iteration.
}


void ObjectEventHandlers::executeOnUserGameSessionChangedHandlers(UID avatar_uid, UID ob_uid, bool active, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserGameSessionChanged_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserGameSessionChanged_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserGameSessionChanged(handler_func.handler_func_ref, avatar_uid, ob_uid, active, world_state_lock);
			z++;
		}
		else
			onUserGameSessionChanged_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnUserKeyPressedHandlers(UID avatar_uid, UID ob_uid, const std::string& key, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserKeyPressed_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserKeyPressed_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserKeyPressed(handler_func.handler_func_ref, avatar_uid, ob_uid, key, world_state_lock);
			z++;
		}
		else
			onUserKeyPressed_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnUserKeyReleasedHandlers(UID avatar_uid, UID ob_uid, const std::string& key, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserKeyReleased_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserKeyReleased_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserKeyReleased(handler_func.handler_func_ref, avatar_uid, ob_uid, key, world_state_lock);
			z++;
		}
		else
			onUserKeyReleased_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnUserGameActionHandlers(UID avatar_uid, UID ob_uid, const std::string& action_id, const std::vector<std::pair<std::string, std::string>>& values, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserGameAction_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserGameAction_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserGameAction(handler_func.handler_func_ref, avatar_uid, ob_uid, action_id, values, world_state_lock);
			z++;
		}
		else
			onUserGameAction_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnUserGameMessageHandlers(UID avatar_uid, UID ob_uid, const std::string& name, const std::vector<uint8>& payload, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserGameMessage_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserGameMessage_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserGameMessage(handler_func.handler_func_ref, avatar_uid, ob_uid, name, payload, world_state_lock);
			z++;
		}
		else
			onUserGameMessage_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnGameStateChangedHandlers(UID ob_uid, uint32 version, const std::vector<uint8>& serialized_value, WorldStateLock& world_state_lock)
{
	for(size_t z = 0; z < onGameStateChanged_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onGameStateChanged_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnGameStateChanged(handler_func.handler_func_ref, ob_uid, version, serialized_value, world_state_lock);
			z++;
		}
		else
			onGameStateChanged_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnGameEventHandlers(UID ob_uid, const std::string& name, uint32 sequence, double server_time, WorldStateLock& world_state_lock)
{
	for(size_t z = 0; z < onGameEvent_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onGameEvent_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnGameEvent(handler_func.handler_func_ref, ob_uid, name, sequence, server_time, world_state_lock);
			z++;
		}
		else
			onGameEvent_handlers.removeHandlerAtIndex(z);
	}
}


void ObjectEventHandlers::executeOnUserTouchedObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock)
{
	// Execute doOnUserTouchedObject event handler in any other scripts that are listening for onUserTouchedObject for this object
	for(size_t z=0; z<onUserTouchedObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserTouchedObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserTouchedObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, world_state_lock);
			z++;
		}
		else
			onUserTouchedObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserMovedNearToObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock)
{
	// Execute doOnUserMovedNearToObject event handler in any other scripts that are listening for onUserMovedNearToObject for this object
	for(size_t z=0; z<onUserMovedNearToObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserMovedNearToObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserMovedNearToObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, world_state_lock);
			z++;
		}
		else
			onUserMovedNearToObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserMovedAwayFromObjectHandlers(UID avatar_uid, UID ob_uid, WorldStateLock& world_state_lock)
{
	// Execute doOnUserMovedAwayFromObject event handler in any other scripts that are listening for onUserMovedAwayFromObject for this object
	for(size_t z=0; z<onUserMovedAwayFromObject_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserMovedAwayFromObject_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserMovedAwayFromObject(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, world_state_lock);
			z++;
		}
		else
			onUserMovedAwayFromObject_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserEnteredParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserEnteredParcel_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserEnteredParcel_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserEnteredParcel(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, parcel_id, world_state_lock);
			z++;
		}
		else
			onUserEnteredParcel_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserExitedParcelHandlers(UID avatar_uid, UID ob_uid, ParcelID parcel_id, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserExitedParcel_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserExitedParcel_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserExitedParcel(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, ob_uid, parcel_id, world_state_lock);
			z++;
		}
		else
			onUserExitedParcel_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserEnteredVehicleHandlers(UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserEnteredVehicle_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserEnteredVehicle_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserEnteredVehicle(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, vehicle_ob_uid, world_state_lock);
			z++;
		}
		else
			onUserEnteredVehicle_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnUserExitedVehicleHandlers(UID avatar_uid, UID vehicle_ob_uid, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onUserExitedVehicle_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onUserExitedVehicle_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnUserExitedVehicle(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, vehicle_ob_uid, world_state_lock);
			z++;
		}
		else
			onUserExitedVehicle_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}


void ObjectEventHandlers::executeOnChatMessageHandlers(UID avatar_uid, const std::string& message, WorldStateLock& world_state_lock)
{
	for(size_t z=0; z<onChatMessage_handlers.handler_funcs.size(); )
	{
		HandlerFunc& handler_func = onChatMessage_handlers.handler_funcs[z];
		if(LuaScriptEvaluator* eval = handler_func.script.getPtrIfAlive())
		{
			eval->doOnChatMessage(handler_func.handler_func_ref, /*avatar_uid=*/avatar_uid, message, world_state_lock);
			z++;
		}
		else
			onChatMessage_handlers.removeHandlerAtIndex(z); // This handler is dead, remove the reference to it from our handler list.
	}
}
