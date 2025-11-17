/*=====================================================================
UserListDisplay.h
-----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once

#include "../shared/WorldObject.h"
#include "../shared/Avatar.h"
#include <string>
#include <map>

class GUIClient;
// Forward declaration - WorldState.h is included in UserListDisplay.cpp
class WorldState;

/*=====================================================================
UserListDisplay
---------------
Utility class for updating text objects with user list information.
=====================================================================*/
class UserListDisplay
{
public:
	// Update the text object to display current users in the world
	// First tries to find object by UID (for backward compatibility), then searches by content marker
	// Returns true if the object was found and updated, false otherwise
	static bool updateUserListTextObject(GUIClient* gui_client, WorldState* world_state, const UID& text_object_uid);
	
	// Find text object by content marker (works on any server/world)
	// Returns UID of found object, or invalid UID if not found
	static UID findUserListTextObjectByMarker(WorldState* world_state);

private:
	// Format date/time as "YYYY-MM-DD HH:MM:SS"
	static std::string formatDateTime(double timestamp);
	
	// Build the user list table text
	// Shows all active users (same logic as updateOnlineUsersList - all avatars with State_Alive)
	static std::string buildUserListTable(const std::map<UID, Reference<Avatar>>& avatars, double current_time);
};

