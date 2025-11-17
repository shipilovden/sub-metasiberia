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
#include "../utils/Clock.h"
#include "../utils/StringUtils.h"
#include "../utils/BitUtils.h"
#include "../utils/ConPrint.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <vector>

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

		// Find the text object
		auto res = world_state->objects.find(text_object_uid);
		if(res == world_state->objects.end())
		{
			conPrint("UserListDisplay: Text object with UID " + text_object_uid.toString() + " not found in world_state->objects (total objects: " + toString(world_state->objects.size()) + ")");
			return false; // Object not found
		}

		WorldObject* ob = res.getValue().ptr();
		if(!ob)
		{
			conPrint("UserListDisplay: Text object pointer is null for UID " + text_object_uid.toString());
			return false; // Object pointer is null
		}
			
		if(ob->object_type != WorldObject::ObjectType_Text)
		{
			conPrint("UserListDisplay: Object with UID " + text_object_uid.toString() + " is not a text object (type=" + toString((int)ob->object_type) + ")");
			return false; // Not a text object
		}

		// Check if object already has OpenGL representation - don't update if it's being recreated
		// Also check if object is being deleted or recreated (has CONTENT_CHANGED flag already set)
		if(ob->opengl_engine_ob.isNull())
		{
			conPrint("UserListDisplay: Text object with UID " + text_object_uid.toString() + " is not yet loaded (opengl_engine_ob is null)");
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
			conPrint("UserListDisplay: Content changed! Old length=" + toString(ob->content.length()) + ", new length=" + toString(new_content.length()));
			ob->content = new_content;
			BitUtils::setBit(ob->changed_flags, WorldObject::CONTENT_CHANGED);
			
			// Add object to dirty_from_local_objects so it gets recreated in timerEvent
			// This ensures the text is actually updated on screen
			world_state->dirty_from_local_objects.insert(ob);
			
			conPrint("UserListDisplay: Updated text object content, length=" + toString(new_content.length()) + ", added to dirty_from_local_objects");
		}
		else
		{
			conPrint("UserListDisplay: Content unchanged, skipping update (length=" + toString(new_content.length()) + ")");
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
	
	conPrint("UserListDisplay: buildUserListTable called with " + toString(avatars.size()) + " avatars");
	
	for(auto entry : avatars)
	{
		const Avatar* av = entry.second.ptr();
		if(av)
		{
			conPrint("UserListDisplay: Found avatar - name='" + av->name + "', state=" + toString((int)av->state) + ", our_avatar=" + toString(av->our_avatar));
			
			if(!av->name.empty())
			{
				// Include all avatars from the map - same as updateOnlineUsersList()
				// Dead avatars are already removed from the map by the time this is called
				active_user_names.push_back(av->name);
				conPrint("UserListDisplay: Added '" + av->name + "' to list");
			}
		}
	}
	
	conPrint("UserListDisplay: Total users in list: " + toString(active_user_names.size()));

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

