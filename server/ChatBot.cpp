/*=====================================================================
ChatBot.cpp
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ChatBot.h"


#include "Server.h"
#include "ServerWorldState.h"
#include "LLMThread.h"
#include "../shared/MessageUtils.h"
#include "../shared/Protocol.h"
#include "../shared/GestureSettings.h"
#include <webserver/Escaping.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <SocketBufferOutStream.h>
#include <KillThreadMessage.h>
#include <RuntimeCheck.h>
#include <RandomAccessOutStream.h>
#include <algorithm>
#include <limits>


static const double GREETING_COOLDOWN_PERIOD = 60.0; // Don't send greeting messages more often than this.
static const double FAREWELL_COOLDOWN_PERIOD = 60.0;

static bool chatMessageMatchesKeywords(const std::string& msg, const std::string& keywords);


ChatBot::ChatBot()
:	flags(0),
	world(nullptr),
	pos(Vec3d(0.0)),
	heading(0),
	greeting_gesture_flags(0),
	greeting_gesture_cooldown_s(8.f),
	idle_gesture_flags(0),
	idle_gesture_interval_s(30.f),
	reactive_gesture_flags(0),
	reactive_gesture_cooldown_s(6.f),
	surprise_gesture_flags(0),
	surprise_gesture_cooldown_s(15.f),
	acknowledge_gesture_flags(0),
	acknowledge_gesture_cooldown_s(10.f),
	use_action_type(USE_ACTION_LLM),
	greeting_distance(6.f),
	farewell_distance(10.f),
	chat_radius(8.f),
	model_scale(Vec3f(1.f)),
	ai_temperature(0.7f),
	ai_max_tokens(0),
	audio_volume(1.f),
	audio_radius(10.f),
	audio_activation_distance(12.f),
	audio_cooldown_s(0.f),
	trigger_flags(TRIGGER_PROXIMITY_FLAG | TRIGGER_CHAT_FLAG),
	trigger_cooldown_s(3.f),
	pending_manual_gesture(false),
	pending_manual_gesture_flags(0),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder)
{
	greeting_gesture_name = "Waving 1";
	reactive_gesture_name = "Quick Informal Bow";
	ai_personality_preset = "assistant";

	next_sentence_search_pos = 0;
	next_sentence_start_index = 0;
	sentences_received_timer.pause();

	repeating_gesture_timer.pause();
	time_since_last_reactive_gesture.pause();
	time_since_last_idle_gesture.pause();
	time_since_last_greeting_gesture.pause();

	clampAnimationSettings();
}


ChatBot::~ChatBot()
{

}


static const float MAX_ATTENTION_DIST = 6.0f;




// Is the other avatar looking at this chatbot's avatar?
static bool isOtherAvatarAttendingToOurAvatar(const Avatar* other_avatar, const Vec3d& our_pos)
{
	// Get distance from other avatar to this avatar.
	const Vec4f other_to_us = our_pos.toVec4fPoint() - other_avatar->pos.toVec4fPoint();
	if(other_to_us.length2() > Maths::square(MAX_ATTENTION_DIST))
		return false;

	// Get angle between the direction the other avatar is looking (their forwards dir), and the vector to this avatar.
	const Vec4f unit_other_to_us = normalise(other_to_us);
	
	const float other_av_heading = other_avatar->rotation.z;
	const Vec4f other_look_dir = Vec4f(std::cos(other_av_heading), std::sin(other_av_heading), 0, 0); // Ignore pitch for now

	const float ANGLE_THRESHOLD = 0.5f;
	const float cos_angle = dot(unit_other_to_us, other_look_dir);
	return cos_angle > std::cos(ANGLE_THRESHOLD); // == angle <= ANGLE_THRESHOLD
}


ChatBot::EventHandlerResults ChatBot::userMovedNearToBotAvatar(AvatarRef other_avatar, Server* server, WorldStateLock& lock)
{
	conPrint("----User moved near chatbot " + toString(id) + "----");

	EventHandlerResults res;
	if(isDisabled())
		return res;
	if(!BitUtils::isBitSet(trigger_flags, TRIGGER_PROXIMITY_FLAG))
		return res;

	// Add to list of avatars nearby the chatbot.
	auto other_res = other_avatar_info.find(other_avatar);
	if(other_res == other_avatar_info.end())
	{
		other_avatar_info[other_avatar] = OtherAvatarInfo();
		other_avatar_info[other_avatar].attention_timer.pause();
		other_avatar_info[other_avatar].time_since_last_greeted_other_av.pause();
		other_avatar_info[other_avatar].time_since_farewelled_other_av.pause();
	}

	return res;
}


ChatBot::EventHandlerResults ChatBot::userMovedAwayFromBotAvatar(AvatarRef other_avatar, Server* server, WorldStateLock& lock)
{
	conPrint("----User moved away from chatbot " + toString(id) + "----");

	EventHandlerResults res;
	if(isDisabled())
		return res;
	if(!BitUtils::isBitSet(trigger_flags, TRIGGER_PROXIMITY_FLAG))
		return res;

	auto other_res = other_avatar_info.find(other_avatar);
	if(other_res != other_avatar_info.end())
	{
		OtherAvatarInfo& info = other_res->second;
		if(info.conversing) // If we were chatting with the avatar that moved away:
		{
			const bool farewelled_other_av_recently = info.time_since_farewelled_other_av.isRunning() && (info.time_since_farewelled_other_av.elapsed() < FAREWELL_COOLDOWN_PERIOD);
			conPrint("    farewelled_other_av_recently: " + boolToString(farewelled_other_av_recently));

			// Append a 'XX moved away from you' message to conversation, which should trigger a "goodbye" response from the LLM.  Only do this if we haven't done so recently, to avoid spamming chat. 
			if(!farewelled_other_av_recently)
			{
				// Create LLM thread if not already created.
				if(!llm_thread)
				{
					this->llm_thread = createLLMThread(server);
					res.new_llm_thread = this->llm_thread;
				}

				// Send a 'XX moved away from you' message to the LLM cloud server.
				SendAIChatPostContent* send_chat_msg = new SendAIChatPostContent();
				send_chat_msg->message = other_avatar->getUseName() + " moved away from you.";
				llm_thread->getMessageQueue().enqueue(send_chat_msg);
			
				time_since_last_LLM_activity.reset();

				info.time_since_farewelled_other_av.resetAndUnpause();
			}
		}
		
		//conPrint("Setting conversing=false and pausing attention_timer.");
		info.conversing = false; // Consider us not conversing with the avatar that moved away.
		info.attention_timer.pause();
	}

	return res;
}


ChatBot::EventHandlerResults ChatBot::processHeardChatMessage(const std::string& msg, AvatarRef sender_avatar, const std::string& avatar_name, Server* server, uint32 client_capabilities, WorldStateLock& lock)
{
	EventHandlerResults res;
	if(isDisabled())
		return res;
	if(!BitUtils::isBitSet(trigger_flags, TRIGGER_CHAT_FLAG))
		return res;
	if(BitUtils::isBitSet(trigger_flags, TRIGGER_KEYWORDS_FLAG) && !chatMessageMatchesKeywords(msg, trigger_keywords))
		return res;

	// Backwards compatible handling for old clients that don't send UserMovedNearToAvatar and userMovedAwayFromBotAvatar msgs:
	// Just add any avatar that chats near the chatbot to other_avatar_info.
	if(!BitUtils::isBitSet(client_capabilities, Protocol::SENDS_USER_MOVED_CHATBOT_MSGS))
	{
		if((sender_avatar->pos.getDist(this->pos) < chat_radius) && (other_avatar_info.count(sender_avatar) == 0))
		{
			other_avatar_info[sender_avatar] = OtherAvatarInfo();
			other_avatar_info[sender_avatar].attention_timer.pause();
			other_avatar_info[sender_avatar].time_since_last_greeted_other_av.pause();
			other_avatar_info[sender_avatar].time_since_farewelled_other_av.pause();
		}
	}


	auto sender_res = other_avatar_info.find(sender_avatar);
	if(sender_res != other_avatar_info.end())
	{
		if(sender_res->second.conversing) // If we are chatting with the avatar:
		{
			// Create LLM thread if not already created.
			if(!llm_thread)
			{
				this->llm_thread = createLLMThread(server);
				res.new_llm_thread = this->llm_thread;
			}

			// Send the chat message to the LLM cloud server.
			SendAIChatPostContent* send_chat_msg = new SendAIChatPostContent();
			send_chat_msg->message = avatar_name + ": " + msg;
			llm_thread->getMessageQueue().enqueue(send_chat_msg);

			time_since_last_LLM_activity.reset();

			this->look_target_avatar = sender_avatar; // Look at the avatar that sent the chat message

			if((!reactive_gesture_name.empty() || !reactive_gesture_URL.empty()) && canTriggerTimer(time_since_last_reactive_gesture, reactive_gesture_cooldown_s))
			{
				playGestureNow(reactive_gesture_name, reactive_gesture_URL, reactive_gesture_flags, server);
				time_since_last_reactive_gesture.resetAndUnpause();
			}
		}
	}

	return res;
}


void ChatBot::queueManualGesturePlayback(const std::string& gesture_name, const URLString& gesture_URL, uint32 gesture_flags)
{
	pending_manual_gesture = true;

	pending_manual_gesture_name = gesture_name;
	if(pending_manual_gesture_name.size() > MAX_GESTURE_NAME_SIZE)
		pending_manual_gesture_name.resize(MAX_GESTURE_NAME_SIZE);

	std::string gesture_url_std = toStdString(gesture_URL);
	if(gesture_url_std.size() > MAX_GESTURE_URL_SIZE)
		gesture_url_std.resize(MAX_GESTURE_URL_SIZE);
	pending_manual_gesture_URL = toURLString(gesture_url_std);
	pending_manual_gesture_flags = gesture_flags;
}


ChatBot::EventHandlerResults ChatBot::processUserUsedBot(AvatarRef user_avatar, Server* server, WorldStateLock& lock)
{
	EventHandlerResults res;
	if(isDisabled())
		return res;
	if(!BitUtils::isBitSet(trigger_flags, TRIGGER_USE_ACTION_FLAG))
		return res;

	switch(use_action_type)
	{
		case USE_ACTION_LLM:
		{
			if(!llm_thread)
			{
				this->llm_thread = createLLMThread(server);
				res.new_llm_thread = this->llm_thread;
			}
			SendAIChatPostContent* send_msg = new SendAIChatPostContent();
			send_msg->message = user_avatar->getUseName() + " interacted with you.";
			llm_thread->getMessageQueue().enqueue(send_msg);
			time_since_last_LLM_activity.reset();
			break;
		}
		case USE_ACTION_SAY_TEXT:
		{
			if(!use_action_param.empty())
				sendChatMessage(use_action_param, server, lock);
			break;
		}
		case USE_ACTION_GESTURE:
		{
			if(use_action_param == "greeting")
				playGestureNow(greeting_gesture_name, greeting_gesture_URL, greeting_gesture_flags, server);
			else if(use_action_param == "idle")
				playGestureNow(idle_gesture_name, idle_gesture_URL, idle_gesture_flags, server);
			else if(use_action_param == "reactive")
				playGestureNow(reactive_gesture_name, reactive_gesture_URL, reactive_gesture_flags, server);
			else if(use_action_param == "surprise")
				playGestureNow(surprise_gesture_name, surprise_gesture_URL, surprise_gesture_flags, server);
			else if(use_action_param == "acknowledge")
				playGestureNow(acknowledge_gesture_name, acknowledge_gesture_URL, acknowledge_gesture_flags, server);
			break;
		}
		default:
			break;
	}
	return res;
}


bool ChatBot::canTriggerTimer(const Timer& timer, float min_interval_s) const
{
	if(min_interval_s <= 0.f)
		return true;

	return timer.isPaused() || (timer.elapsed() >= min_interval_s);
}


static bool chatMessageMatchesKeywords(const std::string& msg, const std::string& keywords)
{
	if(keywords.empty())
		return true;

	const std::string msg_lower = toLowerCase(msg);
	size_t start = 0;
	while(start < keywords.size())
	{
		size_t end = start;
		while(end < keywords.size() && keywords[end] != ',' && keywords[end] != ';' && keywords[end] != '\n')
			end++;

		std::string keyword = stripHeadAndTailWhitespace(keywords.substr(start, end - start));
		if(!keyword.empty() && StringUtils::containsStringCaseInvariant(msg_lower, toLowerCase(keyword)))
			return true;

		start = end + 1;
	}

	return false;
}


void ChatBot::clampAnimationSettings()
{
	if(greeting_gesture_name.size() > MAX_GESTURE_NAME_SIZE)
		greeting_gesture_name.resize(MAX_GESTURE_NAME_SIZE);
	if(idle_gesture_name.size() > MAX_GESTURE_NAME_SIZE)
		idle_gesture_name.resize(MAX_GESTURE_NAME_SIZE);
	if(reactive_gesture_name.size() > MAX_GESTURE_NAME_SIZE)
		reactive_gesture_name.resize(MAX_GESTURE_NAME_SIZE);

	std::string greeting_url = toStdString(greeting_gesture_URL);
	if(greeting_url.size() > MAX_GESTURE_URL_SIZE)
		greeting_url.resize(MAX_GESTURE_URL_SIZE);
	greeting_gesture_URL = toURLString(greeting_url);

	std::string idle_url = toStdString(idle_gesture_URL);
	if(idle_url.size() > MAX_GESTURE_URL_SIZE)
		idle_url.resize(MAX_GESTURE_URL_SIZE);
	idle_gesture_URL = toURLString(idle_url);

	std::string reactive_url = toStdString(reactive_gesture_URL);
	if(reactive_url.size() > MAX_GESTURE_URL_SIZE)
		reactive_url.resize(MAX_GESTURE_URL_SIZE);
	reactive_gesture_URL = toURLString(reactive_url);

	if(!std::isfinite(greeting_gesture_cooldown_s))
		greeting_gesture_cooldown_s = 8.f;
	if(!std::isfinite(idle_gesture_interval_s))
		idle_gesture_interval_s = 30.f;
	if(!std::isfinite(reactive_gesture_cooldown_s))
		reactive_gesture_cooldown_s = 6.f;

	greeting_gesture_cooldown_s = myClamp(greeting_gesture_cooldown_s, 0.f, 3600.f);
	idle_gesture_interval_s = myClamp(idle_gesture_interval_s, 0.f, 3600.f);
	reactive_gesture_cooldown_s = myClamp(reactive_gesture_cooldown_s, 0.f, 3600.f);
	if(!std::isfinite(greeting_distance))
		greeting_distance = 6.f;
	if(!std::isfinite(farewell_distance))
		farewell_distance = 10.f;
	if(!std::isfinite(chat_radius))
		chat_radius = 8.f;
	greeting_distance = myClamp(greeting_distance, 0.5f, 100.f);
	farewell_distance = myClamp(farewell_distance, greeting_distance, 150.f);
	chat_radius = myClamp(chat_radius, 0.5f, 100.f);

	if(!std::isfinite(model_scale.x)) model_scale.x = 1.f;
	if(!std::isfinite(model_scale.y)) model_scale.y = 1.f;
	if(!std::isfinite(model_scale.z)) model_scale.z = 1.f;
	model_scale.x = myClamp(model_scale.x, 0.05f, 20.f);
	model_scale.y = myClamp(model_scale.y, 0.05f, 20.f);
	model_scale.z = myClamp(model_scale.z, 0.05f, 20.f);

	if(use_action_param.size() > MAX_USE_ACTION_PARAM_SIZE)
		use_action_param.resize(MAX_USE_ACTION_PARAM_SIZE);
	if(api_key.size() > MAX_API_KEY_SIZE)
		api_key.resize(MAX_API_KEY_SIZE);
	if(api_endpoint.size() > MAX_API_ENDPOINT_SIZE)
		api_endpoint.resize(MAX_API_ENDPOINT_SIZE);

	if(ai_model_id.size() > MAX_AI_MODEL_ID_SIZE)
		ai_model_id.resize(MAX_AI_MODEL_ID_SIZE);
	if(ai_personality_preset.size() > MAX_AI_PRESET_SIZE)
		ai_personality_preset.resize(MAX_AI_PRESET_SIZE);
	if(ai_knowledge.size() > MAX_AI_KNOWLEDGE_SIZE)
		ai_knowledge.resize(MAX_AI_KNOWLEDGE_SIZE);
	if(!std::isfinite(ai_temperature))
		ai_temperature = 0.7f;
	ai_temperature = myClamp(ai_temperature, 0.f, 2.f);
	if(ai_max_tokens > 32000)
		ai_max_tokens = 32000;

	std::string audio_url = toStdString(audio_source_url);
	if(audio_url.size() > MAX_AUDIO_URL_SIZE)
		audio_url.resize(MAX_AUDIO_URL_SIZE);
	audio_source_url = toURLString(audio_url);
	if(!std::isfinite(audio_volume))
		audio_volume = 1.f;
	if(!std::isfinite(audio_radius))
		audio_radius = 10.f;
	if(!std::isfinite(audio_activation_distance))
		audio_activation_distance = 12.f;
	if(!std::isfinite(audio_cooldown_s))
		audio_cooldown_s = 0.f;
	audio_volume = myClamp(audio_volume, 0.f, 4.f);
	audio_radius = myClamp(audio_radius, 0.1f, 500.f);
	audio_activation_distance = myClamp(audio_activation_distance, 0.1f, 500.f);
	audio_cooldown_s = myClamp(audio_cooldown_s, 0.f, 3600.f);

	if(trigger_keywords.size() > MAX_TRIGGER_KEYWORDS_SIZE)
		trigger_keywords.resize(MAX_TRIGGER_KEYWORDS_SIZE);
	if(!std::isfinite(trigger_cooldown_s))
		trigger_cooldown_s = 3.f;
	trigger_cooldown_s = myClamp(trigger_cooldown_s, 0.f, 3600.f);
}


void ChatBot::playGestureNow(const std::string& gesture_name, const URLString& gesture_URL, uint32 gesture_flags, Server* server)
{
	if(gesture_name.empty() && gesture_URL.empty())
		return;

	const double start_global_time = server->getCurrentGlobalTime();

	// Enqueue AvatarPerformGesture messages to worker threads to send.
	MessageUtils::initPacket(scratch_packet, Protocol::AvatarPerformGesture);
	::writeToStream(avatar_uid, scratch_packet);

	if(gesture_name.empty())
		scratch_packet.writeStringLengthFirst("Custom Gesture");
	else
		scratch_packet.writeStringLengthFirst(gesture_name);

	scratch_packet.writeStringLengthFirst(gesture_URL);
	scratch_packet.writeUInt32(gesture_flags);
	scratch_packet.writeDouble(start_global_time);
	MessageUtils::updatePacketLengthField(scratch_packet);

	server->enqueuePacketToBroadcastForWorld(scratch_packet, world);
}


// Handle some (partial, streaming) chat data coming back from an LLM cloud server.
void ChatBot::handleLLMChatResponse(const std::string& msg, Server* server, WorldStateLock& world_lock)
{
	// conPrint("-----ChatBot::handleLLMChatResponse----\n" + msg);

	total_llm_response += msg;

	const size_t MAX_TOTAL_RESPONSE_SIZE = 1'000'000;
	if(total_llm_response.size() > MAX_TOTAL_RESPONSE_SIZE)
	{
		conPrint("Warning: Truncating oversized LLM response");
		total_llm_response.resize(MAX_TOTAL_RESPONSE_SIZE);
	}

	// Scan through total response, if we have accumulated a sentence, queue it to send to server main thread.
	while(next_sentence_search_pos < total_llm_response.size())
	{
		if(	total_llm_response[next_sentence_search_pos] == '.' || 
			total_llm_response[next_sentence_search_pos] == '\n' ||
			total_llm_response[next_sentence_search_pos] == '\r' ||
			total_llm_response[next_sentence_search_pos] == '?' ||
			total_llm_response[next_sentence_search_pos] == '!')
		{
			next_sentence_search_pos++; // Advance past full stop or newline.

			next_sentence_start_index = next_sentence_search_pos; // Set next_total_llm_response_start to start of next sentence

			// Start sentences-received timer if it isn't already running.
			if(sentences_received_timer.isPaused())
			{
				// conPrint("!!!!!!!!!!!!!! resetAndUnpause sentences_received_timer...");
				sentences_received_timer.resetAndUnpause();
			}
		}
		else
		{
			next_sentence_search_pos++;
		}
	}

	assert(next_sentence_start_index <= total_llm_response.size());

	time_since_last_LLM_activity.reset();
}


void ChatBot::handleLLMChatResponseDone(Server* server, WorldStateLock& world_lock)
{
	conPrint("------- ChatBot::handleLLMChatResponseDone().  total response: -------\n" + total_llm_response + "\n--------------");

	if(!total_llm_response.empty()) // total_llm_response may be empty when doing tool calls.
	{
		sendChatMessage(total_llm_response, server, world_lock);
	}
	else if(!fallback_message.empty())
	{
		sendChatMessage(fallback_message, server, world_lock);
	}

	total_llm_response.clear();
	next_sentence_search_pos = 0;
	next_sentence_start_index = 0;

	sentences_received_timer.pause();

	time_since_last_LLM_activity.reset();
}


void ChatBot::handleLLMToolFunctionCall(const std::vector<Reference<ToolFunctionCall>>& calls, Server* server, WorldStateLock& world_lock)
{
	if(llm_thread)
	{
		for(size_t z=0; z<calls.size(); ++z)
		{
			Reference<ToolFunctionCall> call = calls[z];

			// conPrint("Received tool call for function '" + call->function_name + "'...");

			if(call->function_name == "perform_wave_gesture")
			{
				playGestureNow("Waving 1", /*gesture_URL=*/URLString(), /*gesture_flags=*/0, server);


				// Send response to LLM thread to send to LLM cloud server.
				Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
				result_msg->tool_call_id = call->call_id;
				result_msg->tool_call_name = call->function_name;
				result_msg->content = "Gesture performed successfully.";
				result_msg->should_send_to_server_immediately = false;
				llm_thread->getMessageQueue().enqueue(result_msg);

				repeating_gesture_timer.resetAndUnpause();
			}
			else if(call->function_name == "perform_bow_gesture")
			{
				playGestureNow("Quick Informal Bow", /*gesture_URL=*/URLString(), /*gesture_flags=*/0, server);


				// Send response to LLM thread to send to LLM cloud server.
				Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
				result_msg->tool_call_id = call->call_id;
				result_msg->tool_call_name = call->function_name;
				result_msg->content = "Gesture performed successfully.";
				result_msg->should_send_to_server_immediately = false;
				llm_thread->getMessageQueue().enqueue(result_msg);
			}
			else
			{
				auto res = info_tool_functions.find(call->function_name);
				if(res != info_tool_functions.end())
				{
					const ChatBotToolFunction* func = res->second.ptr();

					// Send response to LLM thread to send to LLM cloud server.
					Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
					result_msg->tool_call_id = call->call_id;
					result_msg->tool_call_name = call->function_name;
					result_msg->content = func->result_content;

					llm_thread->getMessageQueue().enqueue(result_msg);
				}
				else
				{
					// Send response to LLM thread to send to LLM cloud server.
					Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
					result_msg->tool_call_id = call->call_id;
					result_msg->tool_call_name = call->function_name;
					result_msg->content = "Unknown tool function '" + call->function_name + "'.";

					llm_thread->getMessageQueue().enqueue(result_msg);
				}
			}
		}
	}

	time_since_last_LLM_activity.reset();
}


