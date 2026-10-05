#pragma once
#include <algorithm>

// Local session state only, not a delivery/read receipt sent to other users.
struct ChatReadState {
	unsigned unread=0;
	void appended(bool user_message,bool reading_latest) {
		if(reading_latest) unread=0;
		else if(user_message) unread=std::min(100u,unread+1);
	}
	void viewedLatest() { unread=0; }
};
