/*=====================================================================
ChatBotHandlers.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ChatBotHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "WebDataStore.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "WorldHandlers.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include "../shared/GestureSettings.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <functional>
#include <sstream>
#include <algorithm>


namespace ChatBotHandlers
{

static std::string parseWorldNameFromSubURL(const std::string& sub_url)
{
	if(!hasPrefix(sub_url, "sub://"))
		return sub_url;

	const size_t host_start = 6; // after "sub://"
	const size_t slash_pos = sub_url.find('/', host_start);
	if(slash_pos == std::string::npos)
		return ""; // URL points to main world.

	const size_t world_start = slash_pos + 1;
	const size_t query_pos = sub_url.find('?', world_start);
	const std::string world_part = (query_pos == std::string::npos) ? sub_url.substr(world_start) : sub_url.substr(world_start, query_pos - world_start);
	return web::Escaping::URLUnescape(world_part);
}


static std::string normaliseWorldNameField(const std::string& world_field)
{
	const std::string trimmed = stripHeadAndTailWhitespace(world_field);
	if(hasPrefix(trimmed, "sub://"))
		return parseWorldNameFromSubURL(trimmed);
	return trimmed;
}


static bool postCheckboxIsChecked(const web::RequestInfo& request, const std::string& field_name)
{
	return !request.getPostField(field_name).str().empty();
}


static float parseFloatWithDefault(const web::RequestInfo& request, const std::string& field_name, float default_val)
{
	const std::string s = stripHeadAndTailWhitespace(request.getPostField(field_name).str());
	if(s.empty())
		return default_val;

	try
	{
		return (float)stringToDouble(s);
	}
	catch(glare::Exception&)
	{
		return default_val;
	}
}


static int parseIntWithDefault(const web::RequestInfo& request, const std::string& field_name, int default_val)
{
	const std::string s = stripHeadAndTailWhitespace(request.getPostField(field_name).str());
	if(s.empty())
		return default_val;

	try
	{
		return stringToInt(s);
	}
	catch(glare::Exception&)
	{
		return default_val;
	}
}


static std::string matrixToEditableText(const Matrix4f& m)
{
	std::string s;
	for(int row=0; row<4; ++row)
	{
		for(int col=0; col<4; ++col)
		{
			if(col > 0)
				s += " ";
			s += toString(m.elem(row, col));
		}
		if(row < 3)
			s += "\n";
	}
	return s;
}


static bool parseMatrixFromEditableText(const std::string& s, Matrix4f& matrix_out)
{
	std::string cleaned = s;
	for(size_t i=0; i<cleaned.size(); ++i)
	{
		const char c = cleaned[i];
		if((c == ',') || (c == ';') || (c == '\t') || (c == '\r') || (c == '\n'))
			cleaned[i] = ' ';
	}

	std::vector<float> values;
	values.reserve(16);

	std::stringstream ss(cleaned);
	float v = 0.f;
	while(ss >> v)
		values.push_back(v);

	if(values.size() != 16)
		return false;

	for(int i=0; i<16; ++i)
		matrix_out.e[i] = values[i];

	return true;
}


static float clampFloat(const float v, const float lo, const float hi)
{
	return std::max(lo, std::min(v, hi));
}


void renderEditChatBotPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const int chatbot_id = request.getURLIntParam("chatbot_id");

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit ChatBot");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");


			const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
			if(!msg.empty())
				page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";


			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					const ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						page += "<div class=\"grouped-region\">";
						page += "<form action=\"/edit_chatbot_post\" method=\"post\" id=\"usrform\" class=\"full-width\" >";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";

						page += "<div class=\"form-field\">";
						page += "<label for=\"name\">Name:</label><br/>";
						page += "<input type=\"text\" id=\"name\" name=\"name\" value=\"" + web::Escaping::HTMLEscape(chatbot->name) + "\">";
						page += "</div>";


						page += std::string("<div>Position:<br/> ") + 
							"x: <input type=\"number\" step=\"any\" name=\"pos_x\" value=\"" + toString(chatbot->pos.x) + "\"> " + 
							"y: <input type=\"number\" step=\"any\" name=\"pos_y\" value=\"" + toString(chatbot->pos.y) + "\"> " + 
							"z: <input type=\"number\" step=\"any\" name=\"pos_z\" value=\"" + toString(chatbot->pos.z) + "\"> " + 
							"<div class=\"field-description\">Eye position of chatbot avatar.  Z=up.</div></div>";

						page += "<br/>";
						page += "<div>Heading:<br/> <input type=\"number\" step=\"any\" name=\"heading\" value=\"" + toString(chatbot->heading) + "\">" + 
							"<div class=\"field-description\">Default facing direction.  Counter-clockwise rotation in radians from looking in positive x direction.  0 = look east.   1.57 = look north.  3.14 = look west.  4.71 = look south. </div></div>";

						

						page += "<p>Built-in prompt part:</p>";
						page += "<p>" + web::Escaping::HTMLEscape(world_state.server_config.shared_LLM_prompt_part) + "</p>";

						page += "<div class=\"form-field\">";
						page += "<label for=\"base_prompt\">Custom prompt part:</label><br/>";
						page += "<textarea rows=\"20\" class=\"full-width\" id=\"base_prompt\" name=\"base_prompt\">" + web::Escaping::HTMLEscape(chatbot->custom_prompt_part) + "</textarea>";
						page += "<div class=\"field-description\">Max 10,000 characters</div>";
						page += "</div>";

						page += "<input type=\"submit\" value=\"Save Changes\">";
						page += "</form>";
						page += "</div>"; // End grouped-region div


						page += "<h3>Tool Info Functions</h3>";
						page += "Use these to allow the LLM to access more information when it needs to.";



						for(auto it = chatbot->info_tool_functions.begin(); it != chatbot->info_tool_functions.end(); ++it)
						{
							const ChatBotToolFunction* func = it->second.ptr();
							
							page += "<div class=\"grouped-region\">";
							page += "<form action=\"/update_info_tool_function_post\" method=\"post\" class=\"full-width\">";
							page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
							page += "<input type=\"hidden\" name=\"cur_function_name\" value=\"" + web::Escaping::HTMLEscape(func->function_name) + "\">";
							
							page += "<div class=\"form-field\">";
							page += "Function name:<br/>";
							page += "<input type=\"text\" name=\"new_function_name\" value=\"" + web::Escaping::HTMLEscape(func->function_name) + "\"><br>";
							page += "</div>";
	
							page += "<div class=\"form-field\">";
							page += "Description:<br/>";
							page += "<textarea rows=\"2\" class=\"full-width\" name=\"description\">" + web::Escaping::HTMLEscape(func->description) + "</textarea>";
							page += "</div>";

							page += "<div class=\"form-field\">";
							page += "Result content:<br/>";
							page += "<textarea rows=\"10\" class=\"full-width\" name=\"result_content\">" + web::Escaping::HTMLEscape(func->result_content) + "</textarea>";
							page += "<div class=\"field-description\">Max 100,000 characters</div>";
							page += "</div>";

							page += "<input type=\"submit\" value=\"Save Changes to Function\">";
							page += "</form>";

							// Add 'delete function' button
							page += "<form action=\"/delete_info_tool_function_post\" method=\"post\">";
							page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
							page += "<input type=\"hidden\" name=\"function_name\" value=\"" + web::Escaping::HTMLEscape(func->function_name) + "\">";
							page += "<input type=\"submit\" value=\"Delete function\">";
							page += "</form>";
							page += "</div>"; // End grouped-region div
						}

						// Add 'add new function' button
						page += "<form action=\"/add_new_info_tool_function_post\" method=\"post\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "Function Name: <input type=\"text\" name=\"function_name\" value=\"\"><br>";
						page += "<input type=\"submit\" value=\"Add new function\">";
						page += "</form>";


						page += "<h3>Avatar + Animation</h3>";

						WorldMaterial default_mat;
						const WorldMaterial* mat0 = &default_mat;
						if(!chatbot->avatar_settings.materials.empty() && chatbot->avatar_settings.materials[0].nonNull())
							mat0 = chatbot->avatar_settings.materials[0].ptr();

						page += "<div class=\"grouped-region\">";
						page += "<div>Avatar model URL (current): <code>" + web::Escaping::HTMLEscape(toString(chatbot->avatar_settings.model_url)) + "</code></div>";

						page += "<form action=\"/copy_user_avatar_settings_post\" method=\"post\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "<input type=\"submit\" value=\"Copy user's current avatar settings for ChatBot\">";
						page += "</form>";

						page += "<hr/>";
						page += "<form action=\"/edit_chatbot_avatar_animation_post\" method=\"post\" class=\"full-width\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";

						page += "<div class=\"form-field\">";
						page += "<label>Avatar model URL</label><br/>";
						page += "<input type=\"text\" name=\"avatar_model_url\" value=\"" + web::Escaping::HTMLEscape(toString(chatbot->avatar_settings.model_url)) + "\">";
						page += "<div class=\"field-description\">URL to avatar model (.glb/.bmesh/.vox etc).</div>";
						page += "</div>";

						page += "<div class=\"form-field\">";
						page += "<label>Avatar pre-transform matrix (16 floats)</label><br/>";
						page += "<textarea rows=\"5\" class=\"full-width\" name=\"avatar_pre_transform\">" + web::Escaping::HTMLEscape(matrixToEditableText(chatbot->avatar_settings.pre_ob_to_world_matrix)) + "</textarea>";
						page += "<div class=\"field-description\">Used for avatar pivot/orientation fix. Keep identity for default behavior.</div>";
						page += "</div>";

						page += "<div class=\"form-field\">";
						page += "<label>Materials edit mode</label><br/>";
						page += "<select name=\"material_mode\">";
						page += "<option value=\"keep_existing\" selected>Keep existing materials</option>";
						page += "<option value=\"single_material\">Replace with single material (slot 0)</option>";
						page += "<option value=\"clear_all\">Clear all materials</option>";
						page += "</select>";
						page += "</div>";

						page += "<div class=\"form-field\">";
						page += "<label>Material 0 fields (used for single-material mode)</label><br/>";
						page += "Colour RGB: ";
						page += "<input type=\"number\" step=\"any\" name=\"mat0_colour_r\" value=\"" + toString(mat0->colour_rgb.r) + "\"> ";
						page += "<input type=\"number\" step=\"any\" name=\"mat0_colour_g\" value=\"" + toString(mat0->colour_rgb.g) + "\"> ";
						page += "<input type=\"number\" step=\"any\" name=\"mat0_colour_b\" value=\"" + toString(mat0->colour_rgb.b) + "\"><br/>";
						page += "Colour texture URL: <input type=\"text\" name=\"mat0_colour_texture_url\" value=\"" + web::Escaping::HTMLEscape(toString(mat0->colour_texture_url)) + "\"><br/>";
						page += "Normal map URL: <input type=\"text\" name=\"mat0_normal_map_url\" value=\"" + web::Escaping::HTMLEscape(toString(mat0->normal_map_url)) + "\"><br/>";
						page += "Emission texture URL: <input type=\"text\" name=\"mat0_emission_texture_url\" value=\"" + web::Escaping::HTMLEscape(toString(mat0->emission_texture_url)) + "\"><br/>";
						page += "Roughness: <input type=\"number\" step=\"any\" name=\"mat0_roughness\" value=\"" + toString(mat0->roughness.val) + "\"> ";
						page += "Metallic: <input type=\"number\" step=\"any\" name=\"mat0_metallic\" value=\"" + toString(mat0->metallic_fraction.val) + "\"> ";
						page += "Opacity: <input type=\"number\" step=\"any\" name=\"mat0_opacity\" value=\"" + toString(mat0->opacity.val) + "\"><br/>";
						page += "Emission luminance/flux: <input type=\"number\" step=\"any\" name=\"mat0_emission_lum\" value=\"" + toString(mat0->emission_lum_flux_or_lum) + "\"> ";
						page += "Flags: <input type=\"number\" name=\"mat0_flags\" value=\"" + toString((int)mat0->flags) + "\">";
						page += "</div>";

						const bool greeting_animate_head = BitUtils::isBitSet(chatbot->greeting_gesture_flags, SingleGestureSettings::FLAG_ANIMATE_HEAD);
						const bool greeting_loop = BitUtils::isBitSet(chatbot->greeting_gesture_flags, SingleGestureSettings::FLAG_LOOP);
						const bool idle_animate_head = BitUtils::isBitSet(chatbot->idle_gesture_flags, SingleGestureSettings::FLAG_ANIMATE_HEAD);
						const bool idle_loop = BitUtils::isBitSet(chatbot->idle_gesture_flags, SingleGestureSettings::FLAG_LOOP);
						const bool reactive_animate_head = BitUtils::isBitSet(chatbot->reactive_gesture_flags, SingleGestureSettings::FLAG_ANIMATE_HEAD);
						const bool reactive_loop = BitUtils::isBitSet(chatbot->reactive_gesture_flags, SingleGestureSettings::FLAG_LOOP);

						page += "<h4>Animation profile</h4>";
						page += "<div class=\"form-field\">";
						page += "<b>Greeting animation</b><br/>";
						page += "Name: <input type=\"text\" name=\"greeting_name\" value=\"" + web::Escaping::HTMLEscape(chatbot->greeting_gesture_name) + "\"> ";
						page += "URL: <input type=\"text\" name=\"greeting_url\" value=\"" + web::Escaping::HTMLEscape(toString(chatbot->greeting_gesture_URL)) + "\"><br/>";
						page += "<label><input type=\"checkbox\" name=\"greeting_animate_head\" value=\"1\"" + std::string(greeting_animate_head ? " checked" : "") + "> animate head</label> ";
						page += "<label><input type=\"checkbox\" name=\"greeting_loop\" value=\"1\"" + std::string(greeting_loop ? " checked" : "") + "> loop</label> ";
						page += "Cooldown (s): <input type=\"number\" step=\"any\" name=\"greeting_cooldown_s\" value=\"" + toString(chatbot->greeting_gesture_cooldown_s) + "\">";
						page += "</div>";

						page += "<div class=\"form-field\">";
						page += "<b>Idle animation</b><br/>";
						page += "Name: <input type=\"text\" name=\"idle_name\" value=\"" + web::Escaping::HTMLEscape(chatbot->idle_gesture_name) + "\"> ";
						page += "URL: <input type=\"text\" name=\"idle_url\" value=\"" + web::Escaping::HTMLEscape(toString(chatbot->idle_gesture_URL)) + "\"><br/>";
						page += "<label><input type=\"checkbox\" name=\"idle_animate_head\" value=\"1\"" + std::string(idle_animate_head ? " checked" : "") + "> animate head</label> ";
						page += "<label><input type=\"checkbox\" name=\"idle_loop\" value=\"1\"" + std::string(idle_loop ? " checked" : "") + "> loop</label> ";
						page += "Interval (s): <input type=\"number\" step=\"any\" name=\"idle_interval_s\" value=\"" + toString(chatbot->idle_gesture_interval_s) + "\">";
						page += "</div>";

						page += "<div class=\"form-field\">";
						page += "<b>Reactive animation</b><br/>";
						page += "Name: <input type=\"text\" name=\"reactive_name\" value=\"" + web::Escaping::HTMLEscape(chatbot->reactive_gesture_name) + "\"> ";
						page += "URL: <input type=\"text\" name=\"reactive_url\" value=\"" + web::Escaping::HTMLEscape(toString(chatbot->reactive_gesture_URL)) + "\"><br/>";
						page += "<label><input type=\"checkbox\" name=\"reactive_animate_head\" value=\"1\"" + std::string(reactive_animate_head ? " checked" : "") + "> animate head</label> ";
						page += "<label><input type=\"checkbox\" name=\"reactive_loop\" value=\"1\"" + std::string(reactive_loop ? " checked" : "") + "> loop</label> ";
						page += "Cooldown (s): <input type=\"number\" step=\"any\" name=\"reactive_cooldown_s\" value=\"" + toString(chatbot->reactive_gesture_cooldown_s) + "\">";
						page += "</div>";

						page += "<input type=\"submit\" value=\"Save avatar + animation settings\">";
						page += "</form>";

						page += "<hr/>";
						page += "<h4>Test animation playback</h4>";
						page += "<form action=\"/play_chatbot_animation_post\" method=\"post\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "<input type=\"hidden\" name=\"gesture_slot\" value=\"greeting\">";
						page += "<input type=\"submit\" value=\"Play greeting animation\">";
						page += "</form>";
						page += "<form action=\"/play_chatbot_animation_post\" method=\"post\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "<input type=\"hidden\" name=\"gesture_slot\" value=\"idle\">";
						page += "<input type=\"submit\" value=\"Play idle animation\">";
						page += "</form>";
						page += "<form action=\"/play_chatbot_animation_post\" method=\"post\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "<input type=\"hidden\" name=\"gesture_slot\" value=\"reactive\">";
						page += "<input type=\"submit\" value=\"Play reactive animation\">";
						page += "</form>";

						page += "<form action=\"/play_chatbot_animation_post\" method=\"post\" class=\"full-width\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "<input type=\"hidden\" name=\"gesture_slot\" value=\"custom\">";
						page += "Custom name: <input type=\"text\" name=\"custom_name\" value=\"\"> ";
						page += "Custom URL: <input type=\"text\" name=\"custom_url\" value=\"\"><br/>";
						page += "<label><input type=\"checkbox\" name=\"custom_animate_head\" value=\"1\"> animate head</label> ";
						page += "<label><input type=\"checkbox\" name=\"custom_loop\" value=\"1\"> loop</label> ";
						page += "<input type=\"submit\" value=\"Play custom animation\">";
						page += "</form>";

						page += "</div>"; // End grouped-region div


						page += "<h3>Misc Settings</h3>";
						page += "<div>World: " + (world_it->first.empty() ? "Main World" : web::Escaping::HTMLEscape(world_it->first)) + "</div>";
						page += "<div>Chatbot created: " + chatbot->created_time.dayString() + "</div>";

						page += "<h3>Delete ChatBot</h3>";
						page += "<div class=\"danger-zone\">";
						page += "<form action=\"/delete_chatbot_post\" method=\"post\">";
						page += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot_id) + "\">";
						page += "<input type=\"submit\" class=\"delete-chatbot\" value=\"Delete Chatbot\">";
						page += "</form>";
						page += "</div>"; // End grouped-region div
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to view this page.");
				}
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		page += "<script src=\"/files/chatbot.js\"></script>";

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleChatBotPageRequest error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderNewChatBotPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "New ChatBot");
		page += "<div class=\"main\">   \n";

		const std::string world_name_for_form = normaliseWorldNameField(request.getURLParam("world").str());

		Vec3d pos = Vec3d(0.0, 0.0, 1.67);
		if(request.isURLParamPresent("pos_x"))
		{
			pos.x = request.getURLDoubleParam("pos_x");
			pos.y = request.getURLDoubleParam("pos_y");
			pos.z = request.getURLDoubleParam("pos_z");
		}

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");


			const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
			if(!msg.empty())
				page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";


			page += "<div class=\"grouped-region\">";
			page += "<form action=\"/create_new_chatbot_post\" method=\"post\" id=\"usrform\" class=\"full-width\" >";


			page += "<div class=\"form-field\">";
			page += "<label for=\"world\">World:  (leave empty to create in main world)</label><br/>";
			page += "<input type=\"text\" id=\"world\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_name_for_form) + "\">";
			page += "</div>";

			page += std::string("<div>Position:<br/> ") + 
				"x: <input type=\"number\" step=\"any\" name=\"pos_x\" value=\"" + toString(pos.x) + "\"> " + 
				"y: <input type=\"number\" step=\"any\" name=\"pos_y\" value=\"" + toString(pos.y) + "\"> " + 
				"z: <input type=\"number\" step=\"any\" name=\"pos_z\" value=\"" + toString(pos.z) + "\"> </div>";

				
			page += "<input type=\"submit\" value=\"Create ChatBot\">";
			page += "</form>";
			page += "</div>"; // End grouped-region div

		} // End lock scope

		page += "</div>   \n"; // end main div
		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("renderNewChatBotPage error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// Adapted from workerthread
static bool positionIsInParcelForWhichLoggedInUserHasWritePerms(const Vec3d& pos, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock)
{
	assert(user_id.valid());

	const Vec4f ob_pos = pos.toVec4fPoint();

	ServerWorldState::ParcelMapType& parcels = world_state.getParcels(lock);
	for(ServerWorldState::ParcelMapType::iterator it = parcels.begin(); it != parcels.end(); ++it)
	{
		const Parcel* parcel = it->second.ptr();
		if(parcel->pointInParcel(ob_pos) && parcel->userHasWritePerms(user_id))
			return true;
	}

	return false;
}


void handleNewChatBotPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name = normaliseWorldNameField(request.getPostField("world").str());

		Vec3d pos;
		pos.x = request.getPostDoubleField("pos_x");
		pos.y = request.getPostDoubleField("pos_y");
		pos.z = request.getPostDoubleField("pos_z");

		bool successfully_created = false;
		uint64 chatbot_id = 0;

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);


			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			// Look up the world
			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
			{
				world_state.setUserWebMessage(logged_in_user->id, "Could not find the world '" + world_name + "'");
			}
			else
			{
				ServerWorldState* world = res->second.ptr();

				// Check the new position is valid - must be in a world or parcel owned by user.
				if((world->details.owner_id == logged_in_user->id) || // If the user owns this world, or
					positionIsInParcelForWhichLoggedInUserHasWritePerms(pos, logged_in_user->id, *world, lock)) // chatbot is placed in a parcel the user has write permissions for:
				{
					// Create the ChatBot
					ChatBotRef chatbot = new ChatBot();
					chatbot->id = world_state.getNextChatBotUID();
					chatbot->owner_id = logged_in_user->id;
					chatbot->created_time = TimeStamp::currentTime();
					chatbot->name = "New ChatBot";
					chatbot->pos = pos;
					chatbot->heading = 0;
					chatbot->world = world;

					// Insert into world
					world->getChatBots(lock)[chatbot->id] = chatbot;


					// Create avatar for the chatbot
					AvatarRef avatar = world->createAndInsertAvatarForChatBot(&world_state, chatbot.ptr(), lock);
					chatbot->avatar_uid = avatar->uid;
					chatbot->avatar = avatar;


					world->addChatBotAsDBDirty(chatbot, lock);

					world_state.markAsChanged();

					world_state.setUserWebMessage(logged_in_user->id, "Created chatbot.");

					successfully_created = true;
					chatbot_id = chatbot->id;
				}
				else
				{
					world_state.setUserWebMessage(logged_in_user->id, "Position must be in a parcel you have write permission for, or in a world you own.");
				}
			}
		} // End lock scope

		if(successfully_created)
			web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id)); // redirect to edit page.
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/new_chatbot?world=" + web::Escaping::URLEscape(world_name) + 
				"&pos_x=" + toString(pos.x) + "&pos_y=" + toString(pos.y) + "&pos_z=" + toString(pos.z));  // redirect back to new chatbot page.
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleNewChatBotPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleEditChatBotPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");
		const web::UnsafeString new_name = request.getPostField("name");
		const web::UnsafeString new_base_prompt = request.getPostField("base_prompt");

		Vec3d new_pos;
		new_pos.x = request.getPostDoubleField("pos_x");
		new_pos.y = request.getPostDoubleField("pos_y");
		new_pos.z = request.getPostDoubleField("pos_z");

		const double new_heading = request.getPostDoubleField("heading");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);


			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");


			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						// Check the new position is valid - must be in a world or parcel owned by user.
						if((world->details.owner_id == logged_in_user->id) || // If the user owns this world, or
							positionIsInParcelForWhichLoggedInUserHasWritePerms(new_pos, logged_in_user->id, *world, lock)) // chatbot is placed in a parcel the user has write permissions for:
						{
							chatbot->name = new_name.str();
							if(chatbot->name.size() > ChatBot::MAX_NAME_SIZE)
							{
								chatbot->name = chatbot->name.substr(0, ChatBot::MAX_NAME_SIZE);
								world_state.setUserWebMessage(logged_in_user->id, "Name exceeded max length of " + toString(ChatBot::MAX_NAME_SIZE) + " chars, truncated.");
							}

							chatbot->custom_prompt_part = new_base_prompt.str();
							if(chatbot->custom_prompt_part.size() > ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE)
							{
								chatbot->custom_prompt_part = chatbot->custom_prompt_part.substr(0, ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE);
								world_state.setUserWebMessage(logged_in_user->id, "Prompt exceeded max length of " + toString(ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE) + " chars, truncated.");
							}

							chatbot->pos = new_pos;
							chatbot->heading = (float)new_heading;


							// Update the avatar's state
							if(chatbot->avatar)
							{
								chatbot->avatar->name = chatbot->name;
								chatbot->avatar->pos = new_pos;
								chatbot->avatar->rotation = Vec3f(0, Maths::pi_2<float>(), chatbot->heading);
								chatbot->avatar->other_dirty = true;
							}


							world->addChatBotAsDBDirty(chatbot, lock);

							world_state.markAsChanged();

							world_state.setUserWebMessage(logged_in_user->id, "Updated chatbot.");
						}
						else
						{
							world_state.setUserWebMessage(logged_in_user->id, "Position must be in a parcel you have write permission for, or in a world you own.");
						}
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to edit it.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditChatBotPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteChatBotPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");
		
		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						// Remove avatar
						if(chatbot->avatar)
						{
							// Mark as dead so it will be removed in server.cpp main loop.
							chatbot->avatar->state = Avatar::State_Dead;
							chatbot->avatar->other_dirty = true;
						}


						// Remove from dirty-set, so it's not updated in DB.
						world->getDBDirtyChatBots(lock).erase(chatbot);

						// Add DB record to list of records to be deleted.
						world_state.db_records_to_delete.insert(chatbot->database_key);

						world->getChatBots(lock).erase(chatbot->id);

						world_state.markAsChanged();

						world_state.setUserWebMessage(logged_in_user->id, "Deleted chatbot.");
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to edit it.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/account"); // Redirect back to user account page.
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteChatBotPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleCopyUserAvatarSettingsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						chatbot->avatar_settings = logged_in_user->avatar_settings; // Copy the avatar settings

						// Update the avatar's state
						if(chatbot->avatar)
						{
							chatbot->avatar->avatar_settings = chatbot->avatar_settings;
							chatbot->avatar->other_dirty = true;
						}

						world->addChatBotAsDBDirty(chatbot, lock);
						world_state.markAsChanged();
						world_state.setUserWebMessage(logged_in_user->id, "Updated chatbot avatar settings.");
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to edit it.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleCopyUserAvatarSettingsPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleEditChatBotAvatarAnimationPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");
		const std::string avatar_model_url = stripHeadAndTailWhitespace(request.getPostField("avatar_model_url").str());
		const std::string avatar_pre_transform = request.getPostField("avatar_pre_transform").str();
		const std::string material_mode = stripHeadAndTailWhitespace(request.getPostField("material_mode").str());

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			bool found = false;

			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res == world->getChatBots(lock).end())
					continue;

				found = true;
				ChatBot* chatbot = res->second.ptr();
				if((chatbot->owner_id != logged_in_user->id) && !isGodUser(logged_in_user->id))
					throw glare::Exception("You must be the owner of this chatbot to edit it.");

				{
					std::string model_url = avatar_model_url;
					if(model_url.size() > 10000)
						model_url.resize(10000);
					chatbot->avatar_settings.model_url = toURLString(model_url);
				}

				const std::string matrix_text = stripHeadAndTailWhitespace(avatar_pre_transform);
				if(!matrix_text.empty())
				{
					Matrix4f parsed_matrix;
					if(parseMatrixFromEditableText(matrix_text, parsed_matrix))
					{
						chatbot->avatar_settings.pre_ob_to_world_matrix = parsed_matrix;
					}
					else
					{
						world_state.setUserWebMessage(logged_in_user->id, "Avatar transform not updated: expected exactly 16 float values.");
					}
				}

				if(material_mode == "clear_all")
				{
					chatbot->avatar_settings.materials.clear();
				}
				else if(material_mode == "single_material")
				{
					WorldMaterialRef mat = new WorldMaterial();
					mat->colour_rgb.r = clampFloat(parseFloatWithDefault(request, "mat0_colour_r", mat->colour_rgb.r), 0.f, 4.f);
					mat->colour_rgb.g = clampFloat(parseFloatWithDefault(request, "mat0_colour_g", mat->colour_rgb.g), 0.f, 4.f);
					mat->colour_rgb.b = clampFloat(parseFloatWithDefault(request, "mat0_colour_b", mat->colour_rgb.b), 0.f, 4.f);
					mat->colour_texture_url = toURLString(stripHeadAndTailWhitespace(request.getPostField("mat0_colour_texture_url").str()));
					mat->normal_map_url = toURLString(stripHeadAndTailWhitespace(request.getPostField("mat0_normal_map_url").str()));
					mat->emission_texture_url = toURLString(stripHeadAndTailWhitespace(request.getPostField("mat0_emission_texture_url").str()));
					mat->roughness.val = clampFloat(parseFloatWithDefault(request, "mat0_roughness", mat->roughness.val), 0.f, 1.f);
					mat->metallic_fraction.val = clampFloat(parseFloatWithDefault(request, "mat0_metallic", mat->metallic_fraction.val), 0.f, 1.f);
					mat->opacity.val = clampFloat(parseFloatWithDefault(request, "mat0_opacity", mat->opacity.val), 0.f, 1.f);
					mat->emission_lum_flux_or_lum = clampFloat(parseFloatWithDefault(request, "mat0_emission_lum", mat->emission_lum_flux_or_lum), 0.f, 1.0e7f);
					mat->flags = (uint32)std::max(0, parseIntWithDefault(request, "mat0_flags", (int)mat->flags));

					chatbot->avatar_settings.materials.resize(1);
					chatbot->avatar_settings.materials[0] = mat;
				}

				chatbot->greeting_gesture_name = stripHeadAndTailWhitespace(request.getPostField("greeting_name").str());
				chatbot->idle_gesture_name = stripHeadAndTailWhitespace(request.getPostField("idle_name").str());
				chatbot->reactive_gesture_name = stripHeadAndTailWhitespace(request.getPostField("reactive_name").str());

				if(chatbot->greeting_gesture_name.size() > ChatBot::MAX_GESTURE_NAME_SIZE)
					chatbot->greeting_gesture_name.resize(ChatBot::MAX_GESTURE_NAME_SIZE);
				if(chatbot->idle_gesture_name.size() > ChatBot::MAX_GESTURE_NAME_SIZE)
					chatbot->idle_gesture_name.resize(ChatBot::MAX_GESTURE_NAME_SIZE);
				if(chatbot->reactive_gesture_name.size() > ChatBot::MAX_GESTURE_NAME_SIZE)
					chatbot->reactive_gesture_name.resize(ChatBot::MAX_GESTURE_NAME_SIZE);

				std::string greeting_url = stripHeadAndTailWhitespace(request.getPostField("greeting_url").str());
				std::string idle_url = stripHeadAndTailWhitespace(request.getPostField("idle_url").str());
				std::string reactive_url = stripHeadAndTailWhitespace(request.getPostField("reactive_url").str());

				if(greeting_url.size() > ChatBot::MAX_GESTURE_URL_SIZE)
					greeting_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);
				if(idle_url.size() > ChatBot::MAX_GESTURE_URL_SIZE)
					idle_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);
				if(reactive_url.size() > ChatBot::MAX_GESTURE_URL_SIZE)
					reactive_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);

				chatbot->greeting_gesture_URL = toURLString(greeting_url);
				chatbot->idle_gesture_URL = toURLString(idle_url);
				chatbot->reactive_gesture_URL = toURLString(reactive_url);

				chatbot->greeting_gesture_flags = 0;
				if(postCheckboxIsChecked(request, "greeting_animate_head"))
					chatbot->greeting_gesture_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
				if(postCheckboxIsChecked(request, "greeting_loop"))
					chatbot->greeting_gesture_flags |= SingleGestureSettings::FLAG_LOOP;

				chatbot->idle_gesture_flags = 0;
				if(postCheckboxIsChecked(request, "idle_animate_head"))
					chatbot->idle_gesture_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
				if(postCheckboxIsChecked(request, "idle_loop"))
					chatbot->idle_gesture_flags |= SingleGestureSettings::FLAG_LOOP;

				chatbot->reactive_gesture_flags = 0;
				if(postCheckboxIsChecked(request, "reactive_animate_head"))
					chatbot->reactive_gesture_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
				if(postCheckboxIsChecked(request, "reactive_loop"))
					chatbot->reactive_gesture_flags |= SingleGestureSettings::FLAG_LOOP;

				chatbot->greeting_gesture_cooldown_s = clampFloat(parseFloatWithDefault(request, "greeting_cooldown_s", chatbot->greeting_gesture_cooldown_s), 0.f, 3600.f);
				chatbot->idle_gesture_interval_s = clampFloat(parseFloatWithDefault(request, "idle_interval_s", chatbot->idle_gesture_interval_s), 0.f, 3600.f);
				chatbot->reactive_gesture_cooldown_s = clampFloat(parseFloatWithDefault(request, "reactive_cooldown_s", chatbot->reactive_gesture_cooldown_s), 0.f, 3600.f);

				// Update avatar state immediately for connected clients.
				if(chatbot->avatar)
				{
					chatbot->avatar->avatar_settings = chatbot->avatar_settings;
					chatbot->avatar->other_dirty = true;
				}

				world->addChatBotAsDBDirty(chatbot, lock);
				world_state.markAsChanged();
				world_state.setUserWebMessage(logged_in_user->id, "Updated chatbot avatar + animation settings.");
				break;
			}

			if(!found)
				world_state.setUserWebMessage(logged_in_user->id, "ChatBot not found.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditChatBotAvatarAnimationPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handlePlayChatBotAnimationPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const int chatbot_id = request.getPostIntField("chatbot_id");
		const std::string gesture_slot = stripHeadAndTailWhitespace(request.getPostField("gesture_slot").str());

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			bool found = false;
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res == world->getChatBots(lock).end())
					continue;

				found = true;
				ChatBot* chatbot = res->second.ptr();
				if((chatbot->owner_id != logged_in_user->id) && !isGodUser(logged_in_user->id))
					throw glare::Exception("You must be the owner of this chatbot to edit it.");

				std::string gesture_name;
				URLString gesture_url;
				uint32 gesture_flags = 0;

				if(gesture_slot == "greeting")
				{
					gesture_name = chatbot->greeting_gesture_name;
					gesture_url = chatbot->greeting_gesture_URL;
					gesture_flags = chatbot->greeting_gesture_flags;
				}
				else if(gesture_slot == "idle")
				{
					gesture_name = chatbot->idle_gesture_name;
					gesture_url = chatbot->idle_gesture_URL;
					gesture_flags = chatbot->idle_gesture_flags;
				}
				else if(gesture_slot == "reactive")
				{
					gesture_name = chatbot->reactive_gesture_name;
					gesture_url = chatbot->reactive_gesture_URL;
					gesture_flags = chatbot->reactive_gesture_flags;
				}
				else // custom
				{
					gesture_name = stripHeadAndTailWhitespace(request.getPostField("custom_name").str());
					std::string custom_url = stripHeadAndTailWhitespace(request.getPostField("custom_url").str());
					if(custom_url.size() > ChatBot::MAX_GESTURE_URL_SIZE)
						custom_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);
					gesture_url = toURLString(custom_url);

					if(postCheckboxIsChecked(request, "custom_animate_head"))
						gesture_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
					if(postCheckboxIsChecked(request, "custom_loop"))
						gesture_flags |= SingleGestureSettings::FLAG_LOOP;
				}

				if(gesture_name.empty() && gesture_url.empty())
				{
					world_state.setUserWebMessage(logged_in_user->id, "Animation is empty: set name or URL first.");
				}
				else
				{
					chatbot->queueManualGesturePlayback(gesture_name, gesture_url, gesture_flags);
					world_state.setUserWebMessage(logged_in_user->id, "Queued chatbot animation playback.");
				}
				break;
			}

			if(!found)
				world_state.setUserWebMessage(logged_in_user->id, "ChatBot not found.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handlePlayChatBotAnimationPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleUpdateInfoToolFunctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");
		const web::UnsafeString cur_function_name = request.getPostField("cur_function_name");
		const web::UnsafeString new_function_name = request.getPostField("new_function_name");
		const web::UnsafeString description = request.getPostField("description");
		const web::UnsafeString result_content = request.getPostField("result_content");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						auto func_res = chatbot->info_tool_functions.find(cur_function_name.str());
						if(func_res == chatbot->info_tool_functions.end())
							throw glare::Exception("Couldn't find function");

						Reference<ChatBotToolFunction> func = func_res->second;

						chatbot->info_tool_functions.erase(cur_function_name.str()); // Remove from map as name will change

						func->function_name = new_function_name.str();
						func->description = description.str();
						func->result_content = result_content.str();

						chatbot->info_tool_functions[func->function_name] = func; // re-insert into map using new name.


						world->addChatBotAsDBDirty(chatbot, lock);
						world_state.markAsChanged();
						world_state.setUserWebMessage(logged_in_user->id, "Updated info tool function.");
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to edit it.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleUpdateInfoToolFunctionPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteInfoToolFunctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");
		const web::UnsafeString function_name = request.getPostField("function_name");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						chatbot->info_tool_functions.erase(function_name.str()); // Remove from map

						world->addChatBotAsDBDirty(chatbot, lock);
						world_state.markAsChanged();
						world_state.setUserWebMessage(logged_in_user->id, "Deleted info tool function.");
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to edit it.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteInfoToolFunctionPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleAddNewInfoToolFunctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const int chatbot_id = request.getPostIntField("chatbot_id");
		const web::UnsafeString function_name = request.getPostField("function_name");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
				throw glare::Exception("You must be logged in to view this page.");

			// Lookup chatbot
			// Look through all worlds.  NOTE: slow
			for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			{
				ServerWorldState* world = it->second.ptr();
				const auto res = world->getChatBots(lock).find((uint64)chatbot_id);
				if(res != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = res->second.ptr();
					if((chatbot->owner_id == logged_in_user->id) || isGodUser(logged_in_user->id))
					{
						Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
						func->function_name = function_name.str();
						chatbot->info_tool_functions[func->function_name] = func;

						world->addChatBotAsDBDirty(chatbot, lock);
						world_state.markAsChanged();
						world_state.setUserWebMessage(logged_in_user->id, "Added new tool function.");
					}
					else
						throw glare::Exception("You must be the owner of this chatbot to edit it.");
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/edit_chatbot?chatbot_id=" + toString(chatbot_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleAddNewInfoToolFunctionPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace ChatBotHandlers