void ChatBot::sendChatMessage(const string_view message, Server* server, WorldStateLock& world_lock)
{
	// Send total_llm_response as a chat message
	MessageUtils::initPacket(scratch_packet, Protocol::ChatMessageID);
	scratch_packet.writeStringLengthFirst(name); // Write sender name
	scratch_packet.writeStringLengthFirst(message); // Write message 
	::writeToStream(avatar_uid, scratch_packet); // Write sender avatar UID (= the avatar of this chatbot)
	
	MessageUtils::updatePacketLengthField(scratch_packet);

	server->enqueuePacketToBroadcastForWorld(scratch_packet, world);



	//-------- Pass chat message to any nearby chatbots --------
	// This allows chatbot-chatbot conversations.
#if 0
	const Vec3d sender_position = this->pos;

	// Find position of the avatar that sent the chat message (the avatar for this thread's client)
	if(avatar)
	{
		const double MAX_CHAT_HEAR_DIST = 6;

		for(auto& it : world->getChatBots(world_lock))
		{
			ChatBot* bot = it.second.ptr();
			if(bot != this && (bot->pos.getDist2(sender_position) < Maths::square(MAX_CHAT_HEAR_DIST)))
			{
				if(bot->llm_thread.isNull())
				{
					// Create a LLM thread for the chatbot.
					// TODO: handle thread creation failure in some way here?
					Reference<LLMThread> new_llm_thread = bot->createLLMThread(server, world_lock);
					new_llm_thread->out_msg_queue = &server->message_queue;
					new_llm_thread->credentials = &server->world_state->server_credentials;
					bot->llm_thread = new_llm_thread;

					server->llm_thread_manager.addThread(new_llm_thread);
				}

				bot->processHeardChatMessage(world_lock, toString(message), this->avatar, this->name, server);
			}
		}
	}
#endif
}


