/*=====================================================================
UserListDisplay.cpp
-------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/

#include "UserListDisplay.h"
#include "GUIClient.h"
#include "WorldState.h"
#include "../shared/WorldStateLock.h"
#include "../shared/WorldObject.h"
#include "../shared/UID.h"
#include "../utils/Clock.h"
#include "../utils/StringUtils.h"
#include "../utils/BitUtils.h"
#include "../utils/ConPrint.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <vector>

UID UserListDisplay::findUserListTextObjectByMarker(WorldState* world_state)
{
	if(!world_state)
		return UID::invalidUID();
	
	const std::string marker = "🌍 USERS IN THE METASIBERIA 🌍";
	
	WorldStateLock lock(world_state->mutex);
	
	// Search through all objects to find text object with the marker
	for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
	{
		WorldObject* ob = it.getValue().ptr();
		if(ob && ob->object_type == WorldObject::ObjectType_Text)
		{
			// Check if content starts with our marker
			if(ob->content.find(marker) == 0)
			{
				return ob->uid;
			}
		}
	}
	
	return UID::invalidUID();
}

bool UserListDisplay::updateUserListTextObject(GUIClient* gui_client, WorldState* world_state, const UID& text_object_uid)
{
	if(!world_state || !gui_client)
		return false;

	// Don't update during reconnection to prevent access violations
	// Check connection state through gui_client if available
	// For now, just add extra safety checks
	
	try
	{
		WorldStateLock lock(world_state->mutex);

		UID actual_uid = text_object_uid;
		
		// First try to find object by UID (for backward compatibility)
		auto res = world_state->objects.find(text_object_uid);
		if(res == world_state->objects.end())
		{
			// If not found by UID, try to find by content marker (works on any server/world)
			actual_uid = findUserListTextObjectByMarker(world_state);
			if(actual_uid.valid())
			{
				res = world_state->objects.find(actual_uid);
			}
		}
		
		if(res == world_state->objects.end())
		{
			// Object not found by UID or by marker
			return false;
		}

		WorldObject* ob = res.getValue().ptr();
		if(!ob)
		{
			return false; // Object pointer is null
		}
			
		if(ob->object_type != WorldObject::ObjectType_Text)
		{
			return false; // Not a text object
		}

		// Check if object already has OpenGL representation - don't update if it's being recreated
		// Also check if object is being deleted or recreated (has CONTENT_CHANGED flag already set)
		if(ob->opengl_engine_ob.isNull())
		{
			return false; // Object is not yet loaded, skip update
		}
			
		// Note: We don't check CONTENT_CHANGED flag here - we want to update the content even if it's being processed
		// The flag will be cleared by the rendering system after the object is updated

		// Note: It's OK if avatars map is empty - that just means no users are currently in the world
		// We should still update the display to show "Active Users: 0"

		// Build the user list table - use same logic as updateOnlineUsersList
		// Simply take all avatars with State_Alive (no need to check current_time)
		std::string new_content;
		try
		{
			new_content = buildUserListTable(world_state->avatars, 0.0); // current_time not needed
		}
		catch(const std::exception& e)
		{
			conPrint("UserListDisplay: Exception in buildUserListTable: " + std::string(e.what()));
			return false;
		}
		catch(...)
		{
			conPrint("UserListDisplay: Unknown exception in buildUserListTable");
			return false;
		}

		// Only update if content has actually changed to avoid unnecessary recreations
		if(ob->content != new_content)
		{
			ob->content = new_content;
			BitUtils::setBit(ob->changed_flags, WorldObject::CONTENT_CHANGED);
			
			// Add object to dirty_from_local_objects so it gets recreated in timerEvent
			// This ensures the text is actually updated on screen
			world_state->dirty_from_local_objects.insert(ob);
		}

		return true;
	}
	catch(const std::exception& e)
	{
		conPrint("UserListDisplay: Exception in updateUserListTextObject: " + std::string(e.what()));
		return false;
	}
	catch(...)
	{
		conPrint("UserListDisplay: Unknown exception in updateUserListTextObject");
		return false;
	}
}

std::string UserListDisplay::formatDateTime(double timestamp)
{
	// Convert timestamp to time_t
	const time_t time_t_val = (time_t)timestamp;
	
	// Format as "YYYY-MM-DD HH:MM:SS"
	struct tm timeinfo;
#ifdef _WIN32
	localtime_s(&timeinfo, &time_t_val);
#else
	localtime_r(&time_t_val, &timeinfo);
#endif

	std::ostringstream oss;
	oss << std::setfill('0') 
		<< std::setw(4) << (timeinfo.tm_year + 1900) << "-"
		<< std::setw(2) << (timeinfo.tm_mon + 1) << "-"
		<< std::setw(2) << timeinfo.tm_mday << " "
		<< std::setw(2) << timeinfo.tm_hour << ":"
		<< std::setw(2) << timeinfo.tm_min << ":"
		<< std::setw(2) << timeinfo.tm_sec;

	return oss.str();
}

std::string UserListDisplay::buildUserListTable(const std::map<UID, Reference<Avatar>>& avatars, double current_time)
{
	std::ostringstream oss;

	// Header
	oss << "🌍 USERS IN THE METASIBERIA 🌍\n\n";

	// Separator line
	oss << "__________________________________\n\n\n";

	// Collect active users - use EXACTLY the same logic as updateOnlineUsersList() in MainWindow
	// Simply take all avatars from world_state->avatars - the system already removes dead avatars
	// Dead avatars are removed in timerEvent before updateOnlineUsersList() is called
	std::vector<std::string> active_user_names;
	
	for(auto entry : avatars)
	{
		const Avatar* av = entry.second.ptr();
		if(av && !av->name.empty())
		{
			// Include all avatars from the map - same as updateOnlineUsersList()
			// Dead avatars are already removed from the map by the time this is called
			active_user_names.push_back(av->name);
		}
	}

	// Sort by name for consistent display
	std::sort(active_user_names.begin(), active_user_names.end());

	// Active Users count
	oss << "Active Users: " << active_user_names.size() << "\n\n";

	// Separator line
	oss << "______________________________\n\n\n";

	// Display active users with bullet points
	for(size_t i = 0; i < active_user_names.size(); ++i)
	{
		oss << "  • " << active_user_names[i] << "\n";
	}

	if(active_user_names.empty())
	{
		oss << "  (No active users)\n";
	}

	return oss.str();
}