ChatBot::ThinkResults ChatBot::think(Server* server, WorldStateLock& world_lock)
{
	ThinkResults think_results;
	if(isDisabled())
	{
		if(llm_thread)
		{
			think_results.llm_thread_being_killed = llm_thread;
			llm_thread = nullptr;
		}
		return think_results;
	}

	clampAnimationSettings();

	if(pending_manual_gesture)
	{
		playGestureNow(pending_manual_gesture_name, pending_manual_gesture_URL, pending_manual_gesture_flags, server);
		time_since_last_idle_gesture.resetAndUnpause();
		pending_manual_gesture = false;
	}

	if(sentences_received_timer.isRunning() && (sentences_received_timer.elapsed() > 0.3))
	{
		// Send all complete sentences as a chat message
		runtimeCheck(next_sentence_start_index <= total_llm_response.size());
		sendChatMessage(string_view(total_llm_response.data(), next_sentence_start_index), server, world_lock);

		// Remove the prefix of total_llm_response that we sent.
		total_llm_response.erase(/*offset=*/0, /*count=*/next_sentence_start_index);

		// Adjust next_sentence_search_pos to take account of the prefix we just removed.
		runtimeCheck(next_sentence_search_pos >= next_sentence_start_index);
		next_sentence_search_pos -= next_sentence_start_index;
		next_sentence_start_index = 0;

		sentences_received_timer.pause();
	}

	// Stop repeating gestures such as waving.
	if(repeating_gesture_timer.isRunning() && (repeating_gesture_timer.elapsed() > 3.0))
	{
		// Enqueue AvatarStopGesture messages to worker threads to send
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarStopGesture);
		::writeToStream(avatar_uid, scratch_packet);
		MessageUtils::updatePacketLengthField(scratch_packet);

		server->enqueuePacketToBroadcastForWorld(scratch_packet, world);

		repeating_gesture_timer.pause();
	}

	// TODO: find some nice way of removing refs to avatars that are not present in world any more (but never went through dead state for some reason).

	bool any_conversing = false;
	for(auto it = other_avatar_info.begin(); it != other_avatar_info.end();)
	{
		const Avatar* other_avatar = it->first.ptr();
		if(other_avatar->state == Avatar::State_Dead)
		{
			if(this->look_target_avatar == other_avatar)
				this->look_target_avatar = nullptr;

			// Remove avatar from avatar map
			auto old_avatar_iterator = it;
			it++;
			other_avatar_info.erase(old_avatar_iterator);
		}
		else
		{
			OtherAvatarInfo& other_av_info = it->second;
			any_conversing = any_conversing || other_av_info.conversing;
			if(isOtherAvatarAttendingToOurAvatar(other_avatar, this->pos))
			{
				if(other_av_info.attention_timer.isPaused())
				{
					conPrint("----User started attending to chatbot----");
					other_av_info.attention_timer.resetAndUnpause();
				}
			}
			else
			{
				if(other_av_info.attention_timer.isRunning())
				{
					conPrint("----User stopped attending to chatbot----");
					other_av_info.attention_timer.pause();
				}
			}

			// If the other avatar has been looking at this chatbot for a while, and a conversation with it has not yet been started:
			if(other_av_info.attention_timer.isRunning() && (other_av_info.attention_timer.elapsed() > 2.0) && !other_av_info.conversing)
			{
				// Start conversing with the other avatar.

				const bool greeted_other_av_recently = other_av_info.time_since_last_greeted_other_av.isRunning() && (other_av_info.time_since_last_greeted_other_av.elapsed() < GREETING_COOLDOWN_PERIOD);
				conPrint("----User paid attention to chatbot for 2 seconds.  (greeted_other_av_recently=" + boolToString(greeted_other_av_recently) + ")----");

				// Append a 'XX is standing near by' message to conversation, which should trigger a "hello" response from the LLM.  Only do this if we haven't done so recently, to avoid spamming chat. 
				if(!greeted_other_av_recently)
				{
					// Create LLM thread if not already created.
					if(!llm_thread)
					{
						this->llm_thread = createLLMThread(server);
						think_results.new_llm_thread = this->llm_thread;
					}
			
					// Append a 'standing near by' message to conversation, send it immediately if we need a response soon.
					SendAIChatPostContent* send_chat_msg = new SendAIChatPostContent();
					send_chat_msg->message = other_avatar->getUseName() + " is standing near by and looking at you.";
					llm_thread->getMessageQueue().enqueue(send_chat_msg);
			
					time_since_last_LLM_activity.reset();

					other_av_info.time_since_last_greeted_other_av.resetAndUnpause();
				}

				if((!greeting_gesture_name.empty() || !greeting_gesture_URL.empty()) && canTriggerTimer(time_since_last_greeting_gesture, greeting_gesture_cooldown_s))
				{
					playGestureNow(greeting_gesture_name, greeting_gesture_URL, greeting_gesture_flags, server);
					time_since_last_greeting_gesture.resetAndUnpause();
				}


				this->look_target_avatar = other_avatar;

				other_av_info.conversing = true;
				any_conversing = true;
			}

			it++;
		}
	}

	// Play idle animation occasionally when not talking to users.
	if(!any_conversing && (!idle_gesture_name.empty() || !idle_gesture_URL.empty()) && canTriggerTimer(time_since_last_idle_gesture, idle_gesture_interval_s))
	{
		playGestureNow(idle_gesture_name, idle_gesture_URL, idle_gesture_flags, server);
		time_since_last_idle_gesture.resetAndUnpause();
	}


	if(BitUtils::isBitSet(flags, ALWAYS_FACE_NEAREST_USER_FLAG))
	{
		Reference<const Avatar> nearest_avatar;
		double nearest_dist2 = std::numeric_limits<double>::infinity();
		for(auto it = other_avatar_info.begin(); it != other_avatar_info.end(); ++it)
		{
			const AvatarRef other_avatar = it->first;
			const double dist2 = other_avatar->pos.getDist2(this->pos);
			if(dist2 < nearest_dist2)
			{
				nearest_dist2 = dist2;
				nearest_avatar = other_avatar.ptr();
			}
		}
		if(nearest_avatar.nonNull())
			this->look_target_avatar = nearest_avatar;
	}

	// Set rotation so the chatbot avatar looks at look_target_avatar.
	const double FACE_TOWARDS_CHATTING_AV_DIST = 6.0;
	if(look_target_avatar && (look_target_avatar->pos.getDist2(this->pos) < Maths::square(FACE_TOWARDS_CHATTING_AV_DIST)) && avatar)
	{
		// Turn to look at the avatar that we are chatting with.
		const Vec3d to_chat_av = look_target_avatar->pos - this->pos;
		const float target_heading = (float)::atan2(to_chat_av.y, to_chat_av.x);

		avatar->rotation.z = target_heading;
		const float target_pitch = Maths::pi_2<float>() - (float)std::asin(to_chat_av.z / sqrt(Maths::square(to_chat_av.x) + Maths::square(to_chat_av.y)));
		avatar->rotation.y = target_pitch;

		avatar->transform_dirty = true;
	}


	// Terminate the LLM thread after some period of time without user chat messages to the chatbot or replies from the LLM.
	const double KILL_TIME_AFTER_LAST_INTERACTION = 120.0;
	if(llm_thread && (time_since_last_LLM_activity.elapsed() > KILL_TIME_AFTER_LAST_INTERACTION))
	{
		conPrint("ChatBot::think: killing the LLMThread as no interaction for " + toString(KILL_TIME_AFTER_LAST_INTERACTION) + " s.");
		llm_thread->getMessageQueue().enqueue(new KillThreadMessage());
		llm_thread->kill();
		think_results.llm_thread_being_killed = llm_thread;
		llm_thread = nullptr;

		this->look_target_avatar = nullptr;
	}

	return think_results;
}


Reference<LLMThread> ChatBot::createLLMThread(Server* server)
{
	conPrint("ChatBot::createLLMThread()");

	time_since_last_LLM_activity.reset();

	// Make tools_json
	std::string tools_json = 
		"\"tools\": [         \n";


	std::vector<Reference<ChatBotToolFunction>> built_in_tool_functions;
	{
		Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
		func->function_name = "perform_wave_gesture";
		func->description = "Make the chatbot's avatar perform a waving gesture.";
		built_in_tool_functions.push_back(func);
	}
	{
		Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
		func->function_name = "perform_bow_gesture";
		func->description = "Make the chatbot's avatar perform a quick, informal bowing gesture.";
		built_in_tool_functions.push_back(func);
	}


	std::map<std::string, Reference<ChatBotToolFunction>> all_tool_functions = info_tool_functions;
	for(int i=0; i<built_in_tool_functions.size(); ++i)
		all_tool_functions[built_in_tool_functions[i]->function_name] = built_in_tool_functions[i];

	int index = 0;
	for(auto it = all_tool_functions.begin(); it != all_tool_functions.end(); ++it)
	{
		Reference<ChatBotToolFunction> func = it->second;
		tools_json +=
		"	{																			\n"
		"	  \"type\": \"function\",													\n"
		"	  \"function\": {															\n"
		"		\"name\": \"" + web::Escaping::JSONEscape(func->function_name) + "\",		\n"
		"		\"description\": \"" + web::Escaping::JSONEscape(func->description) + "\",	\n"
		"		\"parameters\": {														\n"
		"		  \"type\": \"object\",													\n" // TEMP: just assuming all functions take zero parameters for now.
		"		  \"properties\": {},													\n"
		"		  \"required\": []														\n"
		"		}																		\n"
		"	  }																			\n"
		"	}" + (((index + 1) < all_tool_functions.size()) ? "," : "") + "\n";

		index++;
	}

	tools_json += 
		"],																				\n";
	tools_json += 
		"\"tool_choice\": \"auto\",														\n";

	
	const std::string use_ai_model_id = this->ai_model_id.empty() ? server->config.AI_model_id : this->ai_model_id;
	Reference<LLMThread> new_llm_thread = new LLMThread(use_ai_model_id);

	std::string bot_prompt = server->config.shared_LLM_prompt_part;
	if(!this->ai_personality_preset.empty())
		bot_prompt += "\nBot personality preset: " + this->ai_personality_preset + "\n";
	if(!this->ai_knowledge.empty())
		bot_prompt += "\nBot private knowledge:\n" + this->ai_knowledge + "\n";
	bot_prompt += this->custom_prompt_part;

	// Template variable substitution
	bot_prompt = StringUtils::replaceAll(bot_prompt, "{bot_name}", this->name);
	bot_prompt = StringUtils::replaceAll(bot_prompt, "{world_name}", world ? world->details.name : std::string(""));

	new_llm_thread->base_prompt_json_escaped = web::Escaping::JSONEscape(bot_prompt);
	new_llm_thread->temperature = this->ai_temperature;
	new_llm_thread->max_tokens = this->ai_max_tokens;
	new_llm_thread->tools_json = tools_json;
	new_llm_thread->chatbot = this;
	new_llm_thread->api_key_override      = this->api_key;
	new_llm_thread->api_endpoint_override = this->api_endpoint;

	new_llm_thread->out_msg_queue = &server->message_queue;
	new_llm_thread->credentials = &server->world_state->server_credentials;

	return new_llm_thread;
}


static const uint32 CHATBOT_SERIALISATION_VERSION = 1;


void ChatBot::writeToStream(RandomAccessOutStream& stream)
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(CHATBOT_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later


	stream.writeUInt64(id);

	::writeToStream(owner_id, stream);
	created_time.writeToStream(stream);

	stream.writeStringLengthFirst(name);

	writeAvatarSettingsToStream(avatar_settings, stream);

	stream.writeUInt32(flags);

	::writeToStream(pos, stream);
	stream.writeFloat(heading);

	stream.writeStringLengthFirst(custom_prompt_part);

	// Write info_tool_functions
	stream.writeUInt32((uint32)info_tool_functions.size());
	for(auto it = info_tool_functions.begin(); it != info_tool_functions.end(); ++it)
	{
		it->second->writeToStream(stream);
	}

	stream.writeStringLengthFirst(greeting_gesture_name);
	stream.writeStringLengthFirst(greeting_gesture_URL);
	stream.writeUInt32(greeting_gesture_flags);
	stream.writeFloat(greeting_gesture_cooldown_s);

	stream.writeStringLengthFirst(idle_gesture_name);
	stream.writeStringLengthFirst(idle_gesture_URL);
	stream.writeUInt32(idle_gesture_flags);
	stream.writeFloat(idle_gesture_interval_s);

	stream.writeStringLengthFirst(reactive_gesture_name);
	stream.writeStringLengthFirst(reactive_gesture_URL);
	stream.writeUInt32(reactive_gesture_flags);
	stream.writeFloat(reactive_gesture_cooldown_s);

	stream.writeFloat(greeting_distance);
	stream.writeFloat(farewell_distance);
	stream.writeFloat(chat_radius);

	::writeToStream(model_scale, stream);
	stream.writeStringLengthFirst(ai_model_id);
	stream.writeStringLengthFirst(ai_personality_preset);
	stream.writeStringLengthFirst(ai_knowledge);
	stream.writeFloat(ai_temperature);
	stream.writeUInt32(ai_max_tokens);
	stream.writeStringLengthFirst(audio_source_url);
	stream.writeFloat(audio_volume);
	stream.writeFloat(audio_radius);
	stream.writeFloat(audio_activation_distance);
	stream.writeFloat(audio_cooldown_s);
	stream.writeUInt32(trigger_flags);
	stream.writeStringLengthFirst(trigger_keywords);
	stream.writeFloat(trigger_cooldown_s);

	// Block 3: gesture flags, fallback, extra gesture slots
	stream.writeStringLengthFirst(fallback_message);
	stream.writeStringLengthFirst(surprise_gesture_name);
	stream.writeStringLengthFirst(surprise_gesture_URL);
	stream.writeUInt32(surprise_gesture_flags);
	stream.writeFloat(surprise_gesture_cooldown_s);
	stream.writeStringLengthFirst(acknowledge_gesture_name);
	stream.writeStringLengthFirst(acknowledge_gesture_URL);
	stream.writeUInt32(acknowledge_gesture_flags);
	stream.writeFloat(acknowledge_gesture_cooldown_s);

	// Block 4: use action, per-bot API key/endpoint
	stream.writeUInt32(use_action_type);
	stream.writeStringLengthFirst(use_action_param);
	stream.writeStringLengthFirst(api_key);
	stream.writeStringLengthFirst(api_endpoint);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readChatBotFromStream(RandomAccessInStream& stream, ChatBot& chatbot)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readChatBotFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 10000000ul, "readChatBotFromStream: buffer_size was too large");

	chatbot.id = stream.readUInt64();

	chatbot.owner_id = readUserIDFromStream(stream);
	chatbot.created_time.readFromStream(stream);

	chatbot.name = stream.readStringLengthFirst(ChatBot::MAX_NAME_SIZE);

	readAvatarSettingsFromStream(stream, chatbot.avatar_settings);

	chatbot.flags = stream.readUInt32();

	chatbot.pos = ::readVec3FromStream<double>(stream);
	chatbot.heading = stream.readFloat();

	chatbot.custom_prompt_part = stream.readStringLengthFirst(ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE);

	// Read info_tool_functions
	const uint32 info_tool_functions_size = stream.readUInt32();
	if(info_tool_functions_size > 10000)
		throw glare::Exception("info_tool_functions_size too large: " + toString(info_tool_functions_size));
	for(size_t i=0; i<info_tool_functions_size; ++i)
	{
		Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
		readChatBotToolFunctionFromStream(stream, *func);
		chatbot.info_tool_functions[func->function_name] = func;
	}

	const size_t max_read_index = initial_read_index + buffer_size;
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.greeting_gesture_name = stream.readStringLengthFirst(ChatBot::MAX_GESTURE_NAME_SIZE);
		chatbot.greeting_gesture_URL = toURLString(stream.readStringLengthFirst(ChatBot::MAX_GESTURE_URL_SIZE));
		chatbot.greeting_gesture_flags = stream.readUInt32();
		chatbot.greeting_gesture_cooldown_s = stream.readFloat();
	}
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.idle_gesture_name = stream.readStringLengthFirst(ChatBot::MAX_GESTURE_NAME_SIZE);
		chatbot.idle_gesture_URL = toURLString(stream.readStringLengthFirst(ChatBot::MAX_GESTURE_URL_SIZE));
		chatbot.idle_gesture_flags = stream.readUInt32();
		chatbot.idle_gesture_interval_s = stream.readFloat();
	}
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.reactive_gesture_name = stream.readStringLengthFirst(ChatBot::MAX_GESTURE_NAME_SIZE);
		chatbot.reactive_gesture_URL = toURLString(stream.readStringLengthFirst(ChatBot::MAX_GESTURE_URL_SIZE));
		chatbot.reactive_gesture_flags = stream.readUInt32();
		chatbot.reactive_gesture_cooldown_s = stream.readFloat();
	}
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.greeting_distance = stream.readFloat();
		chatbot.farewell_distance = stream.readFloat();
		chatbot.chat_radius = stream.readFloat();
	}
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.model_scale = readVec3FromStream<float>(stream);
		chatbot.ai_model_id = stream.readStringLengthFirst(ChatBot::MAX_AI_MODEL_ID_SIZE);
		chatbot.ai_personality_preset = stream.readStringLengthFirst(ChatBot::MAX_AI_PRESET_SIZE);
		chatbot.ai_knowledge = stream.readStringLengthFirst(ChatBot::MAX_AI_KNOWLEDGE_SIZE);
		chatbot.ai_temperature = stream.readFloat();
		chatbot.ai_max_tokens = stream.readUInt32();
		chatbot.audio_source_url = toURLString(stream.readStringLengthFirst(ChatBot::MAX_AUDIO_URL_SIZE));
		chatbot.audio_volume = stream.readFloat();
		chatbot.audio_radius = stream.readFloat();
		chatbot.audio_activation_distance = stream.readFloat();
		chatbot.audio_cooldown_s = stream.readFloat();
		chatbot.trigger_flags = stream.readUInt32();
		chatbot.trigger_keywords = stream.readStringLengthFirst(ChatBot::MAX_TRIGGER_KEYWORDS_SIZE);
		chatbot.trigger_cooldown_s = stream.readFloat();
		chatbot.clampAnimationSettings();
	}
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.fallback_message        = stream.readStringLengthFirst(ChatBot::MAX_FALLBACK_MSG_SIZE);
		chatbot.surprise_gesture_name   = stream.readStringLengthFirst(ChatBot::MAX_GESTURE_NAME_SIZE);
		chatbot.surprise_gesture_URL    = toURLString(stream.readStringLengthFirst(ChatBot::MAX_GESTURE_URL_SIZE));
		chatbot.surprise_gesture_flags  = stream.readUInt32();
		chatbot.surprise_gesture_cooldown_s = stream.readFloat();
		chatbot.acknowledge_gesture_name  = stream.readStringLengthFirst(ChatBot::MAX_GESTURE_NAME_SIZE);
		chatbot.acknowledge_gesture_URL   = toURLString(stream.readStringLengthFirst(ChatBot::MAX_GESTURE_URL_SIZE));
		chatbot.acknowledge_gesture_flags = stream.readUInt32();
		chatbot.acknowledge_gesture_cooldown_s = stream.readFloat();
	}
	if(stream.getReadIndex() < max_read_index)
	{
		chatbot.use_action_type  = stream.readUInt32();
		chatbot.use_action_param = stream.readStringLengthFirst(ChatBot::MAX_USE_ACTION_PARAM_SIZE);
		chatbot.api_key          = stream.readStringLengthFirst(ChatBot::MAX_API_KEY_SIZE);
		chatbot.api_endpoint     = stream.readStringLengthFirst(ChatBot::MAX_API_ENDPOINT_SIZE);
	}


	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}


static const uint32 CHATBOT_TOOL_FUNCTION_SERIALISATION_VERSION = 1;


void ChatBotToolFunction::writeToStream(RandomAccessOutStream& stream)
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(CHATBOT_TOOL_FUNCTION_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later


	stream.writeStringLengthFirst(function_name);
	stream.writeStringLengthFirst(description);
	stream.writeStringLengthFirst(result_content);


	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);
	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readChatBotToolFunctionFromStream(RandomAccessInStream& stream, ChatBotToolFunction& func)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readChatBotToolFunctionFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readChatBotToolFunctionFromStream: buffer_size was too large");

	
	func.function_name  = stream.readStringLengthFirst(ChatBotToolFunction::MAX_FUNCTION_NAME_SIZE);
	func.description    = stream.readStringLengthFirst(ChatBotToolFunction::MAX_DESCRIPTION_NAME_SIZE);
	func.result_content = stream.readStringLengthFirst(ChatBotToolFunction::MAX_RESULT_CONTENT_SIZE);


	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
